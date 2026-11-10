\ Text on a Grove OLED Display 0.96" (SSD1315, 128 x 64) at address $3C, on the bus of hub.fs.
\ Paste i2c.fs and hub.fs first: it shows what the two sensors measure. 8 lines of 21
\ characters, in capitals. It sends one character a turn, and only the lines that changed.
\ The font: 5 columns a character, from the space to Z. A column's low bit is its top.
variable font 291 allot
: glyph ( b0 b1 b2 b3 b4 char -- ) 32 - 5 * font + 4 + 5 0 do swap over c! 1- loop drop ;
$00 $00 $00 $00 $00 32 glyph      $00 $00 $5F $00 $00 char ! glyph
$00 $07 $00 $07 $00 char " glyph  $14 $7F $14 $7F $14 char # glyph
$24 $2A $7F $2A $12 char $ glyph  $23 $13 $08 $64 $62 char % glyph
$36 $49 $55 $22 $50 char & glyph  $00 $05 $03 $00 $00 char ' glyph
$00 $1C $22 $41 $00 char ( glyph  $00 $41 $22 $1C $00 char ) glyph
$08 $2A $1C $2A $08 char * glyph  $08 $08 $3E $08 $08 char + glyph
$00 $50 $30 $00 $00 char , glyph  $08 $08 $08 $08 $08 char - glyph
$00 $60 $60 $00 $00 char . glyph  $20 $10 $08 $04 $02 char / glyph
$3E $51 $49 $45 $3E char 0 glyph  $00 $42 $7F $40 $00 char 1 glyph
$42 $61 $51 $49 $46 char 2 glyph  $21 $41 $45 $4B $31 char 3 glyph
$18 $14 $12 $7F $10 char 4 glyph  $27 $45 $45 $45 $39 char 5 glyph
$3C $4A $49 $49 $30 char 6 glyph  $01 $71 $09 $05 $03 char 7 glyph
$36 $49 $49 $49 $36 char 8 glyph  $06 $49 $49 $29 $1E char 9 glyph
$00 $36 $36 $00 $00 char : glyph  $00 $56 $36 $00 $00 char ; glyph
$00 $08 $14 $22 $41 char < glyph  $14 $14 $14 $14 $14 char = glyph
$41 $22 $14 $08 $00 char > glyph  $02 $01 $51 $09 $06 char ? glyph
$32 $49 $79 $41 $3E char @ glyph  $7E $11 $11 $11 $7E char A glyph
$7F $49 $49 $49 $36 char B glyph  $3E $41 $41 $41 $22 char C glyph
$7F $41 $41 $22 $1C char D glyph  $7F $49 $49 $49 $41 char E glyph
$7F $09 $09 $01 $01 char F glyph  $3E $41 $41 $51 $32 char G glyph
$7F $08 $08 $08 $7F char H glyph  $00 $41 $7F $41 $00 char I glyph
$20 $40 $41 $3F $01 char J glyph  $7F $08 $14 $22 $41 char K glyph
$7F $40 $40 $40 $40 char L glyph  $7F $02 $04 $02 $7F char M glyph
$7F $04 $08 $10 $7F char N glyph  $3E $41 $41 $41 $3E char O glyph
$7F $09 $09 $09 $06 char P glyph  $3E $41 $51 $21 $5E char Q glyph
$7F $09 $19 $29 $46 char R glyph  $46 $49 $49 $49 $31 char S glyph
$01 $01 $7F $01 $01 char T glyph  $3F $40 $40 $40 $3F char U glyph
$1F $20 $40 $20 $1F char V glyph  $7F $20 $18 $20 $7F char W glyph
$63 $14 $08 $14 $63 char X glyph  $03 $04 $78 $04 $03 char Y glyph
$61 $51 $49 $45 $43 char Z glyph
\ The text, and the lines that changed since they were sent: one bit a line.
variable text 164 allot
variable cursor  variable dirty  variable line  variable col  variable phase  variable seen
: at ( column line -- ) 21 * + cursor ! ;
: put ( char -- )
  dup 96 > if 32 - then  dup 32 < over 90 > or if drop 32 then
  cursor @ dup 168 < if
    text + 2dup c@ <> if
      c! cursor @ 21 / 1 swap lshift dirty @ or dirty !
    else 2drop then
  else 2drop then
  1 cursor +! ;
