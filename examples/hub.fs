\ Several modules on one port, through a Grove I2C Hub: a SHT40 or SHT41 ($44), an ADXL345
\ ($53), and what display adds, such as oled.fs. Paste i2c.fs first.
\ One word serves the bus every millisecond, every 10 ms on a Pico: it reads the last
\ answer, or starts the next exchange. Another one turns the answers into values, every
\ 100 ms.
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
\ Each answer has a count: odd while hub writes the answer, even once it is all there.
\ A run cut by its budget can stop in the middle, and leave the count odd.
variable sht-raw 2 allot  variable sht-count  variable adxl-raw 2 allot  variable adxl-count
: begin-write ( count -- ) dup @ 1 or swap ! ;
: end-write ( count -- ) 1 swap +! ;
: take-sht sht-count begin-write sht-raw copy6 sht-count end-write ;
: take-adxl adxl-count begin-write adxl-raw copy6 adxl-count end-write ;
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
variable sample 2 allot  variable sht-seen
\ hub can run in the middle of measure, and write an answer there: a copy that it cut
\ would mix two answers. So the copy is made again until no answer came during it. An odd
\ count is an answer being written, or left half written by a cut run: measure then keeps
\ its last values (#53).
: copy-bytes ( from to -- ) 6 0 do over i + c@ over i + c! loop 2drop ;
: snapshot ( raw count -- n )   \ the 6 bytes into sample, and the count they go with
  begin
    dup @ >r  over sample copy-bytes
    dup @ r@ =  r@ 1 and or if r> nip nip exit then  r> drop
  again ;
: whole? ( n -- flag ) 1 and 0= ;
: crc8 ( crc byte -- crc )
  xor 8 0 do
    dup $80 and if 1 lshift $31 xor else 1 lshift then $FF and
  loop ;
: crc-ok? ( address -- flag ) dup c@ over 1+ c@ rot 2 + c@ >r swap $FF swap crc8 swap crc8 r> = ;
: be16 ( address -- n ) dup c@ 8 lshift swap 1+ c@ or ;
: axis ( address -- mg ) dup c@ swap 1+ c@ 8 lshift or dup $8000 and if $10000 - then 39 * 10 / ;
: measure
  sht-raw sht-count snapshot dup whole? over sht-seen @ <> and if sht-seen !
    sample crc-ok? if sample be16 17500 * 65535 / 4500 - temp ! else 1 bad +! then
    sample 3 + crc-ok? if
      sample 3 + be16 12500 * 65535 / 600 - 0 max 10000 min humid !
    else 1 bad +! then
  else drop then
  adxl-raw adxl-count snapshot whole? if
    sample axis ax !  sample 2 + axis ay !  sample 4 + axis az !
  then ;
: hub-setup   \ the ADXL345: 16 g, 4 mg a bit, then measuring
  bus i2c-setup 0 current ! $53 target
  $31 push $20B push  begin bus idle? until
  $2D push $208 push  begin bus idle? until
  0 tick !  0 pending !  0 held !  0 sht-count !  0 sht-seen !  0 adxl-count ! ;
hub-setup
\ On a Pico, a turn takes up to 218 µs: more than a tenth of 1 ms, its budget (#52).
' hub 1 10 chip every
' measure 100 every
slots
