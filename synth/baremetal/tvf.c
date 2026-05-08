#include "tvf.h"
#include "envelope.h"
#include "../include/types.h"

static int32_t g_f0_table[128];   /* Q15 */
static int32_t g_q_table[128];    /* Q15: damping (1/Q) */

void tvf_tables_init(void)
{
    for (int c = 0; c < 128; c++) {
        uint32_t ipart = (uint32_t)c * 10 / 127;
        uint32_t fpart = (uint32_t)c * 10 % 127;
        uint32_t base  = 1u << ipart;
        uint32_t next  = base * 2;
        uint32_t val   = base + (next - base) * fpart / 127;

        uint32_t fc = 20 * val * 9 / 5;
        if (fc > 20000) fc = 20000;
        if (fc < 20)    fc = 20;

        int64_t f0 = (int64_t)fc * 9342 / 2000;
        if (f0 > 32000) f0 = 32000;
        g_f0_table[c] = (int32_t)f0;
    }

    for (int r = 0; r < 128; r++) {
        uint32_t ipart = (uint32_t)r * 6 / 127;
        uint32_t fpart = (uint32_t)r * 6 % 127;
        uint32_t base  = 1u << ipart;
        uint32_t next  = base * 2;
        uint32_t Q     = base + (next - base) * fpart / 127;
        int32_t qc = (int32_t)(65534 / (Q > 0 ? Q : 1));
        g_q_table[r] = CLAMP(qc, 256, 32767);
    }
}

void tvf_note_on(TVFState *t, const TVFParams *p,
                 uint8_t note, uint8_t velocity)
{
    t->params = p;
    t->low    = 0;
    t->band   = 0;

    int32_t kf_offset = 0;
    if (p->kf > 0) {
        int32_t note_off = (int32_t)note - 60;
        int32_t kf_steps = (int32_t)p->kf * note_off / 7;
        kf_offset = kf_steps;
    }

    t->base_cutoff = CLAMP((int32_t)p->cutoff + kf_offset, 0, 127);
    t->smooth_cutoff_q8 = t->base_cutoff * 256;

    envelope_note_on(&t->env, &p->env, velocity, note, (int8_t)p->env_depth);
}

void tvf_note_off(TVFState *t)
{
    envelope_note_off(&t->env);
}

int32_t tvf_process(TVFState *t, int32_t input,
                    int32_t lfo_mod_q15, int32_t after_q15)
{
    const TVFParams *p = t->params;
    if (!p) return input;

    int32_t env_val = envelope_tick(&t->env);

    int32_t env_depth = (int32_t)p->env_depth - 64;
    int32_t cutoff = t->base_cutoff
                   + (env_val * env_depth * 8 / 32767)
                   + (lfo_mod_q15 / 1300)
                   + (after_q15 * 8 / 32767);

    cutoff = CLAMP(cutoff, 0, 127);

    /* Smooth cutoff changes to avoid SVF clicks (~0.7ms time constant) */
    int32_t target_q8 = cutoff * 256;
    t->smooth_cutoff_q8 += (target_q8 - t->smooth_cutoff_q8) / 32;
    int32_t sc = CLAMP(t->smooth_cutoff_q8 >> 8, 0, 127);

    int32_t f0 = g_f0_table[sc];
    int32_t qc = g_q_table[p->resonance & 0x7F];

    /* Stability clamp: SVF blows up when f0 >= 2/Q (Chamberlin limit).
     * Also hard-cap at π (≈16384 in Q15) for Nyquist safety. */
    int32_t f_max = qc * 2;
    if (f0 > f_max) f0 = f_max;
    if (f0 > 16384) f0 = 16384;

    int32_t high = input - t->low - Q15_MUL(qc, t->band);
    t->band      += Q15_MUL(f0, high);
    t->low       += Q15_MUL(f0, t->band);
    int32_t notch = high + t->low;

    t->band = CLAMP(t->band, -32767, 32767);
    t->low  = CLAMP(t->low,  -32767, 32767);

    switch (p->mode) {
    case TVF_MODE_LPF: return CLAMP(t->low,  -32767, 32767);
    case TVF_MODE_BPF: return CLAMP(t->band, -32767, 32767);
    case TVF_MODE_HPF: return CLAMP(high,    -32767, 32767);
    case TVF_MODE_PKG: return CLAMP(notch,   -32767, 32767);
    default:           return CLAMP(t->low,  -32767, 32767);
    }
}
