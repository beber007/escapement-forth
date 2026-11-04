/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File link.c: The console over Bluetooth LE on the Pico W. BTstack runs as a peripheral
** with the Nordic UART service (console.gatt). A web page reaches it with Web Bluetooth.
**
** Once LinkStart is done, BTstack runs only in core 1's 1 ms interrupt. There, its own run
** loop polls the radio, runs BTstack's timers and callbacks, and sends what the console
** wrote into Out. The console's loop runs outside the interrupt and only fills Out.
**
** Platform version: RP2040 (Raspberry Pi Pico W).
*/

#include "btstack_config.h"
#include "btstack_run_loop.h"
#include "btstack_memory.h"
#include "btstack_event.h"
#include "bluetooth_data_types.h"
#include "hci.h"
#include "l2cap.h"
#include "gap.h"
#include "ble/att_server.h"
#include "ble/sm.h"
#include "radio.h"
#include "cyw43.h"
#include "ble/le_device_db.h"
#include "ble/gatt-service/nordic_spp_service_server.h"
#include "console_gatt.h"
#include "transport.h"
#include "link.h"
#include "forth_platform.h"
#include "chip.h"

#define TIMERAWL     (*(volatile uint32_t *)(CHIP_TIMER + 0x28))
#define OUT          2048            /* a power of 2 */
#define PACKETS      4               /* at most this many radio packets read each ms */
#define WAIT_US      2000000         /* LinkSend's wait with nothing sent: the link's supervision timeout */

static volatile int Started, Notified, Stopped;
static uint32_t Ms, MsAt;   /* LoopTime's ms, and the us it last counted to */
static hci_con_handle_t Con = HCI_CON_HANDLE_INVALID;
static uint8_t Out[OUT];
static volatile uint32_t OutHead, OutTail;
static int Asked;
static int More;   /* the last read may have left packets: read again */
static void (*Received)(char);
static btstack_context_callback_registration_t Request;
static btstack_packet_callback_registration_t Events;

/* ---- A run loop for the interrupt. It is BTstack's base loop, with its time in ms from
** the timer. */

/* LoopTime: Counts ms that wrap at 2^32, as BTstack expects. TIMERAWL / 1000 would wrap
** every 71.6 min. */
static uint32_t LoopTime(void)
{
  uint32_t ms = (TIMERAWL - MsAt) / 1000;
  Ms += ms;
  MsAt += ms * 1000;
  return Ms;
} /* end of LoopTime */

static void LoopSetTimer(btstack_timer_source_t *timer, uint32_t ms)
{
  timer->timeout = LoopTime() + ms + 1;   /* like BTstack's embedded loop: never early */
} /* end of LoopSetTimer */

static void LoopNothing(void)
{
} /* end of LoopNothing */

static void LoopOnMain(btstack_context_callback_registration_t *registration)
{
  btstack_run_loop_base_add_callback(registration);
} /* end of LoopOnMain */

static const btstack_run_loop_t Loop = {
   btstack_run_loop_base_init,
   btstack_run_loop_base_add_data_source,
   btstack_run_loop_base_remove_data_source,
   btstack_run_loop_base_enable_data_source_callbacks,
   btstack_run_loop_base_disable_data_source_callbacks,
   LoopSetTimer,
   btstack_run_loop_base_add_timer,
   btstack_run_loop_base_remove_timer,
   LoopNothing,
   btstack_run_loop_base_dump_timer,
   LoopTime,
   LoopNothing,
   LoopOnMain,
   LoopNothing,
};

/* ---- The service. */

/* CanSend: Sends as much of Out as one notification holds. LinkPoll asks to send more at
** the next tick. */
static void CanSend(void *context)
{
  uint8_t chunk[244];
  uint32_t tail = OutTail, n = OutHead - tail, room = att_server_get_mtu(Con) - 3u, i;
  (void)context;
  Asked = 0;
  if (Con == HCI_CON_HANDLE_INVALID || !Notified)
     return;
  if (room > sizeof chunk)
     room = sizeof chunk;
  if (n > room)
     n = room;
  for (i = 0; i < n; i += 1)
     chunk[i] = Out[(tail + i) % OUT];
  if (n > 0 && nordic_spp_service_server_send(Con,chunk,(uint16_t)n) == 0)
     OutTail = tail + n;
} /* end of CanSend */

static void Service(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  uint16_t i;
  (void)channel;
  if (type == RFCOMM_DATA_PACKET) {
     for (i = 0; i < size; i += 1)
        Received((char)packet[i]);
     return;
  }
  if (type != HCI_EVENT_PACKET || hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META)
     return;
  switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
     case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
        Con = gattservice_subevent_spp_service_connected_get_con_handle(packet);
        OutTail = OutHead;   /* what was written before this connection is not for it */
        Stopped = 0;
        Notified = 1;
        break;
     case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
        Notified = 0;
        break;
     default:
        break;
  }
} /* end of Service */

