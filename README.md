# libsynth

A polyphonic synthesis core in C11 that runs from the same source on a desktop,
on a microcontroller, and on Android.

It is small — 9.0 KB of code and 5.0 KB of RAM at eight voices on a Cortex-M4F —
because it has no dependencies at all. No libm, no malloc, no threads, no OS. Pitch, curves, sine,
tangent and exponentials are arithmetic; the caller owns the memory; the audio
callback calls one function.

```c
#include "synth/synth.h"

static synth_t synth;                 /* the caller owns this, so an MCU can
                                         declare it static and never allocate */

synth_init(&synth, 48000.0f);
synth_note_on(&synth, 60, 1.0f);

/* in the audio callback, and nowhere else: */
synth_render(&synth, out, n_frames);
```

## Building

```sh
cmake -S libsynth -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

That builds the library, the test suite and `render_wav`, and needs nothing but
a C compiler. `build/render_wav out.wav` writes a demo — a sequenced line and a
kick drum, as two independent instances summed together — with no audio hardware
and no downloads involved.

`-DSYNTH_BUILD_PC_PLAYBACK=ON` additionally builds a realtime desktop player,
which is the one thing here that fetches something (miniaudio, header-only).

## What a voice is

Oscillator, then filter, then amplifier, which is both the classic arrangement
and the order the units are wired in `synth_render()`.

- **Oscillator** — sine, PolyBLEP saw and square, Casio-style phase distortion,
  VOSIM pulse trains, a wave-terrain orbit, and interpolated noise whose
  bandwidth follows the note, so a high one gives hi-hats.
- **Filter** — a topology-preserving state-variable filter, low/high/band-pass,
  with its own DAHDSR envelope and key tracking.
- **LFO** — one per voice, retriggered by its note, reaching the cutoff in
  octaves, the pitch in semitones and the amplitude as a dip.
- **Pitch envelope** — attack and decay, in octaves. This is what makes
  percussion possible: a sine falling an octave and a half onto a low note in
  forty milliseconds is a kick drum; the same note without the sweep is a beep.
- **Glide** — portamento, in seconds. A note starts on the pitch of the one
  before it and travels; each voice carries its own, so a chord built one note
  at a time does not drag the notes already in it.
- **Amplifier** — per-note level, velocity sensitivity, and a soft saturation
  that is exactly the identity when the drive is zero.

A delay line lives outside the engine, in `src/delay.c`, with its buffer owned
by the caller: a groovebox puts one echo on a track or one on the whole mix, and
which of those it wants is not the library's decision. Nothing in the core
refers to it, so a target that does not want an echo never links it.

Every modulation depth is bipolar and neutral at its centre, and the engine does
not compute one that reaches nothing: a patch that has not asked for an
envelope, an LFO or a sweep does not pay for it, which is 43% of the render loop
on a Cortex-M4F.

Everything is driven through parameters normalized to `[0, 1]`, with range,
curve and name in a descriptor table, so a MIDI CC, an ADC reading and a UI
slider all map onto them the same way. A patch is exactly those numbers:
`synth_save_patch()` writes a plain float array with no format of its own.

## Threads, and the one function that crosses them

Everything in `synth.h` assumes the caller is the audio thread. The exception is
`synth_schedule()`, which is lock-free, never blocks, and carries a frame
number:

```c
synth_event_t e = { synth_frame_time(&synth) + 4800,
                    SYNTH_EVENT_NOTE_ON, 60, 1.0f };
