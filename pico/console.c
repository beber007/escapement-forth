/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File console.c: The platform of the Forth on the Pico (forth_platform.h): the console on
** core 1, the output of both cores, the clock and the pins.
**
** Core 1 runs no kernel and takes no interrupt: it polls UART0, on GP0 (TX) and GP1 (RX),
** 115200 baud, 8N1, the pins and settings of Escapement's UART driver. What a slot prints
** on core 0 goes into a ring that core 1 empties between its own lines; full, the ring
** drops the bytes and counts them, since a task must not wait for the console. Slots of
** different periods preempt one another on core 0: each byte goes in with interrupts
** masked, a few instructions long.
**
** Platform version: RP2040 (Raspberry Pi Pico).
*/

#include "Escapement.h"
#include "forth.h"
#include "forth_platform.h"
#include "slots.h"
#include "console.h"
#include "restart.h"

#define UART0_BASE           0x40034000
#define UART_REG(offset)     *((volatile UINT32 *)(UART0_BASE + (offset)))
#define UART_DR              UART_REG(0x00)
#define UART_FR              UART_REG(0x18)
#define UART_IBRD            UART_REG(0x24)
#define UART_FBRD            UART_REG(0x28)
#define UART_LCR_H           UART_REG(0x2C)
#define UART_CR              UART_REG(0x30)
#define FR_TXFF              (1u << 5)
#define FR_RXFE              (1u << 4)
#define LCR_H_WLEN_8         (3u << 5)
#define LCR_H_FEN            (1u << 4)
#define CR_UARTEN            (1u << 0)
#define CR_TXE               (1u << 8)
#define CR_RXE               (1u << 9)

#define RESETS_CLR           *((volatile UINT32 *)(0x4000C000 + 0x3000))
#define RESETS_RESET_DONE    *((volatile UINT32 *)0x4000C008)
#define RESETS_UART0_BIT     (1u << 22)
#define RESETS_IO_BANK0_BIT  (1u << 5)
#define IO_BANK0_CTRL(p)     *((volatile UINT32 *)(0x40014000 + 0x04 + 8 * (p)))
#define FUNCSEL_UART         2
#define FUNCSEL_SIO          5

#define SIO_CPUID            *((volatile UINT32 *)(0xD0000000 + 0x00))
#define SIO_GPIO_IN          *((volatile UINT32 *)(0xD0000000 + 0x04))
#define SIO_GPIO_OUT_SET     *((volatile UINT32 *)(0xD0000000 + 0x14))
#define SIO_GPIO_OUT_CLR     *((volatile UINT32 *)(0xD0000000 + 0x18))
#define SIO_GPIO_OUT_XOR     *((volatile UINT32 *)(0xD0000000 + 0x1C))
#define SIO_GPIO_OE_SET      *((volatile UINT32 *)(0xD0000000 + 0x24))
#define TIMER_TIMERAWL       *((volatile UINT32 *)(0x40054000 + 0x28))

#define CLK_PERI_HZ          12000000u   /* as OSInitializeSystemClocks leaves it */
#define BAUD_RATE            115200u

/* GP0 and GP1 are the console's; GP23 to GP25 and GP29 the Pico W's wireless chip. */
#define FREE_PINS            0x1C7FFFFCu

#define RING                 256         /* a power of 2 */

/* All of it outlives a restart of core 0, which clears its .bss under core 1. */
static volatile UINT8 Ring[RING] FORTH_KEEP;
static volatile UINT32 RingHead FORTH_KEEP, RingTail FORTH_KEEP;   /* core 0's, core 1's */
static volatile UINT32 Dropped FORTH_KEEP;
static char Line[FORTH_LINE] FORTH_KEEP;

extern volatile uint32_t Overruns;

#ifdef SCRIPT
   /* make SCRIPT=file: core 1 types the file's lines at start, and keeps what the console
   ** prints in Log as well, for a probe to read with no UART wired (tools/smoke_board.sh). */
   volatile char Log[8192] FORTH_KEEP;
   volatile UINT32 LogLength FORTH_KEEP, ScriptDone FORTH_KEEP;
   static const char Script[] = {
      #include "script.inc"
      0
   };
#endif

static void Send(char c)
{
  #ifdef SCRIPT
     if (LogLength < sizeof Log)
        Log[LogLength++] = c;
  #endif
  while (UART_FR & FR_TXFF);
  UART_DR = (UINT8)c;
} /* end of Send */

/* Drain: What the slots printed, sent; core 0 watched meanwhile, and its restarts said. */
static void Drain(void)
{
  static const char *const Said[] = {"", "\n[core 0 restarted]\n",
                                     "\n[core 0 restarted, hard]\n",
                                     "\n[core 0 did not restart]\n"};
  UINT32 tail;
  int restart;
  if ((restart = Watch()) != 0)
     ForthPrintText(Said[restart]);
  tail = RingTail;
  while (tail != RingHead) {
     ForthBarrier();
     if (Ring[tail % RING] == '\n')
        Send('\r');
     Send((char)Ring[tail % RING]);
     tail += 1;
     ForthBarrier();
     RingTail = tail;
  }
} /* end of Drain */

void ForthEmit(char c)
{
  UINT32 primask, head;
  if (SIO_CPUID != 0) {
     if (c == '\n')
        Send('\r');
     Send(c);
     return;
  }
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
  head = RingHead;
  if (head - RingTail < RING) {
     Ring[head % RING] = (UINT8)c;
     ForthBarrier();
     RingHead = head + 1;
  }
  else
     Dropped += 1;
  __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
} /* end of ForthEmit */

uint32_t ForthNow(void)
{
  return TIMER_TIMERAWL;
} /* end of ForthNow */

