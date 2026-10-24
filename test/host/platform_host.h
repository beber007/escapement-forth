/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File platform_host.h: What the tests read of the host's platform. */

#ifndef PLATFORM_HOST_H
#define PLATFORM_HOST_H

#include <stdint.h>

extern char Output[4096];
extern int OutputLength;
extern uint32_t Clock;

#endif /* PLATFORM_HOST_H */
