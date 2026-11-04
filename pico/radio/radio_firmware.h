/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File radio_firmware.h: The radio's firmware for the CYW43 driver, where it lies in the
** flash: wb43439A0_7_95_49_00_combined.h of lib/cyw43-driver/firmware, its bytes as they
** are (tools/radio_firmware.py), at RADIO_FIRMWARE. The image runs from SRAM, too small to
** hold its 232 KB too; the driver reads it once, through the XIP, at the radio's start. */
#ifndef RADIO_FIRMWARE_H
#define RADIO_FIRMWARE_H

#define RADIO_FIRMWARE      0x10100000u      /* 1 MB into the flash */
#include "radio_lengths.h"   /* CYW43_WIFI_FW_LEN, CYW43_CLM_LEN, of the same header */
#define fw_data             ((uintptr_t)RADIO_FIRMWARE)

#endif
