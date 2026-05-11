#
# Makefile
#

CIRCLE_STDLIB_DIR = ../circle-stdlib
CMSIS_DIR = ../CMSIS_5/CMSIS

OBJS = main.o kernel.o minijv880.o config.o \
       emulator/lcd.o emulator/mcu.o emulator/mcu_opcodes.o emulator/pcm.o

OPTIMIZE = -O3

include ./Rules.mk

# Flash to SD card (insert SD card, run this, eject, boot)
sdflash: $(TARGET).img
	@echo "Waiting for SD card at /Volumes/BOOT..."
	@while [ ! -d /Volumes/BOOT ]; do sleep 0.5; done
	@cp $(TARGET).img /Volumes/BOOT/$(TARGET).img
	@diskutil eject /Volumes/BOOT
	@echo "Done! Put SD card in Pi and power up."
