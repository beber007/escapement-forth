#!/bin/sh
# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.
#
# smoke_board.sh ELF [SECONDS]: on Escapement's bench (its tools/board_ci.md), loads an
# image of make SCRIPT=... on a Pico, core 1 running, waits, and prints what its console
# printed, read from Log over SWD: no UART needs to be wired.
#
#   OPENOCD=~/opt/openocd-rpi/bin/openocd PROBE=probe1 tools/smoke_board.sh \
#       pico/build-script/EscapementForth.elf 10
#
# The chip is reset whole first through the rescue debug port, which only Raspberry Pi's
# OpenOCD does, and last: the console routes alarm 2 to core 0's NMI (pico/restart.c),
# which SYSCFG keeps through the bench's next reset halt, and its TestTimerEventPico,
# whose events alarm 2 gives, stopped at the first (2026-10-24). The probe's lock is taken as the bench's tools take it. Run from the root
# of this repository, the submodule checked out (escapement/tools/probe.sh); arm-none-eabi-nm
# or NM gives the addresses.
O=${OPENOCD:-openocd}
W=${BOARD_CI_DIR:-$HOME/escapement-rp2040/board-ci}
PROBE=${PROBE:-probe1}
sym() { ${NM:-arm-none-eabi-nm} "$1" | awk -v s="$2" '$3 == s { print "0x" $1 }'; }
LOG=$(sym "$1" Log) LEN=$(sym "$1" LogLength) DONE=$(sym "$1" ScriptDone)
[ -n "$LOG" ] && [ -n "$LEN" ] && [ -n "$DONE" ] || { echo "$1: not a SCRIPT image" >&2; exit 1; }
set -- "$1" "$LOG" "$LEN" "$DONE" "$2"
mkdir "$W/lock-$PROBE" || exit 1
echo $$ > "$W/lock-$PROBE/pid"
trap 'rm -rf "$W/lock-$PROBE"' EXIT
P=$(PROBE=$PROBE sh escapement/tools/probe.sh)
timeout 20 $O -f interface/cmsis-dap.cfg -c "$P" -c "set RESCUE 1" -f target/rp2040.cfg >/dev/null 2>&1
sleep 1
$O -f interface/cmsis-dap.cfg -c "$P" -c "adapter speed 5000" -c "set USE_CORE 0" -f target/rp2040.cfg \
   -c init -c "reset halt" -c "load_image $1" -c "resume 0x20000000" -c exit 2>&1 | grep -i "error\|downloaded\|written"
sleep ${5:-10}
$O -f interface/cmsis-dap.cfg -c "$P" -c "adapter speed 5000" -c "set USE_CORE 0" -f target/rp2040.cfg \
   -c init -c "echo \"D [read_memory $4 32 1] [read_memory $3 32 1]\"" -c "echo \"L [read_memory $2 8 8192]\"" -c exit 2>&1 \
   | python3 -c "
import sys
for l in sys.stdin:
    if l.startswith(\"D \"): d, n = map(lambda x: int(x, 0), l.split()[1:3]); print(\"done\", d, \"length\", n)
    if l.startswith(\"L \"): b = bytes(int(x, 0) for x in l.split()[1:]); print(b[:n].decode(errors=\"replace\").replace(chr(13), \"\"))
"
timeout 20 $O -f interface/cmsis-dap.cfg -c "$P" -c "set RESCUE 1" -f target/rp2040.cfg >/dev/null 2>&1
