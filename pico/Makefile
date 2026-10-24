# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.

# The Forth and the pool of slots for a Raspberry Pi Pico, from SRAM, on the kernel of the
# submodule escapement/:  make  gives build/EscapementForth.elf, loaded over SWD as
# Escapement's examples are (its docs/rp2040.md), with set USE_CORE 0: core 1 runs.
CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc

E = ../escapement/Escapement
PORT = $(E)/CORTEX-Mx/RP2040
B = build

INCLUDE = -I. -I../src -I$(PORT) -I$(E)/CORTEX-Mx -I$(E)
# -fno-strict-aliasing as Escapement's examples: its kernel needs it.
CFLAGS = -mcpu=cortex-m0plus -mthumb -Wall -c -g -fdata-sections -ffunction-sections \
         -ffreestanding -O2 -fno-strict-aliasing -MMD -MP
# What must outlive a restart of core 0 (restart.c), out of the .bss its start clears.
CFLAGS += -DFORTH_KEEP='__attribute__((section(".noinit")))'
AFLAGS = -mcpu=cortex-m0plus -mthumb -Wall -c -g -MMD -MP

$(shell mkdir -p $(B))

# make SCRIPT=file.fs: the console types the file at start and logs what it prints
# (console.c), in build-script/.
ifdef SCRIPT
   B = build-script
   CFLAGS += -DSCRIPT -I$(B)
   $(shell mkdir -p $(B))
   $(shell od -An -v -tx1 $(SCRIPT) | sed 's/\([0-9a-f][0-9a-f]\)/0x\1,/g' > $(B)/script.inc.new; \
           cmp -s $(B)/script.inc.new $(B)/script.inc || mv $(B)/script.inc.new $(B)/script.inc)
endif

KERNEL = $(B)/Escapement_RamEntry.o $(B)/Escapement_CortexMx.o $(B)/Escapement_CortexMx_a.o \
         $(B)/Escapement_Atomic.o $(B)/Escapement_Interrupts.o $(B)/Escapement_Timer.o \
         $(B)/Escapement_Processor.o $(B)/Escapement_Core1.o $(B)/EscapementHard.o
FORTH = $(B)/forth.o $(B)/slots.o $(B)/console.o $(B)/restart.o $(B)/main.o

all: $(B)/EscapementForth.elf

$(B)/EscapementForth.elf: $(KERNEL) $(FORTH)
	$(CC) -mcpu=cortex-m0plus -mthumb -Wl,--gc-sections -nostdlib -o $@ $^ \
	   -T$(PORT)/RP2040_SRAM.ld -lgcc

$(B)/%.o: $(PORT)/%.S
	$(CC) $(AFLAGS) $(INCLUDE) -o $@ $<
$(B)/%.o: $(PORT)/%.c
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $<
$(B)/%.o: $(E)/CORTEX-Mx/%.S
	$(CC) $(AFLAGS) $(INCLUDE) -o $@ $<
$(B)/%.o: $(E)/CORTEX-Mx/%.c
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $<
$(B)/%.o: $(E)/%.c
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $<
# Ours with -Wextra too, the kernel's as Escapement builds it.
$(B)/%.o: ../src/%.c
	$(CC) $(CFLAGS) -Wextra -Werror $(INCLUDE) -o $@ $<
$(B)/%.o: %.c
	$(CC) $(CFLAGS) -Wextra -Werror $(INCLUDE) -o $@ $<

clean:
	rm -rf $(B)

.PHONY: all clean

-include $(wildcard $(B)/*.d)
