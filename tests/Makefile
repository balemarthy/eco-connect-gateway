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
          -I. -I../app/json -I../app/core -I../app/proto -I../app/registry -I../app/http \
          -I../app/ota -I../app/config

# test name -> sources under test (DEFS_<test>: extra -D for that test only)
SRC_test_json := ../app/json/json_writer.c ../app/json/json_parser.c ../app/json/json_reading.c
SRC_test_serial_protocol := ../app/proto/serial_protocol.c
SRC_test_registry := ../app/registry/registry.c ../app/json/json_writer.c
SRC_test_http := ../app/http/http_router.c ../app/http/http_response.c ../app/registry/registry.c \
                 ../app/json/json_writer.c ../app/json/json_parser.c

SRC_test_fsm := ../app/core/gateway_fsm.c

# OTA demo core (pure C): CRC32, block reassembly, RAM store, download state machine
SRC_test_ota := ../app/ota/ota_crc32.c ../app/ota/ota_reasm.c ../app/ota/ota_storage_ram.c \
                ../app/ota/ota_fsm.c
# OTA HTTP pieces + the /ota route, with the router built as in make DEMO=ota
SRC_test_ota_http := ../app/ota/ota_http_msg.c ../app/http/http_router.c ../app/http/http_response.c \
                     ../app/registry/registry.c ../app/json/json_writer.c ../app/json/json_parser.c
DEFS_test_ota_http := -DAPP_DEMO=1 -DAPP_DEMO_OTA=1

TESTS := test_json test_serial_protocol test_registry test_http test_fsm test_ota test_ota_http

BINS := $(addprefix $(BUILD)/,$(addsuffix $(EXE),$(TESTS)))

.PHONY: all test clean
all: test

test: $(BINS)
	@for t in $(BINS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "ALL HOST TESTS PASS"

$(BUILD)/%$(EXE): %.c test_util.h Makefile
	@mkdir -p $(BUILD)
	$(HOSTCC) $(CFLAGS) $(DEFS_$*) $< $(SRC_$*) -o $@

clean:
	rm -rf $(BUILD)
