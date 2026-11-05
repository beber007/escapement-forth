\ Acceleration from an ADXL345 at address $53, in mg, up to 16 g. Paste i2c.fs first.
\ The word reads the last answer, then asks for the next: it never waits in its slot.
0 i2c-port constant bus   \ the port: 0 for I2C0, 1 for I2C1
variable ax  variable ay  variable az  variable bad
: write-reg ( value register -- ) bus data ! $200 or bus data ! ;
: axis ( -- mg )
  bus data @ $FF and  bus data @ $FF and 8 lshift or
  dup $8000 and if $10000 - then 39 * 10 / ;
: request $32 bus data ! 5 0 do $100 bus data ! loop $300 bus data ! ;
: adxl
  bus aborted? if bus clear-abort 1 bad +! then
  bus $78 + @ 5 > if axis ax ! axis ay ! axis az ! then
  request ;
: show ax @ . ay @ . az @ . ." mg  " bad @ . ." bad" cr ;
: adxl-setup
  bus i2c-setup $53 bus i2c-to
  $0B $31 write-reg
  $08 $2D write-reg ;
adxl-setup
' adxl 10 every
' show 1000 every
slots
