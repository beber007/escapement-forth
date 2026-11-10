\ Several modules on one port, through a Grove I2C Hub: a SHT40 or SHT41 ($44), an ADXL345
\ ($53), and what display adds, such as oled.fs. Paste i2c.fs first.
\ One word serves the bus every millisecond: it reads the last answer, or starts the next
\ exchange. Another one turns the answers into values, every 100 ms.
1 i2c-port constant bus   \ the port: 1 for I2C1, 0 for I2C0
variable bad  variable current  variable tick  variable pending
variable lost  variable held   \ the display's writes lost; the turns the bus was not free
: target ( address -- ) dup current @ <> if 0 current ! dup bus i2c-to current ! else drop then ;
bus data constant fifo
: push ( byte -- ) fifo ! ;
: read6 5 0 do $100 push loop $300 push ;
: copy6 ( address -- )   \ the 6 bytes of the receive FIFO, if they are all there
  bus $78 + @ 5 > if 6 0 do bus data @ over i + c! loop then drop ;
: nothing ;
defer display  ' nothing is display   \ the free turns, for a display
variable sht-raw 2 allot  variable sht-new  variable adxl-raw 2 allot
: take-sht sht-raw copy6 1 sht-new ! ;
: take-adxl adxl-raw copy6 ;
\ Twenty turns. The SHT4x: a measure asked on the first, read on the eleventh. The ADXL345:
\ its 6 registers from $32, on the sixth and the sixteenth. display on the others.
: step
  tick @ dup 1+ 20 mod tick !
  dup 0 = if drop $44 target $2FD push exit then
  dup 10 = if drop $44 target read6 ['] take-sht pending ! exit then
  5 mod 0 = if $53 target $32 push read6 ['] take-adxl pending ! exit then
  display ;
: hub
  \ A run cut by its budget can leave a write with no stop: the controller holds the bus.
  \ After more than 10 turns, the abort bit sends a stop, and the next turn sees an abort.
  bus idle? 0= if 1 held +! held @ 10 > if 3 bus $6C + ! then exit then
  0 held !
  \ An abort is seen once the bus is free: the controller raises it before its stop.
  bus aborted? if 1 bad +! 0 pending !   \ counted first, cleared then
    tick @ 19 + 20 mod 5 mod if 1 lost +! then   \ the last turn was display's
    bus clear-abort exit
  then
  pending @ ?dup if 0 pending ! execute exit then
  step ;
\ The values, from the bytes read.
variable temp  variable humid  variable ax  variable ay  variable az
: crc8 ( crc byte -- crc )
  xor 8 0 do
    dup $80 and if 1 lshift $31 xor else 1 lshift then $FF and
  loop ;
: crc-ok? ( address -- flag ) dup c@ over 1+ c@ rot 2 + c@ >r swap $FF swap crc8 swap crc8 r> = ;
: be16 ( address -- n ) dup c@ 8 lshift swap 1+ c@ or ;
: axis ( address -- mg ) dup c@ swap 1+ c@ 8 lshift or dup $8000 and if $10000 - then 39 * 10 / ;
: measure
  sht-new @ if 0 sht-new !
    sht-raw crc-ok? if sht-raw be16 17500 * 65535 / 4500 - temp ! else 1 bad +! then
    sht-raw 3 + crc-ok? if
      sht-raw 3 + be16 12500 * 65535 / 600 - 0 max 10000 min humid !
    else 1 bad +! then
  then
  adxl-raw axis ax !  adxl-raw 2 + axis ay !  adxl-raw 4 + axis az ! ;
: hub-setup   \ the ADXL345: 16 g, 4 mg a bit, then measuring
  bus i2c-setup 0 current ! $53 target
  $31 push $20B push  begin bus idle? until
  $2D push $208 push  begin bus idle? until
  0 tick !  0 pending !  0 held ! ;
hub-setup
' hub 1 every
' measure 100 every
slots
