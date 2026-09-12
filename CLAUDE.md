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
  4656 bytes at 8 voices on ARM32, of which 1544 is the event queue. The two
  optional pieces are the caller's too and sit outside it: the delay line's
  buffer, and the 288-byte `synth_patch_queue_t` a host needs only if patches
  arrive from another thread.
- **`synth_render()` is the only function for the audio callback**, and it is
  real-time safe. It also drains scheduled events, splitting the block at their
  frame offsets so a sequencer step lands on its own frame.
  `synth_render_add()` is the same render added to what is already in the buffer
  instead of replacing it, which is how several instances mix into one buffer
  with no scratch memory: the first part writes and the rest add, so nothing has
  to be cleared either. It costs one branch a frame on a value that cannot
  change inside a block — 290 instructions on a 96-frame block, measured, which
  is 0.3% of a block with eight voices in it.
- **Two functions are safe to call from another thread, and only two.**
  `synth_schedule()` for events and `synth_patch_send()` for a whole patch;
  everything else assumes the caller is the audio thread. Both are lock-free,
  never block, and return 0 when their fixed-size storage is full. A host driving
  the engine from a UI or sequencer thread goes through them; calling
  synth_note_on directly from there is the race they exist to close. A patch
  needs its own channel because it does not fit in a `synth_event_t` — it is an
  array, not a float — and sending it one parameter at a time is both slower and
  liable to overflow the event queue: 34 events per track, so four tracks
  changing kit at once is 136 against a queue of 64. `src/patch_queue.c` is its
  own translation unit, so a target with one fixed sound never links it.
- **Polyphony is a compile-time constant** (`SYNTH_MAX_VOICES`), so each voice
  count is a different program and CI tests 1, 4, 8 and 32. One voice is in that
  list because a pool sized to a single track is a configuration this file
  recommends, and it had been quietly broken: six assertions expected a chord to
  fit in a pool too small to hold it, which is voice stealing working correctly.
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
  | nothing sounding | 7,369 | 2.2% |
  | 8 voices, no events | 105,235 | 31.3% |
  | 8 voices, one parameter change | 106,972 | 31.8% |
  | 8 voices, 16 parameter changes | 132,634 | 39.5% |
  | 8 voices, every parameter as an event | 254,015 | 75.6% |
  | 8 voices, a whole patch adopted | 209,595 | 62.4% |
  | 8 voices, 8 notes starting | 117,865 | 35.1% |
  | 8 voices, 16 notes starting | 132,280 | 39.4% |
  | 4 instances mixed, 2 voices each | 127,584 | 38.0% |
  | 4 instances mixed, every slot full | 420,670 | 125.2% |
  | 4 instances, all four adopt a patch | 234,219 | 69.7% |
  | 4 instances mixed, all silent | 29,791 | 8.9% |

  The instance rows are the same arithmetic seen from the other side: the same
  eight sounding voices cost 21% more spread over four instances than gathered
  in one, because each instance scans all of its slots on every block, and four
  instances playing nothing at all still cost 8.9% of the budget. Thirty-two
  voices over four tracks do not fit an M4F at all. On a phone they are nothing,
  which is why the Android bridge ships four tracks; on an M4F, four tracks want
  a voice count sized to a track. `SYNTH_MAX_VOICES=4` in the environment re-runs
  the whole table for such a build.

  Those rows hold every slot sounding, which is the expensive end and not the
  usual one. **A kit change costs what the voices it can be heard in cost**, and
  three things had to go for that to be true. Measured as the change alone, with
  the render subtracted:

  | one kit change | 8 of 8 sounding | 1 of 8 sounding |
  |---|---|---|
  | before any of this | 440,066 | 348,348 |
  | the terrain derived once per instance | 136,304 | 44,586 |
  | idle slots skipped, the orbit walked only when something orbits it | 114,728 | **15,044** |

  and the same change sent as one parameter event each, which is what a host
  without `synth_patch_send()` has to do: 819,811 → 209,944 → 166,718 with every
  slot sounding, and 726,377 → 116,510 → **30,463** with one. A kit change used to
  overrun the 2 ms deadline on its own whatever was playing; it is now 4.5% of it
  in the case a groovebox actually hits.

  Each of the three came out of measuring one changed control at a time:

  - **The two wave-terrain controls cost 328,527 instructions for one move** — a
    whole deadline on their own — because every voice walked the orbit to derive a
    cross-section that depends on nothing but those two numbers. It belongs to the
    instance, and the second lap of each walk was only re-finding the extremes the
    first had already passed.
  - **A parameter change applied to every voice slot, sounding or not**, though an
    idle slot is rebuilt from these same parameters by the note_on that claims it.
    A test pins that: for every parameter under every waveform, a note played
    after an edit comes out identical to one that was already sounding when the
    edit arrived.
  - **The orbit was walked even when nothing orbited it.** The walk is 21,545
    instructions and only the terrain waveform reads its result, so a patch that
    is not a terrain patch no longer pays for one. Switching to the terrain
    derives then, against whatever the radius and ratio have become.

  All of it is bit-identical to the previous behaviour: 12,544 samples across
  every waveform with every parameter moved repeatedly against idle, sounding and
  releasing voices, and 3,840 more switching the terrain in and out under a sweep
  of both its controls.

  The map from a parameter to the units it reaches is a switch with no default,
  so `-Wswitch` refuses a parameter nobody has placed in it, and a test checks
  every parameter under every waveform against the long way round: a sounding
  voice edited with `synth_set_param()` has to come out identical to one edited
  by re-loading the whole patch.
