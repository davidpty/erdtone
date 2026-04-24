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
    FUSES = -U lfuse:w:0xFF:m -U hfuse:w:0xDF:m -U efuse:w:0xFF:m
else
    CLOCK = 8000000
    FUSES = -U lfuse:w:0xFD:m -U hfuse:w:0xDF:m -U efuse:w:0xFF:m
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

all: pulse-dtmf.hex

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

pulse-dtmf.elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

pulse-dtmf.hex: pulse-dtmf.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

flash: all
	$(AVRDUDE) -U flash:w:pulse-dtmf.hex:i

fuse:
	$(AVRDUDE) $(FUSES)

install: flash fuse

clean:
	rm -f *.o *.elf *.hex

disasm: pulse-dtmf.elf
	$(OBJDUMP) -d $<

cpp:
	$(CC) $(CFLAGS) -E $(SRCS)
