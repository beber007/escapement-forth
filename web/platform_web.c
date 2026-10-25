/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File platform_web.c: The platform of the page: the Forth compiled to WebAssembly, in a
** Web Worker of the browser (web/worker.js), with no C library.
**
** The kernel is the board's, the hard kernel under EDF, with the tasks of pico/main.c,
** one a period, over the port of kernel/kernel_web.c. The console is its idle task: the
** slots preempt it, and one another, where the Forth reads the time (ForthNow), the
** interrupts unmasked. On the board the console has a core of its own.
*/

#include "Escapement.h"
#include "kernel_web.h"
#include "forth.h"
#include "forth_platform.h"
#include "slots.h"

/* What the worker gives: the time in milliseconds, performance.now(), and where the
** output goes. */
__attribute__((import_module("env"), import_name("web_now"))) double WebNow(void);
__attribute__((import_module("env"), import_name("web_write")))
   void WebWrite(const char *text, int length, uint32_t pins);

#define WEB_PINS     0x1C7FFFFCu   /* the Pico's pins free for words, as on the board */
#define WEB_LINE_US  10000000u     /* a line typed at the console, stopped after 10 s */
#define WEB_FLUSH_US 50000u        /* what is printed goes out at least this often */
#define RING         256u          /* what the slots print, as pico/console.c keeps it */

static char Output[4096];
static int OutputLength;
static uint32_t Pins, Overruns, PinsSent;
static uint32_t LineStart, LastFlush;
static unsigned Broken;
static char Line[FORTH_LINE + 1];
static char Ring[RING];
static uint32_t RingHead, RingTail, Dropped;
static int InSlot;

/* The trace the page draws: each instance of a slots' task and each line of the console,
** its start and its end on the kernel's clock, in a ring the worker reads behind
** TraceHead, the oldest overwritten. An end tells whether a run of that instance went
** past its budget. */
#define TRACE        8192u
enum { TRACE_START = 1, TRACE_END, TRACE_LINE, TRACE_LINE_END };
static uint32_t Trace[TRACE][2];
static uint32_t TraceHead;

static void Note(uint32_t kind, uint32_t period, uint32_t over)
{
  Trace[TraceHead % TRACE][0] = WebKernelClock();
  Trace[TraceHead % TRACE][1] = kind | period << 8 | over << 16;
  TraceHead += 1;
} /* end of Note */

/* The C library the compiler may call. */
void *memset(void *s, int c, size_t n)
{
  unsigned char *p = s;
  while (n--)
     *p++ = (unsigned char)c;
  return s;
} /* end of memset */

void *memcpy(void *d, const void *s, size_t n)
{
  unsigned char *p = d;
  const unsigned char *q = s;
  while (n--)
     *p++ = *q++;
  return d;
} /* end of memcpy */

/* Wall: The browser's clock, in us, less the pauses of the browser inside a call of the
** worker: a jump between two readings within one call past a step of the clock and
** WEB_STALL_US is the browser's, a garbage collection or the system, not the word's nor
** the kernel's, which the board has none of; measured, it put a word of 700 us at 14 ms
** in Chrome (2026-10-25). The step is the smallest seen: 100 us in Chrome, a whole ms in
** other browsers, whose every step would else be taken for a pause, and where a pause of
** 2 ms, left in, read a word of 500 us at 2 ms, over its budget of 1 ms (a review,
** 2026-10-25). The time between two calls is the kernel's, caught up
** (kernel/kernel_web.c). */
#define WEB_STALL_US 500u
static uint32_t Stolen, LastWall, Grain = 0xFFFFFFFFu;
static int InCall;

static uint32_t Wall(void)
{
  uint32_t raw = (uint32_t)(long long)(WebNow() * 1000.0), step = raw - LastWall;
  if (InCall && step != 0 && step < Grain)
     Grain = step;
  if (InCall && step > WEB_STALL_US && step - WEB_STALL_US > Grain)
     Stolen += step;
  InCall = 1;
  LastWall = raw;
  return raw - Stolen;
} /* end of Wall */

