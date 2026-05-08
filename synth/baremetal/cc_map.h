#ifndef CC_MAP_H
#define CC_MAP_H

/* ═══════════════════════════════════════════════════════════════
 * MIDI CC → Rompler Parameter Mapping
 * Uses standard MIDI CC numbers (same as Linux version).
 * ═══════════════════════════════════════════════════════════════ */

/* ── Standard MIDI CCs ──────────────────────────────────────── */
#define CC_VOLUME               7
#define CC_PORTAMENTO           5
#define CC_SUSTAIN              64
#define CC_ALL_SOUND_OFF        120
#define CC_ALL_NOTES_OFF        123

/* ── Sound Control CCs (GM2 standard) ───────────────────────── */
#define CC_SUSTAIN_LEVEL        70
#define CC_RESONANCE            71
#define CC_RELEASE              72
#define CC_ATTACK               73
#define CC_CUTOFF               74
#define CC_DECAY                75

/* ── LFO CCs ────────────────────────────────────────────────── */
#define CC_LFO_RATE             76
#define CC_LFO_FILTER           77
#define CC_LFO_PITCH            78
#define CC_LFO_AMP              79
#define CC_LFO_WAVE             80

/* ── Effects CCs ────────────────────────────────────────────── */
#define CC_FILTER_ENV           82
#define CC_REVERB               91
#define CC_CHORUS               93

#endif /* CC_MAP_H */
