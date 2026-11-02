# Cloudrift

**An ambient instrument for the M-VAVE FM-1**, built on CoralFlower's foundation (its hardware layer, FM and VA
engines, effects, storage and installer) with its own instrument on top, after a browser sketch of the idea:

- **Four tracks**, each its own sound, LENGTH (1 to 32 steps), DIVIDE, PROBABILITY and SWING, so they drift against
  each other.
- **One key, a whole texture.** A key plays the chord of that note in KEY's major scale (a 9th or 11th, voice-led
  from the last one) on every CHORD track, its root on every FOLLOW track, and the arpeggiator over it (8 modes: UP,
  DOWN, UP-DN, RANDOM, EUCLID, DRIFT, TURING, BLOOM). A SEQ track plays its own steps; REC writes what you play into them.
- **The Cloud**: a granular reverb (a long modulated tank, octave and reversed grains fed back into it, FREEZE), fed
  by each track's SEND, with a slow ensemble on the dry sound.

| Control | Does |
|---|---|
| HOME, ENV (PATCH), ARP, FX (SPACE), SEQ, GLO | the pages; KNOB 1..4 set the page's four values |
| SEL | hold (or tap) for the short keys: T1..T4, FOLLOW, LEN, SRC; SEL + ARP makes the track an ARP track |
| EDIT | the selected track CHORD or SEQ |
| LFO | FREEZE |
| PLAY, REC, SAVE | play / stop, record into the selected SEQ track, save the setup (it also saves itself when idle) |
| SELECT, PRESETS, ALGORITHM | the track's LEVEL, SPACE, TEMPO |
| the long keys on SEQ | the track's steps 1..16 |

MIDI: notes in on any channel play like the keys; the tracks' notes go out on channels 1 to 4, with clock.
The package identity is **FM-1_941**, its version text "Cloudrift X.Y"; install it from this fork's page,
https://iamweswebb.github.io/CoralFlowerRemix/, with "Choose a .fwsc file". The official CoralFlower installer can put
CoralFlower back. CoralFlower's own unit (firmware/src/coralflower.c) stays in the tree: `UNIT=coralflower` builds it.

---

What follows is CoralFlower's own README.

**Your M-VAVE FM-1, made for chords.** CoralFlower is free firmware for the FM-1. Press a chord key, and every note
you play becomes a whole chord, with a bass underneath. While you play, a pixel flower grows from your chords.

CoralFlower is inspired by the Telepathic Instruments Orchid. Its controls are redesigned for the FM-1's keys and
buttons, because every instrument is played differently.

<p align="center"><img src="docs/images/grow.gif" width="480" alt="A pink pixel flower growing and changing as chords are played"></p>

<p align="center">
  <a href="https://ficusficusov.github.io/CoralFlower/play/"><b>Play it in your browser</b></a> ·
  <a href="https://ficusficusov.github.io/CoralFlower/"><b>Install it on your FM-1</b></a> ·
  <a href="https://github.com/FicusFicusov/CoralFlower/releases"><b>Releases</b></a>
</p>

Not sure yet? The browser version runs the same firmware with sound. Try it with your mouse or computer keyboard
before you install anything.

## What it does

- **Notes or chords.** The lower keys play the notes of a scale you choose. Press an upper key, and each note
  becomes a chord: major, minor, sus2 or sus4, plus major and minor 6, 7 and add9. Press two upper keys together to
  combine them. The chord stays on until you choose another one. The key marked Off turns chords off, and the keys
  play single notes, several at once. Choose from 27 scales, chromatic included, starting on any note. Chord glide (off by default) slides
  each note into the next chord.
- **Two layouts, each with a Lock mode.** In the Top layout, the upper row picks the chord type and the lower row the
  root. In the Side layout, a few keys on the left pick the type and the rest play roots. Lock mode keeps the chord
  type you tapped, so you don't have to hold its key. A new FM-1 starts in Top with Lock; without Lock, a chord
  plays only while you hold its key.
- **A bass part under your chords** (you can turn it off), with its own sound and range, plus an octaver that doubles each bass note.
- **An arpeggiator** with nine patterns, including a strum like the one on the Orchid chord synth. It locks to the
  metronome or to an external MIDI clock.
- **A sequencer** with eight patterns of chords, bass, drums and one more sound (the layer), and a song made of them.
  REC on the main screen records what you play into a new pattern, after a one-bar count-in.