/* Called: The worker's call ended: the time until the next is the kernel's. */
static void Called(void)
{
  InCall = 0;
} /* end of Called */

/* Flush: Sends what was printed, and the pins, to the page; with nothing printed, a
** change of the pins alone. */
static void Flush(void)
{
  WebWrite(Output,OutputLength,Pins);
  OutputLength = 0;
  PinsSent = Pins;
  LastFlush = Wall();
} /* end of Flush */

static void Put(char c)
{
  if (OutputLength == (int)sizeof Output)
     Flush();
  Output[OutputLength++] = c;
} /* end of Put */

/* ForthEmit: What a slot prints waits in the ring, its bytes past RING dropped and
** counted, until the console is between lines or waits in MS (Drain), as on the board:
** a line's output is not cut by a slot's. */
void ForthEmit(char c)
{
  if (!InSlot)
     Put(c);
  else if (RingHead - RingTail < RING)
     Ring[RingHead++ % RING] = c;
  else
     Dropped += 1;
} /* end of ForthEmit */

static void Drain(void)
{
  while (RingTail != RingHead)
     Put(Ring[RingTail++ % RING]);
} /* end of Drain */

/* ForthNow: Where the timer's interrupts are taken, unless masked: the time read after
** the tasks they elected, as an interrupt between two readings has it. */
uint32_t ForthNow(void)
{
  WebKernelSync(Wall());
  return Wall();
} /* end of ForthNow */

void ForthBarrier(void)
{
} /* end of ForthBarrier */

int ForthMemory(uintptr_t address, size_t size, int write)
{
  (void)address; (void)size; (void)write;
  return 0;
} /* end of ForthMemory */

/* ForthBreak: No key reaches the worker while a word runs: the console's line is stopped
** once it has run WEB_LINE_US, as Ctrl-C would; what it printed goes out meanwhile. */
int ForthBreak(void)
{
  uint32_t now = ForthNow();
  if (now - LastFlush > WEB_FLUSH_US)
     Flush();
  return now - LineStart > WEB_LINE_US;
} /* end of ForthBreak */

/* The store: none; SAVE answers that it could not. */
const void *ForthStored(size_t *room)
{
  *room = 0;
  return 0;
} /* end of ForthStored */

int ForthStore(const void *const part[], const size_t size[], int parts)
{
  (void)part; (void)size; (void)parts;
  return 0;
} /* end of ForthStore */

uint32_t ForthBuild(void)
{
  return 0;
} /* end of ForthBuild */

/* ForthMask, ForthUnmask: The kernel's mask, PRIMASK on the board; an interrupt held
** meanwhile taken once unmasked. */
uint32_t ForthMask(void)
{
  uint32_t mask = WebMasked;
  WebMasked = TRUE;
  return mask;
} /* end of ForthMask */

void ForthUnmask(uint32_t mask)
{
  WebMasked = (BOOL)mask;
  if (!mask)
     WebKernelSync(Wall());
} /* end of ForthUnmask */

/* OUTPUT HIGH LOW TOGGLE ( mask -- ), PINS ( -- mask ): The board's words, on pins the
** page draws; all are outputs. */
static void POutput(Context *c, const Word *w) { (void)w; FORTH_NEED(c,1); (void)FORTH_POP(c); }
static void PHigh(Context *c, const Word *w) { (void)w; FORTH_NEED(c,1); Pins |= (uint32_t)FORTH_POP(c) & WEB_PINS; }
static void PLow(Context *c, const Word *w) { (void)w; FORTH_NEED(c,1); Pins &= ~((uint32_t)FORTH_POP(c) & WEB_PINS); }
static void PToggle(Context *c, const Word *w) { (void)w; FORTH_NEED(c,1); Pins ^= (uint32_t)FORTH_POP(c) & WEB_PINS; }

static void PPins(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,(Cell)Pins);
} /* end of PPins */

