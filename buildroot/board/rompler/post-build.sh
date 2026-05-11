#!/bin/bash
# Post-build script: copy Pi 4 boot files + configure SSH
set -e

BOARD_DIR="$(dirname "$0")"
IMAGES_DIR="$1/../images"

cp "$BOARD_DIR/config.txt" "$IMAGES_DIR/" 2>/dev/null || true
cp "$BOARD_DIR/cmdline.txt" "$IMAGES_DIR/" 2>/dev/null || true

# Flatten rpi-firmware files to images root (Pi bootrom expects them at /)
cp "$IMAGES_DIR/rpi-firmware/start4.elf" "$IMAGES_DIR/" 2>/dev/null || true
cp "$IMAGES_DIR/rpi-firmware/fixup4.dat" "$IMAGES_DIR/" 2>/dev/null || true

# Enable root login via SSH (dev build only)
sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' "$1/etc/ssh/sshd_config" 2>/dev/null || true

# Modules to load at boot
cat > "$1/etc/modules" << 'MODULES'
brcmfmac
snd_bcm2835
snd_usb_audio
MODULES

# Write wpa_supplicant config (no ctrl_interface — not supported by this build)
cat > "$1/etc/wpa_supplicant.conf" << 'WPACONF'
ap_scan=1
country=DE

network={
	ssid="Vodafone-21FB"
	psk="Br3maHndAMpcNeEM"
	key_mgmt=WPA-PSK
}
WPACONF

# Create wpa_supplicant startup script (S39 — before S40network)
# Waits for wlan0 to appear (brcmfmac module may take a moment)
cat > "$1/etc/init.d/S39wpa" << 'WPAEOF'
#!/bin/sh
case "$1" in
  start)
    printf "Waiting for wlan0: "
    for i in $(seq 1 30); do
      [ -d /sys/class/net/wlan0 ] && break
      sleep 0.5
    done
    if [ ! -d /sys/class/net/wlan0 ]; then
      echo "TIMEOUT"
      exit 1
    fi
    echo "OK"
    printf "Starting wpa_supplicant: "
    /usr/sbin/wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -D nl80211
    echo "OK"
    ;;
  stop)
    killall wpa_supplicant 2>/dev/null
    echo "OK"
    ;;
  *)
    echo "Usage: $0 {start|stop}"
    exit 1
esac
WPAEOF
chmod 755 "$1/etc/init.d/S39wpa"

# Remove old S42wpa if it exists from a previous build
rm -f "$1/etc/init.d/S42wpa"

# Late init script to load sound modules after USB is ready
cat > "$1/etc/init.d/S45sound" << 'SNDEOF'
#!/bin/sh
case "$1" in
  start)
    printf "Loading sound modules: "
    modprobe snd_bcm2835 2>/dev/null || true
    modprobe snd_usb_audio 2>/dev/null || true
    echo "OK"
    ;;
  stop)
    ;;
  *)
    echo "Usage: $0 {start|stop}"
    exit 1
esac
SNDEOF
chmod 755 "$1/etc/init.d/S45sound"