: puts ( address length -- ) 0 do dup i + c@ put loop drop ;
: digits ( u -- ) 10 /mod ?dup if recurse then 48 + put ;
: #digits ( u -- n ) 1 begin swap 10 / dup while swap 1+ repeat drop ;
: lead ( n width used -- u )   \ the spaces, then the sign
  - over 0< if 1- then  begin dup 0 > while 32 put 1- repeat drop
  dup 0< if 45 put negate then ;
: field ( n width -- ) over abs #digits lead digits ;   \ n at the right of width characters
: centi ( n width -- )   \ n hundredths, the same way
  over abs 100 / #digits 3 + lead  100 /mod digits 46 put  10 /mod 48 + put 48 + put ;
\ A write ends with $200 on its last byte, the stop. After the address, $00 says commands
\ follow, $40 pixels.
\ The set-up, one write a turn: the settings, then all the pixels off, then the display on,
\ in page addressing: each character then says where it goes, and sent twice, it does no harm.
: setting ( phase -- )
  $3C target
  dup 0 = if 0 push $AE push $D5 push $80 push $A8 push $3F push $D3 push 0 push
    $40 push $8D push $14 push $20 push $200 push then
  dup 1 = if 0 push $A1 push $C8 push $DA push $12 push $81 push $CF push $D9 push
    $F1 push $DB push $40 push $A4 push $A6 $200 or push then
  dup 2 = if 0 push $21 push 0 push 127 push $22 push 0 push $207 push then
  dup 2 > over 131 < and if $40 push 7 0 do 0 push loop $200 push then
  131 = if 0 push $20 push $02 push $2AF push $FF dirty ! -1 col ! then ;
: oled
  lost @ seen @ <> if   \ a write lost: the set-up again, or the lines
    phase @ 0< if $FF dirty ! -1 col ! else 0 phase ! then  lost @ seen !
  then
  \ Phase 132 writes nothing: the last write's abort is seen still in the set-up.
  phase @ 0< 0= if phase @ dup setting 1+ dup 132 > if drop -1 then phase ! exit then
  col @ 0< if
    dirty @ 0= if exit then
    0 begin 1 over lshift dirty @ and 0= while 1+ repeat
    dup line !  0 col !  1 swap lshift invert dirty @ and dirty !  exit
  then
  \ $80 says one command follows: the page, then the column's low and high halves.
  $3C target  $80 push line @ $B0 or push
  col @ 6 * $80 push dup 15 and push $80 push 4 rshift $10 or push
  text line @ 21 * + col @ + c@ 32 - 5 * font +
  $40 push 5 0 do dup i + c@ push loop drop $200 push   \ a character, and a blank column
  1 col +!  col @ 20 > if -1 col ! then ;
: blank-text 168 0 do 32 text i + c! loop ;   \ at the console: too long for a turn
: screen
  0 0 at s" ESCAPEMENT FORTH" puts
  0 2 at s" T" puts temp @ 8 centi s"  C" puts
  0 3 at s" H" puts humid @ 8 centi s"  %" puts
  0 4 at s" X" puts ax @ 8 field s"  MG" puts
  0 5 at s" Y" puts ay @ 8 field s"  MG" puts
  0 6 at s" Z" puts az @ 8 field s"  MG" puts
  0 7 at bad @ 6 field s"  BAD" puts ;
blank-text  0 dirty !  -1 col !  0 phase !  lost @ seen !
' oled is display
' screen 1000 every
slots