/* OVERRUNS ( -- n ): The releases of a slots' task the kernel skipped, its instance
** before not ended, as on the board. */
static void POverruns(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,(Cell)Overruns);
} /* end of POverruns */

/* DROPPED ( -- n ): The bytes the slots printed that the ring had no room for. */
static void PDropped(Context *c, const Word *w)
{
  (void)w;
  FORTH_ROOM(c,1);
  FORTH_PUSH(c,(Cell)Dropped);
} /* end of PDropped */

/* SPEND ( us -- ): Holds the processor n microseconds of the word's own time, as its
** slot measures it, the slots of shorter periods preempting it meanwhile: a word whose
** cost the page sets. At the console, n microseconds of the clock. The budget checked as
** it waits, as NEXT checks it: a run past it cut there. In one wait, not a few of 100 us:
** a browser that reads its clock to the ms made each of these a whole ms (a review,
** 2026-10-25). */
static void PSpend(Context *c, const Word *w)
{
  uint32_t start = ForthNow(), before, wait;
  (void)w;
  FORTH_NEED(c,1);
  if (FORTH_TOP(c) < 0 || FORTH_TOP(c) > 1000000) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  wait = (uint32_t)FORTH_POP(c);
  if (c->Excluded == NULL)
     while (ForthNow() - start < wait)
        ;
  else {
     before = *c->Excluded;
     while (ForthOwnTime(start,c->Excluded) + before < wait)
        if (c->Budget != 0 && ForthOwnTime(c->Start,c->Excluded) > c->Budget) {
           c->Error = FORTH_BUDGET;
           return;
        }
  }
} /* end of PSpend */

/* MS ( n -- ): Waits n milliseconds, at the console alone, the slots run meanwhile. */
static void PMs(Context *c, const Word *w)
{
  uint32_t start = ForthNow(), wait;
  (void)w;
  if (!ForthOnConsole(c)) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  FORTH_NEED(c,1);
  if (FORTH_TOP(c) < 0 || FORTH_TOP(c) > 3600000) {
     c->Error = FORTH_ARGUMENT;
     return;
  }
  wait = (uint32_t)FORTH_POP(c) * 1000;
  while (ForthNow() - start < wait)
     if (ForthBreak()) {
        c->Error = FORTH_BREAK;
        return;
     }
     else
        Drain();
} /* end of PMs */

void ForthPlatformWords(void)
{
  ForthAddWord("output",POutput,0);
  ForthAddWord("high",PHigh,0);
  ForthAddWord("low",PLow,0);
  ForthAddWord("toggle",PToggle,0);
  ForthAddWord("pins",PPins,0);
  ForthAddWord("overruns",POverruns,0);
  ForthAddWord("dropped",PDropped,0);
  ForthAddWord("ms",PMs,0);
  ForthAddWord("spend",PSpend,0);
  SlotsWords();
} /* end of ForthPlatformWords */


/* Over: Whether a run of the period's entries went past its budget since the figures
** were kept in runs and excess, which it updates: a word bound anew starts from zero. */
static int Over(int period, uint32_t runs[SLOT_ENTRIES], uint32_t excess[SLOT_ENTRIES])
{
  int i, over = 0;
  for (i = 0; i < SLOT_ENTRIES; i += 1) {
     SlotView v;
     SlotsView(period * SLOT_ENTRIES + i,&v);
     over |= v.Runs < runs[i] ? v.Excess != 0 : v.Excess > excess[i];
     runs[i] = v.Runs;
     excess[i] = v.Excess;
  }
  return over;
} /* end of Over */

/* SlotTask, Overrun: Those of pico/main.c, each instance traced. */
static void SlotTask(void *argument)
{
  int period = (int)(intptr_t)argument;
  uint32_t runs[SLOT_ENTRIES] = {0}, excess[SLOT_ENTRIES] = {0};
  (void)Over(period,runs,excess);
  Note(TRACE_START,(uint32_t)period,0);
  InSlot += 1;
  SlotsRun(period);
  InSlot -= 1;
  Note(TRACE_END,(uint32_t)period,(uint32_t)Over(period,runs,excess));
  OSEndTask();
} /* end of SlotTask */

