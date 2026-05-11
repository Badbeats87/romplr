/*
 * MIDI Bank Auto-Program: When CC0 (Bank Select) is received,
 * automatically send Program Change 0 after it so the sound
 * changes immediately.
 *
 * Sits as ALSA seq filter between controller and FluidSynth.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <alsa/asoundlib.h>
#include <alsa/seq.h>

static volatile int running = 1;

static void sighandler(int sig) {
    (void)sig;
    running = 0;
}

int main(void) {
    snd_seq_t *seq;
    int port;
    int err;

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    setbuf(stdout, NULL);

    err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
    if (err < 0) {
        fprintf(stderr, "Can't open sequencer: %s\n", snd_strerror(err));
        return 1;
    }

    snd_seq_set_client_name(seq, "BankFix");

    /* Create input port (writable = other clients can send to us) */
    port = snd_seq_create_simple_port(seq, "in",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) {
        fprintf(stderr, "Can't create input port\n");
        return 1;
    }

    /* Create output port (readable = other clients can read from us) */
    int oport = snd_seq_create_simple_port(seq, "out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (oport < 0) {
        fprintf(stderr, "Can't create output port\n");
        return 1;
    }

    /* Auto-connect to all MIDI hardware inputs */
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int cid = snd_seq_client_info_get_client(cinfo);
        if (cid == snd_seq_client_id(seq)) continue;
        if (snd_seq_client_info_get_type(cinfo) != SND_SEQ_KERNEL_CLIENT) continue;

        snd_seq_port_info_set_client(pinfo, cid);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if ((caps & SND_SEQ_PORT_CAP_READ) && (caps & SND_SEQ_PORT_CAP_SUBS_READ)) {
                snd_seq_addr_t src;
                src.client = cid;
                src.port = snd_seq_port_info_get_port(pinfo);
                snd_seq_port_subscribe_t *sub;
                snd_seq_port_subscribe_alloca(&sub);
                snd_seq_addr_t dst = { snd_seq_client_id(seq), port };
                snd_seq_port_subscribe_set_sender(sub, &src);
                snd_seq_port_subscribe_set_dest(sub, &dst);
                err = snd_seq_subscribe_port(seq, sub);
                if (err >= 0) {
                    printf("Connected from %d:%d\n", cid, src.port);
                } else {
                    printf("Failed to connect from %d:%d: %s\n", cid, src.port, snd_strerror(err));
                }
            }
        }
    }

    /* Auto-connect our output to FluidSynth's input */
    printf("Looking for FluidSynth...\n");
    for (;;) {
        snd_seq_client_info_set_client(cinfo, -1);
        while (snd_seq_query_next_client(seq, cinfo) >= 0) {
            int cid = snd_seq_client_info_get_client(cinfo);
            if (cid == snd_seq_client_id(seq)) continue;
            const char *cname = snd_seq_client_info_get_name(cinfo);
            if (strstr(cname, "FLUID") == NULL) continue;

            snd_seq_port_info_set_client(pinfo, cid);
            snd_seq_port_info_set_port(pinfo, -1);
            while (snd_seq_query_next_port(seq, pinfo) >= 0) {
                unsigned int pcaps = snd_seq_port_info_get_capability(pinfo);
                if (pcaps & SND_SEQ_PORT_CAP_WRITE) {
                    snd_seq_addr_t src = { snd_seq_client_id(seq), oport };
                    snd_seq_addr_t fdst;
                    fdst.client = cid;
                    fdst.port = snd_seq_port_info_get_port(pinfo);
                    snd_seq_port_subscribe_t *fsub;
                    snd_seq_port_subscribe_alloca(&fsub);
                    snd_seq_port_subscribe_set_sender(fsub, &src);
                    snd_seq_port_subscribe_set_dest(fsub, &fdst);
                    if (snd_seq_subscribe_port(seq, fsub) >= 0) {
                        printf("Connected to FluidSynth %d:%d\n", cid, fdst.port);
                        goto connected;
                    }
                }
            }
        }
        usleep(200000);
    }
connected:

    printf("BankFix running (CC0 -> auto Program Change 0)\n");

    snd_seq_event_t *ev;
    snd_seq_event_t out_ev;

    while (running) {
        err = snd_seq_event_input(seq, &ev);
        if (err < 0) break;

        /* Filter out aftertouch (causes pitch changes in some presets) */
        if (ev->type == SND_SEQ_EVENT_CHANPRESS ||
            ev->type == SND_SEQ_EVENT_KEYPRESS) {
            snd_seq_free_event(ev);
            continue;
        }

        /* Forward the event as-is */
        snd_seq_ev_clear(&out_ev);
        out_ev = *ev;
        snd_seq_ev_set_source(&out_ev, oport);
        snd_seq_ev_set_subs(&out_ev);
        snd_seq_ev_set_direct(&out_ev);
        snd_seq_event_output_direct(seq, &out_ev);

        /* If it's CC0 (Bank Select MSB), also send Program Change 0 */
        if (ev->type == SND_SEQ_EVENT_CONTROLLER && ev->data.control.param == 0) {
            printf("Bank %d on ch %d -> auto PC 0\n",
                   ev->data.control.value, ev->data.control.channel);

            snd_seq_ev_clear(&out_ev);
            snd_seq_ev_set_source(&out_ev, oport);
            snd_seq_ev_set_subs(&out_ev);
            snd_seq_ev_set_direct(&out_ev);
            out_ev.type = SND_SEQ_EVENT_PGMCHANGE;
            out_ev.data.control.channel = ev->data.control.channel;
            out_ev.data.control.value = 0;
            snd_seq_event_output_direct(seq, &out_ev);
        }

        snd_seq_free_event(ev);
    }

    snd_seq_close(seq);
    printf("BankFix stopped\n");
    return 0;
}
