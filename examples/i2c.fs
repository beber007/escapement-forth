\ I2C at 100 kHz, on a Pico or a Pico 2. The Grove Shield for Pi Pico has two I2C ports:
\ I2C0 on GP8 (SDA) and GP9 (SCL), I2C1 on GP6 and GP7. Paste this first, once.
\ i2c-port gives a port's registers. The words below take them.
: rp2350? $40000000 @ 12 rshift $FFFF and 4 = ;
: chip ( pico2 pico -- x ) rp2350? if drop else nip then ;
$40020000 $4000C000 chip constant resets
$40028000 $40014000 chip constant io
$40038000 $4001C000 chip constant pads
600 500 chip constant scl-high
900 750 chip constant scl-low
: i2c-port ( port -- i2c ) $8000 $4000 chip * $40090000 $40044000 chip + ;
: port# ( i2c -- port ) $40090000 $40044000 chip <> 1 and ;
: data ( i2c -- address ) $10 + ;
\ The port and the pads. On the Pico the pads stay in reset until something asks, and ignore writes.
: reset-bits ( i2c -- mask ) 16 8 chip swap port# lshift 512 256 chip + ;
: wait-reset ( mask -- ) begin resets 8 + @ over and over = until drop ;
: pin-i2c ( gpio -- )
  dup 8 * 4 + io + 3 swap !
  4 * 4 + pads + dup
  8 swap $2000 + !
  $104 swap $3000 + ! ;
: i2c-setup ( i2c -- )
  dup reset-bits dup resets $3000 + ! wait-reset
  dup port# if 6 pin-i2c 7 pin-i2c else 8 pin-i2c 9 pin-i2c then
  0 over $6C + !
  $65 over !
  scl-high over $1C + ! scl-low over $20 + !
  scl-low 16 / over $A0 + !
  scl-high 13 / 1+ over $7C + !
  1 swap $6C + ! ;
: i2c-to ( address i2c -- ) 0 over $6C + ! swap over 4 + ! 1 swap $6C + ! ;
: aborted? ( i2c -- flag ) $34 + @ $40 and 0<> ;
: clear-abort ( i2c -- ) $54 + @ drop ;
: idle? ( i2c -- flag ) $70 + @ dup 4 and 0<> swap 32 and 0= and ;
\ present? ( address i2c -- flag ): writes one byte 0. A read would miss the SHT4x, which does
\ not answer a read until it has a measure. Only the address going unanswered means absent.
: present? ( address i2c -- flag )
  >r r@ i2c-to $200 r@ data !
  now begin
    r@ aborted? r@ idle? or if drop
      r@ aborted? if r@ $80 + @ 1 and 0= r@ clear-abort else true then r> drop exit
    then
    dup now swap - 10000 >
  until drop r> drop false ;
: digit ( n -- ) dup 10 < if 48 else 87 then + emit ;
: .x2 ( n -- ) dup 4 rshift digit 15 and digit space ;
: i2c-scan ( port -- )
  i2c-port dup i2c-setup 0
  $78 8 do over i swap present? if 1+ i .x2 then loop
  . ." found" cr drop ;
