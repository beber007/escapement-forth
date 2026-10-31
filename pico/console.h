/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File console.h: The console, on core 1. */

#ifndef CONSOLE_H
#define CONSOLE_H

/* ConsoleInit: On core 0, at the first start: what the console keeps across restarts of
** core 0, set. */
void ConsoleInit(void);

/* Console: Core 1's code: reads lines on UART0, has the Forth interpret them, and sends
** what the slots print; never returns. */
void Console(void);

/* TraceOn, TraceNote: What core 0 tells of a task's instance for the page to draw, while
** the page asked. On core 0 alone. A restart is told as events lost: what was running will
** not end. */
extern volatile uint32_t TraceOn;
enum { TRACE_START = 0, TRACE_END, TRACE_END_OVER, TRACE_LOST = 6 };
void TraceNote(uint32_t kind, uint32_t period);

#endif /* CONSOLE_H */
