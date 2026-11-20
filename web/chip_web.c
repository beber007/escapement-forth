/* Copyright (c) 2026 Bertrand Hurst. All rights reserved.
** Escapement Forth - a Forth console beside the Escapement real-time OS.
** Distributed under the terms of LICENSE at the root of this repository.
*/
/* File chip_web.c: The registers of a Pico (RP2040) that the I2C examples reach
** (examples/i2c.fs), modelled for the page: the chip's id, RESETS, IO_BANK0 and
** PADS_BANK0, the two I2C controllers, and on each of their buses the bench's three
** devices: a display at $3C, a SHT4x at $44 and an ADXL345 at $53. So each example runs
** unchanged on its port.
**
** The registers, their offsets, bits and reset values are those of the RP2040's headers
** (pico-sdk 2.1.1, hardware/regs). What the controller does with them is what a Pico did
** on the bench (2026-11-11, I2C1 at 100 kHz, through a Grove I2C Hub), where the
** datasheet leaves it open: the measures are in web/test/i2c_reference.txt, which
** test_web.js replays. The model answers as the chip does, not as the examples expect:
** an example that misuses the bus must fail here as it would on the board.
**
** Time is the caller's, in us. Nothing runs between two accesses: each one first brings
** the controller and its devices up to its time, byte after byte.
*/

#include "chip_web.h"

#define CHIP_ID      0x20002927u   /* RP2040 B2, read on the bench's Pico */
#define SYSINFO      0x40000000u
#define RESETS       0x4000C000u
#define IO_BANK0     0x40014000u
#define PADS_BANK0   0x4001C000u
#define I2C0_BASE    0x40044000u
#define I2C1_BASE    0x40048000u
#define CLOCK_MHZ    125u          /* clk_sys, which the controller counts SCL in */

/* What a period of SCL takes past its counts, the line's rise and its synchroniser, and
** what a start and a stop add: measured, 3 bytes written in 321 us and 16 in 1548, from
** the first entry to the bus idle again, 94.4 us a byte at 500 and 750 counts. */
#define RISE_CYCLES  61u
#define START_US     10u
#define STOP_US      10u

/* The atomic aliases of a register, at these offsets from it: its value written, or XORed,
** set or cleared by the value. */
#define ALIAS_XOR    0x1000u
#define ALIAS_SET    0x2000u
#define ALIAS_CLR    0x3000u

#define RESET_I2C0   (1u << 3)
#define RESET_I2C1   (1u << 4)
#define RESET_ALL    0x01FFFFFFu

/* The I2C controller's registers, by offset, and their bits. */
enum {
   IC_CON = 0x00, IC_TAR = 0x04, IC_SAR = 0x08, IC_DATA_CMD = 0x10,
   IC_SS_SCL_HCNT = 0x14, IC_SS_SCL_LCNT = 0x18, IC_FS_SCL_HCNT = 0x1C,
   IC_FS_SCL_LCNT = 0x20, IC_INTR_STAT = 0x2C, IC_INTR_MASK = 0x30,
   IC_RAW_INTR_STAT = 0x34, IC_RX_TL = 0x38, IC_TX_TL = 0x3C, IC_CLR_INTR = 0x40,
   IC_CLR_RX_UNDER = 0x44, IC_CLR_RX_OVER = 0x48, IC_CLR_TX_OVER = 0x4C,
   IC_CLR_RD_REQ = 0x50, IC_CLR_TX_ABRT = 0x54, IC_CLR_RX_DONE = 0x58,
   IC_CLR_ACTIVITY = 0x5C, IC_CLR_STOP_DET = 0x60, IC_CLR_START_DET = 0x64,
   IC_CLR_GEN_CALL = 0x68, IC_ENABLE = 0x6C, IC_STATUS = 0x70, IC_TXFLR = 0x74,
   IC_RXFLR = 0x78, IC_SDA_HOLD = 0x7C, IC_TX_ABRT_SOURCE = 0x80, IC_SDA_SETUP = 0x94,
   IC_ENABLE_STATUS = 0x9C, IC_FS_SPKLEN = 0xA0, IC_COMP_VERSION = 0xF8,
   IC_COMP_TYPE = 0xFC
};