void ForthBarrier(void)
{
  __asm volatile ("dmb" ::: "memory");
} /* end of ForthBarrier */


/* ---- The words of the board. Pins are given as masks: 1 15 lshift is GP15. */

static UINT32 Pins(Context *c)
{
  return (UINT32)FORTH_POP(c) & FREE_PINS;
} /* end of Pins */

/* OUTPUT ( mask -- ): The pins driven by the SIO, outputs. */
static void POutput(Context *c, const Word *w)
{
  UINT32 mask, p;
  (void)w;
  FORTH_NEED(c,1);
  mask = Pins(c);
  for (p = 0; p < 30; p += 1)
     if (mask & (1u << p))
        IO_BANK0_CTRL(p) = FUNCSEL_SIO;
  SIO_GPIO_OE_SET = mask;
} /* end of POutput */

static void PHigh(Context *c, const Word *w) { (void)w; FORTH_NEED(c,1); SIO_GPIO_OUT_SET = Pins(c); }
static void PLow(Context *c, const Word *w) { (void)w; FORTH_NEED(c,1); SIO_GPIO_OUT_CLR = Pins(c); }
static void PToggle(Context *c, const Word *w) { (void)w; FORTH_NEED(c,1); SIO_GPIO_OUT_XOR = Pins(c); }

static void PPins(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,SIO_GPIO_IN);
} /* end of PPins */

/* OVERRUNS ( -- n ), DROPPED ( -- n ): The overruns of the slots' tasks, the bytes
** the slots printed that the ring had no room for. */
static void POverruns(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,Overruns);
} /* end of POverruns */

/* RESTARTS ( -- soft hard ): The restarts of core 0 that brought its beat back. */
static void PRestarts(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,2);
  FORTH_PUSH(c,SoftRestarts);
  FORTH_PUSH(c,HardRestarts);
} /* end of PRestarts */

static void PDropped(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,Dropped);
} /* end of PDropped */

/* MS ( n -- ): Waits n milliseconds, at the console alone, sending what the slots print
** meanwhile: a slot that waited would hold its task. */
static void PMs(Context *c, const Word *w)
{
  UINT32 start = TIMER_TIMERAWL, wait;
  (void)w;
  if (!ForthOnConsole(c)) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  FORTH_NEED(c,1);
  wait = (UINT32)FORTH_POP(c) * 1000;
  while (TIMER_TIMERAWL - start < wait)
     Drain();
} /* end of PMs */

void ForthPlatformWords(void)
{
  ForthAddWord("output",POutput,0);
  ForthAddWord("high",PHigh,0);
  ForthAddWord("low",PLow,0);
  ForthAddWord("toggle",PToggle,0);
  ForthAddWord("pins",PPins,0);
  ForthAddWord("overruns",POverruns,0);
  ForthAddWord("dropped",PDropped,0);
  ForthAddWord("ms",PMs,0);
  ForthAddWord("restarts",PRestarts,0);
  SlotsWords();
} /* end of ForthPlatformWords */


/* ---- The console. */

static void InitUART(void)
{
  UINT32 divisor;
  /* Through the atomic clear alias, as Escapement's driver: core 0 may release other
  ** blocks meanwhile. */
  RESETS_CLR = RESETS_UART0_BIT | RESETS_IO_BANK0_BIT;
  while ((RESETS_RESET_DONE & (RESETS_UART0_BIT | RESETS_IO_BANK0_BIT)) !=
         (RESETS_UART0_BIT | RESETS_IO_BANK0_BIT));
  IO_BANK0_CTRL(0) = FUNCSEL_UART;
  IO_BANK0_CTRL(1) = FUNCSEL_UART;
  divisor = (8 * CLK_PERI_HZ / BAUD_RATE + 1) / 2;   /* in 64ths, rounded */
  UART_IBRD = divisor >> 6;
  UART_FBRD = divisor & 0x3F;
  UART_LCR_H = LCR_H_WLEN_8 | LCR_H_FEN;
  UART_CR = CR_UARTEN | CR_TXE | CR_RXE;
} /* end of InitUART */

void ConsoleInit(void)
{
  RingHead = RingTail = Dropped = 0;
  #ifdef SCRIPT
     LogLength = ScriptDone = 0;
  #endif
} /* end of ConsoleInit */

void Console(void)
{
  char *line = Line;
  UINT32 length = 0;
  char last = 0;
  WatchInit();
  InitUART();
  ForthPrintText("\nEscapement Forth\n");
  #ifdef SCRIPT
     {
        const char *p = Script;
        while (*p != 0) {
           for (length = 0; *p != 0 && *p != '\n' && length < FORTH_LINE - 1; length += 1)
              line[length] = *p++;
           if (*p == '\n')
              p += 1;
           line[length] = 0;
           ForthPrintText(line);
           Send(' ');
           ForthInterpret(line);
           Drain();
        }
        length = 0;
        ScriptDone = 1;
     }
  #endif
  while (TRUE) {
     char c;
     Drain();
     if (UART_FR & FR_RXFE)
        continue;
     c = (char)UART_DR;
     if (c == '\n' && last == '\r') {   /* the second half of CR LF */
        last = 0;
        continue;
     }
     last = c;
     if (c == '\r' || c == '\n') {
        line[length] = 0;
        Send(' ');
        ForthInterpret(line);
        length = 0;
     }
     else if (c == 0x08 || c == 0x7F) {
        if (length > 0) {
           length -= 1;
           ForthPrintText("\b \b");
        }
     }
     else if (length < FORTH_LINE - 1 && c >= ' ') {
        line[length++] = c;
        Send(c);
     }
  }
} /* end of Console */
