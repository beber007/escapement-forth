/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File slots.c: The entries of the pool, bound by the console, run by the kernel's tasks
** (slots.h).
**
** An entry's binding, its word, state and worst case, changes under a sequence counter
** the console's core alone writes: odd while it changes. The kernel's core reads the
** counter, the binding, then the counter again, and runs the word only if the counter
** was even and the same both times. Its figures it keeps for the generation it read, and
** starts again from zero for a new one.
*/

#include "slots.h"
#include "forth_platform.h"

const uint32_t SlotPeriod[SLOT_PERIODS] = {1000, 10000, 100000, 1000000};

typedef struct {
   /* Written by the console's core. */
   volatile uint32_t Sequence;
   const Word *volatile Bound;
   volatile uint8_t State;
   volatile uint32_t Wcet;         /* us, the worst case admitted */
   /* Written by the kernel's core. */
   volatile uint32_t Generation;   /* the sequence the figures are of */
   volatile uint32_t Runs, MaxTime, Excess;
   volatile uint8_t Error;
} Slot;

static Slot Slots[SLOTS] FORTH_KEEP;

/* The entries running on the kernel's core, the one holding it last: slots of shorter
** periods preempt the others, one stack, so they nest. Written by that core alone. */
static volatile int8_t Running[SLOT_PERIODS] FORTH_KEEP;
static volatile uint32_t Excluded[SLOT_PERIODS] FORTH_KEEP;   /* us of the slots above */
static volatile int RunningDepth FORTH_KEEP;

static void Measure(Slot *s, uint32_t sequence, const Word *w, uint8_t state, uint32_t wcet,
                    Context *c);


void SlotsRun(int period)
{
  Context c;
  int pass, i;
  for (pass = 0; pass < 2; pass += 1)
     for (i = period * SLOT_ENTRIES; i < (period + 1) * SLOT_ENTRIES; i += 1) {
        Slot *s = &Slots[i];
        uint32_t sequence = s->Sequence, wcet;
        const Word *w;
        uint8_t state;
        if (sequence & 1)
           continue;
        ForthBarrier();
        w = s->Bound;
        state = s->State;
        wcet = s->Wcet;
        ForthBarrier();
        if (s->Sequence != sequence || state == SLOT_FREE ||
            (state == SLOT_PROMOTED) != (pass == 0))
           continue;
        Measure(s,sequence,w,state,wcet,&c);
     }
} /* end of SlotsRun */

/* Measure: Runs the word once within its budget, the worst case admitted or, on trial,
** a tenth of its period; its time taken on the clock, less what the slots that preempted
** it took (Excluded): the kernel's entries and the interrupts are in it.
** The interrupts held at its start and at its end keep a slot that preempts from being
** counted against this one or the one below; in its run, ForthOwnTime reads again. */
static void Measure(Slot *s, uint32_t sequence, const Word *w, uint8_t state, uint32_t wcet,
                    Context *c)
{
  uint32_t start, end, excluded, time, mask;
  int error, depth;
  if (s->Generation != sequence) {
     s->Runs = s->MaxTime = s->Excess = 0;
     s->Error = FORTH_OK;
     s->Generation = sequence;
  }
  if (s->Error != FORTH_OK)
     return;
  c->Depth = c->ReturnDepth = 0;
  c->IP = NULL;
  /* The depth, the start, the time left out and the entry run, as the end and what the
  ** slot below is told, at one instant: a slot that preempted in between counted against
  ** this one, or against the one below, or was lost (three reviews, 2026-10-24). */
  mask = ForthMask();
  depth = RunningDepth;
  RunningDepth = depth + 1;
  start = ForthNow();
  Excluded[depth] = 0;
  Running[depth] = (int8_t)(s - Slots);
  ForthUnmask(mask);
  c->Start = start;
  c->Excluded = &Excluded[depth];
  c->Budget = state == SLOT_PROMOTED ? wcet : SlotPeriod[(s - Slots) / SLOT_ENTRIES] / 10;
  error = ForthExecute(c,w);
  mask = ForthMask();
  excluded = Excluded[depth];
  end = ForthNow();
  if (depth > 0)
     Excluded[depth - 1] += end - start;
  Running[depth] = -1;
  RunningDepth = depth;
  ForthUnmask(mask);
  time = end - start - excluded;
  /* Over its budget, the run was cut short: counted, the word run again next time.
  ** Counted before its time is written, which PROMOTE reads first (PPromote). */
  if (error == FORTH_BUDGET || (state == SLOT_PROMOTED && time > wcet)) {
     s->Excess += 1;
     error = FORTH_OK;
  }
  s->Runs += 1;
  if (time > s->MaxTime)
     s->MaxTime = time;
  s->Error = (uint8_t)error;
} /* end of Measure */


