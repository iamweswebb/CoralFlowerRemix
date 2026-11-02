/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 CoralFlower contributors; ChoralRoot FM-1 contributors; Felucca by Leo Kuroshita, Hügelton
 * Instruments */
/* CORALFLOWER: one compilation unit (the HAL is header-only), after ChoralRoot's choralroot.c: Felucca's platform and
 * sound engines, ChoralRoot's FX, sound pools and MIDI-in logic, and CoralFlower's own instrument: the chord model
 * (cf_theory.c, cf_chord.c), the player (cf_play.c, cf_out.c), the plant (flora/), the screens (cf_fb.c, cf_draw.c,
 * cf_screens.c) and the UI (cf_ui.c), with the names main.c calls (cf_shim.c).
 *
 * Host check (no device toolchain): cc -fsyntax-only -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal
 * firmware/src/coralflower.c. The device build is tools/build.py with this file as the unit. */

/* ---------------------------------------------------- build options --- */
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1
#endif
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0
#endif
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1
#endif
#ifndef FELUCCA_UAC
#define FELUCCA_UAC 0            /* USB audio recording: later (the settings that switch it are not here yet) */
#endif
#ifndef FELUCCA_UART
#define FELUCCA_UART 1
#endif
#define FELUCCA_SLICE 0
#define FELUCCA_SLICER 0
#define FELUCCA_FM4 0
#define FELUCCA_SAMPLE 0
#define FELUCCA_GRAIN 0
#define FELUCCA_DRUM 0             /* (four synth tracks: no drum part) */
#define ENG_STATE_NPART 4u         /* engines.c: every track has an engine's state */
#define VA_NPART 4u                /* eng_va.c: .. and a VA patch */
#define MIX_NPART 4u               /* fx.c: all four parts sound */
#define TRK_GLIDE(t) 0             /* voice.c: no portamento (the chords are voice-led instead) */
#define CF_VLOAD 1                 /* voice.c: a sound sounds as many voices as the core has time for, by the
                                    * measured load (the owner: "пока хватает производительности — не ограничивать") */
#define VL_NOW() fm1_ticks()
static void cf_boot_note(void);            /* (cf_fx.c) */
#define VOICE_NEW_NOTE() cf_boot_note()    /* voice.c: a new note, the power-on sound gives way */
#define FELUCCA_ANALOG 0
#define FELUCCA_PHASE 0
#define FELUCCA_LOFI 0
#define FELUCCA_VOICE 0
#define FELUCCA_TRIO 0
#define FELUCCA_NOISE 0
#define FELUCCA_PHYS 0
#define FELUCCA_SEQ 0
#ifndef FELUCCA_VA
#define FELUCCA_VA 1
#endif
#define FELUCCA_CZ 0             /* no CZ-1: its factory tones carry no licence (docs/PLAN.md); its code left the tree in
                                  * 0.5.1 with ChoralRoot's unit choralroot.c (GitHub #4) */
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 0
#endif
#ifndef FM6_POLY
#define FM6_POLY 8
#endif
#define FM6_PLAIN_NAMES 1        /* eng_fm6.c: "PAD", not "FM PAD" (the screens show the engine before the name) */
#ifndef CR_FX
#define CR_FX 1
#endif
#define FELUCCA_PERFORM 0
#ifndef CR_CPU1
#define CR_CPU1 1
#endif
#ifndef CV_ROWS
#define CV_ROWS 120u             /* gfx.c's canvas: CoralFlower's framebuffer lives in it (cf_fb.c) */
#endif
#define FELUCCA_ICONS 0
#define FELUCCA_KEYCAPS 0
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_941"    /* package identity (build.py: the .fwsc marker string; CoralFlower's is FM-1_930) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "Cloudrift 0.1"
#endif
#if FELUCCA_OTA && !FELUCCA_FLASH
#error "FELUCCA_OTA needs FELUCCA_FLASH"
#endif

#define CF_UNIT 1                /* (main.c: CoralFlower's calibration and update hooks, kept) */
#define CLOUDRIFT 1
#include <stdint.h>
static volatile uint8_t cd_vbudget = 16;   /* the voices' budget (voice.c VBUDGET): GLOBAL > VOICES x 2 (cd_ui.c) */
#define VBUDGET (cd_vbudget)
#define VOICE_UNITS_FIXED 2u      /* voice.c: every voice two units, so VOICES (6..8) is the voices sounding */

#if defined(__APPLE__)            /* the host syntax check only: Mach-O has no ELF sections (.noinit, .pool) */
#define __attribute__(x)
#endif

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#if CR_CPU1
#include "fm1_cpu1.h"
#endif
#include "felucca_tables.h"

/* ----------------------------------------------------- base, display --- */
#include "libc.c"
#include "lcd.c"
#include "gfx.c"

/* ----------------------------------------------------------- sound --- */
#include "core.h"
#include "engines.c"
#include "params.c"
#include "mod.c"
#include "voice.c"
#include "slicer.c"
#define mix_block fx_mix_block
#define CF_FX_OWN 1                       /* fx.c: CoralFlower's chain (cf_fx.c) on the mix, not ChoralRoot's sends */
#define CF_LOOP_STORE 1                   /* storage.c: CoralFlower's loop object (OBJ_CFLOOP): Cloudrift's setup */
#define CLN_TRIM 4096                     /* fx.c's Clean output: the mix in at 0 dB */
static void cd_fx_chain(int32_t *l, int32_t *r, uint32_t n);
#define FX_MIX_HOOK cd_fx_chain
#include "fx.c"
#undef mix_block
#include "cf_fx.c"                        /* CoralFlower's effects: the Cloud reverb (cl_*) */
#include "cd_fx.c"                        /* .. Cloudrift's chain: the ensemble, the Cloud on the sends */

/* ------------------------------------------------------------ MIDI --- */
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif

/* ------------------------------------------------- the instrument --- */
#include "cd_types.h"
#include "cd_core.c"
static void mix_block(int32_t *out, uint32_t n)
{
    CR_PROF_START();
    cd_audio_block(n);
    fx_mix_block(out, n);
    CR_PROF(CRP_MASTER);
}
#include "audio.c"

/* -------------------------------------------------------------- UI --- */
#include "panel.c"
#if FELUCCA_FLASH
#include "fm1_flash.h"
#include "storage_hw.c"
#include "storage.c"
#endif
#include "cr_bank.c"             /* upreset.c (32 user sounds, fm6_bank.c), the pools per engine */
#define CF_PUMP() cf_ui_pump()
static void cf_ui_pump(void);
#include "cf_fb.c"
#include "cd_sound.c"
#include "cd_ui.h"
#include "cf_draw.c"
#include "cd_settings.c"
#include "cd_ui.c"
#include "cd_screens.c"
#include "fm6_store.c"
#include "cd_backup.c"           /* backup / restore over SysEx (after usb.c, the stores, cd_settings.c) */
#include "cd_shim.c"

/* ----------------------------------------------------------- update --- */
#if FELUCCA_OTA
#include "ota.c"
#include "ota_hw.c"
#endif
#if FELUCCA_CDC
#include "console.c"
#endif
#include "main.c"
