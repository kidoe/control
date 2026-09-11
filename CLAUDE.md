# control

`libsynth` is a portable polyphonic synthesis library in C, built to run from
the same core source on desktop, on microcontrollers, and on Android.

## Two agents work in this repository

Work is split by directory, not by task, so two sessions never edit the same
file:

| Directory | Owner | Contents |
|---|---|---|
| `libsynth/` | the synth agent | DSP core, engine, tests, backends, JNI bridge |
| `android/` | the Android agent | Gradle app module, UI, lifecycle |

Rules that keep this working:

- **Never push to a branch another session is using.** Take your own
  (`claude/<what-you-are-doing>`) and open a pull request.
- **Stay inside your directory.** If you need something changed on the other
  side of the line, say so and let its owner change it.
- The contract between the two is
  `libsynth/backends/android/java/com/kidoe/synth/SynthEngine.java`. Changing
  its signatures breaks the app; adding to it does not.

## The core's guarantees

These are the reason the library exists. CI enforces them; do not trade them
away for convenience.

- **No external dependencies beyond compiler runtime.** No libm, no malloc, no
  OS. Pitch, curves, sine, tangent and exponentials are all arithmetic in
  `src/dsp.c`. What a cross build does pull in is unavoidable: on a core with no
  FPU every float operation becomes an `__aeabi_*` call from libgcc, and struct
  assignment becomes `memcpy`. On Cortex-M4F the whole library needs one symbol,
  `memset`. `tools/check-no-external-deps.sh` separates those from real
  dependencies and fails on the latter; it takes an `nm` to use, so it runs on
  cross builds too. `__atomic_*` is deliberately not excused — it means asking
  for an atomic wider than the target can do in one instruction, which a
  Cortex-M0+ cannot do at all.
- **No allocation and no OS calls, ever.** The caller owns the `synth_t`, which
  is why its fields sit in the header: an MCU declares `static synth_t s;`.
  4256 bytes at 8 voices on ARM32, of which 1544 is the event queue.
- **`synth_render()` is the only function for the audio callback**, and it is
  real-time safe. It also drains scheduled events, splitting the block at their
  frame offsets so a sequencer step lands on its own frame.
- **`synth_schedule()` is the only function safe to call from another thread.**
  Everything else assumes the caller is the audio thread. It is lock-free and
  never blocks, returning 0 when the fixed-size queue is full. A host driving
  the engine from a UI or sequencer thread goes through it; calling
  synth_note_on directly from there is the race this exists to close.
- **Polyphony is a compile-time constant** (`SYNTH_MAX_VOICES`), so each voice
  count is a different program and CI tests 4, 8 and 32.
- **Parameters are always normalized to [0, 1]**, with range, curve and name in
  a descriptor table, so a MIDI CC, an ADC reading and a UI slider all map onto
  them the same way. Reading one is not free — a bounds check, a clamp, a curve
  and a scale — so the render loop denormalizes the controls it needs once per
  block into a `mod_t` rather than per voice per control tick. It used to do the
  latter, and a fifth of every instruction the library executed went on looking
  up parameters that could not have changed.
- **A block has a deadline, so the worst block is the number that matters.**
  96 frames at 48 kHz is 2 ms, which on a 168 MHz Cortex-M4F is 336,000 cycles.
  Throughput averages hide that, and hid a real fault: a parameter change used
  to re-apply every unit of every voice, so the block one landed in cost 453,000
  instructions — 135% of the budget — and a bar of automation arriving at once
  cost 5.5 million, sixteen times over. A change now re-applies only the units
  that parameter reaches, and `tools/bench-block/run.sh` keeps the figures
  honest:

  | one 96-frame block | instructions | of the budget |
  |---|---|---|
  | nothing sounding | 7,062 | 2.1% |
  | 8 voices, no events | 119,052 | 35.4% |
  | 8 voices, one parameter change | 120,786 | 35.9% |
  | 8 voices, 16 parameter changes | 146,461 | 43.6% |
  | 8 voices, 8 notes starting | 132,809 | 39.5% |
  | 8 voices, 16 notes starting | 148,216 | 44.1% |

  The map from a parameter to the units it reaches is a switch with no default,
  so `-Wswitch` refuses a parameter nobody has placed in it, and a test checks
  every parameter under every waveform against the long way round: a sounding
  voice edited with `synth_set_param()` has to come out identical to one edited
  by re-loading the whole patch.
- **Modulation that reaches nothing is not computed.** Every depth in the
  library is bipolar and neutral at its centre, so "does this reach anything"
  is one comparison, and it is loop-invariant: `render_block()` decides once per
  block whether to advance each envelope and the LFO and whether to retune the
  oscillator and the filter. A patch that has not asked for modulation pays for
  none of it, which is 43% of the render loop on a Cortex-M4F. The one thing it
  costs is that an LFO nothing listens to does not free-run, so turning a depth
  up mid-note starts the wobble from where the note began rather than from a
  phase that had been advancing unheard.
