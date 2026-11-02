/* SPDX-License-Identifier: GPL-3.0-only
 * CoralFlower: the effects, as the stock FM-1 has them: Filter, Reverb, Delay, Distortion, Chorus and Phaser in one
 * chain whose order the player sets, on the master bus (both parts, after they are mixed: fx.c's FX_MIX_HOOK, before
 * the master and its limiter). Each has an On/Off and three values 0..100, the stock set:
 *   Filter      TYPE (low pass, band pass, high pass), CUTOFF (100 Hz .. 20 kHz in 100 steps), RESONANCE
 *   Reverb      TYPE (room, hall, plate), DECAY, MIX
 *   Delay       TIME (16 .. 740 ms), FEEDBACK, MIX
 *   Distortion  DRIVE, TONE, LEVEL
 *   Chorus      RATE, DEPTH, MIX
 *   Phaser      RATE, DEPTH, MIX
 * The reverb, the delay and the chorus are ChoralRoot's (cr_fx.c: xr_run, xd_run, xc_block, their state and their
 * parameters xfp[]); here each is fed the bus in mono and its stereo wet goes back into the bus by MIX, in the chain's
 * order. The filter (a state-variable filter in the topology-preserving form, after Zavalishin's "The Art of VA Filter
 * Design"), the distortion (dsp.c's soft clip and a one-pole tone) and the phaser (six first-order all-passes a side
 * on a slow sine, the right side a quarter turn on) are new.
 * MIX: the reverb and the delay keep most of the dry sound (dry 1 - mix / 2, wet x mix); the chorus and the phaser
 * blend (dry 1 - mix / 2, wet mix / 2: at 100 the two halves, where their effect is strongest). An effect switched off
 * takes no new sound, but a reverb's or a delay's tail rings out.
 * Included after fx.c (cr_fx.c's functions), before cf_state.h (cf_set_t holds a cf_fx_t). */

#if CR_FX_DELAY_DS != 1 || CR_FX_REVERB_DS != 2
#error "cf_fx.c: the delay at the full rate, the reverb at half (cr_fx.c's defaults)"
#endif

enum { CFX_FILTER, CFX_REVERB, CFX_DELAY, CFX_DIST, CFX_CHORUS, CFX_PHASER, CFX_N };
#define CL_TYPE 3u                       /* the reverb's TYPE (its first value): Room, Hall, Plate, Cloud (cl_run) */
typedef struct {
    uint8_t order[CFX_N];            /* the chain, first to last: effect ids */
    uint8_t on;                      /* bit e: effect e is on */
    uint8_t v[CFX_N][3];             /* each effect's three values 0..100 (the filter's and the reverb's first: the type
                                      * 0..2; the filter's cutoff 0..99) */
} cf_fx_t;

/* a new unit's chain (the owner's settings): the classic order, a short delay (75 ms, 35 %, mix 8 %) and a touch of
 * hall (decay 40 %, mix 5 %) on, the rest off */
static const cf_fx_t CFX_DEFAULT = {
    {CFX_FILTER, CFX_DIST, CFX_CHORUS, CFX_PHASER, CFX_DELAY, CFX_REVERB},
    (1u << CFX_REVERB) | (1u << CFX_DELAY),
    {{0, 99, 10}, {1, 40, 5}, {40, 35, 8}, {40, 50, 70}, {30, 45, 50}, {30, 60, 60}},
};

/* tables (python, in the comment of each): the filter's g = tan(pi fc / fs) in Q16 for 100 cutoffs 100 Hz .. 20 kHz
 * (log), and their Hz; its k = 1 / Q in Q14 for resonance 0..100 (Q 0.5 .. 12); the distortion tone's one-pole k in
 * Q15 for 700 Hz .. 16 kHz; the phaser's all-pass a = (t - 1) / (t + 1), t = tan(pi f / fs), in Q15 for 64 corners
 * 120 Hz .. 7 kHz; its LFO's phase step a block of 32 for 0.04 .. 6 Hz */
static const int32_t CFX_G[100] = {
    467, 493, 520, 548, 578, 610, 644, 679,
    716, 756, 797, 841, 887, 936, 988, 1042,
    1099, 1160, 1224, 1291, 1362, 1437, 1516, 1599,
    1687, 1780, 1878, 1981, 2090, 2205, 2326, 2454,
    2589, 2732, 2882, 3041, 3208, 3385, 3571, 3768,
    3976, 4195, 4426, 4670, 4928, 5200, 5487, 5791,
    6111, 6449, 6806, 7183, 7581, 8002, 8447, 8917,
    9413, 9939, 10494, 11082, 11703, 12361, 13058, 13797,
    14579, 15409, 16290, 17225, 18219, 19276, 20401, 21600,
    22879, 24246, 25708, 27275, 28958, 30768, 32720, 34832,
    37123, 39616, 42341, 45332, 48633, 52296, 56391, 61004,
    66249, 72279, 79301, 87608, 97624, 109989, 125716, 146515,
    175510, 219087, 292679, 445566,
};
/* (its Hz, for the screen) */
static const uint16_t CFX_HZ[100] = {
    100, 105, 111, 117, 124, 131, 138, 145, 153, 162,
    171, 180, 190, 201, 212, 223, 235, 248, 262, 276,
    292, 308, 325, 342, 361, 381, 402, 424, 448, 472,
    498, 525, 554, 585, 617, 651, 687, 724, 764, 806,
    851, 897, 947, 999, 1054, 1112, 1173, 1237, 1305, 1377,
    1453, 1532, 1617, 1706, 1799, 1898, 2003, 2113, 2229, 2351,
    2481, 2617, 2761, 2913, 3073, 3242, 3420, 3608, 3806, 4016,
    4236, 4469, 4715, 4974, 5248, 5536, 5840, 6162, 6500, 6858,
    7235, 7632, 8052, 8495, 8962, 9454, 9974, 10522, 11101, 11711,
    12355, 13034, 13751, 14507, 15304, 16146, 17033, 17970, 18958, 20000,
};
static const uint16_t CFX_K[101] = {
    32768, 31743, 30750, 29788, 28856, 27954, 27079, 26232, 25412, 24617,
    23847, 23101, 22378, 21678, 21000, 20343, 19707, 19090, 18493, 17915,
    17354, 16811, 16286, 15776, 15283, 14805, 14342, 13893, 13458, 13037,
    12630, 12234, 11852, 11481, 11122, 10774, 10437, 10110, 9794, 9488,
    9191, 8904, 8625, 8355, 8094, 7841, 7595, 7358, 7128, 6905,
    6689, 6480, 6277, 6080, 5890, 5706, 5528, 5355, 5187, 5025,
    4868, 4715, 4568, 4425, 4287, 4153, 4023, 3897, 3775, 3657,
    3542, 3432, 3324, 3220, 3120, 3022, 2927, 2836, 2747, 2661,
    2578, 2497, 2419, 2344, 2270, 2199, 2130, 2064, 1999, 1937,
    1876, 1817, 1761, 1706, 1652, 1600, 1550, 1502, 1455, 1409,
    1365,
};
static const uint16_t CFX_TONE[101] = {
    3110, 3204, 3301, 3400, 3502, 3607, 3715, 3826, 3940, 4057,
    4177, 4301, 4428, 4559, 4693, 4830, 4972, 5117, 5265, 5418,
    5575, 5736, 5900, 6069, 6243, 6420, 6602, 6789, 6980, 7175,
    7376, 7581, 7790, 8005, 8225, 8449, 8678, 8913, 9152, 9397,
    9647, 9902, 10162, 10427, 10697, 10973, 11253, 11539, 11830, 12126,
    12427, 12733, 13044, 13360, 13680, 14005, 14335, 14669, 15007, 15350,
    15696, 16046, 16400, 16757, 17117, 17481, 17847, 18215, 18586, 18959,
    19333, 19708, 20085, 20461, 20839, 21216, 21592, 21968, 22342, 22715,
    23086, 23454, 23819, 24181, 24538, 24892, 25241, 25585, 25923, 26256,
    26582, 26901, 27213, 27518, 27815, 28103, 28384, 28655, 28918, 29171,
    29415,
};
static const int16_t CFX_PA[64] = {
    -32212, -32176, -32137, -32095, -32051, -32003, -31953, -31899,
    -31842, -31782, -31717, -31648, -31575, -31497, -31413, -31325,
    -31231, -31131, -31025, -30912, -30792, -30664, -30529, -30385,
    -30232, -30069, -29897, -29714, -29521, -29315, -29097, -28866,
    -28622, -28363, -28090, -27800, -27494, -27171, -26829, -26469,
    -26088, -25687, -25265, -24820, -24352, -23859, -23341, -22797,
    -22226, -21627, -20999, -20341, -19652, -18931, -18176, -17387,
    -16563, -15701, -14802, -13862, -12880, -11854, -10783, -9662,
};
static const uint32_t CFX_PRATE[101] = {
    124661, 131067, 137801, 144882, 152326, 160153,
    168382, 177034, 186131, 195695, 205750, 216322,
    227438, 239124, 251411, 264329, 277911, 292191,
    307205, 322990, 339586, 357035, 375380, 394668,
    414947, 436269, 458685, 482254, 507034, 533087,
    560478, 589277, 619556, 651390, 684861, 720051,
    757049, 795949, 836847, 879846, 925055, 972587,
    1022562, 1075104, 1130346, 1188426, 1249491, 1313694,
    1381195, 1452165, 1526781, 1605232, 1687713, 1774433,
    1865609, 1961469, 2062255, 2168220, 2279629, 2396763,
    2519916, 2649396, 2785530, 2928659, 3079142, 3237357,
    3403702, 3578594, 3762472, 3955799, 4159059, 4372764,
    4597449, 4833680, 5082048, 5343179, 5617727, 5906382,
    6209869, 6528950, 6864426, 7217141, 7587978, 7977871,
    8387797, 8818786, 9271921, 9748339, 10249237, 10775873,
    11329569, 11911715, 12523773, 13167281, 13843854, 14555192,
    15303080, 16089397, 16916117, 17785316, 18699177,
};