- **What reaches nothing is not computed.** This is the rule the library keeps
  finding new places to apply: a modulation depth at its centre, a parameter
  change aimed at a voice slot nobody can hear, a wave terrain's cross-section
  when the terrain is not the selected waveform, and the two waveform
  derivations that follow the pitch — the VOSIM pulse layout and the phase
  distortion's knee — when their waveform is not the one playing. That last one
  is 12% of a vibrato patch on an M4F and 18% on an M0, because a retune happens
  per voice at control rate and both were being recomputed whatever was
  selected.

  For modulation it works because every depth in the library is bipolar and
  neutral at its centre, so "does this reach anything" is one comparison, and it
  is loop-invariant: `render_block()` decides once per
  block whether to advance each envelope and the LFO and whether to retune the
  oscillator and the filter. A patch that has not asked for modulation pays for
  none of it, which is 47% of the render loop. The one thing it
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
global is the const parameter table, and every translation unit has an empty
`.bss` — so instances are independent by construction. Each carries its own
parameters, voices and event queue, and the host sums their outputs with
`synth_render_add()`: the first part writes the buffer, the rest add to it.

**Render every part on every callback, the silent ones included.** Each clock is
that instance's own and advances only inside a render, so a part skipped because
it had nothing to play falls behind the others — and then a sequencer scheduling
against `synth_frame_time()` aims that part's next note at a frame it believes
has already gone by. Skipping silent parts is the optimisation this design
invites and it is wrong; what it saves is the idle floor below, and what it
costs is the single timeline the whole sequencer depends on. (`synth_set_frame_time()`
is the deliberate way back onto the timeline, for a part created mid-session.)

Counted by `tools/bench-instances/run.sh`, at 48 kHz in 96-frame blocks, for one
second of audio:

| | instructions |
|---|---|
| one instance, 8 notes | 50.1 M |
| eight instances, 1 note each | 76.2 M |
| eight instances, all silent | 29.7 M |
| eight instances, 8 notes each (64 voices) | 401.2 M |

Cost follows sounding voices, not instances: eight notes cost 46.4 M whether
they sit in one instance or in eight, once the idle floor is taken off. That
floor is what an instance costs for existing — 3.7 M a second each, because
every block scans all `SYNTH_MAX_VOICES` slots whether or not they sound. It is
small beside a sounding voice at 5.8 M, but it is per instance and it is paid
forever, so size the voice count to what one *track* needs rather than to the
whole instrument: at `SYNTH_MAX_VOICES=2` the same eight one-note parts cost
60.8 M instead of 76.2 M, and the idle floor drops from 29.7 M to 14.0 M.

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

Loading one belongs to the audio thread, so a UI that changes a track's sound
goes through `synth_patch_send()` and the audio callback answers with
`synth_patch_apply()` before it renders. Two slots per track, lock-free, refusing
rather than tearing when both are still the reader's — which takes three sends to
one track inside a single block. When two are waiting, only the newer is loaded:
no audio was rendered between them, and loading A and then B leaves exactly what
loading B leaves, so the collapsed pass is identical and half the work. The cost
of the channel when nothing is using it is 17 instructions a block per track, two
atomic loads and a comparison.

