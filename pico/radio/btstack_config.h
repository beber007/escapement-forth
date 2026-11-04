/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File btstack_config.h: BTstack's settings for the console over Bluetooth LE (link.c): a
** peripheral with one connection, the Nordic UART service and no pairing. Pools are
** static: BTstack takes no memory from a heap. After the pico-examples'
** btstack_config_common.h. */
#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

#define ENABLE_BLE
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_DATA_LENGTH_EXTENSION

#define HCI_OUTGOING_PRE_BUFFER_SIZE    4      /* the CYW43's header (transport.c) */
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT    4
#define HCI_ACL_PAYLOAD_SIZE            (251 + 4)
#define HCI_RESET_RESEND_TIMEOUT_MS     1000

#define MAX_NR_HCI_CONNECTIONS          1
#define MAX_NR_L2CAP_CHANNELS           1
#define MAX_NR_L2CAP_SERVICES           1
#define MAX_NR_GATT_CLIENTS             0
#define MAX_NR_SM_LOOKUP_ENTRIES        1
#define MAX_NR_WHITELIST_ENTRIES        1
#define MAX_NR_LE_DEVICE_DB_ENTRIES     1
#define MAX_NR_CONTROLLER_ACL_BUFFERS   3

#endif
