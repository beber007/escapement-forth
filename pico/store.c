/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File store.c: The store of the dictionary SAVE keeps (forth_platform.h): the last
** 16 KB of the Pico's 2 MB of flash, written through the bootrom's functions (RP2040
** datasheet 2.8.3.1.3), read through the XIP.
**
** The image runs from SRAM, and so does all it calls while the flash leaves the XIP: no
** code of its own is there. A word that reads the XIP meanwhile, on core 0, which goes
** on, must not: ForthMemory refuses the XIP while Storing, but one checked just before
** may still meet it. Erased then written, the store holds no header until
** its last page: a reset in between leaves it empty, as a cold start sees it.
**
** Platform version: RP2040 (Raspberry Pi Pico).
*/

#include "Escapement.h"
#include "forth.h"
#include "forth_platform.h"

#define STORE_OFFSET  0x1FC000u          /* in the flash: its last 16 KB */
#define STORE_ROOM    0x4000u
#define XIP_BASE      0x10000000u
#define PAGE          256u               /* what the flash programs at once */
#define SECTOR        4096u              /* and erases */
#define SECTOR_ERASE  0x20u

#define ROM_FUNCTIONS  0x14              /* halves: the bootrom's table, its lookup */
#define ROM_LOOKUP     0x18

typedef void *(*Lookup)(UINT16 *table, UINT32 code);
typedef void (*Plain)(void);
typedef void (*Erase)(UINT32 offset, size_t count, UINT32 block, UINT8 command);
typedef void (*Program)(UINT32 offset, const UINT8 *data, size_t count);

/* What SAVE stores at most, its header of six words first, fits. */
typedef char Fits[24 + FORTH_WORDS * sizeof(Word) + FORTH_CODE * sizeof(Cell) + FORTH_NAMES
                  <= STORE_ROOM ? 1 : -1];

volatile int Storing FORTH_KEEP;   /* kept, as the buffers, across a restart of core 0 */

/* In .noinit: a restart of core 0 as core 1 writes the store clears the .bss. */
static UINT8 First[PAGE] FORTH_KEEP, Page[PAGE] FORTH_KEEP;

/* Half: Through a register gcc does not see into: an address this low, to it, is one
** past a null pointer's object (-Warray-bounds). */
static UINT32 Half(UINT32 address)
{
  const volatile UINT16 *p;
  __asm volatile ("" : "=r" (p) : "0" (address));
  return *p;
} /* end of Half */

static void *Rom(char a, char b)
{
  Lookup lookup = (Lookup)(uintptr_t)Half(ROM_LOOKUP);
  return lookup((UINT16 *)(uintptr_t)Half(ROM_FUNCTIONS),
                (UINT32)(UINT8)a | (UINT32)(UINT8)b << 8);
} /* end of Rom */

/* Leave, Enter: The flash out of the XIP, for the bootrom to write it, then back in it,
** its cache flushed, in the bootrom's slow mode: a cold start loaded by the debugger has
** no XIP set up before. */
static void Leave(void)
{
  ((Plain)Rom('I','F'))();
  ((Plain)Rom('E','X'))();
} /* end of Leave */

static void Enter(void)
{
  ((Plain)Rom('F','C'))();
  ((Plain)Rom('C','X'))();
} /* end of Enter */

/* ForthBuild: The sum of the image's code and constants, from its start in SRAM to
** _etext: some 24 KB, 3.3 ms at 125 MHz (gcc makes the multiplication shifts and adds
** on the M0+), at each SAVE and at a cold start that finds a store. */
uint32_t ForthBuild(void)
{
  extern UINT8 _etext;
  const UINT8 *p = (const UINT8 *)0x20000000u;
  UINT32 sum = 2166136261u;
  for (; p < &_etext; p += 1)
     sum = (sum ^ *p) * 16777619u;
  return sum;
} /* end of ForthBuild */

