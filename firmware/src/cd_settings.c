/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors */
/* CLOUDRIFT: the setup in flash: cs (the four tracks, their steps and sounds, the arp, the Cloud, the key and tempo)
 * as one record in the flash object CoralFlower kept its looper in (OBJ_CFLOOP, 0xB6000 / 0xB7000: its own magic,
 * so a CoralFlower installed again finds no loop there and leaves it). Saved by SAVE, and on its own once the setup
 * has rested 3 s, nothing plays and the audio has been quiet a second: a save erases a sector, which holds the audio
 * ~45 ms, so never under sound. Included after cd_ui.h. */

#define CD_SET_OBJ OBJ_CFLOOP
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    uint32_t check;
    cd_set_t s;
} cd_rec_t;
_Static_assert(sizeof(cd_rec_t) <= ST_PAYLOAD_MAX, "settings: one record");

static cd_set_t cs;
static cd_rec_t cd_rec;
static uint32_t cds_saved;
static uint8_t cds_loaded, cd_restore_lock;

static uint32_t cds_fnv(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static void cd_sanitize(cd_set_t *s)
{
    cd_set_t d;
    uint32_t i, k;
    cd_defaults(&d);
    if (s->tempo < 40u || s->tempo > 160u) s->tempo = d.tempo;
    s->key %= 12u;
    if (s->voices < 6u || s->voices > 8u) s->voices = d.voices;
    if (s->space > 100u) s->space = d.space;
    if (s->cloud > 100u) s->cloud = d.cloud;
    if (s->bloom > 100u) s->bloom = d.bloom;
    if (s->tilt > 100u) s->tilt = d.tilt;
    if (s->duck > 100u) s->duck = d.duck;
    if (s->arp_mode >= CD_NARP) s->arp_mode = d.arp_mode;
    s->arp_rate &= 3u;
    if (s->arp_range < 1u || s->arp_range > 4u) s->arp_range = d.arp_range;
    if (s->arp_chaos > 100u) s->arp_chaos = d.arp_chaos;
    s->arp_src = s->arp_src != 0;
    s->track %= CD_NTRK;
    s->midi_out = s->midi_out != 0;
    for (i = 0; i < CD_NTRK; i++) {
        cd_trk_t *t = &s->t[i];
        if (t->mode >= CD_NMODE) t->mode = d.t[i].mode;
        if (t->len < 1u || t->len > CD_NSTEP) t->len = d.t[i].len;
        if (t->div > 2u) t->div = 2;
        if (t->prob > 100u) t->prob = 100;
        if (t->swing > 100u) t->swing = 0;
        if (t->send > 100u) t->send = d.t[i].send;
        if (t->level > 100u) t->level = d.t[i].level;
        if (t->octave < -2 || t->octave > 2) t->octave = 0;
        if (t->tone > 100u) t->tone = 50;
        if (t->env > 100u) t->env = 50;
        for (k = 0; k < CD_NSTEP; k++) {
            t->steps[k].on = t->steps[k].on != 0;
            t->steps[k].note &= 0x7Fu;
            t->steps[k].tone %= 15u;
        }
    }
}
static int cd_settings_load(void)                /* power-on: cs from flash (1), else as it is (the defaults) */
{
    int n;
    cds_loaded = 0;
    if (cr_safe)
        return 0;
    n = st_load(CD_SET_OBJ, &cd_rec, sizeof cd_rec);
    if (n != (int)sizeof cd_rec || cd_rec.magic != CD_SET_MAGIC || cd_rec.version != CD_SET_VERSION ||
        cd_rec.size != sizeof(cd_set_t) || cd_rec.check != cds_fnv(&cd_rec.s, sizeof cd_rec.s))
        return 0;
    cs = cd_rec.s;
    cd_sanitize(&cs);
    cds_saved = cds_fnv(&cs, sizeof cs);
    cds_loaded = 1;
    return 1;
}
static int cd_settings_write(void)               /* 1 written */
{
    if (cr_safe || cd_restore_lock)
        return 0;
    cd_rec.magic = CD_SET_MAGIC;
    cd_rec.version = CD_SET_VERSION;
    cd_rec.size = sizeof(cd_set_t);
    cd_rec.s = cs;
    cd_rec.check = cds_fnv(&cd_rec.s, sizeof cd_rec.s);
    if (st_save(CD_SET_OBJ, &cd_rec, sizeof cd_rec))
        return 0;
    cds_saved = cd_rec.check;
    return 1;
}
static int cd_dirty(void) { return cds_fnv(&cs, sizeof cs) != cds_saved; }

/* main.c's, between frames: SAVE asked, or a change rested 3 s; nothing playing, a quiet second */
static void settings_poll(void)
{
    uint32_t quiet = cfx_quiet >= FS / CTL;
    if (!cu.booted || cr_safe || cd_restore_lock)
        return;
    if (!cu.save_want && (!cd_dirty() || cu.now - cu.changed_ms < 3000u))
        return;
    if (cd_sn.playing || cd_sn.nheld || cu.keys || (!quiet && !cu.save_want))
        return;                                  /* (SAVE: as soon as nothing plays; the Cloud's tail may lose 45 ms.
                                                  * On its own: a quiet second only) */
    if (!cd_dirty()) {
        if (cu.save_want)
            cf_message("Saved");
        cu.save_want = 0;
        return;
    }
    if (cd_settings_write() && cu.save_want)
        cf_message("Saved");
    cu.save_want = 0;
}
