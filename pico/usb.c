/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File usb.c: A serial port on the Pico's own USB, as a USB CDC ACM device, so that a Pico
** needs no probe nor adapter (RP2040 datasheet, 4.1).
**
** Polled in core 1's interrupt of 1 ms: no interrupt of its own, and nothing on core 0. The
** controller answers NAK by itself until a buffer is ready, so a host only waits. That gives
** one packet each way per ms on the data endpoints: 64 kB/s, against 11.5 for UART0.
*/

#include "Escapement.h"
#include "forth.h"
#include "forth_platform.h"
#include "usb.h"

#define RESETS_SET        *((volatile UINT32 *)(0x4000C000 + 0x2000))
#define RESETS_CLR        *((volatile UINT32 *)(0x4000C000 + 0x3000))
#define RESETS_DONE       *((volatile UINT32 *)(0x4000C000 + 0x08))
#define RESETS_PLL_USB    (1u << 13)
#define RESETS_USBCTRL    (1u << 24)

#define PLL_USB_CS        *((volatile UINT32 *)(0x4002C000 + 0x00))
#define PLL_USB_PWR       *((volatile UINT32 *)(0x4002C000 + 0x04))
#define PLL_USB_FBDIV     *((volatile UINT32 *)(0x4002C000 + 0x08))
#define PLL_USB_PRIM      *((volatile UINT32 *)(0x4002C000 + 0x0C))
#define PLL_LOCK          (1u << 31)
#define CLK_USB_CTRL      *((volatile UINT32 *)(0x40008000 + 0x54))
#define CLK_USB_DIV       *((volatile UINT32 *)(0x40008000 + 0x58))
#define CLK_ENABLE        (1u << 11)
#define TIMER_TIMERAWL    *((volatile UINT32 *)(0x40054000 + 0x28))

#define REGS              0x50110000
#define ADDR_ENDP         *((volatile UINT32 *)(REGS + 0x00))
#define MAIN_CTRL         *((volatile UINT32 *)(REGS + 0x40))
#define SIE_CTRL          *((volatile UINT32 *)(REGS + 0x4C))
#define SIE_STATUS        *((volatile UINT32 *)(REGS + 0x50))
#define BUFF_STATUS       *((volatile UINT32 *)(REGS + 0x58))
#define EP_STALL_ARM      *((volatile UINT32 *)(REGS + 0x68))
#define USB_MUXING        *((volatile UINT32 *)(REGS + 0x74))
#define USB_PWR           *((volatile UINT32 *)(REGS + 0x78))
#define SIE_PULLUP_EN     (1u << 16)
#define SIE_EP0_INT_1BUF  (1u << 29)
#define STATUS_SUSPENDED  (1u << 4)
#define STATUS_SETUP_REC  (1u << 17)
#define STATUS_BUS_RESET  (1u << 19)

#define DPRAM             0x50100000
#define SETUP             ((volatile UINT8 *)DPRAM)
#define EP_CTRL(n)        *((volatile UINT32 *)(DPRAM + (n) * 4))   /* below: which of them */
#define EP1_IN_CTRL       2
#define EP2_IN_CTRL       4
#define EP2_OUT_CTRL      5
#define EP_BUF(n)         *((volatile UINT32 *)(DPRAM + 0x80 + (n) * 4))
#define EP0_IN            0
#define EP0_OUT           1
#define EP2_IN            4
#define EP2_OUT           5
#define EP0_DATA          ((volatile UINT8 *)(DPRAM + 0x100))
#define IN_OFFSET         0x180
#define OUT_OFFSET        0x1C0
#define NOTE_OFFSET       0x200
#define EP_ENABLE         (1u << 31)
#define EP_TELL_EACH      (1u << 29)   /* BUFF_STATUS says when a buffer is done */
#define EP_BULK           (2u << 26)
#define EP_INTERRUPT      (3u << 26)
#define BUF_FULL          (1u << 15)
#define BUF_LAST          (1u << 14)
#define BUF_DATA1         (1u << 13)
#define BUF_STALL         (1u << 11)
#define BUF_AVAILABLE     (1u << 10)
#define BIT_EP0_IN        (1u << 0)
#define BIT_EP0_OUT       (1u << 1)
#define BIT_EP2_IN        (1u << 4)
#define BIT_EP2_OUT       (1u << 5)

#define PACKET            64
#define TX                1024       /* a power of 2 */
#define STOPPED_READING   100000     /* us with no room before bytes are dropped */

static const UINT8 Device[18] = {
  18, 1, 0x00, 0x02, 0xEF, 0x02, 0x01, PACKET,   /* USB 2.0, an interface association */
  0x8A, 0x2E, 0x0A, 0x00, 0x00, 0x01,            /* 2E8A:000A, the Pico's serial port */
  1, 2, 0, 1
};

