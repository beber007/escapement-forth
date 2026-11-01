/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File chip.h: What differs between the two chips: the RP2040 of the Pico, and the RP2350 of
** the Pico 2 (make BOARD=pico2). Escapement_Config.h says which.
*/

#ifndef CHIP_H_
#define CHIP_H_

#ifdef RP2350
   #define CHIP_RESETS         0x40020000
   #define CHIP_TIMER          0x400B0000   /* TIMER0 */
   #define CHIP_TIMER_INTR     (CHIP_TIMER + 0x3C)
   #define CHIP_TIMER_INTE     (CHIP_TIMER + 0x40)
   #define CHIP_TIMER_INTF     (CHIP_TIMER + 0x44)
   #define CHIP_WATCHDOG       0x400D8000
   #define CHIP_IO_BANK0       0x40028000
   #define CHIP_PADS_BANK0     0x40038000
   #define CHIP_UART0          0x40070000
   #define CHIP_PLL_USB        0x40058000
   #define CHIP_CLK_USB_CTRL   (0x40010000 + 0x60)
   #define CHIP_CLK_USB_DIV    (0x40010000 + 0x64)
   #define CHIP_CLK_USB_DIV_1  (1u << 16)
   #define CHIP_RESET_IO_BANK0 (1u << 6)
   #define CHIP_RESET_PADS     (1u << 9)
   #define CHIP_RESET_PLL_USB  (1u << 15)
   #define CHIP_RESET_TIMER    (1u << 23)   /* TIMER0 */
   #define CHIP_RESET_UART0    (1u << 26)
   #define CHIP_RESET_USBCTRL  (1u << 28)
   #define CHIP_GPIO_OUT_SET   0x18
   #define CHIP_GPIO_OUT_CLR   0x20
   #define CHIP_GPIO_OUT_XOR   0x28
   #define CHIP_GPIO_OE        0x30
   #define CHIP_GPIO_OE_SET    0x38
   #define CHIP_GPIO_OE_CLR    0x40
   #define CHIP_IRQ_FIFO       25           /* SIO_IRQ_FIFO */
   #define CHIP_VECTORS        68           /* 16 and the 52 interrupts */
   #define CHIP_VECTORS_ALIGN  512
#else
   #define CHIP_RESETS         0x4000C000
   #define CHIP_TIMER          0x40054000
   #define CHIP_TIMER_INTR     (CHIP_TIMER + 0x34)
   #define CHIP_TIMER_INTE     (CHIP_TIMER + 0x38)
   #define CHIP_TIMER_INTF     (CHIP_TIMER + 0x3C)
   #define CHIP_WATCHDOG       0x40058000
   #define CHIP_IO_BANK0       0x40014000
   #define CHIP_UART0          0x40034000
   #define CHIP_PLL_USB        0x4002C000
   #define CHIP_CLK_USB_CTRL   (0x40008000 + 0x54)
   #define CHIP_CLK_USB_DIV    (0x40008000 + 0x58)
   #define CHIP_CLK_USB_DIV_1  (1u << 8)
   #define CHIP_RESET_IO_BANK0 (1u << 5)
   #define CHIP_RESET_PLL_USB  (1u << 13)
   #define CHIP_RESET_TIMER    (1u << 21)
   #define CHIP_RESET_UART0    (1u << 22)
   #define CHIP_RESET_USBCTRL  (1u << 24)
   #define CHIP_GPIO_OUT_SET   0x14
   #define CHIP_GPIO_OUT_CLR   0x18
   #define CHIP_GPIO_OUT_XOR   0x1C
   #define CHIP_GPIO_OE        0x20
   #define CHIP_GPIO_OE_SET    0x24
   #define CHIP_GPIO_OE_CLR    0x28
   #define CHIP_IRQ_FIFO       15           /* SIO_IRQ_PROC0 */
   #define CHIP_VECTORS        48           /* 16 and the 32 interrupts */
   #define CHIP_VECTORS_ALIGN  256
#endif

#endif /* CHIP_H_ */