#define CMD_READ     0x100u
#define CMD_STOP     0x200u
#define CMD_RESTART  0x400u
#define FIRST_BYTE   0x800u

#define RX_UNDER     (1u << 0)
#define RX_OVER      (1u << 1)
#define RX_FULL      (1u << 2)
#define TX_OVER      (1u << 3)
#define TX_EMPTY     (1u << 4)
#define RD_REQ       (1u << 5)
#define TX_ABRT      (1u << 6)
#define RX_DONE      (1u << 7)
#define ACTIVITY     (1u << 8)
#define STOP_DET     (1u << 9)
#define START_DET    (1u << 10)
#define GEN_CALL     (1u << 11)

#define ADDR_NOACK   (1u << 0)
#define TXDATA_NOACK (1u << 3)
#define USER_ABRT    (1u << 16)
#define FLUSH_SHIFT  23

#define ENABLE_BIT   1u
#define ABORT_BIT    2u

#define FIFO         16u

/* A device on a bus: whether it answers its address, for a read or a write, whether it
** takes a byte written, the byte it gives to a read, and the stop. now is the end of the
** byte, when the device sees it. */
typedef struct Device Device;
struct Device {
   uint8_t Address;
   int (*Begin)(Device *d, int read, uint32_t now);
   int (*Put)(Device *d, uint8_t byte, uint32_t now);
   uint8_t (*Get)(Device *d, uint32_t now);
   void (*End)(Device *d, uint32_t now);
};

/* Ahead: Whether at is still to come at now. The clock wraps every 2^32 us, 71.6 min, and
** what the model waits for, a byte or a measure, is never 10 ms ahead: a time further on
** is past, after a page left a long time without a word for the chip. Only an access
** in the last NEAR_US before 71.6 min goes wrong: the time is then still to come, so a
** byte ends up to NEAR_US later, or the SHT4x answers no address until then. */
#define NEAR_US 1000000u

static int Ahead(uint32_t at, uint32_t now)
{
  return at - now - 1u < NEAR_US;
} /* end of Ahead */

/* A triangle of period us, from 0 to 1000 and back, for what the sensors measure. */
static uint32_t Triangle(uint32_t now, uint32_t period)
{
  uint32_t phase = now % period, half = period / 2;
  return phase < half ? phase / (half / 1000) : (period - phase) / (half / 1000);
} /* end of Triangle */

/* The SHT4x (Sensirion, datasheet version 6.6, 2024): a command, then after its stop the
** measure, during which it answers no address; then a read gives it once, two bytes and
** their CRC for the temperature, then the same for the humidity. A write forgets a read
** that no stop ended: on the bench, a measure asked after a read left held was made
** (2026-11-20). Read on the bench: no
** answer up to 6.5 ms after the command $FD, the measure at 7; a second read without a
** command unanswered. */
typedef struct {
   Device D;
   uint32_t Ready;
   int Busy, Command, Reading, Count, Index;
   uint8_t Out[6];
} Sht;

static uint8_t Crc8(const uint8_t *b, int n)
{
  uint8_t crc = 0xFF;
  int i, k;
  for (i = 0; i < n; i += 1) {
     crc ^= b[i];
     for (k = 0; k < 8; k += 1)
        crc = (uint8_t)(crc & 0x80 ? crc << 1 ^ 0x31 : crc << 1);
  }
  return crc;
} /* end of Crc8 */

static void Word16(uint8_t *out, uint32_t value)
{
  out[0] = (uint8_t)(value >> 8);
  out[1] = (uint8_t)value;
  out[2] = Crc8(out,2);
} /* end of Word16 */

static int ShtBegin(Device *d, int read, uint32_t now)
{
  Sht *s = (Sht *)d;
  if (s->Busy && Ahead(s->Ready,now))
     return 0;
  s->Busy = 0;
  if (read) {
     if (s->Count == 0)
        return 0;
     s->Index = 0;
     s->Reading = 1;
  }
  else {
     s->Reading = 0;
     s->Command = -1;
  }
  return 1;
} /* end of ShtBegin */