- **The parameter enum is append-only.** A host stores a patch as the positional
  array `synth_save_patch()` writes, so an index that moved would reinterpret
  every saved sound without anything failing. New parameters go on the end;
  `tools/check-java-constants.sh` fails CI when the Android bridge's copy of the
  enum drifts from the header.

## Several parts at once

A groovebox wants a patch and a voice pool per track. That is several `synth_t`,
not a channel argument: the engine has no mutable global state at all — the only
global is the const parameter table, and all three translation units have an
empty `.bss` — so instances are independent by construction. Each carries its
own parameters, voices and event queue, and the host sums their outputs.

Counted with callgrind, at 48 kHz in 96-frame blocks, for one second of audio:

| | instructions |
|---|---|
| one instance, 8 notes | 57.5 M |
| eight instances, 1 note each | 89.8 M |
| eight instances, all silent | 37.0 M |
| eight instances, 8 notes each (64 voices) | 459.5 M |

Cost follows sounding voices, not instances: eight notes cost 52.8 M whether
they sit in one instance or in eight, once the idle floor is taken off. That
floor is what an instance costs for existing — 4.6 M a second each, because
every block scans all `SYNTH_MAX_VOICES` slots whether or not they sound. It is
small beside a sounding voice at 6.6 M, but it is per instance and it is paid
forever, so size the voice count to what one *track* needs rather than to the
whole instrument: at `SYNTH_MAX_VOICES=2` the same eight one-note parts cost
68.5 M instead of 89.8 M, and the idle floor drops from 37.0 M to 15.3 M.

A patch is exactly the normalized parameters, so `synth_save_patch` and
`synth_load_patch` move one track's sound around as a plain float array with no
format of its own. It is positional and tied to this build's parameter list;
anything meant to outlive a version change should store the names from
`synth_param_info()` beside the values.

A patch written by an older build is shorter than this one's list, and
`synth_load_patch_n()` takes its length and fills the rest from the defaults.
That is not the same as zero-filling: every bipolar control is neutral at its
*centre*, so a zero-filled tail would load a saved sound four octaves down
rather than unchanged.

Two things the host owns:

- **Headroom.** Each instance is bounded by 1 on its own, so four parts in
  unison reach four. Nothing in the library can pick that budget.
- **The clock.** `synth_frame_time()` is per instance and starts at zero, so a
  part created mid-session does not share the frame numbers the others are
  already using. Either create every part up front, or call
  `synth_set_frame_time()` once to put the new one on the running timeline.

## Signal path

Oscillator, then filter, then amplifier — the classic arrangement, and the
order the units are actually wired in `synth_render()`.

- **Oscillator**: sine, PolyBLEP saw and square, Casio-style phase distortion,
  VOSIM pulse trains, wave terrain, and noise. Every waveform but noise is a
  pure function of phase; noise draws a random value each cycle and interpolates
  across it, so the note sets its bandwidth and a high one gives hi-hats. Each
  voice is seeded from its index, which keeps unison voices uncorrelated without
  making a render unrepeatable.
- **Filter**: topology-preserving SVF, low/high/band-pass, with its own DAHDSR
  envelope and key tracking. Retuned every `SYNTH_MOD_INTERVAL` samples because
  recomputing coefficients costs far more than a sample of audio.
- **LFO**: one per voice, retriggered by its note, running at control rate with
  sine, triangle, square and sample-and-hold shapes. It reaches the cutoff in
  octaves, the pitch in semitones and the amplitude as a dip that can only take
  level away. All three depths ship at exactly zero, so the section changes no
  existing patch and, since nothing then listens, the LFO does not run at all.
  Vibrato is the destination that costs most, because retuning the oscillator
  recomputes the VOSIM pulse layout.
- **Pitch envelope**: attack and decay only, in octaves, applied to the
  oscillator's frequency at the same control-rate tick that retunes the filter.
  It is what makes percussion possible: a sine falling an octave and a half onto
  a low note in forty milliseconds is a kick drum, where the same note without
  the sweep is a tuned beep. Negative amounts sweep up onto the note instead.
  Its amount ships centred, so it is silent in every patch that predates it, and
  the envelope is not advanced while it is. With it switched on, 8 voices go
  from 52.7 to 81.0 million instructions a second on a Cortex-M4F: it is the
  most expensive modulation in the library, because it forces the oscillator to
  be retuned on every control tick and that relays out the VOSIM pulses.
- **Glide**: portamento, in seconds, linear in semitones so the time is the
  same whatever the interval. A note starts on the pitch of the one played
  before it and travels; the first note of a session has nothing to come from
  and is in tune at once. Held as the distance still to travel rather than as a
  position, so a voice that has arrived needs no target and no further work, and
  each voice carries its own — a chord built one note at a time does not drag
  the notes already in it. Off by default.
