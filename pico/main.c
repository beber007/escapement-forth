/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File main.c: Core 0 runs the kernel and the tasks of the pool of slots, one a period;
** core 1 the console (console.c), started before the kernel.
**
** A word that runs past its next release would stop the kernel, its default on an
** overrun: the hook lets it go on instead, and counts it, the release it met skipped.
** One that loops for ever is core 1's to stop: it restarts core 0 (restart.c), which
** comes through main again, the dictionary and the pool kept, the word stopped.
**
** Platform version: RP2040 (Raspberry Pi Pico), RP2350 (Raspberry Pi Pico 2).
*/

#include "Escapement.h"
#include "Escapement_Core1.h"
#include "forth.h"
#include "slots.h"
#include "console.h"
#include "restart.h"
#include "chip.h"

#define VTOR              *((volatile UINT32 *)0xE000ED08)
#define WATCHDOG_CTRL_CLR *((volatile UINT32 *)(CHIP_WATCHDOG + 0x3000))
#define WATCHDOG_ENABLE   (1u << 30)
#define RESETS_CLR        *((volatile UINT32 *)(CHIP_RESETS + 0x3000))
#define RESETS_DONE       *((volatile UINT32 *)(CHIP_RESETS + 0x08))
#define TIMER_DBGPAUSE    *((volatile UINT32 *)(CHIP_TIMER + 0x2C))

volatile uint32_t Overruns FORTH_KEEP;

/* Core 1's stack, which a restart of core 0 must not clear under it. */
static UINT32 Core1Stack[512] FORTH_KEEP __attribute__((aligned(8)));

static void SlotTask(void *argument)
{
  int period = (int)(intptr_t)argument;
  int traced = TraceOn && SlotsBound(period), over;
  if (period == 0)
     Beat += 1;
  if (traced)
     TraceNote(TRACE_START,(uint32_t)period);
  over = SlotsRun(period);
  if (traced)
     TraceNote(over ? TRACE_END_OVER : TRACE_END,(uint32_t)period);
  OSEndTask();
} /* end of SlotTask */

static UINT8 Overrun(void *argument)
{
  (void)argument;
  Overruns += 1;
  return OS_OVERRUN_CONTINUE;
} /* end of Overrun */


int main(void)
{
  extern void (* const CortexMxVectorTable[])(void);
  int i, restarted;
  VTOR = (UINT32)CortexMxVectorTable;
  #ifdef RP2350
     /* The kernel's limit stops at its .bss: what the Forth keeps sits above it. */
     {
        extern UINT8 _ForthKeptEnd;
        __asm volatile ("msr msplim, %0" :: "r" (((UINT32)&_ForthKeptEnd + 7) & ~7u) : "memory");
     }
  #endif
  /* First, while .data is as the image had it: the restarts put it back. */
  restarted = RestartInit();
  if (!restarted) {
     /* A firmware in the flash may have armed the watchdog, which the debugger's hold on
     ** core 1 paused: core 1 runs from here on. */
     WATCHDOG_CTRL_CLR = WATCHDOG_ENABLE;
     OSInitializeSystemClocks();
     ForthInit();
     ConsoleInit();
     Overruns = 0;
  }
  else {
     SlotsRestarted();
     if (TraceOn)
        TraceNote(TRACE_LOST,0);
  }
  /* The loader may leave core 1 halted, which pauses the counter the kernel set to pause
  ** with it; core 1 runs from here on, and no core is halted. */
  TIMER_DBGPAUSE = 0;
  for (i = 0; i < SLOT_PERIODS; i += 1)
     if (!OSCreateTask(SlotTask,0,(INT32)SlotPeriod[i],(INT32)SlotPeriod[i],
                       (void *)(intptr_t)i))
        while (TRUE);
  OSSetOverrunHook(Overrun);
  /* Out of reset before core 1 sets its alarm: the kernel releases the timer only later, in
  ** OSStartMultitasking, and a write to a block in reset may be lost (a review, 2026-11-01). */
  RESETS_CLR = CHIP_RESET_TIMER;
  while ((RESETS_DONE & CHIP_RESET_TIMER) == 0);
  if (!restarted)
     OSLaunchCore1(Console,&Core1Stack[sizeof Core1Stack / sizeof Core1Stack[0]]);
  return OSStartMultitasking(NULL,NULL);
} /* end of main */
