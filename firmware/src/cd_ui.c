/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors */
/* CLOUDRIFT: the controls, after the sim's panel:
 *   buttons  FX: SPACE, SEL: TRACK (held: the short keys pick T1..T4, FOLLOW, LEN, SRC; tapped: the layer stays up
 *            for one key), ENV: PATCH, LFO: FREEZE, EDIT: CHORD (the track CHORD or SEQ), GLO, HOME, SAVE, ARP (with
 *            the layer up: the track ARP or SEQ), SEQ, PLAY, REC, OCT- / OCT+ (the keys' octave)
 *   knobs    MASTER: VOL (main.c's), SELECT: the track's LEVEL, PRESETS: SPACE, ALGORITHM: TEMPO, KNOB 1..4: the page's
 *   keys     notes F3..G5 (and the octave); on SEQ the long keys switch the track's steps 1..16
 * Every change goes to the player at once (cd_changed: cd_sync), the sounds' levels, sends and pan to the parts, the
 * Cloud's macros to cf_fx.c. Included after cf_draw.c and cd_settings.c. */

static const char *const CD_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const CD_MODE_TAG[CD_NMODE] = {"SEQ", "CHORD", "FOLLOW", "ARP"};
static const char *const CD_ARP_NAME[CD_NARP] = {"UP", "DOWN", "UP-DN", "RANDOM", "EUCLID", "DRIFT", "TURING", "BLOOM"};
static const char *const CD_RATE[4] = {"1/4", "1/8", "1/16", "1/32"};
static const char *const CD_DIV[3] = {"1/4", "1/8", "1/16"};
static const char *const CD_QNAME[7] = {"maj9", "maj11", "9", "11", "m7b5", "m9", "m11"};
static const uint8_t CD_LEN_CYCLE[9] = {16, 12, 8, 7, 5, 4, 3, 24, 32};
static const int8_t CD_PAN[CD_NTRK] = {-19, 13, 0, 22};   /* the sim's pans, -0.3 .. 0.35 */

static void kfmt(char *b, uint32_t n, const char *s) { str_cpy(b, s, n); }
static cd_trk_t *cur(void) { return &cs.t[cs.track % CD_NTRK]; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int cd_upper_index(int key)               /* the short keys 0..10, -1 a long one */
{
    int k, u = 0;
    if (!cf_key_is_upper(key))
        return -1;
    for (k = 0; k < key; k++)
        u += cf_key_is_upper(k);
    return u;
}
static int cd_lower_index(int key)               /* the long keys 0..15, -1 a short one */
{
    int k, l = 0;
    if (cf_key_is_upper(key))
        return -1;
    for (k = 0; k < key; k++)
        l += !cf_key_is_upper(k);
    return l;
}
static void kint(char *b, int v, const char *suffix)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && n < 10);
    i = 0;
    if (neg)
        b[i++] = '-';
    while (n)
        b[i++] = t[--n];
    b[i] = 0;
    if (suffix)
        str_cpy(b + i, suffix, 8);
}
static void cd_chord_name(char *b, uint32_t n)
{
    if (!cd_sn.has_chord) {
        str_cpy(b, "--", n);
        return;
    }
    str_cpy(b, CD_NOTE[cd_sn.root % 12u], n);
    str_cpy(b + str_len(b), CD_QNAME[cd_sn.q % 7u], n - str_len(b));
}

