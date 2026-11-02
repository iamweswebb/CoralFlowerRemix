/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors; ChoralRoot FM-1 contributors; Felucca by Leo
 * Kuroshita, Hügelton Instruments */
/* CLOUDRIFT: the instrument's state, shared by the player (cd_core.c, the audio ISR) and the UI (cd_ui.c). After the
 * owner's browser sim ("FM-1 Ambient Sim"): four tracks, each its own sound, length, divider, probability and swing,
 * in one of four modes (SEQ: its own steps; CHORD: the chord of the key played, voice-led; FOLLOW: the chord's root;
 * ARP: the arpeggiator over the chord), a diatonic chord in KEY from one key, eight arp modes, and the Cloud reverb
 * (cf_fx.c cl_*) as the one master effect, fed by each track's SEND.
 * The UI owns cd_set_t (cs); the player reads its own copy (cd_sync: IRQ off, a struct copy). */

#define CD_NTRK 4u
#define CD_NSTEP 32u
enum { CD_SEQ, CD_CHORD, CD_FOLLOW, CD_ARP, CD_NMODE };   /* a track's mode (the sim's plain, chord, follow, arp) */
enum { CD_ARP_UP, CD_ARP_DOWN, CD_ARP_UPDN, CD_ARP_RANDOM, CD_ARP_EUCLID, CD_ARP_DRIFT, CD_ARP_TURING, CD_ARP_BLOOM,
       CD_NARP };

typedef struct {
    uint8_t on;                  /* the step plays */
    uint8_t note;                /* a recorded MIDI note, 0: none (then the chord's tone below) */
    uint8_t tone;                /* the chord tone it plays (0: the root, 1 the next one up ...) */
} cd_step_t;

typedef struct {
    uint16_t snd;                /* the sound: its place in the one list of sounds (cd_sound.c) */
    uint8_t mode;                /* CD_SEQ .. CD_ARP */
    uint8_t len;                 /* steps 1..32 */
    uint8_t div;                 /* 0 1/4, 1 1/8, 2 1/16 */
    uint8_t prob;                /* a step's chance, 0..100 */
    uint8_t swing;               /* 0..100: the off-steps late by up to half a step */
    uint8_t send;                /* into the Cloud, 0..100 */
    uint8_t level;               /* 0..100 */
    int8_t octave;               /* -2..2 */
    uint8_t tone, env;           /* PATCH: the sound brighter or darker, its envelope shorter or longer (50: as made) */
    uint8_t rsv;
    cd_step_t steps[CD_NSTEP];
} cd_trk_t;

typedef struct {
    uint16_t tempo;              /* 40..160 BPM */
    uint8_t key;                 /* the major key's tonic, 0..11 (C..B) */
    uint8_t voices;              /* 6..8 */
    uint8_t space, cloud, bloom; /* the Cloud's macros 0..100 */
    uint8_t tilt, duck;          /* .. its TONE TILT and DUCK (GLO) */
    uint8_t arp_mode, arp_rate, arp_range, arp_chaos, arp_src;   /* rate 0 1/4 .. 3 1/32; range 1..4 octaves; src 0
                                                                 * the chord, 1 the keys held */
    uint8_t track;               /* the selected track */
    uint8_t midi_out;            /* the tracks' notes out on channels 1..4, and the clock */
    uint8_t rsv[3];
    cd_trk_t t[CD_NTRK];
} cd_set_t;

#define CD_SET_MAGIC 0x31524443u /* "CDR1" */
#define CD_SET_VERSION 1u

/* the sim's default setup: T1 a chord pad (two hits a bar), T2 a melody of its own (12 steps), T3 the bass following
 * the chord (7 steps), T4 the arpeggiator (5 steps): they drift against each other */
static void cd_steps(cd_trk_t *t, const uint8_t *on, uint32_t n, const uint8_t *tones)
{
    uint32_t i, k;
    for (i = 0; i < CD_NSTEP; i++) {
        t->steps[i].on = 0;
        t->steps[i].note = 0;
        t->steps[i].tone = (uint8_t)(i % 4u);
        for (k = 0; k < n; k++)
            if (on[k] == i) {
                t->steps[i].on = 1;
                t->steps[i].tone = tones ? tones[k] : (uint8_t)(i % 4u);
            }
    }
}
static void cd_trk_set(cd_trk_t *t, uint32_t mode, uint32_t len, uint32_t prob, uint32_t swing, uint32_t send,
                       uint32_t level, int octave, uint32_t tone, uint32_t env)
{
    t->mode = (uint8_t)mode;
    t->len = (uint8_t)len;
    t->div = 2;
    t->prob = (uint8_t)prob;
    t->swing = (uint8_t)swing;
    t->send = (uint8_t)send;
    t->level = (uint8_t)level;
    t->octave = (int8_t)octave;
    t->tone = (uint8_t)tone;
    t->env = (uint8_t)env;
    t->rsv = 0;
}
static void cd_defaults(cd_set_t *s)
{
    static const uint8_t T1[] = {0, 8}, T2[] = {0, 3, 5, 8, 10}, T2T[] = {4, 2, 3, 1, 2}, T3[] = {0, 4};
    uint32_t i;
    s->tempo = 72;
    s->key = 0;
    s->voices = 6;                               /* (the FM-1's load: 6 is safe, 7 and 8 on GLOBAL) */
    s->space = 75;
    s->cloud = 45;
    s->bloom = 60;
    s->tilt = 45;
    s->duck = 35;
    s->arp_mode = CD_ARP_EUCLID;
    s->arp_rate = 2;
    s->arp_range = 2;
    s->arp_chaos = 35;
    s->arp_src = 0;
    s->track = 0;
    s->midi_out = 1;
    s->rsv[0] = s->rsv[1] = s->rsv[2] = 0;
    cd_trk_set(&s->t[0], CD_CHORD, 16, 100, 0, 70, 70, 0, 50, 60);
    cd_steps(&s->t[0], T1, 2, 0);
    cd_trk_set(&s->t[1], CD_SEQ, 12, 85, 10, 55, 55, 1, 45, 45);
    cd_steps(&s->t[1], T2, 5, T2T);
    cd_trk_set(&s->t[2], CD_FOLLOW, 7, 100, 0, 12, 75, -1, 40, 50);
    cd_steps(&s->t[2], T3, 2, 0);
    cd_trk_set(&s->t[3], CD_ARP, 5, 100, 0, 85, 50, 1, 50, 50);
    for (i = 0; i < CD_NSTEP; i++) {
        s->t[3].steps[i].on = 1;
        s->t[3].steps[i].note = 0;
        s->t[3].steps[i].tone = (uint8_t)(i % 4u);
    }
    for (i = 0; i < CD_NTRK; i++)
        s->t[i].snd = 0xFFFFu;   /* (the sound: cd_ui_init names the default ones) */
}
