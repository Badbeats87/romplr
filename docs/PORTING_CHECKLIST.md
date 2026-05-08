# Porting Checklist: Linux → Bare-Metal

Quick reference for porting `rompler-linux/main.c` to Circle bare-metal.

## Phase 1: Extract Synth Core (No OS Dependencies)

- [ ] **Create** `synth/shared/rompler_core.h`
  - [ ] Zone struct (lines ~35-40)
  - [ ] Instrument struct (lines ~45-50)
  - [ ] Voice struct (lines ~55-80)
  
- [ ] **Create** `synth/shared/voice.c`
  - [ ] `voice_init()`
  - [ ] `voice_note_on(ucNote, ucVel)`
  - [ ] `voice_note_off()`
  - [ ] `voice_render(outbuf, frames, *sample_buffers)`
  
- [ ] **Create** `synth/shared/envelope.c`
  - [ ] ADSR state machine
  - [ ] Lines ~200-240
  
- [ ] **Create** `synth/shared/filter.c`
  - [ ] SVF lowpass filter
  - [ ] Lines ~240-280
  
- [ ] **Create** `synth/shared/lfo.c`
  - [ ] LFO oscillator
  - [ ] Lines ~280-320
  
- [ ] **Create** `synth/shared/effects.c`
  - [ ] Chorus implementation (lines ~1450-1520)
  - [ ] Reverb/Schroeder (lines ~1520-1600)
  
- [ ] **Copy** `synth/shared/wave_table.h`
  - [ ] `wave_table.h` (108KB ROM wavetable)
  
- [ ] **Create** `synth/shared/sample_bank.c`
  - [ ] `load_wav_samples()` — OS-agnostic signature
  - [ ] `load_instruments()` — OS-agnostic signature
  - [ ] Zone/instrument management

---

## Phase 2: Audio Output (Circle PWM)

**File:** `platforms/baremetal/kernel.{h,cpp}`

- [ ] **Create** `CSynthSound` class inheriting from `CPWMSoundBaseDevice`
  
- [ ] **Implement** `GetChunk()` callback:
  ```cpp
  unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override {
      int16_t *out = (int16_t *)pBuffer;
      unsigned frames = nChunkSize / 4;  // nChunkSize is in bytes, 2ch stereo
      
      m_pKernel->m_VoiceLock.Acquire();
      synth_render_chunk(out, frames);  // Call into synth_core.c
      m_pKernel->m_VoiceLock.Release();
      
      return nChunkSize;
  }
  ```
  
- [ ] **Instantiate** in `CKernel::Initialize()`:
  ```cpp
  m_pSoundDevice = new CSynthSound(&m_Interrupt, this);
  if (!m_pSoundDevice->Start()) {
      // Handle error
  }
  ```
  
- [ ] **Add member** to `CKernel`:
  ```cpp
  private:
      CSynthSound *m_pSoundDevice;
      CSpinLock m_VoiceLock;
  ```

---

## Phase 3: File I/O (FATFS)

**File:** `platforms/baremetal/kernel.cpp`

- [ ] **Include** FATFS headers:
  ```cpp
  #include <circle/fs/fat/fatfs.h>
  #include <circle/fs/fat/fat.h>
  #include <circle/fs/romdisk.h>
  ```

- [ ] **Add members** to `CKernel`:
  ```cpp
  private:
      CFATFS *m_pFileSystem;
  ```

- [ ] **Mount SD card** in `Initialize()`:
  ```cpp
  m_pFileSystem = new CFATFS();
  if (!m_pFileSystem->Mount(m_pBlockDevice)) {
      CLogger::Get()->Write(FromKernel, LogError, "FS mount failed");
  }
  ```

- [ ] **Create wrapper** `sample_bank_load_baremetal.c`:
  ```c
  // Adapts rompler_core.c's load_wav_samples() to use FATFS
  int load_wav_samples_baremetal(CFATFS *fs, char **out_buffers[]) {
      // Replace fopen/fread with f_open/f_read
      // See LINUX_TO_BAREMETAL_MAPPING.md for code
  }
  ```

- [ ] **Call at boot** in `Run()`:
  ```cpp
  load_wav_samples_baremetal(m_pFileSystem, sample_buffers);
  load_instruments_baremetal(m_pFileSystem, instruments);
  ```

---

## Phase 4: MIDI Input (USB)

**File:** `platforms/baremetal/kernel.cpp`

⚠️ **BLOCKED:** USB MIDI reception currently broken (see STATUS.md)

- [ ] **Investigate blocker** first:
  - [ ] Review xHCI endpoint configuration
  - [ ] Try explicit `SET_INTERFACE(0)` before reading
  - [ ] Dump xHCI device context to verify "Running" state
  - [ ] Consider forcing interrupt endpoints instead of bulk
  - [ ] Test with internal USB controller (`USE_XHCI_INTERNAL`)

- [ ] **Once USB works**, create MIDI callback:
  ```cpp
  class CMIDIReceiver {
      CUSBMIDIDevice *m_pMIDI;
      CFIFOBuffer m_MIDIQueue;  // Lock-free if possible
      
      static void MIDICallback(u8 byte) {
          // Called from USB ISR
          instance->m_MIDIQueue.Write(&byte, 1);
      }
  };
  ```

