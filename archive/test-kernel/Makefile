#
# WiFi test kernel with syslog output
#

CIRCLE_STDLIB_DIR = ../circle-stdlib

-include $(CIRCLE_STDLIB_DIR)/Config.mk

NEWLIBDIR   ?= $(CIRCLE_STDLIB_DIR)/install/$(NEWLIB_ARCH)
CIRCLEHOME  ?= $(CIRCLE_STDLIB_DIR)/libs/circle

OBJS = main.o kernel.o

include $(CIRCLEHOME)/Rules.mk

# Override LIBSTDCPP - Homebrew doesn't have it, get from ARM toolchain
LIBSTDCPP = /Users/invision/arm-gnu-toolchain-15.2.rel1-darwin-arm64-aarch64-none-elf/aarch64-none-elf/lib/libstdc++.a

INCLUDE += \
	-I $(CIRCLE_STDLIB_DIR)/include \
	-I $(NEWLIBDIR)/include

LIBS = \
	$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
	$(CIRCLEHOME)/addon/wlan/libwlan.a \
	$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(NEWLIBDIR)/lib/libm.a \
	$(NEWLIBDIR)/lib/libc.a \
	$(NEWLIBDIR)/lib/libcirclenewlib.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/fs/fat/libfatfs.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/net/libnet.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/libcircle.a

-include $(DEPS)
