/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File slots.h: The pool of slots: periodic tasks of the kernel, created before it
** starts, to which the console binds words while it runs.
**
** The kernel admits no task once it runs; the pool does instead. Each period of the pool
** is one task, released whether a word is bound to it or not, and holds a few entries.
** A word bound with EVERY is on trial: it runs, its time is measured, and nothing is
** promised. PROMOTE admits it, with a worst case drawn from what was measured, if the
** promoted words and the pool itself then fit under SLOT_BOUND of the processor. Each
** run has a budget, its worst case or, on trial, a tenth of its period, which the Forth
** checks at NEXT: past it the run is cut and counted, and the word runs again at its
** next release. The kernel enforces none: what the check does not reach, a word of C,
** delays the tasks of later deadlines all the same.
**
** The console's core alone binds, promotes and stops, through a sequence counter; the
** kernel's core alone writes what it measures. A word that fails, its stack empty or
** full, is stopped there and its error kept.
*/

#ifndef SLOTS_H
#define SLOTS_H

#include "forth.h"

#define SLOT_PERIODS     4
#define SLOT_ENTRIES     4                         /* entries of each period */
#define SLOTS            (SLOT_PERIODS * SLOT_ENTRIES)
#define SLOT_BOUND       700000                    /* parts per million of the processor */
#define SLOT_COST        8                         /* us a release of an empty slot takes */
#define SLOT_MIN_RUNS    10                        /* runs measured before a promotion */

/* The periods of the pool in microseconds, the shortest first. */
extern const uint32_t SlotPeriod[SLOT_PERIODS];

enum { SLOT_FREE = 0, SLOT_TRIAL, SLOT_PROMOTED };

/* SlotsRun: Runs the words bound to the entries of a period, the promoted ones first.
** Called by that period's task, at each of its instances. */
void SlotsRun(int period);

/* SlotsRestarted: Called by the kernel's core when it starts again, the kernel restarted
** with the pool kept: the word that held the core is stopped, its error FORTH_STALLED;
** those it had preempted run again at their next release. */
void SlotsRestarted(void);

/* SlotsWords: Adds EVERY, PROMOTE, STOP and SLOTS to the dictionary, the pool empty.
** Called by ForthPlatformWords, before the kernel runs the slots. */
void SlotsWords(void);

/* SlotsLoad: What the promoted words and the pool take, in parts per million. */
uint32_t SlotsLoad(void);

#endif /* SLOTS_H */