static const uint16_t CFX_DRIVE[101] = {   /* the distortion's drive, Q8: 1 .. 40 times, exponential */
    256, 266, 276, 286, 297, 308, 319, 331, 344, 357,
    370, 384, 399, 414, 429, 445, 462, 479, 497, 516,
    535, 555, 576, 598, 620, 644, 668, 693, 719, 746,
    774, 803, 833, 865, 897, 931, 966, 1002, 1040, 1079,
    1120, 1162, 1205, 1251, 1298, 1346, 1397, 1449, 1504, 1560,
    1619, 1680, 1743, 1809, 1877, 1947, 2020, 2096, 2175, 2257,
    2341, 2429, 2521, 2615, 2714, 2816, 2921, 3031, 3145, 3263,
    3386, 3513, 3645, 3782, 3924, 4072, 4225, 4384, 4548, 4719,
    4897, 5081, 5271, 5470, 5675, 5888, 6110, 6339, 6577, 6825,
    7081, 7347, 7623, 7910, 8207, 8515, 8835, 9167, 9512, 9869,
    10240,
};

static volatile uint32_t cfx_quiet;  /* blocks since the chain's output was last above -54 dBFS: a settings save waits
                                      * for a second of it (cf_settings.c), so a flash erase (the audio stops ~45 ms
                                      * on the unit) never lands in a reverb's or a delay's tail */
static cf_fx_t cfx_set;              /* the chain as the ISR uses it (cf_fx_apply copies cs.fx in, IRQ off) */
static uint8_t cfx_ready;
static uint8_t cfx_cl[4] = {45, 60, 45, 35};   /* the Cloud's CLOUD, BLOOM, TONE, DUCK 0..100 (cs.cloud: cf_cloud_apply;
                                      * the sim's defaults) */
static volatile uint8_t cfx_frz;     /* the Cloud's FREEZE (the FX screen's LFO; not kept: off at power-on) */

static struct {
    uint32_t hash;                   /* of cfx_set: what the values below were worked out from */
    int32_t fg, fg_t, fa1, fa2, fa3, fk;   /* filter: g now and its target (Q16, gliding), the TPT's a1..a3 (Q15), k
                                            * (Q14) */
    int32_t fs[2][2];                /* .. each side's two integrators */
    int32_t dg, dk, dl, dlp[2];      /* distortion: drive (Q8), the tone's k (Q15), level (Q8); the tone's states */
    uint32_t pph, pinc;              /* phaser: the LFO's phase, its step a block */
    int32_t pdep, px[2][6], py[2][6], pfb[2];
    int32_t wg[CFX_N], wt[CFX_N];    /* each effect's wet gain now and set (Q15, eased a block at a time) */
    int32_t m[CTL], wl[CTL], wr[CTL];
} cfx __attribute__((section(".pool")));

#define CFX_MUL(a, b, sh) ((int32_t)(((int64_t)(a) * (b)) >> (sh)))

static uint32_t cfx_fnv(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

/* the UI's chain -> the ISR's (after any change, and at power-on) */
static void cf_fx_apply(const cf_fx_t *s)
{
    fm1_irq_off();
    cfx_set = *s;
    cfx_ready = 1;
    fm1_irq_on();
}

/* the Cloud's four macros (cs.cloud) -> the ISR's; FREEZE on or off */
static void cf_cloud_apply(const uint8_t *p)
{
    uint32_t i;
    fm1_irq_off();
    for (i = 0; i < 4u; i++)
        cfx_cl[i] = p[i] > 100u ? 100u : p[i];
    fm1_irq_on();
}
static void cf_cloud_freeze(int on) { cfx_frz = (uint8_t)(on != 0); }

static void cv_coefs(uint32_t type, uint32_t decay);   /* (the reverb, below) */
static void cl_coefs(uint32_t space, int on);         /* (the Cloud, below) */

/* the values -> what each effect runs on (a change of the set: once) */
static void cfx_coefs(void)
{
    const cf_fx_t *s = &cfx_set;
    const uint8_t *f = s->v[CFX_FILTER], *rv = s->v[CFX_REVERB], *dl = s->v[CFX_DELAY], *ds = s->v[CFX_DIST];
    const uint8_t *ch = s->v[CFX_CHORUS], *ph = s->v[CFX_PHASER];
    uint32_t t = rv[0] > 2u ? 2u : rv[0], e;
    int32_t m;
    cfx.fg_t = CFX_G[f[1] > 99u ? 99u : f[1]];
    cfx.fk = CFX_K[f[2] > 100u ? 100u : f[2]];
    if (rv[0] == CL_TYPE)                               /* (the plate keeps its last set, fading out if it rang) */
        cl_coefs(rv[1], (int)((s->on >> CFX_REVERB) & 1u));   /* (DECAY's value is the Cloud's SPACE) */
    else
        cv_coefs(t, rv[1]);
    xfp[XP_DSYNC] = 0;                                  /* the stock delay's time is free, in ms */
    xfp[XP_DMODE] = 1;                                  /* ping-pong */
    xfp[XP_DTIME] = (int16_t)(dl[0] * 127u / 100u);
    xfp[XP_DFDBK] = (int16_t)(dl[1] * 118u / 100u);     /* (to 0.92: a long tail, never a runaway) */
    song.g[G_CRATE] = (int16_t)(8 + ch[0] * 100u / 100u);
    song.g[G_CDEPTH] = (int16_t)(ch[1] * 127u / 100u);
    cfx.dg = CFX_DRIVE[ds[0] > 100u ? 100u : ds[0]];
    cfx.dk = CFX_TONE[ds[1] > 100u ? 100u : ds[1]];
    cfx.dl = (int32_t)ds[2] * 300 / 100;                /* level 0 .. 1.17 */
    cfx.pinc = CFX_PRATE[ph[0] > 100u ? 100u : ph[0]];
    cfx.pdep = (int32_t)ph[1] * 32 * 256 / 100;         /* the sweep: up to 32 of the 64 corners each way */
    for (e = 0; e < CFX_N; e++) {
        m = e == CFX_DIST || e == CFX_FILTER ? 0 : (int32_t)s->v[e][2];
        cfx.wt[e] = m * 32768 / 100;
    }
}

/* the filter's coefficients from g (Q16) and k (Q14): a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2 */
static void cfx_filter_coefs(void)
{
    int32_t g = cfx.fg;
    uint32_t den = 65536u + (uint32_t)(((int64_t)g * (g + cfx.fk * 4)) >> 16);
    cfx.fa1 = (int32_t)((1u << 31) / den);
    cfx.fa2 = CFX_MUL(g, cfx.fa1, 16);
    cfx.fa3 = CFX_MUL(g, cfx.fa2, 16);
}

static void cfx_filter(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t a1, a2, a3, k = cfx.fk, type = cfx_set.v[CFX_FILTER][0];
    uint32_t c, i;
    if (cfx.fg != cfx.fg_t || !cfx.fa1) {               /* the cutoff glides: a quarter of the way a block */
        int32_t d = cfx.fg_t - cfx.fg;
        cfx.fg += d / 4 ? d / 4 : d;
        cfx_filter_coefs();
    }
    a1 = cfx.fa1;
    a2 = cfx.fa2;
    a3 = cfx.fa3;
    for (c = 0; c < 2u; c++) {
        int32_t *b = c ? r : l, s0 = cfx.fs[c][0], s1 = cfx.fs[c][1];
        for (i = 0; i < n; i++) {
            int32_t x = b[i], v3 = x - s1;
            int32_t v1 = (int32_t)(((int64_t)a1 * s0 + (int64_t)a2 * v3) >> 15);
            int32_t v2 = s1 + (int32_t)(((int64_t)a2 * s0 + (int64_t)a3 * v3) >> 15);
            s0 = 2 * v1 - s0;
            s1 = 2 * v2 - s1;
            b[i] = type == 0 ? v2 : type == 1 ? CFX_MUL(k, v1, 14) : x - CFX_MUL(k, v1, 14) - v2;
        }
        cfx.fs[c][0] = s0;
        cfx.fs[c][1] = s1;
    }
}

static void cfx_dist(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t c, i;
    for (c = 0; c < 2u; c++) {
        int32_t *b = c ? r : l, lp = cfx.dlp[c];
        for (i = 0; i < n; i++) {
            int32_t x = CFX_MUL(b[i], cfx.dg, 8), y;
            y = softclip(x < -(1 << 20) ? -(1 << 20) : x > (1 << 20) ? (1 << 20) : x);
            lp += CFX_MUL(y - lp, cfx.dk, 15);
            b[i] = CFX_MUL(lp, cfx.dl, 8);
        }
        cfx.dlp[c] = lp;
    }
}

/* the wet back into the bus: dry x (1 - w / 2) + wet x w x boost, w the eased wet gain (Q15) */
static void cfx_mix(int32_t *l, int32_t *r, uint32_t e, uint32_t n, int32_t boost_q8, int blend)
{
    int32_t w = cfx.wg[e], dg, wg;
    uint32_t i;
    if (w != cfx.wt[e])
        cfx.wg[e] = w = w + (cfx.wt[e] - w) / 8 + (cfx.wt[e] > w ? 1 : cfx.wt[e] < w ? -1 : 0);
    dg = 32768 - w / 2;
    wg = CFX_MUL(blend ? w / 2 : w, boost_q8, 8);
    for (i = 0; i < n; i++) {
        l[i] = (int32_t)(((int64_t)l[i] * dg + (int64_t)cfx.wl[i] * wg) >> 15);
        r[i] = (int32_t)(((int64_t)r[i] * dg + (int64_t)cfx.wr[i] * wg) >> 15);
    }
}

/* the bus in mono for the send-style effects (feed 0: silence, the effect's tail only) and the wet buffers cleared */
static int32_t cfx_send(const int32_t *l, const int32_t *r, uint32_t n, int feed)
{
    int32_t any = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        cfx.m[i] = feed ? (l[i] + r[i]) >> 1 : 0;
        any |= cfx.m[i];
        cfx.wl[i] = cfx.wr[i] = 0;
    }
    return any;
}

