# FreeWili WASM App Build
#
# Requirements: clang with wasm32 target (ships with LLVM/clang 14+)
#   or wasi-sdk from https://github.com/WebAssembly/wasi-sdk
#
# Usage:
#   make                  # build all .wasm apps
#   make rssi_meter.wasm  # build one app
#   make clean
#
# To install on device:
#   Copy .wasm files to /apps/ on the FreeWili flash filesystem

CC = clang
CFLAGS = --target=wasm32 -nostdlib -O2 -I.
LDFLAGS = -Wl,--no-entry -Wl,--export=_start -Wl,--initial-memory=65536 -Wl,-z,stack-size=8192

SOURCES = $(wildcard *.c)
# Exclude the SDK header from source list
SOURCES := $(filter-out freewili.h,$(SOURCES))
WASMS = $(SOURCES:.c=.wasm)

all: $(WASMS)

%.wasm: %.c freewili.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f *.wasm

.PHONY: all clean
