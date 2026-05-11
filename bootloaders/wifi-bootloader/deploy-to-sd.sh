#!/bin/bash
# Deploy WiFi TFTP bootloader to SD card
# Usage: ./deploy-to-sd.sh [sdcard_mount_point]

SDCARD="${1:-/Volumes/BOOT}"

if [ ! -d "$SDCARD" ]; then
    echo "SD card not found at $SDCARD"
    echo "Insert SD card and try again, or specify mount point: $0 /path/to/sd"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Deploying WiFi bootloader to $SDCARD..."

# Copy bootloader kernel
cp "$SCRIPT_DIR/kernel8-rpi4.img" "$SDCARD/kernel8-rpi4.img"
echo "  Copied kernel8-rpi4.img"

# Create firmware directory and copy WiFi firmware
mkdir -p "$SDCARD/firmware"
cp "$SCRIPT_DIR/sdcard-files/firmware"/brcmfmac43455-sdio.bin "$SDCARD/firmware/"
cp "$SCRIPT_DIR/sdcard-files/firmware"/brcmfmac43455-sdio.txt "$SDCARD/firmware/"
cp "$SCRIPT_DIR/sdcard-files/firmware"/brcmfmac43455-sdio.clm_blob "$SDCARD/firmware/"
echo "  Copied WiFi firmware (brcmfmac43455)"

# Copy wpa_supplicant.conf if not already on card
if [ ! -f "$SDCARD/wpa_supplicant.conf" ]; then
    cp "$SCRIPT_DIR/sdcard-files/wpa_supplicant.conf" "$SDCARD/"
    echo "  Copied wpa_supplicant.conf (EDIT THIS with your WiFi credentials!)"
else
    echo "  wpa_supplicant.conf already exists on SD card (not overwriting)"
fi

sync
echo "Done! Eject SD card and boot Pi."
echo ""
echo "IMPORTANT: Edit $SDCARD/wpa_supplicant.conf with your WiFi SSID and password!"