/* cr_fx.c's coefficients follow xfp[] and the tempo (xf_block's own first part) */
static void cfx_xf_prepare(void)
{
    uint32_t i, h = xf_bpm;
    for (i = 0; i < XP_N; i++)
        h = h * 31u + (uint16_t)xfp[i];
    if (h == xf.hash && xf.on)
        return;
    xf.hash = h;
    xf_coefs();
    if (!xf.on) {                                       /* power-on: everything that glides starts where it is set */
        for (i = 0; i < 8u; i++) {
            xf.r_cur[i] = xf.r_tgt[i];
            xf.r_gc[i] = xf.r_g[i];
        }
        xf.d_cur[0] = xf.d_tgt[0];
        xf.d_cur[1] = xf.d_tgt[1];
        xf.d_fbc = xf.d_fb;
        xf.d_kc = xf.d_k;
        xf.r_kc = xf.r_k;
        xf.d_pm = xf.d_ping ? 256 : 0;
        xf.r_igc = xf.r_ig;
        xf.r_wc = xf.r_w;
        xf.r_pcur = xf.r_pre;
        xf.on = 1;
    }
}

static void cfx_delay(int32_t *l, int32_t *r, uint32_t n, int feed)
{
    if (!cfx_send(l, r, n, feed) && !xf.d_live)
        return;
    xf.d_ph[0] += (uint32_t)(((uint64_t)330u << 32) * CTL / (1000u * FS));   /* the wow (xf_block's) */
    xf.d_ph[1] += (uint32_t)(((uint64_t)3100u << 32) * CTL / (1000u * FS));
    xf.d_wt = ((osc_sine(xf.d_ph[0]) + (osc_sine(xf.d_ph[1]) >> 3)) * (xf.d_wow >> 3)) >> 12;
    XF_CALL(xd_run)(cfx.m, cfx.wl, cfx.wr, n, XF_IN);
    if (!xf.d_live)
        xf.d_lp[0] = xf.d_lp[1] = xf.d_hp[0] = xf.d_hp[1] = 0;
    cfx_mix(l, r, CFX_DELAY, n, 768, 0);
}

/* ---- the reverb: Dattorro's plate (J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES
 * 1997), in integers, at 22.05 kHz between cr_fx.c's half-band filters (the wet's band ends near 10 kHz anyway). The
 * input: a pre-delay, a low cut at 200 Hz and a high cut at 8 kHz (two one-poles each), the bandwidth one-pole, four
 * diffusing all-passes; then the figure-eight tank, two halves each of a swinging all-pass, a delay, the damping
 * one-pole, the decay, an all-pass and a delay, each feeding the other; seven taps a side make the stereo out. FoMni
 * (GPL-3.0) runs the same figure in floats; this is written anew in fixed point. The type sets SIZE (every length and
 * tap scaled: a room is a smaller plate), the pre-delay, the damping and the bandwidth, and the span the DECAY value
 * covers. Lines int16 (the input a quarter of the bus: room for the tank's sums), states int32, gains Q15. */
#define CV_DS(n) ((n) * 22050 / 29761)            /* the paper's lengths (29761 Hz) at 22.05 kHz */
#define CV_PRE 1103                               /* the longest pre-delay: 50 ms */
#ifndef CV_EXC
#define CV_EXC 3                                  /* the swinging all-passes' excursion, samples (12 made the tail
                                                   * of a few pure tones swell and dip by up to 10 dB) */
#endif
enum { V_PRE, V_IN1, V_IN2, V_IN3, V_IN4, V_APL, V_D1L, V_AP2L, V_D2L, V_APR, V_D1R, V_AP2R, V_D2R, V_N };
static const uint16_t CV_L[V_N] = {CV_PRE, CV_DS(142), CV_DS(107), CV_DS(379), CV_DS(277), CV_DS(672), CV_DS(4453),
                                   CV_DS(1800), CV_DS(3720), CV_DS(908), CV_DS(4217), CV_DS(2656), CV_DS(3163)};
#define CV_TOTAL (CV_PRE + CV_DS(142) + CV_DS(107) + CV_DS(379) + CV_DS(277) + CV_DS(672) + CV_DS(4453) + \
                  CV_DS(1800) + CV_DS(3720) + CV_DS(908) + CV_DS(4217) + CV_DS(2656) + CV_DS(3163) + 2 * V_N + \
                  2 * (CV_EXC + 2))           /* (cv_init: 2 more a line, the swing's room on the two that swing) */
/* the output taps: line, the paper's position, sign (left, then right) */
static const struct { uint8_t line; uint16_t pos; int8_t sign; } CV_TAP[14] = {
    {V_D1R, 266, 1}, {V_D1R, 2974, 1}, {V_AP2R, 1913, -1}, {V_D2R, 1996, 1}, {V_D1L, 1990, -1}, {V_AP2L, 187, -1},
    {V_D2L, 1066, -1},
    {V_D1L, 353, 1}, {V_D1L, 3627, 1}, {V_AP2L, 1228, -1}, {V_D2L, 2673, 1}, {V_D1R, 2111, -1}, {V_AP2R, 335, -1},
    {V_D2R, 121, -1}};
/* the types: size (Q15 of the paper's), pre-delay (samples), the tank's damping (one-pole k, Q15). The owner found the
 * first tuning muddy (its tail was 94 % between 200 and 500 Hz, nothing above 1 kHz, 3.7 s at DECAY 45): now the
 * damping is light (the 8 kHz input cut does the rest), the input gets a little presence above 1 kHz, a low cut inside
 * the tank makes the low mids die first, the pre-delay keeps the wet off the note, and DECAY is an RT60 (CV_DEC) */
static const struct { uint16_t size, pre, damp; } CV_TYPE[3] = {
    {13800, 220, 26840},                         /* Room: a small plate, close (10 ms), 0.3 .. 2.5 s */
    {32767, 882, 29415},                         /* Hall: the full size, 40 ms, 0.8 .. 7 s */
    {24500, 330, 30872},                         /* Plate: bright and dense, 15 ms, 0.5 .. 5 s */
};
static const uint16_t CV_DEC[3][101] = {   /* the tank's decay (Q15) for DECAY 0..100: RT60 from the type's span, the
                                                * half-loop at its size; dec = 10^(-1.5 Th / RT60), two passes a half */
    {5637, 5849, 6064, 6283, 6504, 6729, 6956, 7186, 7418, 7653, 7890, 8130, 8371, 8614, 8859, 9105, 9353, 9603, 9853, 10105, 10357, 10610, 10864, 11119, 11374, 11629, 11885, 12140, 12396, 12651, 12906, 13161, 13415, 13669, 13922, 14174, 14426, 14676, 14926, 15174, 15421, 15667, 15911, 16154, 16396, 16636, 16874, 17111, 17345, 17578, 17810, 18039, 18266, 18492, 18715, 18936, 19155, 19372, 19587, 19800, 20010, 20218, 20424, 20628, 20829, 21028, 21224, 21419, 21611, 21800, 21987, 22172, 22355, 22535, 22712, 22888, 23061, 23231, 23399, 23565, 23729, 23890, 24049, 24206, 24360, 24512, 24662, 24809, 24954, 25097, 25238, 25377, 25513, 25648, 25780, 25910, 26038, 26164, 26287, 26409, 26529},
    {6836, 7070, 7307, 7546, 7787, 8031, 8277, 8525, 8775, 9027, 9280, 9535, 9790, 10048, 10306, 10565, 10824, 11085, 11346, 11607, 11868, 12130, 12391, 12652, 12913, 13174, 13434, 13693, 13952, 14210, 14467, 14723, 14978, 15232, 15484, 15735, 15985, 16233, 16480, 16725, 16968, 17209, 17448, 17686, 17922, 18155, 18387, 18616, 18843, 19068, 19291, 19512, 19730, 19946, 20159, 20371, 20580, 20786, 20990, 21192, 21391, 21587, 21781, 21973, 22162, 22349, 22533, 22715, 22894, 23071, 23246, 23418, 23587, 23754, 23918, 24081, 24240, 24398, 24552, 24705, 24855, 25003, 25149, 25292, 25433, 25571, 25708, 25842, 25974, 26104, 26231, 26357, 26480, 26602, 26721, 26838, 26953, 27067, 27178, 27287, 27395},
    {5026, 5245, 5468, 5696, 5927, 6162, 6401, 6644, 6889, 7138, 7390, 7645, 7903, 8163, 8425, 8690, 8956, 9225, 9495, 9766, 10039, 10313, 10588, 10864, 11140, 11417, 11695, 11972, 12250, 12527, 12804, 13081, 13358, 13633, 13908, 14182, 14455, 14727, 14997, 15267, 15534, 15800, 16065, 16328, 16589, 16848, 17105, 17360, 17613, 17863, 18112, 18358, 18602, 18843, 19082, 19318, 19552, 19783, 20012, 20237, 20461, 20681, 20899, 21114, 21326, 21536, 21743, 21947, 22148, 22346, 22542, 22734, 22924, 23112, 23296, 23478, 23656, 23833, 24006, 24177, 24344, 24510, 24672, 24832, 24989, 25144, 25296, 25445, 25592, 25737, 25879, 26018, 26155, 26290, 26422, 26552, 26679, 26804, 26927, 27048, 27166},
};

