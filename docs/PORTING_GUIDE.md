# Porting Linux Rompler to Bare-Metal

The **Linux version** (`rompler-linux/main.c`, 1732 lines) is the **working implementation** with full synth engine. This guide explains how to port it to bare-metal Circle.

## Overview

The Linux version is well-structured:
- **~1200 LOC** — Pure DSP/synth engine (portable)
- **~500 LOC** — OS-specific I/O (ALSA audio, pthread, file I/O)

### Porting Strategy

```
Linux main.c (1732 LOC)
    │
    ├─ DSP Engine (~1200 LOC) ✓ PORTABLE
    │  ├─ Zone, Instrument, Voice structures
    │  ├─ ADSR envelope, SVF filter, LFO
    │  ├─ Chorus & reverb effects
    │  └─ Wavetable synthesis
    │
    └─ OS Layer (~500 LOC) ← NEEDS REPLACEMENT
       ├─ ALSA audio output
       ├─ pthread MIDI input thread
       ├─ File I/O (fopen, opendir)
       └─ Memory management (malloc)

Circle Bare-Metal provides:
    ├─ PWM audio output (CPWMSoundBaseDevice) ✓
    ├─ USB MIDI input ✓ (currently broken, under debug)
    ├─ FAT32 SD card access ✓
    └─ Memory pool allocator ✓
```

## Step-by-Step Porting Plan

### Phase 1: Extract Portable Core (1-2 hours)

**Goal:** Create `synth/shared/rompler_core.c/h` with pure DSP code.

**What to extract from Linux main.c:**

1. **Data structures** (lines ~30-100):
   ```c
   typedef struct { const int16_t *data; int len; ... } Zone;
   typedef struct { char name[32]; Zone zones[MAX_ZONES]; ... } Instrument;
   typedef struct { double phase, gain, ...; } Voice;
   ```

2. **DSP functions** (extract into separate headers):
   ```
   voice.h/c       — Voice initialization, note on/off, rendering
   envelope.h/c    — ADSR envelope generator
   filter.h/c      — SVF lowpass filter
   lfo.h/c         — LFO oscillator
   effects.h/c     — Chorus and reverb
   sample_bank.h/c — Zone/instrument loading and management
   ```

3. **Wave table** (already extracted):
   ```c
   #include "wave_table.h"  /* g_rom_waves[108] — 108KB ROM wavetable */
   ```

4. **Global state** (make thread-safe on bare-metal):
   ```c
   Voice g_voices[NUM_VOICES];
   Instrument g_instruments[MAX_INSTRUMENTS];
   int g_active_voice_count;
   ```

**Deliverable:** Modular C library with no OS dependencies.

---

### Phase 2: Replace ALSA Audio with Circle PWM (1-2 hours)

**Current bare-metal kernel already has:**
- `CPWMSoundBaseDevice` — PWM audio output @ 48kHz stereo
- `GetChunk()` callback to fill audio buffers

**What to do:**

1. **Replace ALSA setup:**
   ```c
   /* Linux version:
      snd_pcm_open(&pcm, "hw:Headphones,0", SND_PCM_STREAM_PLAYBACK, 0);
      snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16, ...);
   */
   
   /* Bare-metal: Already done in kernel.cpp
      m_pSoundDevice = new CSynthSound(&m_Interrupt);
   */
   ```

2. **Adapt render loop:**
   ```c
   /* Linux version: Main loop pumps snd_pcm_writei()
   
      Bare-metal: GetChunk() is called by PWM ISR
      - Must complete in ~10ms for 512 samples @ 48kHz
      - No blocking I/O allowed
   */
   ```

3. **Audio format:** Both use 16-bit signed PCM @ 48kHz, so format is compatible.

---

### Phase 3: Replace pthread MIDI with Circle USB (2-3 hours)

**Current state of bare-metal USB:**
- Device enumerates ✓
- Endpoint configured ✓
- **Data not received** ✗ (blocker being debugged in STATUS.md)

**What to do:**

1. **Replace pthread mutex with Circle's CSynchronize:**
   ```c
   /* Linux version:
      pthread_mutex_t vlock;
      pthread_mutex_lock(&vlock);
      pthread_mutex_unlock(&vlock);
   */
   
   /* Bare-metal equivalent:
      #include <circle/synchronize.h>
      CSpinLock lock;
      lock.Acquire();
      lock.Release();
   */
   ```

2. **MIDI thread replacement:**
   ```c
   /* Linux: Dedicated thread reads MIDI via open()
      pthread_create(&tid, NULL, midi_thread, (void *)midi_dev);
   */
   
   /* Bare-metal: USB MIDI callback
      In xHCI completion ISR, extract MIDI data and queue to g_midi_fifo
      Main audio thread reads from FIFO during GetChunk()
   */
   ```

3. **Callback pattern:**
   ```c
   void OnMIDIByte(u8 byte) {
       // Called from USB ISR
       // Parse MIDI (status, data1, data2)
       // Update g_voices[] appropriately
   }
   ```

---

### Phase 4: Replace File I/O with Circle FAT32 (1 hour)

**Linux version reads:**
- WAV sample banks from `/rom_samples/` directory
- SoundFont 2 files (`rompler.sf2`)

**Circle provides:**
- `CFATFS` — FAT32 driver for SD card
- `CFile` — C-style file operations

**Replacement:**
```c
/* Linux:
   FILE *f = fopen("/rom_samples/01-sine.wav", "rb");
   fread(data, 1, size, f);
   fclose(f);
*/

/* Bare-metal:
   #include <circle/fs/fat/fatfs.h>
   CFile File (m_pFileSystem, "ROM_SAMPLES\\01_SINE.WAV", FM_READ);
   File.Read(data, size);
   File.Close();
*/
```

