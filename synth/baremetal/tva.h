#ifndef TVA_H
#define TVA_H

#include "../include/types.h"
#include "patch.h"
#include "envelope.h"

typedef struct {
    EnvState  env;
    int32_t   base_level;   /* Q15: 0..32767 from patch level */
    int32_t   pan_l;        /* Q15 left channel multiplier */
    int32_t   pan_r;        /* Q15 right channel multiplier */
    const TVAParams *params;
} TVAState;

void tva_note_on (TVAState *t, const TVAParams *p,
                  uint8_t velocity, uint8_t note);
void tva_note_off(TVAState *t);
bool tva_is_idle (const TVAState *t);
void tva_process (TVAState *t, int32_t input,
                  int32_t lfo_mod_q15, int32_t after_q15,
                  int32_t *out_l, int32_t *out_r);

#endif
