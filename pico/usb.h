/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File usb.h: The console's serial port on the Pico's own USB (usb.c). */
#ifndef USB_H
#define USB_H

#include <stdint.h>

/* UsbInit: At a cold start, before the tick. */
void UsbInit(void);
/* UsbPoll: In core 1's interrupt, each ms. Gives what it received to received. */
void UsbPoll(void (*received)(char));
/* UsbActive: Whether a program has the port open: the console then goes to it, not UART0. */
int UsbActive(void);
/* UsbRoom: The bytes UsbSend takes now without waiting. */
uint32_t UsbRoom(void);
/* UsbSend: A byte for the port. It waits for room, on core 1 outside its interrupt. A
** program that stopped reading ends the wait: the console must go on, so bytes are dropped
** until there is room again. */
void UsbSend(char c);

#endif
