/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File Escapement_Timer.h: The timer the kernel expects, kernel_web.c's. */

#ifndef ESCAPEMENT_TIMER_H
#define ESCAPEMENT_TIMER_H

void _OSInitializeTimer(void);
void _OSStartTimer(void);
INT32 _OSGetActualTime(void);
BOOL _OSTimerIsOverflow(INT32 shiftTimeLimit);
BOOL _OSSetTimer(INT32 nextArrivalTime);

#endif /* ESCAPEMENT_TIMER_H */