const void *ForthStored(size_t *room)
{
  Leave();   /* at a cold start alone, by ForthInit */
  Enter();
  Storing = 0;
  *room = STORE_ROOM;
  return (const void *)(XIP_BASE + STORE_OFFSET);
} /* end of ForthStored */

/* Put: A byte into the page it falls in, the first kept for last, each other programmed
** once full; the sum of what is programmed kept, the first page's apart. */
static void Put(size_t at, UINT8 byte, UINT32 sum[2])
{
  UINT8 *page = at < PAGE ? First : Page;
  page[at % PAGE] = byte;
  sum[at >= PAGE] = (sum[at >= PAGE] ^ byte) * 16777619u;
  if (at >= PAGE && at % PAGE == PAGE - 1)
     ((Program)Rom('R','P'))(STORE_OFFSET + at - (PAGE - 1),Page,PAGE);
} /* end of Put */

/* Read: The sum of the store's bytes from to to, through the XIP. */
static UINT32 Read(size_t from, size_t to)
{
  const UINT8 *stored = (const UINT8 *)(XIP_BASE + STORE_OFFSET);
  UINT32 sum = 2166136261u;
  for (; from < to; from += 1)
     sum = (sum ^ stored[from]) * 16777619u;
  return sum;
} /* end of Read */

/* ForthStore: The pages after the first programmed and read back, then the first, which
** holds the header, and it read back: a store read back wrong has its first sector erased
** again, and no header (a review, 2026-10-24). Emptied, the header's room read back. */
int ForthStore(const void *const part[], const size_t size[], int parts)
{
  UINT32 sum[2] = {2166136261u, 2166136261u};
  size_t total = 0, at = 0, i, j, first;
  int k, stored;
  for (k = 0; k < parts; k += 1)
     total += size[k];
  if (total > STORE_ROOM)
     return 0;
  first = total < PAGE ? total : PAGE;
  Storing = 1;
  ForthBarrier();
  Leave();
  ((Erase)Rom('R','E'))(STORE_OFFSET,total == 0 ? SECTOR : (total + SECTOR - 1) / SECTOR * SECTOR,
                        SECTOR,SECTOR_ERASE);
  /* A word at a time where the part is of words: a variable a slot writes meanwhile is
  ** stored whole, as it was before or after. */
  for (k = 0; k < parts; k += 1) {
     const UINT8 *from = part[k];
     for (i = 0; i < size[k]; ) {
        if ((uintptr_t)(from + i) % 4 == 0 && size[k] - i >= 4) {
           UINT32 word = *(const volatile UINT32 *)(from + i);
           for (j = 0; j < 4; j += 1)
              Put(at++,(UINT8)(word >> (8 * j)),sum);
           i += 4;
        }
        else
           Put(at++,from[i++],sum);
     }
  }
  if (at > PAGE && at % PAGE != 0) {
     for (j = at % PAGE; j < PAGE; j += 1)
        Page[j] = 0xFF;
     ((Program)Rom('R','P'))(STORE_OFFSET + at - at % PAGE,Page,PAGE);
  }
  Enter();
  stored = Read(PAGE < total ? PAGE : total,total) == sum[1];
  if (stored && total > 0) {
     for (j = first; j < PAGE; j += 1)
        First[j] = 0xFF;
     Leave();
     ((Program)Rom('R','P'))(STORE_OFFSET,First,PAGE);
     Enter();
     stored = Read(0,first) == sum[0];
     if (!stored) {
        Leave();
        ((Erase)Rom('R','E'))(STORE_OFFSET,SECTOR,SECTOR,SECTOR_ERASE);
        Enter();
     }
  }
  else if (stored)   /* emptied: the header's room erased */
     for (i = 0; i < PAGE; i += 1)
        stored &= ((const UINT8 *)(XIP_BASE + STORE_OFFSET))[i] == 0xFF;
  ForthBarrier();
  Storing = 0;
  return stored;
} /* end of ForthStore */