void SlotsRestarted(void)
{
  int i;
  if (RunningDepth > 0 && Running[RunningDepth - 1] >= 0)
     Slots[Running[RunningDepth - 1]].Error = FORTH_STALLED;
  RunningDepth = 0;
  for (i = 0; i < SLOT_PERIODS; i += 1)
     Running[i] = -1;
} /* end of SlotsRestarted */


/* ---- The console's side. */

static void Bind(Slot *s, const Word *w, uint8_t state, uint32_t wcet)
{
  s->Sequence += 1;
  ForthBarrier();
  s->Bound = w;
  s->State = state;
  s->Wcet = wcet;
  ForthBarrier();
  s->Sequence += 1;
} /* end of Bind */

/* Admitted: The worst case a word is promoted with: what was measured, a quarter more,
** and a microsecond for the clock's grain. */
static uint32_t Admitted(uint32_t max)
{
  return max + max / 4 + 1;
} /* end of Admitted */

static uint32_t Share(uint32_t time, uint32_t period)
{
  return (uint32_t)(((uint64_t)time * 1000000u + period - 1) / period);
} /* end of Share */

uint32_t SlotsLoad(void)
{
  uint32_t load = 0;
  int i;
  for (i = 0; i < SLOT_PERIODS; i += 1)
     load += Share(SLOT_COST,SlotPeriod[i]);
  for (i = 0; i < SLOTS; i += 1)
     if (Slots[i].State == SLOT_PROMOTED)
        load += Share(Slots[i].Wcet,SlotPeriod[i / SLOT_ENTRIES]);
  return load;
} /* end of SlotsLoad */

/* PrintShare: In percent, to a tenth, rounded up. */
static void PrintShare(uint32_t share)
{
  share = (share + 999) / 1000;
  ForthPrintDigits(share / 10);
  ForthEmit('.');
  ForthPrintDigits(share % 10);
  ForthPrintText("% ");
} /* end of PrintShare */

/* Entry: The entry numbered on the stack, NULL with the error set if there is none. */
static Slot *Entry(Context *c)
{
  Cell n;
  if (!ForthOnConsole(c)) {
     c->Error = FORTH_ARGUMENT;
     return NULL;
  }
  if (c->Depth < 1) {
     c->Error = FORTH_UNDERFLOW;
     return NULL;
  }
  n = FORTH_POP(c);
  if (n < 0 || n >= SLOTS) {
     c->Error = FORTH_ARGUMENT;
     return NULL;
  }
  return &Slots[n];
} /* end of Entry */

/* EVERY ( xt ms -- ): Binds the word to a free entry of that period, on trial. */
static void PEvery(Context *c, const Word *w)
{
  Cell period;
  const Word *x;
  int p, i;
  (void)w;
  if (!ForthOnConsole(c)) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  FORTH_NEED(c,2);
  period = FORTH_POP(c);
  if (!ForthIsWord(FORTH_TOP(c))) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  x = (const Word *)FORTH_POP(c);
  /* In ms against the periods divided, not the period multiplied, which wrapped on a
  ** cell of 32 bits: 536870913 was taken for 1 (a review, 2026-10-24). */
  for (p = 0; p < SLOT_PERIODS && (Cell)(SlotPeriod[p] / 1000) != period; p += 1);
  if (p == SLOT_PERIODS) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  for (i = p * SLOT_ENTRIES; i < (p + 1) * SLOT_ENTRIES && Slots[i].State != SLOT_FREE;
       i += 1);
  if (i == (p + 1) * SLOT_ENTRIES) {
     c->Error = FORTH_FULL;
     return;
  }
  Bind(&Slots[i],x,SLOT_TRIAL,0);
  ForthPrintText("slot ");
  ForthPrintNumber(i);
} /* end of PEvery */

