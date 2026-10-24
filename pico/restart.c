/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File restart.c: Core 1 restarts core 0, and with it the kernel, when the 1 ms slot's
** task stops beating: a word looped. The way of Escapement's issue #40, step 2
** (RestartPico.c on its branch forth-step1), where both restarts brought the beat back 10
** times out of 10 on a Pico W, in 3.0 and 8.0 ms of real time.
**
** - Soft: core 1 writes a word into the SIO FIFO, whose interrupt on core 0 rewrites the
**   PC of the frame it stacked so that its return lands in RestartEntry, in Thread mode.
**   That sets the stack pointer back to the top of the one stack and goes through
**   _OSResetHandler and main again. A loop with interrupts masked, or a fault, whose
**   handler loops, never takes it.
** - Hard, if no beat comes back within SOFT_WAIT us: core 1 forces the interrupt of the
**   timer's alarm 2, which the kernel leaves alone (TIMER_INTF), and core 0 takes it as its
**   NMI (SYSCFG_PROC0_NMI_MASK). RestartNMI writes the watchdog's SCRATCH4 to 7, the
**   bootrom's vector to RAM (RP2040 datasheet 2.8.1.1; pico-sdk, watchdog_reboot), and
**   resets its core (AIRCR.SYSRESETREQ); the bootrom jumps to RestartEntry.
**
** Either way the peripherals keep their state: RestartCore0 disarms the timer's alarms and
** clears their flags, clears the NVIC, and puts back .data, which an image in SRAM does
** not reload. What core 1 and the Forth keep sits in .noinit (FORTH_KEEP), which neither
** the loader nor _OSResetHandler touches.
**
** Platform version: RP2040 (Raspberry Pi Pico).
*/

#include "Escapement.h"
#include "forth.h"
#include "restart.h"

#define DETECT     3000      /* us without a beat before a restart */
#define SOFT_WAIT  5000      /* us for the soft one to bring it back */
#define HARD_WAIT  50000

#define TIMER_TIMERAWL    *((volatile UINT32 *)(0x40054000 + 0x28))
#define TIMER_ARMED       *((volatile UINT32 *)(0x40054000 + 0x20))
#define TIMER_INTR        *((volatile UINT32 *)(0x40054000 + 0x34))
#define TIMER_INTF        *((volatile UINT32 *)(0x40054000 + 0x3C))
#define ALARM2_BIT        (1u << 2)
#define WATCHDOG_SCRATCH  ((volatile UINT32 *)(0x40058000 + 0x0C))
#define SYSCFG_PROC0_NMI_MASK *((volatile UINT32 *)0x40004000)
#define AIRCR             *((volatile UINT32 *)0xE000ED0C)
#define RESETS_CLR        *((volatile UINT32 *)(0x4000C000 + 0x3000))
#define RESETS_RESET_DONE *((volatile UINT32 *)(0x4000C000 + 0x08))
#define RESETS_SYSCFG_BIT (1u << 18)
#define SIO_FIFO_ST       *((volatile UINT32 *)(0xD0000000 + 0x50))
#define SIO_FIFO_WR       *((volatile UINT32 *)(0xD0000000 + 0x54))
#define SIO_FIFO_RD       *((volatile UINT32 *)(0xD0000000 + 0x58))
#define SIO_FIFO_VLD      (1u << 0)
#define SIO_FIFO_RDY      (1u << 1)
#define NVIC_ISER         *((volatile UINT32 *)0xE000E100)
#define NVIC_ICER         *((volatile UINT32 *)0xE000E180)
#define NVIC_ICPR         *((volatile UINT32 *)0xE000E280)
#define NVIC_IPR          ((volatile UINT32 *)0xE000E400)
#define ICSR              *((volatile UINT32 *)0xE000ED04)
#define VTOR              *((volatile UINT32 *)0xE000ED08)
#define SIO_IRQ_PROC0     15
#define RESTART_WORD      0x52535452u   /* "RSTR" */
#define RESTARTING        0x52535452u
#define VECTORED_BOOT     0xb007c0d3u

