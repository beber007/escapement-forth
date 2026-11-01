/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* The flash of a Pico with no probe: the second stage of the boot at its start, the table
** where that stage looks for it, then the copier and the image (flash_boot.S). */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2032K   /* the last 16 KB: a saved dictionary */
}

SECTIONS
{
    .boot2 : { KEEP(*(.boot2)) } > FLASH
    .text : { KEEP(*(.vectors)) *(.copy) } > FLASH
}

ASSERT(SIZEOF(.boot2) == 256, "the second stage must be 256 bytes, its CRC included")
ASSERT(ADDR(.text) == 0x10000100, "the table must follow the second stage")