static int ShtPut(Device *d, uint8_t byte, uint32_t now)
{
  Sht *s = (Sht *)d;
  (void)now;
  if (s->Command < 0)
     s->Command = byte;
  return 1;
} /* end of ShtPut */

static uint8_t ShtGet(Device *d, uint32_t now)
{
  Sht *s = (Sht *)d;
  (void)now;
  return s->Index < s->Count ? s->Out[s->Index++] : 0xFF;
} /* end of ShtGet */

/* ShtEnd: A command starts at its stop. The temperature, from 20.5 to 22.5 °C, and the
** humidity, from 50 to 55 %, as the datasheet's formulas read them back: the raw value
** rounded up, since they round down. */
static void ShtEnd(Device *d, uint32_t now)
{
  Sht *s = (Sht *)d;
  uint32_t wait = 0, centi;
  if (s->Reading) {
     s->Reading = 0;
     s->Count = 0;
     return;
  }
  switch (s->Command) {
     case 0xFD: wait = 6900; break;   /* high repeatability, typical */
     case 0xF6: wait = 3700; break;   /* medium */
     case 0xE0: wait = 1300; break;   /* low */
     case 0x94: wait = 1000; s->Count = 0; break;   /* soft reset */
     default: return;
  }
  s->Busy = 1;
  s->Ready = now + wait;
  if (s->Command == 0x94)
     return;
  centi = 2050 + Triangle(now,120000000u) / 5;
  Word16(&s->Out[0],((centi + 4500) * 65535u + 17499) / 17500);
  centi = 5000 + Triangle(now + 37000000u,90000000u) / 2;
  Word16(&s->Out[3],((centi + 600) * 65535u + 12499) / 12500);
  s->Count = 6;
} /* end of ShtEnd */

/* The ADXL345 (Analog Devices, datasheet rev. G): a write's first byte sets the register
** pointer, the next ones write from it; a read gives the registers from it. The pointer
** wraps past $3F, as the bench's read past it to DEVID. The axes read 0 until POWER_CTL
** asks it to measure, then the board tilting slowly on x, y about -1 g. */
typedef struct {
   Device D;
   uint8_t Reg[64], Axes[6], Pointer;
   int Fresh;
} Adxl;

#define ADXL_POWER_CTL   0x2D
#define ADXL_DATA_FORMAT 0x31
#define ADXL_DATAX0      0x32

static int16_t Counts(int32_t mg, uint8_t format)
{
  int32_t lsb = 39 << (format & 3), limit = 511, n;
  if (format & 8) {
     lsb = 39;
     limit = (512 << (format & 3)) - 1;
  }
  n = mg * 10 / lsb;
  if (n > limit)
     n = limit;
  if (n < -limit - 1)
     n = -limit - 1;
  return (int16_t)n;
} /* end of Counts */

static int AdxlBegin(Device *d, int read, uint32_t now)
{
  Adxl *a = (Adxl *)d;
  int32_t mg[3];
  int i;
  if (!read) {
     a->Fresh = 1;
     return 1;
  }
  mg[0] = (int32_t)Triangle(now,10000000u) - 500;
  mg[1] = -1000 + (mg[0] < 0 ? -mg[0] : mg[0]) / 4;
  mg[2] = (int32_t)Triangle(now + 2500000u,10000000u) / 5 - 100;
  for (i = 0; i < 3; i += 1) {
     int16_t n = a->Reg[ADXL_POWER_CTL] & 8 ? Counts(mg[i],a->Reg[ADXL_DATA_FORMAT]) : 0;
     a->Axes[2 * i] = (uint8_t)n;
     a->Axes[2 * i + 1] = (uint8_t)((uint16_t)n >> 8);
  }
  return 1;
} /* end of AdxlBegin */