The one thing a scheduled parameter event still does better is land on an exact
frame. A patch arrives at a block boundary, which at 96 frames is 2 ms.

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
  making a render unrepeatable. A waveform's derived settings are kept current
  only while that waveform is the one selected — `synth_osc_set_wave()` is what
  brings the new one up to date, and assigning the field directly is a mistake
  the header warns about. Wave terrain is the one waveform whose settings
  cost real work to derive — a lap of the orbit to find its mean and its peak,
  since an arbitrary surface is neither centred nor bounded by 1, which is 21,545
  instructions. So that derivation belongs to the instance rather than to the
  voice, since it depends on the radius and the ratio and on nothing else, and it
  does not happen at all unless the terrain is the selected waveform.
- **Filter**: topology-preserving SVF, low/high/band-pass, with its own DAHDSR
  envelope and key tracking. Retuned every `SYNTH_MOD_INTERVAL` samples because
  recomputing coefficients costs far more than a sample of audio.
- **LFO**: one per voice, retriggered by its note, running at control rate with
  sine, triangle, square and sample-and-hold shapes. It reaches the cutoff in
  octaves, the pitch in semitones and the amplitude as a dip that can only take
  level away. All three depths ship at exactly zero, so the section changes no
  existing patch and, since nothing then listens, the LFO does not run at all.
  Switching on any destination costs about the same, because what dominates in
  each case is retuning at control rate: 8 voices go from 47.6 million
  instructions a second to 69.6 with the cutoff moving and 69.3 with the pitch
  moving, on a Cortex-M4F.
- **Pitch envelope**: attack and decay only, in octaves, applied to the
  oscillator's frequency at the same control-rate tick that retunes the filter.
  It is what makes percussion possible: a sine falling an octave and a half onto
  a low note in forty milliseconds is a kick drum, where the same note without
  the sweep is a tuned beep. Negative amounts sweep up onto the note instead.
  Its amount ships centred, so it is silent in every patch that predates it, and
  the envelope is not advanced while it is. With it switched on, 8 voices go
  from 47.2 to 65.8 million instructions a second on a Cortex-M4F, 9% dearer
  than vibrato because it also runs an envelope per voice per sample to decide
  where to move the pitch to. On a Cortex-M0 the two are within a per cent of
  each other, and both are a fifth cheaper than moving the cutoff instead.
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

A delay line sits outside all of this, in `src/delay.c`, for the same reason
the MIDI parser does: nothing in the core refers to it, so a target that does
not want an echo never links it, and it costs about 550 bytes of flash when it
does. It is deliberately not part of `synth_t`. A groovebox wants one echo on a
track or one on the whole mix, and that is the host's arrangement to make;
putting it in the engine would fix the answer and make a patch carry a buffer
size. The buffer is the caller's, like the `synth_t` is and for the same reason.

An effect on one track runs on that track's own audio, which means before the
part joins the mix: render the part, run the delay over its buffer, then
`synth_render_add()` the next part on top. `examples/demo.h` is exactly that —
a lead with an echo, a kick without one, one buffer and no scratch memory.

Feedback is the one place in the library where a bounded input would not give a
bounded signal — with feedback `f` the line settles at `1/(1-f)`, which is 20 at
the most the control allows — so what goes into the line is clamped to full
scale. The line is then bounded by 1 and the output is a crossfade between two
things bounded by 1, so the headroom rule survives: an instance with an echo on
it is bounded by 1 like any other. The clamp is only reachable by a patch that
has asked the echo to run away. A time change slides the read point over 50 ms
rather than jumping it, and the line is read between samples, because a moving
read point that snapped to whole samples is zipper noise.

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

130 test functions, 332 assertions, no audio hardware needed. Spectra are
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
  the Android JNI bridge. It holds `SYNTH_TRACKS` engines (4 by default) and
  mixes them into the one stream, so every control call takes a track index and
  a patch on one track leaves the others alone. What CI can check without an NDK
  it now does check, because this is the one contract in the repository with no
  compiler behind it: `tools/check-jni-bridge.sh` diffs every prototype `javac -h`
  generates against the definitions in `synth_jni.c` — names, return types and
  argument types — and then compiles the bridge with the JDK's real `jni.h` and
  the stand-in AAudio headers in `tools/jni-stubs`, at one, four and eight
  tracks. A native method whose C side has drifted from its Java signature
  otherwise links, loads, and reads its arguments off by one on a device: a
  track index arriving where a note number is expected. What none of that
  proves is agreement with the real AAudio, or a note coming out of a speaker.
  Reported working at 48 kHz with 96-frame bursts on real hardware; reported
  failing to open on an API 37 emulator, which is why start() degrades from
  exclusive mono rather than giving up. Neither report is reproducible from here.
