/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File platform_host.c: The platform of the tests: the output kept in a buffer, a clock
** that moves only when SPEND says so.
*/

#include "forth.h"
#include "forth_platform.h"
#include "slots.h"
#include "platform_host.h"

char Output[4096];
int OutputLength;
uint32_t Clock;

void ForthEmit(char c)
{
  if (OutputLength < (int)sizeof Output - 1)
     Output[OutputLength++] = c;
  Output[OutputLength] = 0;
} /* end of ForthEmit */

uint32_t ForthNow(void)
{
  return Clock;
} /* end of ForthNow */

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

void ForthPlatformWords(void)
{
  ForthAddWord("spend",PSpend,0);
  SlotsWords();
} /* end of ForthPlatformWords */
