#include "rompler.h"
#include "envelope.h"
#include "lfo.h"
#include "wg.h"
#include "tvf.h"
#include "cc_map.h"
#include "../include/types.h"
#include <string.h>

/* ── MIDI callback shims ────────────────────────────────────── */

static Rompler *g_rompler = 0;

static void cb_note_on(uint8_t ch, uint8_t note, uint8_t vel)
{
    if (!g_rompler) return;
    if (g_rompler->midi_channel != 16 && ch != g_rompler->midi_channel) return;
    voice_note_on(&g_rompler->voices,
                  &g_rompler->patches[g_rompler->current_patch],
                  note, vel);
}

static void cb_note_off(uint8_t ch, uint8_t note, uint8_t vel)
{
    if (!g_rompler) return;
    if (g_rompler->midi_channel != 16 && ch != g_rompler->midi_channel) return;
    voice_note_off(&g_rompler->voices, note);
    (void)vel;
}

/* ── Live parameter helpers ─────────────────────────────────── */

static void update_active_cutoff(int32_t cutoff)
{
    VoiceAllocator *va = &g_rompler->voices;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!va->voices[v].active) continue;
        for (int t = 0; t < NUM_TONES; t++) {
            if (va->voices[v].tones[t].active)
                va->voices[v].tones[t].tvf.base_cutoff = cutoff;
        }
    }
}

static void update_active_envelope_time(int rate_idx, uint8_t time_val)
{
    VoiceAllocator *va = &g_rompler->voices;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!va->voices[v].active) continue;
        for (int t = 0; t < NUM_TONES; t++) {
            if (va->voices[v].tones[t].active)
                envelope_update_time(&va->voices[v].tones[t].tva.env,
                                     rate_idx, time_val);
        }
    }
}

static void update_active_envelope_level(int level_idx, uint8_t level_val)
{
    VoiceAllocator *va = &g_rompler->voices;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!va->voices[v].active) continue;
        for (int t = 0; t < NUM_TONES; t++) {
            if (va->voices[v].tones[t].active)
                envelope_update_level(&va->voices[v].tones[t].tva.env,
                                       level_idx, level_val);
        }
    }
}

static void update_active_lfo_rate(uint8_t rate)
{
    VoiceAllocator *va = &g_rompler->voices;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!va->voices[v].active) continue;
        for (int t = 0; t < NUM_TONES; t++) {
            if (va->voices[v].tones[t].active)
                lfo_update_rate(&va->voices[v].tones[t].lfo, rate);
        }
    }
}

static void refresh_effects(void)
{
    effects_patch(&g_rompler->effects,
                  &g_rompler->patches[g_rompler->current_patch]);
}

/* ── CC callback ─────────────────────────────────────────────── */

static void cb_cc(uint8_t ch, uint8_t cc, uint8_t val)
{
    if (!g_rompler) return;
    if (g_rompler->midi_channel != 16 && ch != g_rompler->midi_channel) return;

    PatchParams *p = &g_rompler->patches[g_rompler->current_patch];

    switch (cc) {

    /* ── Standard MIDI CCs ──────────────────────────────────── */
    case CC_VOLUME:
        p->level = (uint8_t)(val * 100u / 127u);
        break;
    case CC_PORTAMENTO:
        p->portamento_sw   = (val > 0) ? 1 : 0;
        p->portamento_time = val;
        break;
    case CC_SUSTAIN:
        voice_sustain(&g_rompler->voices, val >= 64);
        break;
    case CC_ALL_SOUND_OFF:
    case CC_ALL_NOTES_OFF:
        voice_all_off(&g_rompler->voices);
        break;

    /* ── Sound Control CCs ──────────────────────────────────── */
    case CC_CUTOFF:         /* CC 74 — filter cutoff */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].tvf.cutoff = val;
        update_active_cutoff((int32_t)val);
        break;

    case CC_RESONANCE:      /* CC 71 — filter resonance */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].tvf.resonance = val;
        break;

    case CC_ATTACK:         /* CC 73 — attack: 1ms→1000ms (match Linux) */
        {
            uint8_t mapped = (uint8_t)CLAMP((int)val * 97 / 127 - 10, 0, 127);
            for (int t = 0; t < NUM_TONES; t++)
                p->tone[t].tva.env.time[0] = mapped;
            update_active_envelope_time(0, mapped);
        }
        break;

    case CC_DECAY:          /* CC 75 — decay: 10ms→10000ms (match Linux) */
        {
            uint8_t mapped = (uint8_t)CLAMP(23 + (int)val * 97 / 127, 0, 127);
            for (int t = 0; t < NUM_TONES; t++) {
                p->tone[t].tva.env.time[1] = mapped;
                p->tone[t].tva.env.time[2] = mapped;
            }
            update_active_envelope_time(1, mapped);
            update_active_envelope_time(2, mapped);
        }
        break;

    case CC_SUSTAIN_LEVEL:  /* CC 70 — sustain level */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].tva.env.level[2] = val;
        update_active_envelope_level(3, val);
        break;

    case CC_RELEASE:        /* CC 72 — release: 10ms→15000ms (match Linux) */
        {
            uint8_t mapped = (uint8_t)CLAMP(23 + (int)val * 104 / 127, 0, 127);
            for (int t = 0; t < NUM_TONES; t++) {
                p->tone[t].tva.env.time[3] = mapped;
                p->tone[t].tva.env.time[4] = mapped;
            }
            update_active_envelope_time(3, mapped);
            update_active_envelope_time(4, mapped);
        }
        break;

    /* ── LFO CCs ────────────────────────────────────────────── */
    case CC_LFO_RATE:       /* CC 76 */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].lfo.rate = val;
        update_active_lfo_rate(val);
        break;

    case CC_LFO_PITCH:      /* CC 78 */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].lfo.pitch_depth = val;
        break;

    case CC_LFO_FILTER:     /* CC 77 */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].lfo.tvf_depth = val;
        break;

    case CC_LFO_AMP:        /* CC 79 — LFO amplitude depth */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].lfo.tva_depth = val;
        break;

    case CC_LFO_WAVE:       /* CC 80 — LFO waveform (0-5) */
        {
            uint8_t wave = (uint8_t)(val * 5 / 127);
            if (wave > 5) wave = 5;
            for (int t = 0; t < NUM_TONES; t++)
                p->tone[t].lfo.wave = wave;
        }
        break;

    /* ── Effects CCs ────────────────────────────────────────── */
    case CC_FILTER_ENV:     /* CC 82 — filter envelope depth */
        for (int t = 0; t < NUM_TONES; t++)
            p->tone[t].tvf.env_depth = val;
        break;

    case CC_CHORUS:         /* CC 93 */
        p->chorus_level = val;
        refresh_effects();
        break;

    case CC_REVERB:         /* CC 91 */
        p->reverb_level = val;
        refresh_effects();
        break;

    default:
        break;
    }
}

