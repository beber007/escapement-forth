/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File radio.h: The Pico W's radio, on core 1 (radio.c). */
#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

/* RadioFailed: Why the driver gave up and turned the radio off. NULL while the radio
** works. */
extern const char *volatile RadioFailed;

/* RadioSaid: The first RADIO_SAID formats the driver printed, out of RadioSaidCount.
** RadioLine is the line where the driver first failed, in cyw43_ll.c or another of its
** files. */
#define RADIO_SAID 16
extern const char *RadioSaid[RADIO_SAID];
extern int RadioSaidCount, RadioLine;

/* RadioClear: Zeroes the radio's state, only at a cold start, before the console's first
** byte. A restart of core 0 keeps this state (kept.ld). */
void RadioClear(void);

/* RadioStart: Powers the radio and loads its Wi-Fi and Bluetooth firmware. It runs at a
** cold start, from the console's loop: core 1's interrupt leaves the radio alone until
** LinkStart (link.c). Returns 0, or the driver's error. */
int RadioStart(void);

#endif
