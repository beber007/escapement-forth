\ The I2C reference: exchanges on I2C1 of a Pico, through a Grove I2C Hub to a display at
\ $3C, a SHT4x at $44 and an ADXL345 at $53. Pasted after examples/i2c.fs on a Pico just
\ started, its output is i2c_reference.txt; test_web.js pastes it into the page's model
\ and compares (chip_web.c).
\ The SHT4x answers no address while it measures: each of its scenarios waits first.
\ A scenario prints its name, then a line each time the registers change, its first us
\ since go, just before the first entry is written:
\ IC_STATUS, IC_RAW_INTR_STAT, IC_TXFLR, IC_RXFLR, IC_TX_ABRT_SOURCE.
1 i2c-port constant bus
variable buf 4000 allot  variable n  variable t0  variable last
: push ( x -- ) bus data ! ;
: reg ( offset -- x ) bus + @ ;
: at ( address -- ) bus i2c-to bus clear-abort ;
: entry ( i -- address ) 24 * buf + ;
: put ( t -- ) n @ entry ! $70 reg n @ entry 4 + ! $34 reg n @ entry 8 + !
  $74 reg n @ entry 12 + ! $78 reg n @ entry 16 + ! $80 reg n @ entry 20 + ! ;
: next? ( -- flag ) now last @ - 39 > n @ 160 < and ;
: go 0 n ! now t0 ! now 40 - last ! ;
: watch ( us -- ) begin next? if now dup last ! t0 @ - put 1 n +! then now t0 @ - over > until drop ;
: same? ( i -- flag ) true swap dup entry 4 + swap 1- entry 4 +
  20 0 do over i + @ over i + @ <> if rot drop false rot rot then 4 +loop 2drop ;
: show n @ 0 do i 0= if true else i same? 0= then if i entry 6 0 do dup i cells + @ . loop
  drop cr then loop ;
: rx ( n -- ) ." rx " 0 do bus data @ $F00 and . loop cr ;
: idle begin bus idle? until ;
: pause ( us -- ) now begin now over - >r over r> < until 2drop ;
: answer $34 reg $40 and if ." nack" else ." data" then cr ;
: id ." id " $40000000 @ . bus @ . cr ;
: ack ." ack" cr $44 at go 0 $200 or push 2000 watch show ;
: absent ." absent" cr $50 at go 0 $200 or push 2000 watch show ;
: absent-read ." absent-read" cr $50 at go $300 push 2000 watch show ;
: write ." write" cr $3C at go 0 push 12 0 do $E3 push loop $2E3 push 2000 watch show ;
: overflow ." overflow" cr $3C at go 0 push 18 0 do $E3 push loop $2E3 push 3000 watch show ;
: rest 10000 pause ;
: measure ." measure" cr rest $44 at go $2FD push 1000 watch show ;
: read6 5 0 do $100 push loop $300 push ;
: busy ." busy" cr rest $44 at $2FD push idle 1000 pause go read6 1500 watch show 6 rx ;
: ready ( us -- ) ." ready " dup . rest $44 at $2FD push idle pause read6 idle answer 6 rx ;
: again ." again" cr rest $44 at $2FD push idle 10000 pause read6 idle answer 6 rx read6 idle answer ;
: adxl ." adxl" cr $53 at go $32 push read6 2000 watch show 6 rx ;
: devid ." devid" cr $53 at go 0 push $300 push 1000 watch show bus data @ $FF and . cr ;
: hold ." hold" cr $53 at go $2D push 600 watch show go $208 push 400 watch show ;
: user ." user" cr $53 at $2D push 300 pause go 3 bus $6C + ! 400 watch show bus $6C + @ . cr ;
: flushed ." flushed" cr $50 at 0 $200 or push idle $53 bus 4 + ! go 1 push 2 push 500 watch show ;
: cleared ." cleared" cr $54 reg . cr go 1 push $202 push 500 watch show ;
: full ." full" cr $53 at go $32 push 19 0 do $100 push loop $300 push 3000 watch show ;
: drain ." drain" cr 16 rx go 1000 watch show 4 rx ;
: paced ( n -- ) 0 do begin $74 reg 8 < until $100 push loop ;
: rx-full ." rx-full" cr $53 at go $32 push 19 paced $300 push 3000 watch show ;
: rx-drain ." rx-drain" cr 16 rx go 1000 watch show 6 rx ;
: under ." under" cr $53 at go 2 rx 200 watch show ;
: tar ." tar " bus 4 + @ . $44 bus 4 + ! bus 4 + @ . cr ;
: adxl@ ( register -- ) $53 at push $300 push idle bus data @ $FF and . ;
: back ." back " $31 adxl@ $2D adxl@ cr ;
: pair ." pair" cr $53 at go $31 push $20A push $2D push $208 push 2000 watch show back ;
: repeated ." repeated" cr $53 at go $2D push $431 push $20B push 2000 watch show back ;
: late-off ." late-off" cr $53 at $2D push $208 push 0 bus $6C + ! 3000 pause
  $9C reg . $44 bus 4 + ! bus 4 + @ . 1 bus $6C + ! cr ;
bus i2c-setup
id ack absent absent-read write overflow measure busy
6000 ready 6500 ready 7000 ready 7500 ready
again adxl devid hold user flushed cleared full drain rx-full rx-drain under tar
pair repeated late-off
