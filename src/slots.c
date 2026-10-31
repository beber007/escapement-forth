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
   volatile uint32_t Carried;      /* the sequence whose figures go on into the next */
   /* Written by the kernel's core, under Writing, odd while it writes them. */
   volatile uint32_t Writing;
   volatile uint32_t Generation;   /* the sequence the figures are of */
   volatile uint32_t Runs, MaxTime, Excess;
   volatile uint8_t Error;
} Slot;

/* Figures: An entry's figures as the console's core reads them, all of one instant. */
typedef struct {
   uint32_t Generation, Runs, MaxTime, Excess;
   uint8_t Error;
} Figures;

static Slot Slots[SLOTS] FORTH_KEEP;

/* The entries running on the kernel's core, the one holding it last: slots of shorter
** periods preempt the others, one stack, so they nest. Written by that core alone. */
static volatile int8_t Running[SLOT_PERIODS] FORTH_KEEP;
static volatile uint32_t Excluded[SLOT_PERIODS] FORTH_KEEP;   /* us of the slots above */
static volatile int RunningDepth FORTH_KEEP;

static int Measure(Slot *s, uint32_t sequence, const Word *w, uint8_t state, uint32_t wcet,
                   Context *c);


int SlotsBound(int period)
{
  int i;
  for (i = period * SLOT_ENTRIES; i < (period + 1) * SLOT_ENTRIES; i += 1)
     if (Slots[i].State != SLOT_FREE)
        return 1;
  return 0;
} /* end of SlotsBound */

int SlotsRun(int period)
{
  Context c;
  int pass, i, over = 0;
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
        over |= Measure(s,sequence,w,state,wcet,&c);
     }
  return over;
} /* end of SlotsRun */

/* Measure: Runs the word once within its budget, the worst case admitted or, on trial,
** a tenth of its period; its time taken on the clock, less what the slots that preempted
** it took (Excluded): the kernel's entries and the interrupts are in it.
** The interrupts held at its start and at its end keep a slot that preempts from being
** counted against this one or the one below; in its run, ForthOwnTime reads again. */
/* Carries: Whether the figures of generation go on into sequence: the promotion's alone,
** not a word bound anew after it (a review, 2026-10-24). */
static int Carries(const Slot *s, uint32_t generation, uint32_t sequence)
{
  return generation == s->Carried && sequence == generation + 2;
} /* end of Carries */

/* Write, Written: On the kernel's core, around a change of an entry's figures. */
static void Write(Slot *s)
{
  s->Writing += 1;
  ForthBarrier();
} /* end of Write */

static void Written(Slot *s)
{
  ForthBarrier();
  s->Writing += 1;
} /* end of Written */

static int Measure(Slot *s, uint32_t sequence, const Word *w, uint8_t state, uint32_t wcet,
                   Context *c)
{
  uint32_t start, end, excluded, time, mask, budget, now;
  int error, depth, over;
  if (s->Generation != sequence) {
     mask = ForthMask();
     Write(s);
     if (!Carries(s,s->Generation,sequence)) {
        s->Runs = s->MaxTime = s->Excess = 0;
        s->Error = FORTH_OK;
     }
     s->Generation = sequence;
     Written(s);
     ForthUnmask(mask);
  }
  if (s->Error != FORTH_OK)
     return 0;
  c->Depth = c->ReturnDepth = c->Catches = c->Base = c->SavedDepth = c->SavedBase = 0;
  c->Steps = 0;
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
  time = end - start - excluded;
  /* Over its budget, cut short or ended before a check: counted, the word run again
  ** next time, but for an error of its own. The figures written before the entry is let
  ** go, or a restart in between lost them; under Writing, for SLOTS and PROMOTE to read
  ** them of one run (two reviews, 2026-10-24). */
  /* Promoted as it ran: judged against the worst case admitted. The sequence read under
  ** Writing, for PROMOTE, reading again once it has bound, to see the run unless it saw
  ** the promotion (two reviews, 2026-10-24). */
  Write(s);
  budget = c->Budget;
  now = s->Sequence;
  ForthBarrier();
  if (Carries(s,sequence,now))
     budget = s->Wcet;
  s->Runs += 1;
  over = error == FORTH_BUDGET || time > budget;
  if (over)
     s->Excess += 1;
  if (error == FORTH_BUDGET)
     error = FORTH_OK;
  if (time > s->MaxTime)
     s->MaxTime = time;
  s->Error = (uint8_t)error;
  Written(s);
  Running[depth] = -1;
  RunningDepth = depth;
  ForthUnmask(mask);
  return over;
} /* end of Measure */