- **Cross-compiles and fits, but has never run**: bare metal ARM. CI builds the
  library and `backends/embedded/rp2040_example.c` for Cortex-M0+ and
  Cortex-M4F with `-Wconversion -Werror` and runs the dependency check on both.
  Measured at 8 voices: 10.2 KB of flash and 5.1 KB of RAM on M0+, 9.4 KB and
  5.1 KB on M4F. Two of those translation units are optional and a target that
  leaves them out pays neither: the delay line is 0.6 KB and 0.5 KB of that
  flash, and the patch queue 0.18 KB and 0.16 KB plus 288 bytes of RAM per track
  for the two patches it holds. On an RP2040 the whole thing is 0.5% of its flash
  and 1.9% of its SRAM, so memory is not the constraint.

  CPU is the constraint, and it is now measured rather than guessed.
  `tools/bench-arm/run.sh` renders a second of audio on QEMU's Cortex-M0 and
  Cortex-M4F models and counts the instructions with a TCG plugin:

  | instructions per second of audio | M4F | M0 |
  |---|---|---|
  | silent, 8 empty slots | 3.8 M | 12.6 M |
  | sine, 1 voice | 9.2 M | 152 M |
  | sine, 8 voices | 47.2 M | 1121 M |
  | saw, 8 voices | 46.8 M | 895 M |
  | sine, 8 voices + filter LFO | 69.2 M | 1855 M |
  | sine, 8 voices + vibrato | 60.6 M | 1532 M |
  | sine, 8 voices + pitch sweep | 65.8 M | 1522 M |

  Read these as a floor. QEMU counts instructions retired, not cycles, and
  models neither flash wait states nor the multi-cycle loads and taken branches
  a real Cortex-M pays; silicon is worse than this, never better. Counting the
  same workload with callgrind on x86 lands within about 10% either way — 5%
  under for a sine, 8% over for a saw — which is what two instruction sets doing
  the same arithmetic should look like, and is the cross-check that the plugin
  counts the right thing. It is not close enough to treat one as a stand-in for
  the other.

  What they say:

  - **Soft float costs about 24x on the audio path.** Not the "tens of cycles
    per call" a reader might assume from the symbol list — twenty-four times
    the whole render loop.
  - **M4F-class hardware is the supported target, and now with a number.**
    47.2 M instructions a second for 8 voices is 28% of a 168 MHz STM32F405 at
    one instruction per cycle. Since that is a floor, treat 8 voices as usable
    and leave room for whatever else the firmware does.
  - **The Pico is close but not there.** A single sine voice needs 152 M
    instructions per second of audio, 1.2 times a 125 MHz RP2040 core at one
    instruction per cycle — down from 2.8 times before any of this work.
    Overclocked to 250 MHz one voice fits at 61% of a core at that rate, and
    real silicon does not reach one instruction per cycle, so call it plausible
    for one or two voices on an overclocked Pico and unproven until someone
    renders on the real thing.

  **On fixed point**, which is the standing question for the M0:
  `tools/bench-arm/profile.sh` answers it, by attributing every instruction to
  the function it ran in. Before this work 93.8% of every instruction on the M0 was
  inside `__aeabi_*` float helpers, of which 20.8% was `__aeabi_fdiv` — and
  division turned out to be removable *in float*, by keeping a rate beside each
  envelope time and by saying that zero drive is the identity. That is done, and
  it is where the 30% came from. What is left is 92% float helpers, now almost
  entirely multiply (42%), add (22%) and subtract (18%). Those are irreducible
  without changing the number format: only a fixed-point path removes them, and
  it would have to replace essentially every arithmetic expression in `dsp.c`
  and `synth.c`. The M4F column is the floor such a path aims at — the same
  algorithm with arithmetic that costs one instruction — and it sits 24x below
  where the M0 is. It is a rewrite, not an optimisation, and this repository has
  no M0+ hardware to check it against; the measurement is here so the decision
  can be made on numbers.

## Conventions

- C11, four spaces, declarations at the top of a function.
- Comments explain *why*, not what. Most code needs none.
- Add a test with a behaviour change. If a test fails, work out whether the
  test or the code is wrong before touching either — several bugs in this
  repository were found because a measurement disagreed with an assumption.
