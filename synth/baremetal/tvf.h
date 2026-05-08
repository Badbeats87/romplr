#ifndef TVF_H
#define TVF_H

#include "../include/types.h"
#include "patch.h"
#include "envelope.h"

/* Time Variant Filter — State Variable Filter (SVF)
 * Implements LPF / BPF / HPF / Notch (PKG) modes
 *
 * SVF recurrence (Q15):
 *   high  = in − low − q × band
 *   band += f × high
 *   low  += f × band
 */

void tvf_tables_init(void);

typedef struct {
    int32_t  low, band;     /* SVF state, Q15 */
    EnvState env;
    int32_t  base_cutoff;   /* 0–127 from patch */
    int32_t  smooth_cutoff_q8; /* Q8 smoothed cutoff for click-free changes */
    const TVFParams *params;
} TVFState;

void    tvf_note_on (TVFState *t, const TVFParams *p,
                     uint8_t note, uint8_t velocity);
void    tvf_note_off(TVFState *t);
int32_t tvf_process (TVFState *t, int32_t input,
                     int32_t lfo_mod_q15, int32_t after_q15);

#endif
