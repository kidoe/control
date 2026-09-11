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

## Signal path

Oscillator, then filter, then amplifier — the classic arrangement, and the
order the units are actually wired in `synth_render()`.

- **Oscillator**: sine, PolyBLEP saw and square, Casio-style phase distortion,
  VOSIM pulse trains, wave terrain. Every waveform is a pure function of phase.
- **Filter**: topology-preserving SVF, low/high/band-pass, with its own DAHDSR
  envelope and key tracking. Retuned every `SYNTH_MOD_INTERVAL` samples because
  recomputing coefficients costs far more than a sample of audio.
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

65 test functions, 169 assertions, no audio hardware needed. Spectra are
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
  Measured at 8 voices: 6.8 KB of flash and 3.9 KB of RAM on M0+, 6.2 KB and
  3.9 KB on M4F — on an RP2040 that is 0.3% of its flash and 1.5% of its SRAM,
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