/* ------------------------------------------------------------ the parts --- */
static void cd_apply_tracks(void)
{
    uint32_t i;
    for (i = 0; i < CD_NTRK; i++) {
        const cd_trk_t *t = &cs.t[i];
        fm1_irq_off();
        trk[i].p[P_LEVEL] = (int16_t)(t->level * 127u / 100u);
        trk[i].p[P_REV] = (int16_t)(t->send * 127u / 100u);
        trk[i].p[P_PAN] = CD_PAN[i];
        trk[i].p[P_CHOR] = trk[i].p[P_DLY] = 0;
        fm1_irq_on();
        cd_tone_env(i, t->tone, t->env);
    }
    cd_vbudget = (uint8_t)(cs.voices * 2u);      /* (voice.c: a voice is two units of the budget) */
    cd_fx_set(cs.space, cs.cloud, cs.bloom, cs.tilt, cs.duck);
}
static void cd_changed(void)
{
    cu.dirty = 1;
    cu.changed_ms = cu.now;
    cd_sync(&cs);
    cd_apply_tracks();
}
static void cd_rec_drain(void)                   /* the steps REC wrote (the player's copy has them already) */
{
    uint32_t w = cd_recq_w;
    while (cd_recq_r != w) {
        const cd_recev_t *r = &cd_recq[cd_recq_r % CD_RECQ];
        cd_step_t *s = &cs.t[r->trk % CD_NTRK].steps[r->step % CD_NSTEP];
        s->on = 1;
        s->note = r->note;
        cd_recq_r++;
        cu.dirty = 1;
        cu.changed_ms = cu.now;
    }
}

