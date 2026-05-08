# Linux → Bare-Metal Code Mapping

Direct mapping of Linux rompler code to Circle bare-metal equivalents.

## Audio Output

### Linux (ALSA)
```c
#include <alsa/asoundlib.h>

snd_pcm_t *pcm;
snd_pcm_open(&pcm, "hw:Headphones,0", SND_PCM_STREAM_PLAYBACK, 0);
snd_pcm_set_params(pcm, 
    SND_PCM_FORMAT_S16,      /* 16-bit signed */
    SND_PCM_ACCESS_RW_INTERLEAVED,
    1,                        /* 1 channel (mono) */
    44100,                    /* 44.1 kHz */
    1,                        /* no soft resample */
    200000);                  /* latency */

/* Main render loop */
while (running) {
    render_synth(outbuf, period);
    snd_pcm_sframes_t frames = snd_pcm_writei(pcm, outbuf, period);
}

snd_pcm_close(pcm);
```

### Bare-Metal (Circle PWM)
```c
#include <circle/sound/pwmsoundbasedevice.h>

class CSynthSound : public CPWMSoundBaseDevice {
public:
    CSynthSound(CInterruptSystem *pInterrupt)
        : CPWMSoundBaseDevice(pInterrupt, 48000, 512)  // 48kHz, 512 sample chunks
    { }
    
    unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override {
        // Called by PWM ISR (~10ms intervals)
        // Must complete quickly!
        render_synth((int16_t *)pBuffer, nChunkSize / 4);
        return nChunkSize;
    }
};

// In kernel.cpp
m_pSoundDevice = new CSynthSound(&m_Interrupt);
if (!m_pSoundDevice->Start()) {
    CLogger::Get()->Write(FromKernel, LogError, "Cannot start sound device");
    return;
}
```

**Key Differences:**
- Linux: Blocking write loop (handles buffering)
- Bare-metal: Pull model via GetChunk() callback
- Linux: 44.1kHz, 512 samples = ~11.6ms buffering
- Bare-metal: 48kHz, 512 samples = ~10.7ms buffering
- **Action:** Adapt render loop to pull, not push

---

## MIDI Input

### Linux (pthread file descriptor)
```c
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

pthread_mutex_t vlock = PTHREAD_MUTEX_INITIALIZER;

void *midi_thread(void *arg) {
    int fd = open((const char *)arg, O_RDONLY);
    unsigned char buf[3];
    
    while (read_running) {
        ssize_t n = read(fd, buf, 3);  /* Blocking read */
        if (n > 0) {
            pthread_mutex_lock(&vlock);
            process_midi_msg(buf[0], buf[1], buf[2]);
            pthread_mutex_unlock(&vlock);
        }
    }
    close(fd);
    return NULL;
}

int main() {
    pthread_t tid;
    pthread_create(&tid, NULL, midi_thread, (void *)"/dev/midi0");
    
    /* Main render loop uses vlock */
    while (running) {
        pthread_mutex_lock(&vlock);
        render_voices(outbuf);
        pthread_mutex_unlock(&vlock);
    }
}
```

### Bare-Metal (Circle USB callback)
```c
#include <circle/usb/usbmidihost.h>
#include <circle/synchronize.h>

class CKernel {
private:
    CSpinLock m_MIDILock;
    CUSBMIDIDevice *m_pMIDI;
    
    static void MIDICallback(u8 ucByte) {
        // Called from USB ISR — must be fast!
        // Parse MIDI byte (handle running status, SysEx, etc.)
        instance->m_MIDIQueue.Append(ucByte);
    }
    
public:
    boolean Initialize() {
        m_pMIDI = new CUSBMIDIDevice(&m_USB);
        m_pMIDI->RegisterMIDIReceiveCallback(MIDICallback);
        return TRUE;
    }
    
    void Run() {
        while (m_bRunning) {
            /* Render loop */
            m_MIDILock.Acquire();
            while (m_MIDIQueue.GetCount() > 0) {
                u8 byte = m_MIDIQueue.Dequeue();
                ProcessMIDI(byte);
            }
            render_voices(outbuf);
            m_MIDILock.Release();
        }
    }
};
```

