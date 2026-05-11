/*
 * Teensy 4.1 Rompler — main entry point
 *
 * Hardware:
 *   - Teensy 4.1 (600 MHz Cortex-M7)
 *   - Audio Shield rev D (SGTL5000 I2S DAC)
 *   - USB Host cable (5 pads on bottom of Teensy 4.1)
 *   - SD card in Teensy 4.1 built-in SDIO slot
 *
 * Audio path:
 *   AudioSynthRompler → AudioOutputI2S → SGTL5000 → headphones/line out
 *
 * MIDI:
 *   USB Host MIDI keyboard → USBHost_t36 → rompler_midi()
 */
#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <USBHost_t36.h>

extern "C" {
#include "rompler.h"
#include "sample_bank.h"
#ifdef STREAM_FROM_SD
#include "stream.h"
#endif
}

/* SD wrapper init (sd_wrapper.cpp) */
extern bool sd_wrapper_init(void);

/* ═══════════════════════════════════════════════════════════════
 * Audio output — custom AudioStream that renders from the synth
 * ═══════════════════════════════════════════════════════════════ */

class AudioSynthRompler : public AudioStream {
public:
    AudioSynthRompler(void) : AudioStream(0, NULL), m_rompler(NULL) {}

    void begin(Rompler *r) { m_rompler = r; }

    virtual void update(void)
    {
        if (!m_rompler) return;

        audio_block_t *blk_l = allocate();
        if (!blk_l) return;
        audio_block_t *blk_r = allocate();
        if (!blk_r) { release(blk_l); return; }

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            int32_t raw_l = 0, raw_r = 0;
            rompler_render(m_rompler, &raw_l, &raw_r);

            /* Bhaskara tanh soft-clip (same as bare-metal kernel.cpp).
             * Scales Q15 to ±3.0, applies tanh approximation.
             * tanh(x) ≈ x·(27+x²) / (27+9·x²)  */
            int16_t out[2];
            for (int ch = 0; ch < 2; ch++) {
                int32_t s = (ch == 0) ? raw_l : raw_r;
                int64_t x3  = (int64_t)s * 3;
                int64_t x2  = (x3 * x3) >> 15;
                int64_t num = x3 * (884736LL + x2);
                int64_t den = 884736LL + 9 * x2;
                int32_t clipped = (den != 0) ? (int32_t)(num / den) : 0;
                out[ch] = (int16_t)constrain(clipped, -32767, 32767);
            }

            blk_l->data[i] = out[0];
            blk_r->data[i] = out[1];
        }

        transmit(blk_l, 0);
        transmit(blk_r, 1);
        release(blk_l);
        release(blk_r);
    }

private:
    Rompler *m_rompler;
};

/* ═══════════════════════════════════════════════════════════════
 * Audio graph
 * ═══════════════════════════════════════════════════════════════ */

AudioSynthRompler     synth_stream;
AudioOutputI2S        i2s_out;
AudioConnection       patch_L(synth_stream, 0, i2s_out, 0);
AudioConnection       patch_R(synth_stream, 1, i2s_out, 1);
AudioControlSGTL5000  sgtl5000;

/* ═══════════════════════════════════════════════════════════════
 * USB Host MIDI
 * ═══════════════════════════════════════════════════════════════ */

USBHost   usb_host;
MIDIDevice midi_dev(usb_host);

/* ═══════════════════════════════════════════════════════════════
 * Rompler instance (lives in DTCM / RAM1)
 * ═══════════════════════════════════════════════════════════════ */

#ifdef STREAM_FROM_SD
/* With streaming, SampleBank holds metadata for all 128 instruments
 * (~320 KB).  Move Rompler to RAM2 (DMAMEM) since DTCM is only 512 KB.
 * RAM2 is D-cached at 600 MHz — fast enough for audio rendering. */
DMAMEM static Rompler rompler;
#else
static Rompler rompler;
#endif

/* ═══════════════════════════════════════════════════════════════
 * MIDI helpers — feed parsed USB MIDI messages as raw bytes
 * into the synth's MIDI parser.  Interrupts are disabled to
 * prevent the audio callback from reading voice state mid-update.
 * ═══════════════════════════════════════════════════════════════ */

static void process_midi_messages(void)
{
    while (midi_dev.read()) {
        uint8_t type = midi_dev.getType();
        uint8_t ch   = midi_dev.getChannel() - 1;  /* 1-based → 0-based */
        uint8_t d1   = midi_dev.getData1();
        uint8_t d2   = midi_dev.getData2();

        uint8_t status = type | (ch & 0x0F);

        __disable_irq();
        rompler_midi(&rompler, status);
        rompler_midi(&rompler, d1);
        /* Program Change (0xC0) and Channel Pressure (0xD0) are 2-byte */
        if (type != 0xC0 && type != 0xD0)
            rompler_midi(&rompler, d2);
        __enable_irq();
    }
}

/* MIDI poll callback — called from sample_bank.c between SD read
 * chunks to keep MIDI responsive during instrument loading. */
extern "C" void midi_poll_callback(void)
{
    usb_host.Task();
    process_midi_messages();
}

/* ═══════════════════════════════════════════════════════════════
 * Arduino setup / loop
 * ═══════════════════════════════════════════════════════════════ */

#define LED_PIN 13

