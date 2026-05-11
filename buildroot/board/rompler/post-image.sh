#!/bin/bash
# Post-image script: generate SD card image using genimage
set -e

BOARD_DIR="$(dirname "$0")"

# Use Buildroot's genimage support
support/scripts/genimage.sh -c "$BOARD_DIR/genimage.cfg"