static int16_t cv_buf[CV_TOTAL] __attribute__((section(".pool")));
static struct {
    uint16_t base[V_N], n[V_N], w[V_N];          /* each line's place in cv_buf, its length, its next write */
    uint16_t len[V_N], tap[14];                  /* the lengths and taps at this size */
    int32_t pre, damp_k, dec;                    /* pre-delay; damping (Q15); decay (Q15) */
    int32_t hp[2], lp[2], pr, ls, dmp[2], tlc[2];   /* the input's filters, presence, low shelf; the tank's damping, its
                                                 * low cut */
    uint32_t ph, idle;                           /* the swing's phase; samples since anything went in */
    uint8_t ready;
} cv;

static void cv_init(void)                        /* the lines laid out in cv_buf (once) */
{
    uint32_t i, at = 0;
    for (i = 0; i < V_N; i++) {
        cv.base[i] = (uint16_t)at;
        cv.n[i] = (uint16_t)(CV_L[i] + 2u + (i == V_APL || i == V_APR ? CV_EXC + 2u : 0u));
        cv.w[i] = 0;
        at += cv.n[i];
    }
    cv.ready = 1;
}

/* the type and the DECAY value -> the lengths, the taps and the gains */
static void cv_coefs(uint32_t type, uint32_t decay)
{
    uint32_t i, sz = CV_TYPE[type].size;
    if (!cv.ready)
        cv_init();
    for (i = 0; i < V_N; i++) {
        uint32_t l = i == V_PRE ? CV_TYPE[type].pre : (CV_L[i] * sz) >> 15;
        cv.len[i] = (uint16_t)(l < 1u ? 1u : l);
    }
    for (i = 0; i < 14u; i++) {
        uint32_t t = ((uint32_t)CV_DS(CV_TAP[i].pos) * sz) >> 15;
        cv.tap[i] = (uint16_t)(t < 1u ? 1u : t >= cv.len[CV_TAP[i].line] ? cv.len[CV_TAP[i].line] - 1u : t);
    }
    cv.pre = CV_TYPE[type].pre;
    cv.damp_k = CV_TYPE[type].damp;
    cv.dec = CV_DEC[type][decay > 100u ? 100u : decay];
}

XF_AI int32_t cv_tap(uint32_t line, uint32_t k)  /* the value written k samples ago, k >= 1 */
{
    int32_t j = (int32_t)cv.w[line] - (int32_t)k;
    return cv_buf[cv.base[line] + (uint32_t)(j < 0 ? j + cv.n[line] : j)];
}
XF_AI void cv_push(uint32_t line, int32_t v)
{
    cv_buf[cv.base[line] + cv.w[line]] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    if (++cv.w[line] >= cv.n[line])
        cv.w[line] = 0;
}
XF_AI int32_t cv_ap(uint32_t line, int32_t x, int32_t g)   /* an all-pass of the line's length: out z + g v, v x - g z */
{
    int32_t z = cv_tap(line, cv.len[line]), v = x - ((g * z) >> 15);
    cv_push(line, v);
    return z + ((g * v) >> 15);
}
XF_AI int32_t cv_ap_mod(uint32_t line, int32_t x, int32_t g, int32_t off_q8)   /* .. its length swinging (Q8) */
{
    int32_t q = ((int32_t)cv.len[line] << 8) + off_q8, k = q >> 8, f = q & 255;
    int32_t z0 = cv_tap(line, (uint32_t)k), z = z0 + (((cv_tap(line, (uint32_t)k + 1u) - z0) * f) >> 8);
    int32_t v = x - ((g * z) >> 15);
    cv_push(line, v);
    return z + ((g * v) >> 15);
}

/* a half-rate block of the mono send -> the stereo wet (half rate). dec: the tank's decay for this block */
static void cv_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t nh, int32_t dec)
{
    uint32_t j, t;
    int32_t sl, sc, any = 0;
    cv.ph += 2600000u * nh;                       /* the swing: ~0.4 Hz at 22.05 kHz, once a block */
    sl = (osc_sine(cv.ph) * CV_EXC) >> 7;         /* (Q8 samples) */
    sc = (osc_sine(cv.ph + 0x40000000u) * CV_EXC) >> 7;
    for (j = 0; j < nh; j++) {
        int32_t x = in[j] >> 2, a, b, fb_l, fb_r, o;
        any |= x;
        cv_push(V_PRE, x);
        x = cv.pre ? cv_tap(V_PRE, (uint32_t)cv.pre) : x;
        cv.hp[0] += ((x - cv.hp[0]) * 1815) >> 15;                /* the low cut: 200 Hz, two poles */
        x -= cv.hp[0];
        cv.hp[1] += ((x - cv.hp[1]) * 1815) >> 15;
        x -= cv.hp[1];
        cv.ls += ((x - cv.ls) * 3526) >> 15;                      /* the low mids under ~400 Hz, -4 dB: the mud */
        x -= (cv.ls * 12124) >> 15;
        cv.pr += ((x - cv.pr) * 8125) >> 15;                      /* presence: what is above ~1 kHz, +4 dB */
        x += ((x - cv.pr) * 19661) >> 15;
        cv.lp[0] += ((x - cv.lp[0]) * 29415) >> 15;               /* the high cut: 8 kHz, two poles */
        cv.lp[1] += ((cv.lp[0] - cv.lp[1]) * 29415) >> 15;
        x = cv_ap(V_IN1, cv.lp[1], 24576);                        /* the input diffusers: 0.75, 0.75, 0.625, 0.625 */
        x = cv_ap(V_IN2, x, 24576);
        x = cv_ap(V_IN3, x, 20480);
        x = cv_ap(V_IN4, x, 20480);
        fb_l = cv_tap(V_D2R, cv.len[V_D2R]);                      /* the tank: each half fed by the other's end */
        fb_r = cv_tap(V_D2L, cv.len[V_D2L]);
        a = cv_ap_mod(V_APL, x + ((fb_l * dec) >> 15), -22938, sl);
        cv_push(V_D1L, a);
        b = cv_tap(V_D1L, cv.len[V_D1L]);
        cv.dmp[0] += ((b - cv.dmp[0]) * cv.damp_k) >> 15;
        cv.tlc[0] += ((cv.dmp[0] - cv.tlc[0]) * 2685) >> 15;     /* the tank's low cut, 300 Hz: low mids die first */
        cv_push(V_D2L, cv_ap(V_AP2L, ((cv.dmp[0] - cv.tlc[0]) * dec) >> 15, 16384));
        a = cv_ap_mod(V_APR, x + ((fb_r * dec) >> 15), -22938, sc);
        cv_push(V_D1R, a);
        b = cv_tap(V_D1R, cv.len[V_D1R]);
        cv.dmp[1] += ((b - cv.dmp[1]) * cv.damp_k) >> 15;
        cv.tlc[1] += ((cv.dmp[1] - cv.tlc[1]) * 2685) >> 15;
        cv_push(V_D2R, cv_ap(V_AP2R, ((cv.dmp[1] - cv.tlc[1]) * dec) >> 15, 16384));
        for (t = 0, o = 0; t < 7u; t++)
            o += CV_TAP[t].sign * cv_tap(CV_TAP[t].line, cv.tap[t]);
        ol[j] = o * 5;                                            /* 0.6 x 7 taps, x 4 back from the quarter, x 2 */
        for (t = 7, o = 0; t < 14u; t++)
            o += CV_TAP[t].sign * cv_tap(CV_TAP[t].line, cv.tap[t]);
        orr[j] = o * 5;
    }
    cv.idle = any ? 0u : cv.idle + nh;
}

