# Escapement Forth

A Forth console beside the [Escapement](https://github.com/beber007/escapement) real-time
OS, on a Raspberry Pi Pico: words typed at the console are bound, while the kernel runs,
to periodic tasks created before it started, tried, then admitted. A prototype, step 3
of Escapement's issue #40.

- **Core 1** runs the console: a minimal indirect-threaded Forth of its own (`src/forth.c`),
  on UART0 (GP0 TX, GP1 RX, 115200 8N1), polled, no kernel.
- **Core 0** runs Escapement's hard kernel under EDF, and the pool of slots
  (`src/slots.c`): one task for each period, 1, 10, 100 and 1000 ms, each with four
  entries.

```forth
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

## Limits

- The kernel enforces no budget (Escapement's issue #17): a word that runs past its
  worst case delays the tasks of later deadlines all the same. Its runs over are counted
  (`slots`), and a word past its next release is an overrun, which the kernel lets go on
  and counts (`overruns`).
- A word's time is measured on the clock, preemptions included: an upper bound of what
  it held the processor.
- `@` and `!` take any address, as in any Forth: a wrong one faults the core that ran
  it. A cell taken for a word is checked against the dictionary first.
- A word that loops for ever holds core 0. Restarting it from core 1, step 2 of the issue,
  is not built in yet.
- What the slots print goes through a ring of 256 bytes, which drops what it has no room
  for (`dropped`).

## Building

```sh
git clone --recurse-submodules https://github.com/beber007/escapement-forth
make -C test/host run      # the Forth and the pool on the host, under the sanitizers
make -C pico               # pico/build/EscapementForth.elf, loaded into SRAM over SWD
```

The image loads as Escapement's examples do (its `docs/rp2040.md`), with OpenOCD's
`set USE_CORE 0`: core 1 runs. Escapement is a submodule, pinned to the commit the
prototype was built on.

## Licence

The terms of [LICENSE](LICENSE), Escapement's.
