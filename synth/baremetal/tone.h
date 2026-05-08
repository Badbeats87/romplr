#ifndef TONE_H
#define TONE_H

#include "../include/types.h"
#include "patch.h"
#include "wg.h"
#include "tvf.h"
#include "tva.h"
#include "lfo.h"

/* One 'Tone' = WG + TVF + TVA + LFO */
typedef struct {
    WGState  wg;
    TVFState tvf;
    TVAState tva;
    LFOState lfo;
    bool     active;
} ToneState;

void tone_note_on (ToneState *t, const ToneParams *p,
                   uint8_t note, uint8_t velocity,
                   int8_t pitch_bend_semi);
void tone_note_off(ToneState *t);
bool tone_is_idle (const ToneState *t);

/* Render one stereo sample pair */
void tone_tick(ToneState *t, int32_t after_q15,
               int32_t *out_l, int32_t *out_r);

#endif
