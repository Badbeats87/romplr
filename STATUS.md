# Mini JV-880 — Project Status

*Last updated: 2026-08-10*

## Overview

Rompler synthesizer for Raspberry Pi 4 8GB. The **Linux path is the primary focus** — bare-metal is archived as experimental. See `docs/status-by-platform.md` for the full platform truth table.

## Decision: Linux over Bare-Metal (2026-08-10)

The original motivation for bare-metal was faster boot time. The Linux FluidSynth version achieves ~12s boot-to-sound with mmap-patched sample loading, making the bare-metal approach unnecessary. Linux also provides SSH, WiFi, easier debugging, and a proven synth engine (FluidSynth).

## Primary Targets

### linux-fluidsynth (working, needs reproducibility)

- **Status**: Working from backup image (`rompler-linux-backup.img`, 4.3 GB)
- **Boot time**: ~12s power-on to playable sound
- **Engine**: FluidSynth with mmap patch for instant sample loading
- **SoundFonts**: Roland Fantom A/B SF2 files (2.9 GB + 4 GB) on SD partition 3
- **MIDI**: USB MIDI via ALSA, M-Audio Axiom 49
- **Audio**: ALSA PCM output
- **Custom CCs**: CC74=cutoff, CC71=reso, CC73/75/72=attack/decay/release
- **Helper**: `midi_bankfix` for FluidSynth bank selection
- **Scripts**: `platforms/linux-fluidsynth/deploy.sh`, `fluidsynth-start.sh`
- **Patches**: `patches/fluidsynth-mmap.patch` (mmap + CC modulators + LFO shapes)

**Problem**: Not reproducible from source. The backup image is the only tested artifact. No Buildroot package definition exists.

**Next steps**:
1. Make the image reproducible (Buildroot package or documented manual build)
2. Document exact image contents and startup sequence
3. Remove hardcoded WiFi credentials from scripts

### linux-rompler (reference implementation)

- **Source**: `platforms/linux-rompler/main.c` (1732 lines)
- **Status**: Binary exists (`rompler-new`). Not recently tested.
- **Role**: Reference synth engine. Was the basis for the bare-metal port. Could become an alternative to FluidSynth if custom DSP is needed.

## Archived / Experimental

### baremetal-rompler (experimental)

- **Source**: `platforms/baremetal/` + `synth/baremetal/`
- **Status**: Code complete but **never fully verified on hardware**. USB MIDI, HEAP_HIGH allocation, and full instrument loading are unproven on the current build.
- **Why archived**: Linux FluidSynth achieves comparable boot time (~12s) with less complexity. Bare-metal adds maintenance burden (Circle framework, custom synth engine, no networking/debugging).

### baremetal-minijv880 (archived)

- **Source**: `archive/original-jv880-emulator/`
- **Status**: Original JV-880 emulator. Not maintained.

### teensy-rompler (experimental)

- **Source**: `platforms/teensy/`
- **Status**: PlatformIO project for Teensy 4.1. Not actively developed.

## Hardware

- Raspberry Pi 4 Model B 8 GB
- M-Audio Axiom 49 USB MIDI keyboard (VID 0x0763, PID 0x202d)
- HDMI display for console
- SD card: boot partition (FAT32) + rootfs + SF2 partition
- CP2102N USB-serial adapter on `/dev/cu.usbserial-0001` (115200 baud)

## Build

```bash
# Linux rompler (cross-compile)
cd platforms/linux-rompler && make

# Restore FluidSynth image to SD
sudo dd if=rompler-linux-backup.img of=/dev/rdisk4 bs=1m

# Host smoke test (synth engine only)
make test-host
```