/* ------------------------------------------------------------ the knobs --- */
/* a page's knob k: its value set from d detents (cont: 2 % a detent; disc: one place a detent) and shown */
static void cd_cont(uint8_t *v, int d) { *v = (uint8_t)clampi((int)*v + d * 2, 0, 100); }
static int cd_step1(int d) { return d > 0 ? 1 : d < 0 ? -1 : 0; }
static void cd_page_knob(int k, int d, cf_knob_t *o)
{
    cd_trk_t *T = cur();
    uint32_t tn = cs.track % CD_NTRK;
    char b[16];
    if (o) {
        o->bar = -1;
        o->dis = 0;
    }
#define CONT(lbl, var)                                   \
    do {                                                 \
        if (d) cd_cont(&(var), d);                       \
        if (o) {                                         \
            kfmt(o->name, 12, (lbl));                    \
            kint(o->val, (var), "%");                    \
            o->bar = (int16_t)((var) * 255 / 100);       \
        }                                                \
    } while (0)
    switch (cu.page) {
    case PG_PATCH:
        if (k == 0) {
            if (d) {
                uint32_t n = cd_snd_count();
                if (n) {
                    T->snd = (uint16_t)((cd_snd[tn].pos + n + (uint32_t)cd_step1(d)) % n);
                    cd_snd_load(tn, T->snd);
                }
            }
            if (o) {
                kfmt(o->name, 12, "PATCH");
                kfmt(o->val, 14, cd_snd[tn].name);
            }
        } else if (k == 1) {
            CONT("TONE", T->tone);
        } else if (k == 2) {
            CONT("ENVELOPE", T->env);
        } else {
            if (d)
                T->octave = (int8_t)clampi(T->octave + cd_step1(d), -2, 2);
            if (o) {
                kfmt(o->name, 12, "OCTAVE");
                kint(o->val, T->octave, 0);
                if (T->octave > 0) {
                    b[0] = '+';
                    str_cpy(b + 1, o->val, 12);
                    kfmt(o->val, 14, b);
                }
            }
        }
        break;
    case PG_ARP:
        if (k == 0) {
            if (d)
                cs.arp_mode = (uint8_t)clampi(cs.arp_mode + cd_step1(d), 0, CD_NARP - 1);
            if (o) {
                kfmt(o->name, 12, "MODE");
                kfmt(o->val, 14, CD_ARP_NAME[cs.arp_mode % CD_NARP]);
            }
        } else if (k == 1) {
            if (d)
                cs.arp_rate = (uint8_t)clampi(cs.arp_rate + cd_step1(d), 0, 3);
            if (o) {
                kfmt(o->name, 12, "RATE");
                kfmt(o->val, 14, CD_RATE[cs.arp_rate & 3u]);
            }
        } else if (k == 2) {
            if (d)
                cs.arp_range = (uint8_t)clampi(cs.arp_range + cd_step1(d), 1, 4);
            if (o) {
                kfmt(o->name, 12, "RANGE");
                kint(o->val, cs.arp_range, " OCT");
            }
        } else {
            CONT("CHAOS", cs.arp_chaos);
        }
        break;
    case PG_SPACE:
        if (k == 0)
            CONT("SPACE", cs.space);
        else if (k == 1)
            CONT("CLOUD", cs.cloud);
        else if (k == 2)
            CONT("BLOOM", cs.bloom);
        else {
            char l[12];
            str_cpy(l, "SEND T", sizeof l);
            l[6] = (char)('1' + tn);
            l[7] = 0;
            CONT(l, T->send);
        }
        break;
    case PG_SEQ:
        if (k == 0) {
            if (d)
                T->len = (uint8_t)clampi(T->len + cd_step1(d), 1, (int)CD_NSTEP);
            if (o) {
                kfmt(o->name, 12, "LENGTH");
                kint(o->val, T->len, 0);
            }
        } else if (k == 1) {
            if (d)
                T->div = (uint8_t)clampi(T->div + cd_step1(d), 0, 2);
            if (o) {
                kfmt(o->name, 12, "DIVIDE");
                kfmt(o->val, 14, CD_DIV[T->div % 3u]);
            }
        } else if (k == 2) {
            CONT("PROB", T->prob);
        } else {
            CONT("SWING", T->swing);
        }
        break;
    case PG_GLO:
        if (k == 0) {
            if (d)
                cs.voices = (uint8_t)clampi(cs.voices + cd_step1(d), 6, 8);
            if (o) {
                kfmt(o->name, 12, "VOICES");
                kint(o->val, cs.voices, 0);
            }
        } else if (k == 1) {
            if (d)
                cs.key = (uint8_t)((cs.key + 12 + cd_step1(d)) % 12);
            if (o) {
                kfmt(o->name, 12, "KEY");
                str_cpy(b, CD_NOTE[cs.key % 12u], sizeof b);
                str_cpy(b + str_len(b), " MAJ", 6);
                kfmt(o->val, 14, b);
            }
        } else if (k == 2) {
            CONT("TONE TILT", cs.tilt);
        } else {
            CONT("DUCK", cs.duck);
        }
        break;
    default: {                                   /* HOME: the four tracks' levels */
        char l[12];
        str_cpy(l, "T1 LEVEL", sizeof l);
        l[1] = (char)('1' + k);
        CONT(l, cs.t[k & 3].level);
        if (o)
            o->sel = (uint8_t)((uint32_t)k == tn);
        break;
    }
    }
#undef CONT
}
static void ui_knob_info(int k, cf_knob_t *o) { cd_page_knob(k, 0, o); }
static void ui_knob_turn(int k, int d)
{
    if (k >= 0 && k < 4)
        cu.knob_ms[k] = cu.now ? cu.now : 1u;
    cd_page_knob(k, d, 0);
    cd_changed();
}
static void cd_toast_val(const char *name, int v, const char *suffix)
{
    char b[28], n[12];
    str_cpy(b, name, sizeof b);
    str_cpy(b + str_len(b), " ", 2);
    kint(n, v, suffix);
    str_cpy(b + str_len(b), n, (uint32_t)(sizeof b - str_len(b)));
    cf_message(b);
}