static void cb_pitch_bend(uint8_t ch, int16_t value)
{
    if (!g_rompler) return;
    if (g_rompler->midi_channel != 16 && ch != g_rompler->midi_channel) return;
    int8_t semi = (int8_t)(value * 2 / 8192);
    voice_set_bend(&g_rompler->voices, semi);
}

static void cb_aftertouch(uint8_t ch, uint8_t pressure)
{
    if (!g_rompler) return;
    if (g_rompler->midi_channel != 16 && ch != g_rompler->midi_channel) return;
    voice_set_after(&g_rompler->voices, pressure);
}

static void cb_program(uint8_t ch, uint8_t prog)
{
    if (!g_rompler) return;
    if (g_rompler->midi_channel != 16 && ch != g_rompler->midi_channel) return;
    rompler_patch(g_rompler, prog % g_rompler->sample_bank.num_instruments);
}

/* ── Build default patch from instrument settings ──────────── */

static void build_patch_from_instrument(PatchParams *p, const Instrument *inst, int inst_idx)
{
    memset(p, 0, sizeof(PatchParams));

    /* Copy name */
    for (int i = 0; i < 15 && inst->name[i]; i++)
        p->name[i] = inst->name[i];

    p->level = 100;
    p->fine_tune = 50;
    p->structure = 1;

    /* Tone 0 — the only tone */
    ToneParams *t = &p->tone[0];
    t->sw       = 1;
    t->env_mode = 1;

    /* WG */
    t->wg.inst_idx     = (uint8_t)inst_idx;
    t->wg.wave_gain    = 1;  /* ×1 */
    t->wg.pitch_coarse = 0;
    t->wg.pitch_fine   = 0;
    t->wg.pitch_kf     = 7;  /* 1:1 key follow */
    t->wg.bender_sw    = 1;
    t->wg.bender_range = 2;
    /* Pitch envelope: flat */
    t->wg.env.time[0] = 0; t->wg.env.time[1] = 31; t->wg.env.time[2] = 31;
    t->wg.env.time[3] = 31; t->wg.env.time[4] = 31;
    t->wg.env.level[0] = 100; t->wg.env.level[1] = 80;
    t->wg.env.level[2] = 80;  t->wg.env.level[3] = 0;
    t->wg.env.time_kf = 7; t->wg.env.vel_sens = 0;
    t->wg.env_depth   = 64;  /* centre = no modulation */

    /* TVF */
    t->tvf.mode      = TVF_MODE_LPF;
    t->tvf.cutoff    = inst->filter_cutoff;
    t->tvf.resonance = inst->filter_resonance;
    t->tvf.kf        = 9;   /* moderate key follow */
    t->tvf.env_depth = 80;
    /* Filter envelope */
    t->tvf.env.time[0] = 0; t->tvf.env.time[1] = 31; t->tvf.env.time[2] = 31;
    t->tvf.env.time[3] = 49; t->tvf.env.time[4] = 49;
    t->tvf.env.level[0] = 127; t->tvf.env.level[1] = 90;
    t->tvf.env.level[2] = 80;  t->tvf.env.level[3] = 0;
    t->tvf.env.time_kf = 7; t->tvf.env.vel_sens = 1;

    /* TVA */
    t->tva.level    = 100;
    t->tva.vel_sens = 2;  /* velocity sensitive */
    t->tva.pan      = 0;  /* centre */
    /* Amplitude envelope */
    t->tva.env.time[0]  = inst->attack;
    t->tva.env.time[1]  = 25;
    t->tva.env.time[2]  = 25;
    t->tva.env.time[3]  = inst->release;
    t->tva.env.time[4]  = inst->release;
    t->tva.env.level[0] = 127;
    t->tva.env.level[1] = 110;
    t->tva.env.level[2] = 100;
    t->tva.env.level[3] = 0;
    t->tva.env.time_kf  = 7;
    t->tva.env.vel_sens = 2;

    /* LFO — gentle vibrato */
    t->lfo.wave        = LFO_WAVE_TRI;
    t->lfo.rate        = 50;
    t->lfo.delay       = 30;
    t->lfo.fade        = 40;
    t->lfo.pitch_depth = 8;
    t->lfo.tvf_depth   = 0;
    t->lfo.tva_depth   = 0;
    t->lfo.key_trigger = 1;

    /* Chorus */
    p->chorus_sw       = (inst->chorus_level > 0) ? 1 : 0;
    p->chorus_rate     = 50;
    p->chorus_depth    = 40;
    p->chorus_delay    = 20;
    p->chorus_feedback = 20;
    p->chorus_level    = inst->chorus_level;

    /* Reverb */
    p->reverb_sw       = (inst->reverb_level > 0) ? 1 : 0;
    p->reverb_type     = REVERB_HALL1;
    p->reverb_time     = 80;
    p->reverb_level    = inst->reverb_level;

    /* EQ — flat */
    p->eq_low  = 0;
    p->eq_mid  = 0;
    p->eq_high = 0;
    p->eq_mid_freq = 7;
}

