# MiniJV880 Documentation

Complete guides for understanding, building, and porting the rompler synthesizer.

## Quick Start

**New to the project?** Start here:
1. Read [`../README.md`](../README.md) — Project overview
2. Read [`../STATUS.md`](../STATUS.md) — Current status and blockers
3. Choose your path:
   - **Building:** See [`BUILD.md`](BUILD.md)
   - **Understanding the code:** See [`ARCHITECTURE.md`](ARCHITECTURE.md) (if exists)
   - **Porting Linux to bare-metal:** See [`PORTING_GUIDE.md`](PORTING_GUIDE.md)

---

## Documentation Structure

### Overview Documents
- **[`BUILD.md`](BUILD.md)** — Build instructions for both platforms
- **[`../STATUS.md`](../STATUS.md)** — Project status, blockers, and debugging notes
- **[`../MIGRATION.md`](../MIGRATION.md)** — Repository reorganization guide

### Porting (Linux → Bare-Metal)
- **[`PORTING_GUIDE.md`](PORTING_GUIDE.md)** — Comprehensive porting strategy (6-9 hours work)
  - Phase 1: Extract synth core
  - Phase 2: Replace ALSA with Circle PWM
  - Phase 3: Replace pthread with Circle USB
  - Phase 4: Replace file I/O with FATFS
  - Risks and mitigations
  
- **[`LINUX_TO_BAREMETAL_MAPPING.md`](LINUX_TO_BAREMETAL_MAPPING.md)** — Side-by-side code comparisons
  - Audio output: ALSA → Circle PWM
  - MIDI input: pthread → Circle USB callback
  - File I/O: stdio → FATFS
  - Synchronization: pthread_mutex → CSpinLock
  - Render loop: push → pull model
  
- **[`PORTING_CHECKLIST.md`](PORTING_CHECKLIST.md)** — Step-by-step checklist for porting
  - Organized by phase
  - Code snippets provided
  - Testing checkpoints
  - Debugging tips

---

## Key Insights

### The Problem
The bare-metal version is currently stuck debugging USB MIDI reception (see `STATUS.md`). Meanwhile, the **Linux version works perfectly** with a complete synth engine.

### The Solution
Port the working Linux synth engine to bare-metal Circle. The porting is straightforward because:
- **~1200 LOC pure DSP** (portable)
- **~500 LOC OS-specific I/O** (replaceable)
- **Circle provides equivalents** for all OS calls (PWM, USB, FATFS, synchronization)

### Estimated Effort
- **Extract core:** 1-2 hours
- **Audio I/O:** 1 hour
- **File I/O:** 1 hour
- **MIDI input:** 2-3 hours (blocked on USB fix)
- **Integration & testing:** 1-2 hours
- **Total:** 6-9 hours (minus USB blocker)

---

## Current State

### ✓ Linux Version (Working)
- Complete synth engine with 24 voices
- SVF filter, ADSR envelope, LFO
- Chorus and Schroeder reverb effects
- Loads WAV samples from disk
- MIDI input via ALSA
- Audio output via ALSA PCM

### ✗ Bare-Metal Version (Debugging)
- Audio output: ✓ Working (PWM @ 48kHz)
- Sample loading: ✓ Implemented (but synth code stripped)
- MIDI input: ✗ USB enumeration works, but data never received
- Current state: Minimal test kernel (square wave only)

---

## Why Port?

### Advantages of Bare-Metal
1. **Lower latency** — No kernel scheduler interference
2. **Deterministic performance** — Predictable audio with no OS jitter
3. **Smaller footprint** — Full synth in ~500KB kernel image
4. **Full hardware control** — Direct access to PWM, USB, GPIO

### Advantages of Linux
1. **Works today** — No USB debugging needed
2. **Familiar tooling** — GDB, gprof, existing build systems
3. **Easier samples** — Load from regular filesystem
4. **Safe for development** — Kernel isolation means fewer crashes

---

## Project Structure

```
mini-jv880-restructured/
├── synth/                    # Shared synth engine (target for extraction)
│   ├── shared/              # Common DSP (to be created during port)
│   ├── baremetal/           # Current bare-metal synth source
│   └── linux/               # Future: Linux synth extraction
│
├── platforms/
│   ├── baremetal/           # Circle bare-metal (TARGET)
│   └── linux/               # Working buildroot version (SOURCE)
│
├── docs/
│   ├── BUILD.md             # Build instructions
│   ├── PORTING_GUIDE.md     # Porting strategy (START HERE for porting)
│   ├── LINUX_TO_BAREMETAL_MAPPING.md  # Code reference
│   ├── PORTING_CHECKLIST.md # Step-by-step checklist
│   └── README.md            # This file
│
└── Makefile                 # Central build orchestration
```

---

## Reading Guide by Task

### "I want to build and run this"
→ [`BUILD.md`](BUILD.md)

### "I want to understand the current status"
→ [`../STATUS.md`](../STATUS.md)

### "I want to port the Linux version to bare-metal"
→ Start with [`PORTING_GUIDE.md`](PORTING_GUIDE.md)
→ Then reference [`LINUX_TO_BAREMETAL_MAPPING.md`](LINUX_TO_BAREMETAL_MAPPING.md)
→ Finally follow [`PORTING_CHECKLIST.md`](PORTING_CHECKLIST.md)

### "I want to understand the project structure"
→ [`../MIGRATION.md`](../MIGRATION.md)

---

## Next Steps

1. **Immediate:** Fix USB MIDI reception (see `STATUS.md` for hypotheses)
2. **Short-term:** Extract synth core from Linux version (Phase 1 of porting)
3. **Medium-term:** Complete bare-metal port (Phases 2-5)
4. **Long-term:** Add advanced features (MCF preset loading, better effects, etc.)

---

## Technical Details

### Audio
- **Sample rate:** 48kHz (bare-metal), 44.1kHz (Linux)
- **Channels:** Stereo (2x PWM on Pi 4 GPIO 18/19)
- **Bit depth:** 16-bit signed PCM
- **Buffer size:** 512 samples (~10ms)

### Synth Engine
- **Voices:** 24 simultaneous
- **Waveforms:** ROM wavetable (108 waveforms @ 32kHz)
- **Filter:** 2-pole SVF lowpass (analog emulation)
- **Modulation:** ADSR envelope, LFO, CC control
- **Effects:** Chorus (triangle LFO), Reverb (Schroeder)

### Hardware
- **Raspberry Pi 4 Model B** (8GB recommended)
- **SD card:** Bootloader + firmware + samples (min 4GB)
- **MIDI:** USB keyboard (M-Audio Axiom 49 tested)
- **Audio:** PWM @ GPIO18/19 or analog via HAT

---

## References

- **Circle framework:** https://github.com/rsta2/circle
- **Nuked SC-55:** https://github.com/nukeykt/Nuked-SC55
- **MiniDexed:** https://github.com/probonopd/MiniDexed
- **Raspberry Pi:** https://www.raspberrypi.com/

---

## Questions?

- **About building:** See [`BUILD.md`](BUILD.md)
- **About porting:** See [`PORTING_GUIDE.md`](PORTING_GUIDE.md)
- **About structure:** See [`../MIGRATION.md`](../MIGRATION.md)
- **About status:** See [`../STATUS.md`](../STATUS.md)
