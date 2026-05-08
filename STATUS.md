# Mini JV-880 v2 — Project Status

*Last updated: 2026-05-04*

## Overview

Bare-metal rompler synthesizer for Raspberry Pi 4 8GB, built on the Circle framework (Step 46). Receives MIDI via USB, plays WAV samples from SD card through PWM audio output. The project has two main components: the original JV-880 emulator (`src/`) and a new custom rompler engine (`rompler/`).

## What Works

- **WiFi TFTP bootloader** — Power cycle Pi, wait ~19s, upload kernel via TFTP to 192.168.0.92. Chainboots automatically.
- **PWM audio output** — 48kHz stereo via DMA-driven PWM. Verified working (440Hz test tone, square wave from MIDI trigger).
- **Synth engine (code complete)** — 24-voice rompler with SVF filter, amp envelope, pan, LFO, EQ/chorus/reverb effects. Reads WAV samples + instrument.cfg from SD card. All DSP ported from jd800-circle.
- **HDMI console output** — Logger output to screen, used for diagnostics.
- **USB device enumeration** — M-Audio Axiom 49 (VID 0x0763, PID 0x202d) detected and configured correctly. MIDI Streaming interface found, Bulk IN endpoint 0x81 configured.

## What Doesn't Work (Blocker)

### USB MIDI data reception

The Axiom 49 enumerates, the Bulk IN endpoint is configured in the xHCI controller, and an async transfer is submitted — but **no MIDI data is ever received**. Transfer completion events never arrive from the VL805 xHCI controller. This blocks all playback.

**Confirmed working on Mac** — the same keyboard (same PID 0x202d) sends MIDI data correctly on macOS via CoreMIDI. The keyboard hardware is fine.

**Hypotheses eliminated:**
- CScheduler interference — removed entirely
- WiFi/network code — removed entirely
- EMMC/FatFS init order or interference — tried all permutations, removed entirely
- 32MB sample pool allocation — skipped
- Kernel object size / BSS overflow — verified well within bounds
- PWM DMA audio interference — disabled entirely
- Extra CKernel members — stripped to match miniorgan sample exactly
- Async vs sync USB read — both fail identically
- SET_INTERFACE alternate settings — alt 0 and 1 both accepted, no data either way
- Keyboard firmware — factory reset, long power discharge, different USB port

**Remaining hypotheses (untested):**
1. Explicit SET_INTERFACE(intf, 0) needed even for alt setting 0 — *built but not yet uploaded*
2. Force endpoint to Interrupt type instead of Bulk — VL805 may mishandle Full Speed Bulk endpoints
3. Dump xHCI device context to verify endpoint is actually in "Running" state
4. Use Pi 4's internal USB controller (`USE_XHCI_INTERNAL`) instead of VL805
5. JTAG debugging to inspect xHCI registers directly

## Current State of Code

### `rompler/` — STRIPPED DOWN for debugging

The kernel is currently stripped to a minimal test matching Circle's miniorgan sample:
- **kernel.h/cpp** — CTestSound (square wave), no EMMC/FatFS/Serial/Rompler members
- **Makefile** — SYNTH_OBJS empty, only builds main.o kernel.o chainboot.o
- **synth/** — Full rompler engine source exists but is not compiled

### Circle library modifications (`circle-stdlib/libs/circle/`)

- **lib/usb/usbmidihost.cpp** — Added diagnostic logging (VID/PID, endpoint info, completion events) and explicit SET_INTERFACE(0) call
- **lib/usb/xhciendpoint.cpp** — Added transfer event logging
- **tools/bootloader/vectors64.s** — Chainboot multicore fix (ARM_ALLOW_MULTI_CORE assert removed)

## To Restore Full Rompler

Once USB MIDI is working, restore from git or rebuild:
1. `kernel.h` — Add back CEMMCDevice, FATFS, CSerialDevice, Rompler members, CSynthSound class
2. `kernel.cpp` — Add back EMMC init, FatFS mount, rompler_init(), sample loading, serial command parser
3. `Makefile` — Restore SYNTH_OBJS (rompler.o voice.o tone.o wg.o tvf.o tva.o envelope.o lfo.o effects.o sample_bank.o midi.o)
4. `usbmidihost.cpp` / `xhciendpoint.cpp` — Remove debug logging

## Directory Layout

```
mini-jv880-v2/
  rompler/           <- Custom rompler engine (active development)
    kernel.h/cpp     <- Currently stripped down for MIDI debugging
    synth/           <- Full synth engine source (not compiled currently)
    sdcard-files/    <- Sample data for SD card (instruments/sine/)
    deploy-to-sd.sh  <- Copy samples to SD card
  src/               <- Original JV-880 emulator (not actively used)
  wifi-bootloader/   <- TFTP bootloader kernel
  test-kernel/       <- WiFi/syslog test kernel
  circle-stdlib/     <- Circle framework + newlib (Step 46)
```

## Build & Deploy

```bash
cd /Users/invision/mini-jv880-v2/rompler
make clean && make          # Build kernel (~500KB image)
make deploy                 # TFTP upload to Pi at 192.168.0.92
./deploy-to-sd.sh           # Copy samples to SD card at /Volumes/BOOT/
```

## Hardware

- Raspberry Pi 4 Model B 8GB
- M-Audio Axiom 49 USB MIDI keyboard (2nd gen, PID 0x202d)
- HDMI display for console output
- SD card with bootloader + WiFi firmware + instrument samples
- USB-serial adapter at /dev/cu.usbserial-0001 (115200 baud) — optional
