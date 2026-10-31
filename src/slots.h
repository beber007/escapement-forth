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
** next release. EVERY and PROMOTE refuse what would take the budgets of all the words and
** the pool past SLOT_ALL, for a word on trial not to delay a promoted one past its
** deadline. The kernel enforces none: what the check does not reach, a word of C,
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
/* All the budgets and the pool. Not all of the processor: a run passes its budget by up to
** 13 % (slow words on a Pico W, 2026-10-31), 5 % with four trials of 1 ms. */
#define SLOT_ALL         950000
#define SLOT_COST        8                         /* us a release of an empty slot takes */
#define SLOT_MIN_RUNS    10                        /* runs measured before a promotion */

/* The periods of the pool in microseconds, the shortest first. */
extern const uint32_t SlotPeriod[SLOT_PERIODS];

enum { SLOT_FREE = 0, SLOT_TRIAL, SLOT_PROMOTED };

/* SlotsRun: Runs the words bound to the entries of a period, the promoted ones first.
** Called by that period's task, at each of its instances. Whether a run went past its
** budget. */
int SlotsRun(int period);

/* SlotsBound: Whether an entry of a period has a word. */
int SlotsBound(int period);

/* SlotsRestarted: Called by the kernel's core when it starts again, the kernel restarted
** with the pool kept: the word that held the core is stopped, its error FORTH_STALLED;
** those it had preempted run again at their next release. */
void SlotsRestarted(void);

/* SlotsWords: Adds EVERY, PROMOTE, STOP and SLOTS to the dictionary, the pool empty.
** Called by ForthPlatformWords, before the kernel runs the slots. */
void SlotsWords(void);

/* SlotsLoad: What the promoted words and the pool take, in parts per million. */
uint32_t SlotsLoad(void);

/* SlotView: An entry as SLOTS shows it: its word, its state and the figures of that
** word, 0 until the kernel's core first runs it; Budget is what a run may take, the
** worst case admitted or, on trial, a tenth of the period. */
typedef struct {
   const Word *Bound;
   uint8_t State, Error;
   uint32_t Runs, MaxTime, Excess, Wcet, Budget;
} SlotView;

/* SlotsView: Entry n, read on the console's core, for SLOTS and for a platform that
** draws the pool (web/). */
void SlotsView(int n, SlotView *v);

/* SlotsReport: The pool, for a program to read: the load and its bound, then a line for
** each entry bound: its number, state, runs, longest time, runs over budget, worst case
** and budget, its word's name, and the text of its error if it failed. */
void SlotsReport(void);

#endif /* SLOTS_H */