/* ---- the Cloud: the reverb's fourth TYPE, a granular cloud after the owner's browser sim (its FM-1 LIMITS mode:
 * the lengths, the grain count and the grain buffer below are that mode's), at 22.05 kHz between the same half-band
 * filters as the plate. The send: a swell (BLOOM: the wet comes in slowly after each new attack) and a 150 Hz low cut,
 * then a pre-delay tied to SPACE (10 .. 70 ms) and four diffusing all-passes (0.5 .. 0.7 with SPACE). The tank: four
 * lines (41 .. 67 ms, times 1 .. 3 with SPACE), each read between two samples on its own slow sine (0.11 .. 0.29 Hz,
 * 0.3 .. 2.5 ms with BLOOM), a one-pole's damping, a 4 x 4 Hadamard mix, the line's decay (RT60 1.5 .. 40 s with
 * SPACE), an all-pass of its own and a soft knee at 0.8 of the line's range. The grain cloud: the tank's out goes into
 * half a second of buffer; up to six grains read it (80 .. 380 ms, a sin^2 window, each panned), some an octave up or
 * down, some reversed, as CLOUD rises (35 a second at 100), and their sum goes back into the tank (the cloud keeps
 * growing in the tail) and out. Then the tone tilt around 1.2 kHz (TONE) and the ducking (DUCK: the wet gives way
 * while the bus plays, and blooms when it stops). FREEZE: nothing new in, the decay at 0.9998 a pass and no damping
 * (the tank holds what it has), the grains' way back at 0.3.
 * MEMORY: the Cloud keeps its lines in the delay's buffer, the upper half of each of its two lines (cr_fx.c xd_m: the
 * delay runs on the lower halves meanwhile, up to 370 ms); cf_fx_chain's wipe clears both and moves the line between
 * the two layouts when the TYPE goes to or from Cloud. The plate's cv_buf is not touched. Lines int16; states int32;
 * gains Q15; every product in the loops truncated towards 0 (xf_tz15: no DC creeps round a 40 s tank) */
#define CL_MAXG 6u                                /* grains at once (the sim's FM-1 LIMITS) */
#define CL_GL 11025u                              /* the grain buffer: 0.5 s */
#define CL_PRE_N 1546u                            /* the pre-delay's ring: 70 ms and the read's two points */
static const uint16_t CL_LN[4] = {2791, 3347, 3956, 4498};   /* each line's ring: SIZE 3, the swing, the reads */
static const uint16_t CL_APN[4] = {105, 79, 281, 205};       /* the input's all-passes (Dattorro's, at 22.05 kHz) */
static const uint16_t CL_ILN[4] = {117, 147, 174, 200};      /* each line's own all-pass: 5.3 .. 9.1 ms */
static const int32_t CL_BASE[4] = {233130, 280547, 332479, 378766};   /* the lines at SPACE 0, Q8 samples */
static const uint32_t CL_LFO[4] = {342818, 529810, 716802, 903794};   /* 0.11 .. 0.29 Hz: phase a half-rate block */
_Static_assert(2791u + 3347u + 3956u + 4498u + CL_PRE_N <= XD_N / 2u, "the Cloud's tank and pre-delay fit half a line");
_Static_assert(CL_GL + 105u + 79u + 281u + 205u + 117u + 147u + 174u + 200u <= XD_N / 2u,
               "the Cloud's grain buffer and all-passes fit half a line");
static const uint16_t CL_RT[101] = {   /* the tank's RT60 for SPACE 0..100, ms: 1.5 + (s / 100)^1.5 x 38 s (the sim's) */
    1500, 1538, 1607, 1697, 1804, 1925, 2058, 2204, 2360, 2526, 2702, 2886,
    3080, 3281, 3491, 3708, 3932, 4164, 4402, 4647, 4899, 5157, 5421, 5692,
    5968, 6250, 6538, 6831, 7130, 7434, 7744, 8059, 8379, 8704, 9034, 9368,
    9707, 10051, 10399, 10753, 11111, 11474, 11841, 12213, 12589, 12970, 13355, 13744,
    14138, 14535, 14935, 15343, 15752, 16166, 16583, 17004, 17429, 17857, 18289, 18725,
    19164, 19607, 20054, 20504, 20958, 21415, 21875, 22339, 22806, 23277, 23750, 24227,
    24707, 25191, 25678, 26182, 26661, 27157, 27656, 28158, 28663, 29171, 29682, 30196,
    30713, 31233, 31757, 32283, 32812, 33344, 33879, 34417, 34958, 35502, 36048, 36598,
    37150, 37705, 38263, 38823, 39500,
};
typedef struct {
    int32_t pos, inc;                             /* in the grain buffer, Q16; its step (+-0.5, 1, 2; < 0 reversed) */
    uint32_t wph, winc, left;                     /* the window's phase and step, samples to go */
    int32_t pl, pr;                               /* the pan, Q15 */
} cl_grain_t;
static struct {
    int16_t *ln[4], *pre, *gb, *ap[4], *il[4];    /* the rings, in the delay's upper halves (cl_init) */
    uint16_t lw[4], pw, gw, apw[4], ilw[4];       /* their write indices */
    int32_t cur[4], tgt[4];                       /* the lines' lengths now (gliding, Q8) and set by SPACE */
    uint32_t lph[4];                              /* their sines' phases */
    int32_t depth, gl[4], damp, apg, pre_t, pre_c;   /* the swing (Q8 samples), the decays, the damping, the
                                                   * diffusers' gain (Q15), the pre-delay set and now (samples) */
    int32_t dmp[4], hp, tl[2];                    /* the damping's states, the low cut's, the tilt's */
    int32_t fz, fast, slow, sw, slow_k, duck_e, dk;   /* FREEZE's gain in; the swell's two envelopes, its gain and
                                                   * attack; the ducking's envelope and gain (Q15) */
    uint32_t spawn, quiet;                        /* a grain's chance a block (of 2^32); blocks of a silent tail */
    int32_t seed, fbg, gmix, tq, cloud, bloom, duck, frz;   /* .. the grains' way back, their level out, TONE (Q15) */
    int32_t fb[XF_NH];                            /* the grains' last block: into the tank's next */
    cl_grain_t g[CL_MAXG];
    uint8_t ng, ready;
} cl;
static uint8_t cl_live;                           /* the Cloud runs (its lines are in place: cfx_wipe), else the plate */

static void cl_init(void)                         /* the rings laid out in the delay's upper halves, the state cleared */
{
    int16_t *a = &xd_buf[0][XD_N / 2u], *b = &xd_buf[1][XD_N / 2u];
    uint32_t k;
    memset(&cl, 0, sizeof cl);
    for (k = 0; k < 4u; k++) {
        cl.ln[k] = a;
        a += CL_LN[k];
    }
    cl.pre = a;
    cl.gb = b;
    b += CL_GL;
    for (k = 0; k < 4u; k++) {
        cl.ap[k] = b;
        b += CL_APN[k];
    }
    for (k = 0; k < 4u; k++) {
        cl.il[k] = b;
        b += CL_ILN[k];
    }
    cl.fz = cl.sw = cl.dk = 32768;
    cl.seed = 0x2545F491;
    cl.ready = 1;
}

/* SPACE (DECAY's place), the four macros and FREEZE -> the gains; on: the reverb takes the send (off: the tail rings
 * out in a couple of seconds, unfrozen) */
static void cl_coefs(uint32_t space, int on)
{
    uint32_t k, s = space > 100u ? 100u : space, c = cfx_cl[0] > 100u ? 100u : cfx_cl[0];
    uint32_t b = cfx_cl[1] > 100u ? 100u : cfx_cl[1], rt = CL_RT[s], y;
    int frz = on && cfx_frz;
    if (!cl.ready)
        cl_init();
    if (!on && rt > 2000u)
        rt = 2000u;
    for (k = 0; k < 4u; k++) {
        int32_t t = CL_BASE[k] * (int32_t)(100u + 2u * s) / 100;
        uint32_t len = (uint32_t)t >> 8;          /* samples */
        cl.tgt[k] = t;
        if (!cl.cur[k])
            cl.cur[k] = t;                        /* (the first time: where it is set) */
        /* g = 10^(-3 L / RT60) = e^-(6.9078 L / RT60): xf_expn takes the exponent x 65536 */
        y = 452709u * len / (rt * 441u / 20u);
        cl.gl[k] = frz ? 32761 : (int32_t)xf_expn(y);
        if (cl.gl[k] > 32700 && !frz)
            cl.gl[k] = 32700;
    }
    cl.depth = (int32_t)(16934u + 1242u * b) / 10;   /* (0.3 + 2.2 b) ms, Q8 samples */
    cl.damp = frz ? 32768 : 18022;                /* the sim's 0.55 a sample (a corner near 2.8 kHz), open frozen */
    cl.apg = 16384 + (int32_t)(655u * s / 10u);   /* 0.5 + 0.2 s */
    cl.pre_t = (int32_t)((22050u + 1323u * s) / 100u);
    if (!cl.pre_c)
        cl.pre_c = cl.pre_t;
    cl.spawn = c * c * 10909u;                    /* (c / 100)^2 x 35 a second, a block of 16 at 22.05 kHz */
    cl.cloud = (int32_t)c;
    cl.fbg = (int32_t)(c * 18022u / 100u) * (frz ? 3 : 10) / 10;   /* cloud x 0.55 (x 0.3 frozen) */
    cl.gmix = (int32_t)(9830u + 229u * c);        /* 0.3 + 0.7 cloud */
    cl.tq = (int32_t)((cfx_cl[2] > 100u ? 100u : cfx_cl[2]) * 32768u / 100u);
    cl.duck = cfx_cl[3] > 100u ? 100 : cfx_cl[3];
    cl.bloom = (int32_t)b;
    /* the swell: the slow envelope's attack, (0.03 + 2.5 b) s; k = 1 - e^-(a block / it) */
    cl.slow_k = 32768 - (int32_t)xf_expn((uint32_t)(47555u * 100u / (3000u + 250u * b)));
    cl.frz = frz;
}