static int AdxlPut(Device *d, uint8_t byte, uint32_t now)
{
  Adxl *a = (Adxl *)d;
  (void)now;
  if (a->Fresh) {
     a->Pointer = byte & 0x3F;
     a->Fresh = 0;
     return 1;
  }
  if (a->Pointer != 0x00 && a->Pointer != 0x30 &&
      !(a->Pointer >= ADXL_DATAX0 && a->Pointer <= 0x39))
     a->Reg[a->Pointer] = byte;
  a->Pointer = (a->Pointer + 1) & 0x3F;
  return 1;
} /* end of AdxlPut */

static uint8_t AdxlGet(Device *d, uint32_t now)
{
  Adxl *a = (Adxl *)d;
  uint8_t p = a->Pointer, v;
  (void)now;
  v = p >= ADXL_DATAX0 && p < ADXL_DATAX0 + 6 ? a->Axes[p - ADXL_DATAX0] : a->Reg[p];
  a->Pointer = (p + 1) & 0x3F;
  return v;
} /* end of AdxlGet */

static void AdxlEnd(Device *d, uint32_t now)
{
  (void)d; (void)now;
} /* end of AdxlEnd */

/* The display at $3C, an SSD1315: it takes every byte. What it draws is not modelled. */
static int OledBegin(Device *d, int read, uint32_t now) { (void)d; (void)read; (void)now; return 1; }
static int OledPut(Device *d, uint8_t byte, uint32_t now) { (void)d; (void)byte; (void)now; return 1; }
static uint8_t OledGet(Device *d, uint32_t now) { (void)d; (void)now; return 0; }
static void OledEnd(Device *d, uint32_t now) { (void)d; (void)now; }

/* A controller. One entry of the TX FIFO is taken ahead, as its byte before ends, as the
** bench's TXFLR fell: 13 left of 14 once the address went out, 12 during the first
** byte. */
typedef struct {
   uint32_t Con, Tar, Sar, SsHcnt, SsLcnt, FsHcnt, FsLcnt, Mask, RxTl, TxTl, SdaHold,
            SdaSetup, Spklen, Enable, Raw, Source;
   uint16_t Tx[FIFO], Rx[FIFO], Next, Doing;
   unsigned TxHead, TxCount, RxHead, RxCount;
   int HasNext, Active, Read, First, Flushed, On;
   enum { IDLE, ADDRESS, BYTE, STOPPING } Phase;
   uint32_t Until;
   Device *Target, *Devices[3];
} I2c;

static Sht Shts[2];
static Adxl Adxls[2];
static Device Oleds[2];
static I2c Port[2];
static uint32_t Reset = RESET_I2C0 | RESET_I2C1;
static uint32_t Ctrl[30], Pad[33];
static int Started;

/* PortReset: A controller as its reset leaves it; its devices, chips of their own, as
** they were. */
static void PortReset(int n)
{
  I2c *p = &Port[n];
  *p = (I2c){0};
  p->Con = 0x65; p->Tar = 0x55; p->Sar = 0x55;
  p->SsHcnt = 0x28; p->SsLcnt = 0x2F; p->FsHcnt = 0x06; p->FsLcnt = 0x0D;
  p->Mask = 0x8FF; p->SdaHold = 1; p->SdaSetup = 0x64; p->Spklen = 7;
  p->Devices[0] = &Shts[n].D;
  p->Devices[1] = &Adxls[n].D;
  p->Devices[2] = &Oleds[n];
} /* end of PortReset */

static void Start(void)
{
  int i;
  if (Started)
     return;
  for (i = 0; i < 30; i += 1)
     Ctrl[i] = 0x1F;
  for (i = 1; i < 33; i += 1)
     Pad[i] = 0x56;
  for (i = 0; i < 2; i += 1) {
     Shts[i] = (Sht){ {0x44,ShtBegin,ShtPut,ShtGet,ShtEnd}, 0,0,-1,0,0,0,{0} };
     Adxls[i] = (Adxl){ {0x53,AdxlBegin,AdxlPut,AdxlGet,AdxlEnd}, {0},{0},0,0 };
     Adxls[i].Reg[0x00] = 0xE5;
     Adxls[i].Reg[0x2C] = 0x0A;
     Oleds[i] = (Device){0x3C,OledBegin,OledPut,OledGet,OledEnd};
     PortReset(i);
  }
  Started = 1;
} /* end of Start */