- **Amplifier**: per-note level, velocity sensitivity, and soft saturation
  `x(1+d)/(1+d|x|)`, which is the identity at `d = 0` and provably keeps
  `|x| <= 1` mapped to `|y| <= 1`.

Waveforms that are not symmetric about zero (phase distortion, VOSIM, terrain)
have their mean removed when the controls move, so none of them emits DC.

Aliasing is measured rather than assumed, by looking for energy below the
fundamental where a band-limited periodic waveform has none. Every waveform sits
at the probe's own floor. Phase distortion did not: its knee squeezes a half
cycle of sine into that fraction of the period, so at a tight knee and a high
note the fast segment crossed half a sine in under two samples. No correction at
the corner can represent that, so the knee is widened with pitch instead — six
times the phase increment, which is where the measurement reaches the floor.
High notes lose brightness rather than gaining inharmonic tones.

## Building and testing

```sh
cmake -S libsynth -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

106 test functions, 267 assertions, no audio hardware needed. Spectra are
measured with a Goertzel probe at exact frequencies rather than asserted on the
shape of the code, so the tests survive refactoring and catch real regressions.

Optional: `-DSYNTH_BUILD_PC_PLAYBACK=ON` builds a realtime desktop player and
downloads miniaudio; `build/render_wav out.wav` renders a demo with no
dependencies at all.

## What is verified and what is not

Be honest about this line; a lot of it cannot be checked from a container.

- **Verified**: the whole core and its tests, under gcc and clang with
  `-Wconversion -Werror`, at three voice counts, plus the headers compiled as
  C++ and a build with parameter names stripped.
- **Compiles but has never run**: the desktop backend. No sound card in CI.
- **Runs on a device, per the Groovedroid work, but nothing here proves it**:
  the Android JNI bridge. Its signatures match what `javac -h` generates and it
  compiles clean against the real `aaudio/AAudio.h` from three NDK releases,
  which is all this repository can check without an NDK. Reported working at
  48 kHz with 96-frame bursts on real hardware; reported failing to open on an
  API 37 emulator, which is why start() now degrades from exclusive mono rather
  than giving up. Neither report is reproducible from here.
- **Cross-compiles and fits, but has never run**: bare metal ARM. CI builds the
  library and `backends/embedded/rp2040_example.c` for Cortex-M0+ and
  Cortex-M4F with `-Wconversion -Werror` and runs the dependency check on both.
  Measured at 8 voices: 9.1 KB of flash and 4.7 KB of RAM on M0+, 8.3 KB and
  4.7 KB on M4F — on an RP2040 that is 0.4% of its flash and 1.7% of its SRAM,
  so memory is not the constraint.

  CPU is the constraint, and it is now measured rather than guessed.
  `tools/bench-arm/run.sh` renders a second of audio on QEMU's Cortex-M0 and
  Cortex-M4F models and counts the instructions with a TCG plugin:

  | instructions per second of audio | M4F | M0 |
  |---|---|---|
  | silent, 8 empty slots | 3.8 M | 11.2 M |
  | sine, 1 voice | 9.9 M | 210 M |
  | sine, 8 voices | 52.7 M | 1598 M |
  | saw, 8 voices | 52.3 M | 1371 M |
  | sine, 8 voices + pitch sweep | 81.0 M | 2614 M |

  Read these as a floor. QEMU counts instructions retired, not cycles, and
  models neither flash wait states nor the multi-cycle loads and taken branches
  a real Cortex-M pays; silicon is worse than this, never better. The M4F
  figures land within a per cent of what callgrind counts for the same C on
  x86, which is the cross-check that they are counting the right thing.

  What they say:

  - **Soft float costs about 30x on the audio path.** Not the "tens of cycles
    per call" a reader might assume from the symbol list — thirty times the
    whole render loop.
  - **M4F-class hardware is the supported target, and now with a number.**
    52.7 M instructions a second for 8 voices is 31% of a 168 MHz STM32F405 at
    one instruction per cycle. Since that is a floor, treat 8 voices as usable
    and leave room for whatever else the firmware does.
  - **The Pico is still out of reach, but no longer by an order of magnitude.**
    A single sine voice needs 210 M instructions per second of audio, 1.7 times
    a 125 MHz RP2040 core at one instruction per cycle, where before the
    modulation skips it was 2.8 times. Overclocked to 250 MHz one voice is
    within sight of fitting at one instruction per cycle — which real silicon
    does not reach, so it still does not fit in practice. The float path is not
    an RP2040 target; reaching one means fixed point.

## Conventions

- C11, four spaces, declarations at the top of a function.
- Comments explain *why*, not what. Most code needs none.
- Add a test with a behaviour change. If a test fails, work out whether the
  test or the code is wrong before touching either — several bugs in this
  repository were found because a measurement disagreed with an assumption.
