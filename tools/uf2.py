#!/usr/bin/env python3
# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.
"""A flat binary as a UF2 for the flash of an RP2040 or RP2350, whose bootrom takes 256 bytes a
block (RP2040 datasheet, 2.8.4). The file ends with a sector of the flash, the rest of it padded:
no sector is left half written. The file also blanks the start of the saved dictionary
(store.c): copying it is the way out of a saved boot word that stops the console, on a Pico
with no wire on UART0.

A Pico W's adds the radio's firmware where pico/radio/radio_firmware.h reads it.

    tools/uf2.py IN_BIN OUT_UF2 [pico2 | picow RADIO_BIN]
"""

import struct
import sys

FLASH = 0x10000000
PAGE = 256
SECTOR = 4096
BOARDS = {   # the family, and store.c's STORE_OFFSET
    "pico": (0xE48BFF56, 0x1FC000),
    "pico2": (0xE48BFF59, 0x3FC000),   # RP2350, Arm, Secure
    "picow": (0xE48BFF56, 0x1FC000),
}
RADIO = 0x100000   # RADIO_FIRMWARE of pico/radio/radio_firmware.h, in the flash
FAMILY_FLAG = 0x00002000


def sectors(data, at):
    data = data.ljust((len(data) + SECTOR - 1) // SECTOR * SECTOR, b"\xff")
    return [(FLASH + at + i, data[i:i + PAGE].ljust(PAGE, b"\0")) for i in range(0, len(data), PAGE)]


def uf2(data, board="pico", radio=None):
    family, store = BOARDS[board]
    if (board == "picow") != (radio is not None):
        sys.exit("uf2.py: the radio's firmware is for the Pico W, and it needs it")
    pages = sectors(data, 0)
    if radio is not None:
        if len(data) > RADIO or RADIO + len(radio) > store:
            sys.exit("uf2.py: the radio's firmware over the image or the saved words")
        pages += sectors(radio, RADIO)
    pages += [(FLASH + store + i, b"\xff" * PAGE) for i in range(0, SECTOR, PAGE)]
    out = b""
    for n, (address, page) in enumerate(pages):
        head = struct.pack("<8I", 0x0A324655, 0x9E5D5157, FAMILY_FLAG, address, PAGE,
                           n, len(pages), family)
        out += (head + page).ljust(508, b"\0") + struct.pack("<I", 0x0AB16F30)
    return out


def main():
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    radio = None
    if len(sys.argv) > 4:   # read before the output is opened: an empty UF2 would pass for made
        with open(sys.argv[4], "rb") as f:
            radio = f.read()
    out = uf2(data, sys.argv[3] if len(sys.argv) > 3 else "pico", radio)
    with open(sys.argv[2], "wb") as f:   # opened once all is checked: never left empty
        f.write(out)


if __name__ == "__main__":
    main()