/* ByteUs: A byte and its acknowledge, 9 periods of SCL, each its high and low counts of
** clk_sys and RISE_CYCLES. */
static uint32_t ByteUs(const I2c *p)
{
  uint32_t speed = p->Con >> 1 & 3;
  uint32_t period = speed == 1 ? p->SsHcnt + p->SsLcnt : p->FsHcnt + p->FsLcnt;
  return 9 * (period + RISE_CYCLES) / CLOCK_MHZ;
} /* end of ByteUs */

static void Take(I2c *p)
{
  if (!p->HasNext && p->TxCount > 0) {
     p->Next = p->Tx[p->TxHead];
     p->TxHead = (p->TxHead + 1) % FIFO;
     p->TxCount -= 1;
     p->HasNext = 1;
  }
} /* end of Take */

/* Disable: The controller off, IC_ENABLE_STATUS 0: both FIFOs emptied, the interrupts
** and the abort cleared. */
static void Disable(I2c *p)
{
  p->On = 0;
  p->Phase = IDLE;
  p->TxCount = p->RxCount = 0;
  p->HasNext = 0;
  p->Flushed = 0;
  p->Raw = 0;
  p->Source = 0;
} /* end of Disable */

/* Stop: The stop sent; a disable asked meanwhile done then. */
static void Stop(I2c *p, uint32_t now)
{
  if (p->Target != 0)
     p->Target->End(p->Target,now);
  p->Target = 0;
  p->Active = 0;
  p->Raw |= STOP_DET;
  if (!(p->Enable & ENABLE_BIT))
     Disable(p);
} /* end of Stop */

/* Abort: The TX FIFO emptied, the entries it held counted, then a stop; until
** IC_CLR_TX_ABRT is read, the FIFO takes no entry, as on the bench. */
static void Abort(I2c *p, uint32_t why, uint32_t now)
{
  p->Source = why | p->TxCount << FLUSH_SHIFT;
  p->TxCount = 0;
  p->HasNext = 0;
  p->Raw |= TX_ABRT;
  p->Flushed = 1;
  p->Phase = STOPPING;
  p->Until = now + STOP_US;
} /* end of Abort */

static void Finish(I2c *p, uint32_t now)
{
  int i;
  if (p->Phase == STOPPING) {
     p->Phase = IDLE;
     Stop(p,now);
     return;
  }
  if (p->Phase == ADDRESS) {
     p->Phase = IDLE;
     p->Target = 0;
     for (i = 0; i < 3; i += 1)
        if (p->Devices[i]->Address == (p->Tar & 0x7F))
           p->Target = p->Devices[i];
     if (p->Target == 0 || !p->Target->Begin(p->Target,p->Read,now)) {
        p->Target = 0;
        Abort(p,ADDR_NOACK,now);
        return;
     }
     p->First = p->Read;
     return;
  }
  p->Phase = IDLE;
  if (p->Doing & CMD_READ) {
     uint16_t v = p->Target->Get(p->Target,now);
     if (p->First)
        v |= FIRST_BYTE;
     p->First = 0;
     if (p->RxCount == FIFO)
        p->Raw |= RX_OVER;
     else {
        p->Rx[(p->RxHead + p->RxCount) % FIFO] = v;
        p->RxCount += 1;
     }
  }
  else if (!p->Target->Put(p->Target,(uint8_t)p->Doing,now)) {
     Abort(p,TXDATA_NOACK,now);
     return;
  }
  if (p->Doing & CMD_STOP) {
     p->Phase = STOPPING;
     p->Until = now + STOP_US;
  }
} /* end of Finish */

