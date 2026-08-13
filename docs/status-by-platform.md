# Status By Platform

*Created: 2026-08-10*
*Method: Source code inspection only. No hardware tests performed.*

**Priority**: Linux is the primary path. Bare-metal is archived as experimental (boot time advantage no longer applies — Linux FluidSynth achieves ~12s boot).

## Truth Table

| | baremetal-rompler | linux-rompler | linux-fluidsynth | baremetal-minijv880 | teensy-rompler |
|---|---|---|---|---|---|
| **Source location** | `platforms/baremetal/` + `synth/baremetal/` | `platforms/linux-rompler/main.c` | `platforms/linux-fluidsynth/` | `archive/original-jv880-emulator/` | `platforms/teensy/` |
| **Build command** | `cd platforms/baremetal && make` | `cd platforms/linux-rompler && make` | N/A (deploy scripts only) | Unknown (archived) | `pio run` (PlatformIO) |
| **Artifact** | `platforms/baremetal/kernel8-rpi4.img` (542KB) | `platforms/linux-rompler/rompler-new` (76KB) | No local artifact | `archive/original-jv880-emulator/kernel8-rpi4.img` | Unknown |
| **Does it build?** | Assumed yes (objects present) | Assumed yes (binary present) | N/A | Unknown | Unknown |
| **Does it boot?** | NOT VERIFIED on current build | NOT VERIFIED | Yes (from backup image) | Unknown | Unknown |
| **Auto-start?** | Yes (bare-metal kernel) | No (manual launch) | Yes (via fluidsynth-start.sh in image) | Yes (bare-metal kernel) | N/A |
| **Last hardware test** | None on current source | None on current source | Working from `rompler-linux-backup.img` | Unknown | Unknown |
| **Blockers** | Needs fresh build + hardware test | Needs Pi with Linux image | Image exists but not reproducible from source | Archived, not maintained | Not actively developed |

## Detailed Platform Notes

### baremetal-rompler (primary target)

**Synth engine:** 11 C source files in `synth/baremetal/`, 24-voice rompler with SVF filter, ADSR, LFO, chorus, reverb.

**What the code contains (verified in source):**
- HEAP_HIGH allocation: `alloc_high_mem()` wrapper in `kernel.cpp:16` calls `CMemorySystem::HeapAllocate(size, HEAP_HIGH)`. Called from `sample_bank.c:440`. Pool size: 512M samples (1GB).
- USB MIDI: `MIDIPacketHandler` registered at `kernel.cpp:221`. Forwards each byte to `rompler_midi()` via `CSynthSound`. Logs hex packets to console.
- Lazy loading: `sample_bank_load_next_pending()` loads one instrument per main-loop pass.
- Loop crossfading: 1024-sample crossfade in `wg.c`.
- Standard MIDI CCs: CC74=cutoff, CC71=reso, CC73=attack, etc.

**What is NOT verified:**
- Whether HEAP_HIGH allocation succeeds at runtime (does the pool actually get 1GB?)
- Whether all 129 instruments load (was 33/129 before HEAP_HIGH)
- Whether USB MIDI receives data with the xHCI fixes (DMA alignment, polling mode, packet parsing)
- Whether the existing `kernel8-rpi4.img` on disk matches the current source
- Boot-to-sound latency with 129 instruments lazy-loading

**Circle patches:** `patches/circle-usb-midi.patch` (5.3KB) fixes 3 VL805 xHCI bugs. Patch must be applied to `libs/circle-stdlib/` before building.

**SD card layout:** 129 instrument dirs in `platforms/baremetal/sdcard-files/instruments/` (760MB WAV data).

### linux-rompler

**Source:** Single-file synth engine, `platforms/linux-rompler/main.c` (1732 lines). This is the reference implementation — known working before the bare-metal port.

**Build:** Cross-compiled with `aarch64-linux-gnu-gcc`, links ALSA + pthreads.

**Status:** Binary `rompler-new` exists but no record of when it was built or tested. Needs a Linux image (Buildroot or manual) to run on the Pi.

**Wavetable:** `wave_table.h` (110KB) included but purpose unclear relative to sample-based playback.

### linux-fluidsynth

**Source:** Two shell scripts only (`deploy.sh`, `fluidsynth-start.sh`). No compiled code — uses upstream FluidSynth binary with mmap patch.

**Patches:** `patches/fluidsynth-mmap.patch` (16.2KB) adds mmap sample loading + custom CC modulators.

**Known working from:** `rompler-linux-backup.img` (4.3GB). Boots in ~12s, plays via Fantom SF2 files on partition 3.

**Not reproducible:** No Buildroot package definition. The backup image is the only tested artifact.

### baremetal-minijv880 (archived)

**Source:** `archive/original-jv880-emulator/`. Original MiniJV880 JV-880 emulator, not the custom rompler.

**Status:** Archived. Not actively maintained. Historical kernels in `releases/` (6 images from various stages).

### teensy-rompler

**Source:** `platforms/teensy/`. PlatformIO project for Teensy 4.1 with Audio Shield.

**Status:** Exists in repo but no evidence of recent development or testing. 8 voices, 44.1kHz, 128 instruments max.

## Known Repo Issues

1. **Top-level Makefile `build-linux` target** points to `platforms/linux/` which does not exist. Should be `platforms/linux-rompler/`.
2. **Top-level Makefile `clean` target** also references `platforms/linux/`.
3. **STATUS.md is stale** — dated 2026-05-04, still describes USB MIDI as broken and references old paths (`rompler/`, `mini-jv880-v2/`). Does not reflect HEAP_HIGH, lazy loading, MIDI fix, or repo restructuring.
4. **`synth/baremetal/` naming** — misleading since the engine is meant to be shared. Plan calls for rename to `synth/core/` (deferred).
5. **`synth/shared/` directory** exists but is empty.
6. **Compiled objects** (`.o`, `.d`, `.elf`, `.map`, `.lst` files) are tracked in the repo.

## What To Do Next

1. **Fresh build + hardware test** of baremetal-rompler. This resolves the three open questions (HEAP_HIGH, USB MIDI, instrument count).
2. **Update STATUS.md** to reflect current source state.
3. **Fix Makefile** `build-linux` and `clean` targets.
