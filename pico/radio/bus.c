/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File bus.c: The CYW43's gSPI bus on the Pico W, with no pico-sdk. The file also holds the
** other functions that the driver asks of its platform (cyw43_configport.h). State machine
** 0 of PIO0 clocks the bus. The CPU fills and empties its FIFOs, without DMA. The machine
** stalls when a FIFO waits, and the clock stops with it. The bus allows this: the CPU may
** be late, but never too early.
**
** The bus is half duplex on one pin, GP24. When CS is high, this pin is also the radio's
** interrupt. The PIO program and its settings come from the pico-sdk: spi_gap01_sample0 of
** pico_cyw43_driver/cyw43_bus_pio_spi.pio, the clock divided by 2, the clock pad at 12 mA
** with a fast slew rate, and no input synchroniser on the data pin. They follow the
** pico-sdk's cyw43_bus_pio_spi.c, Copyright (c) 2022 Raspberry Pi (Trading) Ltd.,
** BSD-3-Clause.
**
** Platform version: RP2040 (Raspberry Pi Pico W).
*/

#include "cyw43.h"
#include "cyw43_internal.h"
#include "cyw43_spi.h"
#include "radio.h"
#include "chip.h"

#define REG(a)               (*(volatile uint32_t *)(a))
#define SET(a)               REG((a) + 0x2000)
#define CLR(a)               REG((a) + 0x3000)

#define RESETS               CHIP_RESETS
#define RESETS_DONE          REG(RESETS + 0x08)
#define RESET_IO_BANK0       CHIP_RESET_IO_BANK0
#define RESET_PADS_BANK0     CHIP_RESET_PADS
#define RESET_PIO0           (1u << 10)

#define IO_CTRL(p)           (CHIP_IO_BANK0 + 0x04 + 8 * (p))
#define FUNC_SIO             5
#define FUNC_PIO0            6
#define PADS(p)              (CHIP_PADS_BANK0 + 0x04 + 4 * (p))
#define PAD_SLEWFAST         (1u << 0)
#define PAD_SCHMITT          (1u << 1)
#define PAD_PDE              (1u << 2)
#define PAD_PUE              (1u << 3)
#define PAD_DRIVE            (3u << 4)
#define PAD_DRIVE_12MA       (3u << 4)
#define PAD_IE               (1u << 6)

#define SIO_IN               REG(0xD0000004)
#define SIO_OUT_SET          REG(0xD0000000 + CHIP_GPIO_OUT_SET)
#define SIO_OUT_CLR          REG(0xD0000000 + CHIP_GPIO_OUT_CLR)
#define SIO_OE_SET           REG(0xD0000000 + CHIP_GPIO_OE_SET)
#define SIO_OE_CLR           REG(0xD0000000 + CHIP_GPIO_OE_CLR)

#define TIMERAWL             REG(CHIP_TIMER + 0x28)

#define PIO0                 0x50200000
#define PIO_CTRL             REG(PIO0 + 0x000)
#define PIO_FSTAT            REG(PIO0 + 0x004)
#define PIO_FDEBUG           REG(PIO0 + 0x008)
#define PIO_TXF0             REG(PIO0 + 0x010)
#define PIO_RXF0             REG(PIO0 + 0x020)
#define PIO_SYNC_BYPASS      REG(PIO0 + 0x038)
#define PIO_INSTR_MEM(i)     REG(PIO0 + 0x048 + 4 * (i))
#define SM0_CLKDIV           REG(PIO0 + 0x0C8)
#define SM0_EXECCTRL         REG(PIO0 + 0x0CC)
#define SM0_SHIFTCTRL        REG(PIO0 + 0x0D0)
#define SM0_INSTR            REG(PIO0 + 0x0D8)
#define SM0_PINCTRL          REG(PIO0 + 0x0DC)
#define CTRL_SM0             (1u << 0)
#define CTRL_RESTART0        (1u << 4)
#define CTRL_CLKDIV_RESTART0 (1u << 8)
#define FSTAT_RXEMPTY0       (1u << 8)
#define FSTAT_TXFULL0        (1u << 16)
#define FDEBUG_TXSTALL0      (1u << 24)