**Key Differences:**
- Linux: Dedicated thread blocks on read()
- Bare-metal: Callback-driven from USB ISR
- Linux: Can sleep while waiting for MIDI
- Bare-metal: Must be non-blocking, use queue
- **Action:** Implement MIDI parser state machine, use lock-free queue if possible

---

## File I/O (Sample Loading)

### Linux (standard C file I/O)
```c
#include <stdio.h>
#include <dirent.h>

int load_wav_samples() {
    DIR *d = opendir("/rom_samples");
    if (!d) return -1;
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(d)) != NULL && count < MAX_SAMPLE_BUFFERS) {
        if (entry->d_type != DT_REG) continue;  /* Skip directories */
        if (!str_ends_with(entry->d_name, ".wav")) continue;
        
        char path[256];
        snprintf(path, sizeof(path), "/rom_samples/%s", entry->d_name);
        
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        
        size_t size = get_file_size(f);
        sample_buffers[count] = malloc(size);
        fread(sample_buffers[count], 1, size, f);
        fclose(f);
        
        count++;
    }
    closedir(d);
    return count;
}

int load_instrument_config() {
    FILE *f = fopen("/instruments/config.txt", "rb");
    /* Parse instrument definitions */
    fclose(f);
}
```

### Bare-Metal (Circle FATFS)
```c
#include <circle/fs/fat/fatfs.h>
#include <circle/fs/fat/fat.h>

int load_wav_samples() {
    FATFS fs;
    DIR dir;
    FILINFO fno;
    
    // Mount SD card
    FRESULT res = f_mount(&fs, "", 1);
    if (res != FR_OK) {
        CLogger::Get()->Write(FromKernel, LogError, "Mount failed");
        return 0;
    }
    
    // Open directory
    res = f_opendir(&dir, "ROM_SAMPLES");
    if (res != FR_OK) return 0;
    
    int count = 0;
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (fno.fattrib & AM_DIR) continue;  /* Skip directories */
        if (!str_ends_with(fno.fname, ".wav")) continue;
        
        char path[256];
        snprintf(path, sizeof(path), "ROM_SAMPLES/%s", fno.fname);
        
        FIL file;
        res = f_open(&file, path, FA_READ);
        if (res != FR_OK) continue;
        
        UINT br;
        DWORD size = f_size(&file);
        sample_buffers[count] = malloc(size);
        f_read(&file, sample_buffers[count], size, &br);
        f_close(&file);
        
        count++;
        if (count >= MAX_SAMPLE_BUFFERS) break;
    }
    f_closedir(&dir);
    return count;
}
```

**Key Differences:**
- Linux: POSIX `open/read/close`
- Bare-Metal: FatFS `f_open/f_read/f_close`
- Linux: Returns `struct dirent` with `d_name`
- Bare-Metal: Fills `FILINFO` with `fname`
- Linux: Absolute paths from root `/`
- Bare-Metal: Paths relative to SD root (no leading `/`)
- **Action:** Create compatibility wrapper or straightforward port

---

## Memory & Threading Synchronization

### Linux (POSIX Mutex)
```c
#include <pthread.h>

static pthread_mutex_t vlock = PTHREAD_MUTEX_INITIALIZER;

void critical_section() {
    pthread_mutex_lock(&vlock);
    /* Update shared voice state */
    pthread_mutex_unlock(&vlock);
}
```

### Bare-Metal (Circle Spinlock)
```c
#include <circle/synchronize.h>

class CKernel {
private:
    CSpinLock m_VoiceLock;
    
public:
    void CriticalSection() {
        m_VoiceLock.Acquire();
        /* Update shared voice state */
        m_VoiceLock.Release();
    }
};
```

