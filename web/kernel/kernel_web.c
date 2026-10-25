/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File kernel_web.c: The layer the kernel expects from a target, in the page, after the
** host build of Escapement (test/host/host_port.c).
**
** The kernel is the board's, EscapementHard.c, compiled unchanged. Its clock is a
** variable in us, which follows the browser's: each time the Forth reads the time with
** interrupts unmasked (WebKernelSync), the clock moves on by the time passed, stopping at
** each instant the hardware would interrupt, the comparator's or the 2^30 wraparound.
** There the timer's handler runs, and the tasks it elects are called on top of the one
** it interrupted, the console or a slot of a longer period, until that one is at the head
** of the ready queue again: one stack, as on the board.
**
** The browser runs the worker every few ms at best, seconds apart in a tab left in the
** background. The time it held the worker the clock passes event by event, each release
** served in its turn, late on the wall but on time on the kernel's clock; past
** WEB_KERNEL_CATCH, the rest is not given at all.
*/

#include "Escapement.h"
#include "kernel_web.h"

#define TIME_LIMIT        0x40000000    /* 2^30, the wraparound of the kernel clock */
#define WEB_KERNEL_CATCH  200000u       /* us of wall time given at once, at most */

unsigned WebSoftTimerRequests = 0, WebSoftTimerCleared = 0;
BOOL WebMasked = TRUE;
unsigned WebKernelBroken = 0;

volatile BOOL _OSOverflowInterruptFlag = FALSE;
volatile BOOL _OSComparatorInterruptFlag = FALSE;

static INT32 Clock = 0;            /* the kernel clock, modulo 2^30 */
static INT32 ArmedDeadline = -1;   /* what _OSSetTimer last armed */
static BOOL OverflowPending = FALSE;
static BOOL Started = FALSE;
static uint32_t WallSeen;          /* the wall's time last given to the clock */

/* Mirror of the head of the task control block, as far as the port reads it, that of the
** hard kernel under EDF (EscapementHard.c), which keeps it private. */
typedef struct WebTCB {
  struct WebTCB *Next[2];
  UINT8 TaskState;
  INT32 NextArrivalTimeLow;
  void (*TaskCodePtr)(void *);
  void *Argument;
} WebTCB;

#define STATE_RUNNING 0x01

extern WebTCB *_OSActiveTask;
extern BOOL _OSNoSaveContext;
extern void _OSTimerInterruptHandler(void);
static WebTCB *IdleTCB;


void _OSInitializeTimer(void) { }

void _OSStartTimer(void)
{
  Clock = 0;
  ArmedDeadline = -1;
  OverflowPending = FALSE;
  Started = TRUE;
}

INT32 _OSGetActualTime(void) { return Clock; }

uint32_t WebKernelClock(void) { return (uint32_t)Clock; }

BOOL _OSTimerIsOverflow(INT32 shiftTimeLimit)
{
  (void)shiftTimeLimit;
  if (OverflowPending) {
     OverflowPending = FALSE;
     _OSOverflowInterruptFlag = FALSE;
     return TRUE;
  }
  return FALSE;
}

BOOL _OSSetTimer(INT32 nextArrivalTime)
{
  ArmedDeadline = nextArrivalTime;
  return nextArrivalTime > Clock;
}

/* TicksToNextEvent: How far the clock moves before the hardware would interrupt: the
** deadline armed, or else the wraparound. */
static INT32 TicksToNextEvent(void)
{
  if (ArmedDeadline > Clock && ArmedDeadline < TIME_LIMIT)
     return ArmedDeadline - Clock;
  return TIME_LIMIT - Clock;
}

/* AdvanceBy: Moves the clock on, wrapping at 2^30 as the counter does, the flags raised
** as the target's interrupts raise them. */
static void AdvanceBy(INT32 delta)
{
  Clock += delta;
  if (ArmedDeadline >= 0 && ArmedDeadline < TIME_LIMIT && Clock >= ArmedDeadline)
     _OSComparatorInterruptFlag = TRUE;
  if (Clock >= TIME_LIMIT) {
     Clock -= TIME_LIMIT;
     OverflowPending = TRUE;
     _OSOverflowInterruptFlag = TRUE;
     ArmedDeadline = -1;
  }
}

