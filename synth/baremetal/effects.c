#include "effects.h"
#include "../include/types.h"

static const int comb_lens[REVERB_COMB_LEN]       = { 1687, 1601, 2053, 2251 };
static const int allpass_lens[REVERB_ALLPASS_LEN]  = { 347,  113  };

static void biquad_low_shelf(Biquad *b, int32_t gain_db, int32_t fc)
{
    if (gain_db == 0) {
        b->b0 = 32767; b->b1 = 0; b->b2 = 0;
        b->a1 = 0;     b->a2 = 0;
        return;
    }
    int32_t A_q15 = 32767 + gain_db * 820;
    A_q15 = CLAMP(A_q15, 8192, 65534);
    int32_t w = (int32_t)(6283 * fc / SYNTH_SAMPLE_RATE);
    b->b0 = (int32_t)A_q15;
    b->b1 = 0; b->b2 = 0;
    b->a1 = (int32_t)(32767 - w); b->a2 = 0;
    (void)fc;
}

static void biquad_high_shelf(Biquad *b, int32_t gain_db, int32_t fc)
{
    if (gain_db == 0) {
        b->b0 = 32767; b->b1 = 0; b->b2 = 0;
        b->a1 = 0;     b->a2 = 0;
        return;
    }
    int32_t A_q15 = 32767 + gain_db * 820;
    A_q15 = CLAMP(A_q15, 8192, 65534);
    int32_t w = (int32_t)(6283 * fc / SYNTH_SAMPLE_RATE);
    b->b0 = A_q15;
    b->b1 = (int32_t)(-A_q15 + 32767); b->b2 = 0;
    b->a1 = (int32_t)(32767 - w);       b->a2 = 0;
    (void)fc;
}

static void biquad_peak(Biquad *b, int32_t gain_db, int32_t fc)
{
    if (gain_db == 0) {
        b->b0 = 32767; b->b1 = 0; b->b2 = 0;
        b->a1 = 0;     b->a2 = 0;
        return;
    }
    int32_t A_q15 = 32767 + gain_db * 820;
    A_q15 = CLAMP(A_q15, 8192, 65534);
    int32_t w = (int32_t)(6283 * fc / SYNTH_SAMPLE_RATE);
    b->b0 = A_q15;
    b->b1 = 0;
    b->b2 = (int32_t)(32767 - A_q15);
    b->a1 = -w;
    b->a2 = 0;
}

static int32_t biquad_process(Biquad *b, int32_t x)
{
    int64_t y = (int64_t)b->b0 * x
              + (int64_t)b->b1 * b->x1
              + (int64_t)b->b2 * b->x2
              - (int64_t)b->a1 * b->y1
              - (int64_t)b->a2 * b->y2;
    y >>= 15;
    b->x2 = b->x1; b->x1 = x;
    b->y2 = b->y1; b->y1 = (int32_t)y;
    return (int32_t)CLAMP(y, -32767, 32767);
}

void effects_init(EffectsState *e)
{
    for (int i = 0; i < CHORUS_MAX_DELAY; i++)
        e->chorus_buf_l[i] = e->chorus_buf_r[i] = 0;
    e->chorus_write    = 0;
    e->chorus_lfo_phase = 0;
    e->chorus_lfo_inc  = (uint32_t)(PHASE_ONE / SYNTH_SAMPLE_RATE * 5 / 10);

    for (int i = 0; i < REVERB_COMB_LEN; i++) {
        for (int j = 0; j < 8192; j++) e->comb[i].buf[j] = 0;
        e->comb[i].pos      = 0;
        e->comb[i].len      = comb_lens[i];
        e->comb[i].feedback = 22000;
    }

    for (int i = 0; i < REVERB_ALLPASS_LEN; i++) {
        for (int j = 0; j < 1024; j++) e->allpass[i].buf[j] = 0;
        e->allpass[i].pos   = 0;
        e->allpass[i].len   = allpass_lens[i];
        e->allpass[i].coeff = 16384;
    }

    biquad_low_shelf (&e->eq_low_l,  0, 200);
    biquad_low_shelf (&e->eq_low_r,  0, 200);
    biquad_peak      (&e->eq_mid_l,  0, 1000);
    biquad_peak      (&e->eq_mid_r,  0, 1000);
    biquad_high_shelf(&e->eq_high_l, 0, 8000);
    biquad_high_shelf(&e->eq_high_r, 0, 8000);

    e->patch = 0;
}

