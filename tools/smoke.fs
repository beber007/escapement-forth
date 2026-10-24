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
: bad drop ;
' bad 10 every
100 ms slots
