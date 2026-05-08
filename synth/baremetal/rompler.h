#ifndef ROMPLER_H
#define ROMPLER_H

#include "../include/types.h"
#include "patch.h"
#include "voice.h"
#include "effects.h"
#include "midi.h"
#include "sample_bank.h"

typedef struct {
    SampleBank       sample_bank;
    PatchParams      patches[NUM_PATCHES];
    int              current_patch;
    VoiceAllocator   voices;
    EffectsState     effects;
    MidiParser       midi;
    uint8_t          midi_channel;  /* 0–15, or 16=omni */
} Rompler;

void rompler_init   (Rompler *r);      /* init tables + load first instrument from SD */
int  rompler_load_next_pending(Rompler *r);  /* load next instrument + build patch. Returns 1 if more, 0 if done */
void rompler_patch  (Rompler *r, int patch_num);
void rompler_midi   (Rompler *r, uint8_t byte);
void rompler_render (Rompler *r, int32_t *out_l, int32_t *out_r);

#endif
