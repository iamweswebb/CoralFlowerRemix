/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors; ChoralRoot FM-1 contributors */
/* CLOUDRIFT: the player, in the audio ISR (cd_audio_block at the top of every CTL-sample block, before fx.c mixes
 * it). A port of the owner's browser sim's Engine (its clock, tracks, chords, arpeggiator), in integers, a block at a
 * time (0.73 ms: the sim's per-sample timing, rounded to the block):
 *   the clock    16ths at TEMPO; each track ticks every DIVIDE of them, its own LENGTH long, so the tracks drift
 *   a key        the selected track SEQ: that note on it (REC: into its step); else the chord of the key in KEY's
 *                major scale (a 9th, or an 11th where the 9th leaves the scale; a note outside it: maj9), played on
 *                every CHORD track (voice-led: the inversion and octave nearest the last one), the root on every
 *                FOLLOW track, and the arpeggiator over it
 *   a step       CHORD: the chord, FOLLOW: its root, SEQ: the step's note or chord tone, each held to the next step
 *                that plays; ARP tracks' steps are its gate
 * The notes go to the parts (trk[0..3]: voice.c's trk_note_on / off) and out on USB MIDI, channels 1..4.
 * Also here: what cf_out.c gave the kept files (panic_req, events_block ...), the UI's queue, MIDI in, the snapshot. */

/* ---------------------------------------------- what seq.c provided --- */
static volatile uint8_t panic_req;               /* bit per part: its notes released (a sound load, M-UPGRADE) */
static const uint8_t transport_req = 0;
#define chain_defaults(c) ((void)0)              /* (main.c felucca_init: no song chain) */
#ifdef FM1_INPUT_LAT
static uint32_t kb_out_tick;
#endif
static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static int drum_track(const track_t *t) { (void)t; return 0; }
static int16_t motion_base_value(const track_t *t, uint32_t id) { return t->p[id]; }
static uint32_t motion_guard(void)
{
#if defined(FM1_IRQ_TARGET)
    uint32_t f = fm1_icfg();
    fm1_irq_off();
    return f;
#else
    return 0;
#endif
}
static void motion_unguard(uint32_t f)
{
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#else
    (void)f;
#endif
}
static void cd_notes_forget(uint32_t part);      /* (below: the player's notes on a part, gone with its voices) */
static void events_block(uint32_t n)             /* fx.c's, before each part renders: releases, engine switches */
{
    uint32_t i, pr = panic_req;
    (void)n;
    panic_req = 0;
    if (midi_in_overflow) {                      /* (a lost note-off: every part released, no stuck note) */
        mi_r = mi_w;
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        if ((pr >> i) & 1u) {
            trk_all_off(&trk[i]);
            cd_notes_forget(i);
        }
        engine_block(&trk[i]);
    }
}

/* ------------------------------------------------------------ the queue --- */
enum { CDE_KEY, CDE_PLAY, CDE_REC, CDE_PANIC, CDE_SYNC };   /* KEY: a note, v down; PLAY / REC: v on */
typedef struct { uint8_t op, a; int16_t v; } cd_ev_t;
#define CD_EVQ 64u
static cd_ev_t cd_evq[CD_EVQ];
static volatile uint32_t cd_evq_w, cd_evq_r;
static int cd_post(uint32_t op, uint32_t a, int32_t v)
{
    cd_ev_t *e;
    if (cd_evq_w - cd_evq_r >= CD_EVQ)
        return 0;
    e = &cd_evq[cd_evq_w % CD_EVQ];
    e->op = (uint8_t)op;
    e->a = (uint8_t)a;
    e->v = (int16_t)v;
    RING_PUBLISH();
    cd_evq_w++;
    return 1;
}
/* the player -> the UI: a note REC wrote into a step (the UI owns the steps: it writes it into cs and syncs) */
typedef struct { uint8_t trk, step, note; } cd_recev_t;
#define CD_RECQ 16u
static cd_recev_t cd_recq[CD_RECQ];
static volatile uint32_t cd_recq_w, cd_recq_r;

