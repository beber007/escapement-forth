\ A word that loops is cut short by its budget, each release, and core 0 goes on; one
\ that faults has core 1 restart core 0, hard, and the pool goes on without it.
variable n
: tick 1 n +! ;
' tick 1 every
: spin begin again ;
' spin 1000 every
3000 ms slots
starts . restarts . . n @ .
: crash fault ;
' crash 100 every
1000 ms slots
restarts . . n @ . overruns . starts .
