#ifndef MIDI_H
#define MIDI_H

#include "../include/types.h"

/* MIDI parser — feeds bytes one at a time, calls callbacks */

typedef struct {
    uint8_t status;         /* current status byte */
    uint8_t data[2];        /* data bytes */
    uint8_t data_count;     /* bytes received for current message */
    uint8_t expected;       /* bytes expected for this message */
    uint8_t channel;        /* current MIDI channel (0–15) */

    /* Callbacks (set by application) */
    void (*on_note_on)    (uint8_t ch, uint8_t note, uint8_t vel);
    void (*on_note_off)   (uint8_t ch, uint8_t note, uint8_t vel);
    void (*on_cc)         (uint8_t ch, uint8_t cc,   uint8_t val);
    void (*on_pitch_bend) (uint8_t ch, int16_t value);  /* -8192..8191 */
    void (*on_aftertouch) (uint8_t ch, uint8_t pressure);
    void (*on_program)    (uint8_t ch, uint8_t prog);
} MidiParser;

void midi_init(MidiParser *m);
void midi_feed(MidiParser *m, uint8_t byte);

#endif
