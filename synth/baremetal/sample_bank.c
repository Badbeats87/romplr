#include "sample_bank.h"
#include "../include/types.h"
#include <fatfs/ff.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Sorted directory list for lazy loading ── */
static char g_dir_names[MAX_INSTRUMENTS][64];
static int  g_num_dirs = 0;
static int  g_next_pending = 1;  /* index 0 loaded in init */

/* ── Poll callback — called during SD reads so MIDI stays responsive ── */
static void (*g_poll_cb)(void) = NULL;

void sample_bank_set_poll_cb(void (*cb)(void))
{
    g_poll_cb = cb;
}

/* ── Sample pool — heap-allocated at init to avoid BSS overflow ── */
/* Circle's KERNEL_MAX_SIZE (4MB) can't hold 32MB of BSS.          */
/* Heap is after the coherent region (~11MB+), plenty of room.     */
static int16_t *g_sample_pool = NULL;

int16_t *sample_bank_pool(void)
{
    return g_sample_pool;
}

/* ── WAV file parser ─────────────────────────────────────────
 * Supports: RIFF/WAVE, PCM format (1), 16-bit, mono, 44100 Hz
 * Returns number of samples loaded, or 0 on error.
 */
static uint32_t load_wav(const char *path, int16_t *dest, uint32_t max_samples)
{
    FIL fp;
    UINT br;

    FRESULT wfr = f_open(&fp, path, FA_READ);
    if (wfr != FR_OK) {
        return 0;
    }

    /* Read RIFF header */
    uint8_t hdr[44];
    if (f_read(&fp, hdr, 44, &br) != FR_OK || br < 44) {
        f_close(&fp);
        return 0;
    }

    /* Verify RIFF/WAVE */
    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F' ||
        hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E') {
        f_close(&fp);
        return 0;
    }

    /* Parse fmt chunk — expect it at offset 12 */
    /* fmt chunk: bytes 12-15 = "fmt ", 16-19 = chunk size,
     * 20-21 = format (1=PCM), 22-23 = channels,
     * 24-27 = sample rate, 28-31 = byte rate,
     * 32-33 = block align, 34-35 = bits per sample */
    uint16_t format      = hdr[20] | (hdr[21] << 8);
    uint16_t channels    = hdr[22] | (hdr[23] << 8);
    uint32_t sample_rate = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    uint16_t bits        = hdr[34] | (hdr[35] << 8);

    (void)sample_rate;  /* We trust the user provides 44100Hz files */

    if (format != 1 || bits != 16) {
        f_close(&fp);
        return 0;
    }

    /* Find data chunk — it may not be at offset 36 if there are
     * extra fmt bytes or other chunks. Scan for "data". */
    uint32_t fmt_size = hdr[16] | (hdr[17] << 8) | (hdr[18] << 16) | (hdr[19] << 24);
    uint32_t data_offset = 12 + 8 + fmt_size;  /* after RIFF header + fmt chunk */

    /* Seek to after fmt chunk */
    f_lseek(&fp, data_offset);

    /* Scan for "data" chunk */
    uint8_t chunk_hdr[8];
    uint32_t data_size = 0;
    for (int attempt = 0; attempt < 10; attempt++) {
        if (f_read(&fp, chunk_hdr, 8, &br) != FR_OK || br < 8)
            break;
        data_size = chunk_hdr[4] | (chunk_hdr[5] << 8) |
                    (chunk_hdr[6] << 16) | (chunk_hdr[7] << 24);
        if (chunk_hdr[0] == 'd' && chunk_hdr[1] == 'a' &&
            chunk_hdr[2] == 't' && chunk_hdr[3] == 'a')
            break;
        /* Skip this chunk */
        f_lseek(&fp, f_tell(&fp) + data_size);
        data_size = 0;
    }

    if (data_size == 0) {
        f_close(&fp);
        return 0;
    }

    /* Calculate samples */
    uint32_t bytes_per_sample = (channels * bits) / 8;
    uint32_t total_frames = data_size / bytes_per_sample;
    if (total_frames > max_samples)
        total_frames = max_samples;

    if (channels == 1) {
        /* Mono: read in 8KB chunks, poll MIDI between chunks */
        uint32_t loaded = 0;
        while (loaded < total_frames) {
            uint32_t chunk = total_frames - loaded;
            if (chunk > 4096) chunk = 4096;  /* 4096 samples = 8KB */
            if (f_read(&fp, &dest[loaded], chunk * 2, &br) != FR_OK || br == 0)
                break;
            loaded += br / 2;
            if (g_poll_cb) g_poll_cb();
        }
        total_frames = loaded;
    } else {
        /* Stereo: read and mix to mono */
        /* Read in chunks to conserve stack */
        uint32_t loaded = 0;
        int16_t buf[512];  /* 256 stereo frames */
        while (loaded < total_frames) {
            uint32_t chunk = total_frames - loaded;
            if (chunk > 256) chunk = 256;
            uint32_t read_bytes = chunk * channels * 2;
            if (f_read(&fp, buf, read_bytes, &br) != FR_OK || br == 0)
                break;
            uint32_t frames_read = br / (channels * 2);
            for (uint32_t i = 0; i < frames_read; i++) {
                int32_t l = buf[i * channels];
                int32_t r = buf[i * channels + 1];
                dest[loaded + i] = (int16_t)((l + r) / 2);
            }
            loaded += frames_read;
            if (g_poll_cb) g_poll_cb();
        }
        total_frames = loaded;
    }

    f_close(&fp);
    return total_frames;
}

