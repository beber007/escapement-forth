/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File kept.ld: Keeps the radio's state, its driver's and BTstack's, out of .data and .bss.
** A restart of core 0 puts .data back and clears .bss (restart.c), but core 1's interrupt
** goes on using this state meanwhile. The state is loaded with the image. Only a cold
** start clears it (RadioClear). This script is added to the kernel's script. Its sections
** go just before the sections they would fall in, in the same memory: this script comes
** before the kernel's on the command line, and it names no memory region. */
SECTIONS
{
    .radio_data :
    {
        . = ALIGN(4);
        *radio.a:*(.data .data.*)
        . = ALIGN(4);
    }
}
INSERT BEFORE .data;

SECTIONS
{
    .radio_bss (NOLOAD) :
    {
        . = ALIGN(4);
        _radio_bss = .;
        *radio.a:*(.bss .bss.* COMMON)
        . = ALIGN(4);
        _eradio_bss = .;
    }
}
INSERT BEFORE .bss;
