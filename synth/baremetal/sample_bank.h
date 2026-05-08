#ifndef SAMPLE_BANK_H
#define SAMPLE_BANK_H

#include "../include/types.h"

/* ═══════════════════════════════════════════════════════════
 * Sample Bank — WAV loader + instrument.cfg parser
 *
 * Loads 16-bit 44100Hz mono WAV files from SD card.
 * Each instrument has multiple key zones defined in
 * instrument.cfg.
 *
 * SD card layout:
 *   SD:/instruments/piano/
 *     instrument.cfg
 *     C2.wav
 *     C3.wav
 *     ...
 * ═══════════════════════════════════════════════════════════ */

#define MAX_INSTRUMENTS     128
#define MAX_ZONES           16
#define SAMPLE_POOL_SAMPLES (512 * 1024 * 1024)  /* 512M samples = 1GB */

/* One key zone within an instrument multisample */
typedef struct {
    const int16_t *pcm;     /* pointer into sample pool */
    uint32_t length;        /* total sample count */
    uint8_t  low_key;       /* lowest MIDI note for this zone */
    uint8_t  high_key;      /* highest MIDI note for this zone */
    uint8_t  root_key;      /* MIDI note at original pitch */
    uint8_t  volume;        /* 0–100 */
    uint32_t loop_start;    /* loop start sample (0 = no loop) */
    uint32_t loop_end;      /* loop end sample (0 = loop to length) */
    int8_t   fine_tune;     /* fine tuning in cents */
    uint8_t  pad[3];
} SampleZone;

/* One loaded instrument */
typedef struct {
    char       name[32];
    SampleZone zones[MAX_ZONES];
    int        num_zones;
    /* Default patch settings from cfg */
    uint8_t    filter_cutoff;    /* 0–127 */
    uint8_t    filter_resonance; /* 0–127 */
    uint8_t    reverb_level;     /* 0–127 */
    uint8_t    chorus_level;     /* 0–127 */
    uint8_t    attack;           /* 0–127 */
    uint8_t    release;          /* 0–127 */
} Instrument;

/* The global sample bank */
typedef struct {
    Instrument instruments[MAX_INSTRUMENTS];
    int        num_instruments;
    uint32_t   pool_used;        /* samples used in pool */
} SampleBank;

/* Initialise: scan + sort SD:/instruments/, load first instrument only.
 * Remaining instruments are loaded via sample_bank_load_next_pending(). */
void sample_bank_init(SampleBank *sb);

/* Load one pending instrument. Returns 1 if more remain, 0 if done. */
int sample_bank_load_next_pending(SampleBank *sb);

/* Get number of instruments still waiting to load */
int sample_bank_pending_count(void);

/* Load a single instrument directory. Returns instrument index or -1. */
int sample_bank_load_instrument(SampleBank *sb, const char *dir_path);

/* Find the zone matching a MIDI note for an instrument.
 * Returns pointer to zone, or NULL if no match. */
const SampleZone *sample_bank_find_zone(const SampleBank *sb,
                                         int inst_idx, uint8_t note);

/* Get pointer to global sample pool */
int16_t *sample_bank_pool(void);

/* Get number of loaded instruments */
int sample_bank_count(const SampleBank *sb);

/* Set a callback that fires during SD reads so MIDI stays responsive */
void sample_bank_set_poll_cb(void (*cb)(void));

#endif /* SAMPLE_BANK_H */
