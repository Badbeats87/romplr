#include "midi.h"
#include "../include/types.h"

static uint8_t midi_data_len(uint8_t status)
{
    uint8_t type = status & 0xF0;
    switch (type) {
    case 0x80: return 2;
    case 0x90: return 2;
    case 0xA0: return 2;
    case 0xB0: return 2;
    case 0xC0: return 1;
    case 0xD0: return 1;
    case 0xE0: return 2;
    case 0xF0: return 0;
    default:   return 0;
    }
}

void midi_init(MidiParser *m)
{
    m->status      = 0;
    m->data[0]     = m->data[1] = 0;
    m->data_count  = 0;
    m->expected    = 0;
    m->channel     = 0;
    m->on_note_on    = 0;
    m->on_note_off   = 0;
    m->on_cc         = 0;
    m->on_pitch_bend = 0;
    m->on_aftertouch = 0;
    m->on_program    = 0;
}

static void midi_dispatch(MidiParser *m)
{
    uint8_t type = m->status & 0xF0;
    uint8_t ch   = m->status & 0x0F;

    switch (type) {
    case 0x80:
        if (m->on_note_off)
            m->on_note_off(ch, m->data[0], m->data[1]);
        break;

    case 0x90:
        if (m->data[1] == 0) {
            if (m->on_note_off)
                m->on_note_off(ch, m->data[0], 0);
        } else {
            if (m->on_note_on)
                m->on_note_on(ch, m->data[0], m->data[1]);
        }
        break;

    case 0xA0:
        if (m->on_aftertouch)
            m->on_aftertouch(ch, m->data[1]);
        break;

    case 0xB0:
        if (m->on_cc)
            m->on_cc(ch, m->data[0], m->data[1]);
        break;

    case 0xC0:
        if (m->on_program)
            m->on_program(ch, m->data[0]);
        break;

    case 0xD0:
        if (m->on_aftertouch)
            m->on_aftertouch(ch, m->data[0]);
        break;

    case 0xE0: {
        int16_t bend = (int16_t)((m->data[1] << 7) | m->data[0]) - 8192;
        if (m->on_pitch_bend)
            m->on_pitch_bend(ch, bend);
        break;
    }

    default:
        break;
    }
}

void midi_feed(MidiParser *m, uint8_t byte)
{
    if (byte >= 0xF8) return;

    if (byte & 0x80) {
        if (byte == 0xF0) {
            m->status = 0xF0;
            m->expected = 0;
            return;
        }
        if (byte == 0xF7) {
            m->status = 0;
            return;
        }

        m->status     = byte;
        m->data_count = 0;
        m->expected   = midi_data_len(byte);
        return;
    }

    if (m->status == 0 || m->status == 0xF0) return;

    if (m->data_count < 2)
        m->data[m->data_count] = byte;
    m->data_count++;

    if (m->data_count >= m->expected) {
        midi_dispatch(m);
        m->data_count = 0;
    }
}
