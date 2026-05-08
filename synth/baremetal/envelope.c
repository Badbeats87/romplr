#include "envelope.h"
#include "../include/types.h"

/* ── Time parameter → samples conversion ───────────────────
 * Q24 envelope accumulator for fine-grained long envelopes.
 * Time range: 0 = instant, 127 ≈ 16 seconds
 * Exponential curve: time_ms = 2 × 2^(t × 13/127) ms
 *
 * Matches Linux version ranges:
 *   Attack:  ~2ms (t≈1) to ~1s   (t≈100)
 *   Decay:   ~2ms (t≈1) to ~10s  (t≈120)
 *   Release: ~2ms (t≈1) to ~16s  (t≈127)
 */

#define ENV_Q24_MAX  16777215  /* (1<<24) - 1 */

static uint32_t g_time_rate[128];  /* Q24 increment per sample */

static uint32_t time_to_rate(uint8_t t)
{
    if (t == 0) return ENV_Q24_MAX;           /* instant */

    uint32_t integer_part = (uint32_t)t * 13 / 127;      /* 0..13 */
    uint32_t frac_part    = ((uint32_t)t * 13 % 127) * 128 / 127; /* 0..127 */

    uint32_t base = 1u << integer_part;  /* 1..8192 */
    uint32_t next_base = base * 2;
    uint32_t val = base + (next_base - base) * frac_part / 128;

    uint32_t time_ms = 2 * val;
    if (time_ms < 1) time_ms = 1;

    uint32_t time_samples = (uint64_t)time_ms * SYNTH_SAMPLE_RATE / 1000;
    if (time_samples < 1) time_samples = 1;

    uint32_t rate = (uint32_t)((uint64_t)ENV_Q24_MAX / time_samples);
    if (rate == 0) rate = 1;
    return rate;
}

void envelope_tables_init(void)
{
    for (int i = 0; i < 128; i++)
        g_time_rate[i] = time_to_rate((uint8_t)i);
}

/* Level value 0–127 → Q24 (0..16777215) */
static int32_t level_to_q24(uint8_t level)
{
    return (int32_t)((uint32_t)level * ENV_Q24_MAX / 127);
}

void envelope_reset(EnvState *e)
{
    e->stage     = ENV_IDLE;
    e->value     = 0;
    e->target    = 0;
    e->increment = 0;
    e->counter   = 0;
    e->params    = 0;
}

void envelope_update_time(EnvState *e, int rate_idx, uint8_t time_val)
{
    if (e->stage == ENV_IDLE) return;
    if (rate_idx < 0 || rate_idx > 4) return;

    uint32_t rate = g_time_rate[time_val];
    if (rate == 0) rate = 1;
    e->rates[rate_idx] = (int32_t)rate;

    /* If currently in the affected stage, update the active increment */
    EnvStage affected;
    switch (rate_idx) {
        case 0: affected = ENV_ATTACK;   break;
        case 1: affected = ENV_DECAY1;   break;
        case 2: affected = ENV_DECAY2;   break;
        case 3: affected = ENV_RELEASE1; break;
        case 4: affected = ENV_RELEASE2; break;
        default: return;
    }

    if (e->stage == affected) {
        if (rate_idx == 0) {
            e->increment = (int32_t)rate;
        } else {
            e->increment = (e->target < e->value) ? -(int32_t)rate : (int32_t)rate;
        }
    }
}

void envelope_update_level(EnvState *e, int level_idx, uint8_t level_val)
{
    if (e->stage == ENV_IDLE) return;
    if (level_idx < 1 || level_idx > 4) return;

    e->levels[level_idx] = level_to_q24(level_val);

    /* Sustain level (L3 = levels[3]): ramp smoothly to avoid clicks */
    if (level_idx == 3) {
        if (e->stage == ENV_SUSTAIN) {
            /* Re-enter decay2 to ramp to new sustain level */
            e->stage  = ENV_DECAY2;
            e->target = e->levels[3];
            int32_t ramp_rate = ENV_Q24_MAX / 1536;  /* ~32ms ramp */
            if (ramp_rate < 1) ramp_rate = 1;
            e->increment = (e->target < e->value) ? -ramp_rate : ramp_rate;
        } else if (e->stage == ENV_DECAY2) {
            e->target = e->levels[3];
            e->increment = (e->target < e->value) ? -e->rates[2] : e->rates[2];
        }
    }
}