/* ── Instrument config parser ─────────────────────────────── */

/* Simple integer parser */
static int parse_int(const char *s)
{
    int result = 0;
    int sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return sign * result;
}

/* Skip whitespace and return pointer to next non-space */
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Copy string until delimiter or end, return chars copied */
static int copy_until(const char *src, char *dst, int max, char delim)
{
    int i = 0;
    while (i < max - 1 && src[i] && src[i] != delim && src[i] != '\n' && src[i] != '\r') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

/* Parse one zone line: "zone=C2.wav, 36, 41, 36, 0, 0, 0, 100" */
static int parse_zone_line(const char *line, SampleBank *sb,
                           Instrument *inst, const char *dir_path)
{
    if (inst->num_zones >= MAX_ZONES) return -1;

    /* Skip "zone=" */
    const char *p = line + 5;
    p = skip_spaces(p);

    /* Parse filename */
    char filename[64];
    int n = copy_until(p, filename, sizeof(filename), ',');
    p += n;
    if (*p == ',') p++;

    /* Parse: low_key, high_key, root_key, loop_start, loop_end, fine_tune, volume */
    int vals[7] = {0};
    for (int i = 0; i < 7; i++) {
        p = skip_spaces(p);
        vals[i] = parse_int(p);
        while (*p && *p != ',' && *p != '\n' && *p != '\r') p++;
        if (*p == ',') p++;
    }

    /* Build full path */
    char path[128];
    int len = 0;
    const char *dp = dir_path;
    while (*dp && len < 100) path[len++] = *dp++;
    if (len > 0 && path[len-1] != '/') path[len++] = '/';
    const char *fn = filename;
    while (*fn == ' ') fn++;  /* trim leading space */
    while (*fn && len < 126) path[len++] = *fn++;
    path[len] = '\0';

#ifdef STREAM_FROM_SD
    /* Streaming mode: parse WAV header for metadata, don't load PCM */
    {
        FIL wfp;
        UINT wbr;
        if (f_open(&wfp, path, FA_READ) != FR_OK)
            return -1;

        uint8_t hdr[44];
        if (f_read(&wfp, hdr, 44, &wbr) != FR_OK || wbr < 44) {
            f_close(&wfp);
            return -1;
        }

        /* Verify RIFF/WAVE */
        if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F' ||
            hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E') {
            f_close(&wfp);
            return -1;
        }

        uint16_t wfmt  = hdr[20] | (hdr[21] << 8);
        uint16_t wchan = hdr[22] | (hdr[23] << 8);
        uint16_t wbits = hdr[34] | (hdr[35] << 8);
        if (wfmt != 1 || wbits != 16) {
            f_close(&wfp);
            return -1;
        }

        /* Find data chunk */
        uint32_t fmt_sz = hdr[16] | (hdr[17] << 8) | (hdr[18] << 16) | (hdr[19] << 24);
        uint32_t scan_ofs = 12 + 8 + fmt_sz;
        f_lseek(&wfp, scan_ofs);

        uint8_t chunk_hdr[8];
        uint32_t data_size = 0;
        uint32_t data_ofs = 0;
        for (int attempt = 0; attempt < 10; attempt++) {
            if (f_read(&wfp, chunk_hdr, 8, &wbr) != FR_OK || wbr < 8)
                break;
            data_size = chunk_hdr[4] | (chunk_hdr[5] << 8) |
                        (chunk_hdr[6] << 16) | (chunk_hdr[7] << 24);
            if (chunk_hdr[0] == 'd' && chunk_hdr[1] == 'a' &&
                chunk_hdr[2] == 't' && chunk_hdr[3] == 'a') {
                data_ofs = f_tell(&wfp);
                break;
            }
            f_lseek(&wfp, f_tell(&wfp) + data_size);
            data_size = 0;
        }

        f_close(&wfp);

        if (data_size == 0) return -1;

        uint32_t bytes_per_sample = (wchan * wbits) / 8;
        uint32_t total_frames = data_size / bytes_per_sample;

        SampleZone *z = &inst->zones[inst->num_zones];
        z->pcm        = NULL;
        z->length     = total_frames;
        z->low_key    = (uint8_t)vals[0];
        z->high_key   = (uint8_t)vals[1];
        z->root_key   = (uint8_t)vals[2];
        z->loop_start = (uint32_t)vals[3];
        z->loop_end   = (uint32_t)vals[4];
        z->fine_tune  = (int8_t)vals[5];
        z->volume     = (uint8_t)vals[6];

        /* Store streaming metadata */
        strncpy(z->wav_path, path, sizeof(z->wav_path) - 1);
        z->wav_path[sizeof(z->wav_path) - 1] = '\0';
        z->wav_data_offset = data_ofs;

        inst->num_zones++;
        return 0;
    }
#else
    /* Normal mode: load WAV into pool */
    uint32_t avail = SAMPLE_POOL_SAMPLES - sb->pool_used;
    uint32_t samples = load_wav(path, &g_sample_pool[sb->pool_used], avail);
    if (samples == 0) return -1;

    SampleZone *z = &inst->zones[inst->num_zones];
    z->pcm        = &g_sample_pool[sb->pool_used];
    z->length     = samples;
    z->low_key    = (uint8_t)vals[0];
    z->high_key   = (uint8_t)vals[1];
    z->root_key   = (uint8_t)vals[2];
    z->loop_start = (uint32_t)vals[3];
    z->loop_end   = (uint32_t)vals[4];
    z->fine_tune  = (int8_t)vals[5];
    z->volume     = (uint8_t)vals[6];

    sb->pool_used += samples;
    inst->num_zones++;
    return 0;
#endif
}