XF_AI int32_t cl_knee(int32_t v)                  /* itself to 0.8 of the line's range, then a parabola to its limit */
{
    int32_t a, e;
    if ((uint32_t)(v + 26214) <= 52428u)
        return v;
    a = v < 0 ? -v : v;
    e = a - 26214;
    a = e >= 13106 ? 32767 : 26214 + e - (int32_t)(((uint32_t)e * (uint32_t)e) / 26214u);
    return v < 0 ? -a : a;
}
XF_AI int32_t cl_rd(const int16_t *b, uint32_t n, uint32_t w, int32_t d)   /* a ring of n read d (Q8) behind w */
{
    int32_t i = (int32_t)w - (d >> 8), j, a;
    if (i < 0)
        i += (int32_t)n;
    j = i - 1;
    if (j < 0)
        j += (int32_t)n;
    a = b[i];
    return a + xf_tz8((b[j] - a) * (d & 255));
}
XF_AI int32_t cl_ap(int16_t *b, uint16_t *w, uint32_t n, int32_t x, int32_t g)   /* an all-pass of the ring's length */
{
    int32_t z = b[*w], v = cl_knee(x - xf_tz15(g * z));
    b[*w] = (int16_t)v;
    if (++*w >= n)
        *w = 0;
    return z + xf_tz15(g * v);
}
XF_AI uint32_t cl_u16(void) { return noise32(&cl.seed) >> 16; }   /* 0 .. 65535 */

/* a new grain: its length, pitch, direction, place and pan, after the sim (with its reads kept clear of the write:
 * a reversed grain and a slow one move away from it, and are kept short enough not to reach round to it) */
static void cl_spawn(void)
{
    cl_grain_t *g = &cl.g[cl.ng];
    uint32_t c = (uint32_t)cl.cloud, p16 = 65536u, len, s0, grow, spread, room = CL_GL - 882u, s, u;
    int rev;
    if (cl_u16() < 13107u + 327u * c)             /* an octave: 0.2 + 0.5 cloud of them, 0.6 of those up */
        p16 = cl_u16() < 39322u ? 131072u : 32768u;
    rev = cl_u16() < 196u * c;                    /* reversed: 0.3 cloud */
    len = 1764u + ((cl_u16() * 6615u) >> 16);     /* 80 .. 380 ms */
    if (rev) {
        uint32_t lmax = (room - 441u) * 65536u / (65536u + p16);
        if (len > lmax)
            len = lmax;
        s0 = 441u;
        grow = (uint32_t)(((uint64_t)len * (65536u + p16)) >> 16);
    } else {
        if (((uint64_t)len * p16 >> 16) > room * 6u / 10u)
            len = (uint32_t)((uint64_t)(room * 6u / 10u) * 65536u / p16);
        s0 = (uint32_t)(((uint64_t)len * p16) >> 16) + 441u;
        grow = p16 < 65536u ? (uint32_t)(((uint64_t)len * (65536u - p16)) >> 16) : 0u;
    }
    spread = room > s0 + grow ? room - s0 - grow : 0u;
    if (spread > CL_GL)
        spread = CL_GL;
    s = s0 + ((cl_u16() * spread) >> 16);
    s = (uint32_t)cl.gw + CL_GL - s;              /* (s < room < CL_GL) */
    if (s >= CL_GL)
        s -= CL_GL;
    g->pos = (int32_t)(s << 16);
    g->inc = rev ? -(int32_t)p16 : (int32_t)p16;
    g->left = len;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    u = noise32(&cl.seed) >> 2;                   /* the pan: an angle 0 .. pi / 2, equal power */
    g->pl = osc_sine(u + 0x40000000u);
    g->pr = osc_sine(u);
    cl.ng++;
}

/* a half-rate block of the mono send -> the stereo wet (half rate, the bus's scale x 1/2: as xf_down's) */
static void cl_run(const int32_t *in, int32_t *ol, int32_t *orr, uint32_t nh)
{
    int32_t tl[XF_NH], tr[XF_NH], gl[XF_NH], gr[XF_NH], d0[4];
    int32_t pk = 0, opk = 0, fz, sw0, sw1, dk0, dk1, tgt;
    uint32_t j, k, q;
    /* the block's envelopes: the swell's (slow / fast) and the ducking's, from the send's peak */
    for (j = 0; j < nh; j++) {
        int32_t a = in[j] < 0 ? -in[j] : in[j];
        pk = a > pk ? a : pk;
    }
    cl.fast += xf_tz15((pk - cl.fast) * (pk > cl.fast ? 10158 : 296));   /* 2 ms up, 80 ms down */
    cl.slow += xf_tz15((pk - cl.slow) * (pk > cl.slow ? cl.slow_k : 296));
    sw0 = cl.sw;
    sw1 = cl.fast < 64 ? 32768 : cl.slow >= cl.fast ? 32768 : (int32_t)(((int64_t)cl.slow << 15) / cl.fast);
    cl.sw = sw1;
    cl.duck_e += xf_tz15((pk - cl.duck_e) * (pk > cl.duck_e ? 2295 : 79));   /* 10 ms up, 300 ms down */
    {
        int32_t melo = cl.duck_e >= 2500 ? 32768 : cl.duck_e * 32768 / 2500;  /* (2500: a chord at the
                                                   * default level, -19 dBFS out, ducks the wet by most of DUCK) */
        tgt = 32768 - melo * cl.duck / 100;
    }
    dk0 = cl.dk;
    cl.dk += xf_tz15((tgt - cl.dk) * 819) + (tgt > cl.dk ? 1 : tgt < cl.dk ? -1 : 0);
    dk1 = cl.dk;
    fz = cl.fz;                                   /* FREEZE: the send fades out (and back) over ~40 ms */
    cl.fz += cl.frz ? -(cl.fz >> 5) - 1 : ((32768 - cl.fz) >> 5) + 1;
    cl.fz = xf_clamp(cl.fz, 0, 32768);
    for (k = 0; k < 4u; k++) {                    /* each line's read this block (its sine moves ~0.07 samples a
                                                   * block at most: one read point a block) */
        int32_t dd = cl.tgt[k] - cl.cur[k], s = xf_tz15(dd * 21);   /* (the size glides: ~1 s) */
        cl.cur[k] += s ? s : dd > 0 ? 1 : dd < 0 ? -1 : 0;
        cl.lph[k] += CL_LFO[k];
        d0[k] = cl.cur[k] + xf_tz15(osc_sine(cl.lph[k]) * cl.depth);
    }
    if (cl.pre_c != cl.pre_t)
        cl.pre_c += cl.pre_c < cl.pre_t ? 1 : -1;
    for (j = 0; j < nh; j++) {
        int32_t x = in[j] >> 1, o[4], h[4], inp, s;
        x = xf_tz15(x * (sw0 + (((sw1 - sw0) * (int32_t)j) >> 4)));   /* the swell */
        cl.hp += xf_tz15((x - cl.hp) * 1371);    /* the low cut: 150 Hz */
        x = xf_tz15((x - cl.hp) * fz);
        cl.pre[cl.pw] = (int16_t)xf_clamp(x, -32767, 32767);
        {
            int32_t i = (int32_t)cl.pw - cl.pre_c;
            if (i < 0)
                i += (int32_t)CL_PRE_N;
            x = cl.pre[i];
        }
        if (++cl.pw >= CL_PRE_N)
            cl.pw = 0;
        for (k = 0; k < 4u; k++)
            x = cl_ap(cl.ap[k], &cl.apw[k], CL_APN[k], x, cl.apg);
        inp = (x + cl.fb[j]) >> 1;
        for (k = 0; k < 4u; k++) {
            int32_t y = cl_rd(cl.ln[k], CL_LN[k], cl.lw[k], d0[k]);
            cl.dmp[k] += xf_tz15((y - cl.dmp[k]) * cl.damp);
            o[k] = cl.dmp[k];
        }
        h[0] = (o[0] + o[1] + o[2] + o[3]) >> 1;  /* the Hadamard mix (orthonormal: / 2) */
        h[1] = (o[0] - o[1] + o[2] - o[3]) >> 1;
        h[2] = (o[0] + o[1] - o[2] - o[3]) >> 1;
        h[3] = (o[0] - o[1] - o[2] + o[3]) >> 1;
        for (k = 0; k < 4u; k++) {
            int32_t w = ((k & 1u) ? -inp : inp) + xf_tz15(cl.gl[k] * h[k]);
            w = cl_ap(cl.il[k], &cl.ilw[k], CL_ILN[k], cl_knee(w), 16384);
            cl.ln[k][cl.lw[k]] = (int16_t)cl_knee(w);
            if (++cl.lw[k] >= CL_LN[k])
                cl.lw[k] = 0;
        }
        tl[j] = xf_tz15((o[0] - xf_tz15(o[1] * 13107) + xf_tz15(o[2] * 19661)) * 22938);   /* the sim's taps:
                                                   * (o0 - 0.4 o1 + 0.6 o2) x 0.7, and the mirror */
        tr[j] = xf_tz15((o[1] - xf_tz15(o[0] * 13107) + xf_tz15(o[3] * 19661)) * 22938);
        s = (tl[j] + tr[j]) >> 1;
        cl.gb[cl.gw] = (int16_t)xf_clamp(s, -32767, 32767);
        if (++cl.gw >= CL_GL)
            cl.gw = 0;
        gl[j] = gr[j] = 0;
    }
    /* the grains: a new one now and then, then each over the block (its state in registers) */
    if (cl.cloud > 1 && cl.ng < CL_MAXG && noise32(&cl.seed) < cl.spawn)
        cl_spawn();
    for (q = 0; q < cl.ng;) {
        cl_grain_t *g = &cl.g[q];
        int32_t pos = g->pos, inc = g->inc, pl = g->pl, pr = g->pr;
        uint32_t wph = g->wph, winc = g->winc, left = g->left, m = left < nh ? left : nh;
        const int32_t top = (int32_t)(CL_GL << 16);
        for (j = 0; j < m; j++) {
            int32_t i = pos >> 16, i2 = i + 1 >= (int32_t)CL_GL ? 0 : i + 1, a = cl.gb[i], y, win;
            y = a + xf_tz8((cl.gb[i2] - a) * ((pos >> 8) & 255));
            {                                    /* sin^2 (pi x) as (4 x (1 - x))^2: three products, no sine */
                int32_t u = (int32_t)(wph >> 17), p4 = (u * (32768 - u)) >> 13;
                win = (p4 * p4) >> 15;
            }
            y = xf_tz15(xf_tz15(y * win) * 18022);              /* x 0.55 */
            gl[j] += xf_tz15(y * pl);
            gr[j] += xf_tz15(y * pr);
            pos += inc;
            if (pos < 0)
                pos += top;
            else if (pos >= top)
                pos -= top;
            wph += winc;
        }
        g->pos = pos;
        g->wph = wph;
        g->left = left - m;
        if (!g->left)
            cl.g[q] = cl.g[--cl.ng];              /* (done: the last one in its place) */
        else
            q++;
    }
    /* out: the tank's taps and the grains, the tilt, x 1.6, the ducking; the grains' way back for the next block */
    for (j = 0; j < nh; j++) {
        int32_t dk = dk0 + (((dk1 - dk0) * (int32_t)j) >> 4), c, w, lo, gs[2];
        gs[0] = xf_clamp(gl[j], -65535, 65535);  /* (six grains at their loudest: the products below stay 32-bit) */
        gs[1] = xf_clamp(gr[j], -65535, 65535);
        cl.fb[j] = xf_tz15(((gs[0] + gs[1]) >> 1) * cl.fbg);
        for (c = 0; c < 2; c++) {
            w = (c ? tr[j] : tl[j]) + xf_tz15(gs[c] * cl.gmix);
            cl.tl[c] += CFX_MUL(w - cl.tl[c], 9490, 15);   /* the tilt's low side: 1.2 kHz */
            lo = cl.tl[c];
            w = CFX_MUL(lo, 32768 - cl.tq, 15) + CFX_MUL(w - lo, cl.tq, 15);
            w = CFX_MUL(w, dk, 15);
            (c ? orr : ol)[j] = w * 9;          /* x 1.6, x 2 back from the half the tank took, and x 2.8: near the Hall's
                                                   * level at the same MIX (an emulator measurement) */
            opk |= w < 0 ? -w : w;
        }
    }
    cl.quiet = pk || opk > 3 || cl.ng || cl.frz ? 0u : cl.quiet + 1u;
}

