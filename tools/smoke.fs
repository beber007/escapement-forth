variable n
: tick 1 n +! ;
' tick 1 every
: blink 1 15 lshift toggle ;
1 15 lshift output
' blink 100 every
: hi 104 emit 105 emit ;
' hi 1000 every
words
2000 ms slots
0 promote 8 promote 12 promote
2000 ms slots
n @ . overruns . dropped .
100 spend   \ at the console: its slot's Excluded is not set there
: bad drop ;
' bad 10 every
100 ms slots
\ A pin driven high reads back: -1, then 0 once low.
1 16 lshift output  1 16 lshift high  pins 1 16 lshift and 0<> .
1 16 lshift low  pins 1 16 lshift and 0<> .