void SlotsRestarted(void)
{
  int i;
  /* A change of figures the restart cut short, ended. */
  for (i = 0; i < SLOTS; i += 1)
     if (Slots[i].Writing & 1)
        Written(&Slots[i]);
  if (RunningDepth > 0 && Running[RunningDepth - 1] >= 0) {
     Write(&Slots[Running[RunningDepth - 1]]);
     Slots[Running[RunningDepth - 1]].Error = FORTH_STALLED;
     Written(&Slots[Running[RunningDepth - 1]]);
  }
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

/* Budgets: What all the budgets and the pool take, the words on trial's tenths included. */
static uint32_t Budgets(void)
{
  uint32_t load = SlotsLoad();
  int i;
  for (i = 0; i < SLOTS; i += 1)
     if (Slots[i].State == SLOT_TRIAL)
        load += Share(SlotPeriod[i / SLOT_ENTRIES] / 10,SlotPeriod[i / SLOT_ENTRIES]);
  return load;
} /* end of Budgets */

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
     c->Error = FORTH_PERIOD_FULL;
     return;
  }
  if (Budgets() + Share(SlotPeriod[p] / 10,SlotPeriod[p]) > SLOT_ALL) {
     c->Error = FORTH_PROCESSOR_FULL;
     return;
  }
  Bind(&Slots[i],x,SLOT_TRIAL,0);
  ForthPrintText("slot ");
  ForthPrintNumber(i);
} /* end of PEvery */

/* Read: On the console's core, an entry's figures, read again while the kernel's core
** wrote them. */
static void Read(const Slot *s, Figures *f)
{
  uint32_t writing;
  ForthBarrier();       /* after what this core stored before, Bind's sequence */
  do {
     writing = s->Writing;
     ForthBarrier();
     f->Generation = s->Generation;
     f->Runs = s->Runs;
     f->MaxTime = s->MaxTime;
     f->Excess = s->Excess;
     f->Error = s->Error;
     ForthBarrier();
  } while ((writing & 1) != 0 || s->Writing != writing);
} /* end of Read */