void envelope_note_on(EnvState *e, const Envelope *p,
                      uint8_t velocity, uint8_t note, int8_t env_depth)
{
    e->params = p;

    /* Velocity scaling */
    int32_t vel_scale = 32767;
    if (p->vel_sens > 0) {
        int32_t v = (int32_t)velocity;
        vel_scale = (v * 32767 / 127);
        if (p->vel_sens == 1) vel_scale = 16384 + vel_scale / 2;
        if (p->vel_sens == 2) vel_scale = vel_scale;
        if (p->vel_sens == 3) vel_scale = vel_scale * vel_scale / 32767;
    }

    /* Key-follow time scaling */
    int32_t kf = (int32_t)p->time_kf - 7;
    int32_t note_off = (int32_t)note - 60;
    int32_t time_scale_q8 = 256 + kf * note_off / 12;
    if (time_scale_q8 < 16) time_scale_q8 = 16;

    /* Precompute levels (Q24) */
    e->levels[0] = 0;
    for (int i = 0; i < 4; i++)
        e->levels[i + 1] = level_to_q24(p->level[i]);

    /* Apply velocity to peak (L1) */
    e->levels[1] = (int32_t)(e->levels[1] * (int64_t)vel_scale / 32767);

    /* Precompute rates (Q24), adjusted for key-follow */
    for (int i = 0; i < 5; i++) {
        uint32_t rate = g_time_rate[p->time[i]];
        rate = (uint32_t)((uint64_t)rate * time_scale_q8 / 256);
        if (rate == 0) rate = 1;
        e->rates[i] = (int32_t)rate;
    }

    /* Start attack */
    e->value     = 0;
    e->stage     = ENV_ATTACK;
    e->target    = e->levels[1];
    e->increment = e->rates[0];

    (void)env_depth;
}

void envelope_note_off(EnvState *e)
{
    if (e->stage == ENV_IDLE) return;
    e->stage  = ENV_RELEASE1;
    e->target = e->params ? e->levels[4] : 0;
    int32_t rate = e->params ? e->rates[3] : ENV_Q24_MAX;
    if (rate == 0) rate = 1;
    e->increment = (e->target < e->value) ? -rate : rate;
}

int32_t envelope_tick(EnvState *e)
{
    if (e->stage == ENV_IDLE) return 0;
    if (!e->params) return 0;

    switch (e->stage) {

    case ENV_ATTACK:
        e->value += e->increment;
        if (e->value >= e->target) {
            e->value = e->target;
            e->stage = ENV_DECAY1;
            e->target = e->levels[2];
            if (e->target < e->value) {
                e->increment = -e->rates[1];
            } else {
                e->increment = e->rates[1];
            }
        }
        break;

    case ENV_DECAY1:
        e->value += e->increment;
        if ((e->increment < 0 && e->value <= e->target) ||
            (e->increment > 0 && e->value >= e->target)) {
            e->value = e->target;
            e->stage = ENV_DECAY2;
            e->target = e->levels[3];
            e->increment = (e->target < e->value) ? -e->rates[2] : e->rates[2];
        }
        break;

    case ENV_DECAY2:
        e->value += e->increment;
        if ((e->increment < 0 && e->value <= e->target) ||
            (e->increment > 0 && e->value >= e->target)) {
            e->value = e->target;
            e->stage = ENV_SUSTAIN;
            e->increment = 0;
        }
        break;

    case ENV_SUSTAIN:
        break;

    case ENV_RELEASE1:
        e->value += e->increment;
        if ((e->increment < 0 && e->value <= e->target) ||
            (e->increment > 0 && e->value >= e->target) ||
             e->value <= 0) {
            e->value = e->target;
            e->stage = ENV_RELEASE2;
            e->target = 0;
            e->increment = -e->rates[4];
        }
        break;

    case ENV_RELEASE2:
        e->value += e->increment;
        if (e->value <= 0) {
            e->value = 0;
            e->stage = ENV_IDLE;
        }
        break;

    default:
        break;
    }

    if (e->value < 0)          e->value = 0;
    if (e->value > ENV_Q24_MAX) e->value = ENV_Q24_MAX;

    /* Return Q15 (0..32767) */
    return e->value >> 9;
}
