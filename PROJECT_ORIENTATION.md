# Project Orientation

This document records the current understanding of the MiniJV880 / rompler project state after inspecting the local repositories and artifacts in August 2026.

## Short Version

The project contains several related but different synth efforts:

- an original bare-metal MiniJV880 / JV-880 style emulator
- a newer custom bare-metal rompler
- a custom Linux rompler executable
- a Linux + FluidSynth deployment path
- an incomplete Buildroot image pipeline

These are not cleanly separated yet. Some docs refer to old paths, some Makefile targets are stale, and the Buildroot config does not currently package and auto-start either Linux synth path.

## Platform Variants

### Original Bare-Metal MiniJV880

Likely location in the older local tree:

```text
../mini-jv880-v2/src/
../mini-jv880-v2/sdcard/
```

The `sdcard/` directory contains files that look like a self-contained bare-metal SD card:

```text
kernel8-rpi4.img
minijv880.ini
jv880_rom1.bin
jv880_rom2.bin
jv880_waverom1.bin
jv880_waverom2.bin
jv880_exp_*.bin
```

This path best matches the memory of: put SD card in Pi, boot, and play notes. It does not require Linux, SSH, or manually running a program.

### Newer Bare-Metal Rompler

Current restructured location:

```text
platforms/baremetal/
synth/baremetal/
```

This is the custom rompler port to Circle / bare metal. It includes PWM audio, SD/FATFS sample loading, MIDI handling hooks, and the extracted synth engine.

Known status from existing docs:

- PWM audio was tested on Raspberry Pi 4.
- SD/FATFS sample loading is implemented.
- USB MIDI device enumeration works for an M-Audio Axiom 49.
- Older status docs say USB MIDI data reception was still the blocker.
- Current code appears to include later USB MIDI integration work: packet handler registration, xHCI interrupt disabling, polling mode, and feeding received packet bytes into the rompler MIDI parser.

Interpretation: the code has progressed beyond the stale `STATUS.md`, but the repository does not currently contain a fresh hardware-test log proving the final "USB MIDI fixed" state. If this works on hardware, the docs should be updated with the exact test date, keyboard, build artifact, and result.

This is technically interesting, but it should still be described carefully until the hardware-tested status is refreshed.

### Custom Linux Rompler

Older local location:

```text
../mini-jv880-v2/rompler-linux/
```

Restructured location:

```text
platforms/linux-rompler/
```

This is a custom C userspace program. It uses ALSA for audio, reads MIDI from `/dev/snd/midi*`, and calls the custom rompler synth engine.

Important files/artifacts found:

```text
../mini-jv880-v2/rompler-linux/rompler
../mini-jv880-v2/rompler-linux/rompler-new
platforms/linux-rompler/rompler-new
```

These are AArch64 Linux ELF binaries. They are not SD card images.

Typical manual run command:

```sh
./rompler /root/sf2/ -m /dev/snd/midiC2D0
```

The `deploy_sf2.sh` script copies a curated set of SF2 files to `/root/sf2` on a Pi over SSH. It assumes the source SoundFont folder exists locally:

```text
~/Downloads/Roland Fantom X SoundFont
```

This path was likely tested at some point, but the current restructured repo does not preserve a clean auto-starting image for it.

### Linux + FluidSynth

Current restructured location:

```text
platforms/linux-fluidsynth/
libs/fluidsynth/
patches/fluidsynth-mmap.patch
tools/midi_bankfix.c
```

This path uses FluidSynth as the synth engine. The project-specific pieces are deployment scripts, a startup script, patches, and MIDI/bank behavior fixes.

Plain-language distinction:

- `linux-rompler`: custom synth engine written in this project.
- `linux-fluidsynth`: mature external synth engine, configured and patched for the desired appliance behavior.

The FluidSynth startup script expects:

```text
/mnt/sf2/Fantom_A.sf2
/mnt/sf2/Fantom_B.sf2
/usr/bin/fluidsynth
/usr/bin/midi_bankfix
```

The deploy script assumes an already-running Pi with SSH. It is not currently integrated into the Buildroot image.

### Buildroot Linux Image

Current restructured location:

```text
buildroot/
```

Important files:

```text
buildroot/configs/rompler_pi4_dev_defconfig
buildroot/board/rompler/genimage.cfg
buildroot/board/rompler/post-build.sh
buildroot/board/rompler/post-image.sh
```

The Buildroot external tree can, in principle, generate an `sdcard.img`. It includes Pi firmware, kernel config, ALSA packages, SSH, WiFi, and sound module setup.

However, as currently inspected:

- `buildroot/Config.in` says there are no custom packages yet.
- There is no Buildroot package for `linux-rompler`.
- There is no Buildroot package for FluidSynth or `midi_bankfix`.
- `post-build.sh` configures WiFi, SSH, and sound modules, but does not install/start a synth.
- No generated `sdcard.img`, `rootfs.ext4`, or `boot.vfat` exists in this restructured repo.

So this is a base Pi 4 Buildroot dev image pipeline, not a complete ready-to-flash synth appliance.

## Reconciliation With Later AI Summary

Another AI summary claimed the original porting plan was essentially complete:

```text
Phase 1: synth core extracted
Phase 2: PWM audio done
Phase 3: FatFS file loading done
Phase 4: USB MIDI fixed
Phase 5: mostly tested, remaining sample-pool size issue
```

The repository partly supports that claim:

- Phase 1 is supported by `synth/baremetal/*.c`.
- Phase 2 is supported by `platforms/baremetal/kernel.cpp` implementing `CSynthSound::GetChunk()`.
- Phase 3 is supported by `sample_bank.c` scanning `SD:/instruments` and loading WAV/instrument configs.
- Phase 4 is partially supported by current code that registers a Circle MIDI packet handler, switches xHCI to polling mode, and forwards MIDI packet bytes to `rompler_midi()`.

