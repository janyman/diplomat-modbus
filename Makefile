###############################################################################
# Diplomat Modbus firmware
#
# Plain avr-libc build for an ATmega328-family device.  Application sources
# belong in src/.  All generated files are kept below BUILD_DIR.
###############################################################################

PROJECT       ?= diplomat-modbus
MCU           ?= atmega328p
# The custom board has no crystal.  Use the ATmega328P's 8 MHz calibrated
# internal RC oscillator; the fuse target below clears CKDIV8 so the CPU
# actually runs at 8 MHz rather than the 1 MHz power-on default.
F_CPU         ?= 8000000UL
BAUD          ?= 9600
I2C_SLAVE_ADDRESS ?= 0x2e
PROGRAMMER	  ?= arduino
PROGRAMMER_PORT ?= /dev/ttyACM0

# Fuse values for an ISP-programmed bare ATmega328P with no bootloader:
#   LFUSE 0xE2: internal 8 MHz RC oscillator, CKDIV8 unprogrammed
#   HFUSE 0xD9: reset to the application, SPI programming enabled
#   EFUSE 0xFF: brown-out detector disabled
# Override these on the command line if the board uses a bootloader or a
# different brown-out threshold.  Fuses are written by the `fuses` target.
FUSE_L        ?= 0xE2
FUSE_H        ?= 0xD9
FUSE_E        ?= 0xFF

BUILD_DIR     ?= build
OBJ_DIR       := $(BUILD_DIR)/obj
DEP_DIR       := $(BUILD_DIR)/dep

CC            := avr-gcc
OBJCOPY       := avr-objcopy
OBJDUMP       := avr-objdump
SIZE          := avr-size
AVRDUDE       := avrdude
AVRDUDE_PORT  := $(if $(strip $(PROGRAMMER_PORT)),-P $(PROGRAMMER_PORT),)

FREEMODBUS_DIR := freemodbus/modbus
AVR_PORT_DIR   := src/mbport

TARGET        := $(BUILD_DIR)/$(PROJECT)
ELF           := $(TARGET).elf
HEX           := $(TARGET).hex
EEP           := $(TARGET).eep
LSS           := $(TARGET).lss
MAP           := $(TARGET).map

# Add application sources under src/ as they are implemented.
APP_SOURCES := $(wildcard src/*.c)

I2C_LIB_SOURCES := avr-i2c-slave/I2CSlave.c

# FreeModbus protocol sources used by the RTU slave.
MODBUS_SOURCES := \
	$(FREEMODBUS_DIR)/mb.c \
	$(FREEMODBUS_DIR)/rtu/mbrtu.c \
	$(FREEMODBUS_DIR)/rtu/mbcrc.c \
	$(FREEMODBUS_DIR)/functions/mbfunccoils.c \
	$(FREEMODBUS_DIR)/functions/mbfuncdiag.c \
	$(FREEMODBUS_DIR)/functions/mbfuncdisc.c \
	$(FREEMODBUS_DIR)/functions/mbfuncholding.c \
	$(FREEMODBUS_DIR)/functions/mbfuncinput.c \
	$(FREEMODBUS_DIR)/functions/mbfuncother.c \
	$(FREEMODBUS_DIR)/functions/mbutils.c

# The demo port is used as the initial avr-libc integration point.  It can be
# replaced by project-specific sources later without changing the build layout.
PORT_SOURCES := \
	$(AVR_PORT_DIR)/portserial.c \
	$(AVR_PORT_DIR)/portevent.c \
	$(AVR_PORT_DIR)/porttimer.c

SOURCES       := $(APP_SOURCES) $(MODBUS_SOURCES) $(PORT_SOURCES) $(I2C_LIB_SOURCES)
OBJECTS       := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPFILES      := $(patsubst %.c,$(DEP_DIR)/%.d,$(SOURCES))

CPPFLAGS      := \
	-I$(FREEMODBUS_DIR)/include \
	-I$(FREEMODBUS_DIR)/rtu \
	-I$(FREEMODBUS_DIR)/functions \
	-I$(AVR_PORT_DIR) \
	-D F_CPU=$(F_CPU) \
	-D MODBUS_BAUD_RATE=$(BAUD) \
	-D I2C_SLAVE_ADDRESS=$(I2C_SLAVE_ADDRESS) \
	-D MB_ASCII_ENABLED=0 \
	-D MB_RTU_ENABLED=1 \
	-D MB_TCP_ENABLED=0 \
	-D RTS_ENABLE

CFLAGS        := -mmcu=$(MCU) -std=gnu11 -Os -Wall -Wextra \
	-ffunction-sections -fdata-sections
LDFLAGS       := -mmcu=$(MCU) -Wl,--gc-sections,-Map=$(MAP),--cref

HEX_FLASH_FLAGS := -R .eeprom
HEX_EEPROM_FLAGS := -j .eeprom \
	--set-section-flags=.eeprom=alloc,load \
	--change-section-lma .eeprom=0

.PHONY: all elf hex eep lss size clean flash fuses read-fuses print-config

# Build the output files and report the final flash/RAM usage.
all: $(HEX) $(EEP) $(LSS) size

elf: $(ELF)
hex: $(HEX)
eep: $(EEP)
lss: $(LSS)

$(ELF): $(OBJECTS) | $(BUILD_DIR)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(HEX): $(ELF)
	$(OBJCOPY) -O ihex $(HEX_FLASH_FLAGS) $< $@

$(EEP): $(ELF)
	$(OBJCOPY) $(HEX_EEPROM_FLAGS) -O ihex $< $@

$(LSS): $(ELF)
	$(OBJDUMP) -h -S $< > $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@) $(dir $(DEP_DIR)/$*.d)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -MT $@ -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

size: $(ELF)
	$(SIZE) --mcu=$(MCU) --format=avr $(ELF)

flash: $(HEX)
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) $(AVRDUDE_PORT) -U flash:w:$(HEX):i

# Program the clock and board fuses over ISP.  This is intentionally separate
# from `flash`: changing the clock source can make a part inaccessible if the
# hardware does not match the selected fuse values.
fuses:
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) $(AVRDUDE_PORT) \
		-U lfuse:w:$(FUSE_L):m -U hfuse:w:$(FUSE_H):m -U efuse:w:$(FUSE_E):m

read-fuses:
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) $(AVRDUDE_PORT) \
		-U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h

print-config:
	@echo "MCU=$(MCU) F_CPU=$(F_CPU) BAUD=$(BAUD) I2C_SLAVE_ADDRESS=$(I2C_SLAVE_ADDRESS)"
	@echo "BUILD_DIR=$(BUILD_DIR)"
	@echo "SOURCES=$(SOURCES)"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPFILES)