/* ── Init ───────────────────────────────────────────────────── */

/* External declaration for wg.c's sample bank setter */
extern void wg_set_sample_bank(const SampleBank *sb);

void rompler_init(Rompler *r)
{
    g_rompler = r;

    /* Init precomputed tables */
    envelope_tables_init();
    lfo_tables_init();
    wg_tables_init();
    tvf_tables_init();

    /* Load samples from SD card */
    sample_bank_init(&r->sample_bank);

    /* Set sample bank for wave generator */
    wg_set_sample_bank(&r->sample_bank);

    /* Build patches from loaded instruments */
    memset(r->patches, 0, sizeof(r->patches));
    for (int i = 0; i < r->sample_bank.num_instruments && i < NUM_PATCHES; i++) {
        build_patch_from_instrument(&r->patches[i],
                                     &r->sample_bank.instruments[i], i);
    }

    /* If no instruments loaded, create a silent fallback */
    if (r->sample_bank.num_instruments == 0) {
        strcpy(r->patches[0].name, "No Samples");
        r->patches[0].level = 0;
    }

    r->current_patch = 0;
    r->midi_channel  = 16;  /* omni — respond to all channels */

    voice_alloc_init(&r->voices);
    effects_init(&r->effects);

    /* Set up MIDI parser */
    midi_init(&r->midi);
    r->midi.on_note_on    = cb_note_on;
    r->midi.on_note_off   = cb_note_off;
    r->midi.on_cc         = cb_cc;
    r->midi.on_pitch_bend = cb_pitch_bend;
    r->midi.on_aftertouch = cb_aftertouch;
    r->midi.on_program    = cb_program;

    /* Apply first patch to effects */
    effects_patch(&r->effects, &r->patches[0]);
}

int rompler_load_next_pending(Rompler *r)
{
    int before = r->sample_bank.num_instruments;
    int more = sample_bank_load_next_pending(&r->sample_bank);
    int after = r->sample_bank.num_instruments;

    /* Build patch for each newly loaded instrument */
    for (int i = before; i < after && i < NUM_PATCHES; i++) {
        build_patch_from_instrument(&r->patches[i],
                                     &r->sample_bank.instruments[i], i);
    }

    return more;
}

void rompler_patch(Rompler *r, int patch_num)
{
    int max = r->sample_bank.num_instruments;
    if (max < 1) max = 1;
    patch_num = CLAMP(patch_num, 0, max - 1);
    r->current_patch = patch_num;
    effects_patch(&r->effects, &r->patches[patch_num]);
}

void rompler_midi(Rompler *r, uint8_t byte)
{
    midi_feed(&r->midi, byte);
}

void rompler_render(Rompler *r, int32_t *out_l, int32_t *out_r)
{
    int32_t raw_l = 0, raw_r = 0;
    voice_render_sample(&r->voices, &raw_l, &raw_r);
    effects_process(&r->effects, raw_l, raw_r, out_l, out_r);
}
