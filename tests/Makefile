# Host unit tests for the pure-C modules (no RTOS, no HAL).
#
#   make -C tests          build and run every test
#   make -C tests clean
#
# Needs a host C compiler as HOSTCC (default gcc), e.g. MSYS2 ucrt64 gcc on Windows.

HOSTCC ?= gcc
BUILD  := build

ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif

CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror \
          -I. -I../app/json -I../app/core -I../app/proto

# test name -> sources under test
SRC_test_json := ../app/json/json_writer.c ../app/json/json_parser.c ../app/json/json_reading.c
SRC_test_serial_protocol := ../app/proto/serial_protocol.c

TESTS := test_json test_serial_protocol

BINS := $(addprefix $(BUILD)/,$(addsuffix $(EXE),$(TESTS)))

.PHONY: all test clean
all: test

test: $(BINS)
	@for t in $(BINS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "ALL HOST TESTS PASS"

$(BUILD)/%$(EXE): %.c test_util.h Makefile
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) $< $(SRC_$*) -o $@

clean:
	rm -rf $(BUILD)
