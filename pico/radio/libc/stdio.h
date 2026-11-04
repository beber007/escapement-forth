/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File stdio.h: No output: what the radio's code would print goes nowhere. */
#ifndef RADIO_STDIO_H
#define RADIO_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define printf(...) ((void)0)
/* vsnprintf: An empty string: only BTstack's logs and names for them call it (libc.c). */
int vsnprintf(char *to, size_t size, const char *format, va_list arguments);
#endif
