#ifndef LFO_H
#define LFO_H

#include "../include/types.h"
#include "patch.h"

typedef struct {
    uint32_t phase;         /* Q24 phase accumulator */
    uint32_t rate_inc;      /* Q24 increment per sample */
    int32_t  output;        /* Q15: -32767..32767 */
    uint32_t delay_samp;    /* samples of delay remaining */
    uint32_t fade_samp;     /* samples of fade-in remaining */
    uint32_t fade_total;    /* total fade-in samples */
    int32_t  fade_level;    /* current Q15 fade multiplier */
    uint32_t hold_noise;    /* for S&H: holds value until next trigger */
    uint32_t lfsr;          /* random state */
    const LFOParams *params;
} LFOState;

void    lfo_tables_init(void);
void    lfo_note_on(LFOState *l, const LFOParams *p);
void    lfo_note_off(LFOState *l);
void    lfo_update_rate(LFOState *l, uint8_t rate);
int32_t lfo_tick(LFOState *l);   /* returns Q15 signed output */

#endif