/* ------------------------------------------------------------ the state --- */
static const uint8_t CD_SCALE[7] = {0, 2, 4, 5, 7, 9, 11};
enum { CDQ_MAJ9, CDQ_MAJ11, CDQ_9, CDQ_11, CDQ_M7B5, CDQ_M9, CDQ_M11 };
#define CD_NCH 4u                                /* a chord's notes: the 9th or 11th chord with no fifth (the
                                                  * root, third, seventh and the top: its colour, in 4 of the voices) */
#define CD_NON 40u                               /* notes the player holds at once */
#define CD_NQ 24u                                /* steps waiting (swing) */
static struct {
    cd_set_t s;                                  /* the UI's settings, copied (cd_sync) */
    uint8_t ready, playing, rec;
    uint32_t t;                                  /* samples since power-on */
    uint32_t next16, frac16, sp16q;              /* the next 16th (samples, its Q8 remainder); a 16th, Q8 samples */
    uint32_t g;                                  /* 16ths since PLAY */
    uint32_t ticks[CD_NTRK];                     /* each track's own ticks since PLAY */
    int16_t step[CD_NTRK];                       /* .. the step it is on (-1: stopped) */
    uint8_t gate[CD_NTRK];                       /* .. an ARP track's step: on */
    struct { uint32_t at; uint8_t i, st, used; } q[CD_NQ];
    uint8_t held[8], nheld;                      /* the keys held (MIDI notes), in order */
    uint8_t lk_trk[128], lk_note[128];           /* a key held on a SEQ track: its track + 1, the note it plays */
    uint8_t root;                                /* the chord: its root (MIDI), quality, intervals */
    uint8_t q_;
    int8_t ints[CD_NCH];
    uint8_t has_chord;
    uint8_t prev[CD_NTRK][CD_NCH], prev_ok[CD_NTRK];   /* each track's last voicing (voice leading) */
    uint32_t arp_next, arp_i, euclid_n;
    uint8_t arp_running, reg, ndrift, drift[48];
    struct { uint32_t off; uint8_t trk, note, live, used, timed; } on[CD_NON];
    int32_t seed;
    uint32_t ck_next, ck_frac, ckq;              /* MIDI clock out: the next pulse (24 a beat), Q8 */
    uint16_t tempo_was;
} cde;

static uint32_t cd_rnd(void) { return noise32(&cde.seed); }
static uint32_t cd_r16(void) { return cd_rnd() >> 16; }          /* 0 .. 65535 */
static int cd_chance(uint32_t pct) { return cd_r16() < pct * 65536u / 100u; }   /* true pct % of the time */

static void cd_midi_note(uint32_t trk_i, uint32_t note, uint32_t vel)
{
    uint32_t st = (vel ? 0x90u : 0x80u) | (trk_i & 15u);
    if (cde.s.midi_out)
        midi_out_event((st >> 4) | st << 8 | (note & 0x7Fu) << 16 | (vel & 0x7Fu) << 24);
}

/* ------------------------------------------------------------ the notes --- */
static void cd_off_at(uint32_t k)
{
    trk_note_off(&trk[cde.on[k].trk], cde.on[k].note);
    cd_midi_note(cde.on[k].trk, cde.on[k].note, 0);
    cde.on[k].used = 0;
}
/* a note on track i: vel 1..127, dur samples (0: until it is let go), live: a key holds it */
static void cd_note_on(uint32_t i, int32_t note, uint32_t vel, uint32_t dur, int live)
{
    uint32_t k, free = CD_NON, old = 0, oldest = 0;
    if (note < 0 || note > 127)
        return;
    for (k = 0; k < CD_NON; k++) {
        if (!cde.on[k].used) {
            if (free == CD_NON)
                free = k;
            continue;
        }
        if (cde.on[k].trk == i && cde.on[k].note == (uint8_t)note) {
            free = k;                            /* (the same note again: it restarts, its time anew) */
            cd_midi_note(i, (uint32_t)note, 0);
            break;
        }
        if (!oldest || (int32_t)(cde.on[k].off - oldest) < 0) {
            oldest = cde.on[k].off;
            old = k;
        }
    }
    if (free == CD_NON) {                        /* (full: the oldest let go) */
        cd_off_at(old);
        free = old;
    }
    cde.on[free].used = 1;
    cde.on[free].trk = (uint8_t)i;
    cde.on[free].note = (uint8_t)note;
    cde.on[free].live = (uint8_t)(live != 0);
    cde.on[free].timed = (uint8_t)(dur != 0);
    cde.on[free].off = cde.t + (dur ? dur : 0);
    trk_note_on(&trk[i], (uint32_t)note, vel);
    cd_midi_note(i, (uint32_t)note, vel);
}
static void cd_release(uint32_t i, int live_only)
{
    uint32_t k;
    for (k = 0; k < CD_NON; k++)
        if (cde.on[k].used && cde.on[k].trk == i && (!live_only || cde.on[k].live))
            cd_off_at(k);
}
static void cd_notes_forget(uint32_t part)       /* (the part's voices were cut: its notes are not held any more) */
{
    uint32_t k;
    for (k = 0; k < CD_NON; k++)
        if (cde.on[k].used && cde.on[k].trk == part) {
            cd_midi_note(part, cde.on[k].note, 0);
            cde.on[k].used = 0;
        }
}

