/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File Escapement_Processor.h: The processor of the page, kernel_web.c: one thread, no
** context switch. A task the kernel elects is called, and runs to its end; the timer's
** interrupt is taken where the Forth reads the time, the tasks it elects called there,
** on top of the one it interrupted, as on the single stack of the board. */

#ifndef ESCAPEMENT_PROCESSOR_H
#define ESCAPEMENT_PROCESSOR_H

#include "Escapement_Config.h"

/* Non-blocking algorithms mark a pointer with a bit the address space never uses. */
#define MARKEDBIT    0x80000000u
#define UNMARKEDBIT  0x7FFFFFFFu

extern unsigned WebSoftTimerRequests, WebSoftTimerCleared;
extern BOOL WebMasked;

#define _OSScheduleTask()               ((void)0)
#define _OSGenerateSoftTimerInterrupt() ((void)(WebSoftTimerRequests += 1))
#define _OSClearSoftTimerInterrupt()    ((void)(WebSoftTimerCleared = WebSoftTimerRequests))
#define _OSEnableInterrupts()           ((void)(WebMasked = FALSE))
#define _OSDisableInterrupts()          ((void)(WebMasked = TRUE))
#define _OSSleep()                      ((void)0)
#define _OSMemoryBarrier()              __asm volatile ("" ::: "memory")

/* The assembler context switch, and the offsets it assumes, do not exist here. */
#define OSCheckTCBLayout() struct OSCheckTCBLayoutNotApplicable

/* Load-linked / store-conditional pairs: nothing comes between the two here, an
** interrupt being taken only where the Forth reads the time. */
UINT8  OSUINT8_LL(UINT8 *addr);
BOOL   OSUINT8_SC(UINT8 *addr, UINT8 value);
UINT16 OSUINT16_LL(UINT16 *addr);
BOOL   OSUINT16_SC(UINT16 *addr, UINT16 value);
INT16  OSINT16_LL(INT16 *addr);
BOOL   OSINT16_SC(INT16 *addr, INT16 value);
UINT32 OSUINT32_LL(UINT32 *addr);
BOOL   OSUINT32_SC(UINT32 *addr, UINT32 value);
INT32  OSINT32_LL(INT32 *addr);
BOOL   OSINT32_SC(INT32 *addr, INT32 value);
UINTPTR OSUINTPTR_LL(UINTPTR *addr);
BOOL   OSUINTPTR_SC(UINTPTR *addr, UINTPTR value);

void _OSIOHandler(void);
void *OSMalloc(UINT16 size);

#endif /* ESCAPEMENT_PROCESSOR_H */
