#include "voice.h"
#include "../include/types.h"

void voice_alloc_init(VoiceAllocator *va)
{
    for (int v = 0; v < MAX_VOICES; v++) {
        va->voices[v].active    = false;
        va->voices[v].sustained = false;
        va->voices[v].age       = 0;
        va->voices[v].patch     = 0;
        for (int t = 0; t < NUM_TONES; t++) {
            va->voices[v].tones[t].active = false;
            envelope_reset(&va->voices[v].tones[t].tvf.env);
            envelope_reset(&va->voices[v].tones[t].tva.env);
        }
    }
    va->voice_clock    = 0;
    va->sustain_pedal  = false;
    va->pitch_bend_semi = 0;
    va->aftertouch_q15 = 0;
    va->gain_smooth_q15 = 32767;
}

static int find_voice(VoiceAllocator *va)
{
    /* 1. Look for a completely idle voice */
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!va->voices[v].active) return v;
    }

    /* 2. Check for all-tones-idle voices */
    for (int v = 0; v < MAX_VOICES; v++) {
        bool all_idle = true;
        for (int t = 0; t < NUM_TONES; t++) {
            if (!tone_is_idle(&va->voices[v].tones[t])) {
                all_idle = false;
                break;
            }
        }
        if (all_idle) return v;
    }

    /* 3. Steal oldest voice */
    int oldest = 0;
    uint32_t oldest_age = va->voices[0].age;
    for (int v = 1; v < MAX_VOICES; v++) {
        if (va->voices[v].age < oldest_age) {
            oldest     = v;
            oldest_age = va->voices[v].age;
        }
    }
    return oldest;
}

void voice_note_on(VoiceAllocator *va, const PatchParams *patch,
                   uint8_t note, uint8_t velocity)
{
    if (velocity == 0) {
        voice_note_off(va, note);
        return;
    }

    /* Kill any existing voice with same note */
    for (int v = 0; v < MAX_VOICES; v++) {
        if (va->voices[v].active && va->voices[v].note == note) {
            va->voices[v].active    = false;
            va->voices[v].sustained = false;
        }
    }

    int v = find_voice(va);
    Voice *voice = &va->voices[v];

    voice->note      = note;
    voice->velocity  = velocity;
    voice->sustained = false;
    voice->age       = ++va->voice_clock;
    voice->patch     = patch;

    /* Init tones BEFORE marking active — prevents DMA IRQ race
     * where GetChunk sees active voice with uninitialized tones */
    for (int t = 0; t < NUM_TONES; t++) {
        tone_note_on(&voice->tones[t], &patch->tone[t],
                     note, velocity, va->pitch_bend_semi);
    }
    voice->active    = true;
}

void voice_note_off(VoiceAllocator *va, uint8_t note)
{
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *voice = &va->voices[v];
        if (voice->active && voice->note == note) {
            if (va->sustain_pedal) {
                voice->sustained = true;
            } else {
                for (int t = 0; t < NUM_TONES; t++)
                    tone_note_off(&voice->tones[t]);
            }
        }
    }
}

void voice_all_off(VoiceAllocator *va)
{
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *voice = &va->voices[v];
        if (voice->active) {
            for (int t = 0; t < NUM_TONES; t++)
                tone_note_off(&voice->tones[t]);
        }
    }
    va->sustain_pedal = false;
}

void voice_sustain(VoiceAllocator *va, bool on)
{
    va->sustain_pedal = on;
    if (!on) {
        for (int v = 0; v < MAX_VOICES; v++) {
            if (va->voices[v].active && va->voices[v].sustained) {
                va->voices[v].sustained = false;
                for (int t = 0; t < NUM_TONES; t++)
                    tone_note_off(&va->voices[v].tones[t]);
            }
        }
    }
}

void voice_set_bend(VoiceAllocator *va, int8_t semitones)
{
    va->pitch_bend_semi = semitones;
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *voice = &va->voices[v];
        if (!voice->active || !voice->patch) continue;
        for (int t = 0; t < NUM_TONES; t++) {
            if (!voice->tones[t].active) continue;
            wg_note_on(&voice->tones[t].wg,
                       &voice->patch->tone[t].wg,
                       voice->note, 0, semitones);
        }
    }
}

void voice_set_after(VoiceAllocator *va, uint8_t pressure)
{
    va->aftertouch_q15 = (int32_t)pressure * 32767 / 127;
}

void voice_render_sample(VoiceAllocator *va,
                         int32_t *out_l, int32_t *out_r)
{
    int32_t sum_l = 0, sum_r = 0;
    int count = 0;

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *voice = &va->voices[v];
        if (!voice->active) continue;

        int32_t vl = 0, vr = 0;
        bool any_active = false;

        for (int t = 0; t < NUM_TONES; t++) {
            if (!voice->tones[t].active) continue;
            any_active = true;

            int32_t tl = 0, tr = 0;
            tone_tick(&voice->tones[t], va->aftertouch_q15, &tl, &tr);
            vl += tl;
            vr += tr;
        }

        /* Apply patch level */
        if (voice->patch) {
            int32_t pl = (int32_t)voice->patch->level * 32767 / 100;
            vl = Q15_MUL(vl, pl);
            vr = Q15_MUL(vr, pl);
        }

        sum_l += CLAMP(vl, -32767, 32767);
        sum_r += CLAMP(vr, -32767, 32767);
        count++;

        if (!any_active) {
            voice->active = false;
        }
    }

    /* Fixed headroom reduction — leaves room for chords without saturating.
     * Soft-clip in kernel.cpp handles any remaining overload. */
    *out_l = CLAMP(sum_l / 2, -32767, 32767);
    *out_r = CLAMP(sum_r / 2, -32767, 32767);
}
