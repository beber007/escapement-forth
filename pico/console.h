/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File console.h: The console, on core 1. */

#ifndef CONSOLE_H
#define CONSOLE_H

/* Console: Core 1's code: reads lines on UART0, has the Forth interpret them, and sends
** what the slots print; never returns. */
void Console(void);

#endif /* CONSOLE_H */