/* RunElected: Calls each task the kernel elects, each to its end, the soft timer's
** interrupts its end raised served, until the one interrupted, or the idle task, is at
** the head of the ready queue again. */
static void RunElected(WebTCB *interrupted)
{
  while (TRUE) {
     WebTCB *task;
     while (WebSoftTimerCleared != WebSoftTimerRequests) {
        WebSoftTimerCleared = WebSoftTimerRequests;
        _OSTimerInterruptHandler();
     }
     task = _OSActiveTask;
     if (task == NULL || task == IdleTCB || task == interrupted)
        return;
     if (task->TaskState & STATE_RUNNING) {   /* below on the stack, not ended */
        WebKernelBroken += 1;
        return;
     }
     task->TaskState |= STATE_RUNNING;        /* as the context switch does */
     task->TaskCodePtr(task->Argument);
     _OSNoSaveContext = FALSE;                /* cleared by the switch that follows */
     if (_OSActiveTask == task)               /* the task did not end */
        return;
  }
}

void WebKernelBoot(uint32_t wall)
{
  (void)OSStartMultitasking(NULL,NULL);
  IdleTCB = _OSActiveTask;
  _OSNoSaveContext = FALSE;
  IdleTCB->TaskCodePtr(NULL);   /* starts the timer */
  WallSeen = wall;
  _OSComparatorInterruptFlag = TRUE;   /* the arrivals at time zero */
  WebKernelSync(wall);
}

/* WebKernelSync: The time passed since the clock was last given some is this call's
** alone: the calls inside the tasks it runs give the clock what passed since, as they
** run, and not this, or the clock would pass the time the browser held the page while a
** task runs, faster than the wall. A task of 1 ms then overran at each release behind a
** slot of 10 ms that its own releases had made late, under EDF ahead of it. */
void WebKernelSync(uint32_t wall)
{
  uint32_t pending;
  if (!Started || WebMasked)
     return;
  pending = wall - WallSeen;
  WallSeen = wall;
  if (pending > WEB_KERNEL_CATCH)
     pending = WEB_KERNEL_CATCH;
  while (TRUE) {
     if (_OSComparatorInterruptFlag || OverflowPending) {
        WebTCB *interrupted = _OSActiveTask;
        _OSTimerInterruptHandler();
        RunElected(interrupted);
        continue;
     }
     if (pending == 0)
        return;
     {
        INT32 step = TicksToNextEvent();
        if ((uint32_t)step > pending)
           step = (INT32)pending;
        pending -= (uint32_t)step;
        AdvanceBy(step);
     }
  }
}


/* Interrupt table, present only because the kernel refers to it. */
void *_OSTabDevice[OS_IO_NB_ENTRIES] = { 0 };
void OSSetISRDescriptor(UINT16 entry, void *descriptor) { _OSTabDevice[entry] = descriptor; }
void *OSGetISRDescriptor(UINT16 entry) { return _OSTabDevice[entry]; }
void _OSIOHandler(void) { }

/* Allocation from a heap of its own, never freed, as the target's. */
extern void *_OSStackBasePointer;
static UINT32 Heap[1024];
static UINT32 HeapUsed;
void *OSMalloc(UINT16 size)
{
  UINT32 words = ((UINT32)size + 3) / 4;
  if (_OSStackBasePointer == NULL)
     _OSStackBasePointer = &Heap[sizeof Heap / sizeof Heap[0]];
  if (words > sizeof Heap / sizeof Heap[0] - HeapUsed)
     return NULL;
  HeapUsed += words;
  return &Heap[HeapUsed - words];
}

/* Atomics: plain accesses, nothing coming between an LL and its SC. */
#define LL(a)    return *(a)
#define SC(a, v) do { *(a) = (v); return TRUE; } while (0)
UINT8  OSUINT8_LL(UINT8 *a)   { LL(a); }
BOOL   OSUINT8_SC(UINT8 *a, UINT8 v)   { SC(a, v); }
UINT16 OSUINT16_LL(UINT16 *a) { LL(a); }
BOOL   OSUINT16_SC(UINT16 *a, UINT16 v) { SC(a, v); }
INT16  OSINT16_LL(INT16 *a)   { LL(a); }
BOOL   OSINT16_SC(INT16 *a, INT16 v)   { SC(a, v); }
UINT32 OSUINT32_LL(UINT32 *a) { LL(a); }
BOOL   OSUINT32_SC(UINT32 *a, UINT32 v) { SC(a, v); }
INT32  OSINT32_LL(INT32 *a)   { LL(a); }
BOOL   OSINT32_SC(INT32 *a, INT32 v)   { SC(a, v); }
UINTPTR OSUINTPTR_LL(UINTPTR *a) { LL(a); }
BOOL   OSUINTPTR_SC(UINTPTR *a, UINTPTR v) { SC(a, v); }
