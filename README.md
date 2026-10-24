# Escapement Forth

A Forth console beside the [Escapement](https://github.com/beber007/escapement) real-time
OS, on a Raspberry Pi Pico: words typed at the console are bound, while the kernel runs,
to periodic tasks created before it started, tried, then admitted. A prototype, step 3
of Escapement's issue #40.

- **Core 1** runs the console: a minimal indirect-threaded Forth of its own (`src/forth.c`),
  on UART0 (GP0 TX, GP1 RX, 115200 8N1), no kernel: one interrupt a millisecond watches
  core 0 and takes what the UART received, and Ctrl-C stops the word the console runs.
- **Core 0** runs Escapement's hard kernel under EDF, and the pool of slots
  (`src/slots.c`): one task for each period, 1, 10, 100 and 1000 ms, each with four
  entries.

```forth
\ words of the board: output high low toggle pins ( masks: 1 15 lshift is GP15 ),
\ ms, overruns, restarts, starts, dropped, fault ( a HardFault, to try the restart )
variable n
: tick  1 n +! ;
' tick 10 every      \ slot 4, on trial: run every 10 ms, its time measured
slots                \ each entry: runs, longest time
4 promote            \ admitted if the promoted words still fit under 70 %
4 stop
```

A word on trial runs and is measured, and nothing is promised for it. `promote` gives it
a worst case of what was measured, a quarter more and 1 µs, if the promoted words and the
pool's own cost (8 µs a release, measured at 6.6 to 7.3 µs on the Pico, issue #40 step 1)
then take at most 70 % of the processor.

Each run has a budget: its worst case once promoted, a tenth of its period on trial. The
inner interpreter checks it every 16 words, at its `NEXT`, and cuts the run short there;
the run is counted over (`slots`), and the word runs again at its next release. A word on
trial that went over is not promoted; promoted, its figures go on from its trial, and a
run that ended as it was promoted counts against it. A word's time is taken on the
clock, less what the slots that preempted it took in their words; the kernel's entries,
the interrupts and those slots' own cost, which the load counts already, are in it.

## Limits

- The budget is the Forth's, not the kernel's (Escapement's issue #17): a word of C is
  not stopped midway, and a run cut short has done what it had done. Between two checks
  a word may run past its worst case, delaying the tasks of later deadlines all the
  same; an instance past its next release is an overrun, which the kernel lets go on and
  counts (`overruns`).
- The check costs some 13 % at most on a loop of `do loop` with nothing in it: 1,000 turns
  took 466 µs at the console, which has no budget, and 529 µs in a slot (Pico W,
  2026-10-24, the two cores' work not quite the same).
- `@`, `!`, `+!`, `c@` and `c!` reach the Forth's memory and the RP2040's map, the
  ROM and the flash read only (`pico/console.c`, `ForthMemory`), a cell at a cell's
  boundary: past it, `bad address` rather than a fault. Within it they may still write
  over the kernel's memory, or a peripheral's register. A cell that is no word compiled
  into a definition, by an immediate word doing `,`, faults the core that runs it. A cell given to `execute`, `is` or `every` is checked against the
  dictionary first.
- What the budget does not reach, a word of C that does not return or a fault, holds
  core 0. Core 1 then sees the 1 ms slot stop beating, and after 3 ms restarts core 0
  (`pico/restart.c`, step 2 of the issue): softly, through an interrupt that rewrites
  where the loop returns, or, 5 ms later, through the NMI and a reset of core 0 alone,
  for interrupts masked or a fault. The kernel starts again; the dictionary, the pool and the console are
  kept in `.noinit`, and the word that held the core is stopped (`failed: stalled`). A
  word that was preempted midway has done what it had done. The word stopped is the
  one running on top when the restart lands: one of the 1 ms slot that held the core
  as it ended, or in its task between two words, leaves another stopped, or none.
- A word typed at the console that loops holds the console until Ctrl-C (`interrupted`);
  the watch of core 0 goes on meanwhile, in core 1's interrupt. What the slots print
  waits in the ring then, and what does not fit is dropped.
- What the slots print goes through a ring of 256 bytes, which drops what it has no room
  for (`dropped`).

## Building

```sh
git clone --recurse-submodules https://github.com/beber007/escapement-forth
make -C test/host run      # the Forth and the pool on the host, under the sanitizers
make -C pico               # pico/build/EscapementForth.elf, loaded into SRAM over SWD
```

With no UART wired, `make -C pico SCRIPT=tools/smoke.fs` (or `tools/restart.fs`) builds an image whose console
types that file at start and keeps what it prints in RAM, which `tools/smoke_board.sh`
reads over SWD on Escapement's bench.

The image loads as Escapement's examples do (its `docs/rp2040.md`), with OpenOCD's
`set USE_CORE 0`: core 1 runs. Escapement is a submodule, pinned to the commit the
prototype was built on.

## Licence

The terms of [LICENSE](LICENSE), Escapement's.
