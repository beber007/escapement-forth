# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.

# The Forth and the pool of slots on the host, under AddressSanitizer and
# UndefinedBehaviorSanitizer:  make run
CC ?= cc
SANITIZE ?= address,undefined
CFLAGS = -std=c99 -Wall -Wextra -Werror -g -O1 -fsanitize=$(SANITIZE) \
         -fno-sanitize-recover=all -I../../src -I.
B = $(or $(BUILD),build)

SOURCES = ../../src/forth.c ../../src/slots.c platform_host.c test_forth.c

$(B)/test_forth: $(SOURCES) ../../src/forth.h ../../src/forth_platform.h ../../src/slots.h platform_host.h
	mkdir -p $(B)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

run: $(B)/test_forth
	./$(B)/test_forth

clean:
	rm -rf $(B)

.PHONY: run clean
