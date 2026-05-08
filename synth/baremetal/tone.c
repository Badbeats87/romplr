#include "tone.h"
#include "../include/types.h"

void tone_note_on(ToneState *t, const ToneParams *p,
                  uint8_t note, uint8_t velocity,
                  int8_t pitch_bend_semi)
{
    if (!p->sw) {
        t->active = false;
        return;
    }

    t->active = true;

    lfo_note_on (&t->lfo, &p->lfo);
    wg_note_on  (&t->wg,  &p->wg,  note, 0, pitch_bend_semi);
    tvf_note_on (&t->tvf, &p->tvf, note, velocity);
    tva_note_on (&t->tva, &p->tva, velocity, note);
}

void tone_note_off(ToneState *t)
{
    if (!t->active) return;
    lfo_note_off (&t->lfo);
    wg_note_off  (&t->wg);
    tvf_note_off (&t->tvf);
    tva_note_off (&t->tva);
}

bool tone_is_idle(const ToneState *t)
{
    return !t->active || tva_is_idle(&t->tva);
}

void tone_tick(ToneState *t, int32_t after_q15,
               int32_t *out_l, int32_t *out_r)
{
    if (!t->active) {
        *out_l = *out_r = 0;
        return;
    }

    /* LFO */
    int32_t lfo_raw = lfo_tick(&t->lfo);
    const LFOParams *lp = t->lfo.params;

    int32_t lfo_pitch  = lp ? Q15_MUL(lfo_raw, (int32_t)lp->pitch_depth * 258) : 0;
    int32_t lfo_filter = lp ? Q15_MUL(lfo_raw, (int32_t)lp->tvf_depth   * 258) : 0;

    /* LFO amp: unipolar tremolo matching Linux: mul = 1 - depth*0.5*(1-lfo)
     * At full depth: lfo=+1 → mul=1.0, lfo=-1 → mul=0.0 */
    int32_t lfo_amp = 32767;  /* Q15 multiplier, default = no modulation */
    if (lp && lp->tva_depth > 0) {
        int32_t depth_q15 = (int32_t)lp->tva_depth * 258;  /* 0..32766 */
        int32_t half_inv_lfo = (32767 - lfo_raw) / 2;       /* 0..32767 */
        int32_t mod = Q15_MUL(depth_q15, half_inv_lfo);
        lfo_amp = CLAMP(32767 - mod, 0, 32767);
    }

    /* WG → PCM sample */
    int32_t wg_out = wg_tick(&t->wg, lfo_pitch);

    /* TVF → filtered sample */
    int32_t flt_out = tvf_process(&t->tvf, wg_out, lfo_filter, after_q15);

    /* TVA → stereo, with envelope */
    tva_process(&t->tva, flt_out, lfo_amp, after_q15, out_l, out_r);

    if (tva_is_idle(&t->tva)) {
        t->active = false;
    }
}
