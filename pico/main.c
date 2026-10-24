/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File main.c: Core 0 runs the kernel and the tasks of the pool of slots, one a period;
** core 1 the console (console.c), started before the kernel.
**
** A word that runs past its next release would stop the kernel, its default on an
** overrun: the hook lets it go on instead, and counts it, the release it met skipped.
**
** Platform version: RP2040 (Raspberry Pi Pico).
*/

#include "Escapement.h"
#include "Escapement_Core1.h"
#include "forth.h"
#include "slots.h"
#include "console.h"

#define VTOR              *((volatile UINT32 *)0xE000ED08)
#define WATCHDOG_CTRL_CLR *((volatile UINT32 *)(0x40058000 + 0x3000))
#define WATCHDOG_ENABLE   (1u << 30)

volatile UINT32 Overruns;

static UINT32 Core1Stack[512] __attribute__((aligned(8)));

static void SlotTask(void *argument)
{
  SlotsRun((int)(intptr_t)argument);
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
  int i;
  VTOR = (UINT32)CortexMxVectorTable;
  /* A firmware in the flash may have armed the watchdog, which the debugger's hold on
  ** core 1 paused: core 1 runs from here on. */
  WATCHDOG_CTRL_CLR = WATCHDOG_ENABLE;
  OSInitializeSystemClocks();
  ForthInit();
  for (i = 0; i < SLOT_PERIODS; i += 1)
     if (!OSCreateTask(SlotTask,0,(INT32)SlotPeriod[i],(INT32)SlotPeriod[i],
                       (void *)(intptr_t)i))
        while (TRUE);
  OSSetOverrunHook(Overrun);
  OSLaunchCore1(Console,&Core1Stack[sizeof Core1Stack / sizeof Core1Stack[0]]);
  return OSStartMultitasking(NULL,NULL);
} /* end of main */
