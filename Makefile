# eco-connect-gateway - hand-written Makefile
#
#   make            build build/eco-connect-gateway.{elf,hex,bin,map} and print the size
#   make clean      remove build/
#   make V=1        show full compiler command lines
#
# The toolchain (arm-none-eabi-*) and make must be on PATH. On Windows use build.ps1,
# which adds the STM32CubeIDE toolchain to PATH for that one call.

TARGET  := eco-connect-gateway
BUILD   := build

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

# ---------------------------------------------------------------- sources
# Our code
SRC_APP := \
  app/main.c \
  app/log/log.c \
  app/net/net_init.c \
  app/sensor/sensor_task.c \
  app/core/app_task.c \
  app/rtos_hooks.c \
  app/task_heartbeat.c

SRC_BSP := \
  bsp/syscalls.c \
  bsp/system_stm32f4xx.c \
  bsp/clock.c \
  bsp/board.c \
  bsp/hal_timebase_tim.c \
  bsp/stm32f4xx_it.c

SRC_DRV := \
  drivers/uart_console.c \
  drivers/spi_bus.c \
  drivers/i2c_bus.c \
  drivers/enc28j60/enc28j60.c

SRC_ASM := bsp/startup_stm32f411retx.s

# Third-party code (vendored, unmodified)
HAL_DIR := third_party/stm32f4xx_hal
HAL_MODULES := hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_dma hal_dma_ex hal_exti \
               hal_flash hal_flash_ex hal_flash_ramfunc hal_pwr hal_pwr_ex hal_uart \
               hal_tim hal_tim_ex hal_spi
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
  middleware/lwip_port/ethernetif_enc28j60.c

INC := \
  -Iapp \
  -Iapp/log \
  -Iapp/net \
  -Iapp/sensor \
  -Iapp/core \
  -Imiddleware/lwip_port \
  -Ibsp \
  -Idrivers \
  -Idrivers/enc28j60 \
  -I$(HAL_DIR)/Inc \
  -Ithird_party/cmsis/Include \
  -Ithird_party/cmsis/Device/ST/STM32F4xx/Include \
  -I$(RTOS_DIR)/include \
  -I$(RTOS_DIR)/portable/GCC/ARM_CM4F \
  -I$(LWIP_DIR)/src/include \
  -I$(LWIP_DIR)/contrib/ports/freertos/include

LDSCRIPT := bsp/STM32F411RETX_FLASH.ld

SRC_C := $(SRC_APP) $(SRC_BSP) $(SRC_DRV) $(SRC_MW) $(SRC_HAL) $(SRC_RTOS) $(SRC_LWIP)

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
.PHONY: all clean size

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/%.o: %.c Makefile
	@echo "CC  $<"
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

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
