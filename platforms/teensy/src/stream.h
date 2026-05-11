#ifndef STREAM_H
#define STREAM_H

/*
 * SD Streaming Engine for Teensy 4.1 Rompler
 *
 * Each playing voice gets a read-ahead cache (4096 samples = 8 KB)
 * kept filled by the main loop.  The audio ISR only reads from cache,
 * never touches SD.  A separate loop cache (1028 samples) pre-loads
 * the loop-start region for crossfade blending.
 *
 * ISR safety: The audio ISR calls only stream_fetch_4() which reads
 * from cache memory — no SD access.  All file I/O (open, read, close)
 * happens in the main loop via stream_refill_all().
 *
 * Memory: MAX_VOICES * ~10 KB total in DTCM.
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef MAX_VOICES
#define MAX_VOICES 8
#endif

#define STREAM_CACHE_SAMPLES   4096
#define STREAM_LOOP_CACHE_SAMPLES 1028

#ifdef __cplusplus
extern "C" {
#endif

/* Include rompler types for stream_refill_all parameter */
#include "rompler.h"

typedef struct {
    int16_t  cache[STREAM_CACHE_SAMPLES];       /* main sequential read-ahead */
    uint32_t cache_start;                        /* file sample index of cache[0] */
    uint32_t cache_valid;                        /* valid samples in cache */

    int16_t  loop_cache[STREAM_LOOP_CACHE_SAMPLES]; /* loop-start region for crossfade */
    uint32_t loop_cache_start;                   /* file sample index of loop_cache[0] */
    uint32_t loop_cache_valid;                   /* valid samples in loop cache */
    bool     loop_cache_loaded;

    int      file_handle;                        /* >= 0 if file open, -1 = closed */
    uint32_t wav_data_offset;                    /* byte offset of PCM data in WAV file */
    uint32_t total_samples;                      /* total PCM samples in file */
    uint32_t loop_start;                         /* loop start in samples */
    char     path[128];                          /* WAV file path */
    bool     active;                             /* true = slot in use by a playing voice */

    /* Deferred open request (set by ISR via stream_request_open,
     * processed by main loop in stream_refill_all) */
    volatile bool pending_open;
    char     pending_path[128];
    uint32_t pending_data_offset;
    uint32_t pending_total_samples;
    uint32_t pending_loop_start;
} StreamVoice;

/* Initialise all stream voices to inactive */
void stream_init(void);

/* ISR-safe: request a stream to be opened on the given voice slot.
 * Does NOT touch SD — just stores metadata for the main loop to process.
 * Any previously open stream on this slot will be closed first. */
void stream_request_open(int voice_idx, const char *wav_path,
                         uint32_t data_offset, uint32_t total_samples,
                         uint32_t loop_start);

/* ISR-safe: fetch 4 consecutive samples starting at sample_idx for
 * Hermite interpolation.  Reads from cache, returns zeros on underrun.
 * out[] must have room for 4 int16_t values. */
void stream_fetch_4(int voice_idx, uint32_t sample_idx,
                    uint32_t length, int16_t out[4]);

/* Main-loop: process pending opens, refill caches, close inactive streams.
 * This is the ONLY function that performs SD I/O. */
void stream_refill_all(Rompler *r);

/* Get total underrun count (for diagnostics) */
uint32_t stream_underrun_count(void);

#ifdef __cplusplus
}
#endif

#endif /* STREAM_H */
