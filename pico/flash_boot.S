/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File flash_boot.S: What starts the SRAM image from the flash, for a Pico with no probe.
**
** The second stage of the boot (Escapement_Boot2.S) enters here through this table. This
** copies the image, as it is, to the SRAM it is linked for, and starts it where a debugger
** does (Escapement_RamEntry.S). So the image runs as it does when loaded over SWD: nothing
** of it reads the flash but a saved dictionary (store.c), which stays in the last 16 KB.
*/

    .syntax unified
    .cpu cortex-m0plus
    .thumb

    .section .vectors, "a"
    .word 0x20042000         /* a stack, for the table's sake: the copy uses none */
    .word Copy

    .section .copy, "ax"
    .thumb_func
Copy:
    ldr  r0, =Image
    ldr  r1, =0x20000000
    ldr  r2, =ImageEnd
1:
    ldmia r0!, {r3}
    stmia r1!, {r3}
    cmp  r0, r2
    blo  1b
    ldr  r0, =0x20000001
    bx   r0

    .ltorg
    .align 2
Image:
    .incbin IMAGE_BIN
    .align 2
ImageEnd:
