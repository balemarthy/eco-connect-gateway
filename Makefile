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

DEFS := -DSTM32F411xE

# ---------------------------------------------------------------- sources
# Our code
SRC_APP := \
  app/main.c

SRC_ASM := bsp/startup_stm32f411retx.s

INC := \
  -Iapp \
  -Ibsp \
  -Ithird_party/cmsis/Include \
  -Ithird_party/cmsis/Device/ST/STM32F4xx/Include

LDSCRIPT := bsp/STM32F411RETX_FLASH.ld

SRC_BSP := \
  bsp/syscalls.c

SRC_C := $(SRC_APP) $(SRC_BSP)

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