**Directory scanning:**
```c
/* Linux:
   DIR *d = opendir("/rom_samples");
   struct dirent *entry;
   while ((entry = readdir(d)) != NULL) { ... }
   closedir(d);
*/

/* Bare-metal:
   #include <circle/fs/fat/fatfs.h>
   FATFS fs;
   FILINFO fno;
   DIR dir;
   f_opendir(&dir, "ROM_SAMPLES");
   while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) { ... }
   f_closedir(&dir);
*/
```

---

### Phase 5: Memory Management (30 min)

**Linux uses malloc/free:** Should work via Circle's malloc (backed by sbrk).

**Optimization for bare-metal (optional):**
- Pre-allocate voice buffers at boot
- Use memory pool allocator instead of malloc
- Cap sample buffer pool to 32MB

---

## Implementation Checklist

### 1. Extract Synth Core
- [ ] Create `synth/shared/rompler_core.h`
- [ ] Extract data structures (Zone, Instrument, Voice)
- [ ] Extract DSP functions into modular files
- [ ] Add no-OS-dependency compilation test

### 2. Replace Audio I/O
- [ ] Implement `GetChunk()` callback in `CSynthSound`
- [ ] Call synth engine render function
- [ ] Verify 48kHz stereo output works

### 3. Replace MIDI Input
- [ ] Fix USB MIDI reception (current blocker)
- [ ] Add MIDI parser callback
- [ ] Queue MIDI events for audio thread
- [ ] Replace pthread mutexes with `CSpinLock`

### 4. Replace File I/O
- [ ] Add CFATFS initialization to kernel
- [ ] Load WAV samples from SD card at boot
- [ ] Load instrument patches from SD card
- [ ] Implement directory scanning

### 5. Integration Testing
- [ ] Build bare-metal kernel with full synth
- [ ] Deploy and verify audio output
- [ ] Test MIDI note on/off
- [ ] Test effect controls (CC)

---

## Code Locations

### Linux Source
```
rompler-linux/main.c (1732 LOC)
  - Lines 30-100   : Data structures
  - Lines 120-300  : Effect implementations (chorus, reverb)
  - Lines 300-700  : Synth core (voice rendering, envelope, filter)
  - Lines 700-1100 : Sample loading, instrument management
  - Lines 1100-1600: Main loop, ALSA I/O
  - Lines 1600-1732: pthread MIDI thread
```

### Current Bare-Metal
```
rompler/kernel.{h,cpp}
  - CTestSound class (test square wave)
  - CKernel main loop
  - PWM audio already configured
  - USB MIDI enumeration code (needs debugging)
```

### Target Integration
```
rompler/kernel.cpp (update):
  - Replace CTestSound with CSynthSound
  - Integrate rompler_core.c
  - Add CFATFS initialization
  - Replace USB MIDI polling with callback

rompler/synth/shared/ (new):
  - rompler_core.{h,c}
  - voice.{h,c}
  - effects.{h,c}
  - sample_bank.{h,c}
```

---

## Key Differences (Bare-Metal vs Linux)

| Aspect | Linux | Bare-Metal | Porting Notes |
|--------|-------|-----------|---------------|
| **Audio Output** | ALSA PCM | Circle PWM | Format same, API different |
| **MIDI Input** | pthread file read | Circle USB callback | Must be non-blocking |
| **Sample Storage** | Linux filesystem | FAT32 on SD card | Path syntax changes |
| **Threading** | POSIX pthreads | None (bare-metal) | Use spinlocks if needed |
| **Sample Rate** | 44.1 kHz | 48 kHz | Filter coefficients may need adjustment |
| **Memory** | Unlimited heap | ~4GB on Pi 4 | Pre-allocate buffers |

---

## Estimated Effort

| Phase | LOC | Time | Difficulty |
|-------|-----|------|------------|
| Extract core | 200-300 | 1-2h | Low |
| Audio I/O | 100-150 | 1h | Low |
| MIDI input | 150-200 | 2-3h | Medium (USB blocked) |
| File I/O | 100-150 | 1h | Low |
| Integration testing | - | 1-2h | Medium |
| **Total** | **550-800** | **6-9h** | **Medium** |

---

## Risks & Mitigations

### Risk 1: USB MIDI Still Broken
**Mitigation:** Debug USB reception first (ongoing in STATUS.md). Consider:
- Forcing interrupt endpoints instead of bulk
- Using internal USB controller instead of VL805
- JTAG debugging to inspect xHCI registers

### Risk 2: Audio Dropout
**Mitigation:** Ensure GetChunk() completes in <10ms. Profile with Circle's profiling tools.

### Risk 3: Sample Bank Size
**Mitigation:** Currently 32MB pool. On 8GB Pi 4, this is conservative. Can increase if needed.

### Risk 4: Sample Rate Mismatch
**Mitigation:** Linux uses 44.1kHz, bare-metal targets 48kHz. May need to resample or regenerate sample data.

---

## References

- **Linux source:** `rompler-linux/main.c`
- **Circle PWM:** `circle-stdlib/libs/circle/device/pwmsoundbasedevice.cpp`
- **Circle USB MIDI:** `circle-stdlib/libs/circle/usb/usbmidihost.cpp`
- **Circle FATFS:** `circle-stdlib/libs/circle/fs/fat/fatfs.h`
- **Current status:** `STATUS.md` (USB MIDI blocker)