void effects_patch(EffectsState *e, const PatchParams *p)
{
    e->patch = p;

    static const int32_t mid_freqs[16] = {
        200, 250, 315, 400, 500, 630, 800, 1000,
        1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300
    };
    int32_t mf = mid_freqs[p->eq_mid_freq & 0xF];
    biquad_low_shelf (&e->eq_low_l,  (int32_t)p->eq_low,  200);
    biquad_low_shelf (&e->eq_low_r,  (int32_t)p->eq_low,  200);
    biquad_peak      (&e->eq_mid_l,  (int32_t)p->eq_mid,  mf);
    biquad_peak      (&e->eq_mid_r,  (int32_t)p->eq_mid,  mf);
    biquad_high_shelf(&e->eq_high_l, (int32_t)p->eq_high, 8000);
    biquad_high_shelf(&e->eq_high_r, (int32_t)p->eq_high, 8000);

    if (p->reverb_sw) {
        int32_t rt = (int32_t)p->reverb_time;
        int32_t fb = 13107 + rt * 103;
        for (int i = 0; i < REVERB_COMB_LEN; i++)
            e->comb[i].feedback = CLAMP(fb, 8000, 30000);
    }

    if (p->chorus_sw) {
        uint32_t rate = (uint32_t)p->chorus_rate;
        e->chorus_lfo_inc = (uint32_t)((uint64_t)(200 + rate * 63) * PHASE_ONE / (SYNTH_SAMPLE_RATE * 1000));
    }
}

void effects_process(EffectsState *e,
                     int32_t in_l, int32_t in_r,
                     int32_t *out_l, int32_t *out_r)
{
    const PatchParams *p = e->patch;

    int32_t l = in_l, r = in_r;

    /* EQ */
    l = biquad_process(&e->eq_low_l,  l);
    l = biquad_process(&e->eq_mid_l,  l);
    l = biquad_process(&e->eq_high_l, l);
    r = biquad_process(&e->eq_low_r,  r);
    r = biquad_process(&e->eq_mid_r,  r);
    r = biquad_process(&e->eq_high_r, r);

    /* Chorus */
    int32_t chorus_l = l, chorus_r = r;
    if (p && p->chorus_sw) {
        e->chorus_lfo_phase = (e->chorus_lfo_phase + e->chorus_lfo_inc) & PHASE_MASK;
        uint32_t ph256 = e->chorus_lfo_phase >> (PHASE_BITS - 8);

        int32_t lfo_val = (ph256 < 128)
            ? (int32_t)ph256 * 256 - 32767
            : 32767 - (int32_t)(ph256 - 128) * 256;

        int32_t depth     = (int32_t)p->chorus_depth * 20;
        int32_t base_del  = 100 + (int32_t)p->chorus_delay * 10;
        int32_t del_samp  = base_del + Q15_MUL(lfo_val, depth);
        del_samp = CLAMP(del_samp, 1, CHORUS_MAX_DELAY - 1);

        int idx_l = (e->chorus_write - del_samp + CHORUS_MAX_DELAY) % CHORUS_MAX_DELAY;
        int idx_r = (e->chorus_write - del_samp + 20 + CHORUS_MAX_DELAY) % CHORUS_MAX_DELAY;
        int32_t del_l = e->chorus_buf_l[idx_l];
        int32_t del_r = e->chorus_buf_r[idx_r];

        int32_t fb = (int32_t)p->chorus_feedback * 256;
        e->chorus_buf_l[e->chorus_write] = l + Q15_MUL(del_l, fb);
        e->chorus_buf_r[e->chorus_write] = r + Q15_MUL(del_r, fb);
        e->chorus_write = (e->chorus_write + 1) % CHORUS_MAX_DELAY;

        int32_t wet = (int32_t)p->chorus_level * 256;
        chorus_l = l + Q15_MUL(del_l, wet);
        chorus_r = r + Q15_MUL(del_r, wet);
    }

    /* Reverb (mono Schroeder, spread to stereo) */
    int32_t rev_out = 0;
    if (p && p->reverb_sw) {
        int32_t rev_in = (chorus_l + chorus_r) / 2;

        int32_t comb_sum = 0;
        for (int i = 0; i < REVERB_COMB_LEN; i++) {
            CombFilter *c = &e->comb[i];
            int32_t delayed = c->buf[c->pos];
            int32_t write   = rev_in + Q15_MUL(delayed, c->feedback);
            c->buf[c->pos]  = write;
            c->pos = (c->pos + 1) % c->len;
            comb_sum += delayed;
        }
        comb_sum /= REVERB_COMB_LEN;

        int32_t ap_out = comb_sum;
        for (int i = 0; i < REVERB_ALLPASS_LEN; i++) {
            AllpassFilter *a = &e->allpass[i];
            int32_t delayed  = a->buf[a->pos];
            int32_t write    = ap_out + Q15_MUL(delayed, a->coeff);
            a->buf[a->pos]   = write;
            a->pos = (a->pos + 1) % a->len;
            ap_out = delayed - Q15_MUL(write, a->coeff);
        }
        rev_out = CLAMP(ap_out, -32767, 32767);
    }

    /* Final mix — keep dry at full volume, blend in reverb */
    if (p && p->reverb_sw) {
        int32_t wet = (int32_t)p->reverb_level * 256;
        *out_l = CLAMP(chorus_l + Q15_MUL(rev_out, wet), -32767, 32767);
        *out_r = CLAMP(chorus_r + Q15_MUL(rev_out, wet), -32767, 32767);
    } else {
        *out_l = CLAMP(chorus_l, -32767, 32767);
        *out_r = CLAMP(chorus_r, -32767, 32767);
    }
}