static const UINT8 Configuration[75] = {
  9, 2, 75, 0, 2, 1, 0, 0x80, 50,                /* 2 interfaces, 100 mA */
  8, 11, 0, 2, 2, 2, 0, 0,                       /* the two as one function */
  9, 4, 0, 0, 1, 2, 2, 0, 0,                     /* the control interface */
  5, 0x24, 0x00, 0x20, 0x01,                     /* CDC 1.20 */
  5, 0x24, 0x01, 0, 1,                           /* call management */
  4, 0x24, 0x02, 0x02,                           /* line coding, line state */
  5, 0x24, 0x06, 0, 1,                           /* union */
  7, 5, 0x81, 3, 8, 0, 16,                       /* EP1 IN, interrupt: never used */
  9, 4, 1, 0, 2, 10, 0, 0, 0,                    /* the data interface */
  7, 5, 0x02, 2, PACKET, 0, 0,                   /* EP2 OUT, bulk */
  7, 5, 0x82, 2, PACKET, 0, 0                    /* EP2 IN, bulk */
};

static const UINT8 Languages[4] = { 4, 3, 0x09, 0x04 };
static const UINT8 Maker[22] = { 22, 3, 'E',0,'s',0,'c',0,'a',0,'p',0,'e',0,'m',0,'e',0,'n',0,'t',0 };
static const UINT8 Product[34] = { 34, 3, 'E',0,'s',0,'c',0,'a',0,'p',0,'e',0,'m',0,'e',0,'n',0,'t',0,
                                   ' ',0,'F',0,'o',0,'r',0,'t',0,'h',0 };

/* All of it outlives a restart of core 0, which clears its .bss under core 1. */
static const UINT8 *volatile InLeft FORTH_KEEP;
static volatile UINT32 InCount FORTH_KEEP, InPid FORTH_KEEP;
static volatile UINT32 NewAddress FORTH_KEEP, Configured FORTH_KEEP, Open FORTH_KEEP;
static volatile UINT32 LineCodingDue FORTH_KEEP, Stopped FORTH_KEEP;
static volatile UINT32 TxPid FORTH_KEEP, RxPid FORTH_KEEP, TxBusy FORTH_KEEP, LastFull FORTH_KEEP;
static volatile UINT8 LineCoding[7] FORTH_KEEP;
static volatile UINT8 Tx[TX] FORTH_KEEP;
static volatile UINT32 TxHead FORTH_KEEP, TxTail FORTH_KEEP;

/* Arm: A buffer given to the controller: AVAILABLE a moment after the rest, as it says
** (datasheet, 4.1.2.5.1), the controller running slower than the processor. */
static void Arm(int ep, UINT32 value)
{
  EP_BUF(ep) = value;
  __asm volatile ("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop");
  EP_BUF(ep) = value | BUF_AVAILABLE;
} /* end of Arm */

/* SendIn: The next packet of what EP0 is sending, the last one short. */
static void SendIn(void)
{
  UINT32 i, count = InCount < PACKET ? InCount : PACKET;
  for (i = 0; i < count; i += 1)
     EP0_DATA[i] = InLeft[i];
  InLeft += count;
  InCount -= count;
  Arm(EP0_IN,count | BUF_FULL | BUF_LAST | InPid);
  InPid ^= BUF_DATA1;
} /* end of SendIn */

/* Reply: The data stage of a control read, cut to what the host asked. */
static void Reply(const UINT8 *data, UINT32 count, UINT32 asked)
{
  InLeft = data;
  InCount = count < asked ? count : asked;
  InPid = BUF_DATA1;
  SendIn();
  Arm(EP0_OUT,PACKET | BUF_LAST | BUF_DATA1);   /* its status, or a host that stops early */
} /* end of Reply */

/* Status: The status stage of a request with no data read. */
static void Status(void)
{
  InCount = 0;
  InLeft = 0;
  Arm(EP0_IN,BUF_FULL | BUF_LAST | BUF_DATA1);
} /* end of Status */

static void Stall(void)
{
  EP_STALL_ARM = 3;
  EP_BUF(EP0_IN) = BUF_STALL;
  EP_BUF(EP0_OUT) = BUF_STALL;
} /* end of Stall */

static void Configure(void)
{
  EP_CTRL(EP1_IN_CTRL) = EP_ENABLE | EP_INTERRUPT | NOTE_OFFSET;
  EP_CTRL(EP2_IN_CTRL) = EP_ENABLE | EP_TELL_EACH | EP_BULK | IN_OFFSET;
  EP_CTRL(EP2_OUT_CTRL) = EP_ENABLE | EP_TELL_EACH | EP_BULK | OUT_OFFSET;
  TxPid = RxPid = 0;
  TxBusy = LastFull = 0;
  Arm(EP2_OUT,PACKET | BUF_LAST);
  RxPid = BUF_DATA1;
  Configured = 1;
} /* end of Configure */