int sample_bank_load_instrument(SampleBank *sb, const char *dir_path)
{
    if (sb->num_instruments >= MAX_INSTRUMENTS) return -1;

    Instrument *inst = &sb->instruments[sb->num_instruments];
    memset(inst, 0, sizeof(Instrument));

    /* Default values */
    inst->filter_cutoff    = 100;
    inst->filter_resonance = 20;
    inst->reverb_level     = 50;
    inst->chorus_level     = 0;
    inst->attack           = 5;
    inst->release          = 60;
    strcpy(inst->name, "Unnamed");

    /* Build cfg path */
    char cfg_path[128];
    int len = 0;
    const char *dp = dir_path;
    while (*dp && len < 100) cfg_path[len++] = *dp++;
    if (len > 0 && cfg_path[len-1] != '/') cfg_path[len++] = '/';
    const char *cfg = "instrument.cfg";
    while (*cfg && len < 126) cfg_path[len++] = *cfg++;
    cfg_path[len] = '\0';

    /* Read and parse config file line by line */
    FIL fp;
    FRESULT cfr = f_open(&fp, cfg_path, FA_READ);
    if (cfr != FR_OK) {
        return -1;
    }

    char line[256];
    UINT br;
    int line_pos = 0;

    while (1) {
        uint8_t ch;
        if (f_read(&fp, &ch, 1, &br) != FR_OK || br == 0) {
            /* Process last line if any */
            if (line_pos > 0) {
                line[line_pos] = '\0';
                goto process_line;
            }
            break;
        }

        if (ch == '\n' || ch == '\r') {
            if (line_pos == 0) continue;
            line[line_pos] = '\0';
process_line:
            /* Skip comments and empty lines */
            if (line[0] == '#' || line[0] == '\0') {
                line_pos = 0;
                continue;
            }

            /* Parse key=value */
            if (strncmp(line, "name=", 5) == 0) {
                copy_until(line + 5, inst->name, sizeof(inst->name), '\n');
            } else if (strncmp(line, "zone=", 5) == 0) {
                parse_zone_line(line, sb, inst, dir_path);
            } else if (strncmp(line, "filter_cutoff=", 14) == 0) {
                inst->filter_cutoff = (uint8_t)CLAMP(parse_int(line + 14), 0, 127);
            } else if (strncmp(line, "filter_resonance=", 17) == 0) {
                inst->filter_resonance = (uint8_t)CLAMP(parse_int(line + 17), 0, 127);
            } else if (strncmp(line, "reverb_level=", 13) == 0) {
                inst->reverb_level = (uint8_t)CLAMP(parse_int(line + 13), 0, 127);
            } else if (strncmp(line, "chorus_level=", 13) == 0) {
                inst->chorus_level = (uint8_t)CLAMP(parse_int(line + 13), 0, 127);
            } else if (strncmp(line, "attack=", 7) == 0) {
                inst->attack = (uint8_t)CLAMP(parse_int(line + 7), 0, 127);
            } else if (strncmp(line, "release=", 8) == 0) {
                inst->release = (uint8_t)CLAMP(parse_int(line + 8), 0, 127);
            }

            line_pos = 0;
            if (br == 0) break;  /* was last line */
            continue;
        }

        if (line_pos < (int)sizeof(line) - 1)
            line[line_pos++] = (char)ch;
    }

    f_close(&fp);

    if (inst->num_zones == 0) return -1;

    int idx = sb->num_instruments;
    /* Memory barrier: ensure all instrument data is visible before
     * the audio thread sees the incremented count.
     * Not needed on single-core platforms (Teensy Cortex-M7). */
#ifndef PLATFORM_SINGLE_CORE
    __sync_synchronize();
#endif
    sb->num_instruments++;
    return idx;
}