/* Next: The next byte started at now, or none: the FIFO empty, the bus held if no stop
** ended the last, as on the bench. A new direction, or a restart, sends the address
** again. A byte read into a full RX FIFO is lost, RX_OVER set, the transfer going on:
** IC_CON's RX_FIFO_FULL_HLD_CTRL is 0, and the bench's Pico did so. */
static int Next(I2c *p, uint32_t now)
{
  int read;
  if (!p->On || p->Flushed)
     return 0;
  Take(p);
  if (!p->HasNext)
     return 0;
  read = (p->Next & CMD_READ) != 0;
  if (!p->Active || read != p->Read || (p->Next & CMD_RESTART)) {
     if (!p->Active)
        p->Raw |= START_DET;
     p->Next &= (uint16_t)~CMD_RESTART;
     p->Until = now + ByteUs(p) + (p->Active ? 0 : START_US);
     p->Active = 1;
     p->Read = read;
     p->Phase = ADDRESS;
  }
  else {
     p->Doing = p->Next;
     p->HasNext = 0;
     p->Phase = BYTE;
     p->Until = now + ByteUs(p);
     Take(p);
  }
  p->Raw |= ACTIVITY;
  return 1;
} /* end of Next */

static void Advance(I2c *p, uint32_t now)
{
  uint32_t at = now;
  for (;;) {
     if (p->Phase != IDLE) {
        if (Ahead(p->Until,now))
           return;
        at = p->Until;
        Finish(p,at);
        if (p->Phase != IDLE)
           continue;
     }
     if (!Next(p,at))
        return;
  }
} /* end of Advance */

static uint32_t Status(const I2c *p)
{
  uint32_t busy = p->Active || p->Phase != IDLE;
  return busy | (p->TxCount < FIFO) << 1 | (p->TxCount == 0) << 2 | (p->RxCount > 0) << 3 |
         (p->RxCount == FIFO) << 4 | busy << 5;
} /* end of Status */

static uint32_t RawIntr(const I2c *p)
{
  uint32_t raw = p->Raw;
  if (p->On) {
     if (p->TxCount <= p->TxTl)
        raw |= TX_EMPTY;
     if (p->RxCount > p->RxTl)
        raw |= RX_FULL;
  }
  return raw;
} /* end of RawIntr */

/* Clear: An IC_CLR_ register read: whether its bits were set, then cleared. */
static uint32_t Clear(I2c *p, uint32_t bits)
{
  uint32_t was = (p->Raw & bits) != 0;
  p->Raw &= ~bits;
  if (bits & TX_ABRT) {
     p->Source = 0;
     p->Flushed = 0;
  }
  return was;
} /* end of Clear */

static uint32_t I2cRead(I2c *p, uint32_t offset, uint32_t now)
{
  uint32_t v;
  Advance(p,now);
  switch (offset) {
     case IC_CON: return p->Con;
     case IC_TAR: return p->Tar;
     case IC_SAR: return p->Sar;
     case IC_DATA_CMD:
        if (p->RxCount == 0) {
           p->Raw |= RX_UNDER;
           return p->Rx[p->RxHead];
        }
        v = p->Rx[p->RxHead];
        p->RxHead = (p->RxHead + 1) % FIFO;
        p->RxCount -= 1;
        Advance(p,now);
        return v;
     case IC_SS_SCL_HCNT: return p->SsHcnt;
     case IC_SS_SCL_LCNT: return p->SsLcnt;
     case IC_FS_SCL_HCNT: return p->FsHcnt;
     case IC_FS_SCL_LCNT: return p->FsLcnt;
     case IC_INTR_STAT: return RawIntr(p) & p->Mask;
     case IC_INTR_MASK: return p->Mask;
     case IC_RAW_INTR_STAT: return RawIntr(p);
     case IC_RX_TL: return p->RxTl;
     case IC_TX_TL: return p->TxTl;
     case IC_CLR_INTR:
        return Clear(p,RX_UNDER | RX_OVER | TX_OVER | RD_REQ | TX_ABRT | RX_DONE |
                       ACTIVITY | STOP_DET | START_DET | GEN_CALL);
     case IC_CLR_RX_UNDER: return Clear(p,RX_UNDER);
     case IC_CLR_RX_OVER: return Clear(p,RX_OVER);
     case IC_CLR_TX_OVER: return Clear(p,TX_OVER);
     case IC_CLR_RD_REQ: return Clear(p,RD_REQ);
     case IC_CLR_TX_ABRT: return Clear(p,TX_ABRT);
     case IC_CLR_RX_DONE: return Clear(p,RX_DONE);
     case IC_CLR_ACTIVITY: return Status(p) & 1 ? 1 : Clear(p,ACTIVITY);
     case IC_CLR_STOP_DET: return Clear(p,STOP_DET);
     case IC_CLR_START_DET: return Clear(p,START_DET);
     case IC_CLR_GEN_CALL: return Clear(p,GEN_CALL);
     case IC_ENABLE: return p->Enable;
     case IC_STATUS: return Status(p);
     case IC_TXFLR: return p->TxCount;
     case IC_RXFLR: return p->RxCount;
     case IC_SDA_HOLD: return p->SdaHold;
     case IC_TX_ABRT_SOURCE: return p->Source;
     case IC_SDA_SETUP: return p->SdaSetup;
     case IC_ENABLE_STATUS: return (uint32_t)p->On;
     case IC_FS_SPKLEN: return p->Spklen;
     case IC_COMP_VERSION: return 0x3230312Au;
     case IC_COMP_TYPE: return 0x44570140u;
     default: return 0;
  }
} /* end of I2cRead */

