/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* The flash of a Pico 2 with no probe: the table with its block, then the copier and the
** image (flash_boot.S). */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 4080K   /* the last 16 KB: a saved dictionary */
}

SECTIONS
{
    .text : { KEEP(*(.vectors)) *(.copy) } > FLASH
}
