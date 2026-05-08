#ifndef EFFECTS_H
#define EFFECTS_H

#include "../include/types.h"
#include "patch.h"

/* Effects Chain:
 *   Stereo input
 *   → 3-band EQ (biquad)
 *   → Chorus (modulated delay)
 *   → Reverb (Schroeder reverberator)
 *   → Stereo output
 */

#define CHORUS_MAX_DELAY    4096    /* samples (~93ms at 44100 Hz) */
#define REVERB_COMB_LEN     4       /* 4 comb filters */
#define REVERB_ALLPASS_LEN  2       /* 2 allpass filters */

/* Comb filter state */
typedef struct {
    int32_t buf[8192];
    int     pos;
    int     len;
    int32_t feedback;   /* Q15 */
} CombFilter;

/* Allpass filter state */
typedef struct {
    int32_t buf[1024];
    int     pos;
    int     len;
    int32_t coeff;      /* Q15 */
} AllpassFilter;

/* Biquad (for EQ) */
typedef struct {
    int32_t b0, b1, b2;    /* Q15 coefficients */
    int32_t a1, a2;
    int32_t x1, x2;        /* input history */
    int32_t y1, y2;        /* output history */
} Biquad;

typedef struct {
    /* Chorus */
    int32_t  chorus_buf_l[CHORUS_MAX_DELAY];
    int32_t  chorus_buf_r[CHORUS_MAX_DELAY];
    int      chorus_write;
    uint32_t chorus_lfo_phase;
    uint32_t chorus_lfo_inc;

    /* Reverb */
    CombFilter    comb[REVERB_COMB_LEN];
    AllpassFilter allpass[REVERB_ALLPASS_LEN];

    /* EQ */
    Biquad eq_low_l,  eq_low_r;
    Biquad eq_mid_l,  eq_mid_r;
    Biquad eq_high_l, eq_high_r;

    /* Current patch params (not owned) */
    const PatchParams *patch;
} EffectsState;

void effects_init  (EffectsState *e);
void effects_patch (EffectsState *e, const PatchParams *p);
void effects_process(EffectsState *e,
                     int32_t in_l, int32_t in_r,
                     int32_t *out_l, int32_t *out_r);

#endif