/* ------------------------------------------------------------ the chord --- */
static int32_t cd_deg(int d, int k)              /* scale degree d + k above degree d, in semitones */
{
    return CD_SCALE[(d + k) % 7] + 12 * ((d + k) / 7) - CD_SCALE[d];
}
static void cd_set_chord(uint32_t root)
{
    int rel = (int)((root + 120u - cde.s.key) % 12u), d, i;
    for (d = 0; d < 7 && CD_SCALE[d] != rel; d++)
        ;
    cde.root = (uint8_t)root;
    cde.has_chord = 1;
    if (d == 7) {                                /* (outside the key: maj9) */
        static const int8_t M9[CD_NCH] = {0, 4, 11, 14};
        for (i = 0; i < (int)CD_NCH; i++)
            cde.ints[i] = M9[i];
        cde.q_ = CDQ_MAJ9;
    } else {
        int third = cd_deg(d, 2), fifth = cd_deg(d, 4), sev = cd_deg(d, 6), top = cd_deg(d, 8), ext9 = 1;
        if (top != 14) {
            top = cd_deg(d, 10);
            ext9 = 0;
        }
        cde.q_ = third == 4 && sev == 11 ? (ext9 ? CDQ_MAJ9 : CDQ_MAJ11) : third == 4 && sev == 10 ? (ext9 ? CDQ_9 : CDQ_11) :
                 fifth == 6 ? CDQ_M7B5 : ext9 ? CDQ_M9 : CDQ_M11;
        cde.ints[0] = 0;
        cde.ints[1] = (int8_t)third;
        cde.ints[2] = (int8_t)(cde.q_ == CDQ_M7B5 ? fifth : sev);   /* (m7b5: its flat fifth is the chord; the top
                                                  * goes instead) */
        cde.ints[3] = (int8_t)(cde.q_ == CDQ_M7B5 ? sev : top);
    }
}
static int32_t cd_tone_note(uint32_t k)          /* chord tone k: the root's octave and up */
{
    return cde.root + cde.ints[k % CD_NCH] + 12 * (int32_t)(k / CD_NCH);
}
static void cd_sort(int32_t *a, uint32_t n)
{
    uint32_t i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && a[j - 1] > a[j]; j--) {
            int32_t t = a[j];
            a[j] = a[j - 1];
            a[j - 1] = t;
        }
}
/* track i's voicing of the chord: the inversion (and open voicings, a shift of octaves) nearest its last one, away from
 * a half-step cluster and from the edges of its register (the sim's chordNotes) */
