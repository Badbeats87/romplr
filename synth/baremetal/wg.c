#include "wg.h"
#include "sample_bank.h"
#include "../include/types.h"

uint32_t g_note_phase_inc[128];

/* Quarter-cosine table for equal-power crossfade: cos(i * π/2 / 1024) in Q15 */
static int16_t g_cos_q15[1025];

/* Semitone ratios in Q16 */
static const uint32_t semi_ratio_q16[12] = {
    65536,  /* 2^( 0/12) = 1.0000 */
    69433,  /* 2^( 1/12) = 1.0595 */
    73562,  /* 2^( 2/12) = 1.1225 */
    77936,  /* 2^( 3/12) = 1.1892 */
    82570,  /* 2^( 4/12) = 1.2599 */
    87480,  /* 2^( 5/12) = 1.3348 */
    92682,  /* 2^( 6/12) = 1.4142 */
    98193,  /* 2^( 7/12) = 1.4983 */
    104032, /* 2^( 8/12) = 1.5874 */
    110218, /* 2^( 9/12) = 1.6818 */
    116771, /* 2^(10/12) = 1.7818 */
    123714, /* 2^(11/12) = 1.8877 */
};

void wg_tables_init(void)
{
    uint64_t a4_inc = (uint64_t)440 * PHASE_ONE / SYNTH_SAMPLE_RATE;

    for (int note = 0; note < 128; note++) {
        int diff = note - 69;
        uint64_t inc = a4_inc;

        if (diff >= 0) {
            int octaves = diff / 12;
            int semis   = diff % 12;
            inc = (a4_inc * semi_ratio_q16[semis] >> 16) << octaves;
        } else {
            int diff_pos = -diff;
            int octaves  = diff_pos / 12;
            int semis    = diff_pos % 12;
            inc = (a4_inc << 16) / semi_ratio_q16[semis];
            inc >>= octaves;
        }

        g_note_phase_inc[note] = (uint32_t)CLAMP((int64_t)inc, 1, 0xFFFFFF);
    }

    /* Precompute quarter-cosine table using Bhaskara I approximation:
     * cos(x) ≈ (π² - 4x²) / (π² + x²).
     * Simplified: cos(i*π/2/1024) = (4194304 - 4*i²) / (4194304 + i²) */
    for (int i = 0; i <= 1024; i++) {
        int64_t u2 = (int64_t)i * i;
        int64_t num = 4194304LL - 4 * u2;
        int64_t den = 4194304LL + u2;
        g_cos_q15[i] = (int16_t)CLAMP((int32_t)(num * 32767 / den), 0, 32767);
    }
}

/* Global sample bank pointer — set by rompler_init() */
static const SampleBank *g_sample_bank = 0;

void wg_set_sample_bank(const SampleBank *sb)
{
    g_sample_bank = sb;
}

void wg_note_on(WGState *w, const WGParams *p, uint8_t note,
                int32_t lfo_pitch_q15, int8_t pitch_bend_semi)
{
    w->params = p;
    w->pos_int  = 0;
    w->pos_frac = 0;
    w->finished = 0;

    /* Find zone from sample bank */
    if (!g_sample_bank) {
        w->finished = 1;
        return;
    }

    const SampleZone *zone = sample_bank_find_zone(g_sample_bank,
                                                    (int)p->inst_idx, note);
    if (!zone || !zone->pcm) {
        w->finished = 1;
        return;
    }

    /* Set up PCM pointers */
    w->pcm        = zone->pcm;
    w->length     = zone->length;
    w->loop_start = zone->loop_start;
    w->loop_end   = (zone->loop_end > 0 && zone->loop_end <= zone->length)
                  ? zone->loop_end : zone->length;

    /* Compute playback rate (Q16 fixed-point)
     *
     * Samples are native 44100 Hz and output is 48 kHz, so base rate is
     * PCM_SAMPLE_RATE / SYNTH_SAMPLE_RATE in Q16.
     * Transpose by (effective_note - root_key) semitones:
     *   rate = PCM_RATE_Q16 × phase_inc[eff_note] / phase_inc[root_key]
     */
    int32_t eff_note = (int32_t)note
                     + (int32_t)p->pitch_coarse
                     + (int32_t)pitch_bend_semi;
    eff_note = CLAMP(eff_note, 0, 127);

    int32_t root = CLAMP((int32_t)zone->root_key, 0, 127);
    uint64_t rate = (uint64_t)PCM_RATE_Q16 * g_note_phase_inc[eff_note];
    rate /= g_note_phase_inc[root];

    /* Fine tune from zone + patch */
    int32_t fine = (int32_t)p->pitch_fine + (int32_t)zone->fine_tune;
    if (fine != 0) {
        int64_t adj = (int64_t)rate * fine / 1731;
        rate = (uint64_t)((int64_t)rate + adj);
    }

    w->base_rate = (uint32_t)CLAMP((int64_t)rate, 1, 0x7FFFFF);

    (void)lfo_pitch_q15;
}

