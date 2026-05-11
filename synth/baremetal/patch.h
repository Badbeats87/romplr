#ifndef PATCH_H
#define PATCH_H

#include "../include/types.h"

/* ═══════════════════════════════════════════════════════════
 * Rompler Patch Data Structures
 * Adapted from JD-800 — single tone per voice, WAV samples
 * ═══════════════════════════════════════════════════════════ */

/* --- LFO waveforms --- */
#define LFO_WAVE_TRI        0
#define LFO_WAVE_SINE       1
#define LFO_WAVE_SAW        2
#define LFO_WAVE_SQUARE     3
#define LFO_WAVE_RANDOM     4
#define LFO_WAVE_SH         5   /* sample & hold */

/* --- TVF filter modes --- */
#define TVF_MODE_LPF        0
#define TVF_MODE_BPF        1
#define TVF_MODE_HPF        2
#define TVF_MODE_PKG        3   /* peaking/notch */

#ifndef NUM_PATCHES
#define NUM_PATCHES         128  /* max instruments loadable */
#endif
#ifndef NUM_TONES
#define NUM_TONES           1   /* single tone per voice for rompler */
#endif

/* --- Reverb types --- */
#define REVERB_ROOM1        0
#define REVERB_ROOM2        1
#define REVERB_STAGE1       2
#define REVERB_STAGE2       3
#define REVERB_HALL1        4
#define REVERB_HALL2        5

/* ── Per-Tone Envelope (5-segment) ────────────────────────── */
typedef struct {
    uint8_t  time[5];   /* T1–T5: 0=instant, 127=very long */
    uint8_t  level[4];  /* L1–L4: 0–127 (L0=0, L5=0 implicit) */
    uint8_t  time_kf;   /* time key-follow: 0–14 */
    uint8_t  vel_sens;  /* velocity sensitivity: 0–3 */
} Envelope;

/* ── LFO ───────────────────────────────────────────────────── */
typedef struct {
    uint8_t  wave;          /* LFO_WAVE_* */
    uint8_t  rate;          /* 0–127 */
    uint8_t  delay;         /* 0–127: delay before LFO starts */
    uint8_t  fade;          /* 0–127: fade-in time */
    uint8_t  offset;        /* 0=center, 1=+, 2=- */
    uint8_t  pitch_depth;   /* 0–127 */
    uint8_t  tvf_depth;     /* 0–127 */
    uint8_t  tva_depth;     /* 0–127 */
    uint8_t  key_trigger;   /* 0=free-run, 1=retrigger on note */
} LFOParams;

/* ── Wave Generator ─────────────────────────────────────────── */
typedef struct {
    uint8_t  inst_idx;      /* instrument index in sample bank */
    uint8_t  wave_gain;     /* 0–3: ×0.5, ×1, ×2, ×4 */
    int8_t   pitch_coarse;  /* −48 to +48 semitones */
    int8_t   pitch_fine;    /* −50 to +50 cents */
    uint8_t  pitch_kf;      /* key-follow: 0–14 (7=1:1) */
    uint8_t  bender_sw;     /* 0=off, 1=on */
    uint8_t  bender_range;  /* 0–48 semitones */
    uint8_t  fxm_sw;        /* unused in rompler */
    uint8_t  fxm_color;     /* unused */
    uint8_t  fxm_depth;     /* unused */
    Envelope env;           /* pitch envelope */
    uint8_t  env_depth;     /* pitch env depth −63..+63 stored as 0–126 */
} WGParams;

/* ── Time Variant Filter ────────────────────────────────────── */
typedef struct {
    uint8_t  mode;          /* TVF_MODE_* */
    uint8_t  cutoff;        /* 0–127 */
    uint8_t  resonance;     /* 0–127 */
    uint8_t  kf;            /* key-follow (cutoff tracks pitch) 0–14 */
    int8_t   bias_point;
    int8_t   bias_level;
    uint8_t  env_depth;     /* 0–127 (centre=64 = no depth) */
    Envelope env;
} TVFParams;

/* ── Time Variant Amplifier ─────────────────────────────────── */
typedef struct {
    uint8_t  level;         /* 0–100 */
    uint8_t  vel_sens;      /* 0–3 */
    int8_t   pan;           /* −63 to +63 (L=neg, R=pos) */
    uint8_t  rand_pan;      /* random pan depth 0–63 */
    Envelope env;
} TVAParams;

/* ── Single Tone ────────────────────────────────────────────── */
typedef struct {
    uint8_t  sw;            /* tone on/off */
    uint8_t  env_mode;      /* 0=no sustain, 1=sustain */
    WGParams  wg;
    TVFParams tvf;
    TVAParams tva;
    LFOParams lfo;
} ToneParams;

/* ── Patch (single tone + common) ─────────────────────────── */
typedef struct {
    char     name[16];      /* null-terminated patch name */

    /* Common */
    uint8_t  level;         /* 0–100 */
    int8_t   transpose;     /* −24 to +24 semitones */
    uint8_t  fine_tune;     /* 0–100 (50 = centre) */
    uint8_t  bender_mode;
    uint8_t  structure;
    uint8_t  solo_sw;
    uint8_t  portamento_sw;
    uint8_t  portamento_time;

    /* NUM_TONES tones (1 for rompler) */
    ToneParams tone[NUM_TONES];

    /* Chorus */
    uint8_t  chorus_sw;
    uint8_t  chorus_rate;
    uint8_t  chorus_depth;
    uint8_t  chorus_delay;
    uint8_t  chorus_feedback;
    uint8_t  chorus_level;

    /* Reverb */
    uint8_t  reverb_sw;
    uint8_t  reverb_type;
    uint8_t  reverb_time;
    uint8_t  reverb_predelay;
    uint8_t  reverb_level;

    /* EQ (3-band) */
    int8_t   eq_low;
    int8_t   eq_mid;
    int8_t   eq_high;
    uint8_t  eq_mid_freq;
} PatchParams;

#endif /* PATCH_H */
