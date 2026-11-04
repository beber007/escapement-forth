/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File transport.c: BTstack's HCI transport over the CYW43's shared bus, and the chipset
** that sets its Bluetooth address. Core 1's interrupt polls the transport for reads
** (link.c). The pico-sdk reads from the radio's own interrupt instead.
**
** After the pico-sdk's pico_cyw43_driver/btstack_hci_transport_cyw43.c and
** btstack_chipset_cyw43.c, Copyright (c) 2023 Raspberry Pi (Trading) Ltd., BSD-3-Clause.
*/

#include "cyw43.h"
#include "btstack_config.h"
#include "hci.h"
#include "hci_transport.h"
#include "btstack_chipset.h"
#include "transport.h"
#include "radio.h"

#define HEADER 4   /* the CYW43's header before each HCI packet */
#define PRE    ((HCI_INCOMING_PRE_BUFFER_SIZE + 3) & ~3)

static void (*Handler)(uint8_t type, uint8_t *packet, uint16_t size);
/* 3 more bytes: the driver reads whole words, so it rounds a packet's length up to 4. */
static uint8_t Packet[PRE + HCI_INCOMING_PACKET_BUFFER_SIZE + 3] __attribute__((aligned(4)));
static uint8_t *const Received = &Packet[PRE - HEADER];
static int Ready;

static void SetAddress(bd_addr_t addr, uint8_t *command)
{
  command[0] = 0x01;
  command[1] = 0xFC;
  command[2] = 0x06;
  reverse_bd_addr(addr,&command[3]);
} /* end of SetAddress */

static const btstack_chipset_t Chipset = {
   .name = "CYW43",
   .init = NULL,
   .next_command = NULL,
   .set_baudrate_command = NULL,
   .set_bd_addr_command = SetAddress,
};

static void Init(const void *config)
{
  (void)config;
} /* end of Init */

/* Open: The firmware is already up (radio.c). cyw43_bluetooth_hci_init sees this, and
** only checks the bus. The address is the Wi-Fi MAC plus one. */
static int Open(void)
{
  bd_addr_t addr;
  int failed = cyw43_bluetooth_hci_init();
  if (failed)
     return failed;
  memcpy(addr,cyw43_state.mac,6);
  addr[BD_ADDR_LEN - 1] += 1;
  hci_set_chipset(&Chipset);
  hci_set_bd_addr(addr);
  Ready = 1;
  return 0;
} /* end of Open */

static int Close(void)
{
  Ready = 0;
  return 0;
} /* end of Close */

static void Register(void (*handler)(uint8_t type, uint8_t *packet, uint16_t size))
{
  Handler = handler;
} /* end of Register */

static int Send(uint8_t type, uint8_t *packet, int size)
{
  uint8_t *buffer = &packet[-HEADER];
  if (RadioFailed != NULL)   /* with the radio off, each access to it would wait 300 ms */
     return -1;
  buffer[3] = type;
  return cyw43_bluetooth_hci_write(buffer,(size_t)size + HEADER);
} /* end of Send */

static const hci_transport_t Transport = {
   "CYW43", Init, Open, Close, Register, NULL, Send, NULL, NULL, NULL,
};

const hci_transport_t *TransportCYW43(void)
{
  return &Transport;
} /* end of TransportCYW43 */

int TransportPoll(int most)
{
  int read = 0;
  uint32_t length;
  while (Ready && RadioFailed == NULL && read < most) {
     length = 0;
     if (cyw43_bluetooth_hci_read(Received,HEADER + HCI_INCOMING_PACKET_BUFFER_SIZE,&length) != 0 ||
         length <= HEADER)
        break;
     Handler(Received[3],&Received[HEADER],(uint16_t)(length - HEADER));
     read += 1;
  }
  return read;
} /* end of TransportPoll */
