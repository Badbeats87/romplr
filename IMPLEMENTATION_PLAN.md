# Implementation Plan

This plan describes the target structure for making each synth variant standalone, reproducible, and testable while keeping the custom rompler engine shared where appropriate.

## Goals

1. Separate the platform variants cleanly.
2. Keep the custom rompler synth engine shared across custom-rompler targets.
3. Make every platform produce a flashable artifact that matches the current source.
4. Record the exact status of each artifact: what works, what was tested, and what is known broken.
5. Remove ambiguity between old images, old binaries, current source, and hardware-tested builds.

## Target Architecture

```text
synth/core/
  Shared custom rompler engine:
  voice allocation, sample playback, envelopes, filters, LFO, effects,
  MIDI parsing, patch/instrument logic.

platforms/baremetal-rompler/
  Circle wrapper around synth/core:
  PWM audio, USB MIDI, FATFS, boot/deploy scripts.

platforms/linux-rompler/
  Linux wrapper around synth/core:
  ALSA audio, /dev/snd/midi*, POSIX file loading, Buildroot integration.

platforms/linux-fluidsynth/
  Separate FluidSynth appliance:
  FluidSynth patches/config, SoundFont loading, MIDI bank helper,
  startup service, Buildroot integration.

platforms/baremetal-minijv880/
  Original MiniJV880/JV-880 emulator path:
  kept separate from the newer custom rompler.

images/
  Reproducible image recipes and release artifacts.
```

## Shared Engine Rule

The custom rompler engine should be shared by:

- bare-metal rompler
- Linux rompler
- possible Teensy rompler target

FluidSynth should not be forced through the shared engine. It is a separate implementation path because FluidSynth already provides its own synth engine.

The current `synth/baremetal/` name is misleading if Linux and Teensy also use the code. Rename it later to:

```text
synth/core/
```

Only do this rename after the platform boundaries and build targets are clear.

## Platform Deliverables

Each platform must answer the same questions:

```text
Does it build?
Does it boot?
Does it auto-start?
What hardware was tested?
Which exact artifact was tested?
What is known broken?
```

Each platform should include:

```text
README.md
BUILD.md
STATUS.md
scripts/build-image.sh or equivalent
scripts/flash.sh or documented flashing command
```

## Flashable Artifact Standard

Every flashable artifact must include a `BUILD_INFO.md` next to it:

```text
Platform:
Engine:
Commit:
Build date:
Artifact:
Autostart:
Tested hardware:
Test result:
Known issues:
```

Example:

```text
Platform: linux-rompler
Engine: shared custom rompler engine
Commit: abc123
Build date: 2026-08-10
Artifact: sdcard.img
Autostart: yes
Tested hardware: Raspberry Pi 4 8GB, M-Audio Axiom 49
Test result: boots, loads 33 instruments, MIDI works, audio works
Known issues: full 129-instrument load not verified
```

## Phase 1: Inventory And Truth Table

Create a status table for all variants:

```text
docs/status-by-platform.md
```

Initial rows:

```text
baremetal-minijv880
baremetal-rompler
linux-rompler
linux-fluidsynth
buildroot-base
teensy-rompler
```

For each row, record:

- source location
- current build command
- current artifact location
- whether it boots
- whether it auto-starts
- last known hardware test
- blockers

This phase is documentation only. The goal is to stop guessing.

## Phase 2: Fix Repo Boundaries

Clean up platform folders without changing behavior:

```text
platforms/baremetal-minijv880/
platforms/baremetal-rompler/
platforms/linux-rompler/
platforms/linux-fluidsynth/
platforms/teensy-rompler/
```

Fix stale references:

- top-level `Makefile` currently points `build-linux` at `platforms/linux`, which does not exist
- docs still mention old paths like `rompler-linux/`
- Buildroot docs overstate the current image status

Expected result: a reader can tell which code belongs to which platform.

## Phase 3: Normalize The Shared Engine

Once the platform folders are clear, move or alias:

```text
synth/baremetal/ -> synth/core/
```

Then update includes and build scripts for:

- bare-metal rompler
- Linux rompler
- Teensy rompler, if kept

Do not include FluidSynth in this shared engine migration.

Expected result: custom rompler platforms share one engine and only differ at the platform wrapper layer.

## Phase 4: Buildroot Integration For Linux Rompler

Turn the custom Linux rompler into a Buildroot package:

```text
buildroot/package/rompler/
  Config.in
  rompler.mk
```

The package should:

- build the Linux rompler from current source
- install `/usr/bin/rompler`
- install an init script or service
- install default config
- create expected sample/instrument directories

The image should auto-start the synth when booted, or clearly document why it does not.

Expected artifact:

```text
images/linux-rompler/sdcard.img
images/linux-rompler/BUILD_INFO.md
```

## Phase 5: Buildroot Integration For Linux FluidSynth

Turn the FluidSynth path into its own image target:

- include patched FluidSynth or document exact upstream package
- build/install `midi_bankfix`
- install `fluidsynth-start.sh`
- install an init script or service
- define expected SoundFont storage

Do not mix this with the custom Linux rompler image.

Expected artifact:

```text
images/linux-fluidsynth/sdcard.img
images/linux-fluidsynth/BUILD_INFO.md
```

## Phase 6: Bare-Metal Flashable Artifacts

Separate original MiniJV880 from the newer custom rompler.

For each one, create either:

- an SD-card folder that can be copied to a FAT32 partition
- or a zip/image artifact with boot files

Expected artifacts:

```text
images/baremetal-minijv880/sdcard.zip
images/baremetal-minijv880/BUILD_INFO.md

images/baremetal-rompler/sdcard.zip
images/baremetal-rompler/BUILD_INFO.md
```

## Phase 7: Hardware Verification

For every artifact, run a hardware test and record the result.

Minimum test log:

```text
docs/test-runs/YYYY-MM-DD-platform.md
```

Include:

- exact artifact path
- commit
- Raspberry Pi model
- MIDI keyboard
- audio output used
- boot result
- instrument loading result
- MIDI result
- audio result
- failures or limits

For the bare-metal rompler specifically, verify:

- whether USB MIDI packets are actually received
- whether notes produce audio
- whether loading stops at 33, 128, 129, or another count
- whether `HEAP_HIGH` allocation is actually active in the tested build

## Phase 8: Publishable Repo Cleanup

Before presenting the repo:

- remove hard-coded WiFi credentials from Buildroot scripts
- remove or ignore generated binaries unless intentionally versioned
- replace stale docs with current platform READMEs
- add a top-level status matrix
- make old historical artifacts clearly marked as historical
- ensure every "works" claim has a matching test log

## Definition Of Done

The project is in good shape when:

1. A new user can choose one platform and follow its README without reading unrelated folders.
2. Every platform build creates a reproducible artifact.
3. Every flashable artifact has `BUILD_INFO.md`.
4. Every "tested" status links to a hardware test log.
5. The custom rompler platforms share one synth engine.
6. FluidSynth is clearly documented as a separate engine path.
7. There are no mystery images or unexplained binaries.

## Immediate Next Steps

1. Create `docs/status-by-platform.md`.
2. Fix the stale top-level `Makefile` Linux target.
3. Add per-platform README files.
4. Decide whether to keep old `sdcard.img` as a historical artifact or rebuild a fresh image.
5. Run one fresh hardware test and document the result.