But the claim is too confident without a current hardware-test note:

- `STATUS.md` still says USB MIDI reception is blocked.
- There is no clear "tested fixed on date X with Axiom 49" document.
- There is no captured boot log showing MIDI packets received and notes playing.

The sample-pool statement also needs nuance. Current code already uses `HEAP_HIGH`:

```text
platforms/baremetal/kernel.cpp     alloc_high_mem() -> CMemorySystem::HeapAllocate(..., HEAP_HIGH)
synth/baremetal/sample_bank.c      sample pool allocated with alloc_high_mem()
synth/baremetal/sample_bank.h      SAMPLE_POOL_SAMPLES = 512M samples = 1GB
```

So "needs HEAP_HIGH allocation" is stale. If there is still a 33/129 instrument limit, the remaining issue is likely one of:

- the fixed `MAX_INSTRUMENTS` value, currently 128
- actual sample pool size versus the full instrument set
- addressability/cache/DMA constraints of high heap memory
- a different build artifact than the current source
- stale hardware image not matching current code

The next evidence needed is a fresh bare-metal hardware run that records:

```text
commit/build artifact
Raspberry Pi model
MIDI keyboard
number of instruments loaded
whether MIDI packets are logged
whether notes produce audio
whether loading stops at 33, 128, or another count
```

## Old Local Linux Image

An older local tree contains:

```text
../mini-jv880-v2/sdcard.img
```

This was inspected read-only. Its boot partition confirms it is a Linux image:

```text
kernel=Image
root=/dev/mmcblk0p2 rootfstype=ext4 rootwait quiet console=tty1
```

The image has a FAT boot partition and Linux ext4 root partition. The boot partition contains:

```text
Image
bcm2711-rpi-4-b.dtb
config.txt
cmdline.txt
start4.elf
fixup4.dat
```

A raw string search of the image found init scripts for WiFi, SSH, and sound-module loading, but did not clearly show `rompler`, `fluidsynth`, `midi_bankfix`, `/root/sf2`, or `/usr/bin/fluidsynth`.

Interpretation:

- The old image is probably a Buildroot dev image.
- It may have been used as the base for testing the Linux synth.
- The auto-starting synth state, if it existed, is not clearly preserved in the currently inspected files.

## Why The Memory Is Confusing

The remembered behavior is:

```text
put SD card in Pi, boot, play notes
```

That behavior could have come from at least three different states:

1. The original bare-metal MiniJV880 SD card, which really is boot-and-play.
2. A Linux SD card that had a synth binary and init script manually added after the base image was created.
3. A Linux image that was booted, then the synth was started manually over SSH, later remembered as part of the boot flow.

The currently preserved `mini-jv880-restructured` tree does not prove a complete Linux boot-and-play image exists.

## Current Test Paths

### Test The Old Custom Linux Rompler

Use the older local Buildroot image and binary:

```text
../mini-jv880-v2/sdcard.img
../mini-jv880-v2/rompler-linux/rompler-new
```

Likely process:

```sh
# Flash ../mini-jv880-v2/sdcard.img to an SD card, then boot the Pi.

scp ../mini-jv880-v2/rompler-linux/rompler-new root@192.168.0.92:/root/rompler
../mini-jv880-v2/rompler-linux/deploy_sf2.sh 192.168.0.92

ssh root@192.168.0.92
chmod +x /root/rompler
/root/rompler /root/sf2/ -m /dev/snd/midiC2D0
```

If the MIDI device differs:

```sh
ls /dev/snd/midi*
aplay -l
```

### Check Whether A Physical SD Card Auto-Starts Linux Synth

Boot the physical SD card, SSH in if possible, then run:

```sh
ps
ls /etc/init.d
find / -name '*rompler*' -o -name '*fluidsynth*' -o -name '*midi*'
```

If it was truly Linux boot-and-play, there should be a process or init script that starts `rompler`, `fluidsynth`, or a related helper.

## Repository Cleanliness Issues

The variants are not currently cleanly separated.

Known issues:

- Top-level `Makefile` says `build-linux`, but points to `platforms/linux`, which does not exist in the restructured repo.
- Current Linux folders are `platforms/linux-rompler` and `platforms/linux-fluidsynth`.
- Some docs still mention old paths like `rompler-linux/`.
- Buildroot exists, but does not package the Linux synth variants.
- FluidSynth deployment assumes an already-running Pi rather than an image build.
- The old `sdcard.img` lives in `../mini-jv880-v2`, not this restructured repo.
- The original bare-metal emulator and newer custom bare-metal rompler are conceptually mixed in the docs.
- Buildroot post-build scripts contain hard-coded local WiFi credentials and dev root-login settings. These should be removed or templated before publishing.

## Recommended Cleanup

Create clear platform boundaries:

```text
platforms/baremetal-minijv880/
platforms/baremetal-rompler/
platforms/linux-rompler/
platforms/linux-fluidsynth/
images/buildroot/
docs/status-by-platform.md
```

For each platform, document:

```text
Does it build?
Does it boot?
Does it auto-start?
What hardware was tested?
What command runs it?
What is missing?
```

For portfolio use, the most honest framing is:

```text
This project explores several Raspberry Pi synth implementations:
bare-metal emulation, a custom rompler engine, and Linux-based prototypes.
The strongest technical work is the AI-assisted direction, hardware testing,
DSP/synth architecture, and debugging across MIDI, ALSA, Circle, Buildroot,
and Raspberry Pi boot flows.
```

Avoid claiming that the restructured repo currently provides a complete ready-to-flash Linux synth image. It does not, based on the inspected files.
