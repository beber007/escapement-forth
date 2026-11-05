\ Temperature and humidity from a SHT40 or SHT41 at address $44. Paste i2c.fs first.
\ The word starts a measure, then reads it at its next run: it never waits in its slot.
1 i2c-port constant bus   \ the port: 1 for I2C1, 0 for I2C0
variable temp  variable humid  variable bad  variable good  variable asked
: crc8 ( crc byte -- crc )
  xor 8 0 do
    dup $80 and if 1 lshift $31 xor else 1 lshift then $FF and
  loop ;
: crc-ok? ( high low crc -- flag ) >r swap $FF swap crc8 swap crc8 r> = ;
: >temp ( raw -- centi-degrees ) 17500 * 65535 / 4500 - ;
: >humid ( raw -- centi-percent ) 12500 * 65535 / 600 - 0 max 10000 min ;
: byte bus data @ $FF and ;
: raw ( -- raw flag ) byte byte byte >r 2dup r> crc-ok? >r swap 8 lshift or r> ;
: take
  raw if >temp temp ! else drop 1 bad +! then
  raw if >humid humid ! 1 good +! else drop 1 bad +! then ;
: ask $2FD bus data ! ;
: fetch 5 0 do $100 bus data ! loop $300 bus data ! ;
: sht
  bus aborted? if bus clear-abort 1 bad +! then
  asked @ if
    fetch 0 asked !
  else
    bus $78 + @ 5 > if take then
    ask 1 asked !
  then ;
: .n ( n -- ) 10 /mod ?dup if recurse then 48 + emit ;
: .centi ( n -- )
  dup 0< if 45 emit negate then
  100 /mod .n 46 emit dup 10 < if 48 emit then .n ;
: show temp @ .centi ." C  " humid @ .centi ." %  " bad @ . ." bad" cr ;
bus i2c-setup
$44 bus i2c-to
' sht 100 every
' show 1000 every
slots
