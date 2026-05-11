#
# Makefile - Ethernet TFTP/HTTP bootloader
#

CIRCLEHOME = ../circle-stdlib/libs/circle

OBJS	= main.o kernel.o httpbootserver.o tftpbootserver.o

LIBS	= $(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	  $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/fat/libfatfs.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/lib/libcircle.a

ARMTOOLCHAIN = /Users/invision/arm-gnu-toolchain-15.2.rel1-darwin-arm64-aarch64-none-elf
EXTRAINCLUDE = -isystem $(ARMTOOLCHAIN)/aarch64-none-elf/include

CONTENT	= index.h style.h favicon.h

EXTRACLEAN = $(CONTENT)

include $(CIRCLEHOME)/sample/Rules.mk

# Homebrew aarch64-elf-gcc lacks libstdc++.a, use ARM toolchain's copy
LIBSTDCPP = "$(ARMTOOLCHAIN)/aarch64-none-elf/lib/libstdc++.a"

# Need newlib for C library functions (strcmp, strlen, etc.)
NEWLIBDIR = ../circle-stdlib/install/aarch64-none-circle/lib
EXTRALIBS += $(NEWLIBDIR)/libc.a $(NEWLIBDIR)/libcirclenewlib.a $(NEWLIBDIR)/libnosys.a $(NEWLIBDIR)/libm.a

%.h: %.html
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

%.h: %.css
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool $< > $@

%.h: %.ico
	@echo "  GEN   $@"
	@$(CIRCLEHOME)/tools/converttool -b $< > $@

httpbootserver.o: $(CONTENT)

-include $(DEPS)