/* ------------------------------------------------------------ the keys --- */
static void cd_sel_key(int u)                    /* a short key with the SEL layer up */
{
    cd_trk_t *T = cur();
    char b[20];
    if (u <= 3) {
        cs.track = (uint8_t)u;
        str_cpy(b, "T1", sizeof b);
        b[1] = (char)('1' + u);
        cf_message(b);
    } else if (u == 4) {
        T->mode = T->mode == CD_FOLLOW ? CD_SEQ : CD_FOLLOW;
        str_cpy(b, "T1 ", sizeof b);
        b[1] = (char)('1' + cs.track);
        str_cpy(b + 3, T->mode == CD_FOLLOW ? "FOLLOW" : "SEQ", 8);
        cf_message(b);
    } else if (u == 5) {
        uint32_t i;
        for (i = 0; i < 9u && CD_LEN_CYCLE[i] != T->len; i++)
            ;
        T->len = CD_LEN_CYCLE[i < 9u ? (i + 1u) % 9u : 0u];
        cd_toast_val("LENGTH", T->len, 0);
    } else if (u == 6) {
        cs.arp_src = !cs.arp_src;
        cf_message(cs.arp_src ? "SRC HELD" : "SRC CHORD");
    } else {
        return;
    }
    if (cu.sel_phys)
        cu.sel_used = 1;
    else
        cu.sel = 0;                              /* (a tapped layer: down after its one key) */
    cd_changed();
}
static void ui_note_key(int key, int down)
{
    int u = cd_upper_index(key), l = cd_lower_index(key);
    if (down) {
        if (u >= 0 && cu.sel) {
            cd_sel_key(u);
            cu.keynote[key] = 0;
            return;
        }
        if (l >= 0 && l < 16 && cu.page == PG_SEQ) {   /* SEQ: a long key switches its step */
            cd_step_t *s = &cur()->steps[l];
            s->on = !s->on;
            if (!s->on)
                s->note = 0;
            cu.keynote[key] = 0;
            cd_changed();
            return;
        }
        {
            int n = 53 + key + 12 * cu.oct;
            if (n < 0 || n > 127)
                return;
            cu.keynote[key] = (uint8_t)(n + 1);
            cd_post(CDE_KEY, (uint32_t)n, 1);
        }
        return;
    }
    if (cu.keynote[key]) {
        cd_post(CDE_KEY, cu.keynote[key] - 1u, 0);
        cu.keynote[key] = 0;
    }
}
static void cf_ui_pump(void)                     /* the keys, as often as the main loop can (main.c's idle too) */
{
    uint32_t now = fm1_in.notes, tap = fm1_input_note_edges() & ~now & ~cu.pkeys & ((1u << CF_NKEYS) - 1u);
    uint32_t ch = (now ^ cu.pkeys) | tap, k;
    if (!cu.booted)
        return;
    if (ch) {
        for (k = 0; k < CF_NKEYS; k++)
            if ((ch >> k) & 1u)
                ui_note_key((int)k, (int)(((now | tap) >> k) & 1u));
        for (k = 0; k < CF_NKEYS; k++)           /* (a tap: its release after its press) */
            if ((tap >> k) & 1u)
                ui_note_key((int)k, 0);
        cu.idle_ms = 0;
    }
    cu.pkeys = now;
    cu.keys = now;
}

