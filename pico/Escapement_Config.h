/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File Escapement_Config.h: The kernel the pool of slots runs on, for a Raspberry Pi Pico:
** the hard kernel under EDF, as the costs of the pool were measured on (issue #40 of
** Escapement, step 1). The values are the examples' of Escapement.
*/

#ifndef ESCAPEMENT_CONFIG_H_
#define ESCAPEMENT_CONFIG_H_

#define SCHEDULER_REAL_TIME_MODE EARLIEST_DEADLINE_FIRST

#define RP2040
#define CORTEX_M0

#define ESCAPEMENT_VERSION_HARD

#define OSMALLOC_INTERNAL_HEAP_SIZE  2048

/* PendSV at the lowest of the 4 levels, SysTick above it, the timer at the highest. */
#define LOWEST_PRIORITY_LEVEL  3
#define TIMER_PRIORITY  (UINT8)0

#endif /* ESCAPEMENT_CONFIG_H_ */
