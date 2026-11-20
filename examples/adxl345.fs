\ Acceleration from an ADXL345 at address $53, in mg, up to 16 g. Paste i2c.fs first.
\ The word reads the last answer, then asks for the next: it never waits in its slot.
\ It asks only once the bus is free, and drops an answer that is not 6 bytes: runs that
\ come one after the other, as in a web page held back, would else shift the axes.
0 i2c-port constant bus   \ the port: 0 for I2C0, 1 for I2C1
variable ax  variable ay  variable az  variable bad
: write-reg ( value register -- ) bus data ! $200 or bus data ! ;
: axis ( -- mg )
  bus data @ $FF and  bus data @ $FF and 8 lshift or
  dup $8000 and if $10000 - then 39 * 10 / ;
: request $32 bus data ! 5 0 do $100 bus data ! loop $300 bus data ! ;
: drop-bytes ( n -- ) dup if 0 do bus data @ drop loop else drop then ;
: adxl
  bus aborted? if bus clear-abort 1 bad +! then
  bus idle? if
    bus $78 + @ dup 6 = if drop axis ax ! axis ay ! axis az ! else drop-bytes then
    request
  then ;
: show ax @ . ay @ . az @ . ." mg  " bad @ . ." bad" cr ;
: adxl-setup
  bus i2c-setup $53 bus i2c-to
  $0B $31 write-reg
  $08 $2D write-reg ;
adxl-setup
' adxl 10 every
' show 1000 every
slots