/* ------------------------------------------------------------ the buttons --- */
static void cd_page(uint32_t p)
{
    cu.page = (uint8_t)p;
}
static void ui_button(uint32_t b)
{
    cd_trk_t *T = cur();
    char m[20];
    switch (b) {
    case B_HOME: cd_page(PG_HOME); break;
    case B_FX: cd_page(PG_SPACE); break;
    case B_ENV: cd_page(PG_PATCH); break;
    case B_SEQ: cd_page(PG_SEQ); break;
    case B_GLO: cd_page(PG_GLO); break;
    case B_ARP:
        if (cu.sel) {                            /* SEL + ARP: the track an arp track, or back to SEQ */
            T->mode = T->mode == CD_ARP ? CD_SEQ : CD_ARP;
            str_cpy(m, "T1 ", sizeof m);
            m[1] = (char)('1' + cs.track);
            str_cpy(m + 3, T->mode == CD_ARP ? "ARP" : "SEQ", 8);
            cf_message(m);
            if (cu.sel_phys)
                cu.sel_used = 1;
            else
                cu.sel = 0;
            cd_changed();
        } else {
            cd_page(PG_ARP);
        }
        break;
    case B_SCL:                                  /* SEL: the layer up while held; a tap leaves it up for one key */
        if (cu.sel && !cu.sel_phys) {
            cu.sel = 0;
            break;
        }
        cu.sel = cu.sel_phys = 1;
        cu.sel_used = 0;
        break;
    case B_EDIT:                                 /* CHORD: the track plays the chord, or its own steps */
        T->mode = T->mode == CD_CHORD ? CD_SEQ : CD_CHORD;
        str_cpy(m, "T1 ", sizeof m);
        m[1] = (char)('1' + cs.track);
        str_cpy(m + 3, T->mode == CD_CHORD ? "CHORD" : "SEQ", 8);
        cf_message(m);
        cd_changed();
        break;
    case B_LFO:                                  /* FREEZE */
        cu.freeze = !cu.freeze;
        cf_cloud_freeze(cu.freeze);
        cf_message(cu.freeze ? "FREEZE" : "FREEZE OFF");
        break;
    case B_PLAY: cd_post(CDE_PLAY, 0, !cd_sn.playing); break;
    case B_REC:
        cu.rec = !cu.rec;
        cd_post(CDE_REC, 0, cu.rec);
        break;
    case B_SAVE:
        cu.save_want = 1;
        cf_message(cd_sn.playing ? "Saves when stopped" : "Saving");
        break;
    case B_OCTDN:
    case B_OCTUP:
        cu.oct = (int8_t)clampi(cu.oct + (b == B_OCTUP ? 1 : -1), -2, 2);
        cd_toast_val("OCT", cu.oct, 0);
        break;
    default: break;
    }
}
static void ui_button_up(uint32_t b)
{
    if (b == B_SCL && cu.sel_phys) {
        cu.sel_phys = 0;
        if (cu.sel_used)
            cu.sel = 0;                          /* (held and used: down with the button; a tap: up for one key) */
    }
}

/* ------------------------------------------------------------ input --- */
static void cf_ui_input(void)
{
    uint32_t rel = 0, pr = fm1_input_edges(&rel), i;
    int32_t d;
    cd_rec_drain();
    cf_ui_pump();
    for (i = 0; i < 14u; i++) {
        if ((pr >> i) & 1u) {
            uint32_t b = panel_btn_of(i);
            if (b < NB)
                ui_button(b);
            cu.idle_ms = 0;
        }
    }
    (void)rel;
    if (cu.sel_phys && !((fm1_in.buttons >> panel.btn[B_SCL]) & 1u))
        ui_button_up(B_SCL);                     /* (SEL let go: read from the held state, as on every build) */
    if ((d = panel_enc(EN_SELECT)) != 0) {       /* SELECT: the track's LEVEL */
        cd_cont(&cur()->level, (int)d);
        cd_changed();
        {
            char l[12];
            str_cpy(l, "T1 LEVEL", sizeof l);
            l[1] = (char)('1' + cs.track);
            cd_toast_val(l, cur()->level, "%");
        }
    }
    if ((d = panel_enc(EN_PRESET)) != 0) {       /* PRESETS: SPACE */
        cd_cont(&cs.space, (int)d);
        cd_changed();
        cd_toast_val("SPACE", cs.space, "%");
    }
    if ((d = panel_enc(EN_ALGO)) != 0) {         /* ALGORITHM: TEMPO */
        cs.tempo = (uint16_t)clampi((int)cs.tempo + (int)d, 40, 160);
        cd_changed();
        cd_toast_val("TEMPO", cs.tempo, " BPM");
    }
    for (i = 0; i < 4u; i++)
        if ((d = panel_enc(EN_K1 + i)) != 0)
            ui_knob_turn((int)i, (int)d), cu.idle_ms = 0;
}

