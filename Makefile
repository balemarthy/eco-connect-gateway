# eco-connect-gateway - hand-written Makefile
#
#   make            build build/eco-connect-gateway.{elf,hex,bin,map} and print the size
#   make clean      remove build/
#   make V=1        show full compiler command lines
#   make test       build and run the host unit tests (needs a host gcc)
#   make DEMO=x     add one demo task: none (default) | tcp_echo | mqtt | plusio | ota
#                   (output in build/demo-x/, see docs/demos/)
#   make DEMO=ota OTA_STORE=ram      OTA image in RAM instead of the FatFs file (build/demo-ota-ram/)
#   make EXTRA_DEFS="-DNAME=value"   override a setting of app/config/app_config.h
#
# The toolchain (arm-none-eabi-*) and make must be on PATH. On Windows use build.ps1,
# which adds the STM32CubeIDE toolchain to PATH for that one call.

TARGET  := eco-connect-gateway
BUILD   := build

# plain "make" builds the firmware, whatever rule comes first in this file
.DEFAULT_GOAL := all

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
AS      := $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

ifeq ($(V),1)
Q :=
else
Q := @
endif

# ---------------------------------------------------------------- target CPU
MCU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

DEFS := -DSTM32F411xE -DUSE_HAL_DRIVER

# make DHCP=1 : take the address from a DHCP server instead of the static one
DHCP ?= 0
DEFS += -DNET_USE_DHCP=$(DHCP)

# make SENSOR=i2c : real I2C sensor (mentee task) instead of the simulated source
SENSOR ?= sim
ifeq ($(SENSOR),i2c)
DEFS += -DSENSOR_USE_I2C=1
endif

# make WATCHDOG=0 : supervisor still reports missed tasks but never starts the IWDG
WATCHDOG ?= 1
DEFS += -DSUP_USE_IWDG=$(WATCHDOG)

# make DEMO=<name> : one optional demo task (docs/demos/, settings in app/config/app_config.h).
# DEMO=none (default) adds no define, no source and no include path: the firmware is the plain gateway.
# Each demo builds into its own folder (build/demo-<name>/), so switching never mixes objects.
DEMO ?= none
DEMOS := none tcp_echo mqtt plusio ota
ifeq ($(filter $(DEMO),$(DEMOS)),)
$(error DEMO=$(DEMO) is not one of: $(DEMOS))
endif

# ---------------------------------------------------------------- sources
# Our code
SRC_APP := \
  app/main.c \
  app/log/log.c \
  app/net/net_init.c \
  app/sensor/sensor_task.c \
  app/core/app_task.c \
  app/core/gateway_fsm.c \
  app/core/supervisor.c \
  app/json/json_writer.c \
  app/json/json_parser.c \
  app/json/json_reading.c \
  app/proto/serial_protocol.c \
  app/proto/proto_task.c \
  app/registry/registry.c \
  app/registry/commands.c \
  app/http/http_response.c \
  app/http/http_router.c \
  app/http/http_server.c \
  app/storage/storage_task.c \
  app/rtos_hooks.c \
  app/task_heartbeat.c

SRC_BSP := \
  bsp/syscalls.c \
  bsp/system_stm32f4xx.c \
  bsp/clock.c \
  bsp/board.c \
  bsp/hal_timebase_tim.c \
  bsp/iwdg.c \
  bsp/stm32f4xx_it.c

# IO layer: open/read/write/ioctl by path, board table, UART/SPI/I2C drivers, transfer modes
SRC_IO := \
  app/io/io_core.c \
  app/io/io_board.c \
  app/io/io_mode_polled.c \
  app/io/io_mode_circ_buf.c \
  app/io/io_mode_char_queue.c \
  app/io/io_mode_zero_copy.c \
  app/io/io_uart.c \
  app/io/io_spi.c \
  app/io/io_i2c.c

# device drivers on top of the IO layer
SRC_DRV := \
  drivers/w25q/w25q.c \
  drivers/enc28j60/enc28j60.c

