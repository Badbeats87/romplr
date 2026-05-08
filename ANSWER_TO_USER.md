# How to Port the Linux Rompler to Bare-Metal — Complete Answer

**Your Question:** "The Linux version is the correct version. How to port it to baremetal?"

**Short Answer:** Extract the synth engine (6-9 hours of work, 3-6 of it immediately doable).

**Long Answer:** See guides below.

---

## What You Have

### Working: Linux Version (`rompler-linux/main.c`)
- **1732 lines** of complete synth code
- 24-voice rompler with effects (chorus, reverb)
- MIDI input from M-Audio Axiom 49
- Audio output via ALSA PCM
- Loads WAV samples and instrument configs

### Broken: Bare-Metal Version (`rompler/kernel.cpp`)
- **Audio output**: ✓ Works (48kHz stereo via PWM)
- **Sample loading**: ✓ Can work (SD card + FATFS available)
- **MIDI input**: ✗ Broken (USB enumeration OK, data reception not working)
- **Current state**: Stripped down to test kernel (just square wave)

---

## Why Porting Works

The Linux code breaks down nicely:

```
rompler-linux/main.c (1732 LOC)
├─ PORTABLE DSP (1200 LOC)  ← Extract & reuse
│  ├─ Voice structures & management
│  ├─ ADSR envelope generator
│  ├─ SVF filter (lowpass)
│  ├─ LFO oscillator
│  ├─ Chorus effect
│  ├─ Reverb (Schroeder)
│  └─ Sample bank loading
│
└─ OS-SPECIFIC I/O (500 LOC)  ← Replace with Circle
   ├─ ALSA audio setup        → Circle PWM
   ├─ ALSA playback loop      → GetChunk() callback
   ├─ pthread MIDI thread     → USB callback
   ├─ pthread mutex locking   → CSpinLock
   └─ File I/O (fopen/fread)  → FATFS
```

---

## The 5-Phase Plan

### Phase 1: Extract Synth Core (1-2 hours)
**What:** Modularize the DSP engine
**Output:** `synth/shared/` with portable C files

Create:
- `voice.c/h` — Voice lifecycle (note on/off, rendering)
- `envelope.c/h` — ADSR envelope state machine
- `filter.c/h` — SVF lowpass filter
- `lfo.c/h` — LFO oscillator
- `effects.c/h` — Chorus & reverb
- `sample_bank.c/h` — Zone/instrument management

**Why this works:** Pure C, no OS dependencies. Just cut & paste + minimal refactoring.

### Phase 2: Audio I/O (1 hour)
**What:** Replace ALSA with Circle PWM
**Change:** Push model (main loop) → Pull model (ISR callback)

Replace:
```c
/* Linux: snd_pcm_open, snd_pcm_set_params, snd_pcm_writei */
/* Bare-metal: CPWMSoundBaseDevice::GetChunk() override */
```

**Deliverable:** CSynthSound class that calls synth engine on each PWM interrupt.

### Phase 3: File I/O (1 hour)
**What:** Replace stdio with FATFS
**Change:** Linux filesystem paths → SD card FAT32

Replace:
```c
/* Linux: fopen/fread/closedir */
/* Bare-metal: FATFS f_open/f_read/f_readdir */
```

**Deliverable:** Load WAV samples and instrument configs from SD at boot.

### Phase 4: MIDI Input (2-3 hours) **[BLOCKED]**
**What:** Replace pthread with Circle USB callback
**Status:** Blocked on USB MIDI reception fix (see STATUS.md)

Replace:
```c
/* Linux: pthread_create + open()/read() on /dev/midi0 */
/* Bare-metal: USB callback + CFIFOBuffer queue */
```

**Once USB works:** Add MIDI callback to handle incoming bytes.

### Phase 5: Build & Test (1-2 hours)
**What:** Integration testing

Checkpoints:
- [ ] Synth core compiles without errors
- [ ] Audio output at 48kHz (verify with headphones/scope)
- [ ] Samples load from SD card
- [ ] MIDI keys trigger voices (after USB fix)
- [ ] Effects (chorus, reverb) work
- [ ] No crashes in 1+ hour runtime

---

## What You Can Do NOW (Don't Wait for USB Fix)

The USB MIDI issue shouldn't block you:

1. **Extract synth core** (Phase 1) — 1-2 hours
2. **Implement GetChunk()** (Phase 2) — 1 hour
3. **Load samples** (Phase 3) — 1 hour
4. **Test with hardcoded notes** — 30 min