- [ ] **Add to `CKernel`**:
  ```cpp
  private:
      CMIDIReceiver m_MIDIReceiver;
  ```

- [ ] **Process MIDI** in render loop:
  ```cpp
  void CKernel::Run() {
      while (m_bRunning) {
          m_VoiceLock.Acquire();
          
          // Drain MIDI queue
          while (m_MIDIReceiver.HasData()) {
              u8 byte = m_MIDIReceiver.Dequeue();
              ProcessMIDIByte(byte);
          }
          
          m_VoiceLock.Release();
      }
  }
  ```

- [ ] **Replace mutex** with `CSpinLock`:
  ```cpp
  m_VoiceLock.Acquire();
  synth_render_chunk(...);
  m_VoiceLock.Release();
  ```

---

## Phase 5: Compilation & Linking

**File:** `platforms/baremetal/Makefile`

- [ ] **Add synth sources**:
  ```makefile
  SYNTH_OBJS = ../../../synth/shared/voice.o \
               ../../../synth/shared/envelope.o \
               ../../../synth/shared/filter.o \
               ../../../synth/shared/lfo.o \
               ../../../synth/shared/effects.o \
               ../../../synth/shared/sample_bank.o \
               ../../../synth/shared/rompler_core.o
  ```

- [ ] **Update object list**:
  ```makefile
  OBJS = kernel.o main.o chainboot.o $(SYNTH_OBJS)
  ```

- [ ] **Add include paths**:
  ```makefile
  INCLUDE = -I../../../synth/shared \
            -I$(CIRCLEHOME)/include
  ```

- [ ] **Test build**:
  ```bash
  cd platforms/baremetal
  make clean && make
  ```

---

## Phase 6: Integration Testing

### Checkpoint 1: Audio Without MIDI
- [ ] Build kernel successfully
- [ ] Deploy to Pi via WiFi TFTP
- [ ] Hear test tone on HDMI/audio out
- [ ] No crashes or underruns

### Checkpoint 2: Sample Loading
- [ ] Load WAV samples from SD card at boot
- [ ] Verify samples load without errors
- [ ] Log sample count and sizes

### Checkpoint 3: Basic Synth Rendering
- [ ] Render synth with hardcoded note (no MIDI)
- [ ] Hear synthesized sound (not just silence)
- [ ] Adjust volume/pan settings

### Checkpoint 4: MIDI Input (After USB Fixed)
- [ ] Press keys on M-Audio Axiom 49
- [ ] Hear notes play
- [ ] Verify note on/off works
- [ ] Test velocity sensitivity

### Checkpoint 5: Effects
- [ ] Enable chorus, hear modulation
- [ ] Enable reverb, hear tail
- [ ] Test CC controls (filter cutoff, reverb mix, etc.)

---

## Debugging Tips

### Build Errors
```bash
# Check for missing includes
grep -r "undefined reference" build.log

# Verify Circle paths
echo $CIRCLEHOME
ls $(CIRCLEHOME)/include/circle/
```

### Runtime Crashes
- Enable HDMI console logging
- Add debug prints to synth_render_chunk()
- Use Circle's `CLogger::Get()->Write()` frequently

### Audio Issues
- Check sample rate mismatch (Linux 44.1kHz → Bare-metal 48kHz)
- Verify PWM output enabled (check GPIO pins 18, 19)
- Monitor for ISR overruns: `GetChunk()` must complete in <10ms

### MIDI Reception
- Use USB analyzer to capture packets (if available)
- Log all USB events: device enumeration, endpoint setup, transfers
- Check xHCI controller logs in STATUS.md

---

## Minimal Viable Product

Get working audio without MIDI first:

1. ✓ Extract synth core (Phase 1)
2. ✓ Implement GetChunk() with fixed note (Phase 2)
3. ✓ Load samples from SD (Phase 3)
4. ⊘ Skip MIDI (Phase 4) — wait for USB fix
5. ✓ Verify audio output

**Expected result:** Play 440Hz sine wave through effects chain.

---

## Estimated Timeline

| Phase | Task | Duration | Status |
|-------|------|----------|--------|
| 1 | Extract core | 1-2h | Ready |
| 2 | Audio output | 1h | Ready |
| 3 | File I/O | 1h | Ready |
| 4 | MIDI input | 2-3h | **BLOCKED** |
| 5 | Build | 30min | Ready |
| 6 | Testing | 1-2h | Ready |
| **Total** | | **6-9h** | Minus USB blocker |

---

## Files to Review

Before starting:

- [ ] `rompler-linux/main.c` — Source of truth (~1732 lines)
- [ ] `platforms/baremetal/kernel.cpp` — Target integration point
- [ ] `STATUS.md` — Current USB MIDI blocker
- [ ] `LINUX_TO_BAREMETAL_MAPPING.md` — API reference
- [ ] `circle-stdlib/libs/circle/device/pwmsoundbasedevice.h` — Audio API
- [ ] `circle-stdlib/libs/circle/fs/fat/fatfs.h` — File I/O API

---

## Success Criteria

✓ **Audio working:** Hear synth output at 48kHz stereo via PWM
✓ **Samples loaded:** Custom instruments playable from SD card
✓ **MIDI working:** Play notes from M-Audio Axiom 49 (after USB fix)
✓ **Effects working:** Chorus and reverb operational
✓ **No crashes:** Stable runtime >1 hour