/* spi_gap01_sample0, loaded at offset 0. It sends bits out, waits one clock, then reads
** bits in. Each bit takes one clock, and a clock takes two cycles of the machine. */
static const uint16_t Program[] = {
   0x6001,   /* 0: out pins, 1      side 0 */
   0x1040,   /* 1: jmp x--, 0       side 1 */
   0xE080,   /* 2: set pindirs, 0   side 0  (lp1_end) */
   0xB042,   /* 3: nop              side 1 */
   0x4001,   /* 4: in pins, 1       side 0 */
   0x1084,   /* 5: jmp y--, 4       side 1 */
};
#define LP1_END              2
#define END                  6
#define OUT_X_32             0x6020
#define OUT_Y_32             0x6040
#define JMP_0                0x0000
#define SET_PINDIRS_1        0xE081
#define SET_PINDIRS_0        0xE080
#define SET_PINS_1           0xE001
#define MOV_PINS_NULL        0xA003

#define EXEC_WRAP(top)       (((uint32_t)(top) - 1) << 12)   /* WRAP_TOP; WRAP_BOTTOM 0 */
/* Both shifts go left, with autopull and autopush at 32 bits: words go out MSB first. */
#define SHIFT                ((1u << 17) | (1u << 16))
#define PINS                 ((1u << 29) | (CYW43_PIN_WL_DATA_IN << 15) |                \
                              (CYW43_PIN_WL_CLOCK << 10) | (CYW43_PIN_WL_DATA_OUT << 5) | \
                              (1u << 26) | (1u << 20) | CYW43_PIN_WL_DATA_OUT)

static uint32_t Swap(uint32_t w)
{
  return w >> 24 | (w >> 8 & 0xFF00u) | (w << 8 & 0xFF0000u) | w << 24;
} /* end of Swap */

/* Exec: Runs one instruction at once on state machine 0. */
static void Exec(uint32_t instruction)
{
  SM0_INSTR = instruction;
} /* end of Exec */

/* ---- What the driver asks of the platform. */

uint32_t cyw43_hal_ticks_us(void)
{
  return TIMERAWL;
} /* end of cyw43_hal_ticks_us */

uint32_t cyw43_hal_ticks_ms(void)
{
  return TIMERAWL / 1000;
} /* end of cyw43_hal_ticks_ms */

void cyw43_delay_us(uint32_t us)
{
  uint32_t start = TIMERAWL;
  while (TIMERAWL - start <= us);   /* at least us: start may be late in its microsecond */
} /* end of cyw43_delay_us */

void cyw43_delay_ms(uint32_t ms)
{
  while (ms-- > 0)
     cyw43_delay_us(1000);
} /* end of cyw43_delay_ms */

int cyw43_hal_pin_read(cyw43_hal_pin_obj_t pin)
{
  return (SIO_IN >> pin) & 1;
} /* end of cyw43_hal_pin_read */

void cyw43_hal_pin_low(cyw43_hal_pin_obj_t pin)
{
  SIO_OUT_CLR = 1u << pin;
} /* end of cyw43_hal_pin_low */

void cyw43_hal_pin_high(cyw43_hal_pin_obj_t pin)
{
  SIO_OUT_SET = 1u << pin;
} /* end of cyw43_hal_pin_high */

/* cyw43_hal_pin_config: Gives a pin to the SIO, as an input or an output, with the pulls
** asked. The data pin is the radio's interrupt when the bus is idle. It stays with the
** PIO: its input reads the same. */
void cyw43_hal_pin_config(cyw43_hal_pin_obj_t pin, uint32_t mode, uint32_t pull, uint32_t alt)
{
  (void)alt;
  CLR(PADS(pin)) = PAD_PUE | PAD_PDE;
  SET(PADS(pin)) = PAD_IE | (pull == CYW43_HAL_PIN_PULL_UP ? PAD_PUE :
                             pull == CYW43_HAL_PIN_PULL_DOWN ? PAD_PDE : 0);
  if (pin == CYW43_PIN_WL_DATA_IN)
     return;
  REG(IO_CTRL(pin)) = FUNC_SIO;
  if (mode == CYW43_HAL_PIN_MODE_OUTPUT)
     SIO_OE_SET = 1u << pin;
  else
     SIO_OE_CLR = 1u << pin;
} /* end of cyw43_hal_pin_config */

