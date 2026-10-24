/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File restart.h: Core 1 restarts core 0 when the 1 ms slot stops beating (restart.c). */

#ifndef RESTART_H
#define RESTART_H

#include <stdint.h>

/* Beat: Counted by the 1 ms slot's task at each instance, watched by core 1. */
extern volatile uint32_t Beat;
extern volatile uint32_t SoftRestarts, HardRestarts, FailedRestarts, Starts;

/* RestartInit: On core 0, from main, before the kernel starts: the restarts' interrupts
** set up. Returns 1 when core 0 starts again after a restart, 0 at the first start. */
int RestartInit(void);

/* WatchInit, Watch: On core 1, at its start, then as often as it can: Watch restarts
** core 0 after DETECT us without a beat, and returns what happened when it sees the
** outcome, 0 otherwise. */
enum { RESTART_SOFT = 1, RESTART_HARD, RESTART_FAILED };
void WatchInit(void);
int Watch(void);

#endif /* RESTART_H */
