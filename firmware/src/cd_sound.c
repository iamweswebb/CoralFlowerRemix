/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Cloudrift contributors; CoralFlower contributors; ChoralRoot FM-1 contributors */
/* CLOUDRIFT: the tracks' sounds (after CoralFlower's cf_sound.c): one list for every track, the FM and VA engines'
 * pools one after the other (cr_bank.c: each engine's INIT, its factory sounds, the user's), every factory sound in
 * it (CoralFlower kept bells, glass pads and the like off its chord list; here they are the point). A load releases
 * the track's notes (panic_req) and keeps what is the track's own (cr_bank.c param_kept: level, pan, sends); then
 * TONE and ENVELOPE are put on it from the sound's own values (cd_tone_env). Included after cr_bank.c. */

typedef struct { char name[13]; uint8_t engine, kind; uint16_t pos; } cd_snd_t;
static cd_snd_t cd_snd[CD_NTRK];
static struct { int16_t tone, atk, rel; uint8_t ok; } cd_base[CD_NTRK];   /* the sound's own TONE target and envelope */

static const uint8_t CD_ENGINES[] = {ENGI_FM6,
#if FELUCCA_VA
                                     ENGI_VA,
#endif
};
#define CD_NENG ((uint32_t)sizeof CD_ENGINES)
#define CD_LIST_MAX 200u
typedef struct { uint8_t e, pos; } cd_ent_t;
static struct { uint32_t gen; uint8_t ok; uint16_t n; cd_ent_t at[CD_LIST_MAX]; } cd_lst;

static const char *cd_engine_name(uint32_t e)
{
    if (e == ENGI_FM6)
        return "FM";
#if FELUCCA_VA
    if (e == ENGI_VA)
        return "VA";
#endif
    return ENGINES[e % NENGINES]->name;
}
static void cd_list_build(void)
{
    uint32_t i, pos;
    if (cd_lst.ok && cd_lst.gen == up_gen)
        return;
    cd_lst.ok = 1;
    cd_lst.gen = up_gen;
    cd_lst.n = 0;
    for (i = 0; i < CD_NENG; i++) {
        uint32_t e = CD_ENGINES[i], np = pool_count(e);
        for (pos = 0; pos < np && pos < 256u && cd_lst.n < CD_LIST_MAX; pos++) {
            cd_lst.at[cd_lst.n].e = (uint8_t)e;
            cd_lst.at[cd_lst.n].pos = (uint8_t)pos;
            cd_lst.n++;
        }
    }
}
static uint32_t cd_snd_count(void)
{
    cd_list_build();
    return cd_lst.n;
}

/* TONE (FM: the modulators' level; VA: the cutoff) and ENVELOPE (attack and release) from the sound's own values */
static void cd_tone_env(uint32_t part, uint32_t tone, uint32_t env)
{
    track_t *t = &trk[part];
    int32_t dt = (int32_t)tone - 50, de = (int32_t)env - 50, v;
    if (!cd_base[part].ok)
        return;
    fm1_irq_off();
    if (t->eng_req == ENGI_FM6) {
        v = cd_base[part].tone + dt * 4 / 5;     /* (MLVL -64..63: +-40) */
        t->p[P_E2] = (int16_t)(v < -64 ? -64 : v > 63 ? 63 : v);
    }
#if FELUCCA_VA
    else if (t->eng_req == ENGI_VA) {
        v = cd_base[part].tone + dt;             /* (CUT 0..127: +-50) */
        t->p[P_E0] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    }
#endif
    v = cd_base[part].atk + de * 4 / 5;
    t->p[P_ATK] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    v = cd_base[part].rel + de * 4 / 5;
    t->p[P_REL] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    fm1_irq_on();
}
static void cd_take_base(uint32_t part)
{
    track_t *t = &trk[part];
    cd_base[part].tone = t->eng_req == ENGI_FM6 ? t->p[P_E2] : t->p[P_E0];
    cd_base[part].atk = t->p[P_ATK];
    cd_base[part].rel = t->p[P_REL];
    cd_base[part].ok = 1;
}

static void cd_load_factory(uint32_t part, uint32_t e, uint32_t pi)
{
    track_t *t = &trk[part];
    const engine_t *en = ENGINES[e % NENGINES];
    const preset_t *pr;
    uint32_t i;
    if (!en->npresets)
        return;
    pr = &en->presets[pi % en->npresets];
    fm1_irq_off();
    t->eng_req = (uint8_t)(e % NENGINES);
    t->preset = (uint8_t)(pi % en->npresets);
    t->user = 0;
    t->trim = 0;
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = V_POLY;
    fm6_track_loaded(t);
    panic_req |= (uint8_t)(1u << part);
    fm1_irq_on();
    str_cpy(cd_snd[part].name, pr->name, sizeof cd_snd[part].name);
}
static void cd_load_user(uint32_t part, uint32_t k)
{
    track_t *t = &trk[part];
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    if (!up_used(k))
        return;
    r = up_rec(k);
    up_values(r, v);
    fm1_irq_off();
    t->eng_req = eng_ok(r->engine) ? r->engine : ENGI_FM6;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = v[i];
    t->preset = 0;
    t->user = (uint8_t)(k + 1u);
    t->trim = 0;
    t->p[P_VOICE] = V_POLY;
    fm6_track_loaded(t);
    panic_req |= (uint8_t)(1u << part);
    fm1_irq_on();
    up_name(k, cd_snd[part].name);
}
static void cd_load_init(uint32_t part, uint32_t e)
{
    track_t *t = &trk[part];
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t i;
    fm1_irq_off();
    t->eng_req = (uint8_t)(e % NENGINES);
    t->preset = 0;
    t->user = 0;
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = en->edit[i].def;
    t->trim = 0;
    t->p[P_VOICE] = V_POLY;
    fm6_track_loaded(t);
    panic_req |= (uint8_t)(1u << part);
    fm1_irq_on();
    str_cpy(cd_snd[part].name, "INIT", sizeof cd_snd[part].name);
}
/* the list's place idx on track `part` */
static void cd_snd_load(uint32_t part, uint32_t idx)
{
    uint32_t n = cd_snd_count(), e, pos;
    pool_ent_t en;
    if (!n)
        return;
    idx %= n;
    e = cd_lst.at[idx].e;
    pos = cd_lst.at[idx].pos;
    en = pool_entry(e, pos);
    if (en.kind == PK_USER) {
        cd_load_user(part, en.slot);
        if (en.fk != PF_NONE)
            trk[part].trim = cb_trim(e, en.fk);
    } else if (en.kind == PK_FACTORY) {
        cd_load_factory(part, e, en.fk);
        trk[part].trim = cb_trim(e, en.fk);
    } else {
        cd_load_init(part, e);
    }
    cd_snd[part].engine = (uint8_t)e;
    cd_snd[part].kind = en.kind;
    cd_snd[part].pos = (uint16_t)idx;
    cd_take_base(part);
}
/* a sound by its engine and name: its place in the list, or -1 */
static int cd_snd_find(uint32_t e, const char *name)
{
    uint32_t i, n = cd_snd_count();
    for (i = 0; i < n; i++) {
        pool_ent_t en;
        char nm[13];
        if (cd_lst.at[i].e != e)
            continue;
        en = pool_entry(e, cd_lst.at[i].pos);
        if (en.kind == PK_FACTORY)
            str_cpy(nm, ENGINES[e % NENGINES]->presets[en.fk].name, sizeof nm);
        else if (en.kind == PK_USER)
            up_name(en.slot, nm);
        else
            continue;
        if (cb_streq(nm, name))
            return (int)i;
    }
    return -1;
}
