#!/usr/bin/env python3
# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.
"""A flat binary as a UF2 for the flash of an RP2040, whose bootrom takes 256 bytes a block
(RP2040 datasheet, 2.8.4).

    tools/uf2.py IN_BIN OUT_UF2
"""

import struct
import sys

FLASH = 0x10000000
PAGE = 256
FAMILY_FLAG = 0x00002000
RP2040 = 0xE48BFF56


def uf2(data):
    pages = [data[i:i + PAGE].ljust(PAGE, b"\0") for i in range(0, len(data), PAGE)]
    out = b""
    for n, page in enumerate(pages):
        head = struct.pack("<8I", 0x0A324655, 0x9E5D5157, FAMILY_FLAG, FLASH + n * PAGE, PAGE,
                           n, len(pages), RP2040)
        out += (head + page).ljust(508, b"\0") + struct.pack("<I", 0x0AB16F30)
    return out


def main():
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    with open(sys.argv[2], "wb") as f:
        f.write(uf2(data))


if __name__ == "__main__":
    main()
