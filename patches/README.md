# Patches

Both patches must be applied after cloning the repo and initializing submodules.

## Quick Setup

```bash
git submodule update --init --recursive
cd libs/circle-stdlib && git apply ../../patches/circle-usb-midi.patch && cd ../..
cd libs/fluidsynth && git apply ../../patches/fluidsynth-mmap.patch && cd ../..
```

---

## circle-usb-midi.patch

Fixes three USB MIDI bugs in Circle's VL805 xHCI driver (Pi 4). Without this patch, USB MIDI does not work.

1. **DMA buffer alignment** (`lib/usb/usbmidihost.cpp`)
   - `new u8[]` not cache-aligned for PCIe DMA on VL805
   - Fixed with manual 64-byte aligned allocation

2. **Interrupt vs polling** (`lib/usb/xhcidevice.cpp`, `include/circle/usb/xhcidevice.h`)
   - xHCI interrupt handler unreliable for bulk IN transfer completions
   - Added `DisableXHCIInterrupts()` + `PollEvents()` from main loop

3. **MIDI packet parsing** (`lib/usb/usbmidihost.cpp`)
   - Circle's PacketHandler gives pre-parsed 3-byte MIDI, not 4-byte USB MIDI Event Packets
   - Old loop `i + 3 < nLength` with nLength=3 never executed
   - Fixed to `for (i=0; i<nLength; i++) MIDIByte(pPacket[i])`

### Applying

```bash
cd libs/circle-stdlib
git apply ../../patches/circle-usb-midi.patch
```

---

## fluidsynth-mmap.patch

Patches FluidSynth v2.4.3 with:

1. **mmap for SF2 sample loading** (`fluid_samplecache.c`)
   - Uses `mmap()` instead of `malloc+read` for sample data
   - Instant loading — kernel pages in data on demand
   - `MADV_WILLNEED` for background readahead
   - Falls back to malloc+read for 24-bit samples or on mmap failure

2. **Custom CC modulators** (`fluid_synth.c`)
   - CC74 → filter cutoff (9600 cents range)
   - CC71 → filter resonance (400 centibels range)
   - CC73 → attack time (12000 timecents range)
   - CC75 → decay time (12000 timecents range)
   - CC72 → release time (12000 timecents range)

3. **LFO shape control** (`fluid_lfo.c`, `fluid_lfo.h`)
   - CC79 selects LFO waveform: 0-31=triangle, 32-63=sine, 64-95=saw, 96-127=square
   - Soft square wave (tanh) to avoid discontinuity pops

### Applying

```bash
cd libs/fluidsynth
git apply ../../patches/fluidsynth-mmap.patch
```

### Building (Docker cross-compile for Pi 4)

```bash
cd libs/fluidsynth
docker run --rm --platform linux/arm64 -v "$(pwd)":/src debian:bookworm bash -c "
  apt-get update -qq &&
  apt-get install -y -qq cmake build-essential pkg-config libasound2-dev libglib2.0-dev &&
  cd /src && rm -rf build && mkdir build && cd build &&
  cmake .. -DCMAKE_BUILD_TYPE=Release \
    -Denable-libsndfile=off -Denable-dbus=off \
    -Denable-pulseaudio=off -Denable-jack=off -Denable-pipewire=off \
    -Denable-sdl2=off -Denable-readline=off &&
  make -j4"
```

Output: `build/src/libfluidsynth.so.3.3.3`
