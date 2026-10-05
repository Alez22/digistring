# STRING for Digitakt

STRING is a Karplus-Strong plucked string SRC machine for the original
Digitakt (Mk1), OS 1.53. It is an [elekloader](https://github.com/irpina/elekloader)
mod: a new machine in the SRC menu whose audio goes through the Digitakt's
regular AMP, filter, mixer and effects path, like any stock machine.

**Website, with a demo you can listen to:**
[alez22.github.io/digistring](https://alez22.github.io/digistring/)

Get the current `.elemod` and release notes from the
[v0.7.0 release](https://github.com/Alez22/digistring/releases/tag/v0.7.0).

It works on its own and alongside
[Sophie for Digitakt](https://github.com/soejrd/digisophie): the two mods
patch different bytes and can be installed together. elekloader's lint
also links it with every other Digitakt mod in its shop: digislicer,
digineighbor, digichain, digimono, digipoly, digihealth, digieq,
digimatrix and digiutils (some of those cannot be combined with one
another, independently of STRING).

---

**USE AT YOUR OWN RISK.** This modifies your instrument's firmware. Back up
your projects and sounds, and keep a stock OS 1.53 file for recovery.

---

## Controls

STRING borrows SLICE's eight parameter slots, so its values are saved,
p-lockable and reachable by MIDI CC/NRPN like any stock parameter. It keeps
SLICE's ranges and defaults; the controls are placed so that those defaults
give a sensible new sound.

| SRC knob | Control | Range | Default | What it does |
| --- | --- | --- | --- | --- |
| A | TUNE | stock | 0 | Pitch |
| B | EXC | 4 icons | PLUCK | Exciter: BOW, HIT (mallet), NOISE, PLUCK |
| C | STIFF | 0–127 | 0 | Stiffness: from an ideal string to a bell-like, inharmonic tone |
| D | SAMP | stock | – | Stock sample selector; STRING does not use the sample |
| E | POS | OFF, 1–64 | OFF | Pluck position, from the bridge to the middle of the string |
| F | SOFT | 0–63 | 0 | Darker, softer attack |
| G | TONE | 0–4 | 0 | Damping inside the string: 0 warm, 3 bright, 4 open and metallic |
| H | DECAY | 0–127 | 100 | How long the string itself rings: 0.07 s to about 17 s, endless at the top |

- **EXC** is drawn as an icon instead of a knob. BOW keeps exciting the
  string for as long as the note is held; the other three are one-shot.
- **The AMP page** controls the note envelope as usual. DECAY sets how
  long the string resonates; the AMP decides how long you hear it.
- **DECAY is a time** (T60, the time to fall by 60 dB): the same setting
  rings as long on a low note as on a high one.
- **TONE** is a lowpass inside the string whose cutoff follows the note,
  so a setting sounds alike across the keyboard. As in Rings, the cutoff
  also rises with DECAY: long notes stay bright, and darker TONE settings
  shorten high notes somewhat. On high notes with a long DECAY the filter
  opens completely, so TONE 3 and 4 can sound the same there.
- **Retriggering** a ringing string plucks it again: the new excitation
  adds to the vibration, as on a real string.
- **Pitch:** an untransposed note plays C4 at 261.6 Hz, and the keyboard
  follows equal temperament up to about 12 kHz.
- **Low notes:** below F#1 (47 Hz) the string no longer fits its delay
  line and runs at a lower internal rate instead, down to about 1.5 Hz,
  without octave jumps.
- **STIFF** keeps the fundamental in tune up to about 880 Hz. Above that,
  high STIFF values make very high notes slightly sharp (about +8 cents at
  1760 Hz).
- **LFO** destinations show STRING's names, grouped as `STRG`.

## Install: no compiler required

You need only the prebuilt
[STRING mod](release/digistring-0.7.0.elemod),
[elekloader](https://github.com/irpina/elekloader/releases/latest), and your
own stock Digitakt Mk1 OS 1.53 `.syx`. You do not need ColdFire tools,
Python or a source checkout.

1. In elekloader, select your stock OS with **Change stock firmware**.
2. Choose **Install from file** and select `digistring-0.7.0.elemod`. Enable STRING;
   core 2.1 must be enabled with it. Sophie and digihealth can be enabled
   too.
3. Set the four-character OS version, then choose **Build Firmware** and
   save the generated `.syx`.
4. Send that `.syx` to the Digitakt with Elektron Transfer. Do not power
   off during the update.

The `.elemod` holds only this project's code, not Elektron's firmware.
Neither the stock nor the modified OS file belongs in this repository.

## STRING-fast: more tracks with FAST AUDIO

[STRING-fast](release/digistring-fast-0.7.0.elemod) is the same machine,
same sound and controls, for people who also run **digihealth**. While
digihealth's **FAST AUDIO** is on, STRING's per-sample loop runs from the
Digitakt's fast on-chip memory; when it is off, STRING runs as usual.

On one Digitakt, five STRING tracks played without crackles at about 95%
DSP load with FAST AUDIO on, and went over 100% with it off.

- Install **either** STRING **or** STRING-fast, not both: elekloader
  refuses the pair.
- STRING-fast needs digihealth (GPL, not part of this project). Two
  builds exist:
  - [digihealth 1.0.1 from Sophie's repository](https://github.com/soejrd/digisophie/blob/main/release/digihealth-1.0.1.elemod):
    a diagnostic build where FAST AUDIO starts **off**; switch it on in
    SETTINGS > FAST AUDIO.
  - the original [irpina/digihealth](https://github.com/irpina/digihealth),
    where FAST AUDIO switches itself on after boot.
- FAST AUDIO copies OS code into on-chip memory the OS does not use;
  digihealth checks the copies and falls back if anything overwrites
  them. Treat it, like the rest, as use at your own risk.

Install it as above, choosing `digistring-fast-0.7.0.elemod` and also
enabling digihealth.

## Build from source (developers only)

You need Python 3.9+, a source checkout of elekloader, the ColdFire cross
toolchain and your stock OS 1.53 file. On Debian or Ubuntu:

```sh
sudo apt install binutils-m68k-linux-gnu gcc-m68k-linux-gnu
```

Test and cross-compile from this repository:

```sh
make test          # DSP and UI tests on the host
make cross-check   # compile for the Digitakt's ColdFire (MCF54418; built with -mcpu=54455, same ISA)
```

Build the mod, check it against your stock OS and write a firmware, from
the elekloader checkout:

```sh
STOCK=/path/to/Digitakt_OS1.53.syx
python3 -m elekloader.sdk.build mods/core --stock $STOCK
python3 -m elekloader.sdk.build /path/to/digistring --stock $STOCK
python3 -m elekloader.lint /path/to/digistring/out/digistring-0.8.0.elemod \
    --stock $STOCK --with mods/core/out/core-2.1.elemod
python3 -m elekloader.patch --stock $STOCK \
    --mod mods/core/out/core-2.1.elemod \
    --mod /path/to/digistring/out/digistring-0.8.0.elemod \
    --out Digitakt_OS1.53_STRING.syx --version K005
```

Add `--mod` lines (and the same files as `--with` to `lint`) for Sophie,
digihealth or any other mod to build them into the same firmware; lint
exits 0 only when the whole set links. STRING-fast is built from
`variants/fast`, whose `mod.json` compiles the same sources with
`-DKS_FAST`; it needs digihealth in the firmware.

## How it works

| File | Role |
| --- | --- |
| `karplus.c` | Fixed-point Karplus-Strong engine at 48 kHz: delay line, fractional allpass tuning, note-tracking lowpass, stiffness allpasses, T60 decay, exciters, sleep |
| `digitakt.c` | OS 1.53 adapter: reads pitch, controls, triggers and AMP state, renders each STRING track into its source buffer |
| `ui.c` | SRC page names, value texts, knob scaling, EXC icons and LFO names |
| `ks_loop.inc` | The per-sample loop; STRING-fast compiles it a second time into `.fast` |
| `glue.s` | Machine descriptor, menu icon and the assembly stubs at the patch sites |
| `variants/fast/` | STRING-fast's manifest: same sources, `-DKS_FAST`, needs digihealth |
| `scripts/gen_tables.py` | Generates `ks_tables.inc` (allpass coefficients) |
| `scripts/gen_icons.py` | Generates `ks_icons.inc` from ASCII-art icons |

- **Audio:** the render is hooked at `0x40077fb2`, right after the stock
  source voices and before the buffers reach AMP and filter; the
  following instructions are other mods' render hooks. Each STRING
  track gets 32 samples per block.
- **Memory:** each track's delay line is 2 KB, all statically allocated:
  about 17 KB of RAM in total, small enough to share elekloader's 128 KB
  mod RAM with large mods such as digislicer.
- **CPU:** the per-sample loop has no divides. digihealth's DSP readout
  shows the load on the instrument; see STRING-fast above for a
  measurement.
- **Coexistence with other mods:** Sophie, digichain and digimono hook
  the entry (or an early instruction) of several stock UI routines and
  return to the stock code for other machines. STRING hooks a later
  instruction in the same routines, or redirects their callers, so the
  hooks run in turn without touching the same bytes.
- **Ranges:** STRING cannot change SLICE's ranges and defaults. They come
  from a stock descriptor table shared with the real SLICE machine, and
  changing them safely would mean intercepting a lookup with 31 callers.

To change the EXC icons, edit the ASCII art in `scripts/gen_icons.py` and
run `python3 scripts/gen_icons.py > ks_icons.inc`.

All addresses are for Digitakt Mk1 OS 1.53 only.

## Credits and license

STRING is [MIT licensed](LICENSE). DECAY's T60 curve, TONE's note-tracking cutoff
and the slower loop for very low notes follow the String model of
[Mutable Instruments Rings](https://github.com/pichenettes/eurorack/tree/master/rings)
by Emilie Gillet (MIT), rewritten in fixed point. The Digitakt RAM addresses, the pitch lookup and
the SRC page hooking approach come from
[Sophie for Digitakt](https://github.com/soejrd/digisophie) (MIT), and the
machine slots from elekloader's core mod. STRING is independent of, and
not endorsed by, Elektron. It contains no Elektron firmware or samples.