/* I2cWrite: The settings are written only while IC_ENABLE is 0, as IC_TAR was on the
** bench, the controller still on or not; the TX FIFO only while it is 1. A disable during
** a transfer empties the TX FIFO, a stop still in it too, and the bus stays held: the
** controller does not go off, and the next byte, once enabled again, goes on that
** transfer, to the device before (2026-11-11 and 2026-11-20). A stop under way ends at
** once, the controller off then: the Pico's Forth takes longer than a stop between seeing
** an abort and writing IC_ENABLE, which i2c-scan does, and the page's does not. */
static void I2cWrite(I2c *p, uint32_t offset, uint32_t value, uint32_t now)
{
  int off;
  Advance(p,now);
  off = !(p->Enable & ENABLE_BIT);
  switch (offset) {
     case IC_CON: if (off) p->Con = value & 0x7FF; break;
     case IC_TAR: if (off) p->Tar = value & 0xFFF; break;
     case IC_SAR: if (off) p->Sar = value & 0x3FF; break;
     case IC_SS_SCL_HCNT: if (off) p->SsHcnt = value & 0xFFFF; break;
     case IC_SS_SCL_LCNT: if (off) p->SsLcnt = value & 0xFFFF; break;
     case IC_FS_SCL_HCNT: if (off) p->FsHcnt = value & 0xFFFF; break;
     case IC_FS_SCL_LCNT: if (off) p->FsLcnt = value & 0xFFFF; break;
     case IC_SDA_HOLD: if (off) p->SdaHold = value & 0xFFFFFF; break;
     case IC_SDA_SETUP: if (off) p->SdaSetup = value & 0xFF; break;
     case IC_FS_SPKLEN: if (off) p->Spklen = value & 0xFF; break;
     case IC_INTR_MASK: p->Mask = value & 0x1FFF; break;
     case IC_RX_TL: p->RxTl = value & 0xFF; break;
     case IC_TX_TL: p->TxTl = value & 0xFF; break;
     case IC_DATA_CMD:
        if (off || p->Flushed)
           break;
        if (p->TxCount == FIFO)
           p->Raw |= TX_OVER;
        else {
           p->Tx[(p->TxHead + p->TxCount) % FIFO] = (uint16_t)(value & 0x7FF);
           p->TxCount += 1;
        }
        break;
     case IC_ENABLE:
        p->Enable = value & ENABLE_BIT;
        if (value & ABORT_BIT && !off)
           Abort(p,USER_ABRT,now);
        if (value & ENABLE_BIT)
           p->On = 1;
        else if (p->Phase == STOPPING) {
           p->Phase = IDLE;
           Stop(p,now);
        }
        else if (!p->Active && p->Phase == IDLE)
           Disable(p);
        else {
           p->TxCount = 0;
           p->HasNext = 0;
        }
        break;
     default: break;
  }
  Advance(p,now);
} /* end of I2cWrite */

