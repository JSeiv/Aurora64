BUILD_DIR := build
include $(N64_INST)/include/n64.mk

N64_ROM_SAVETYPE := none
N64_ROM_REGIONFREE := 1

SRC := main.c
TARGET := phase3_discovery

all: $(TARGET).z64

$(BUILD_DIR)/$(TARGET).elf: $(SRC:%.c=$(BUILD_DIR)/%.o)

$(TARGET).z64: N64_ROM_TITLE := "Aurora P3 Discovery"
$(TARGET).z64: N64_ROM_SAVETYPE := none
$(TARGET).z64: N64_ROM_REGIONFREE := 1

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
