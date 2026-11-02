/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors */
/* CLOUDRIFT: the master effects (fx.c's FX_MIX_HOOK, on the mixed parts, before MASTER and the limiter), as the sim
 * has them:
 *   the ensemble  a slow stereo chorus (cr_fx.c's xc_block, ~0.7 Hz) on the dry mix, deeper and wider with BLOOM
 *                 (the sim's: 15 .. 45 % of it, 1 .. 4 ms of swing): the pads breathe
 *   the Cloud     cf_fx.c's granular reverb (cl_run), fed by the tracks' SENDs (fx.c's reverb bus, send_r: each
 *                 part's P_REV), its wet added over the full dry sound (no MIX: a send effect, as in the sim)
 * Included after cf_fx.c. */

static volatile uint8_t cdx_space = 75, cdx_bloom = 60;   /* (the UI's: cd_fx_set) */
#define CDX_WET 24576                            /* the Cloud's wet over the dry, Q15 (0.75: the sim's 0.9 at its
                                                  * louder reverb, measured against the sim in the emulator) */
static uint32_t cdx_hash;

static void cd_fx_set(uint32_t space, uint32_t cloud, uint32_t bloom, uint32_t tilt, uint32_t duck)
{
    uint8_t p[4];
    p[0] = (uint8_t)cloud;
    p[1] = (uint8_t)bloom;
    p[2] = (uint8_t)tilt;
    p[3] = (uint8_t)duck;
    cf_cloud_apply(p);
    cdx_space = (uint8_t)(space > 100u ? 100u : space);
    cdx_bloom = (uint8_t)(bloom > 100u ? 100u : bloom);
}

static void cd_fx_chain(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, nh = n >> 1, h;
    int32_t any = 0, mix, wg;
    if (!cl.ready)
        cl_init();
    h = cdx_space * 2654435761u ^ (cfx_fnv(cfx_cl, sizeof cfx_cl) * 31u) ^ (cfx_frz ? 0x9E3779B9u : 0u);
    if (h != cdx_hash) {
        cdx_hash = h;
        cl_coefs(cdx_space, 1);
        song.g[G_CRATE] = 50;                    /* (~0.7 Hz) */
        song.g[G_CDEPTH] = (int16_t)(24 + cdx_bloom * 70u / 100u);
    }
    /* the ensemble: the dry mix x (1 - m / 2) + the chorus x m, m 15 .. 45 % with BLOOM */
    mix = (int32_t)(4915 + cdx_bloom * 9830 / 100);
    cfx_send(l, r, n, 1);
    xc_block(cfx.m, cfx.wl, cfx.wr, n);
    for (i = 0; i < n; i++) {
        l[i] = CFX_MUL(l[i], 32768 - mix / 2, 15) + CFX_MUL(cfx.wl[i], mix, 15);
        r[i] = CFX_MUL(r[i], 32768 - mix / 2, 15) + CFX_MUL(cfx.wr[i], mix, 15);
    }
    /* the Cloud on the sends */
    for (i = 0; i < n; i++) {
        any |= send_r[i];
        cfx.wl[i] = cfx.wr[i] = 0;
    }
    if (!any && cl.quiet > 1378u && !cfx_frz && !xf.up_live)
        goto out;                                /* (a second of a silent tail: nothing runs) */
    {
        int32_t *hr = xf.t[1], *ol = xf.t[2], *orr = xf.t[3], live = 0;
        XF_CALL(xf_down)(&xf.dn[1], send_r, hr, n);
        cl_run(hr, ol, orr, nh);
        for (i = 0; i < nh; i++)
            live |= ol[i] | orr[i];
        if (live)
            xf.up_live = 256u;
        else if (xf.up_live)
            xf.up_live = xf.up_live > nh ? xf.up_live - nh : 0u;
        XF_CALL(xf_up)(&xf.up[0], ol, cfx.wl, nh);
        XF_CALL(xf_up)(&xf.up[1], orr, cfx.wr, nh);
        wg = CDX_WET;
        for (i = 0; i < n; i++) {
            l[i] += CFX_MUL(cfx.wl[i], wg, 15);
            r[i] += CFX_MUL(cfx.wr[i], wg, 15);
        }
    }
out:
    {
        int32_t pk = 0;
        for (i = 0; i < n; i++) {
            int32_t a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
            pk |= a | b;
        }
        cfx_quiet = pk > 63 ? 0u : cfx_quiet + 1u;   /* (the settings' save waits for a quiet second) */
    }
}
