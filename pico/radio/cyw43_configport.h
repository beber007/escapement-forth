/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File cyw43_configport.h: What the CYW43 driver (lib/cyw43-driver) asks of its platform,
** for the Pico W with no pico-sdk: the radio's pins, the clock and the waits. The driver
** runs only on core 1. It runs from the console's loop until LinkStart, then only from
** core 1's 1 ms interrupt (link.c, Started). So it takes no lock.
**
** After the pico-sdk's pico_cyw43_driver/include/cyw43_configport.h, Copyright (c) 2022
** Raspberry Pi (Trading) Ltd., BSD-3-Clause.
*/
#ifndef CYW43_CONFIGPORT_H
#define CYW43_CONFIGPORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define CYW43_USE_SPI            1
#define CYW43_LWIP               0
#define CYW43_NETUTILS           0
#define CYW43_NO_NETUTILS        1
#define CYW43_ENABLE_BLUETOOTH   1
#define CYW43_USE_OTP_MAC        1
#define CYW43_USE_STATS          0
#define CYW43_GPIO               1
#define CYW43_LOGIC_DEBUG        0
#define CYW43_HAL_MAC_WLAN0      0
#define CYW43_IOCTL_TIMEOUT_US   1000000
#define CYW43_WL_GPIO_COUNT      3
#define CYW43_NUM_GPIOS          CYW43_WL_GPIO_COUNT
#define CYW43_HOST_NAME          "PicoW"
#ifndef STATIC
   #define STATIC static
#endif

/* The radio's Wi-Fi firmware stays in the flash (radio_firmware.h). The Bluetooth patch,
** 7 KB, is in the image. */
#define CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE "radio_firmware.h"
#define CYW43_WIFI_NVRAM_INCLUDE_FILE       "wifi_nvram_43439.h"
#define CYW43_BT_FIRMWARE_INCLUDE_FILE      "cyw43_btfw_43439.h"
#define CYW43_RESOURCE_ATTRIBUTE            __attribute__((aligned(4)))

/* The Pico W's wiring (pico-sdk, boards/include/boards/pico_w.h): one pin for data in, out
** and the radio's interrupt. */
#define CYW43_PIN_WL_REG_ON      23
#define CYW43_PIN_WL_DATA_OUT    24
#define CYW43_PIN_WL_DATA_IN     24
#define CYW43_PIN_WL_HOST_WAKE   24
#define CYW43_PIN_WL_CLOCK       29
#define CYW43_PIN_WL_CS          25

#define CYW43_EPERM              1
#define CYW43_EIO                5
#define CYW43_EINVAL             22
#define CYW43_ETIMEDOUT          110

#ifndef MIN
   #define MIN(a, b)             ((a) < (b) ? (a) : (b))
   #define MAX(a, b)             ((a) > (b) ? (a) : (b))
#endif
#define CYW43_ARRAY_SIZE(a)      (sizeof(a) / sizeof((a)[0]))
/* The formats of the driver's messages, without their arguments, are kept for the console
** to show (radio.c). */
void RadioSays(const char *format, ...);
int RadioFailedAt(int result, int line);
#define CYW43_PRINTF(...)        RadioSays(__VA_ARGS__)
#define CYW43_FAIL_FAST_CHECK(r) RadioFailedAt((r), __LINE__)

#define cyw43_hal_pin_obj_t      unsigned
#define CYW43_HAL_PIN_MODE_INPUT    0
#define CYW43_HAL_PIN_MODE_OUTPUT   1
#define CYW43_HAL_PIN_PULL_NONE     0
#define CYW43_HAL_PIN_PULL_UP       1
#define CYW43_HAL_PIN_PULL_DOWN     2

uint32_t cyw43_hal_ticks_us(void);
uint32_t cyw43_hal_ticks_ms(void);
int cyw43_hal_pin_read(cyw43_hal_pin_obj_t pin);
void cyw43_hal_pin_low(cyw43_hal_pin_obj_t pin);
void cyw43_hal_pin_high(cyw43_hal_pin_obj_t pin);
void cyw43_hal_pin_config(cyw43_hal_pin_obj_t pin, uint32_t mode, uint32_t pull, uint32_t alt);
void cyw43_hal_get_mac(int idx, uint8_t buf[6]);
void cyw43_hal_generate_laa_mac(int idx, uint8_t buf[6]);

void cyw43_delay_ms(uint32_t ms);
void cyw43_delay_us(uint32_t us);
void cyw43_schedule_internal_poll_dispatch(void (*func)(void));

#define CYW43_THREAD_ENTER
#define CYW43_THREAD_EXIT
#define CYW43_THREAD_LOCK_CHECK
#define CYW43_EVENT_POLL_HOOK
#define CYW43_POST_POLL_HOOK
#define CYW43_SDPCM_SEND_COMMON_WAIT  cyw43_delay_us(100);
#define CYW43_DO_IOCTL_WAIT           cyw43_delay_us(100);

/* panic: The driver hit an error it cannot recover from. The radio is turned off
** (radio.c). */
void RadioPanic(const char *why);
#define panic(why)               RadioPanic(why)

void *cyw43_malloc(size_t size);   /* radio.c */
void cyw43_free(void *p);

#endif
