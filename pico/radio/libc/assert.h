/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File assert.h: Checks dropped, as in a build with NDEBUG. */
#ifndef RADIO_ASSERT_H
#define RADIO_ASSERT_H
#define assert(x) ((void)0)
#define static_assert _Static_assert
#endif