static void Setup(void)
{
  UINT32 type = SETUP[0], request = SETUP[1], value = SETUP[2] | (UINT32)SETUP[3] << 8;
  UINT32 asked = SETUP[6] | (UINT32)SETUP[7] << 8;
  static const UINT8 zeros[2] = { 0, 0 };
  LineCodingDue = 0;
  if (type == 0x00 && request == 5) {   /* SET_ADDRESS: once its status is through */
     NewAddress = (value & 0x7F) | 0x100;
     Status();
  }
  else if (type == 0x00 && request == 9) {   /* SET_CONFIGURATION */
     if (value != 0)
        Configure();
     else
        Configured = Open = 0;
     Status();
  }
  else if (type == 0x80 && request == 6) {   /* GET_DESCRIPTOR */
     const UINT8 *what = NULL;
     UINT32 count = 0;
     switch (value >> 8) {
        case 1: what = Device; count = sizeof Device; break;
        case 2: what = Configuration; count = sizeof Configuration; break;
        case 3:
           what = (value & 0xFF) == 0 ? Languages : (value & 0xFF) == 1 ? Maker :
                  (value & 0xFF) == 2 ? Product : NULL;
           count = what == NULL ? 0 : what[0];
           break;
        default: break;
     }
     if (what == NULL)
        Stall();
     else
        Reply(what,count,asked);
  }
  else if (type == 0x80 && request == 8) {   /* GET_CONFIGURATION */
     static const UINT8 one = 1, none = 0;
     Reply(Configured ? &one : &none,1,asked);
  }
  else if ((type & 0xFC) == 0x80 && request == 0)   /* GET_STATUS */
     Reply(zeros,2,asked);
  else if (type == 0x02 && request == 1 && value == 0) {   /* CLEAR_FEATURE, ENDPOINT_HALT */
     UINT32 endpoint = SETUP[4];
     if (endpoint == 0x82)
        TxPid = 0;
     else if (endpoint == 0x02) {
        Arm(EP2_OUT,PACKET | BUF_LAST);
        RxPid = BUF_DATA1;
     }
     Status();
  }
  else if (type == 0x21 && request == 0x20) {   /* SET_LINE_CODING: its data, then status */
     LineCodingDue = 1;
     Arm(EP0_OUT,PACKET | BUF_LAST | BUF_DATA1);
  }
  else if (type == 0xA1 && request == 0x21)   /* GET_LINE_CODING */
     Reply((const UINT8 *)LineCoding,sizeof LineCoding,asked);
  else if (type == 0x21 && request == 0x22) {   /* SET_CONTROL_LINE_STATE: DTR, a program */
     UINT32 open = Configured && (value & 1) != 0;
     if (open != Open) {   /* nothing of the last program for the next one */
        TxHead = TxTail;
        LastFull = 0;
     }
     Open = open;
     Status();
  }
  else if (type == 0x21 && request == 0x23)   /* SEND_BREAK */
     Status();
  else
     Stall();
} /* end of Setup */

static void BusReset(void)
{
  ADDR_ENDP = 0;
  NewAddress = Configured = Open = LineCodingDue = 0;
  InCount = 0;
  TxHead = TxTail = 0;
  EP_CTRL(EP1_IN_CTRL) = EP_CTRL(EP2_IN_CTRL) = EP_CTRL(EP2_OUT_CTRL) = 0;
} /* end of BusReset */

/* SendData: What the ring holds, a packet at a time; one that ended full is followed by an
** empty one, or the host would wait for more. */
static void SendData(void)
{
  UINT32 i, count = TxHead - TxTail;
  if (count == 0 && !LastFull)
     return;
  if (count > PACKET)
     count = PACKET;
  for (i = 0; i < count; i += 1)
     ((volatile UINT8 *)(DPRAM + IN_OFFSET))[i] = Tx[(TxTail + i) % TX];
  TxTail += count;
  LastFull = count == PACKET;
  TxBusy = 1;
  Arm(EP2_IN,count | BUF_FULL | BUF_LAST | TxPid);
  TxPid ^= BUF_DATA1;
} /* end of SendData */