Result: Hear synth output at 48kHz without MIDI input.

Then wait for USB fix, then add MIDI.

---

## Documentation Provided

All guides are in `/mini-jv880-restructured/`:

| Guide | Purpose |
|-------|---------|
| `START_HERE.md` | 2-min entry point |
| `PORTING_SUMMARY.txt` | 5-min executive overview |
| `docs/PORTING_GUIDE.md` | 30-min detailed strategy |
| `docs/LINUX_TO_BAREMETAL_MAPPING.md` | API reference (use while coding) |
| `docs/PORTING_CHECKLIST.md` | Step-by-step tasks with code |
| `docs/README.md` | Documentation index |

---

## Key Technical Points

### Audio
- **Linux:** 44.1kHz ALSA PCM
- **Bare-metal:** 48kHz Circle PWM (may need resampling)

### MIDI
- **Linux:** Blocking read from `/dev/midi0` in separate thread
- **Bare-metal:** Interrupt-driven USB callback into FIFO queue

### File I/O
- **Linux:** Standard POSIX open/read/closedir
- **Bare-metal:** FATFS (similar API, different paths)

### Synchronization
- **Linux:** `pthread_mutex_lock/unlock`
- **Bare-metal:** `CSpinLock::Acquire/Release`

---

## Risk Mitigation

| Risk | Probability | Mitigation |
|------|-----------|-----------|
| Audio dropout | Medium | Keep GetChunk() <10ms, profile code |
| Sample rate mismatch | Low | Resample or regenerate WAV data |
| USB MIDI still broken | Medium | Start without MIDI, debug first |
| Memory too tight | Low | Pi 4 has 8GB, synth needs ~40MB |

---

## Success Criteria

✓ Synth renders at 48kHz stereo via PWM (you hear it)
✓ Samples load from SD card without errors
✓ MIDI input works (after USB fix)
✓ Chorus and reverb effects are audible
✓ No crashes in 1+ hour runtime

---

## Estimated Timeline

| Phase | Task | Duration | Status |
|-------|------|----------|--------|
| 1 | Extract core | 1-2h | Ready now ✓ |
| 2 | Audio I/O | 1h | Ready now ✓ |
| 3 | File I/O | 1h | Ready now ✓ |
| 4 | MIDI input | 2-3h | Blocked on USB |
| 5 | Testing | 1-2h | Ready after 1-3 |
| **Total** | | **6-9h** | 3-6h now, rest blocked |

---

## Next Steps

1. **Read** `START_HERE.md` (2 min)
2. **Read** `PORTING_SUMMARY.txt` (5 min)
3. **Read** `docs/PORTING_GUIDE.md` (30 min)
4. **Open** `docs/PORTING_CHECKLIST.md`
5. **Start** Phase 1: Extract synth core

---

## FAQ

**Q: Do I need to know Circle?**
A: No. All API replacements are documented with examples in `docs/LINUX_TO_BAREMETAL_MAPPING.md`.

**Q: Can I start without fixing USB?**
A: Yes! Get audio working first (Phases 1-3), then wait for USB fix.

**Q: How much code do I need to write?**
A: Mostly copy-paste from Linux version. New code: ~200-300 lines of Circle integration.

**Q: Will the bare-metal version be better than Linux?**
A: Yes! Lower latency, no OS jitter, full hardware control. But Linux version works today.

**Q: What if I can't figure out Phase 4 (MIDI)?**
A: Wait for the USB fix (see STATUS.md). It's a known blocker with documented hypotheses.

---

## References

- **Linux source:** `/mini-jv880-v2/rompler-linux/main.c`
- **Circle PWM:** `circle-stdlib/libs/circle/device/pwmsoundbasedevice.h`
- **Circle FATFS:** `circle-stdlib/libs/circle/fs/fat/fatfs.h`
- **USB MIDI blocker:** `STATUS.md`

---

## Summary

**The Linux version works. The bare-metal port is straightforward:**

1. Extract the synth engine (~1200 lines of portable C)
2. Replace OS I/O calls with Circle equivalents (~500 lines)
3. Build and test

**Start immediately:** Phases 1-3 are unblocked (3-6 hours work).
**Then wait:** Phase 4 (MIDI) blocked on USB fix.

All guidance provided. Begin with `START_HERE.md` → `PORTING_SUMMARY.txt` → `docs/PORTING_GUIDE.md`.

Good luck! 🚀