- **Six effects** in an order you choose: filter, drive, chorus, phaser, delay and reverb.
- **About 60 sounds** from two synth engines, virtual analogue and FM: pads, keys, strings and basses. Many pads
  change slowly on their own. Chords and basses have separate lists. Every sound has an editor, and you can save your
  own.
- **A plant that grows from your music.** In the Top key layout a flower grows; in the Side layout, a coral.
  Each note of the chord adds a part: a bloom, a bud, a cluster of berries. The plant's structure follows
  [PlantStudio](https://github.com/pdfernhout/PlantStudio), a program that models real plants.
- **MIDI over USB.** The chords, the bass and the sequencer's Layer and Drums go out on four channels you set. The
  FM-1 follows an external MIDI clock or sends its own.

<table>
  <tr>
    <td><img src="docs/images/home.png" width="260" alt="HOME: the chord name above a pink flower"></td>
    <td><img src="docs/images/coral.png" width="260" alt="The Side layout: a pink coral"></td>
    <td><img src="docs/images/chord.png" width="260" alt="CHORD: a chord button's notes on a piano"></td>
  </tr>
  <tr>
    <td><img src="docs/images/fm-voice.png" width="260" alt="The FM editor: the algorithm's operators"></td>
    <td><img src="docs/images/arp.png" width="260" alt="ARPEGGIO: the pattern as a little piano roll"></td>
    <td><img src="docs/images/take.png" width="260" alt="A take being recorded into a new pattern's Chords track"></td>
  </tr>
</table>

## Install

1. Open <https://ficusficusov.github.io/CoralFlower/> in **Chrome or Edge** on a computer. Only these browsers can talk
   to the FM-1 over USB.
2. Connect the FM-1 straight to the computer. Don't use a USB hub, and use a cable that carries data, not a
   charge-only one.
3. Press **Install on your FM-1**. Keep the page open and the cable in for about a minute. The FM-1 restarts as
   CoralFlower.

If the FM-1 runs CoralFlower, ChoralRoot or Felucca, the installer saves your sounds to a file first. The official firmware's sounds can't be backed up. To return to the stock firmware, download FM-1
V15 from [m-vave.com](https://www.m-vave.com) and install it from the same CoralFlower page. Installing firmware is at
your own risk.

## Build it yourself

- **From a fresh clone.** Nothing generated is in git: every build first writes the fonts, icons, tables and samples
  into `build/gen/`, with Python 3, Pillow and fontTools. `python3 tools/build.py --gen-only` does that step alone, for
  any other way of building the firmware. On GitHub, the check workflow (`.github/workflows/check.yml`) builds a fresh
  clone on every push.
- **The emulator and the site.** Install the [Emscripten SDK](https://emscripten.org), then run
  `sh tools/emu/web/build_web.sh` and `sh tools/make_site.sh build/site`. The site lands in `build/site/`, with the
  emulator in `build/site/play/`.
- **The headless runner.** `sh tools/emu/cf_headless.sh SCRIPT` plays a script of key presses, takes screenshots and
  records audio. The tests use it.
- **The firmware package.** `./build.sh --release X.Y` needs the toolchain from JieLi, the maker of the FM-1's chip.
  The toolchain runs on Linux x86-64 only, and `tools/get_toolchain.sh` fetches it. On GitHub, the release workflow
  (`.github/workflows/release.yml`) builds the package for you.

## Credits and licence

CoralFlower is a fork of [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1) by Quixotic7, which is itself
a fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). The FM engine comes
from [Melodee](https://github.com/keremimo/melodee) by Kerem Kilic, and the screens use
[Departure Mono](https://departuremono.com), a font by Helena Zhang. Thank you all.

CoralFlower is free software under the GPL-3.0. `LICENSE` and `LICENSING.md` have the details.

M-VAVE and FM-1 are trademarks of their respective owners. Orchid is a product of Telepathic Instruments. CoralFlower
is not affiliated with M-VAVE, Cuvave or Telepathic Instruments.

## Feedback and support

CoralFlower is new, and your feedback decides what comes next. Tell me what feels good, what feels off, and which
sounds or features you want:

- open a [GitHub issue](https://github.com/FicusFicusov/CoralFlower/issues);
- email me at iambemoli@gmail.com;
- follow the news in the Telegram channel [t.me/ficusficusov](https://t.me/ficusficusov).

CoralFlower has no ads and no paid version. To support it, press **Support** on
[the site](https://ficusficusov.github.io/CoralFlower/): it lists crypto wallets.