void wg_note_off(WGState *w)
{
    (void)w;  /* WG continues until TVA envelope reaches 0 */
}

/* 4-point Hermite cubic interpolation — ~10-15dB less aliasing than linear */
static inline int32_t hermite_interp(const int16_t *pcm, uint32_t idx,
                                     uint32_t frac, uint32_t length)
{
    int32_t s0  = (int32_t)pcm[idx];
    int32_t sm1 = (idx > 0)          ? (int32_t)pcm[idx - 1] : s0;
    int32_t s1  = (idx + 1 < length) ? (int32_t)pcm[idx + 1] : s0;
    int32_t s2  = (idx + 2 < length) ? (int32_t)pcm[idx + 2] : s1;

    /* Coefficients × 2 to avoid /2 rounding loss */
    int32_t d0 = 2 * s0;
    int32_t d1 = s1 - sm1;
    int32_t d2 = 2 * sm1 - 5 * s0 + 4 * s1 - s2;
    int32_t d3 = (s2 - sm1) + 3 * (s0 - s1);

    /* Horner's method with Q16 fraction, result × 2 */
    int32_t t = (int32_t)(frac & 0xFFFF);
    int64_t r = (int64_t)d3 * t;
    r = ((r >> 16) + d2) * t;
    r = ((r >> 16) + d1) * t;
    return (int32_t)((r >> 16) + d0) >> 1;
}

int32_t wg_tick(WGState *w, int32_t lfo_pitch_mod_q15)
{
    if (w->finished) return 0;

    /* Apply LFO pitch modulation to rate */
    uint32_t rate = w->base_rate;
    if (lfo_pitch_mod_q15 != 0) {
        int64_t delta = (int64_t)rate * lfo_pitch_mod_q15 / 546307L;
        rate = (uint32_t)CLAMP((int64_t)rate + delta, 1, 0x7FFFFF);
    }

    /* Hermite cubic interpolation (4-point) */
    uint32_t idx = w->pos_int;
    uint32_t frac = w->pos_frac;
    int32_t out = hermite_interp(w->pcm, idx, frac, w->length);

    /* Loop crossfade: near loop_end, blend with loop_start region
     * using equal-power (cosine/sine) crossfade. */
    uint32_t end = w->loop_end;
    uint32_t ls  = w->loop_start;
    if (ls > 0 && ls < end) {
        uint32_t loop_len = end - ls;
        uint32_t fade_len = 1024;
        if (fade_len > loop_len / 4)
            fade_len = loop_len / 4;

        if (fade_len > 0 && idx >= end - fade_len && idx < end) {
            uint32_t rel = idx - (end - fade_len);
            uint32_t wrap_idx = ls + rel;
            if (wrap_idx + 2 < end) {
                int32_t wrapped = hermite_interp(w->pcm, wrap_idx, frac, w->length);
                /* Equal-power crossfade: cos/sin from precomputed table */
                uint32_t ti = rel * 1024 / fade_len;  /* 0..1024 */
                int32_t g_out  = (int32_t)g_cos_q15[ti];        /* cos: 1→0 */
                int32_t g_wrap = (int32_t)g_cos_q15[1024 - ti]; /* sin: 0→1 */
                out = Q15_MUL(out, g_out) + Q15_MUL(wrapped, g_wrap);
            }
        }
    }

    /* Advance position by rate */
    uint32_t new_frac = w->pos_frac + (rate & 0xFFFF);
    w->pos_int += (rate >> 16) + (new_frac >> 16);
    w->pos_frac = new_frac & 0xFFFF;

    /* Handle end-of-sample / looping */
    if (w->pos_int >= end) {
        if (ls > 0 && ls < end) {
            uint32_t loop_len = end - ls;
            if (loop_len > 0) {
                w->pos_int = ls
                           + ((w->pos_int - ls) % loop_len);
            } else {
                w->finished = 1;
            }
        } else {
            w->finished = 1;
            return 0;
        }
    }

    /* Wave gain: 0=×0.5, 1=×1, 2=×2, 3=×4 */
    const WGParams *pp = w->params;
    if (pp->wave_gain == 0) out >>= 1;
    else if (pp->wave_gain == 2) out = CLAMP(out * 2, -32767, 32767);
    else if (pp->wave_gain == 3) out = CLAMP(out * 4, -32767, 32767);

    return (int32_t)CLAMP(out, -32767, 32767);
}
