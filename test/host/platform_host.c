/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File platform_host.c: The platform of the tests: the output kept in a buffer, a clock
** that moves only when SPEND says so.
*/

#include <stdio.h>
#include "forth.h"
#include "forth_platform.h"
#include "slots.h"
#include "platform_host.h"

jmp_buf Restart;

char Output[4096];
int OutputLength;
uint32_t Clock;
static int Preempt, Masked, Pending;

void ForthEmit(char c)
{
  if (OutputLength < (int)sizeof Output - 1)
     Output[OutputLength++] = c;
  Output[OutputLength] = 0;
} /* end of ForthEmit */

/* Preempted: The 1 ms slot's task run now, or once the interrupts are let be. */
static void Preempted(void)
{
  if (Masked)
     Pending = 1;
  else
     SlotsRun(0);
} /* end of Preempted */

uint32_t ForthNow(void)
{
  uint32_t now;
  if (Preempt < 0 && ++Preempt == 0)
     Preempted();
  now = Clock;
  if (Preempt > 0 && --Preempt == 0)
     Preempted();
  return now;
} /* end of ForthNow */

int Break;

int ForthBreak(void)
{
  int asked = Break;
  Break = 0;
  return asked;
} /* end of ForthBreak */

uint32_t ForthMask(void)
{
  uint32_t mask = (uint32_t)Masked;
  Masked = 1;
  return mask;
} /* end of ForthMask */

void ForthUnmask(uint32_t mask)
{
  Masked = (int)mask;
  if (!Masked && Pending) {
     Pending = 0;
     SlotsRun(0);
  }
} /* end of ForthUnmask */

void ForthBarrier(void)
{
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
} /* end of ForthBarrier */

/* SPEND ( us -- ): Moves the clock on, as the time a word takes. */
static void PSpend(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  Clock += (uint32_t)FORTH_POP(c);
} /* end of PSpend */

/* STALL ( -- ): Leaves the slot's run where it is, as a restart of the core abandons the
** word that held it (test_forth.c, Run). */
static void PStall(Context *c, const Word *w)
{
  (void)c; (void)w;
  longjmp(Restart,1);
} /* end of PStall */

/* RUN0 ( -- ): The 1 ms slot's task, run in the middle of a console's line, as core 0 may
** run it on the board. */
static void PRun0(Context *c, const Word *w)
{
  (void)c; (void)w;
  SlotsRun(0);
} /* end of PRun0 */

/* PREEMPT ( n -- ): The 1 ms slot's task run just after the n-th reading of the clock
** from here, or just before the -n-th, as core 0 may preempt between two readings. */
static void PPreempt(Context *c, const Word *w)
{
  (void)w;
  FORTH_NEED(c,1);
  Preempt = (int)FORTH_POP(c);
} /* end of PPreempt */

/* PROMOTING ( n -- ): The console promotes entry n, as core 1 may while a slot's run
** goes on. */
static void PPromoting(Context *c, const Word *w)
{
  char line[32];
  (void)w;
  FORTH_NEED(c,1);
  snprintf(line,sizeof line,"%ld promote",(long)FORTH_POP(c));
  ForthInterpret(line);
} /* end of PPromoting */

void ForthPlatformWords(void)
{
  ForthAddWord("promoting",PPromoting,0);
  ForthAddWord("preempt",PPreempt,0);
  ForthAddWord("run0",PRun0,0);
  ForthAddWord("spend",PSpend,0);
  ForthAddWord("stall",PStall,0);
  SlotsWords();
} /* end of ForthPlatformWords */