volatile uint32_t Beat FORTH_KEEP;
volatile uint32_t SoftRestarts FORTH_KEEP, HardRestarts FORTH_KEEP, FailedRestarts FORTH_KEEP;
static volatile UINT32 Restarting FORTH_KEEP;
static UINT32 SavedData[64] FORTH_KEEP;     /* .data as the image had it */

extern void (* const CortexMxVectorTable[])(void);
static UINT32 RamTable[64] __attribute__((aligned(256)));

void _OSResetHandler(void);
void RestartEntry(void);
void RestartIRQ(void);

/* RestartCore0: From RestartEntry, the stack set back, interrupts masked. */
void __attribute__((noreturn, used)) RestartCore0(void)
{
  extern UINT32 _data, _edata;
  UINT32 *p, i = 0;
  NVIC_ICER = 0xFFFFFFFF;
  NVIC_ICPR = 0xFFFFFFFF;
  ICSR = (1u << 27) | (1u << 25);    /* PENDSVCLR, PENDSTCLR */
  TIMER_ARMED = 0xF;                 /* written 1, an alarm is disarmed */
  TIMER_INTR = 0xF;
  TIMER_INTF = 0;
  while (SIO_FIFO_ST & SIO_FIFO_VLD)
     (void)SIO_FIFO_RD;
  SIO_FIFO_ST = 0xFF;                /* clears the error flags */
  for (p = &_data; p < &_edata && i < 64; p += 1)   /* as many as RestartInit saved */
     *p = SavedData[i++];
  Restarting = RESTARTING;
  _OSResetHandler();
  for (;;);
} /* end of RestartCore0 */

/* RestartEntry: Where both restarts land, in Thread mode: the stack at its top. */
__asm__(
"  .syntax unified\n"
"  .section .text.RestartEntry,\"ax\",%progbits\n"
"  .global RestartEntry\n"
"  .thumb_func\n"
"RestartEntry:\n"
"  cpsid i\n"
"  ldr  r0, =CortexMxVectorTable\n"
"  ldr  r1, [r0]\n"
"  msr  msp, r1\n"
"  ldr  r2, =0xE000ED08\n"
"  str  r0, [r2]\n"
"  bl   RestartCore0\n"
"  .ltorg\n"
/* RestartIRQ: The SIO FIFO's interrupt. Interrupting Thread mode (EXC_RETURN 0xFFFFFFF9),
** it rewrites the PC and xPSR of the frame stacked on the one stack, and returns into
** RestartEntry; interrupting a handler, it does nothing, and the hard restart follows.
** The word taken first, and only if there is one, then the error flags cleared: read
** last, the interrupt came again on an empty FIFO, and its flag (ROE) held it for ever
** (step 2, 2026-10-24). */
"  .section .text.RestartIRQ,\"ax\",%progbits\n"
"  .global RestartIRQ\n"
"  .thumb_func\n"
"RestartIRQ:\n"
"  ldr  r2, =0xD0000050\n"
"  ldr  r1, [r2]\n"               /* FIFO_ST */
"  lsls r1, r1, #31\n"
"  beq  2f\n"
"  ldr  r1, [r2, #8]\n"           /* FIFO_RD */
"2:\n"
"  movs r1, #0xFF\n"
"  str  r1, [r2]\n"               /* clears WOF and ROE */
"  dsb\n"
"  ldr  r1, =0xFFFFFFF9\n"
"  cmp  lr, r1\n"
"  bne  1f\n"
"  mrs  r0, msp\n"
"  ldr  r1, =RestartEntry\n"
"  movs r2, #1\n"
"  bics r1, r2\n"                 /* a stacked PC has bit 0 clear */
"  str  r1, [r0, #24]\n"
"  ldr  r1, =0x01000000\n"
"  str  r1, [r0, #28]\n"
"1:\n"
"  bx   lr\n"
"  .ltorg\n"
);

