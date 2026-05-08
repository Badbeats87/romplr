#include "lfo.h"
#include "../include/types.h"

static uint32_t g_lfo_rate[128];  /* Q24 phase inc per sample */

/* Sine table for LFO (64 entries quarter-wave) */
static int16_t g_lfo_sin64[64];

static int32_t lfo_sin(uint32_t phase)  /* phase Q24, 0..2^24 */
{
    uint32_t idx = (phase >> (PHASE_BITS - 8)) & 0xFF;  /* 0..255 */
    int32_t sign = 1;
    if (idx >= 128) { idx = 255 - idx; if (idx >= 128) { idx = 255 - idx; sign = -1; } }
    if (idx >= 64)  { idx = 127 - idx; }
    return sign * g_lfo_sin64[idx];
}

void lfo_tables_init(void)
{
    /* Precompute quarter-wave sine, Q15 */
    for (int i = 0; i < 64; i++) {
        int32_t x = i * 512;
        int32_t pi2 = 32767;
        int64_t num   = (int64_t)4 * x * (pi2 - x);
        int64_t denom = (int64_t)pi2 * pi2 - (int64_t)2 * x * (pi2 - x) / pi2;
        if (denom <= 0) denom = 1;
        g_lfo_sin64[i] = (int16_t)(num * 32767 / (denom * 32767 / pi2) / pi2);
    }
    g_lfo_sin64[0]  = 0;
    g_lfo_sin64[63] = 32767;

    for (int r = 0; r < 128; r++) {
        uint32_t ipart = (uint32_t)r * 11 / 127;
        uint32_t fpart = (uint32_t)r * 11 % 127;
        uint32_t base  = 1u << ipart;
        uint32_t next  = base * 2;
        uint32_t val   = base + (next - base) * fpart / 127;

        uint64_t f_num = (uint64_t)val * 3;
        uint64_t phase_inc = f_num * (1u << PHASE_BITS) / (100 * SYNTH_SAMPLE_RATE);
        if (phase_inc < 1) phase_inc = 1;
        g_lfo_rate[r] = (uint32_t)phase_inc;
    }
}

void lfo_note_on(LFOState *l, const LFOParams *p)
{
    l->params = p;
    l->rate_inc = g_lfo_rate[p->rate & 0x7F];

    uint32_t delay_ms = (uint32_t)(p->delay) * (uint32_t)(p->delay) / 16;
    l->delay_samp = (uint64_t)delay_ms * SYNTH_SAMPLE_RATE / 1000;

    uint32_t fade_ms = (uint32_t)(p->fade) * (uint32_t)(p->fade) / 16;
    l->fade_total = (uint64_t)fade_ms * SYNTH_SAMPLE_RATE / 1000;
    l->fade_samp  = l->fade_total;
    l->fade_level = (l->fade_total > 0) ? 0 : 32767;

    if (p->key_trigger) {
        l->phase = 0;
    }

    l->lfsr = 0xACE1 + p->rate;
    l->output = 0;
}

void lfo_note_off(LFOState *l)
{
    (void)l;
}

void lfo_update_rate(LFOState *l, uint8_t rate)
{
    l->rate_inc = g_lfo_rate[rate & 0x7F];
}

static int32_t lfo_generate(LFOState *l)
{
    const LFOParams *p = l->params;
    int32_t out = 0;
    uint32_t phase256 = l->phase >> (PHASE_BITS - 8);

    switch (p->wave) {
    case LFO_WAVE_TRI:
        out = (phase256 < 128)
            ? (int32_t)phase256 * 256 - 32767
            : 32767 - (int32_t)(phase256 - 128) * 256;
        break;
    case LFO_WAVE_SINE:
        out = lfo_sin(l->phase);
        break;
    case LFO_WAVE_SAW:
        out = (int32_t)phase256 * 256 - 32767;
        break;
    case LFO_WAVE_SQUARE:
        out = (phase256 < 128) ? 32767 : -32767;
        break;
    case LFO_WAVE_RANDOM: {
        if (phase256 < 4 && (l->phase & 0xFFFF) < (l->rate_inc & 0xFFFF)) {
            l->lfsr = l->lfsr * 1664525 + 1013904223;
            l->hold_noise = l->lfsr >> 17;
        }
        out = (int32_t)l->hold_noise - 32768;
        break;
    }
    case LFO_WAVE_SH: {
        uint32_t prev = l->phase - l->rate_inc;
        if ((prev >> (PHASE_BITS - 1)) && !(l->phase >> (PHASE_BITS - 1))) {
            l->lfsr = l->lfsr * 1664525 + 1013904223;
            l->hold_noise = l->lfsr >> 17;
        }
        out = (int32_t)l->hold_noise - 32768;
        break;
    }
    default: break;
    }

    if (p->offset == 1)      out = (out + 32767) / 2;
    else if (p->offset == 2) out = (out - 32767) / 2;

    return CLAMP(out, -32767, 32767);
}

int32_t lfo_tick(LFOState *l)
{
    if (!l->params) return 0;

    if (l->delay_samp > 0) {
        l->delay_samp--;
        return 0;
    }

    l->phase = (l->phase + l->rate_inc) & PHASE_MASK;

    int32_t raw = lfo_generate(l);

    if (l->fade_samp > 0) {
        l->fade_samp--;
        l->fade_level = (int32_t)(l->fade_total - l->fade_samp)
                        * 32767 / (int32_t)l->fade_total;
    } else {
        l->fade_level = 32767;
    }

    l->output = Q15_MUL(raw, l->fade_level);
    return l->output;
}