static void cfx_reverb(int32_t *l, int32_t *r, uint32_t n, int feed)
{
    int32_t *hr = xf.t[1], *ol = xf.t[2], *orr = xf.t[3], any;
    uint32_t i, nh = n >> 1;
    int fed = cfx_send(l, r, n, feed), cloud = cl_live;   /* (the layout's: during the fade, the one fading out) */
    if (!cv.ready)
        cv_coefs(1, 45);
    if (cloud) {
        if (!fed && cl.quiet > 1378u && !xf.up_live)
            return;                               /* a second of a silent tail (FREEZE never is): nothing runs */
    } else if (!fed && cv.idle > 15u * 22050u && !xf.up_live)
        return;                                   /* 15 s of nothing in: the tail is gone, nothing runs */
    XF_CALL(xf_down)(&xf.dn[1], cfx.m, hr, n);
    /* (switched off: the tail rings out, but in a second or two even at DECAY 100, where it would never end) */
    if (cloud)
        cl_run(hr, ol, orr, nh);
    else
        cv_run(hr, ol, orr, nh, feed || cv.dec < 16000 ? cv.dec : 16000);
    for (i = 0, any = 0; i < nh; i++)
        any |= ol[i] | orr[i];
    if (any)
        xf.up_live = 256u;
    else if (xf.up_live)
        xf.up_live = xf.up_live > nh ? xf.up_live - nh : 0u;
    XF_CALL(xf_up)(&xf.up[0], ol, cfx.wl, nh);
    XF_CALL(xf_up)(&xf.up[1], orr, cfx.wr, nh);
    cfx_mix(l, r, CFX_REVERB, n, 256, 0);
}

static void cfx_chorus(int32_t *l, int32_t *r, uint32_t n)
{
    cfx_send(l, r, n, 1);
    xc_block(cfx.m, cfx.wl, cfx.wr, n);
    cfx_mix(l, r, CFX_CHORUS, n, 256, 1);
}

static void cfx_phaser(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t c, i, s;
    cfx.pph += cfx.pinc;
    for (c = 0; c < 2u; c++) {
        const int32_t *b = c ? r : l;
        int32_t q = 32 * 256 + CFX_MUL(osc_sine(cfx.pph + (c ? 0x40000000u : 0u)), cfx.pdep, 15), k, a;
        int32_t *w = c ? cfx.wr : cfx.wl, fb = cfx.pfb[c];
        q = q < 0 ? 0 : q > 63 * 256 - 1 ? 63 * 256 - 1 : q;
        k = q >> 8;
        a = CFX_PA[k] + (((CFX_PA[k + 1] - CFX_PA[k]) * (q & 255)) >> 8);
        for (i = 0; i < n; i++) {
            int32_t u = b[i] + CFX_MUL(fb, 19660, 15);    /* (feedback 0.6) */
            for (s = 0; s < 6u; s++) {
                int32_t y = cfx.px[c][s] + CFX_MUL(a, u - cfx.py[c][s], 15);
                cfx.px[c][s] = u;
                cfx.py[c][s] = y;
                u = y;
            }
            fb = u;
            w[i] = u;
        }
        cfx.pfb[c] = fb;
    }
    cfx_mix(l, r, CFX_PHASER, n, 256, 1);
}

/* fx.c's FX_MIX_HOOK: the chain, in its order, on the mixed parts */
/* ------------------------------------------------------ the power-on sound --- */
/* the intro's sound (cf_splash.c starts it; GLO > Display > BOOT SND), always the same: a voice of its own, not a part,
 * after the effects (so neither the sounds nor the effects set then change it) and before MASTER. A strum of glass
 * bells: a wide Cmaj9#11 (Lydian: the bright, floating fourth) upward over a soft sub C, faster and louder as it
 * rises, echoed once a third of a second later, softer; a second on, a shower down from above. Each bell: two sines
 * 5 cents apart (they beat, slowly), a shimmer of phase modulation at 3.5 times its pitch gone in a tenth of a second
 * (glass), coming in 40 cents flat and sliding up (the bloom), 6 ms in, a long decay (0.74 s). */
static const struct { uint16_t ms; uint8_t note, vel; int8_t pan; } CF_BOOT[] = {
    {0, 36, 90, 0},                                              /* (the sub: a plain sine) */
    {0, 48, 50, -6}, {75, 55, 56, 6}, {140, 62, 62, -10}, {198, 64, 68, 10}, {250, 71, 74, -14}, {296, 78, 80, 14},
    {338, 81, 86, -18}, {376, 86, 92, 18}, {411, 91, 98, 0},
    {420, 62, 26, 16}, {480, 71, 30, -16}, {540, 78, 34, 18}, {590, 86, 38, -18},          /* (the echo) */
    {1050, 98, 40, 12}, {1105, 93, 44, -12}, {1155, 90, 48, 9}, {1200, 83, 52, -9}, {1242, 76, 56, 4},
};
#define CF_BOOT_N ((uint32_t)(sizeof CF_BOOT / sizeof CF_BOOT[0]))
static const uint32_t CF_BOOT_INC[12] = {796254u, 843601u, 893765u, 946911u, 1003217u, 1062871u, 1126073u, 1193033u,
                                         1263974u, 1339134u, 1418763u, 1503127u};   /* MIDI note 0..11, per sample */