static void Event(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  (void)channel;
  (void)size;
  if (type != HCI_EVENT_PACKET)
     return;
  switch (hci_event_packet_get_type(packet)) {
     case HCI_EVENT_DISCONNECTION_COMPLETE:
        Con = HCI_CON_HANDLE_INVALID;
        Notified = 0;
        Asked = 0;
        break;
     case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
           case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
              if (hci_subevent_le_connection_complete_get_status(packet) != ERROR_CODE_SUCCESS)
                 break;
              /* 15 ms, the shortest interval Apple accepts: a page's request and its
              ** answer take under 50 ms. */
              gap_request_connection_parameter_update(
                 hci_subevent_le_connection_complete_get_connection_handle(packet),12,12,0,200);
              break;
           default:
              break;
        }
        break;
     default:
        break;
  }
} /* end of Event */

/* The advertisement carries the service, which a page asks for. The scan response carries
** the name. */
static const uint8_t Advertised[] = {
   2, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
   17, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
   0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E,
};
static const uint8_t Named[] = {
   17, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
   'E', 's', 'c', 'a', 'p', 'e', 'm', 'e', 'n', 't', ' ', 'F', 'o', 'r', 't', 'h',
};

int LinkStart(void)
{
  bd_addr_t none = {0};
  MsAt = TIMERAWL;
  btstack_memory_init();
  btstack_run_loop_init(&Loop);
  hci_init(TransportCYW43(),NULL);
  l2cap_init();
  le_device_db_init();
  sm_init();
  att_server_init(profile_data,NULL,NULL);
  nordic_spp_service_server_init(Service);
  Events.callback = Event;
  hci_add_event_handler(&Events);
  Request.callback = CanSend;
  gap_advertisements_set_params(0x0030,0x0030,0,0,none,0x07,0);
  gap_advertisements_set_data(sizeof Advertised,(uint8_t *)Advertised);
  gap_scan_response_set_data(sizeof Named,(uint8_t *)Named);
  gap_advertisements_enable(1);
  if (hci_power_control(HCI_POWER_ON) != 0)   /* the transport did not open: no BTstack */
     return -1;
  Started = 1;
  return 0;
} /* end of LinkStart */

void LinkPoll(void (*received)(char))
{
  if (!Started || RadioFailed != NULL)   /* the radio is off: a read would wait 300 ms for it */
     return;
  Received = received;
  /* The radio raises its host wake line when it needs the host. Between transfers, this
  ** line is the data line. The radio's status tells why, and a read clears it. Packets for
  ** Bluetooth are read until none are left. */
  if (More || (cyw43_hal_pin_read(CYW43_PIN_WL_HOST_WAKE) &&
               cyw43_ll_bt_has_work(&cyw43_state.cyw43_ll)))
     More = TransportPoll(PACKETS) == PACKETS;
  if (RadioFailed != NULL)   /* the read failed: BTstack would write to the radio next */
     return;
  btstack_run_loop_base_execute_callbacks();
  btstack_run_loop_base_process_timers(LoopTime());
  if (Notified && !Asked && OutHead != OutTail) {
     /* Asked is set first: CanSend may run within the call, and it clears Asked. With no
     ** connection, CanSend never runs. Asked is then cleared here, and the request comes
     ** again at the next tick. */
     Asked = 1;
     if (att_server_request_to_send_notification(&Request,Con) ==
         ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER)
        Asked = 0;
  }
} /* end of LinkPoll */

int LinkActive(void)
{
  return Notified && RadioFailed == NULL;   /* the radio is off: no disconnection will come */
} /* end of LinkActive */

void LinkDrop(void)
{
  if (Con != HCI_CON_HANDLE_INVALID && Notified) {
     Notified = 0;   /* at once: the console stays on the USB until the disconnection */
     gap_disconnect(Con);
  }
} /* end of LinkDrop */

uint32_t LinkRoom(void)
{
  return OUT - (OutHead - OutTail);
} /* end of LinkRoom */

/* Core 1's interrupt also sends the trace here. So the interrupt is held off while the
** room is checked and the byte is put. Otherwise, a byte sent from outside the interrupt
** could write OutHead back over the interrupt's events. */
void LinkSend(char c)
{
  uint32_t start = TIMERAWL, tail = OutTail, mask;
  for (;;) {
     mask = ForthMask();
     if (OutHead - OutTail < OUT) {
        Out[OutHead % OUT] = (uint8_t)c;
        OutHead += 1;
        Stopped = 0;
        ForthUnmask(mask);
        return;
     }
     ForthUnmask(mask);
     if (OutTail != tail) {
        tail = OutTail;
        start = TIMERAWL;
     }
     if (Stopped || !LinkActive() || TIMERAWL - start > WAIT_US) {
        Stopped = 1;   /* the next bytes are dropped at once, without a 2 s wait each */
        return;
     }
  }
} /* end of LinkSend */