/* ------------------------------------------------------------ keycaps --- */
static void ui_keycap(int key, cf_keycap_t *o)
{
    int u = cd_upper_index(key), l = cd_lower_index(key), down = (int)((cu.keys >> key) & 1u);
    int note = 53 + key + 12 * cu.oct, pc = (note % 12 + 12) % 12, rel = (pc - cs.key + 12) % 12, d;
    if (u >= 0 && cu.sel) {                      /* the SEL layer */
        static const char *const SL[7] = {"T1", "T2", "T3", "T4", "FOL", "LEN", "SRC"};
        if (u < 7) {
            str_cpy(o->top, SL[u], 8);
            o->style = u < 4 && u == (int)cs.track ? KC_ON : KC_BTN;
        } else {
            o->style = KC_OFF;
        }
        o->btn = 1;
        return;
    }
    if (l >= 0 && l < 16 && cu.page == PG_SEQ) { /* SEQ: the track's steps */
        const cd_trk_t *T = cur();
        kint(o->top, l + 1, 0);
        o->style = l >= T->len ? KC_OFF : (cd_sn.playing && cd_sn.step[cs.track] == l) ? KC_DOWN :
                   T->steps[l].on ? KC_ON : KC_ROOT;
        return;
    }
    if (u >= 0) {                                /* a short key: no name (the sim's), its light says the rest */
        o->style = down ? KC_DOWN : (cd_sn.has_chord && cd_sn.nheld && pc == cd_sn.root % 12u) ? KC_SOUNDING : KC_ROOT;
        return;
    }
    str_cpy(o->top, CD_NOTE[pc], 8);
    for (d = 0; d < 7 && CD_SCALE[d] != rel; d++)
        ;
    if (l >= 0 && d < 7) {
        o->bot[0] = (char)('1' + d);
        o->bot[1] = 0;
    }
    o->style = down ? KC_DOWN : (cd_sn.has_chord && cd_sn.nheld && pc == cd_sn.root % 12u) ? KC_SOUNDING :
               rel == 0 ? KC_SCALE : KC_ROOT;
}

