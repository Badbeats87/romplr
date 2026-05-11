/*
 * SD Streaming Engine for Teensy 4.1 Rompler
 *
 * Streams WAV samples from SD card on demand.  The audio ISR reads
 * from per-voice caches; the main loop keeps them filled from SD.
 *
 * ISR safety: stream_fetch_4() and stream_request_open() are safe to
 * call from interrupt context.  All actual SD I/O (file open, read,
 * seek, close) happens only in stream_refill_all(), called from loop().
 */
#include <Arduino.h>
#include "stream.h"
#include "fatfs/ff.h"
#include <string.h>

extern "C" {
#include "voice.h"
#include "tone.h"
#include "wg.h"
}

/* ── Per-voice state — stays in DTCM for fast ISR cache reads ── */
static StreamVoice g_streams[MAX_VOICES];
static uint32_t    g_underruns = 0;

/* ── Per-voice open file handles ─────────────────────────────── */
static FIL g_stream_files[MAX_VOICES];

/* ── Helpers ─────────────────────────────────────────────────── */

/* Close the file for a stream slot (main loop only) */
static void close_stream(int idx)
{
    StreamVoice *sv = &g_streams[idx];
    if (sv->file_handle >= 0) {
        f_close(&g_stream_files[idx]);
        sv->file_handle = -1;
    }
    sv->active = false;
    sv->cache_valid = 0;
    sv->loop_cache_valid = 0;
    sv->loop_cache_loaded = false;
}

/* Read PCM samples from the WAV file at a given sample offset.
 * Returns number of samples actually read. */
static uint32_t read_samples(int voice_idx, uint32_t sample_offset,
                             int16_t *dest, uint32_t count)
{
    StreamVoice *sv = &g_streams[voice_idx];
    if (sv->file_handle < 0) return 0;
    if (sample_offset >= sv->total_samples) return 0;

    /* Clamp to file boundary */
    if (sample_offset + count > sv->total_samples)
        count = sv->total_samples - sample_offset;

    /* Seek to byte position: data_offset + sample_offset * 2 (16-bit mono) */
    uint32_t byte_pos = sv->wav_data_offset + sample_offset * 2;
    FIL *fp = &g_stream_files[voice_idx];
    if (f_lseek(fp, byte_pos) != FR_OK) return 0;

    UINT br = 0;
    if (f_read(fp, dest, count * 2, &br) != FR_OK) return 0;

    return br / 2;
}

/* Open a stream slot: open file, fill initial cache (main loop only) */
static int open_stream(int voice_idx)
{
    StreamVoice *sv = &g_streams[voice_idx];

    /* Close any existing stream on this slot */
    close_stream(voice_idx);

    /* Open the WAV file */
    FIL *fp = &g_stream_files[voice_idx];
    if (f_open(fp, sv->path, FA_READ) != FR_OK)
        return -1;

    sv->file_handle = voice_idx;  /* marker that file is open */
    sv->active = true;

    /* Pre-fill the cache from sample 0 */
    sv->cache_start = 0;
    sv->cache_valid = read_samples(voice_idx, 0, sv->cache,
                                   STREAM_CACHE_SAMPLES);

    /* Pre-load loop cache if there's a loop point */
    sv->loop_cache_loaded = false;
    sv->loop_cache_start  = 0;
    sv->loop_cache_valid  = 0;
    if (sv->loop_start > 0 && sv->loop_start < sv->total_samples) {
        sv->loop_cache_start = sv->loop_start;
        sv->loop_cache_valid = read_samples(voice_idx, sv->loop_start,
                                            sv->loop_cache,
                                            STREAM_LOOP_CACHE_SAMPLES);
        sv->loop_cache_loaded = (sv->loop_cache_valid > 0);
    }

    return 0;
}

/* ── Public API ──────────────────────────────────────────────── */

extern "C" void stream_init(void)
{
    memset(g_streams, 0, sizeof(g_streams));
    for (int i = 0; i < MAX_VOICES; i++) {
        g_streams[i].file_handle = -1;
        g_streams[i].active = false;
        g_streams[i].pending_open = false;
    }
    g_underruns = 0;
}

extern "C" void stream_request_open(int voice_idx, const char *wav_path,
                                    uint32_t data_offset,
                                    uint32_t total_samples,
                                    uint32_t loop_start)
{
    if (voice_idx < 0 || voice_idx >= MAX_VOICES) return;

    StreamVoice *sv = &g_streams[voice_idx];

    /* Store parameters for deferred open — NO SD I/O here */
    strncpy(sv->pending_path, wav_path, sizeof(sv->pending_path) - 1);
    sv->pending_path[sizeof(sv->pending_path) - 1] = '\0';
    sv->pending_data_offset   = data_offset;
    sv->pending_total_samples = total_samples;
    sv->pending_loop_start    = loop_start;

    /* Mark active immediately so fetch_4 doesn't return zeros for
     * a cache that may still have valid data from a previous note */
    sv->active = true;

    /* Signal the main loop to process this open request.
     * volatile write — ensures the main loop sees it. */
    sv->pending_open = true;
}

