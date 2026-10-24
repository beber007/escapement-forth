/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File platform_host.h: What the tests read of the host's platform. */

#ifndef PLATFORM_HOST_H
#define PLATFORM_HOST_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

extern char Output[4096];
extern int OutputLength;
extern uint32_t Clock;
extern int Break;          /* a break asked, which ForthBreak takes */
extern jmp_buf Restart;     /* where STALL leaves a slot's run for */
extern unsigned char StoredBytes[65536];   /* the store, StoredRoom bytes of it */
extern size_t StoredRoom;
extern int StoreRuns;       /* the 1 ms slot run as ForthStore writes */
extern uint32_t HostBuild;  /* what ForthBuild gives */

#endif /* PLATFORM_HOST_H */