/* cyw43_hal_get_mac: Not used. The driver reads the MAC from the radio's OTP. */
void cyw43_hal_get_mac(int idx, uint8_t buf[6])
{
  (void)idx;
  memset(buf,0,6);
} /* end of cyw43_hal_get_mac */

void cyw43_hal_generate_laa_mac(int idx, uint8_t buf[6])
{
  (void)idx;
  memset(buf,0,6);
  buf[0] = 0x02;
} /* end of cyw43_hal_generate_laa_mac */

/* The driver's poll serves Wi-Fi packets and its interrupt line. This port has no poll. */
void cyw43_schedule_internal_poll_dispatch(void (*func)(void))
{
  (void)func;
} /* end of cyw43_schedule_internal_poll_dispatch */

/* ---- The bus. */

int cyw43_spi_init(cyw43_int_t *self)
{
  unsigned i;
  (void)self;
  CLR(RESETS) = RESET_IO_BANK0 | RESET_PADS_BANK0 | RESET_PIO0;
  while ((RESETS_DONE & (RESET_IO_BANK0 | RESET_PADS_BANK0 | RESET_PIO0)) !=
         (RESET_IO_BANK0 | RESET_PADS_BANK0 | RESET_PIO0));
  PIO_CTRL = 0;
  for (i = 0; i < sizeof Program / sizeof Program[0]; i += 1)
     PIO_INSTR_MEM(i) = Program[i];
  SM0_CLKDIV = 2u << 16;
  CLR(PADS(CYW43_PIN_WL_CLOCK)) = PAD_DRIVE | PAD_PUE;
  SET(PADS(CYW43_PIN_WL_CLOCK)) = PAD_DRIVE_12MA | PAD_SLEWFAST | PAD_PDE | PAD_IE;
  SM0_EXECCTRL = EXEC_WRAP(END);
  SM0_SHIFTCTRL = SHIFT;
  PIO_SYNC_BYPASS |= 1u << CYW43_PIN_WL_DATA_IN;
  /* Makes the clock pin an output. SET does it, with its base moved to that pin for a
  ** moment. */
  SM0_PINCTRL = (1u << 26) | (CYW43_PIN_WL_CLOCK << 5);
  Exec(SET_PINDIRS_1);
  SM0_PINCTRL = PINS;
  CLR(PADS(CYW43_PIN_WL_DATA_IN)) = PAD_PUE;
  SET(PADS(CYW43_PIN_WL_DATA_IN)) = PAD_PDE | PAD_SCHMITT | PAD_IE;
  REG(IO_CTRL(CYW43_PIN_WL_DATA_OUT)) = FUNC_PIO0;
  REG(IO_CTRL(CYW43_PIN_WL_CLOCK)) = FUNC_PIO0;
  Exec(SET_PINS_1);
  return 0;
} /* end of cyw43_spi_init */

void cyw43_spi_deinit(cyw43_int_t *self)
{
  (void)self;
  PIO_CTRL = 0;
} /* end of cyw43_spi_deinit */

/* cyw43_spi_gpio_setup: Also drives the data pin low through the SIO, for the reset that
** follows. The radio reads this pin as it powers up: low selects gSPI, high selects
** SDIO. */
void cyw43_spi_gpio_setup(void)
{
  REG(IO_CTRL(CYW43_PIN_WL_DATA_OUT)) = FUNC_SIO;
  SIO_OUT_CLR = 1u << CYW43_PIN_WL_DATA_OUT;
  SIO_OE_SET = 1u << CYW43_PIN_WL_DATA_OUT;
  SET(PADS(CYW43_PIN_WL_REG_ON)) = PAD_PUE;
  REG(IO_CTRL(CYW43_PIN_WL_REG_ON)) = FUNC_SIO;
  SIO_OE_SET = 1u << CYW43_PIN_WL_REG_ON;
  REG(IO_CTRL(CYW43_PIN_WL_CS)) = FUNC_SIO;
  SIO_OUT_SET = 1u << CYW43_PIN_WL_CS;
  SIO_OE_SET = 1u << CYW43_PIN_WL_CS;
} /* end of cyw43_spi_gpio_setup */

