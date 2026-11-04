#!/usr/bin/env python3
# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.
#
# radio_firmware.py HEADER OUT LENGTHS: HEADER is a C header of the CYW43 driver's
# firmware (lib/cyw43-driver/firmware). The script writes the bytes of its first array,
# unchanged, to OUT, for the flash (pico/radio/radio_firmware.h). It writes the
# firmware's lengths, the header's #defines, to LENGTHS.
import os
import re
import sys

text = open(sys.argv[1]).read()
body = text[text.index("{") + 1:text.index("};")]
data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
lengths = re.findall(r"^#define (CYW43_WIFI_FW_LEN|CYW43_CLM_LEN) \((\d+)\)", text, re.M)
if sorted(name for name, _ in lengths) != ["CYW43_CLM_LEN", "CYW43_WIFI_FW_LEN"]:
    sys.exit("radio_firmware.py: no lengths in " + sys.argv[1])
# The driver reads the CLM after the Wi-Fi firmware, whose length it rounds up to 512
# (cyw43_ll.c).
sizes = dict((name, int(n)) for name, n in lengths)
if (sizes["CYW43_WIFI_FW_LEN"] + 511) // 512 * 512 + sizes["CYW43_CLM_LEN"] > len(data):
    sys.exit("radio_firmware.py: lengths past the bytes of " + sys.argv[1])
# The files are written only after the checks, each one whole (renamed into place).
# radio.bin is written first and touched last: if the script stops in between, the header
# is the older file, and make writes both again.
def write(path, content, mode):
    with open(path + ".new", mode) as f:
        f.write(content)


write(sys.argv[2], data, "wb")
write(sys.argv[3], "".join("#define %s (%s)\n" % pair for pair in lengths), "w")
os.replace(sys.argv[2] + ".new", sys.argv[2])
os.replace(sys.argv[3] + ".new", sys.argv[3])
os.utime(sys.argv[2])
