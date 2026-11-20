/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File chip_web.h: The registers of a Pico (RP2040) that the page models, for the I2C
** examples (examples/i2c.fs): what platform_web.c asks of chip_web.c. */

#ifndef CHIP_WEB_H
#define CHIP_WEB_H

#include <stdint.h>

/* ChipHas: Whether the 32-bit word at address is a register the page models. */
int ChipHas(uint32_t address);

/* ChipRead, ChipWrite: A read or a write of that register at now, in us, modulo 2^32:
** the I2C controllers and their devices brought up to now first. */
uint32_t ChipRead(uint32_t address, uint32_t now);
void ChipWrite(uint32_t address, uint32_t value, uint32_t now);

/* ChipScreen: What the display of port shows, 128 x 64 pixels, a byte each, 1 lit; and
** a count that changes with what it shows, alone if pixels is NULL; at now, in us. */
uint32_t ChipScreen(int port, uint8_t *pixels, uint32_t now);

#endif /* CHIP_WEB_H */
