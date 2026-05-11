#ifndef VOICE_H
#define VOICE_H

#include "../include/types.h"
#include "tone.h"
#include "patch.h"

#ifndef MAX_VOICES
#define MAX_VOICES  24
#endif

typedef struct {
    ToneState tones[NUM_TONES]; /* 1 tone per voice for rompler */
    uint8_t   note;
    uint8_t   velocity;
    bool      active;
    bool      sustained;        /* held by sustain pedal */
    uint32_t  age;              /* for voice stealing (older = higher) */
    const PatchParams *patch;
} Voice;

typedef struct {
    Voice    voices[MAX_VOICES];
    uint32_t voice_clock;       /* increment for age tracking */
    bool     sustain_pedal;
    int8_t   pitch_bend_semi;   /* current pitch bend in semitones */
    int32_t  aftertouch_q15;    /* channel aftertouch Q15 */
    int32_t  gain_smooth_q15;   /* IIR-smoothed voice gain to avoid clicks */
} VoiceAllocator;

void voice_alloc_init (VoiceAllocator *va);
void voice_note_on    (VoiceAllocator *va, const PatchParams *patch,
                       uint8_t note, uint8_t velocity);
void voice_note_off   (VoiceAllocator *va, uint8_t note);
void voice_all_off    (VoiceAllocator *va);
void voice_sustain    (VoiceAllocator *va, bool on);
void voice_set_bend   (VoiceAllocator *va, int8_t semitones);
void voice_set_after  (VoiceAllocator *va, uint8_t pressure);

/* Render one stereo sample, accumulate all active voices */
void voice_render_sample(VoiceAllocator *va,
                         int32_t *out_l, int32_t *out_r);

#endif