void UsbPoll(void (*received)(char))
{
  UINT32 status = SIE_STATUS, done = BUFF_STATUS;
  if (status & STATUS_BUS_RESET) {
     SIE_STATUS = STATUS_BUS_RESET;
     BusReset();
     return;
  }
  if (status & STATUS_SETUP_REC) {
     SIE_STATUS = STATUS_SETUP_REC;
     BUFF_STATUS = BIT_EP0_IN | BIT_EP0_OUT;
     done &= ~(BIT_EP0_IN | BIT_EP0_OUT);
     Setup();
  }
  if (done & BIT_EP0_IN) {
     BUFF_STATUS = BIT_EP0_IN;
     if (NewAddress != 0) {
        ADDR_ENDP = NewAddress & 0x7F;
        NewAddress = 0;
     }
     if (InCount != 0)
        SendIn();
  }
  if (done & BIT_EP0_OUT) {
     BUFF_STATUS = BIT_EP0_OUT;
     if (LineCodingDue) {
        UINT32 i;
        LineCodingDue = 0;
        for (i = 0; i < sizeof LineCoding; i += 1)
           LineCoding[i] = EP0_DATA[i];
        Status();
     }
     else
        InCount = 0;   /* a host that stopped reading early */
  }
  if (done & BIT_EP2_OUT) {
     UINT32 i, count = EP_BUF(EP2_OUT) & 0x3FF;
     BUFF_STATUS = BIT_EP2_OUT;
     for (i = 0; i < count; i += 1)
        received((char)((volatile UINT8 *)(DPRAM + OUT_OFFSET))[i]);
     Arm(EP2_OUT,PACKET | BUF_LAST | RxPid);
     RxPid ^= BUF_DATA1;
  }
  if (done & BIT_EP2_IN) {
     BUFF_STATUS = BIT_EP2_IN;
     TxBusy = 0;
  }
  if (Configured && !TxBusy)
     SendData();
} /* end of UsbPoll */

int UsbActive(void)
{
  /* Suspended: no host, or one asleep, which cannot read; UART0 then. */
  return Open != 0 && (SIE_STATUS & STATUS_SUSPENDED) == 0;
} /* end of UsbActive */

uint32_t UsbRoom(void)
{
  return TX - (TxHead - TxTail);
} /* end of UsbRoom */

void UsbSend(char c)
{
  UINT32 start = TIMER_TIMERAWL, mask;
  for (;;) {
     mask = ForthMask();   /* the interrupt sends here too: room looked at with it held off */
     if (UsbRoom() != 0) {
        Tx[TxHead % TX] = (UINT8)c;
        TxHead += 1;
        Stopped = 0;
        ForthUnmask(mask);
        return;
     }
     ForthUnmask(mask);
     if (Stopped || !UsbActive() || TIMER_TIMERAWL - start > STOPPED_READING) {
        Stopped = 1;
        return;
     }
  }
} /* end of UsbSend */

void UsbInit(void)
{
  UINT32 i;
  static const UINT8 coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };   /* 115200 8N1 */
  /* Whatever a firmware before left of them: a host that saw it must see it go. */
  RESETS_SET = RESETS_USBCTRL | RESETS_PLL_USB;
  RESETS_CLR = RESETS_PLL_USB;
  while ((RESETS_DONE & RESETS_PLL_USB) == 0);
  /* 12 MHz x 100 / 5 / 5 = 48 MHz (datasheet, 2.18.2.1). */
  PLL_USB_FBDIV = 100;
  PLL_USB_PWR &= ~((1u << 0) | (1u << 5));
  while ((PLL_USB_CS & PLL_LOCK) == 0);
  PLL_USB_PRIM = (5u << 16) | (5u << 12);
  PLL_USB_PWR &= ~(1u << 3);
  CLK_USB_DIV = 1u << 8;
  CLK_USB_CTRL = CLK_ENABLE;
  /* About 20 ms, for a host that saw a firmware before to see it go. Counted: the timer may
  ** not be running yet, after a power on. */
  for (i = 0; i < 400000; i += 1)
     __asm volatile ("nop");
  RESETS_CLR = RESETS_USBCTRL;
  while ((RESETS_DONE & RESETS_USBCTRL) == 0);
  for (i = 0; i < 4096; i += 4)
     *((volatile UINT32 *)(DPRAM + i)) = 0;
  for (i = 0; i < sizeof LineCoding; i += 1)
     LineCoding[i] = coding[i];
  InCount = TxHead = TxTail = 0;
  NewAddress = Configured = Open = LineCodingDue = 0;
  TxBusy = LastFull = Stopped = 0;
  USB_MUXING = (1u << 0) | (1u << 3);   /* TO_PHY, SOFTCON */
  USB_PWR = (1u << 2) | (1u << 3);      /* VBUS seen, whatever the pin */
  MAIN_CTRL = 1;
  SIE_CTRL = SIE_EP0_INT_1BUF | SIE_PULLUP_EN;
} /* end of UsbInit */