SRC_ASM := bsp/startup_stm32f411retx.s

# Third-party code (vendored, unmodified)
HAL_DIR := third_party/stm32f4xx_hal
HAL_MODULES := hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_dma hal_dma_ex hal_exti \
               hal_flash hal_flash_ex hal_flash_ramfunc hal_pwr hal_pwr_ex hal_uart \
               hal_tim hal_tim_ex hal_spi hal_i2c
SRC_HAL := $(foreach m,$(HAL_MODULES),$(HAL_DIR)/Src/stm32f4xx_$(m).c)

RTOS_DIR := third_party/freertos
SRC_RTOS := \
  $(RTOS_DIR)/tasks.c \
  $(RTOS_DIR)/queue.c \
  $(RTOS_DIR)/list.c \
  $(RTOS_DIR)/timers.c \
  $(RTOS_DIR)/event_groups.c \
  $(RTOS_DIR)/stream_buffer.c \
  $(RTOS_DIR)/portable/GCC/ARM_CM4F/port.c \
  $(RTOS_DIR)/portable/MemMang/heap_4.c

LWIP_DIR := third_party/lwip
SRC_LWIP := \
  $(wildcard $(LWIP_DIR)/src/core/*.c) \
  $(wildcard $(LWIP_DIR)/src/core/ipv4/*.c) \
  $(wildcard $(LWIP_DIR)/src/api/*.c) \
  $(LWIP_DIR)/src/netif/ethernet.c \
  $(LWIP_DIR)/contrib/ports/freertos/sys_arch.c

SRC_MW := \
  middleware/lwip_port/port_misc.c \
  middleware/lwip_port/ethernetif_enc28j60.c \
  middleware/fatfs_port/diskio.c

FATFS_DIR := third_party/fatfs
SRC_FATFS := $(FATFS_DIR)/source/ff.c

INC := \
  -Iapp \
  -Iapp/log \
  -Iapp/net \
  -Iapp/sensor \
  -Iapp/core \
  -Iapp/json \
  -Iapp/proto \
  -Iapp/registry \
  -Iapp/http \
  -Iapp/storage \
  -Iapp/io \
  -Imiddleware/fatfs_port \
  -I$(FATFS_DIR)/source \
  -Imiddleware/lwip_port \
  -Ibsp \
  -Idrivers \
  -Idrivers/enc28j60 \
  -Idrivers/w25q \
  -I$(HAL_DIR)/Inc \
  -Ithird_party/cmsis/Include \
  -Ithird_party/cmsis/Device/ST/STM32F4xx/Include \
  -I$(RTOS_DIR)/include \
  -I$(RTOS_DIR)/portable/GCC/ARM_CM4F \
  -I$(LWIP_DIR)/src/include \
  -I$(LWIP_DIR)/contrib/ports/freertos/include

# ---------------------------------------------------------------- demos (make DEMO=...)
# SRC_DEMO: our demo code; SRC_DEMO_3P: vendored code a demo needs. Both empty for DEMO=none.
SRC_DEMO :=
SRC_DEMO_3P :=
ifneq ($(DEMO),none)
BUILD := build/demo-$(DEMO)
DEFS  += -DAPP_DEMO=1
INC   += -Iapp/config -Iapp/demos
endif

# DEMO=tcp_echo : echo server on TCP_ECHO_PORT (netconn)
ifeq ($(DEMO),tcp_echo)
DEFS     += -DAPP_DEMO_TCP_ECHO=1
SRC_DEMO += app/demos/tcp_echo.c
endif

# DEMO=mqtt : coreMQTT client over an lwIP netconn, publishes the latest reading JSON
MQTT_DIR := third_party/coreMQTT
ifeq ($(DEMO),mqtt)
DEFS        += -DAPP_DEMO_MQTT=1
SRC_DEMO    += app/mqtt/mqtt_demo.c app/mqtt/mqtt_transport_lwip.c
SRC_DEMO_3P += $(MQTT_DIR)/source/core_mqtt.c \
               $(MQTT_DIR)/source/core_mqtt_serializer.c \
               $(MQTT_DIR)/source/core_mqtt_state.c
INC         += -Iapp/mqtt -I$(MQTT_DIR)/source/include -I$(MQTT_DIR)/source/interface
endif

# DEMO=plusio : small demo task of the IO layer API (app/io/, docs/io-layer.md)
ifeq ($(DEMO),plusio)
DEFS     += -DAPP_DEMO_PLUSIO=1
SRC_DEMO += app/demos/plusio_demo.c
endif

# DEMO=ota : image download by HTTP Range into a FatFs file (default) or a RAM store, size + CRC32 check
#   make DEMO=ota OTA_STORE=ram : 16 KB RAM buffer instead (no W25Q needed, +16 KB RAM)
# Both stores are compiled (so both stay warning-free); --gc-sections drops the unused one.
OTA_STORE ?= fatfs
ifeq ($(DEMO),ota)
DEFS     += -DAPP_DEMO_OTA=1
SRC_DEMO += app/ota/ota_crc32.c app/ota/ota_reasm.c app/ota/ota_fsm.c \
            app/ota/ota_storage_ram.c app/ota/ota_storage_fatfs.c \
            app/ota/ota_http_msg.c app/ota/ota_http.c app/ota/ota_task.c
INC      += -Iapp/ota
ifeq ($(OTA_STORE),ram)
DEFS     += -DOTA_STORAGE=OTA_STORAGE_RAM
BUILD    := build/demo-ota-ram
else ifneq ($(OTA_STORE),fatfs)
$(error OTA_STORE=$(OTA_STORE) is not one of: fatfs ram)
endif
endif

# make EXTRA_DEFS="-DMQTT_QOS=0 ..." : override any #ifndef setting of app/config/app_config.h
DEFS += $(EXTRA_DEFS)

LDSCRIPT := bsp/STM32F411RETX_FLASH.ld

SRC_C := $(SRC_APP) $(SRC_IO) $(SRC_BSP) $(SRC_DRV) $(SRC_MW) $(SRC_HAL) $(SRC_RTOS) $(SRC_LWIP) $(SRC_FATFS) \
         $(SRC_DEMO) $(SRC_DEMO_3P)

# ---------------------------------------------------------------- flags
OPT    := -Os -g3
CSTD   := -std=gnu11
WARN   := -Wall -Wextra
CFLAGS := $(MCU) $(DEFS) $(INC) $(OPT) $(CSTD) $(WARN) \
          -ffunction-sections -fdata-sections -fno-common -MMD -MP
ASFLAGS := $(MCU) $(DEFS) $(INC) -g3 -MMD -MP
LDFLAGS := $(MCU) -T$(LDSCRIPT) --specs=nano.specs \
           -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments
LDLIBS := -Wl,--start-group -lc -lm -Wl,--end-group

OBJ := $(patsubst %.c,$(BUILD)/%.o,$(SRC_C)) $(patsubst %.s,$(BUILD)/%.o,$(SRC_ASM))
DEP := $(OBJ:.o=.d)

# ---------------------------------------------------------------- rules
.PHONY: all clean size test

# Host unit tests (host gcc, see tests/Makefile)
HOSTCC ?= gcc
test:
	$(MAKE) -C tests HOSTCC=$(HOSTCC)

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/%.o: %.c Makefile
	@echo "CC  $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# coreMQTT checks its parameters with assert(); with NDEBUG those become no-ops instead of
# pulling newlib's __assert_func + fprintf into the image (the functions still return errors).
$(BUILD)/$(MQTT_DIR)/%.o: CFLAGS += -DNDEBUG

$(BUILD)/%.o: %.s Makefile
	@echo "AS  $<"
	@mkdir -p $(dir $@)
	$(Q)$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ) $(LDSCRIPT)
	@echo "LD  $@"
	$(Q)$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(Q)$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(Q)$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	$(Q)$(SIZE) $<

clean:
	@echo "RM  $(BUILD)"
	$(Q)rm -rf $(BUILD)

-include $(DEP)
