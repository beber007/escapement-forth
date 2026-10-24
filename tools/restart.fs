\ A word that loops, then one that faults: core 1 restarts core 0 each time, softly
\ then hard, and the pool goes on without them.
variable n
: tick 1 n +! ;
' tick 1 every
: spin begin again ;
' spin 1000 every
3000 ms slots
restarts . . n @ .
: crash 1 $C0000000 ! ;
' crash 100 every
1000 ms slots
restarts . . n @ . overruns .
