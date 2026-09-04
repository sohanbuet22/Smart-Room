MCU = atmega32
F_CPU = 1000000UL
CC = avr-gcc
CFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os

all: project1.hex

project1.hex: project1.elf
	avr-objcopy -O ihex -R .eeprom project1.elf project1.hex

project1.elf: project1.c
	$(CC) $(CFLAGS) -o project1.elf project1.c

clean:
	rm -f project1.elf project1.hex