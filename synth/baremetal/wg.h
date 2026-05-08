#ifndef WG_H
#define WG_H

#include "../include/types.h"
#include "patch.h"

/* Wave Generator: sample-based playback from loaded WAV files
 *
 * Uses sample_bank to find the correct zone for each note.
 * Playback uses Q16 fixed-point position with linear interpolation.
 * Samples are native 44100 Hz. Output is 48 kHz, so original-pitch
 * playback advances by 44100/48000 samples per output frame.
 */

/* MIDI note → Q24 phase increment (used for pitch ratio calculation) */
extern uint32_t g_note_phase_inc[128];

void wg_tables_init(void);

/* PCM playback rate at original pitch, Q16. */
#define PCM_RATE_Q16    ((uint32_t)(((uint64_t)PCM_SAMPLE_RATE << 16) / SYNTH_SAMPLE_RATE))

typedef struct {
    const int16_t *pcm;     /* pointer to zone's PCM data start */
    uint32_t pos_int;       /* integer sample position */
    uint32_t pos_frac;      /* Q16 fractional position */
    uint32_t base_rate;     /* Q16 base playback rate (before LFO) */
    uint32_t length;        /* zone sample count */
    uint32_t loop_start;    /* loop start offset (0 = no loop) */
    uint32_t loop_end;      /* loop end offset (0 = use length) */
    uint8_t  finished;      /* 1 if sample reached end (no loop) */
    uint8_t  pad[3];
    const WGParams *params;
} WGState;

void    wg_note_on (WGState *w, const WGParams *p, uint8_t note,
                    int32_t lfo_pitch_q15, int8_t pitch_bend_semi);
void    wg_note_off(WGState *w);
int32_t wg_tick    (WGState *w, int32_t lfo_pitch_mod_q15);  /* returns Q15 */

#endif
