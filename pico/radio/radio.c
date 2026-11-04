/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File radio.c: The Pico W's radio on core 1. This file starts the radio and holds the
** functions that the CYW43 driver calls back. Wi-Fi is not used: its firmware is loaded
** because the Bluetooth part of the chip is reached through it.
**
** Platform version: RP2040 (Raspberry Pi Pico W).
*/

#include "cyw43.h"
#include "radio.h"

/* The driver allocates only a few buffers, to download the Bluetooth firmware. They come
** from a pool that empties once all are freed: the driver takes them and gives them back
** together. */
static uint32_t Pool[512];
static size_t Taken, Held;

void *cyw43_malloc(size_t size)
{
  void *p;
  size = (size + 3) / 4;
  if (Taken + size > sizeof Pool / sizeof Pool[0])
     return NULL;
  p = &Pool[Taken];
  Taken += size;
  Held += 1;
  return p;
} /* end of cyw43_malloc */

void cyw43_free(void *p)
{
  if (p != NULL && Held > 0 && --Held == 0)
     Taken = 0;
} /* end of cyw43_free */

/* The driver's calls for a network stack. There is no network stack here. */
void cyw43_cb_tcpip_init(cyw43_t *self, int itf) { (void)self; (void)itf; }
void cyw43_cb_tcpip_deinit(cyw43_t *self, int itf) { (void)self; (void)itf; }
void cyw43_cb_tcpip_set_link_up(cyw43_t *self, int itf) { (void)self; (void)itf; }
void cyw43_cb_tcpip_set_link_down(cyw43_t *self, int itf) { (void)self; (void)itf; }
void cyw43_cb_process_ethernet(void *cb_data, int itf, size_t len, const uint8_t *buf)
{
  (void)cb_data; (void)itf; (void)len; (void)buf;
}

const char *volatile RadioFailed;
const char *RadioSaid[RADIO_SAID];
int RadioSaidCount, RadioLine;

void RadioSays(const char *format, ...)
{
  if (RadioSaidCount < RADIO_SAID)
     RadioSaid[RadioSaidCount] = format;
  RadioSaidCount += 1;
} /* end of RadioSays */

int RadioFailedAt(int result, int line)
{
  if (RadioLine == 0)
     RadioLine = line;
  return result;
} /* end of RadioFailedAt */

/* cyw43_bluetooth_hci_process: The radio has Bluetooth data for the host. Only the driver's
** poll calls this function, and that poll never runs here: the link reads the radio each
** ms (link.c). */
void cyw43_bluetooth_hci_process(void)
{
} /* end of cyw43_bluetooth_hci_process */

void RadioClear(void)
{
  extern uint32_t _radio_bss, _eradio_bss;
  volatile uint32_t *p;   /* volatile: gcc must not turn this loop into memset */
  for (p = &_radio_bss; p < &_eradio_bss; p += 1)
     *p = 0;
} /* end of RadioClear */

void RadioPanic(const char *why)
{
  RadioFailed = why;
  cyw43_hal_pin_low(CYW43_PIN_WL_REG_ON);
} /* end of RadioPanic */

int RadioStart(void)
{
  cyw43_init(&cyw43_state);
  return cyw43_bluetooth_hci_init();
} /* end of RadioStart */
