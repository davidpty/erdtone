##############################################################################
# AVR Makefile (Linux, selectable clock)
##############################################################################

DEVICE     = attiny85

# -------- Clock selection --------
# Usage:
#   make CLOCK_MODE=4   (default, external crystal)
#   make CLOCK_MODE=8   (internal)


CLOCK_MODE ?= 4

ifeq ($(CLOCK_MODE),4)
    CLOCK = 4000000
    FUSES = -U lfuse:w:0xFF:m -U hfuse:w:0xD7:m -U efuse:w:0xFF:m
else
    CLOCK = 8000000
    FUSES = -U lfuse:w:0xFD:m -U hfuse:w:0xD7:m -U efuse:w:0xFF:m
endif

# -------- Programmer --------
PROGRAMMER = -c usbasp

# -------- Sources --------
SRCS = main.c dtmf.c
OBJS = $(SRCS:.c=.o)

# -------- Tools --------
CC       = avr-gcc
OBJCOPY  = avr-objcopy
OBJDUMP  = avr-objdump
AVRDUDE  = avrdude $(PROGRAMMER) -p $(DEVICE)

CFLAGS = -Wall -Os -DF_CPU=$(CLOCK) -mmcu=$(DEVICE)

# -------- Targets --------

all: pulse2dtmf.hex

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

pulse2dtmf.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

pulse2dtmf.hex: pulse2dtmf.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

flash: all
	$(AVRDUDE) -U flash:w:pulse2dtmf.hex:i

fuse:
	$(AVRDUDE) $(FUSES)

erase:
	dd if=/dev/zero bs=512 count=1 | tr '\000' '\377' | $(AVRDUDE) -U eeprom:w:/dev/stdin:r

install: clean flash fuse
	@echo "NOTE: EEPROM preserved. Run 'make erase' to clear stored numbers."

clean:
	rm -f *.o *.elf *.hex

disasm: pulse2dtmf.elf
	$(OBJDUMP) -d $<

cpp:
	$(CC) $(CFLAGS) -E $(SRCS)
