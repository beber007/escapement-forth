# Copyright (c) 2026 Bertrand Hurst. All rights reserved.
# Escapement Forth - a Forth console beside the Escapement real-time OS.
# Distributed under the terms of LICENSE at the root of this repository.

# The Forth, the pool of slots and the board's kernel, EscapementHard.c of the submodule
# escapement/, compiled to WebAssembly, for the page of web/:
#   make            build/forth.wasm, beside index.html and worker.js in build/
#   make page       the page loaded in a headless Chrome, in English and in French
#                   (test/check_page.sh): make page CHROME=.../Google\ Chrome
#   make serve      the page on http://localhost:8000
#   make test       build/forth.wasm driven as the worker drives it (test/test_web.js),
#                   the browser's clock read to the us, to 0.1 ms as in Chrome, then to
#                   the ms as in other browsers,
#                   under Node, or JavaScriptCore: make test JS=.../jsc JS_ARGS=--
# A clang with the wasm32 target and wasm-ld (Homebrew's llvm and lld), no C library.
CLANG ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin/clang) clang)
CFLAGS = --target=wasm32 -std=c99 -Wall -Wextra -Werror -O2 -fwrapv -nostdlib \
         -ffreestanding -fno-builtin -I../src -Ikernel -I$(E)
LDFLAGS = -Wl,--no-entry -Wl,--allow-undefined-file=imports.txt
B = $(or $(BUILD),build)
E = ../escapement/Escapement

SOURCES = ../src/forth.c ../src/slots.c platform_web.c kernel/kernel_web.c
HEADERS = ../src/forth.h ../src/forth_platform.h ../src/slots.h $(wildcard kernel/*.h) \
          $(E)/EscapementHard.h $(E)/Escapement_Modes.h

all: $(B)/forth.wasm

# The kernel as its own build warns: -Wall, not -Wextra.
$(B)/EscapementHard.o: $(E)/EscapementHard.c $(HEADERS)
	mkdir -p $(B)
	$(CLANG) $(filter-out -Wextra,$(CFLAGS)) -c -o $@ $<

$(B)/forth.wasm: $(SOURCES) $(B)/EscapementHard.o $(HEADERS) imports.txt index.html view.js worker.js
	$(CLANG) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(B)/EscapementHard.o
	cp index.html view.js worker.js $(B)/

JS ?= node
JS_ARGS ?=

test: $(B)/forth.wasm
	$(JS) test/test_web.js $(JS_ARGS) $(B)/forth.wasm index.html worker.js view.js
	$(JS) test/test_web.js $(JS_ARGS) $(B)/forth.wasm index.html worker.js view.js 0.1
	$(JS) test/test_web.js $(JS_ARGS) $(B)/forth.wasm index.html worker.js view.js 1

CHROME ?= google-chrome

page: $(B)/forth.wasm
	test/check_page.sh "$(CHROME)" $(B)

# The firmware the web page offers, if make -C pico built it.
serve: $(B)/forth.wasm
	-cp ../pico/build/EscapementForth.uf2 ../pico/build-pico2/EscapementForth-pico2.uf2 $(B)/ 2>/dev/null
	cd $(B) && python3 -m http.server 8000

clean:
	rm -rf $(B)

.PHONY: all test page serve clean