/* ------------------------------------------------------------ the lights --- */
static uint8_t cu_led_pos[41];
static void cf_led_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        cu_led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    cu_led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
static void cf_led(uint8_t *m, uint32_t id, int on)
{
    uint8_t q = cu_led_pos[id % 41u];
    if (q != 0xFF && on)
        m[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
static void cd_leds(void)
{
    uint8_t lit[FM1_NCOL] = {0}, mid[FM1_NCOL] = {0}, dim[FM1_NCOL] = {0}, faint[FM1_NCOL] = {0};
    static const uint8_t PAGE_BTN[PG_N] = {B_HOME, B_ENV, B_ARP, B_FX, B_SEQ, B_GLO};
    const cd_trk_t *T = cur();
    uint32_t k, blink = (cu.now / 250u) & 1u;
    for (k = 0; k < PG_N; k++)
        cf_led(k == cu.page ? lit : dim, panel.btn[PAGE_BTN[k]], 1);
    cf_led(cd_sn.playing ? (cd_sn.step[0] >= 0 && (cd_sn.step[0] & 3) == 0 ? lit : mid) : dim, panel.btn[B_PLAY], 1);
    cf_led(cu.rec ? (cd_sn.playing ? lit : (blink ? lit : faint)) : dim, panel.btn[B_REC], 1);
    cf_led(cu.freeze ? lit : dim, panel.btn[B_LFO], 1);
    cf_led(T->mode == CD_CHORD ? lit : dim, panel.btn[B_EDIT], 1);
    cf_led(cu.sel ? lit : dim, panel.btn[B_SCL], 1);
    cf_led(cu.save_want ? (blink ? lit : faint) : cd_dirty() ? mid : faint, panel.btn[B_SAVE], 1);
    cf_led(cu.oct < 0 ? (cu.oct < -1 ? lit : mid) : faint, panel.btn[B_OCTDN], 1);
    cf_led(cu.oct > 0 ? (cu.oct > 1 ? lit : mid) : faint, panel.btn[B_OCTUP], 1);
    for (k = 0; k < CF_NKEYS; k++) {
        int u = cd_upper_index((int)k), l = cd_lower_index((int)k), id = (int)(14u + k);
        int note = 53 + (int)k + 12 * cu.oct, pc = (note % 12 + 12) % 12;
        if ((cu.keys >> k) & 1u) {
            cf_led(lit, (uint32_t)id, 1);
            continue;
        }
        if (u >= 0 && cu.sel) {
            if (u < 4)
                cf_led(u == (int)cs.track ? lit : mid, (uint32_t)id, 1);
            else if (u < 7)
                cf_led(dim, (uint32_t)id, 1);
            continue;
        }
        if (l >= 0 && l < 16 && cu.page == PG_SEQ) {
            if (cd_sn.playing && cd_sn.step[cs.track] == l)
                cf_led(lit, (uint32_t)id, 1);
            else if (l < T->len && T->steps[l].on)
                cf_led(mid, (uint32_t)id, 1);
            else if (l < T->len)
                cf_led(faint, (uint32_t)id, 1);
            continue;
        }
        if (cd_sn.has_chord && cd_sn.nheld && pc == cd_sn.root % 12u)
            cf_led(mid, (uint32_t)id, 1);
        else if (pc == cs.key)
            cf_led(dim, (uint32_t)id, 1);
    }
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_faint[k] = faint[k];
        fm1_led_dim[k] = dim[k];
        fm1_led_dim2[k] = mid[k];
        fm1_led[k] = lit[k];
    }
}

/* ------------------------------------------------------------ power-on, the frame --- */
static void cd_default_sounds(void)
{
    static const struct { uint8_t e; char name[13]; } DEF[CD_NTRK] = {   /* (VA: its voices follow the load) */
#if FELUCCA_VA
        {ENGI_VA, "LUSH PAD"}, {ENGI_VA, "SOFT LEAD"}, {ENGI_VA, "DEEP SUB"}, {ENGI_VA, "DREAM BELLS"}};
#else
        {ENGI_FM6, "TINE EP"}, {ENGI_FM6, "TINE EP"}, {ENGI_FM6, "BASS"}, {ENGI_FM6, "TINE EP"}};
#endif
    uint32_t i;
    for (i = 0; i < CD_NTRK; i++) {
        int at = cd_snd_find(DEF[i].e, DEF[i].name);
        cs.t[i].snd = (uint16_t)(at >= 0 ? (uint32_t)at : i + 1u);
    }
}
static void cf_ui_init(void)
{
    uint32_t i;
    if (!cds_loaded)
        cd_settings_load();                      /* (the setup kept in flash, if any) */
    if (!cds_loaded)
        cd_defaults(&cs);
    if (!cds_loaded || cs.t[0].snd == 0xFFFFu)
        cd_default_sounds();
    cu.page = PG_HOME;
    cf_led_init();
    cf_palette_build();
    cd_out_init();
    for (i = 0; i < CD_NTRK; i++)
        cd_snd_load(i, cs.t[i].snd);
    cd_sync(&cs);
    cd_apply_tracks();
    cu.dirty = 0;
    fb_full = 1;
    cu.last_ms = fm1_ms;
    cu.booted = 1;
}
static void cf_ui_frame(void)                    /* main.c's ui_leds: the snapshot, the lights */
{
    uint32_t now = fm1_ms, dt = now - cu.last_ms;
    cu.last_ms = now;
    cu.frame_dt = dt;
    cu.now = now;
    cu.idle_ms += dt;
    cu.frames++;
    cd_snapshot();
    cd_leds();
}

/* fm6_store.c's (a DX7 voice over SysEx): it goes to the selected track */
static int cf_editing_part(void) { return (int)(cs.track % CD_NTRK); }
static void cf_edited(uint32_t part) { (void)part; }
static void ui_say(const char *a, const char *b)
{
    char m[28];
    str_cpy(m, a, sizeof m);
    str_cpy(m + str_len(m), b, (uint32_t)(sizeof m - str_len(m)));
    cf_message(m);
}
