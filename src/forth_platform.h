/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File forth_platform.h: What the Forth asks of the platform it runs on: the host for
** the tests, the Pico for the board.
*/

#ifndef FORTH_PLATFORM_H
#define FORTH_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* ForthEmit: Sends a byte to the console. Called from either core on the board: from a
** slot's task it must not wait. */
void ForthEmit(char c);

/* ForthNow: The time in microseconds, modulo 2^32. */
uint32_t ForthNow(void);

/* ForthBarrier: Orders the stores before it to memory before those after it, for the
** other core: a word compiled before the store that binds it. */
void ForthBarrier(void);

/* ForthMemory: Whether the size bytes at address may be read, or written, besides the
** Forth's own memory, which @ ! +! c@ c! reach without asking: what is there, and does
** not fault on the board. */
int ForthMemory(uintptr_t address, size_t size, int write);

/* ForthBreak: Whether a break was asked at the console since it was last asked, Ctrl-C
** on the board: the word the console runs then stops, FORTH_BREAK. */
int ForthBreak(void);

/* ForthStored, ForthStore: The store that keeps a saved dictionary across a reset, the
** flash on the board: ForthStored gives where it reads, and room, the bytes it holds;
** ForthStore writes the parts one after the other in place of what it held, or with no
** part empties it, and gives FALSE, 0, if it could not. The first part, the header,
** goes in last, and a part of words a word at a time: the other core may write a cell
** meanwhile. Called on the console's core, and ForthStored at a cold start, by
** ForthInit. */
const void *ForthStored(size_t *room);
int ForthStore(const void *const part[], const size_t size[], int parts);

/* ForthBuild: What tells this firmware from another of the same addresses, which the
** stamp of a store holds: on the board a sum of its code. */
uint32_t ForthBuild(void);

/* ForthMask, ForthUnmask: Hold the interrupts of the core that calls, a slot's task
** not preempted in between, then let them be as ForthMask found them. */
uint32_t ForthMask(void);
void ForthUnmask(uint32_t mask);

/* ForthPlatformWords: Adds the words of the platform, the pins of the board, to the
** dictionary, through ForthAddWord. */
void ForthPlatformWords(void);

#endif /* FORTH_PLATFORM_H */
