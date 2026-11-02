/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors */
/* CLOUDRIFT: the screens (into cf_fb.c's framebuffer; the keys and the knobs below are cf_draw.c's rows):
 *   the top bar  the page, the tracks T1..T4 (the selected one lit), the tempo and PLAY on HOME
 *   HOME         the chord, big; FREEZE and REC; the four lanes: track, sound, mode, its steps (the playhead lit),
 *                its length
 *   PATCH        the selected track's sound, big, its engine and mode
 *   ARP          the mode, big, and a picture of its next notes; the source
 *   SPACE        the Cloud: its grains drawn as they are, FREEZE, the voices sounding
 *   SEQ          the track's 32 steps in two rows, the playhead
 *   GLOBAL       the key, big; the voices
 * Included after cd_ui.c. */

static void draw_tracks_chips(int xr)            /* T1..T4 from the right edge xr leftward */
{
    int i;
    for (i = (int)CD_NTRK - 1; i >= 0; i--) {
        char b[3] = {'T', (char)('1' + i), 0};
        int w = fb_text_w(&CF_FONT_UI, b, 1) + 6, on = (uint32_t)i == cs.track;
        xr -= w;
        if (on)
            fb_rbox(xr, Y_TOP + 2, w, 11, 2, C_ACC(1));
        else
            fb_rframe(xr, Y_TOP + 2, w, 11, FL_BG3);
        fb_text(&CF_FONT_UI, xr + 3, Y_TOP + 1, b, on ? FL_BG : FL_INK2, 1);
        xr -= 3;
    }
}
static void draw_cd_topbar(void)
{
    fb_rect(0, Y_TOP, FB_W, 16, FL_BG);
    fb_text(&CF_FONT_UI, 4, Y_TOP + 1, PG_NAME[cu.page % PG_N], FL_INK, 1);
    if (cu.page == PG_HOME) {                    /* the tempo, a play triangle */
        char b[12];
        int x = 206;
        if (cd_sn.playing) {
            int j;
            for (j = 0; j < 7; j++)
                fb_vline(x - 7 + j / 1, Y_TOP + 4 + j / 2, 7 - (j / 2) * 2, C_ACC(0));
        } else {
            fb_rframe(x - 7, Y_TOP + 4, 6, 7, FL_INK3);
        }
        kint(b, cs.tempo, " BPM");
        fb_text_r(&CF_FONT_UI, x - 11, Y_TOP + 1, b, FL_INK2, 1);
    } else {
        draw_tracks_chips(210);
    }
    draw_battery(216, Y_TOP + 3);
    fb_hline(0, Y_TOP + 15, FB_W, FL_BG1);
}
static int draw_pill(int xr, int y, const char *s, uint8_t fill, uint8_t ink)   /* right-aligned at xr: its left */
{
    int w = fb_text_w(&CF_FONT_UI, s, 1) + 8;
    fb_rbox(xr - w, y, w, 12, 3, fill);
    fb_text(&CF_FONT_UI, xr - w + 4, y, s, ink, 1);
    return xr - w;
}
static void draw_lane(uint32_t i, int y)
{
    const cd_trk_t *T = &cs.t[i];
    int sel = i == cs.track, x0 = 80, w = 130, k, sz, gap;
    char b[12];
    fb_rbox(2, y, FB_W - 4, 23, 3, sel ? cf_panel(2) : cf_panel(1));
    b[0] = 'T';
    b[1] = (char)('1' + i);
    b[2] = 0;
    fb_text(&CF_FONT_UI, 6, y + 3, b, sel ? C_ACC(0) : FL_INK, 1);
    str_cpy(b, cd_snd[i].name, 11);
    fb_text(&CF_FONT_MICRO, 22, y + 3, b, FL_INK2, 1);
    fb_text(&CF_FONT_MICRO, 22, y + 13, CD_MODE_TAG[T->mode % CD_NMODE], T->mode == CD_SEQ ? FL_INK3 : C_ACC(1), 1);
    sz = T->len <= 16 ? 5 : 3;
    gap = T->len <= 16 ? 2 : 1;
    if (T->len * (sz + gap) > w)
        gap = 0;
    for (k = 0; k < T->len; k++) {
        int x = x0 + k * (sz + gap), head = cd_sn.playing && cd_sn.step[i] == k;
        uint8_t c = head ? C_ACC(0) : (T->steps[k].on && T->mode != CD_ARP) ? FL_INK2 :
                    T->mode == CD_ARP && T->steps[k].on ? FL_BG3 : FL_BG2;
        fb_rect(x, y + 9, sz, sz, c);
    }
    kint(b, T->len, 0);
    fb_text_r(&CF_FONT_UI, FB_W - 6, y + 6, b, FL_INK3, 1);
}
static void draw_home(void)
{
    char b[20];
    int x, i;
    cd_chord_name(b, sizeof b);
    if (!cd_sn.has_chord) {                      /* (no chord yet: the key, pale) */
        str_cpy(b, CD_NOTE[cs.key % 12u], sizeof b);
        str_cpy(b + str_len(b), " MAJ", 6);
        fb_text(&CF_FONT_UI, 10, Y_VIEW + 10, b, FL_INK3, 2);
    } else {                                     /* the chord: its root big, its quality after it */
        char r[4];
        int n = b[0] && b[1] == '#' ? 2 : 1;
        str_cpy(r, b, (uint32_t)n + 1u);
        fb_signs(r, r, sizeof r);
        x = 8 + fb_text_halo(&CF_FONT_UI, 8, Y_VIEW + 4, r, cd_sn.nheld ? C_ACC(0) : FL_INK, FL_BG, 3);
        if (cd_sn.has_chord)
            fb_text(&CF_FONT_UI, x + 2, Y_VIEW + 8, b + n, FL_INK2, 2);
    }
    x = FB_W - 6;
    if (cu.freeze)
        x = draw_pill(x, Y_VIEW + 8, "FREEZE", C_ACC(1), FL_BG) - 4;
    if (cu.rec)
        draw_pill(x, Y_VIEW + 8, "REC", FL_C(FL_R_PEACH, 1), FL_BG);
    for (i = 0; i < (int)CD_NTRK; i++)
        draw_lane((uint32_t)i, Y_VIEW + 40 + i * 25);
}
static void draw_big_title(const char *s, const char *sub)
{
    char b[24];
    fb_signs(s, b, sizeof b);
    fb_text_halo(&CF_FONT_UI, 10, Y_VIEW + 10, b, FL_INK, FL_BG, 3);
    if (sub && sub[0])
        fb_text(&CF_FONT_UI, 12, Y_VIEW + 44, sub, FL_INK2, 1);
}
static void draw_patch(void)
{
    const cd_trk_t *T = cur();
    char sub[28];
    str_cpy(sub, cd_engine_name(cd_snd[cs.track].engine), sizeof sub);
    str_cpy(sub + str_len(sub), "  ", 3);
    str_cpy(sub + str_len(sub), CD_MODE_TAG[T->mode % CD_NMODE], 8);
    fb_text_halo(&CF_FONT_UI, 10, Y_VIEW + 10, cd_snd[cs.track].name, FL_INK, FL_BG, 2);
    fb_text(&CF_FONT_UI, 12, Y_VIEW + 34, sub, FL_INK2, 1);
    draw_lane(cs.track, Y_VIEW + 108);
}
/* the arp's next 16 notes, as the deterministic modes play them (the random ones: a scatter) */
static void draw_arp(void)
{
    char sub[28];
    int k, n = 5 * (cs.arp_range < 1 ? 1 : cs.arp_range), x0 = 12, y0 = Y_VIEW + 104, h = 44;
    str_cpy(sub, "SRC ", sizeof sub);
    str_cpy(sub + 4, cs.arp_src ? "HELD" : "CHORD", 8);
    str_cpy(sub + str_len(sub), "   ", 4);
    str_cpy(sub + str_len(sub), CD_RATE[cs.arp_rate & 3u], 6);
    draw_big_title(CD_ARP_NAME[cs.arp_mode % CD_NARP], sub);
    for (k = 0; k < 16; k++) {
        int idx = -1, c = k;
        switch (cs.arp_mode) {
        case CD_ARP_UP: idx = c % n; break;
        case CD_ARP_DOWN: idx = n - 1 - c % n; break;
        case CD_ARP_UPDN: { int p = n > 1 ? 2 * n - 2 : 1, q = c % p; idx = q < n ? q : p - q; break; }
        case CD_ARP_EUCLID: { int L = 16 + 3, pos = c % L; idx = (pos * 5) % L < 5 ? (c * 5 / L) % n : -1; break; }
        case CD_ARP_BLOOM: idx = c % 4 == 0 ? (c / 4) % n : -1; break;
        default: idx = (int)((c * 2654435761u >> 7) % (uint32_t)n); break;
        }
        if (idx >= 0)
            fb_rect(x0 + k * 13, y0 - idx * h / n, 10, 3, k == 0 ? C_ACC(0) : C_ACC(1));
        else
            fb_rect(x0 + k * 13, y0, 10, 1, FL_BG3);
    }
}
static void draw_space(void)
{
    char b[24];
    int i;
    uint32_t seed = cu.now / 120u;
    kint(b, cs.space, 0);
    draw_big_title("CLOUD", 0);
    for (i = 0; i < 40; i++) {                   /* the cloud: dots drifting, more with CLOUD, bright for each grain */
        uint32_t r = (uint32_t)(i * 2654435761u) ^ (seed * 40503u + (uint32_t)i * 97u);
        int x = 120 + (int)(r % 200u) - 100, y = Y_VIEW + 70 + (int)((r >> 8) % 50u) - 25;
        if ((r >> 20) % 100u < cs.cloud)
            fb_rect(x, y, 2, 2, i < cd_sn.grains * 4 ? C_ACC(0) : FL_BG3);
    }
    if (cu.freeze)
        draw_pill(FB_W - 8, Y_VIEW + 12, "FREEZE", C_ACC(1), FL_BG);
    kint(b, cd_sn.sounding, " voices sounding");
    fb_text(&CF_FONT_UI, 12, Y_VIEW + 124, b, FL_INK2, 1);
}
static void draw_seq(void)
{
    const cd_trk_t *T = cur();
    int k;
    char b[24];
    str_cpy(b, "T1 ", sizeof b);
    b[1] = (char)('1' + cs.track);
    str_cpy(b + 3, CD_MODE_TAG[T->mode % CD_NMODE], 8);
    fb_text_halo(&CF_FONT_UI, 10, Y_VIEW + 8, b, FL_INK, FL_BG, 2);
    fb_text(&CF_FONT_UI, 12, Y_VIEW + 32, cd_snd[cs.track].name, FL_INK2, 1);
    for (k = 0; k < (int)CD_NSTEP; k++) {
        int x = 8 + (k % 16) * 14, y = Y_VIEW + 62 + (k / 16) * 34, in = k < T->len;
        int head = cd_sn.playing && cd_sn.step[cs.track] == k;
        uint8_t c = !in ? FL_BG1 : head ? FL_INK : T->steps[k].on ? C_ACC(1) : cf_panel(2);
        fb_rbox(x, y, 12, 26, 2, c);
        if (in && T->steps[k].note && T->mode == CD_SEQ)
            fb_rect(x + 3, y + 3, 6, 2, FL_BG);  /* (a recorded note) */
    }
}
static void draw_glo(void)
{
    char b[24];
    str_cpy(b, CD_NOTE[cs.key % 12u], sizeof b);
    str_cpy(b + str_len(b), " MAJ", 6);
    draw_big_title(b, 0);
    kint(b, cd_sn.sounding, " voices sounding");
    fb_text(&CF_FONT_UI, 12, Y_VIEW + 60, b, FL_INK2, 1);
    fb_text(&CF_FONT_UI, 12, Y_VIEW + 76, cs.midi_out ? "MIDI OUT  ch 1-4" : "MIDI OUT  off", FL_INK3, 1);
    fb_text(&CF_FONT_UI, 12, Y_VIEW + 124, FELUCCA_VERSION, FL_INK3, 1);
}
static void draw_msg(void)
{
    int w;
    if (!cu.msg[0] || (int32_t)(cu.now - cu.msg_until) > 0)
        return;
    w = fb_text_w(&CF_FONT_UI, cu.msg, 1) + 12;
    fb_rbox(FB_W - 6 - w, Y_VIEW + H_VIEW - 20, w, 16, 3, C_ACC(1));
    fb_text(&CF_FONT_UI, FB_W - w, Y_VIEW + H_VIEW - 18, cu.msg, FL_BG, 1);
}
static void cf_ui_draw(void)
{
    fb_unclip();
    if (cu.upd_left) {                           /* the update gesture: a countdown over everything */
        char b[2];
        b[0] = (char)('0' + cu.upd_left % 10u);
        b[1] = 0;
        fb_fill(FL_BG);
        fb_text_c(&CF_FONT_UI, 120, 70, b, C_ACC(0), 4);
        fb_text_c(&CF_FONT_UI, 120, 140, cu.upd_stub ? "update mode" : "USB boot", FL_INK, 1);
        fb_text_c(&CF_FONT_UI, 120, 156, "let go to cancel", FL_INK2, 1);
        cf_present();
        return;
    }
    draw_cd_topbar();
    fb_rect(0, Y_VIEW, FB_W, Y_KEYS - Y_VIEW, FL_BG);
    fb_clip(0, Y_VIEW, FB_W, Y_VIEW + H_VIEW);
    switch (cu.page) {
    case PG_PATCH: draw_patch(); break;
    case PG_ARP: draw_arp(); break;
    case PG_SPACE: draw_space(); break;
    case PG_SEQ: draw_seq(); break;
    case PG_GLO: draw_glo(); break;
    default: draw_home(); break;
    }
    draw_msg();
    fb_unclip();
    draw_keys();
    draw_knobs();
    cf_present();
}

/* the power-on picture: the name, a cloud of dots (main.c shows it until the first frame) */
static void cf_splash(void)
{
    int i;
    cf_palette_build();
    fb_full = 1;
    fb_fill(FL_BG);
    for (i = 0; i < 90; i++) {
        uint32_t r = (uint32_t)(i + 1) * 2654435761u;
        fb_rect(120 + (int)(r % 180u) - 90, 100 + (int)((r >> 9) % 60u) - 30, 2, 2, i % 5 ? FL_BG3 : C_ACC(1));
    }
    fb_text_c(&CF_FONT_UI, 120, 140, "CLOUDRIFT", FL_INK, 2);
    fb_text_c(&CF_FONT_UI, 120, 168, FELUCCA_VERSION, FL_INK3, 1);
    cf_present();
}