static uint32_t Alias(uint32_t old, uint32_t value, uint32_t alias)
{
  switch (alias) {
     case ALIAS_XOR: return old ^ value;
     case ALIAS_SET: return old | value;
     case ALIAS_CLR: return old & ~value;
     default: return value;
  }
} /* end of Alias */

/* Block: Which block address is in, its offset in reg, its alias in alias; -1 if none of
** the page's. */
enum { B_SYSINFO, B_RESETS, B_IO, B_PADS, B_I2C0, B_I2C1 };

static int Block(uint32_t address, uint32_t *reg, uint32_t *alias)
{
  uint32_t off;
  *alias = 0;
  if (address % 4 != 0)
     return -1;
  if (address == SYSINFO) {
     *reg = 0;
     return B_SYSINFO;
  }
  if (address >= I2C0_BASE && address < I2C0_BASE + 0x100) {
     *reg = address - I2C0_BASE;
     return B_I2C0;
  }
  if (address >= I2C1_BASE && address < I2C1_BASE + 0x100) {
     *reg = address - I2C1_BASE;
     return B_I2C1;
  }
  if (address >= RESETS && address < RESETS + 0x4000) {
     off = address - RESETS;
     *reg = off & 0xFFF; *alias = off & 0x3000;
     return *reg <= 8 ? B_RESETS : -1;
  }
  if (address >= IO_BANK0 && address < IO_BANK0 + 0x4000) {
     off = address - IO_BANK0;
     *reg = off & 0xFFF; *alias = off & 0x3000;
     return *reg <= 0xEC ? B_IO : -1;
  }
  if (address >= PADS_BANK0 && address < PADS_BANK0 + 0x4000) {
     off = address - PADS_BANK0;
     *reg = off & 0xFFF; *alias = off & 0x3000;
     return *reg <= 0x80 ? B_PADS : -1;
  }
  return -1;
} /* end of Block */

int ChipHas(uint32_t address)
{
  uint32_t reg, alias;
  return Block(address,&reg,&alias) >= 0;
} /* end of ChipHas */

/* A controller held in reset reads 0 and drops what is written. */
uint32_t ChipRead(uint32_t address, uint32_t now)
{
  uint32_t reg, alias;
  int b = Block(address,&reg,&alias);
  Start();
  switch (b) {
     case B_SYSINFO: return CHIP_ID;
     case B_RESETS:
        if (reg == 0) return Reset;
        if (reg == 8) return ~Reset & RESET_ALL;
        return 0;
     case B_IO: return reg % 8 == 4 ? Ctrl[reg / 8] : 0;
     case B_PADS: return reg == 0 ? Pad[0] : Pad[reg / 4];
     case B_I2C0: return Reset & RESET_I2C0 ? 0 : I2cRead(&Port[0],reg,now);
     case B_I2C1: return Reset & RESET_I2C1 ? 0 : I2cRead(&Port[1],reg,now);
     default: return 0;
  }
} /* end of ChipRead */

void ChipWrite(uint32_t address, uint32_t value, uint32_t now)
{
  uint32_t reg, alias, was;
  int b = Block(address,&reg,&alias);
  Start();
  switch (b) {
     case B_RESETS:
        if (reg != 0)
           break;
        was = Reset;
        Reset = Alias(Reset,value,alias) & RESET_ALL;
        if (Reset & ~was & RESET_I2C0) PortReset(0);
        if (Reset & ~was & RESET_I2C1) PortReset(1);
        break;
     case B_IO:
        if (reg % 8 == 4)
           Ctrl[reg / 8] = Alias(Ctrl[reg / 8],value,alias) & 0x3331F;
        break;
     case B_PADS:
        Pad[reg / 4] = Alias(Pad[reg / 4],value,alias) & (reg == 0 ? 1 : 0xFF);
        break;
     case B_I2C0: if (!(Reset & RESET_I2C0)) I2cWrite(&Port[0],reg,value,now); break;
     case B_I2C1: if (!(Reset & RESET_I2C1)) I2cWrite(&Port[1],reg,value,now); break;
     default: break;
  }
} /* end of ChipWrite */