static void cd_chord_notes(uint32_t i, int32_t *out)
{
    const cd_trk_t *T = &cde.s.t[i];
    int32_t base[CD_NCH], best[CD_NCH], bestc = 0x7FFFFFFF, center = 60 + 12 * T->octave;
    uint32_t k, r, sh, sp;
    static const int8_t SH[4] = {-24, -12, 0, 12};
    for (k = 0; k < CD_NCH; k++)
        base[k] = cde.root + cde.ints[k] + 12 * T->octave;
    cd_sort(base, CD_NCH);
    if (!cde.prev_ok[i]) {
        for (k = 0; k < CD_NCH; k++)
            cde.prev[i][k] = (uint8_t)(base[k] < 0 ? 0 : base[k] > 127 ? 127 : base[k]), out[k] = base[k];
        cde.prev_ok[i] = 1;
        return;
    }
    for (r = 0; r < CD_NCH; r++)
        for (sp = 0; sp < 3u; sp++) {
            int32_t shape[CD_NCH];
            for (k = 0; k < CD_NCH; k++)
                shape[k] = base[k] + (k < r ? 12 : 0);
            cd_sort(shape, CD_NCH);
            if (sp >= 1u) {
                shape[1] += 12;
                if (sp == 2u)
                    shape[3] += 12;
                cd_sort(shape, CD_NCH);
            }
            for (sh = 0; sh < 4u; sh++) {
                int32_t c[CD_NCH], cost = 0, mid, dm;
                for (k = 0; k < CD_NCH; k++) {
                    c[k] = shape[k] + SH[sh];
                    cost += c[k] > cde.prev[i][k] ? c[k] - cde.prev[i][k] : cde.prev[i][k] - c[k];
                }
                for (k = 1; k < CD_NCH; k++) {
                    int32_t iv = c[k] - c[k - 1];
                    cost += iv == 1 ? 10 : iv == 2 && k < 3u ? 4 : 0;
                }
                mid = (c[0] + c[CD_NCH - 1]) / 2;
                dm = mid > center ? mid - center : center - mid;
                cost += dm > 9 ? (dm - 9) * 3 : 0;
                if (c[0] < 12 || c[CD_NCH - 1] > 115)
                    cost += 1000;                /* (off the MIDI range) */
                if (cost < bestc) {
                    bestc = cost;
                    for (k = 0; k < CD_NCH; k++)
                        best[k] = c[k];
                }
            }
        }
    for (k = 0; k < CD_NCH; k++) {
        cde.prev[i][k] = (uint8_t)(best[k] < 0 ? 0 : best[k] > 127 ? 127 : best[k]);
        out[k] = best[k];
    }
}

/* ---------------------------------------------------------- the clock --- */
static uint32_t cd_div_ticks(const cd_trk_t *T) { return T->div == 0 ? 4u : T->div == 1 ? 2u : 1u; }
static uint32_t cd_sp16(void) { return cde.sp16q >> 8; }        /* a 16th, samples */
static uint32_t cd_next_on(uint32_t i, uint32_t st)              /* steps to the next step that plays (its length) */
{
    const cd_trk_t *T = &cde.s.t[i];
    uint32_t k, len = T->len ? T->len : 1u;
    for (k = 1; k <= len; k++)
        if (T->steps[(st + k) % len].on)
            return k;
    return len;
}
static uint32_t cd_step_dur(uint32_t i, uint32_t st)             /* a step's note, held to the next one, 95 % */
{
    return cd_next_on(i, st) * cd_div_ticks(&cde.s.t[i]) * cd_sp16() * 95u / 100u;
}

