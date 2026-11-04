# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.

# The Forth and the pool of slots for a Raspberry Pi Pico, from SRAM, on the kernel of the
# submodule escapement/:  make  gives build/EscapementForth.elf, loaded over SWD as
# Escapement's examples are (its docs/rp2040.md), with set USE_CORE 0: core 1 runs; and
# build/EscapementForth.uf2, the same image for a Pico with no probe (flash_boot.S).
# make BOARD=pico2 does the same for a Pico 2, in build-pico2/.
CROSS_COMPILE ?= arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy

E = ../escapement/Escapement
ifeq ($(BOARD),pico2)
   PORT = $(E)/CORTEX-Mx/RP2350
   B = build-pico2
   SRAM_LD = RP2350_SRAM.ld
   # Without its floating-point unit, as Escapement builds it: the kernel saves no such state.
   CPU = -mcpu=cortex-m33+nofp -mthumb -mfloat-abi=soft -DPICO2
   UF2 = EscapementForth-pico2.uf2
else
   PORT = $(E)/CORTEX-Mx/RP2040
   B = build
   SRAM_LD = RP2040_SRAM.ld
   CPU = -mcpu=cortex-m0plus -mthumb
   UF2 = EscapementForth.uf2
endif
# make BOARD=picow builds the Pico's image plus the radio's Bluetooth (radio/), in
# build-picow/.
ifeq ($(BOARD),picow)
   B = build-picow
   UF2 = EscapementForth-picow.uf2
   RADIO = 1
endif

INCLUDE = -I. -I../src -I$(PORT) -I$(E)/CORTEX-Mx -I$(E)
# -fno-strict-aliasing as Escapement's examples: its kernel needs it.
CFLAGS = $(CPU) -Wall -c -g -fdata-sections -ffunction-sections \
         -ffreestanding -O2 -fno-strict-aliasing -MMD -MP
# What must outlive a restart of core 0 (restart.c), out of the .bss its start clears.
CFLAGS += -DFORTH_KEEP='__attribute__((section(".noinit")))'
AFLAGS = $(CPU) -Wall -c -g -MMD -MP

$(shell mkdir -p $(B))

# make SCRIPT=file.fs: the console types the file at start and logs what it prints
# (console.c), in build-script/.
ifdef SCRIPT
   B := $(B)-script
   CFLAGS += -DSCRIPT -I$(B)
   $(shell mkdir -p $(B))
   $(shell od -An -v -tx1 $(SCRIPT) | sed 's/\([0-9a-f][0-9a-f]\)/0x\1,/g' > $(B)/script.inc.new; \
           cmp -s $(B)/script.inc.new $(B)/script.inc || mv $(B)/script.inc.new $(B)/script.inc)
endif

KERNEL = $(B)/Escapement_RamEntry.o $(B)/Escapement_CortexMx.o $(B)/Escapement_CortexMx_a.o \
         $(B)/Escapement_Atomic.o $(B)/Escapement_Interrupts.o $(B)/Escapement_Timer.o \
         $(B)/Escapement_Processor.o $(B)/Escapement_Core1.o $(B)/EscapementHard.o
FORTH = $(B)/forth.o $(B)/slots.o $(B)/console.o $(B)/store.o $(B)/restart.o $(B)/usb.o $(B)/main.o

# The radio: the CYW43 driver and the pico-sdk's Bluetooth shared bus, each with its own
# warnings. They use a few C library functions of our own (radio/libc).
ifdef RADIO
   CYW43 = ../lib/cyw43-driver/src
   CFLAGS += -DPICO_W
   INCLUDE += -Iradio
   BTSTACK = ../lib/btstack/src
   RADIO_INCLUDE = -Iradio/libc -I$(CYW43) -I../lib/cyw43-driver/firmware -I$(BTSTACK) -I$(B)
   RADIO_BIN = $(B)/radio.bin
   RADIO_OBJS = $(B)/bus.o $(B)/radio.o $(B)/libc.o $(B)/cybt_shared_bus.o \
            $(B)/cybt_shared_bus_driver.o $(B)/cyw43_ll.o $(B)/cyw43_spi.o $(B)/cyw43_ctrl.o \
            $(B)/transport.o $(B)/link.o $(B)/btstack_run_loop.o $(B)/btstack_linked_list.o \
            $(B)/btstack_memory.o $(B)/btstack_memory_pool.o $(B)/btstack_util.o \
            $(B)/btstack_crypto.o $(B)/hci.o $(B)/hci_cmd.o $(B)/hci_dump.o $(B)/l2cap.o \
            $(B)/l2cap_signaling.o $(B)/ad_parser.o $(B)/att_db.o $(B)/att_server.o \
            $(B)/att_dispatch.o $(B)/le_device_db_memory.o $(B)/sm.o \
            $(B)/nordic_spp_service_server.o $(B)/btstack_tlv.o $(B)/hci_event.o
   # The radio's objects go in an archive, which radio/kept.ld names: a restart of core 0
   # keeps their state.
   RADIO_A = $(B)/radio.a
   RADIO_LINK = -Wl,--whole-archive $(RADIO_A) -Wl,--no-whole-archive -Tradio/kept.ld
endif

all: $(B)/EscapementForth.elf $(B)/$(UF2)

$(B)/EscapementForth.elf: $(KERNEL) $(FORTH) $(RADIO_A) $(if $(RADIO),radio/kept.ld)
	$(CC) $(CPU) -Wl,--gc-sections -nostdlib -o $@ $(KERNEL) $(FORTH) $(RADIO_LINK) \
	   -T$(PORT)/$(SRAM_LD) -lgcc -Wl,--defsym=_ForthKeptEnd=ADDR\(.noinit\)+SIZEOF\(.noinit\)

