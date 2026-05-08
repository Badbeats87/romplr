#!/bin/bash
# Deploy rompler files to SD card
# Usage: ./deploy-to-sd.sh [SD_MOUNT_POINT]

SD="${1:-/Volumes/BOOT}"

if [ ! -d "$SD" ]; then
    echo "Error: SD card not found at $SD"
    echo "Usage: $0 /path/to/sd/mount"
    exit 1
fi

echo "Deploying to $SD..."

# Copy instrument samples
echo "Copying instruments..."
mkdir -p "$SD/instruments"
cp -r sdcard-files/instruments/* "$SD/instruments/"

echo "Done! Files on SD card:"
find "$SD/instruments" -type f | head -20

echo ""
echo "To TFTP upload the kernel after boot:"
echo "  make deploy"
echo "  # or manually:"
echo "  echo -e 'binary\nput kernel8-rpi4.img\nquit' | tftp 192.168.0.92"
