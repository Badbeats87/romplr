#
# Makefile — Minimal rompler kernel (no WiFi)
#

CIRCLE_STDLIB_DIR ?= ../../libs/circle-stdlib
SYNTH_DIR = ../../synth/baremetal

-include $(CIRCLE_STDLIB_DIR)/Config.mk

NEWLIBDIR   ?= $(CIRCLE_STDLIB_DIR)/install/$(NEWLIB_ARCH)
CIRCLEHOME  ?= $(CIRCLE_STDLIB_DIR)/libs/circle

SYNTH_OBJS = \
	$(SYNTH_DIR)/rompler.o \
	$(SYNTH_DIR)/voice.o \
	$(SYNTH_DIR)/tone.o \
	$(SYNTH_DIR)/wg.o \
	$(SYNTH_DIR)/tvf.o \
	$(SYNTH_DIR)/tva.o \
	$(SYNTH_DIR)/envelope.o \
	$(SYNTH_DIR)/lfo.o \
	$(SYNTH_DIR)/effects.o \
	$(SYNTH_DIR)/sample_bank.o \
	$(SYNTH_DIR)/midi.o

OBJS = main.o kernel.o $(SYNTH_OBJS)

include $(CIRCLEHOME)/Rules.mk

INCLUDE += \
	-I $(CIRCLE_STDLIB_DIR)/include \
	-I $(NEWLIBDIR)/include \
	-I ../../synth/include \
	-I $(SYNTH_DIR)

LIBS = \
	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(NEWLIBDIR)/lib/libm.a \
	$(NEWLIBDIR)/lib/libc.a \
	$(NEWLIBDIR)/lib/libcirclenewlib.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/fs/fat/libfatfs.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/sound/libsound.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/libcircle.a

-include $(DEPS)

# ── Serial bootloader flash ─────────────────────────────
SERIALPORT  ?= /dev/cu.usbserial-0001
FLASHBAUD   ?= 115200
USERBAUD    ?= 115200
USEFLASHY   ?= 1
FLASHY      = /Users/invision/.nvm/versions/node/v20.19.5/bin/node $(CIRCLEHOME)/tools/flashy/flashy.js
REBOOTMAGIC ?=
FLASHYFLAGS ?= --reboot:r --rebootdelay:3000 --go --monitor

.PHONY: flash