extern "C" void stream_fetch_4(int voice_idx, uint32_t sample_idx,
                               uint32_t length, int16_t out[4])
{
    /* Default to silence */
    out[0] = out[1] = out[2] = out[3] = 0;

    if (voice_idx < 0 || voice_idx >= MAX_VOICES) return;
    const StreamVoice *sv = &g_streams[voice_idx];
    if (!sv->active) return;

    /* We need samples at: sample_idx-1, sample_idx, sample_idx+1, sample_idx+2
     * (for Hermite: sm1, s0, s1, s2) */
    for (int i = 0; i < 4; i++) {
        int32_t req = (int32_t)sample_idx - 1 + i;

        /* Boundary: clamp to valid range */
        if (req < 0) {
            out[i] = 0;
            continue;
        }
        uint32_t r = (uint32_t)req;
        if (r >= length) {
            out[i] = (i > 0) ? out[i - 1] : 0;
            continue;
        }

        /* Try main cache first */
        if (r >= sv->cache_start &&
            r < sv->cache_start + sv->cache_valid) {
            out[i] = sv->cache[r - sv->cache_start];
            continue;
        }

        /* Try loop cache */
        if (sv->loop_cache_loaded &&
            r >= sv->loop_cache_start &&
            r < sv->loop_cache_start + sv->loop_cache_valid) {
            out[i] = sv->loop_cache[r - sv->loop_cache_start];
            continue;
        }

        /* Cache miss — count underrun, output zero */
        g_underruns++;
        out[i] = 0;
    }
}

extern "C" void stream_refill_all(Rompler *r)
{
    VoiceAllocator *va = &r->voices;

    /* ── Process pending opens and close inactive streams ──── */
    for (int v = 0; v < MAX_VOICES; v++) {
        StreamVoice *sv = &g_streams[v];

        /* Process deferred open requests */
        if (sv->pending_open) {
            sv->pending_open = false;

            /* Copy pending params into main fields */
            strncpy(sv->path, sv->pending_path, sizeof(sv->path) - 1);
            sv->path[sizeof(sv->path) - 1] = '\0';
            sv->wav_data_offset = sv->pending_data_offset;
            sv->total_samples   = sv->pending_total_samples;
            sv->loop_start      = sv->pending_loop_start;

            /* Actually open the file and fill cache */
            open_stream(v);
            continue;  /* cache is fresh, no need to refill */
        }

        /* Close streams for voices that are no longer active */
        if (sv->active && sv->file_handle >= 0) {
            Voice *voice = &va->voices[v];
            if (!voice->active) {
                close_stream(v);
                continue;
            }
        }

        /* ── Refill cache for active streams ──────────────── */
        if (!sv->active || sv->file_handle < 0) continue;

        Voice *voice = &va->voices[v];
        if (!voice->active) continue;

        /* Check each tone's WG for stream position */
        for (int t = 0; t < NUM_TONES; t++) {
            ToneState *tone = &voice->tones[t];
            if (!tone->active) continue;

            WGState *w = &tone->wg;
            if (w->stream_idx != v) continue;

            uint32_t pos = w->pos_int;

            /* ── Refill main cache ────────────────────── */
            uint32_t cache_end = sv->cache_start + sv->cache_valid;
            bool need_refill = false;

            if (sv->cache_valid == 0) {
                need_refill = true;
            } else if (pos < sv->cache_start) {
                /* Position jumped backward (loop wrap) */
                need_refill = true;
            } else if (pos >= sv->cache_start + STREAM_CACHE_SAMPLES / 4) {
                /* Past first quarter — shift forward */
                need_refill = true;
            } else if (pos + STREAM_CACHE_SAMPLES > cache_end &&
                       cache_end < sv->total_samples) {
                need_refill = true;
            }

            if (need_refill) {
                uint32_t new_start = (pos > 64) ? pos - 64 : 0;
                sv->cache_start = new_start;
                sv->cache_valid = read_samples(v, new_start,
                                               sv->cache,
                                               STREAM_CACHE_SAMPLES);
            }

            /* ── Load loop cache if needed ────────────── */
            if (sv->loop_start > 0 && !sv->loop_cache_loaded) {
                sv->loop_cache_start = sv->loop_start;
                sv->loop_cache_valid = read_samples(v,
                                                    sv->loop_start,
                                                    sv->loop_cache,
                                                    STREAM_LOOP_CACHE_SAMPLES);
                sv->loop_cache_loaded = (sv->loop_cache_valid > 0);
            }
        }
    }
}

extern "C" uint32_t stream_underrun_count(void)
{
    return g_underruns;
}