static void cd_trigger(uint32_t i, uint32_t st)
{
    const cd_trk_t *T = &cde.s.t[i];
    uint32_t dur = cd_step_dur(i, st), k;
    if (!cde.has_chord)
        cd_set_chord(48u + cde.s.key + 2u);      /* (no key yet: the key's ii, the sim's Dm9 in C) */
    if (T->mode == CD_CHORD) {
        int32_t n[CD_NCH];
        cd_release(i, 0);
        cd_chord_notes(i, n);
        for (k = 0; k < CD_NCH; k++)
            cd_note_on(i, n[k], 108, dur, 0);
    } else if (T->mode == CD_FOLLOW) {
        cd_note_on(i, cde.root + 12 * T->octave, 121, dur, 0);
    } else {
        const cd_step_t *s = &T->steps[st % CD_NSTEP];
        cd_note_on(i, s->note ? (int32_t)s->note : cd_tone_note(s->tone) + 12 * T->octave, 102, dur, 0);
    }
}
static void cd_fire_tick(void)
{
    uint32_t g = cde.g++, i;
    for (i = 0; i < CD_NTRK; i++) {
        const cd_trk_t *T = &cde.s.t[i];
        uint32_t div = cd_div_ticks(T), local, st, len = T->len ? T->len : 1u, k;
        if (g % div)
            continue;
        local = cde.ticks[i]++;
        st = local % len;
        cde.step[i] = (int16_t)st;
        if (T->mode == CD_ARP) {
            cde.gate[i] = T->steps[st].on;
            continue;
        }
        if (!T->steps[st].on || (T->prob < 100u && !cd_chance(T->prob)))
            continue;
        for (k = 0; k < CD_NQ && cde.q[k].used; k++)
            ;
        if (k == CD_NQ)
            continue;
        cde.q[k].used = 1;
        cde.q[k].i = (uint8_t)i;
        cde.q[k].st = (uint8_t)st;
        cde.q[k].at = cde.t + ((local & 1u) ? T->swing * cd_sp16() * div / 200u : 0u);   /* swing: up to half a step */
    }
}

/* --------------------------------------------------------- the arpeggiator --- */
static uint32_t cd_arp_pool(uint32_t i, int32_t *out)            /* its notes, low to high, over RANGE octaves */
{
    const cd_trk_t *T = &cde.s.t[i];
    int32_t pool[8];
    uint32_t n = 0, k, o, m = 0, range = cde.s.arp_range < 1u ? 1u : cde.s.arp_range > 4u ? 4u : cde.s.arp_range;
    if (cde.s.arp_src == 1u && cde.nheld) {
        for (k = 0; k < cde.nheld && k < 8u; k++)
            pool[n++] = cde.held[k];
        cd_sort(pool, n);
    } else {
        int32_t c[CD_NCH];
        cd_chord_notes(i, c);
        for (k = 0; k < CD_NCH; k++)
            pool[n++] = c[k] - 12 * T->octave;
    }
    for (o = 0; o < range; o++)
        for (k = 0; k < n; k++)
            out[m++] = pool[k] + 12 * (int32_t)o + 12 * T->octave;
    return m;
}
static void cd_arp_step(uint32_t i, uint32_t period)
{
    int32_t notes[32];
    uint32_t n, c = cde.arp_i, dmul = 16;        /* (the note's length: period x dmul / 10) */
    int idx = -1;
    if (!cde.has_chord)
        cd_set_chord(48u + cde.s.key + 2u);
    n = cd_arp_pool(i, notes);
    if (!n)
        return;
    switch (cde.s.arp_mode) {
    case CD_ARP_UP: idx = (int)(c % n); break;
    case CD_ARP_DOWN: idx = (int)(n - 1u - c % n); break;
    case CD_ARP_UPDN: {
        uint32_t p = n > 1u ? 2u * n - 2u : 1u, q = c % p;
        idx = (int)(q < n ? q : p - q);
        break;
    }
    case CD_ARP_RANDOM: idx = (int)(cd_r16() * n >> 16); break;
    case CD_ARP_EUCLID: {                        /* the chord's tones spread evenly over a cycle a little longer than
                                                  * the track (the sim's) */
        uint32_t kk = CD_NCH, L = cde.s.t[i].len + 3u > kk + 2u ? cde.s.t[i].len + 3u : kk + 2u, pos = c % L;
        if ((pos * kk) % L < kk) {
            idx = (int)(cde.euclid_n % n);
            cde.euclid_n++;
        }
        break;
    }
    case CD_ARP_DRIFT: {                         /* a loop of the notes that drifts: now and then one place changes */
        uint32_t k;
        if (cde.ndrift != n) {
            cde.ndrift = (uint8_t)n;
            for (k = 0; k < n; k++)
                cde.drift[k] = (uint8_t)k;
        }
        if (c > 0u && c % n == 0u && cd_r16() < 32768u + cde.s.arp_chaos * 32768u / 100u)
            cde.drift[cd_r16() * n >> 16] = (uint8_t)(cd_r16() * n >> 16);
        idx = cde.drift[c % n];
        break;
    }
    case CD_ARP_TURING: {                        /* an 8-bit shift register, its bit flipped by CHAOS */
        uint32_t bit = cde.reg & 1u;
        if (cd_r16() < cde.s.arp_chaos * 32768u / 100u)
            bit ^= 1u;
        cde.reg = (uint8_t)((cde.reg >> 1) | (bit << 7));
        idx = (int)(cde.reg % n);
        break;
    }
    default:                                     /* BLOOM: one long note every four */
        if (c % 4u == 0u)
            idx = (int)((c / 4u) % n);
        dmul = 80;
        break;
    }
    if (idx >= 0 && cd_r16() >= cde.s.arp_chaos * 26214u / 100u)   /* (CHAOS leaves some out: 0.4 of it) */
        cd_note_on(i, notes[idx], 89, period * dmul / 10u, 0);
}
static void cd_arp_clock(uint32_t t_end)
{
    static const uint8_t RATE_X2[4] = {8, 4, 2, 1};   /* 1/4 .. 1/32: 16ths x 2 */
    uint32_t period, i;
    if (!(cde.playing || cde.nheld || cde.arp_running))
        return;
    period = cd_sp16() * RATE_X2[cde.s.arp_rate & 3u] / 2u;
    if (!period)
        period = 1;
    while ((int32_t)(t_end - cde.arp_next) > 0) {
        cde.arp_next += period;
        if ((int32_t)(cde.arp_next - cde.t) < 0)
            cde.arp_next = cde.t + period;
        for (i = 0; i < CD_NTRK; i++) {
            if (cde.s.t[i].mode != CD_ARP)
                continue;
            if (cde.playing ? !cde.gate[i] : !cde.nheld)
                continue;
            cd_arp_step(i, period);
        }
        cde.arp_i++;
    }
}

