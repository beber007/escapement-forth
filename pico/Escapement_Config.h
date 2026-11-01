/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File Escapement_Config.h: The kernel the pool of slots runs on, for a Raspberry Pi Pico,
** or a Pico 2 with make BOARD=pico2:
** the hard kernel under EDF, as the costs of the pool were measured on (issue #40 of
** Escapement, step 1). The values are the examples' of Escapement.
*/

#ifndef ESCAPEMENT_CONFIG_H_
#define ESCAPEMENT_CONFIG_H_

#define SCHEDULER_REAL_TIME_MODE EARLIEST_DEADLINE_FIRST

#ifdef PICO2
   #define RP2350
   #define CORTEX_M33
#else
   #define RP2040
   #define CORTEX_M0
#endif

#define ESCAPEMENT_VERSION_HARD

#define OSMALLOC_INTERNAL_HEAP_SIZE  2048

#ifdef PICO2
   /* All 4 bits of the 16 levels for preemption: SysTick at 14, PendSV at 15. */
   #define PRIGROUP  (UINT32)3
#else
   /* PendSV at the lowest of the 4 levels, SysTick above it. */
   #define LOWEST_PRIORITY_LEVEL  3
#endif
/* The timer at the highest. */
#define TIMER_PRIORITY  (UINT8)0

#endif /* ESCAPEMENT_CONFIG_H_ */