**Key Differences:**
- Linux: `pthread_mutex_t` is heavyweight (kernel futex)
- Bare-Metal: `CSpinLock` is lightweight (busy-wait on ARM)
- Linux: Can sleep waiting for lock
- Bare-Metal: Spins (but ISRs are brief anyway)
- **Action:** Direct 1:1 replacement, no behavior change needed

---

## Global Voice State

### Linux
```c
Voice g_voices[NUM_VOICES] = {};
int g_active_voice_count = 0;
```

### Bare-Metal (unchanged)
```c
Voice g_voices[NUM_VOICES] = {};
int g_active_voice_count = 0;
```

Both access via mutex/spinlock, so no changes needed.

---

## Render Loop Structure

### Linux (Push Model)
```c
int main() {
    snd_pcm_t *pcm;
    snd_pcm_open(&pcm, ...);
    
    while (running) {
        pthread_mutex_lock(&vlock);
        render_voices(outbuf, period);
        pthread_mutex_unlock(&vlock);
        
        snd_pcm_sframes_t frames = snd_pcm_writei(pcm, outbuf, period);
        if (frames < 0) snd_pcm_recover(pcm, frames, 0);
    }
    
    snd_pcm_close(pcm);
}
```

### Bare-Metal (Pull Model)
```c
class CSynthSound : public CPWMSoundBaseDevice {
    unsigned GetChunk(u32 *pBuffer, unsigned nChunkSize) override {
        m_pKernel->m_VoiceLock.Acquire();
        render_voices((int16_t *)pBuffer, nChunkSize / 4);
        m_pKernel->m_VoiceLock.Release();
        return nChunkSize;
    }
};

void CKernel::Run() {
    while (m_bRunning) {
        m_VoiceLock.Acquire();
        // Process MIDI events from queue
        while (m_MIDIQueue.GetCount() > 0) {
            u8 byte = m_MIDIQueue.Dequeue();
            ProcessMIDI(byte);
        }
        m_VoiceLock.Release();
    }
}
```

**Key Pattern Difference:**
- **Linux:** Main thread continuously renders (push)
- **Bare-metal:** PWM ISR requests chunks as needed (pull)
- **Action:** Decompose render loop into `GetChunk()` callback

---

## Summary of Code Replacements

| Component | Linux | Bare-Metal | Lines |
|-----------|-------|-----------|-------|
| Audio init | `snd_pcm_open()` | `new CSynthSound()` | 10 |
| Audio write | `snd_pcm_writei()` | `GetChunk()` callback | 20 |
| MIDI init | `pthread_create()` | USB callback registration | 5 |
| MIDI read | `read(fd, ...)` | USB interrupt queue | 10 |
| File I/O init | `opendir()` | `f_opendir()` | 5 |
| File read | `fread()` | `f_read()` | 5 |
| Locking | `pthread_mutex_*` | `CSpinLock::*` | 5 |

**Total API replacements: ~60 lines of wrapper code**

---

## Minimal Viable Port

Start with this subset to verify porting works:

1. **Core synth:** Extract all DSP to `synth_core.c` (no OS dependencies)
2. **Simple audio:** Implement GetChunk() callback with dummy synth
3. **File I/O:** Load sample bank at boot using FATFS
4. **MIDI stub:** Parse MIDI but ignore (to avoid USB blocker)
5. **Test:** Hear audio output with sine wave

Then add MIDI once USB issue is resolved.

---

## References

- **Circle Sound:** `circle-stdlib/libs/circle/device/pwmsoundbasedevice.h`
- **Circle FATFS:** `circle-stdlib/libs/circle/fs/fat/fatfs.h`
- **Circle Synchronize:** `circle-stdlib/libs/circle/include/circle/synchronize.h`
- **Circle USB MIDI:** `circle-stdlib/libs/circle/usb/usbmidihost.h`