/* ------------------------------------------------------------- keys, play --- */
static void cd_key(uint32_t note, int down)
{
    const cd_set_t *s = &cde.s;
    uint32_t sel = s->track % CD_NTRK, i, k;
    note &= 0x7Fu;
    if (down) {
        if (s->t[sel].mode != CD_SEQ) {
            cd_set_chord(note);
            if (cde.nheld < 8u)
                cde.held[cde.nheld++] = (uint8_t)note;
            for (i = 0; i < CD_NTRK; i++) {
                const cd_trk_t *T = &s->t[i];
                if (T->mode == CD_CHORD) {
                    int32_t n[CD_NCH];
                    uint32_t dur = 0;
                    cd_release(i, 0);
                    if (cde.playing)
                        dur = cd_step_dur(i, cde.step[i] < 0 ? 0u : (uint32_t)cde.step[i]);
                    cd_chord_notes(i, n);
                    for (k = 0; k < CD_NCH; k++)
                        cd_note_on(i, n[k], 108, dur, !cde.playing);
                } else if (T->mode == CD_FOLLOW && !cde.playing) {
                    cd_release(i, 0);
                    cd_note_on(i, cde.root + 12 * T->octave, 114, 0, 1);
                }
            }
            if (!cde.playing && !cde.arp_running) {
                cde.arp_running = 1;
                cde.arp_next = cde.t;
                cde.arp_i = 0;
                cde.euclid_n = 0;
            }
        } else {
            int32_t n = (int32_t)note + 12 * s->t[sel].octave;
            cd_note_on(sel, n, 114, 0, 1);
            cde.lk_trk[note] = (uint8_t)(sel + 1u);
            cde.lk_note[note] = (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n);
            if (cde.rec && cde.playing && n >= 0 && n <= 127) {   /* REC: into the step it is on */
                uint32_t st = cde.step[sel] < 0 ? 0u : (uint32_t)cde.step[sel];
                cde.s.t[sel].steps[st].on = 1;
                cde.s.t[sel].steps[st].note = (uint8_t)n;
                if (cd_recq_w - cd_recq_r < CD_RECQ) {
                    cd_recev_t *r = &cd_recq[cd_recq_w % CD_RECQ];
                    r->trk = (uint8_t)sel;
                    r->step = (uint8_t)st;
                    r->note = (uint8_t)n;
                    RING_PUBLISH();
                    cd_recq_w++;
                }
            }
        }
        return;
    }
    for (k = 0; k < cde.nheld; k++)
        if (cde.held[k] == note) {
            for (; k + 1u < cde.nheld; k++)
                cde.held[k] = cde.held[k + 1u];
            cde.nheld--;
            break;
        }
    if (cde.lk_trk[note]) {                      /* (a SEQ track's note: that one let go) */
        uint32_t t = cde.lk_trk[note] - 1u;
        for (k = 0; k < CD_NON; k++)
            if (cde.on[k].used && cde.on[k].live && cde.on[k].trk == t && cde.on[k].note == cde.lk_note[note])
                cd_off_at(k);
        cde.lk_trk[note] = 0;
    }
    if (!cde.nheld) {
        for (i = 0; i < CD_NTRK; i++)
            if (s->t[i].mode == CD_CHORD || s->t[i].mode == CD_FOLLOW)
                cd_release(i, 1);
        if (!cde.playing)
            cde.arp_running = 0;
    }
}
static void cd_midi_rt(uint32_t b)
{
    if (cde.s.midi_out)
        midi_out_event(0x0Fu | b << 8);
}
static void cd_play(int on)
{
    uint32_t i, k;
    if (on && !cde.playing) {
        cde.playing = 1;
        cde.next16 = cde.t;
        cde.frac16 = 0;
        cde.g = 0;
        for (i = 0; i < CD_NTRK; i++)
            cde.ticks[i] = 0;
        cde.arp_next = cde.t;
        cde.arp_i = cde.euclid_n = 0;
        cde.ck_next = cde.t;
        cde.ck_frac = 0;
        cd_midi_rt(0xFAu);
    } else if (!on && cde.playing) {
        cde.playing = 0;
        for (k = 0; k < CD_NQ; k++)
            cde.q[k].used = 0;
        for (i = 0; i < CD_NTRK; i++) {
            cde.step[i] = -1;
            cde.gate[i] = 0;
        }
        for (k = 0; k < CD_NON; k++)             /* (what the steps played lets go; what the keys hold stays) */
            if (cde.on[k].used && !cde.on[k].live)
                cd_off_at(k);
        cd_midi_rt(0xFCu);
    }
}