synth_schedule(&synth, &e);          /* safe from a UI or sequencer thread */
```

It returns 0 when the fixed-size queue is full, which is a signal to schedule
less far ahead, not an error to ignore.

`synth_render()` splits its block at those frame offsets, so a sequencer step
lands on its own frame rather than on the buffer boundary. A block also has a
deadline — 96 frames at 48 kHz is 2 ms — so what a *busy* block costs is
measured rather than averaged away: sixteen notes or sixteen parameter changes
landing in the same one take it from 31% of a 168 MHz Cortex-M4F's budget to
40%, not past it. `tools/bench-block/run.sh` is where that comes from.

## Several parts

There is no channel argument, because there is no mutable global state: every
translation unit has an empty `.bss`, and the only global is the const parameter
table. A groovebox gives each track its own `synth_t` — its own patch, voices
and event queue — and sums the outputs. Cost follows sounding voices rather than
instances; eight idle parts cost 0.12% of a desktop core.

The host owns two things the library cannot pick: the headroom (each instance is
bounded by 1 on its own, so four in unison reach four) and the clock (each
instance starts at frame zero, so a part created mid-session needs
`synth_set_frame_time()`).

## Platforms

| | how | state |
|---|---|---|
| Desktop | miniaudio, `backends/pc/` | builds in CI, never run — no sound card there |
| Android | AAudio through JNI, `backends/android/` | reported working on hardware at 48 kHz; CI checks the signatures and the headers |
| Bare metal | `backends/embedded/rp2040_example.c` | cross-builds and fits; see below |

The Android side is a contract, not a suggestion:
`backends/android/java/com/kidoe/synth/SynthEngine.java` is the file the app
talks to, and `tools/check-java-constants.sh` fails CI when its copy of the
parameter list drifts from the header.

MIDI lives in its own translation unit (`src/midi.c`), so a target that does not
want it never links it. It parses bytes, not decoded events, because the same
stream arrives from USB on a desktop, a UART on a microcontroller and
MidiManager on Android.

## Tuning it for a target

Everything configurable is in `include/synth/config.h` and overridable from the
compiler:

| | default | what it costs |
|---|---|---|
| `SYNTH_MAX_VOICES` | 8 | every block scans all slots, sounding or not |
| `SYNTH_MOD_INTERVAL` | 4 | how often a modulated filter is retuned |
| `SYNTH_EVENT_QUEUE_LEN` | 64 | 24 bytes each; it is a lookahead budget |
| `SYNTH_PARAM_NAMES` | 1 | set to 0 to drop the name strings from flash |

Polyphony is a compile-time constant, so each voice count is a different
program; CI tests 4, 8 and 32.

## Claims, and how far they are checked

The point of this library is portability, so the claims about it are measured
rather than asserted, and the ones that are not are labelled.

- **Verified here**: the core and its 116 tests, under gcc and clang with
  `-Wconversion -Werror`, at three voice counts, with the headers compiled as
  C++ and with parameter names stripped. Spectra are measured with a Goertzel
  probe at exact frequencies rather than asserted on the shape of the code, and
  aliasing is measured as energy below the fundamental, where a band-limited
  waveform has none.
- **Verified by construction**: the dependency claim.
  `tools/check-no-external-deps.sh` reads the symbol table and separates
  compiler runtime — the `__aeabi_*` float routines a core without an FPU pulls
  in, and `memcpy`/`memset` from struct assignment — from anything real, and
  fails on the latter. It takes an `nm` to use, so it runs on cross builds too.
  On Cortex-M4F the whole library needs one symbol: `memset`.
- **Measured on emulated silicon**: `tools/bench-arm/run.sh` renders a second
  of audio on QEMU's Cortex-M0 and Cortex-M4F models and counts instructions
  with a TCG plugin, and a second plugin attributes them to functions. Eight
  voices cost 47.6 M instructions a second on the M4F and 1122 M on the M0 —
  soft float is about 24x the whole render loop, not the modest per-call tax
  the symbol list suggests. So an M4F-class part runs eight voices in under a
  third of a 168 MHz core, while an RP2040 needs 1.2 cores for one voice.
  Instructions are not cycles, so those are floors.
- **Not verified**: the desktop backend has never been run, and no embedded
  target has ever run on real silicon. `CLAUDE.md` keeps the full list, kept
  deliberately honest.

## Layout

```
libsynth/
  include/synth/   config.h, dsp.h, synth.h, midi.h — the public surface
  src/             dsp.c (units), synth.c (engine), midi.c and delay.c, each
                   its own translation unit so a target can leave it out
  tests/           one host suite, no audio hardware needed
  backends/        pc, android, embedded
  examples/        render_wav, and the demo sequencer both players share
  tools/           the checks CI runs
```
