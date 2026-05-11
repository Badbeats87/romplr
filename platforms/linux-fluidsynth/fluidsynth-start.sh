#!/bin/sh
# FluidSynth startup script for Raspberry Pi 4
# Runs at boot via /etc/init.d/ or manually
#
# Requires: FluidSynth with mmap patch, SF2 files on partition 3

export LD_LIBRARY_PATH=/usr/lib
mkdir -p /mnt/sf2
mount /dev/mmcblk0p3 /mnt/sf2 2>/dev/null || true
amixer -c 1 cset numid=1 400 > /dev/null 2>&1 || true
modprobe snd-seq 2>/dev/null
modprobe snd-seq-midi 2>/dev/null

# Pre-read SF2 metadata into page cache (parallel background reads)
(dd if=/mnt/sf2/Fantom_A.sf2 of=/dev/null bs=1M count=1; \
 dd if=/mnt/sf2/Fantom_A.sf2 of=/dev/null bs=1M skip=2795) 2>/dev/null &
(dd if=/mnt/sf2/Fantom_B.sf2 of=/dev/null bs=1M count=1; \
 dd if=/mnt/sf2/Fantom_B.sf2 of=/dev/null bs=1M skip=3853) 2>/dev/null &

# Wait for USB MIDI device to appear
for i in $(seq 1 100); do
  [ -e /dev/snd/midiC*D* ] && break
  usleep 100000 2>/dev/null || sleep 0.1
done
wait

# Start FluidSynth (stdin kept open via sleep infinity pipe)
(sleep infinity | /usr/bin/fluidsynth \
  -a alsa -o audio.alsa.device=hw:1,0 \
  -o audio.period-size=512 -o audio.periods=3 \
  -o synth.dynamic-sample-loading=1 \
  -m alsa_seq -r 44100 -g 1.0 \
  "/mnt/sf2/Fantom_A.sf2" "/mnt/sf2/Fantom_B.sf2" > /dev/null 2>&1) &

# Start BankFix (waits for FluidSynth automatically)
/usr/bin/midi_bankfix > /dev/null 2>&1 &
