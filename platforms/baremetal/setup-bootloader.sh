#!/bin/bash
#
# Setup Circle serial bootloader on SD card.
# Run once — after this, use "gmake flash" to deploy kernels over UART.
#
set -e

SD="/Volumes/UNTITLED"
BOOTLOADER="../../libs/circle-stdlib/libs/circle/tools/bootloader/kernel8-rpi4.img"

if [ ! -d "$SD" ]; then
    echo "ERROR: SD card not mounted at $SD"
    exit 1
fi

if [ ! -f "$BOOTLOADER" ]; then
    echo "ERROR: Bootloader image not found at $BOOTLOADER"
    exit 1
fi

# Backup current kernel if present
if [ -f "$SD/kernel8-rpi4.img" ]; then
    echo "Backing up current kernel to kernel8-rpi4.img.bak"
    cp "$SD/kernel8-rpi4.img" "$SD/kernel8-rpi4.img.bak"
fi

# Copy bootloader
echo "Installing Circle serial bootloader..."
cp "$BOOTLOADER" "$SD/kernel8-rpi4.img"

echo ""
echo "Done! SD card now has the serial bootloader."
echo ""
echo "Workflow:"
echo "  1. Insert SD card, boot Pi"
echo "  2. Bootloader waits on UART for hex data"
echo "  3. Run: gmake flash"
echo "  4. Kernel loads over serial and runs"
echo "  5. To reflash: gmake flash (sends 'r' to reboot, then flashes)"
echo ""
echo "IMPORTANT: Make sure config.txt still has:"
echo "  enable_uart=1"
echo "  dtoverlay=disable-bt"
