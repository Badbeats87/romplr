#!/bin/bash
# Boot timer: measures time from serial port open to "playable" milestones.
# Usage: power cycle the Pi, then immediately run this script.
PORT="${1:-/dev/cu.usbserial-0001}"
BAUD=115200

echo "Waiting for serial port $PORT..."
while [ ! -c "$PORT" ]; do sleep 0.1; done

echo "=== BOOT TIMER START ==="
START=$(python3 -c 'import time; print(time.time())')

stty -f "$PORT" $BAUD cs8 -cstopb -parenb raw -echo

while IFS= read -r line; do
    NOW=$(python3 -c 'import time; print(time.time())')
    ELAPSED=$(python3 -c "print(f'{$NOW - $START:.2f}s')")
    echo "[$ELAPSED] $line"

    if echo "$line" | grep -q "MIDI device found"; then
        echo ""
        echo "=== PLAYABLE at $ELAPSED ==="
        echo ""
    fi
    if echo "$line" | grep -q "All.*instruments loaded"; then
        echo ""
        echo "=== ALL LOADED at $ELAPSED ==="
        break
    fi
done < "$PORT"
