/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File kernel_web.h: What the page's platform (platform_web.c) asks of the port. */

#ifndef KERNEL_WEB_H
#define KERNEL_WEB_H

#include <stdint.h>

/* WebKernelBoot: Starts the kernel once its tasks are created, as OSStartMultitasking
** does on the board; the idle task's first run starts the timer at wall, in us. */
void WebKernelBoot(uint32_t wall);

/* WebKernelSync: Gives the kernel's clock the time passed on the wall's, in us, and takes
** the timer's interrupts met on the way, the tasks they elect run before it returns.
** Held while interrupts are masked; at most WEB_KERNEL_CATCH is given at once. */
void WebKernelSync(uint32_t wall);

/* WebKernelClock: The kernel's clock, in us, modulo 2^30. */
uint32_t WebKernelClock(void);

/* WebKernelBroken: The nesting of the tasks found broken, 0 if never: a task elected
** while one below it on the stack had not ended, which EDF never does. */
extern unsigned WebKernelBroken;

#endif /* KERNEL_WEB_H */