static UINT8 Overrun(void *argument)
{
  (void)argument;
  Overruns += 1;
  return OS_OVERRUN_CONTINUE;
} /* end of Overrun */

/* WebInit: The dictionary, once, and the kernel started with the tasks of the pool. */
__attribute__((export_name("web_init"))) void WebInit(void)
{
  int i;
  ForthInit();
  for (i = 0; i < SLOT_PERIODS; i += 1)
     (void)OSCreateTask(SlotTask,0,(INT32)SlotPeriod[i],(INT32)SlotPeriod[i],
                        (void *)(intptr_t)i);
  OSSetOverrunHook(Overrun);
  WebKernelBoot(Wall());
  Flush();
  Called();
} /* end of WebInit */

/* WebRun: The time passed given to the kernel, while the console waits for a line; the
** browser calls it every few milliseconds, no oftener. */
__attribute__((export_name("web_run"))) void WebRun(void)
{
  WebKernelSync(Wall());
  Drain();
  if (WebKernelBroken != Broken) {
     Broken = WebKernelBroken;
     ForthPrintText("\nthe nesting of the tasks broken\n");
  }
  if (OutputLength > 0 || Pins != PinsSent)
     Flush();
  Called();
} /* end of WebRun */

/* WebLine: Where the page writes a line of the console, of at most FORTH_LINE bytes. */
__attribute__((export_name("web_line"))) char *WebLine(void)
{
  return Line;
} /* end of WebLine */

/* WebInterpret: Interprets the line the page wrote, ended by a 0. */
__attribute__((export_name("web_interpret"))) void WebInterpret(void)
{
  Line[FORTH_LINE] = 0;
  LineStart = ForthNow();
  Note(TRACE_LINE,0,0);
  ForthInterpret(Line);
  (void)ForthNow();
  Note(TRACE_LINE_END,0,0);
  Drain();
  Flush();
  Called();
} /* end of WebInterpret */

/* WebTrace, WebTraceHead: The ring of the trace, TRACE pairs of words, and the count of
** pairs ever written. */
__attribute__((export_name("web_trace"))) uint32_t *WebTrace(void)
{
  return &Trace[0][0];
} /* end of WebTrace */

__attribute__((export_name("web_trace_head"))) uint32_t WebTraceHead(void)
{
  return TraceHead;
} /* end of WebTraceHead */

/* WebClock: The kernel's clock, in us modulo 2^30, the trace's. */
__attribute__((export_name("web_clock"))) uint32_t WebClock(void)
{
  return WebKernelClock();
} /* end of WebClock */

/* WebView: The pool as the page draws it, read at the console between two of its runs:
** the load admitted and its bound, in parts per million, then for each entry its state,
** error, name and its length, runs, longest time, runs over budget, worst case admitted
** and budget, in us, and the text of its error and its length. */
#define VIEW_ENTRY 11
static uint32_t View[2 + SLOTS * VIEW_ENTRY];

__attribute__((export_name("web_view"))) uint32_t *WebView(void)
{
  int i;
  View[0] = SlotsLoad();
  View[1] = SLOT_BOUND;
  for (i = 0; i < SLOTS; i += 1) {
     uint32_t *e = &View[2 + i * VIEW_ENTRY];
     const char *text;
     SlotView v;
     SlotsView(i,&v);
     text = ForthErrorText(v.Error);
     e[0] = v.State;
     e[1] = v.Error;
     e[2] = v.Bound != NULL ? (uint32_t)(uintptr_t)v.Bound->Name : 0;
     e[3] = v.Bound != NULL ? v.Bound->Length : 0;
     e[4] = v.Runs;
     e[5] = v.MaxTime;
     e[6] = v.Excess;
     e[7] = v.Wcet;
     e[8] = v.Budget;
     e[9] = (uint32_t)(uintptr_t)text;
     for (e[10] = 0; text[e[10]] != 0; e[10] += 1);
  }
  return View;
} /* end of WebView */
