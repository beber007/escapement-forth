/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File console.c: The platform of the Forth on the Pico (forth_platform.h): the console on
** core 1, the output of both cores, the clock and the pins.
**
** Core 1 runs no kernel. It takes one interrupt, the timer's alarm 3 every millisecond,
** which watches core 0 (restart.c) and moves what UART0 received into a ring, a Ctrl-C a
** break of the word the console runs (ForthBreak): a word typed at the console that loops
** holds neither the watch nor the console. A fault on core 1 holds both: its HardFault loops,
** above the interrupt. UART0 is on GP0 (TX) and GP1 (RX), 115200 baud,
** 8N1, the pins and settings of Escapement's UART driver. What a slot prints
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
#define TIMER_ALARM3         *((volatile UINT32 *)(0x40054000 + 0x1C))
#define TIMER_INTR           *((volatile UINT32 *)(0x40054000 + 0x34))
#define TIMER_INTE_SET       *((volatile UINT32 *)(0x40054000 + 0x2000 + 0x38))
#define ALARM3_BIT           (1u << 3)
#define TIMER_IRQ_3          3
#define NVIC_ISER            *((volatile UINT32 *)0xE000E100)
#define VTOR                 *((volatile UINT32 *)0xE000ED08)
#define TICK                 1000        /* us between two of core 1's interrupts */
#define BREAK_CHAR           0x03        /* Ctrl-C */

#define CLK_PERI_HZ          12000000u   /* as OSInitializeSystemClocks leaves it */
#define BAUD_RATE            115200u

/* GP0 and GP1 are the console's; GP23 to GP25 and GP29 the Pico W's wireless chip. */
#define FREE_PINS            0x1C7FFFFCu

#define RING                 256         /* a power of 2 */
#define INPUT                64          /* a power of 2 */

/* All of it outlives a restart of core 0, which clears its .bss under core 1. */
static volatile UINT8 Ring[RING] FORTH_KEEP;
static volatile UINT32 RingHead FORTH_KEEP, RingTail FORTH_KEEP;   /* core 0's, core 1's */
static volatile UINT32 Dropped FORTH_KEEP;
static char Line[FORTH_LINE] FORTH_KEEP;
/* What core 1's interrupt received, and saw; its vector table. */
static volatile UINT8 Input[INPUT] FORTH_KEEP;
static volatile UINT32 InputHead FORTH_KEEP, InputTail FORTH_KEEP;
static volatile int Break FORTH_KEEP;
static volatile UINT32 Restarted[RESTART_FAILED + 1] FORTH_KEEP;   /* each outcome, counted */
static UINT32 Core1Table[48] FORTH_KEEP __attribute__((aligned(256)));

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

/* Drain: What the slots printed, sent, and the restarts of core 0 the interrupt saw. */
static void Drain(void)
{
  static const char *const Said[] = {"", "\n[core 0 restarted]\n",
                                     "\n[core 0 restarted, hard]\n",
                                     "\n[core 0 did not restart]\n"};
  UINT32 tail, primask;
  UINT32 seen;
  int i;
  for (i = RESTART_SOFT; i <= RESTART_FAILED; i += 1) {
     __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
     seen = Restarted[i];
     Restarted[i] = 0;
     __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
     while (seen-- > 0)
        ForthPrintText(Said[i]);
  }
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

/* Tick: Core 1's interrupt, alarm 3: core 0 watched, what UART0 received taken. */
static void Tick(void)
{
  UINT32 head;
  int restart;
  TIMER_INTR = ALARM3_BIT;
  TIMER_ALARM3 = TIMER_TIMERAWL + TICK;
  if ((restart = Watch()) != 0)
     Restarted[restart] += 1;
  while ((UART_FR & FR_RXFE) == 0) {
     char c = (char)UART_DR;
     /* A Ctrl-C into the ring too: the break holds for what runs until the console
     ** meets it there, which then clears it, before the lines that follow; one that
     ** comes with the end of a line before it, in the same tick, is for that line's word
     ** (a review, 2026-10-24). The ring full, it takes the place of the last byte, a
     ** line typed too fast lost anyway: a break the console never met would stop a line
     ** typed long after. */
     head = InputHead;
     if (c == BREAK_CHAR) {
        Break = 1;
        if (head - InputTail >= INPUT)
           head -= 1, InputHead = head;
     }
     if (head - InputTail < INPUT) {   /* full, the byte is lost: a line typed too fast */
        Input[head % INPUT] = (UINT8)c;
        InputHead = head + 1;
     }
  }
} /* end of Tick */

/* ForthMemory: The RP2040's map (datasheet 2.2): the ROM and the flash through the XIP
** read only, the SRAM but the guard of core 0's stack, the peripherals of APB and
** AHB-Lite, the SIO and the core's System Control Space, which takes words alone (a byte
** read there gave 0, on the board, 2026-10-24); past what each holds, or into the XIP's
** flash with no image of it, an access faults. */
int ForthMemory(uintptr_t address, size_t size, int write)
{
  static const struct { uintptr_t Start, End; int Write, Words; } Map[] = {
     {0x00000000, 0x00004000, 0, 0},   /* ROM */
     {0x10000000, 0x11000000, 0, 0},   /* XIP, the flash */
     {0x20000000, 0x20042000, 1, 0},   /* SRAM */
     {0x40000000, 0x40070000, 1, 0},   /* APB */
     {0x50000000, 0x50400000, 1, 0},   /* AHB-Lite */
     {0xD0000000, 0xD0000180, 1, 0},   /* SIO */
     {0xE000E000, 0xE000F000, 1, 1}};  /* the core's own, its SCS: words alone */
  extern UINT32 _OSStackGuard, _OSStackGuardEnd;
  size_t i;
  /* The guard at the bottom of core 0's stack, which its MPU forbids. */
  if (address < (uintptr_t)&_OSStackGuardEnd &&
      (address >= (uintptr_t)&_OSStackGuard || size > (uintptr_t)&_OSStackGuard - address))
     return 0;
  for (i = 0; i < sizeof Map / sizeof Map[0]; i += 1)
     if (address >= Map[i].Start && address <= Map[i].End && size <= Map[i].End - address)
        return (!write || Map[i].Write) && (!Map[i].Words || size == 4);
  return 0;
} /* end of ForthMemory */

/* Take: The next byte of the ring, which is not empty; a Ctrl-C's break cleared with
** it, at once: another the interrupt took meanwhile is for the lines after. */
static char Take(void)
{
  UINT32 primask;
  char c;
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
  c = (char)Input[InputTail % INPUT];
  InputTail += 1;
  if (c == BREAK_CHAR)
     Break = 0;
  __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
  return c;
} /* end of Take */

int ForthBreak(void)
{
  UINT32 primask;
  int asked;
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
  asked = Break;
  Break = 0;
  __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
  return asked;
} /* end of ForthBreak */

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

uint32_t ForthMask(void)
{
  uint32_t primask;
  __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (primask) :: "memory");
  return primask;
} /* end of ForthMask */

