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

- **No external dependencies at all.** Not libm, not malloc, not libc. Pitch,
  curves, sine, tangent and exponentials are all computed with arithmetic in
  `src/dsp.c`. `tools/check-no-external-deps.sh` fails the build if a symbol
  the core does not define ever appears — calling `sinf()` costs nothing on a
  PC and silently breaks the microcontroller target.
- **No allocation and no OS calls, ever.** The caller owns the `synth_t`, which
  is why its fields sit in the header: an MCU declares `static synth_t s;`.
  1840 bytes at 8 voices.
- **`synth_render()` is the only function for the audio callback**, and it is
  real-time safe. Control calls are not synchronised; the host marshals them.
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

47 test functions, 113 assertions, no audio hardware needed. Spectra are
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
- **Type-checks but has never run**: the Android JNI bridge. Its JNI signatures
  match what `javac -h` generates, and `synth_jni.c` compiles clean against the
  real `aaudio/AAudio.h` from three different NDK releases, so the function
  names, argument types and constants are right. It has never been linked
  against `libaaudio` or run on a device, so whether the stream actually opens
  is still unknown.
- **Never built**: any embedded target. "Runs on a microcontroller" is a design
  claim backed by the dependency and memory checks, not by hardware.

## Conventions

- C11, four spaces, declarations at the top of a function.
- Comments explain *why*, not what. Most code needs none.
- Add a test with a behaviour change. If a test fails, work out whether the
  test or the code is wrong before touching either — several bugs in this
  repository were found because a measurement disagreed with an assumption.