/* RestartNMI: Core 0's NMI, the hard restart: the vector to RAM, then a reset of the core. */
static void RestartNMI(void)
{
  UINT32 pc = (UINT32)RestartEntry | 1;
  TIMER_INTF = 0;
  WATCHDOG_SCRATCH[4] = VECTORED_BOOT;
  WATCHDOG_SCRATCH[5] = pc ^ -VECTORED_BOOT;
  WATCHDOG_SCRATCH[6] = (UINT32)CortexMxVectorTable[0];
  WATCHDOG_SCRATCH[7] = pc;
  __asm volatile ("dsb" ::: "memory");
  AIRCR = (0x05FAu << 16) | (1u << 2);    /* VECTKEY, SYSRESETREQ */
  for (;;);
} /* end of RestartNMI */

int RestartInit(void)
{
  extern UINT32 _data, _edata;
  UINT32 i, *p;
  int restarted = Restarting == RESTARTING;
  Restarting = 0;
  if ((UINT32)(&_edata - &_data) > 64)   /* more than SavedData keeps */
     while (TRUE);
  if (!restarted) {
     /* .data as loaded, for the restarts to put back. */
     i = 0;
     for (p = &_data; p < &_edata && i < 64; p += 1)
        SavedData[i++] = *p;
     Beat = SoftRestarts = HardRestarts = FailedRestarts = 0;
  }
  /* The vector table in RAM, the SIO FIFO's interrupt ours, of the highest priority. */
  for (i = 0; i < 64; i += 1)
     RamTable[i] = (UINT32)CortexMxVectorTable[i];
  RamTable[16 + SIO_IRQ_PROC0] = (UINT32)RestartIRQ;
  RamTable[2] = (UINT32)RestartNMI;
  /* SYSCFG leaves reset here, or the mask written is lost and no NMI comes (step 2). */
  RESETS_CLR = RESETS_SYSCFG_BIT;
  while ((RESETS_RESET_DONE & RESETS_SYSCFG_BIT) == 0);
  SYSCFG_PROC0_NMI_MASK = ALARM2_BIT;
  VTOR = (UINT32)RamTable;
  while (SIO_FIFO_ST & SIO_FIFO_VLD)
     (void)SIO_FIFO_RD;
  NVIC_IPR[SIO_IRQ_PROC0 >> 2] &= ~(0xFFu << (8 * (SIO_IRQ_PROC0 & 3)));
  NVIC_ISER = 1u << SIO_IRQ_PROC0;
  return restarted;
} /* end of RestartInit */


/* ---- Core 1. */

static enum { WATCHING, SOFT, HARD, GAVE_UP } State FORTH_KEEP;
static UINT32 Last FORTH_KEEP, LastAt FORTH_KEEP, AskedAt FORTH_KEEP;

void WatchInit(void)
{
  State = WATCHING;
  Last = Beat;
  LastAt = TIMER_TIMERAWL;
} /* end of WatchInit */

int Watch(void)
{
  UINT32 now = TIMER_TIMERAWL, beat = Beat;
  int back = 0;
  if (beat != Last) {
     /* Back after GAVE_UP too, late: watched again from here. */
     if (State != WATCHING) {
        back = State == SOFT ? RESTART_SOFT : RESTART_HARD;
        if (State == SOFT)
           SoftRestarts += 1;
        else
           HardRestarts += 1;
        State = WATCHING;
     }
     Last = beat, LastAt = now;
  }
  if (State == WATCHING && now - LastAt > DETECT) {
     while ((SIO_FIFO_ST & SIO_FIFO_RDY) == 0);
     SIO_FIFO_WR = RESTART_WORD;
     State = SOFT, AskedAt = now;
  }
  else if (State == SOFT && now - AskedAt > SOFT_WAIT) {
     TIMER_INTF = ALARM2_BIT;
     State = HARD, AskedAt = TIMER_TIMERAWL;
  }
  else if (State == HARD && now - AskedAt > HARD_WAIT) {
     FailedRestarts += 1;
     State = GAVE_UP;
     back = RESTART_FAILED;
  }
  return back;
} /* end of Watch */
