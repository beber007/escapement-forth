/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File forth_platform.h: What the Forth asks of the platform it runs on: the host for
** the tests, the Pico for the board.
*/

#ifndef FORTH_PLATFORM_H
#define FORTH_PLATFORM_H

#include <stdint.h>

/* ForthEmit: Sends a byte to the console. Called from either core on the board: from a
** slot's task it must not wait. */
void ForthEmit(char c);

/* ForthNow: The time in microseconds, modulo 2^32. */
uint32_t ForthNow(void);

/* ForthBarrier: Orders the stores before it to memory before those after it, for the
** other core: a word compiled before the store that binds it. */
void ForthBarrier(void);

/* ForthPlatformWords: Adds the words of the platform, the pins of the board, to the
** dictionary, through ForthAddWord. */
void ForthPlatformWords(void);

#endif /* FORTH_PLATFORM_H */
