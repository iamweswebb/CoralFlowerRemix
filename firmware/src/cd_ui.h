/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors */
/* CLOUDRIFT: the UI's state, the palette, and what CoralFlower's drawing (cf_draw.c, kept as it is) asks of the UI.
 * Included after cf_fb.c and cd_sound.c, before cf_draw.c. */

#include "flora/flora_pal.h"

/* the pages, after the sim: HOME (the four lanes), PATCH (ENV), ARP, SPACE (FX), SEQ, GLO */
enum { PG_HOME, PG_PATCH, PG_ARP, PG_SPACE, PG_SEQ, PG_GLO, PG_N };
static const char *const PG_NAME[PG_N] = {"HOME", "PATCH", "ARP", "SPACE", "SEQ", "GLOBAL"};

typedef struct {
    uint8_t page;
    uint8_t booted;
    uint8_t sel, sel_phys;       /* the SEL layer is up (the short keys: T1..T4, FOLLOW, LEN, SRC); SEL is held */
    uint8_t sel_used;            /* .. a short key acted while SEL was held (its release keeps the layer down) */
    int8_t oct;                  /* OCT- / OCT+: the keys' octave, -2..2 */
    uint8_t rec, freeze;
    uint32_t now, last_ms, frame_dt, idle_ms, frames;
    uint32_t knob_ms[4];         /* when each knob last moved (its cell lights: cf_draw.c) */
    uint32_t pkeys, keys;        /* the keys down: as last read */
    uint8_t keynote[27];         /* the note each key down sent (its release sends the same) */
    char msg[28];                /* a message over the view, a moment */
    uint32_t msg_until;
    uint8_t upd_left, upd_stub;  /* the update gesture's countdown (cd_shim.c cr_update_poll) */
    uint8_t dirty;               /* cs changed since the last save */
    uint32_t changed_ms;
    uint8_t save_want;           /* SAVE pressed: written at the next quiet moment */
} cd_ui_t;
static cd_ui_t cu;

/* ------------------------------------------- what cf_draw.c asks of the UI --- */
#define CF_NKEYS 27
static const uint8_t CD_UPPER_PC[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
static int cf_key_is_upper(int key) { return key >= 0 && key < CF_NKEYS && CD_UPPER_PC[(53 + key) % 12]; }
enum { FL_SP_PLANT = 0, FL_SP_CORAL = 1 };
static struct { uint8_t species; } flo = {FL_SP_CORAL};   /* (the panels take the deep-water tint: cf_draw.c) */
#define FLW 1
#define FLH 1
static uint8_t fl_px[1];

/* ---------------------------------------------------------- colours --- */
/* palette entries 48..55: the accent ramp (the sim's teal: the SEA ramp), twice (C_ACC, C_SEC) */
#define C_ACC(s) ((uint8_t)(48 + (s)))
#define C_SEC(s) ((uint8_t)(52 + (s)))
static uint32_t cf_pal[64];
#ifndef CF_EMU
/* the FM-1's panel lifts the darks and washes out soft colours (CoralFlower's correction, cf_state.h: the owner's
 * photos): each colour's saturation raised away from its luma, then a gamma of 1.2 */
static uint32_t cf_panel_fix(uint32_t c, int colour)
{
    int32_t ch[3] = {(int32_t)((c >> 16) & 255u), (int32_t)((c >> 8) & 255u), (int32_t)(c & 255u)};
    int32_t y = (ch[0] * 77 + ch[1] * 150 + ch[2] * 29) >> 8, i, v, mx, mn, sat, k;
    uint32_t out = 0;
    mx = ch[0] > ch[1] ? ch[0] : ch[1];
    mx = mx > ch[2] ? mx : ch[2];
    mn = ch[0] < ch[1] ? ch[0] : ch[1];
    mn = mn < ch[2] ? mn : ch[2];
    sat = mx ? (mx - mn) * 256 / mx : 0;
    k = colour ? 637 - ((258 * ((sat * sat) >> 8)) >> 8) : 410;
    for (i = 0; i < 3; i++) {
        uint32_t g;
        v = y + (((ch[i] - y) * k) >> 8);
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        g = (uint32_t)v;                         /* gamma 1.2: (v / 255)^1.2 x 255, as a product */
        g = g * g / 255u;
        g = ((uint32_t)v * 4u + g) / 5u;
        out = (out << 8) | (g > 255u ? 255u : g);
    }
    return out;
}
#endif
static void cf_palette_build(void)
{
    uint32_t i;
    for (i = 0; i < FL_PAL_SIZE; i++)
        cf_pal[i] = fl_pal_rgb[i];
    cf_pal[48] = 0x8FD3CBu;                      /* the sim's accent, then the SEA ramp's darker steps */
    for (i = 1; i < 4u; i++)
        cf_pal[48 + i] = fl_pal_rgb[FL_C(FL_R_SEA, i)];
    for (i = 0; i < 4u; i++)
        cf_pal[52 + i] = cf_pal[48 + i];
    for (i = 0; i < 8u; i++)
        cf_pal[56 + i] = fl_back_rgb[i];
#if !defined CF_EMU && !defined CF_PANEL_PLAIN
    for (i = 0; i < 64u; i++)
        cf_pal[i] = cf_panel_fix(cf_pal[i], i >= FL_RAMP0);
#endif
    fb_palette(cf_pal, 64);
}

static void cf_message(const char *s)
{
    str_cpy(cu.msg, s, sizeof cu.msg);
    cu.msg_until = (cu.now ? cu.now : fm1_ms) + 1300u;
}