/* PROMOTE ( n -- ): Admits the word on trial in entry n, or says why not. */
static void PPromote(Context *c, const Word *w)
{
  Slot *s;
  uint32_t max, wcet, load;
  (void)w;
  if ((s = Entry(c)) == NULL)
     return;
  /* The longest time read before what Measure counts ahead of it: a run cut short in
  ** between is not admitted at its cut (a review, 2026-10-24). */
  max = s->MaxTime;
  if (s->State != SLOT_TRIAL || s->Generation != s->Sequence || s->Error != FORTH_OK) {
     ForthPrintText("not on trial ");
     return;
  }
  if (s->Excess != 0) {
     ForthPrintText("over budget ");
     return;
  }
  if (s->Runs < SLOT_MIN_RUNS) {
     ForthPrintText("too few runs ");
     return;
  }
  wcet = Admitted(max);
  load = SlotsLoad() + Share(wcet,SlotPeriod[(s - Slots) / SLOT_ENTRIES]);
  if (load > SLOT_BOUND) {
     ForthPrintText("refused, load ");
     PrintShare(load);
     return;
  }
  Bind(s,s->Bound,SLOT_PROMOTED,wcet);
  ForthPrintText("promoted, wcet ");
  ForthPrintNumber(wcet);
  ForthPrintText("us, load ");
  PrintShare(load);
} /* end of PPromote */

/* STOP ( n -- ): Frees entry n. */
static void PStop(Context *c, const Word *w)
{
  Slot *s;
  (void)w;
  if ((s = Entry(c)) != NULL)
     Bind(s,NULL,SLOT_FREE,0);
} /* end of PStop */

/* SLOTS ( -- ): Each entry bound, then the load of the processor admitted. */
static void PSlots(Context *c, const Word *w)
{
  static const char *const States[] = {"free", "trial", "promoted"};
  int i, j;
  (void)w;
  if (!ForthOnConsole(c)) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  for (i = 0; i < SLOTS; i += 1) {
     const Slot *s = &Slots[i];
     const Word *b = s->Bound;
     int current;
     if (s->State == SLOT_FREE || b == NULL)
        continue;
     ForthEmit('\n');
     ForthPrintNumber(i);
     ForthPrintDigits(SlotPeriod[i / SLOT_ENTRIES]);
     ForthPrintText("us ");
     for (j = 0; j < b->Length; j += 1)
        ForthEmit(b->Name[j]);
     ForthEmit(' ');
     ForthPrintText(States[s->State]);
     /* The figures of the word bound before, until the kernel's core first runs this one. */
     current = s->Generation == s->Sequence;
     ForthPrintText(" runs ");
     ForthPrintNumber(current ? s->Runs : 0);
     ForthPrintText("max ");
     ForthPrintNumber(current ? s->MaxTime : 0);
     if (s->State == SLOT_PROMOTED) {
        ForthPrintText("wcet ");
        ForthPrintNumber(s->Wcet);
     }
     if (s->State == SLOT_PROMOTED || (current && s->Excess != 0)) {
        ForthPrintText("over ");
        ForthPrintNumber(current ? s->Excess : 0);
     }
     if (current && s->Error != FORTH_OK) {
        ForthPrintText("failed: ");
        ForthPrintText(ForthErrorText(s->Error));
     }
  }
  ForthPrintText("\nload ");
  PrintShare(SlotsLoad());
} /* end of PSlots */

void SlotsWords(void)
{
  int i;
  /* Field by field: the board links no memset. */
  for (i = 0; i < SLOTS; i += 1) {
     Slots[i].Sequence = Slots[i].Generation = 0;
     Slots[i].Bound = NULL;
     Slots[i].State = SLOT_FREE;
     Slots[i].Wcet = Slots[i].Runs = Slots[i].MaxTime = Slots[i].Excess = 0;
     Slots[i].Error = FORTH_OK;
  }
  RunningDepth = 0;
  for (i = 0; i < SLOT_PERIODS; i += 1)
     Running[i] = -1;
  ForthAddWord("every",PEvery,0);
  ForthAddWord("promote",PPromote,0);
  ForthAddWord("stop",PStop,0);
  ForthAddWord("slots",PSlots,0);
} /* end of SlotsWords */