void ForthUnmask(uint32_t mask)
{
  __asm volatile ("msr primask, %0" :: "r" (mask) : "memory");
} /* end of ForthUnmask */


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

/* RESTARTS ( -- soft hard ): The restarts of core 0 that brought its beat back. STARTS
** ( -- n ): core 0's starts, the first included: one more than the restarts, unless one
** came that the watch did not ask for. */
static void PRestarts(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,2);
  FORTH_PUSH(c,SoftRestarts);
  FORTH_PUSH(c,HardRestarts);
} /* end of PRestarts */

static void PStarts(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,Starts);
} /* end of PStarts */

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
  if (FORTH_TOP(c) < 0 || FORTH_TOP(c) > 3600000) {   /* an hour at most */
     c->Error = FORTH_ARGUMENT;
     return;
  }
  wait = (UINT32)FORTH_POP(c) * 1000;
  while (TIMER_TIMERAWL - start < wait)
     if (ForthBreak()) {
        c->Error = FORTH_BREAK;
        return;
     }
     else
        Drain();
} /* end of PMs */

/* FAULT ( -- ): An undefined instruction, a HardFault on the core that runs it: what the
** restart of core 0 is tried with, @ and ! no longer faulting. */
static void PFault(Context *c, const Word *w)
{
  (void)c; (void)w;
  __asm volatile ("udf #0");
} /* end of PFault */

void ForthPlatformWords(void)
{
  ForthAddWord("fault",PFault,0);
  ForthAddWord("output",POutput,0);
  ForthAddWord("high",PHigh,0);
  ForthAddWord("low",PLow,0);
  ForthAddWord("toggle",PToggle,0);
  ForthAddWord("pins",PPins,0);
  ForthAddWord("overruns",POverruns,0);
  ForthAddWord("dropped",PDropped,0);
  ForthAddWord("ms",PMs,0);
  ForthAddWord("restarts",PRestarts,0);
  ForthAddWord("starts",PStarts,0);
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

/* StartTick: Core 1's interrupt set going, from its own vector table, in RAM. */
static void StartTick(void)
{
  extern void (* const CortexMxVectorTable[])(void);
  int i;
  for (i = 0; i < 48; i += 1)
     Core1Table[i] = (UINT32)CortexMxVectorTable[i];
  Core1Table[16 + TIMER_IRQ_3] = (UINT32)Tick;
  VTOR = (UINT32)Core1Table;
  TIMER_INTR = ALARM3_BIT;
  TIMER_ALARM3 = TIMER_TIMERAWL + TICK;
  TIMER_INTE_SET = ALARM3_BIT;
  NVIC_ISER = 1u << TIMER_IRQ_3;
  __asm volatile ("cpsie i" ::: "memory");
} /* end of StartTick */

void ConsoleInit(void)
{
  RingHead = RingTail = Dropped = 0;
  InputHead = InputTail = 0;
  Break = 0;
  Restarted[RESTART_SOFT] = Restarted[RESTART_HARD] = Restarted[RESTART_FAILED] = 0;
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
  StartTick();
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
     if (InputTail == InputHead)
        continue;
     c = Take();
     if (c == '\n' && last == '\r') {   /* the second half of CR LF */
        last = 0;
        continue;
     }
     if (c == BREAK_CHAR)   /* for the word before, done, or none: Take cleared it */
        continue;
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