/* ------------------------------------------------------------------ MIDI in --- */
/* notes on any channel play as the FM-1's keys do; start / stop run the transport */
static void cd_midi_in(void)
{
    uint32_t w = mi_w;
    if (midi_in_overflow)
        return;
    while (mi_r != w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], status = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        mi_r++;
        if (status == 0xFAu || status == 0xFBu)
            cd_play(1);
        else if (status == 0xFCu)
            cd_play(0);
        else if ((status & 0xF0u) == 0x90u)
            cd_key(d1, d2 != 0);
        else if ((status & 0xF0u) == 0x80u)
            cd_key(d1, 0);
    }
}

/* --------------------------------------------------------- the audio side --- */
static void cd_apply(const cd_ev_t *e)
{
    switch (e->op) {
    case CDE_KEY: cd_key(e->a, e->v != 0); break;
    case CDE_PLAY: cd_play(e->v != 0); break;
    case CDE_REC: cde.rec = (uint8_t)(e->v != 0); break;
    case CDE_PANIC: {
        uint32_t i;
        cd_play(0);
        for (i = 0; i < CD_NTRK; i++)
            cd_release(i, 0);
        cde.nheld = 0;
        cde.arp_running = 0;
        for (i = 0; i < 128u; i++)
            cde.lk_trk[i] = 0;
        break;
    }
    default: break;
    }
}
static void cd_out_init(void)                    /* power-on (the audio ISR may already run: IRQ off) */
{
    uint32_t i;
    fm1_irq_off();
    cde.ready = 0;
    if (master_pot_q12)
        fx_master_now(master_pot_q12 - 1u);
    voice_fade_steal = 1;                        /* voice.c: a stolen voice fades out, its new note waits a block */
    fx_smooth = 1;                               /* fx.c: the limiter's gain eased */
    fx_output_set(1u, 1);                        /* fx.c's Clean output: the limiter a safety at -2 dBFS */
#if CR_FX
    xf_defaults();
#endif
    for (i = 0; i < CD_NTRK; i++)
        cde.step[i] = -1;
    cde.seed = 0x13579BDF;
    cde.reg = 0xA5;
    cd_evq_r = cd_evq_w = 0;
    cde.ready = 1;
    fm1_irq_on();
}
static void cd_tempo(void)
{
    uint32_t bpm = cde.s.tempo < 20u ? 20u : cde.s.tempo > 300u ? 300u : cde.s.tempo;
    cde.sp16q = (uint32_t)(((uint64_t)FS * 60u * 256u) / (bpm * 4u));
    cde.ckq = (uint32_t)(((uint64_t)FS * 60u * 256u) / (bpm * 24u));
    cde.tempo_was = (uint16_t)bpm;
}
static void cd_audio_block(uint32_t n)
{
    uint32_t w = cd_evq_w, t_end, k;
    if (!cde.ready)
        return;
    if (cde.tempo_was != cde.s.tempo || !cde.sp16q)
        cd_tempo();
    while (cd_evq_r != w) {
        cd_apply(&cd_evq[cd_evq_r % CD_EVQ]);
        cd_evq_r++;
    }
    cd_midi_in();
    t_end = cde.t + n;
    if (cde.playing)
        while ((int32_t)(t_end - cde.next16) > 0) {
            uint32_t f = cde.frac16 + cde.sp16q;
            cd_fire_tick();
            cde.next16 += f >> 8;
            cde.frac16 = f & 255u;
        }
    for (k = 0; k < CD_NQ; k++)                  /* the steps due (swing) */
        if (cde.q[k].used && (int32_t)(t_end - cde.q[k].at) > 0) {
            cde.q[k].used = 0;
            cd_trigger(cde.q[k].i, cde.q[k].st);
        }
    cd_arp_clock(t_end);
    for (k = 0; k < CD_NON; k++)                 /* the notes whose time is up */
        if (cde.on[k].used && cde.on[k].timed && (int32_t)(t_end - cde.on[k].off) >= 0)
            cd_off_at(k);
    if (cde.playing && cde.s.midi_out)           /* MIDI clock out: 24 a beat */
        while ((int32_t)(t_end - cde.ck_next) > 0) {
            uint32_t f = cde.ck_frac + cde.ckq;
            cd_midi_rt(0xF8u);
            cde.ck_next += f >> 8;
            cde.ck_frac = f & 255u;
        }
    cde.t = t_end;
}