/* PROMOTE ( n -- ): Admits the word on trial in entry n, or says why not. */
static void PPromote(Context *c, const Word *w)
{
  Slot *s;
  Figures f;
  uint32_t wcet, load, period, all;
  (void)w;
  if ((s = Entry(c)) == NULL)
     return;
  Read(s,&f);
  if (s->State != SLOT_TRIAL || f.Generation != s->Sequence || f.Error != FORTH_OK) {
     ForthPrintText("not on trial ");
     return;
  }
  if (f.Excess != 0) {
     ForthPrintText("over budget ");
     return;
  }
  if (f.Runs < SLOT_MIN_RUNS) {
     ForthPrintText("too few runs ");
     return;
  }
  wcet = Admitted(f.MaxTime);
  period = SlotPeriod[(s - Slots) / SLOT_ENTRIES];
  load = SlotsLoad() + Share(wcet,period);
  if (load > SLOT_BOUND) {
     ForthPrintText("refused, load ");
     PrintShare(load);
     return;
  }
  /* Its tenth given back, its worst case taken: up to a quarter more. */
  all = Budgets() - Share(period / 10,period) + Share(wcet,period);
  if (all > SLOT_ALL) {
     ForthPrintText("refused, with trials ");
     PrintShare(all);
     return;
  }
  /* The figures of the trial go on: a run that ended as it was promoted, or that was
  ** running, counts against it, over or failed (a review, 2026-10-24). */
  s->Carried = s->Sequence;
  Bind(s,s->Bound,SLOT_PROMOTED,wcet);
  /* A run that ended as it was weighed, past the worst case then admitted or failed: back
  ** on trial, anew (two reviews, 2026-10-24). */
  Read(s,&f);
  if (f.MaxTime > wcet || f.Excess != 0 || f.Error != FORTH_OK) {
     Bind(s,s->Bound,SLOT_TRIAL,0);
     if (f.Error != FORTH_OK) {
        ForthPrintText("failed: ");
        ForthPrintText(ForthErrorText(f.Error));
        ForthEmit(' ');
     }
     else
        ForthPrintText("over budget ");
     return;
  }
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

void SlotsView(int n, SlotView *v)
{
  const Slot *s = &Slots[n];
  Figures f;
  int current;
  v->Bound = s->Bound;
  v->State = s->State;
  v->Wcet = s->Wcet;
  v->Budget = v->State == SLOT_PROMOTED ? v->Wcet : SlotPeriod[n / SLOT_ENTRIES] / 10;
  /* The figures of the word bound before, until the kernel's core first runs this one. */
  Read(s,&f);
  current = f.Generation == s->Sequence || Carries(s,f.Generation,s->Sequence);
  v->Runs = current ? f.Runs : 0;
  v->MaxTime = current ? f.MaxTime : 0;
  v->Excess = current ? f.Excess : 0;
  v->Error = current ? f.Error : FORTH_OK;
} /* end of SlotsView */

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
     SlotView v;
     SlotsView(i,&v);
     if (v.State == SLOT_FREE || v.Bound == NULL)
        continue;
     ForthEmit('\n');
     ForthPrintNumber(i);
     ForthPrintDigits(SlotPeriod[i / SLOT_ENTRIES]);
     ForthPrintText("us ");
     for (j = 0; j < v.Bound->Length; j += 1)
        ForthEmit(v.Bound->Name[j]);
     ForthEmit(' ');
     ForthPrintText(States[v.State]);
     ForthPrintText(" runs ");
     ForthPrintNumber(v.Runs);
     ForthPrintText("max ");
     ForthPrintNumber(v.MaxTime);
     if (v.State == SLOT_PROMOTED) {
        ForthPrintText("wcet ");
        ForthPrintNumber(v.Wcet);
     }
     if (v.State == SLOT_PROMOTED || v.Excess != 0) {
        ForthPrintText("over ");
        ForthPrintNumber(v.Excess);
     }
     if (v.Error != FORTH_OK) {
        ForthPrintText("failed: ");
        ForthPrintText(ForthErrorText(v.Error));
     }
  }
  ForthPrintText("\nload ");
  PrintShare(SlotsLoad());
  if (Budgets() != SlotsLoad()) {
     ForthPrintText("with trials ");
     PrintShare(Budgets());
  }
} /* end of PSlots */

void SlotsReport(void)
{
  int i, j;
  ForthPrintNumber(SlotsLoad());
  ForthPrintNumber(SLOT_BOUND);
  for (i = 0; i < SLOTS; i += 1) {
     SlotView v;
     SlotsView(i,&v);
     if (v.State == SLOT_FREE || v.Bound == NULL)
        continue;
     ForthEmit('\n');
     ForthPrintNumber(i);
     ForthPrintNumber(v.State);
     ForthPrintNumber(v.Runs);
     ForthPrintNumber(v.MaxTime);
     ForthPrintNumber(v.Excess);
     ForthPrintNumber(v.Wcet);
     ForthPrintNumber(v.Budget);
     for (j = 0; j < v.Bound->Length; j += 1)
        ForthEmit(v.Bound->Name[j]);
     ForthEmit(' ');
     if (v.Error != FORTH_OK)
        ForthPrintText(ForthErrorText(v.Error));
  }
} /* end of SlotsReport */

void SlotsWords(void)
{
  int i;
  /* Field by field: the board links no memset. */
  for (i = 0; i < SLOTS; i += 1) {
     Slots[i].Sequence = Slots[i].Writing = Slots[i].Generation = 0;
     Slots[i].Carried = 1;    /* odd: no sequence */
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