/* qsort comparator for 64-byte name entries */
static int dir_name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

void sample_bank_init(SampleBank *sb)
{
    memset(sb, 0, sizeof(SampleBank));
    g_num_dirs = 0;
    g_next_pending = 1;

#ifndef STREAM_FROM_SD
    /* Allocate sample pool from high memory (above 1GB).
     * Default malloc uses low heap (~448MB), too small for all instruments.
     * alloc_high_mem() uses Circle's HEAP_HIGH (1-3GB range, 2GB available). */
    if (!g_sample_pool) {
        extern void *alloc_high_mem(size_t size);
        g_sample_pool = (int16_t *)alloc_high_mem(SAMPLE_POOL_SAMPLES * sizeof(int16_t));
        if (!g_sample_pool) {
            /* Fall back to regular malloc (won't fit all instruments) */
            g_sample_pool = (int16_t *)malloc(SAMPLE_POOL_SAMPLES * sizeof(int16_t));
        }
        if (!g_sample_pool) {
            return;
        }
    }
#endif

    /* Scan SD:/instruments/ for subdirectories */
    {
        DIR dir;
        FILINFO fno;

        FRESULT fr = f_opendir(&dir, "SD:/instruments");
        if (fr != FR_OK) {
            return;
        }

        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
            if (!(fno.fattrib & AM_DIR)) continue;
            if (fno.fname[0] == '.') continue;
            if (g_num_dirs >= MAX_INSTRUMENTS) break;

            size_t nlen = strlen(fno.fname);
            if (nlen > 63) nlen = 63;
            memcpy(g_dir_names[g_num_dirs], fno.fname, nlen);
            g_dir_names[g_num_dirs][nlen] = '\0';
            g_num_dirs++;

            if (g_poll_cb) g_poll_cb();
        }
        f_closedir(&dir);

        /* Sort by name — zero-padded numbers sort correctly with strcmp */
        qsort(g_dir_names, g_num_dirs, 64, dir_name_cmp);
    }

    /* Load first instrument synchronously so audio can start immediately */
    if (g_num_dirs > 0) {
        char path[128];
        snprintf(path, sizeof(path), "SD:/instruments/%s", g_dir_names[0]);
        sample_bank_load_instrument(sb, path);
    }
}

int sample_bank_load_next_pending(SampleBank *sb)
{
    if (g_next_pending >= g_num_dirs) return 0;

    char path[128];
    snprintf(path, sizeof(path), "SD:/instruments/%s", g_dir_names[g_next_pending]);
    sample_bank_load_instrument(sb, path);
    g_next_pending++;

    return (g_next_pending < g_num_dirs) ? 1 : 0;
}

int sample_bank_pending_count(void)
{
    int remaining = g_num_dirs - g_next_pending;
    return (remaining > 0) ? remaining : 0;
}

const SampleZone *sample_bank_find_zone(const SampleBank *sb,
                                         int inst_idx, uint8_t note)
{
    if (inst_idx < 0 || inst_idx >= sb->num_instruments) return 0;

    const Instrument *inst = &sb->instruments[inst_idx];

    /* Find zone whose range covers this note */
    for (int i = 0; i < inst->num_zones; i++) {
        const SampleZone *z = &inst->zones[i];
        if (note >= z->low_key && note <= z->high_key)
            return z;
    }

    /* Fallback: find nearest zone */
    int best = 0;
    int best_dist = 256;
    for (int i = 0; i < inst->num_zones; i++) {
        int mid = (inst->zones[i].low_key + inst->zones[i].high_key) / 2;
        int dist = ABS((int)note - mid);
        if (dist < best_dist) {
            best = i;
            best_dist = dist;
        }
    }
    return &inst->zones[best];
}

int sample_bank_count(const SampleBank *sb)
{
    return sb->num_instruments;
}
