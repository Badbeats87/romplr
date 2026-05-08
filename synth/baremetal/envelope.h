#ifndef ENVELOPE_H
#define ENVELOPE_H

#include "../include/types.h"
#include "patch.h"

/* 5-segment envelope state machine
 *
 *  Level
 *  L1 ─────┐
 *           │ T2
 *  L2 ──────┤
 *           │ T3             key release
 *  L3 ──────┴─── SUSTAIN ──── T4 ──→ L4 ──→ T5 ──→ 0
 *
 *  T1: 0  → L1  (attack)
 *  T2: L1 → L2  (decay 1)
 *  T3: L2 → L3  (decay 2 / pre-sustain)
 *  T3 holds at L3 while key held
 *  T4: L3 → L4  (release 1)
 *  T5: L4 → 0   (release 2)
 */

typedef enum {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_DECAY1,
    ENV_DECAY2,
    ENV_SUSTAIN,
    ENV_RELEASE1,
    ENV_RELEASE2
} EnvStage;

typedef struct {
    EnvStage stage;
    int32_t  value;         /* Q16: current output 0..65535 */
    int32_t  target;        /* Q16 target for current segment */
    int32_t  increment;     /* Q16 per-sample increment (signed) */
    uint32_t counter;       /* samples remaining in current segment */

    const Envelope *params; /* pointer to patch parameters */
    int32_t  levels[5];     /* Q16 precomputed L0..L4 for this note */
    int32_t  rates[5];      /* Q16/sample precomputed T1..T5 increments */
} EnvState;

/* Precomputed time→rate table (samples to reach full scale).
 * Called once at init. */
void envelope_tables_init(void);

void envelope_note_on (EnvState *e, const Envelope *p,
                       uint8_t velocity, uint8_t note, int8_t env_depth);
void envelope_note_off(EnvState *e);
void envelope_reset   (EnvState *e);

/* Returns Q15 value (0..32767) for TVA/TVF scaling */
int32_t envelope_tick(EnvState *e);

/* Live-update a single rate (for ADSR CC changes on active voices).
 * rate_idx: 0=attack, 1=decay1, 2=decay2, 3=release1, 4=release2 */
void envelope_update_time(EnvState *e, int rate_idx, uint8_t time_val);

/* Live-update a single level (for sustain level CC changes).
 * level_idx: 1..4 mapping to L1..L4 */
void envelope_update_level(EnvState *e, int level_idx, uint8_t level_val);

#endif
