/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File link.h: The console over Bluetooth LE, on the Pico W (link.c). As with usb.h, a
** program that is connected and has turned on notifications holds the console. */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>

/* LinkStart: Sets up BTstack and starts advertising, once RadioStart has loaded the radio.
** Returns 0, or -1 if BTstack could not start. */
int LinkStart(void);
/* LinkPoll: Runs in core 1's interrupt, each ms. Passes the bytes it received to
** received. */
void LinkPoll(void (*received)(char));
/* LinkActive: Whether a program is connected and takes the console's output. */
int LinkActive(void);
/* LinkDrop: Closes the connection, in core 1's interrupt: the console now belongs to
** another program. */
void LinkDrop(void);
/* LinkRoom: How many bytes LinkSend takes now without waiting. */
uint32_t LinkRoom(void);
/* LinkSend: Sends a byte to the program. On core 1 outside its interrupt, it waits for
** room. The wait ends if the program is gone or has stopped reading, and the byte is
** dropped. */
void LinkSend(char c);

#endif
