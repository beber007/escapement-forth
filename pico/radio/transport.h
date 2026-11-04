/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File transport.h: BTstack's HCI transport over the CYW43 (transport.c). */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "hci_transport.h"

const hci_transport_t *TransportCYW43(void);
/* TransportPoll: Gives BTstack up to most packets that the radio has for the host. Returns
** how many it gave. */
int TransportPoll(int most);

#endif
