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
  3416 bytes at 8 voices on ARM32, of which 1544 is the event queue.
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
  them the same way.

## Several parts at once

A groovebox wants a patch and a voice pool per track. That is several `synth_t`,
not a channel argument: the engine has no mutable global state at all — the only
global is the const parameter table, and all three translation units have an
empty `.bss` — so instances are independent by construction. Each carries its
own parameters, voices and event queue, and the host sums their outputs.

Measured at 48 kHz in 96-frame blocks, eight voices sounding:

| | cost |
|---|---|
| one instance, 8 notes | 0.74% of a core |
| eight instances, 1 note each | 0.80% |
| eight instances, all silent | 0.12% |
| eight instances, 8 notes each (64 voices) | 6.66% |

So cost follows sounding voices, not instances, and the floor for holding idle
parts is small. What does cost something is that every block scans all
`SYNTH_MAX_VOICES` slots per instance whether or not they sound: dropping it
from 8 to 2 takes eight one-note parts from 0.80% to 0.67%. Size it to what one
*track* needs, not the whole instrument.

A patch is exactly the normalized parameters, so `synth_save_patch` and
`synth_load_patch` move one track's sound around as a plain float array with no
format of its own. It is positional and tied to this build's parameter list;
anything meant to outlive a version change should store the names from
`synth_param_info()` beside the values.

Two things the host owns:

- **Headroom.** Each instance is bounded by 1 on its own, so four parts in
  unison reach four. Nothing in the library can pick that budget.
- **The clock.** `synth_frame_time()` is per instance and starts at zero, so a
  part created mid-session does not share the frame numbers the others are
  already using. Create every part up front, or the sequencer has to offset.

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
  existing patch. Vibrato is the one destination that costs anything, since
  retuning the oscillator recomputes the VOSIM pulse layout, so it is skipped
  entirely when its depth is zero: 8 voices cost 0.73% of a core with the filter
  modulated and 0.86% with vibrato as well.
- **Amplifier**: per-note level, velocity sensitivity, and soft saturation
  `x(1+d)/(1+d|x|)`, which is the identity at `d = 0` and provably keeps
  `|x| <= 1` mapped to `|y| <= 1`.

Waveforms that are not symmetric about zero (phase distortion, VOSIM, terrain)
have their mean removed when the controls move, so none of them emits DC.

## Building and testing

```sh
cmake -S libsynth -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

86 test functions, 219 assertions, no audio hardware needed. Spectra are
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
  Measured at 8 voices: 8.1 KB of flash and 4.3 KB of RAM on M0+, 7.3 KB and
  4.3 KB on M4F — on an RP2040 that is 0.3% of its flash and 1.5% of its SRAM,
  so memory is not the constraint.

  CPU is, and nothing here measures it. An M0+ has no FPU, so each of the
  eleven `__aeabi_*` float routines the build pulls in costs tens of cycles
  where an M4F spends one instruction and needs none of them. Until someone
  renders on real silicon and times it, treat M4F-class hardware as the
  supported target and the Pico as unproven.

## Conventions

- C11, four spaces, declarations at the top of a function.
- Comments explain *why*, not what. Most code needs none.
- Add a test with a behaviour change. If a test fails, work out whether the
  test or the code is wrong before touching either — several bugs in this
  repository were found because a measurement disagreed with an assumption.