$(B)/image.bin: $(B)/EscapementForth.elf
	$(OBJCOPY) -O binary $< $@
$(B)/flash_boot.o: flash_boot.S $(B)/image.bin
	$(CC) $(AFLAGS) -DIMAGE_BIN='"$(B)/image.bin"' -o $@ $<
ifeq ($(BOARD),pico2)
$(B)/flash.elf: $(B)/flash_boot.o flash_boot_pico2.ld
	$(CC) $(CPU) -nostdlib -o $@ $(B)/flash_boot.o -Tflash_boot_pico2.ld
else
$(B)/Boot2Code.o: $(PORT)/Escapement_Boot2.S
	$(CC) $(AFLAGS) $(INCLUDE) -o $@ $<
$(B)/Boot2.S: $(B)/Boot2Code.o ../escapement/tools/rp2040_boot2.py
	$(OBJCOPY) -O binary -j .boot2 $< $(B)/Boot2.bin
	python3 ../escapement/tools/rp2040_boot2.py $(B)/Boot2.bin $@
$(B)/Boot2.o: $(B)/Boot2.S
	$(CC) $(AFLAGS) -o $@ $<
$(B)/flash.elf: $(B)/Boot2.o $(B)/flash_boot.o flash_boot.ld
	$(CC) $(CPU) -nostdlib -o $@ $(B)/Boot2.o $(B)/flash_boot.o -Tflash_boot.ld
endif
$(B)/$(UF2): $(B)/flash.elf ../tools/uf2.py $(RADIO_BIN)
	$(OBJCOPY) -O binary $< $(B)/flash.bin
	python3 ../tools/uf2.py $(B)/flash.bin $@ $(BOARD) $(RADIO_BIN)

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
# Ours with -Wextra too, the kernel's as Escapement builds it; -fwrapv for the Forth's
# arithmetic, modulo the cell as Forth has it.
$(B)/%.o: ../src/%.c
	$(CC) $(CFLAGS) -Wextra -Werror -fwrapv $(INCLUDE) -o $@ $<
$(B)/%.o: %.c
	$(CC) $(CFLAGS) -Wextra -Werror -fwrapv $(INCLUDE) -o $@ $<
ifdef RADIO
$(B)/radio.a: $(RADIO_OBJS)
	rm -f $@
	$(CROSS_COMPILE)ar rcs $@ $^
# The firmware's bytes and its lengths come from one header. One rule writes both, even
# under -j: the make 3.81 of macOS has no grouped targets.
RADIO_FIRMWARE = ../lib/cyw43-driver/firmware/wb43439A0_7_95_49_00_combined.h
$(B)/radio_lengths.h: $(RADIO_FIRMWARE) ../tools/radio_firmware.py
	python3 ../tools/radio_firmware.py $(RADIO_FIRMWARE) $(B)/radio.bin $@
# If radio.bin alone is gone, the header is current. The header keeps its date, and the
# new copy of it is thrown away.
$(B)/radio.bin: $(B)/radio_lengths.h
	test -f $@ || { python3 ../tools/radio_firmware.py $(RADIO_FIRMWARE) $@ $@.lengths && rm -f $@.lengths; }
$(B)/cyw43_ll.o: $(B)/radio_lengths.h
# The pico-sdk's copies (cybt_*) build as they came. Our own files build as strictly as
# the rest. Make 3.81 takes the first pattern that fits, so cybt_% comes first.
$(B)/cybt_%.o: radio/cybt_%.c
	$(CC) $(CFLAGS) $(INCLUDE) $(RADIO_INCLUDE) -o $@ $<
$(B)/%.o: radio/%.c
	$(CC) $(CFLAGS) -Wextra -Werror -fwrapv $(INCLUDE) $(RADIO_INCLUDE) -o $@ $<
$(B)/%.o: $(CYW43)/%.c
	$(CC) $(CFLAGS) -Wno-unused-function $(INCLUDE) $(RADIO_INCLUDE) -o $@ $<
$(B)/%.o: $(BTSTACK)/%.c
	$(CC) $(CFLAGS) $(INCLUDE) $(RADIO_INCLUDE) -o $@ $<
$(B)/%.o: $(BTSTACK)/ble/%.c
	$(CC) $(CFLAGS) $(INCLUDE) $(RADIO_INCLUDE) -o $@ $<
$(B)/%.o: $(BTSTACK)/ble/gatt-service/%.c
	$(CC) $(CFLAGS) $(INCLUDE) $(RADIO_INCLUDE) -o $@ $<
# Without PyCryptodome or openssl's CMAC, compile_gatt.py hashes the database at random.
# The image could not be built again the same way, so the build refuses it.
$(B)/console_gatt.h: radio/console.gatt ../lib/btstack/tool/compile_gatt.py \
                    $(BTSTACK)/ble/gatt-service/nordic_spp_service.gatt
	python3 ../lib/btstack/tool/compile_gatt.py $< $@ -I $(BTSTACK)/ble/gatt-service >/dev/null 2>$@.log; \
	status=$$?; cat $@.log >&2; \
	if [ $$status != 0 ] || grep -q "random value" $@.log; then rm -f $@; exit 1; fi
$(B)/link.o: $(B)/console_gatt.h
endif

clean:
	rm -rf build build-script build-pico2 build-pico2-script build-picow build-picow-script

.PHONY: all clean

-include $(wildcard $(B)/*.d)
