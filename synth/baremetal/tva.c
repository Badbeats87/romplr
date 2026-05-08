#include "tva.h"
#include "../include/types.h"

/* Equal-power pan table: g_pan_r[i] = sin(i * π/2 / 126) * 32767
 * g_pan_l[i] = g_pan_r[126 - i]  (just index reversed)
 * Computed at init using Bhaskara cos: cos(x)≈(π²-4x²)/(π²+x²) */
static int16_t g_pan_r[127];  /* 0..126 */
static bool g_pan_inited = false;

static void pan_tables_init(void)
{
    if (g_pan_inited) return;
    /* sin(i * π/2 / 126) = cos((126-i) * π/2 / 126).
     * Map i in [0,126] → u in [0,1024] for the cos formula:
     * u = (126-i) * 1024 / 126.
     * cos(u*π/2/1024) ≈ (4194304 - 4*u²) / (4194304 + u²) */
    for (int i = 0; i <= 126; i++) {
        int32_t u = (126 - i) * 1024 / 126;
        int64_t u2 = (int64_t)u * u;
        int64_t num = 4194304LL - 4 * u2;
        int64_t den = 4194304LL + u2;
        int32_t val = (int32_t)(num * 32767 / den);
        g_pan_r[i] = (int16_t)CLAMP(val, 0, 32767);
    }
    g_pan_inited = true;
}

static void compute_pan(int8_t pan, int32_t *l, int32_t *r)
{
    pan_tables_init();
    int32_t p = CLAMP((int32_t)pan + 63, 0, 126);
    *r = (int32_t)g_pan_r[p];
    *l = (int32_t)g_pan_r[126 - p];
}

void tva_note_on(TVAState *t, const TVAParams *p,
                 uint8_t velocity, uint8_t note)
{
    t->params = p;

    t->base_level = (int32_t)p->level * 32767 / 100;

    int32_t vel = (int32_t)velocity;
    switch (p->vel_sens) {
    case 0: break;
    case 1: t->base_level = t->base_level * (64 + vel / 2) / 127; break;
    case 2: t->base_level = (int32_t)(t->base_level * vel / 127); break;
    case 3: t->base_level = (int32_t)(t->base_level * vel * vel / (127 * 127)); break;
    default: break;
    }
    t->base_level = CLAMP(t->base_level, 0, 32767);

    compute_pan(p->pan, &t->pan_l, &t->pan_r);

    envelope_note_on(&t->env, &p->env, velocity, note, 0);
}

void tva_note_off(TVAState *t)
{
    envelope_note_off(&t->env);
}

bool tva_is_idle(const TVAState *t)
{
    return t->env.stage == ENV_IDLE;
}

void tva_process(TVAState *t, int32_t input,
                 int32_t lfo_mod_q15, int32_t after_q15,
                 int32_t *out_l, int32_t *out_r)
{
    int32_t env = envelope_tick(&t->env);

    /* LFO tremolo: lfo_mod_q15 is a Q15 multiplier (0..32767) from tone.c */
    if (lfo_mod_q15 < 32767) {
        env = Q15_MUL(env, lfo_mod_q15);
    }

    if (after_q15 > 0) {
        int32_t at_add = Q15_MUL(after_q15, 4096);
        env = CLAMP(env + at_add, 0, 32767);
    }

    int32_t amp = Q15_MUL(t->base_level, env);
    int32_t sig = Q15_MUL(input, amp);

    *out_l = Q15_MUL(sig, t->pan_l);
    *out_r = Q15_MUL(sig, t->pan_r);
}
