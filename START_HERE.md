# START HERE: Porting Guide for Linux Rompler to Bare-Metal

## The Situation in 30 Seconds

1. **Linux version works** (`rompler-linux/main.c`): Complete synth with MIDI, audio, effects. ✓
2. **Bare-metal version is debugging USB** (`rompler/kernel.cpp`): Audio works, MIDI broken. ✗
3. **Solution**: Port the working Linux synth to bare-metal using Circle's APIs

## The Quick Path

You have **two entry points** depending on your experience:

### Quick Summary (5 min)
→ Read [`PORTING_SUMMARY.txt`](PORTING_SUMMARY.txt)

### Deep Dive (1 hour)
→ Read [`docs/PORTING_GUIDE.md`](docs/PORTING_GUIDE.md)
→ Then [`docs/LINUX_TO_BAREMETAL_MAPPING.md`](docs/LINUX_TO_BAREMETAL_MAPPING.md)
→ Finally [`docs/PORTING_CHECKLIST.md`](docs/PORTING_CHECKLIST.md)

## What You'll Learn

- **Why porting is easy**: ~70% of Linux code is portable DSP; ~30% needs OS replacement
- **How long it takes**: 6-9 hours (3-6 hours work immediately, rest blocked on USB fix)
- **What to do first**: Extract synth core, then implement audio callback, then file I/O

## Key Deliverable

By following this guide, you'll produce:
- `synth/shared/` — Portable synth engine (no OS dependencies)
- Updated `platforms/baremetal/kernel.cpp` — Circle integration
- Working bare-metal synth at 48kHz with effects

## Files to Read (in order)

| File | Purpose | Duration |
|------|---------|----------|
| `PORTING_SUMMARY.txt` | High-level overview | 5 min |
| `docs/PORTING_GUIDE.md` | Detailed strategy + risks | 30 min |
| `docs/LINUX_TO_BAREMETAL_MAPPING.md` | Code-by-code replacements | 20 min |
| `docs/PORTING_CHECKLIST.md` | Step-by-step tasks | Reference |

## Absolute Minimum to Start

Read `PORTING_SUMMARY.txt` (5 min), then:

1. **Phase 1** (1-2 hours): Extract synth core from `rompler-linux/main.c` into modular C files
2. **Phase 2** (1 hour): Implement `GetChunk()` callback in Circle
3. **Phase 3** (1 hour): Load samples from SD using FATFS
4. **Test**: Should hear synth output @ 48kHz

Then wait for USB MIDI fix to proceed with Phase 4.

## Why This Approach?

✓ **No OS knowledge needed**: Just C programming
✓ **No Circle expertise required**: API mapping provided
✓ **Unblocked immediately**: Get audio working without MIDI first
✓ **Modular approach**: Can test each phase independently

## Repository Structure

```
mini-jv880-restructured/
├── PORTING_SUMMARY.txt               ← Start here (5 min)
├── START_HERE.md                     ← You are here
├── docs/
│   ├── PORTING_GUIDE.md              ← Main guide (30 min read)
│   ├── LINUX_TO_BAREMETAL_MAPPING.md ← Code reference (while coding)
│   ├── PORTING_CHECKLIST.md          ← Step-by-step checklist
│   └── README.md                     ← Documentation index
├── platforms/
│   ├── linux/                        ← Working version (source)
│   └── baremetal/                    ← Target (modify this)
└── synth/
    └── shared/                       ← Where extracted code goes
```

## Next Step

➜ Read `PORTING_SUMMARY.txt` (5 min)
➜ Then read `docs/PORTING_GUIDE.md` (30 min)
➜ Then start Phase 1 of `docs/PORTING_CHECKLIST.md`

Good luck! 🚀