void cyw43_spi_reset(void)
{
  SIO_OUT_CLR = 1u << CYW43_PIN_WL_REG_ON;
  cyw43_delay_ms(20);
  SIO_OUT_SET = 1u << CYW43_PIN_WL_REG_ON;
  cyw43_delay_ms(250);
  SIO_OE_CLR = 1u << CYW43_PIN_WL_DATA_OUT;   /* the pin is now the radio's interrupt */
} /* end of cyw43_spi_reset */

void cyw43_spi_set_polarity(cyw43_int_t *self, int pol)
{
  (void)self;
  (void)pol;
} /* end of cyw43_spi_set_polarity */

/* Start: Stops the machine, empties its FIFOs and sets its wrap at top. Then it loads the
** counts of bits to send and to receive, minus one, into X and Y. Last, it makes the data
** pin an output and drives CS low. */
static void Start(unsigned top, uint32_t out, uint32_t in)
{
  PIO_CTRL = 0;
  SM0_EXECCTRL = EXEC_WRAP(top);
  SM0_SHIFTCTRL = 0;   /* toggling the joins clears both FIFOs */
  SM0_SHIFTCTRL = SHIFT | (1u << 30);
  SM0_SHIFTCTRL = SHIFT;
  Exec(SET_PINDIRS_1);
  PIO_CTRL = CTRL_RESTART0 | CTRL_CLKDIV_RESTART0;
  PIO_TXF0 = out;
  Exec(OUT_X_32);
  PIO_TXF0 = in;
  Exec(OUT_Y_32);
  Exec(JMP_0);
  REG(IO_CTRL(CYW43_PIN_WL_DATA_OUT)) = FUNC_PIO0;
  SIO_OUT_CLR = 1u << CYW43_PIN_WL_CS;
} /* end of Start */

/* Stop: Stops the machine and drives CS high. The data pin becomes an input again, set to
** drive low the next time it is an output. After a write alone, the program never sets the
** pin back to an input. Between transfers, the pin is the radio's interrupt, which the
** driver reads. */
static void Stop(void)
{
  PIO_CTRL = 0;
  Exec(SET_PINDIRS_0);
  Exec(MOV_PINS_NULL);
  SIO_OUT_SET = 1u << CYW43_PIN_WL_CS;
  cyw43_delay_us(1);   /* the pico-sdk waits 100 ns before it samples the interrupt */
} /* end of Stop */

/* cyw43_spi_transfer: Sends tx_length bytes of tx. With rx, it also receives the bytes of
** rx that follow the first tx_length. The driver's generic calls (cyw43_spi.c) pass the
** same buffer for both, with the command first. Lengths are multiples of 4, and buffers
** are word aligned. Each word goes out in its byte order in memory, as the pico-sdk's DMA
** sends it with its byte swap. */
int cyw43_spi_transfer(cyw43_int_t *self, const uint8_t *tx, size_t tx_length, uint8_t *rx,
                       size_t rx_length)
{
  const uint32_t *out = (const uint32_t *)tx;
  uint32_t *in;
  size_t words, i;
  (void)self;
  if (tx == NULL || tx_length == 0)
     return -CYW43_EINVAL;
  if (rx != NULL) {
     /* Only the command goes out: the radio sends the rest of a read. */
     size_t sent = 4;
     in = (uint32_t *)(rx + sent);
     words = (rx_length - sent) / 4;
     Start(END,sent * 8 - 1,(uint32_t)(rx_length - sent) * 8 - 1);
     PIO_TXF0 = Swap(out[0]);
     PIO_CTRL = CTRL_SM0;
     for (i = 0; i < words; i += 1) {
        while (PIO_FSTAT & FSTAT_RXEMPTY0);
        in[i] = Swap(PIO_RXF0);
     }
  }
  else {
     words = tx_length / 4;
     Start(LP1_END,(uint32_t)tx_length * 8 - 1,0);
     PIO_CTRL = CTRL_SM0;
     for (i = 0; i < words; i += 1) {
        while (PIO_FSTAT & FSTAT_TXFULL0);
        PIO_TXF0 = Swap(out[i]);
     }
     PIO_FDEBUG = FDEBUG_TXSTALL0;
     while ((PIO_FDEBUG & FDEBUG_TXSTALL0) == 0);
  }
  Stop();
  return 0;
} /* end of cyw43_spi_transfer */
