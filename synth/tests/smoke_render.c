#include <stdio.h>
#include <string.h>

#include "voice.h"
#include "effects.h"
#include "sample_bank.h"

extern void wg_tables_init(void);
extern void wg_set_sample_bank(const SampleBank *sb);

static SampleBank g_bank;
static int16_t g_pcm[256];

const SampleZone *sample_bank_find_zone(const SampleBank *sb,
                                        int inst_idx, uint8_t note)
{
    (void)note;

    if (inst_idx < 0 || inst_idx >= sb->num_instruments)
        return 0;

    return &sb->instruments[inst_idx].zones[0];
}

static void make_loop(void)
{
    for (int i = 0; i < 256; i++) {
        int phase = i & 63;
        int value = phase < 32 ? phase : 63 - phase;
        g_pcm[i] = (int16_t)((value * 18000 / 31) - 9000);
    }
}

static void make_bank(void)
{
    memset(&g_bank, 0, sizeof(g_bank));
    strcpy(g_bank.instruments[0].name, "host-smoke");
    g_bank.instruments[0].num_zones = 1;
    g_bank.instruments[0].zones[0].pcm = g_pcm;
    g_bank.instruments[0].zones[0].length = 256;
    g_bank.instruments[0].zones[0].low_key = 0;
    g_bank.instruments[0].zones[0].high_key = 127;
    g_bank.instruments[0].zones[0].root_key = 60;
    g_bank.instruments[0].zones[0].volume = 100;
    g_bank.instruments[0].zones[0].loop_start = 1;
    g_bank.instruments[0].zones[0].loop_end = 255;
    g_bank.num_instruments = 1;
    wg_set_sample_bank(&g_bank);
}

static void make_patch(PatchParams *p)
{
    memset(p, 0, sizeof(*p));
    strcpy(p->name, "smoke");
    p->level = 100;

    ToneParams *t = &p->tone[0];
    t->sw = 1;
    t->wg.inst_idx = 0;
    t->wg.wave_gain = 1;
    t->wg.pitch_kf = 7;
    t->wg.env.time[0] = 0;
    t->wg.env.time[1] = 50;
    t->wg.env.time[2] = 50;
    t->wg.env.time[3] = 50;
    t->wg.env.time[4] = 50;
    t->wg.env.level[0] = 100;
    t->wg.env.level[1] = 100;
    t->wg.env.level[2] = 100;
    t->wg.env.level[3] = 0;

    t->tvf.mode = TVF_MODE_LPF;
    t->tvf.cutoff = 127;
    t->tvf.resonance = 0;
    t->tvf.kf = 7;
    t->tvf.env_depth = 64;
    t->tvf.env.time[0] = 0;
    t->tvf.env.time[1] = 50;
    t->tvf.env.time[2] = 50;
    t->tvf.env.time[3] = 50;
    t->tvf.env.time[4] = 50;
    t->tvf.env.level[0] = 127;
    t->tvf.env.level[1] = 127;
    t->tvf.env.level[2] = 127;
    t->tvf.env.level[3] = 0;
    t->tvf.env.time_kf = 7;

    t->tva.level = 100;
    t->tva.vel_sens = 0;
    t->tva.pan = 0;
    t->tva.env.time[0] = 0;
    t->tva.env.time[1] = 50;
    t->tva.env.time[2] = 50;
    t->tva.env.time[3] = 50;
    t->tva.env.time[4] = 50;
    t->tva.env.level[0] = 127;
    t->tva.env.level[1] = 127;
    t->tva.env.level[2] = 127;
    t->tva.env.level[3] = 0;
    t->tva.env.time_kf = 7;

    t->lfo.wave = LFO_WAVE_TRI;
    t->lfo.rate = 0;
    t->lfo.key_trigger = 1;
}

int main(void)
{
    make_loop();
    make_bank();

    envelope_tables_init();
    lfo_tables_init();
    wg_tables_init();
    tvf_tables_init();

    PatchParams patch;
    VoiceAllocator voices;
    EffectsState effects;

    make_patch(&patch);
    voice_alloc_init(&voices);
    effects_init(&effects);
    effects_patch(&effects, &patch);

    voice_note_on(&voices, &patch, 60, 100);

    int32_t peak = 0;
    int64_t energy = 0;

    for (int i = 0; i < 4800; i++) {
        int32_t l = 0;
        int32_t r = 0;
        int32_t out_l = 0;
        int32_t out_r = 0;

        voice_render_sample(&voices, &l, &r);
        effects_process(&effects, l, r, &out_l, &out_r);

        int32_t abs_l = out_l < 0 ? -out_l : out_l;
        int32_t abs_r = out_r < 0 ? -out_r : out_r;
        if (abs_l > peak) peak = abs_l;
        if (abs_r > peak) peak = abs_r;
        energy += abs_l + abs_r;
    }

    if (peak < 64 || energy < 100000) {
        fprintf(stderr, "render smoke failed: peak=%ld energy=%lld\n",
                (long)peak, (long long)energy);
        return 1;
    }

    printf("render smoke passed: peak=%ld energy=%lld\n",
           (long)peak, (long long)energy);
    return 0;
}