/* ---------------------------------------------------- what the UI reads --- */
typedef struct {
    uint8_t playing, rec, nheld, root, q, has_chord;
    int16_t step[CD_NTRK];
    uint8_t sounding;            /* voices sounding, every part */
    uint8_t grains;              /* the Cloud's grains */
    uint32_t t;
} cd_sn_t;
static cd_sn_t cd_sn;
static void cd_snapshot(void)
{
    uint32_t i, k, v = 0;
    fm1_irq_off();
    cd_sn.playing = cde.playing;
    cd_sn.rec = cde.rec;
    cd_sn.nheld = cde.nheld;
    cd_sn.root = cde.root;
    cd_sn.q = cde.q_;
    cd_sn.has_chord = cde.has_chord;
    for (i = 0; i < CD_NTRK; i++)
        cd_sn.step[i] = cde.step[i];
    cd_sn.grains = cl.ng;
    cd_sn.t = cde.t;
    fm1_irq_on();
    for (i = 0; i < NPART; i++)
        for (k = 0; k < NVOICE; k++)
            v += trk[i].v[k].active != 0;
    cd_sn.sounding = (uint8_t)v;
}
/* the UI's settings to the player (after any change): a copy, IRQ off. The steps REC wrote meanwhile are in cs already
 * (cd_rec_drain runs first) */
static void cd_sync(const cd_set_t *s)
{
    fm1_irq_off();
    cde.s = *s;
    fm1_irq_on();
}