void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    Serial.begin(115200);
    delay(500);  /* let serial settle */

    Serial.println("========================================");
    Serial.println("  ROMPLER — Teensy 4.1");
    Serial.printf ("  Compiled %s %s\n", __DATE__, __TIME__);
    Serial.printf ("  CPU: %u MHz\n", F_CPU / 1000000);
    Serial.println("========================================");

    /* Audio system — 12 blocks ≈ 35 ms buffer at 44100 Hz */
    AudioMemory(12);
    sgtl5000.enable();
    sgtl5000.volume(0.7f);
    Serial.println("Audio: I2S + SGTL5000 OK");

    /* USB Host */
    usb_host.begin();
    Serial.println("USB Host: started");

    /* SD card */
    if (!sd_wrapper_init()) {
        Serial.println("*** SD card init failed — no instruments ***");
    }

    /* Set MIDI poll callback before init (so SD reads stay responsive) */
    sample_bank_set_poll_cb(midi_poll_callback);

#ifdef STREAM_FROM_SD
    stream_init();
#endif

    /* Init synth engine — scans SD, loads first instrument */
    Serial.println("Loading instruments...");
    rompler_init(&rompler);

    /* Connect synth to audio output */
    synth_stream.begin(&rompler);

    Serial.printf("Ready: %d instrument(s), %d pending\n",
                  rompler.sample_bank.num_instruments,
                  sample_bank_pending_count());
}

static bool     s_loading = true;
static uint32_t s_heartbeat = 0;

/* ═══════════════════════════════════════════════════════════════
 * Serial test commands — play notes without USB Host cable.
 * Open serial monitor at 115200 and type:
 *   n60      → note on  (middle C, velocity 100)
 *   n60,127  → note on  (middle C, velocity 127)
 *   o60      → note off (middle C)
 *   p5       → program change to patch 5
 *   x        → all notes off
 *   ?        → print status
 * ═══════════════════════════════════════════════════════════════ */

static void process_serial_commands(void)
{
    static char cmd_buf[32];
    static int  cmd_pos = 0;

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmd_pos == 0) continue;
            cmd_buf[cmd_pos] = '\0';
            cmd_pos = 0;

            switch (cmd_buf[0]) {
            case 'n': case 'N': {
                /* Note on: "n60" or "n60,127" */
                int note = atoi(&cmd_buf[1]);
                int vel = 100;
                char *comma = strchr(cmd_buf, ',');
                if (comma) vel = atoi(comma + 1);
                note = constrain(note, 0, 127);
                vel  = constrain(vel, 1, 127);
                __disable_irq();
                rompler_midi(&rompler, 0x90);  /* note on, ch 0 */
                rompler_midi(&rompler, (uint8_t)note);
                rompler_midi(&rompler, (uint8_t)vel);
                __enable_irq();
                Serial.printf("Note ON: %d vel %d\n", note, vel);
                break;
            }
            case 'o': case 'O': {
                /* Note off: "o60" */
                int note = atoi(&cmd_buf[1]);
                note = constrain(note, 0, 127);
                __disable_irq();
                rompler_midi(&rompler, 0x80);  /* note off, ch 0 */
                rompler_midi(&rompler, (uint8_t)note);
                rompler_midi(&rompler, 0);
                __enable_irq();
                Serial.printf("Note OFF: %d\n", note);
                break;
            }
            case 'p': case 'P': {
                /* Program change: "p5" */
                int prog = atoi(&cmd_buf[1]);
                prog = constrain(prog, 0, 127);
                __disable_irq();
                rompler_midi(&rompler, 0xC0);
                rompler_midi(&rompler, (uint8_t)prog);
                __enable_irq();
                Serial.printf("Program: %d\n", prog);
                break;
            }
            case 'x': case 'X':
                /* All notes off */
                __disable_irq();
                rompler_midi(&rompler, 0xB0);
                rompler_midi(&rompler, 123);
                rompler_midi(&rompler, 0);
                __enable_irq();
                Serial.println("All notes off");
                break;
            case '?':
                Serial.printf("Instruments: %d, Pool: %lu KB, Patch: %d\n",
                    rompler.sample_bank.num_instruments,
                    (unsigned long)(rompler.sample_bank.pool_used * 2 / 1024),
                    rompler.current_patch);
                Serial.printf("CPU: %.1f%%, Mem: %d blocks\n",
                    AudioProcessorUsage(), AudioMemoryUsage());
#ifdef STREAM_FROM_SD
                Serial.printf("Stream underruns: %lu\n",
                    (unsigned long)stream_underrun_count());
#endif
                break;
            default:
                Serial.println("Commands: n60 o60 p5 x ?");
                break;
            }
        } else if (cmd_pos < (int)sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_pos++] = c;
        }
    }
}

void loop()
{
#ifdef STREAM_FROM_SD
    /* Refill SD stream caches for active voices */
    stream_refill_all(&rompler);
#endif

    /* Poll USB Host for new devices / transfers */
    usb_host.Task();

    /* Process any pending MIDI messages */
    process_midi_messages();

    /* Serial test commands (works without USB Host cable) */
    process_serial_commands();

#ifdef STREAM_FROM_SD
    /* Process pending stream opens immediately after MIDI/serial
     * note-on events — minimizes initial cache-miss underruns */
    stream_refill_all(&rompler);
#endif

    /* Lazy-load one pending instrument per loop iteration */
    if (s_loading) {
        if (!rompler_load_next_pending(&rompler)) {
            s_loading = false;
            Serial.printf("All %d instruments loaded (%lu KB samples)\n",
                          rompler.sample_bank.num_instruments,
                          (unsigned long)(rompler.sample_bank.pool_used * 2 / 1024));
        }
    }

    /* Heartbeat LED: toggle every 2 seconds */
    uint32_t now = millis();
    if (now - s_heartbeat >= 2000) {
        s_heartbeat = now;
        digitalToggle(LED_PIN);
    }
}