static volatile uint8_t cf_boot_go;              /* the UI's: start it */
static struct {
    uint8_t on;
    uint8_t yield;                               /* the audio is overloaded: it fades out at once (audio.c shed) */
    int32_t gain;                                /* .. its level on the way out, Q15 */
    uint32_t t;                                  /* samples since the start */
    uint32_t pa[CF_BOOT_N], pb[CF_BOOT_N], pm[CF_BOOT_N];
    int32_t e[CF_BOOT_N], ei[CF_BOOT_N], bloom[CF_BOOT_N];   /* level, shimmer (Q30); the slide still to go (Q16) */
} cfb;
static void cf_boot_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, k;
    uint32_t live = 0;
    if (cf_boot_go) {
        cf_boot_go = 0;
        memset(&cfb, 0, sizeof cfb);
        cfb.on = 1;
        cfb.gain = 32768;
    }
    if (!cfb.on)
        return;
    if (cfb.yield) {                             /* (the chords come first: out over 4 blocks, 2.9 ms) */
        cfb.gain -= 8192;
        if (cfb.gain <= 0) {
            cfb.on = 0;
            return;
        }
    }
    for (k = 0; k < CF_BOOT_N; k++) {
        uint32_t t0 = CF_BOOT[k].ms * (FS / 1000u), inc, inc_a, inc_b, inc_m, a;
        int32_t amp = (CF_BOOT[k].vel * 36 * cfb.gain) >> 15, pan = CF_BOOT[k].pan, sub = k == 0u;
        if (cfb.t + n <= t0) {
            live = 1;                            /* (still to come) */
            continue;
        }
        if (cfb.e[k] == 0 && cfb.t <= t0 + n) {      /* (its start: in this block) */
            cfb.e[k] = 1 << 30;
            cfb.ei[k] = 1 << 30;
            cfb.bloom[k] = sub ? 0 : 1497;
        }
        if (cfb.e[k] < (1 << 20))
            continue;                                /* (gone: -60 dB under its strike, below -79 dBFS; it was computed on to
                                                      * -84 dB for the whole 6 s, as dear as a 6-operator FM chord on the
                                                      * one core: the first chords after power-on lost their notes to the
                                                      * overload guard, the owner, 2026-10-21) */
        live = 1;
        inc = CF_BOOT_INC[CF_BOOT[k].note % 12u] << (CF_BOOT[k].note / 12u);
        inc = (uint32_t)(((uint64_t)inc * (uint32_t)(65536 - cfb.bloom[k])) >> 16);
        inc_a = inc + (uint32_t)(((uint64_t)inc * 190u) >> 17);
        inc_b = inc - (uint32_t)(((uint64_t)inc * 190u) >> 17);
        inc_m = inc * 7u / 2u;
        cfb.bloom[k] -= cfb.bloom[k] >> 5;           /* (the slide up: ~50 ms) */
        for (i = 0; i < n; i++) {
            uint32_t tt = cfb.t + i;
            int32_t y, off;
            if (tt < t0)
                continue;
            a = tt - t0 < FS * 6u / 1000u ? (tt - t0) * 32768u / (FS * 6u / 1000u) : 32768u;
            if (sub) {
                y = osc_sine(cfb.pa[k]);
            } else {
                off = osc_sine(cfb.pm[k]) * (cfb.ei[k] >> 17);
                y = (osc_sine(cfb.pa[k] + (uint32_t)off) + osc_sine(cfb.pb[k] + (uint32_t)off)) >> 1;
                cfb.pm[k] += inc_m;
            }
            y = (y * (cfb.e[k] >> 15)) >> 15;        /* (32 bits: |y| <= 2^15, e >> 15 <= 2^15) */
            y = ((y * (int32_t)a) >> 15) * amp / 32768;
            l[i] += y * (32 - pan) / 32;
            r[i] += y * (32 + pan) / 32;
            cfb.pa[k] += inc_a;
            cfb.pb[k] += inc_b;
            cfb.e[k] -= cfb.e[k] >> 15;
            cfb.ei[k] -= cfb.ei[k] >> 12;
        }
    }
    cfb.t += n;
    if (cfb.t > FS * 6u || !live)
        cfb.on = 0;
}
/* audio.c's SHED_FIRST: the audio overloaded; the power-on sound, while it rings, goes before any voice. 1: it was it */
static int cf_boot_yield(void)
{
    if (!cfb.on)
        return voice_tail_yield();               /* (then the tail ringing out: CF_NTAIL, voice.c) */
    cfb.yield = 1;
    return 1;
}
#define SHED_FIRST() cf_boot_yield()
/* voice.c VOICE_NEW_NOTE: a note starts while the power-on sound rings: it gives way at once. It takes about half the
 * core at its start, and a chord beside it took the half past the guard's line: the guard let it go only two halves
 * later and, a half after that, still cut a note of the chord (rc3, the emulator's CF_LOADMODEL) */
static void cf_boot_note(void)
{
    if (cfb.on)
        cfb.yield = 1;
}

/* the delay's buffer between its two layouts: the whole of each line the delay's, or the lower halves the delay's and
 * the upper ones the Cloud's (cl_init). When the reverb's TYPE goes to or from Cloud: first the delay's and the
 * reverb's wet fade out (48 blocks, 35 ms: their wet gains eased to 0, -56 dB), then both lines are cleared, 4 KiB a
 * block (23 ms; the two send nothing meanwhile), then the ring is set, the delay starts again from rest at its time
 * (clamped to the ring), the Cloud from silence, and their wet gains ease back in */
#define CLW_FADE 48u
static struct {
    uint8_t mode;                    /* the layout now: 1 the Cloud's lines are there */
    uint8_t busy, snap;              /* 1 fading, 2 wiping; the delay's time to be taken as it is set (no glide) */
    uint32_t at;                     /* blocks faded, then samples cleared */
} clw;
static void cfx_wipe(int want)
{
    int16_t *b = &xd_buf[0][0];
    uint32_t k;
    if (!clw.busy) {
        if (want == clw.mode)
            return;
        clw.busy = 1;
        clw.at = 0;
    }
    cfx.wt[CFX_DELAY] = cfx.wt[CFX_REVERB] = 0;   /* (held at 0 until the end: cfx_coefs sets them again then) */
    if (clw.busy == 1) {
        if (++clw.at < CLW_FADE && (cfx.wg[CFX_DELAY] | cfx.wg[CFX_REVERB]))
            return;
        cfx.wg[CFX_DELAY] = cfx.wg[CFX_REVERB] = 0;
        clw.busy = 2;
        clw.at = 0;
    }
    memset(b + clw.at, 0, 2048u * sizeof *b);
    clw.at += 2048u;
    if (clw.at < 2u * XD_N)
        return;
    xd_m = want ? XD_N / 2u - 1u : XD_M;
    xf.d_w = 0;
    xf.d_live = 0;
    xf.d_q = 0;
    for (k = 0; k < 2u; k++) {
        xf.d_lp[k] = xf.d_hp[k] = 0;
        xf.d_e[k][0] = xf.d_e[k][1] = xf.d_e[k][2] = 0;
    }
    xf.hash = 0;                     /* (the delay's times again, against the new ring) */
    clw.snap = 1;
    cl_init();
    cfx.hash = 0;                    /* (the Cloud's gains and the wet gains again) */
    clw.mode = (uint8_t)want;
    cl_live = (uint8_t)want;
    clw.busy = 0;
}

static void cf_fx_chain(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, h;
    if (!cfx_ready)
        return;
    h = cfx_fnv(&cfx_set, sizeof cfx_set) ^ (cfx_fnv(cfx_cl, sizeof cfx_cl) * 31u) ^ (cfx_frz ? 0x9E3779B9u : 0u);
    if (h != cfx.hash || !cfx.fa1) {
        cfx.hash = h;
        cfx_coefs();
        if (!cfx.fg)
            cfx.fg = cfx.fg_t;
        cfx_filter_coefs();
    }
    cfx_wipe(cfx_set.v[CFX_REVERB][0] == CL_TYPE);   /* (after the coefficients: it holds the two wet gains at 0) */
    cfx_xf_prepare();
    if (clw.snap) {                  /* (after a wipe: the delay at its time at once, nothing to glide from) */
        xf.d_cur[0] = xf.d_tgt[0];
        xf.d_cur[1] = xf.d_tgt[1];
        clw.snap = 0;
    }
    for (i = 0; i < CFX_N; i++) {
        uint32_t e = cfx_set.order[i];
        int on = e < CFX_N && ((cfx_set.on >> e) & 1u);
        switch (e) {
        case CFX_FILTER: if (on) cfx_filter(l, r, n); break;
        case CFX_DIST: if (on) cfx_dist(l, r, n); break;
        case CFX_CHORUS: if (on) cfx_chorus(l, r, n); break;
        case CFX_PHASER: if (on) cfx_phaser(l, r, n); break;
        case CFX_DELAY:                  /* (off: no new sound in, the tail rings out) */
        case CFX_REVERB:
            if (clw.busy == 2) {         /* (the wipe: their wet silent; faded out before it) */
                cfx_send(l, r, n, 0);
                cfx_mix(l, r, e, n, 256, 0);
            } else if (e == CFX_DELAY) {
                cfx_delay(l, r, n, on);
            } else {
                cfx_reverb(l, r, n, on);
            }
            break;
        default: break;
        }
    }
#if CF_VLOAD
    {
        uint32_t vl0 = VL_NOW();
        cf_boot_mix(l, r, n);                    /* (the power-on sound: after the effects, before MASTER) */
        vl_aside_ticks += VL_NOW() - vl0;        /* (voice.c: it gives way, the parts' voices do not wait for it) */
    }
#else
    cf_boot_mix(l, r, n);                        /* (the power-on sound: after the effects, before MASTER) */
#endif
    {
        int32_t pk = 0;
        for (i = 0; i < n; i++) {
            int32_t a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
            pk |= a | b;
        }
        cfx_quiet = pk > 63 ? 0u : cfx_quiet + 1u;
    }
}
