#!/usr/bin/env python3
"""Boot timer: measures wall-clock time from script start to key milestones.
Power off Pi, run this script, then power on."""
import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-0001"
baud = 115200

print(f"Opening {port} at {baud}...")
ser = serial.Serial(port, baud, timeout=1)
ser.reset_input_buffer()

print("=== BOOT TIMER START — power on the Pi now ===")
start = time.time()

while True:
    raw = ser.readline()
    if not raw:
        continue
    try:
        line = raw.decode("utf-8", errors="replace").strip()
    except Exception:
        continue
    if not line:
        continue

    elapsed = time.time() - start
    print(f"[{elapsed:6.2f}s] {line}")

    if "MIDI device found" in line:
        print(f"\n>>> PLAYABLE at {elapsed:.2f}s <<<\n")
    if "All" in line and "instruments loaded" in line:
        print(f"\n>>> ALL LOADED at {elapsed:.2f}s <<<\n")
        break
