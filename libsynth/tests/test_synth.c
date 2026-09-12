#include "synth/synth.h"
#include "synth/midi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                  \
    do {                                                                       \
        float va = (a), vb = (b);                                              \
        if (!(fabsf(va - vb) <= (tol))) {                                      \
            printf("FAIL %s:%d: %s (%f) != %s (%f)\n", __FILE__, __LINE__,     \
                   #a, (double)va, #b, (double)vb);                            \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define SR 44100.0f

/* A pool smaller than the chord steals voices, so what has to be sounding is
   the chord or the pool, whichever is smaller. Written out this way the
   voice-count assertions still mean something at SYNTH_MAX_VOICES=1 — a voice
   per track is a configuration this library recommends — rather than simply
   failing there. */
static int at_most_pool(int wanted)
{
    return (wanted < SYNTH_MAX_VOICES) ? wanted : SYNTH_MAX_VOICES;
}

static float peak(const float *buf, int n)
{
    float p = 0.0f;
    int i;

    for (i = 0; i < n; ++i) {
        float a = fabsf(buf[i]);
        if (a > p) {
            p = a;
        }
    }
    return p;
}

/* Two engines the same sound: what they render has to be bit-identical, not
   close. Both are advanced, so this consumes state — call it last. */
static int rendered_is_identical(synth_t *a, synth_t *b, int frames)
{
    static float ba[512], bb[512];
    int i;

    if (frames > 512) {
        frames = 512;
    }
    synth_render(a, ba, frames);
    synth_render(b, bb, frames);
    for (i = 0; i < frames; ++i) {
        if (ba[i] != bb[i]) {
            return 0;
        }
    }
    return 1;
}

static void render_seconds(synth_t *s, float *buf, int cap, float seconds)
{
    int total = (int)(SR * seconds);

    while (total > 0) {
        int n = (total > cap) ? cap : total;
        synth_render(s, buf, n);
        total -= n;
    }
}

static void test_note_to_hz(void)
{
    CHECK_NEAR(synth_note_to_hz(69.0f), 440.0f, 0.05f);
    CHECK_NEAR(synth_note_to_hz(81.0f), 880.0f, 0.1f);
    CHECK_NEAR(synth_note_to_hz(57.0f), 220.0f, 0.05f);
    CHECK_NEAR(synth_note_to_hz(60.0f), 261.6256f, 0.05f);
}

static void test_sine_accuracy(void)
{
    synth_osc_t osc;
    float worst = 0.0f;
    int i;

    synth_osc_init(&osc, SR);
    synth_osc_set_freq(&osc, SR / 1024.0f);

    for (i = 0; i < 1024; ++i) {
        float got = synth_osc_next(&osc);
        float want = sinf(2.0f * 3.14159265f * (float)i / 1024.0f);
        float err = fabsf(got - want);

        if (err > worst) {
            worst = err;
        }
    }
    CHECK(worst < 0.002f);
}

/* Magnitude at one exact frequency, so alias components can be measured where
   they actually land instead of being smeared across FFT bins. */
static float goertzel(const float *x, int n, float freq)
{
    float w = 2.0f * 3.14159265f * freq / SR;
    float cw = cosf(w);
    float sw = sinf(w);
    float coeff = 2.0f * cw;
    float s1 = 0.0f, s2 = 0.0f;
    float real, imag;
    int i;

    for (i = 0; i < n; ++i) {
        float s0 = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    real = s1 - s2 * cw;
    imag = s2 * sw;
    return sqrtf(real * real + imag * imag) / ((float)n * 0.5f);
}

#define ALIAS_FRAMES 4096

/* A 3 kHz saw at 44.1 kHz has harmonics past Nyquist that fold back onto these
   frequencies. None of them is a harmonic of 3 kHz, so any energy there is
   aliasing and nothing else. */
static const float k_alias_hz[] = { 900.0f, 2100.0f, 5100.0f, 8100.0f, 11100.0f, 14100.0f };
#define ALIAS_POINTS ((int)(sizeof(k_alias_hz) / sizeof(k_alias_hz[0])))

static float alias_energy(const float *buf)
{
    float sum = 0.0f;
    int i;

    for (i = 0; i < ALIAS_POINTS; ++i) {
        sum += goertzel(buf, ALIAS_FRAMES, k_alias_hz[i]);
    }
    return sum;
}

static void render_naive_saw(float *buf, float freq)
{
    float phase = 0.0f;
    float inc = freq / SR;
    int i;

    for (i = 0; i < ALIAS_FRAMES; ++i) {
        buf[i] = 2.0f * phase - 1.0f;
        phase += inc;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}

static void render_osc(float *buf, float freq, synth_wave_t wave)
{
    synth_osc_t osc;
    int i;

    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, wave);
    synth_osc_set_freq(&osc, freq);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        buf[i] = synth_osc_next(&osc);
    }
}

static void test_polyblep_reduces_saw_aliasing(void)
{
    static float blep[ALIAS_FRAMES];
    static float naive[ALIAS_FRAMES];

    render_osc(blep, 3000.0f, SYNTH_WAVE_SAW);
    render_naive_saw(naive, 3000.0f);

    CHECK(alias_energy(blep) < alias_energy(naive) * 0.35f);

    /* The tone itself must survive the correction. */
    CHECK(goertzel(blep, ALIAS_FRAMES, 3000.0f) > goertzel(naive, ALIAS_FRAMES, 3000.0f) * 0.8f);
    CHECK(goertzel(blep, ALIAS_FRAMES, 6000.0f) > goertzel(naive, ALIAS_FRAMES, 6000.0f) * 0.8f);
}

static void test_polyblep_reduces_square_aliasing(void)
{
    static float blep[ALIAS_FRAMES];
    float aliased, fundamental;

    render_osc(blep, 3000.0f, SYNTH_WAVE_SQUARE);
    aliased = goertzel(blep, ALIAS_FRAMES, 2100.0f) + goertzel(blep, ALIAS_FRAMES, 8100.0f);
    fundamental = goertzel(blep, ALIAS_FRAMES, 3000.0f);

    CHECK(fundamental > 0.5f);
    CHECK(aliased < fundamental * 0.05f);
}

static void test_polyblep_leaves_low_notes_alone(void)
{
    static float blep[ALIAS_FRAMES];
    static float naive[ALIAS_FRAMES];
    float diff = 0.0f;
    int i;

    /* At 110 Hz the correction spans 2 samples out of 400, so the waveform
       should be all but identical to the naive one. */
    render_osc(blep, 110.0f, SYNTH_WAVE_SAW);
    render_naive_saw(naive, 110.0f);

    for (i = 0; i < ALIAS_FRAMES; ++i) {
        diff += fabsf(blep[i] - naive[i]);
    }
    CHECK(diff / (float)ALIAS_FRAMES < 0.02f);
}

static void test_phase_distortion_neutral_at_half(void)
{
    static float pd[ALIAS_FRAMES];
    static float sine[ALIAS_FRAMES];
    synth_osc_t osc;
    float worst = 0.0f;
    int i;

    /* A knee at 0.5 leaves the phase ramp linear, so PD must be a plain sine. */
    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_PD);
    synth_osc_set_pd_knee(&osc, 0.5f);
    synth_osc_set_freq(&osc, 440.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        pd[i] = synth_osc_next(&osc);
    }

    render_osc(sine, 440.0f, SYNTH_WAVE_SINE);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        float d = fabsf(pd[i] - sine[i]);
        if (d > worst) {
            worst = d;
        }
    }
    CHECK(worst < 1e-6f);
}

static void test_phase_distortion_adds_harmonics(void)
{
    static float bent[ALIAS_FRAMES];
    static float sine[ALIAS_FRAMES];
    synth_osc_t osc;
    float bent_high, sine_high;
    int i;

    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_PD);
    synth_osc_set_pd_knee(&osc, 0.08f);
    synth_osc_set_freq(&osc, 220.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        bent[i] = synth_osc_next(&osc);
    }
    render_osc(sine, 220.0f, SYNTH_WAVE_SINE);

    /* Upper harmonics appear without any filter being involved. */
    bent_high = goertzel(bent, ALIAS_FRAMES, 1100.0f) + goertzel(bent, ALIAS_FRAMES, 1540.0f);
    sine_high = goertzel(sine, ALIAS_FRAMES, 1100.0f) + goertzel(sine, ALIAS_FRAMES, 1540.0f);
    CHECK(bent_high > sine_high * 20.0f);

    /* The pitch must not move: the fundamental stays put. */
    CHECK(goertzel(bent, ALIAS_FRAMES, 220.0f) > 0.1f);
    CHECK(peak(bent, ALIAS_FRAMES) <= 1.001f);
}

static void test_phase_distortion_is_centred(void)
{
    static float buf[ALIAS_FRAMES];
    synth_osc_t osc;
    float knees[] = { 0.5f, 0.25f, 0.08f, 0.9f };
    int k, i;

    /* Bending the phase makes the two half-cycles unequal in length, so the
       waveform would carry a DC offset that rides the knee control. */
    for (k = 0; k < 4; ++k) {
        float mean = 0.0f;
        int period = (int)(SR / 100.0f);
        int whole = (ALIAS_FRAMES / period) * period; /* a partial period would bias the mean */

        synth_osc_init(&osc, SR);
        synth_osc_set_wave(&osc, SYNTH_WAVE_PD);
        synth_osc_set_pd_knee(&osc, knees[k]);
        synth_osc_set_freq(&osc, 100.0f);
        for (i = 0; i < ALIAS_FRAMES; ++i) {
            buf[i] = synth_osc_next(&osc);
            CHECK(fabsf(buf[i]) <= 1.001f);
        }
        for (i = 0; i < whole; ++i) {
            mean += buf[i];
        }
        CHECK_NEAR(mean / (float)whole, 0.0f, 0.01f);
    }
}

/* Frequency of the strongest harmonic inside a fixed band. VOSIM's formant is a
   local peak, not the global one: the pulse train also carries a lot of energy
   near the fundamental, exactly as a glottal source does. Searching a band well
   above the fundamental finds the formant without presupposing where it is. */
static float band_peak(const float *buf, float f0)
{
    float best_mag = 0.0f;
    float best_hz = 0.0f;
    int k;

    for (k = 1; (float)k * f0 < 6000.0f; ++k) {
        float hz = (float)k * f0;
        float mag;

        if (hz < 500.0f) {
            continue;
        }
        mag = goertzel(buf, ALIAS_FRAMES, hz);
        if (mag > best_mag) {
            best_mag = mag;
            best_hz = hz;
        }
    }
    return best_hz;
}

static void render_vosim(float *buf, float f0, float formant, int pulses, float decay)
{
    synth_osc_t osc;
    int i;

    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_VOSIM);
    synth_osc_set_vosim(&osc, formant, pulses, decay);
    synth_osc_set_freq(&osc, f0);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        buf[i] = synth_osc_next(&osc);
    }
}

static void test_vosim_formant_is_independent_of_pitch(void)
{
    static float buf[ALIAS_FRAMES];

    /* The whole point of VOSIM: pulse width sets the formant, the period sets
       the pitch, and moving one must not drag the other. The comb of harmonics
       only samples the formant every f0 Hz, so that is the resolution limit. */
    render_vosim(buf, 110.0f, 1500.0f, 4, 0.7f);
    CHECK_NEAR(band_peak(buf, 110.0f), 1500.0f, 1.5f * 110.0f);

    render_vosim(buf, 220.0f, 1500.0f, 4, 0.7f);
    CHECK_NEAR(band_peak(buf, 220.0f), 1500.0f, 1.5f * 220.0f);

    render_vosim(buf, 55.0f, 1500.0f, 4, 0.7f);
    CHECK_NEAR(band_peak(buf, 55.0f), 1500.0f, 150.0f);
}

static void test_vosim_formant_follows_pulse_width(void)
{
    static float buf[ALIAS_FRAMES];

    render_vosim(buf, 110.0f, 800.0f, 4, 0.7f);
    CHECK_NEAR(band_peak(buf, 110.0f), 800.0f, 1.5f * 110.0f);

    render_vosim(buf, 110.0f, 2500.0f, 4, 0.7f);
    CHECK_NEAR(band_peak(buf, 110.0f), 2500.0f, 1.5f * 110.0f);

    /* Above the formant the spectrum falls away sharply, which is what makes it
       read as a resonance rather than as plain brightness. */
    render_vosim(buf, 110.0f, 1500.0f, 4, 0.7f);
    CHECK(goertzel(buf, ALIAS_FRAMES, 1430.0f) > goertzel(buf, ALIAS_FRAMES, 2860.0f) * 5.0f);
}

static void test_vosim_is_centred_and_bounded(void)
{
    static float buf[ALIAS_FRAMES];
    float mean = 0.0f;
    int i;

    /* A raw sin^2 train is unipolar; the oscillator removes its mean so the
       engine never emits DC into the filter or the output. */
    render_vosim(buf, 110.0f, 1200.0f, 3, 0.6f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        mean += buf[i];
        CHECK(fabsf(buf[i]) <= 1.0f);
    }
    CHECK_NEAR(mean / (float)ALIAS_FRAMES, 0.0f, 0.02f);
}

static void test_vosim_decay_shapes_the_burst(void)
{
    static float flat[ALIAS_FRAMES];
    static float steep[ALIAS_FRAMES];

    /* With no decay every pulse is full height, so the burst carries more
       energy than one that fades across the same number of pulses. */
    render_vosim(flat, 110.0f, 1200.0f, 4, 1.0f);
    render_vosim(steep, 110.0f, 1200.0f, 4, 0.3f);

    CHECK(goertzel(flat, ALIAS_FRAMES, 1200.0f) > goertzel(steep, ALIAS_FRAMES, 1200.0f));
}

static void test_vosim_pulses_cannot_overflow_the_period(void)
{
    static float buf[ALIAS_FRAMES];
    int i;

    /* 16 pulses of 1/200 s cannot fit in a 1/110 s period; the oscillator must
       clamp rather than run past the end of the cycle. */
    render_vosim(buf, 110.0f, 200.0f, 16, 1.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        CHECK(isfinite(buf[i]));
        CHECK(fabsf(buf[i]) <= 1.0f);
    }
}

/* The two pitch-dependent derivations are kept current only for the waveform
   that is playing, which is what makes a retune cheap for a sine. The price is
   that switching to one of them has to derive it from the pitch and the settings
   as they are now, not as they were when that waveform was last selected. Both
   halves of this are a silent wrong note if they break: a VOSIM burst laid out
   for the wrong fundamental, or a phase-distortion knee narrow enough to alias
   at a pitch it was never checked against. */
static void test_switching_to_a_waveform_derives_it_at_the_pitch_it_finds(void)
{
    synth_osc_t direct, switched;

    /* VOSIM throughout, against VOSIM interrupted by a sine while both the pitch
       and the pulse settings move. */
    synth_osc_init(&direct, SR);
    synth_osc_set_wave(&direct, SYNTH_WAVE_VOSIM);
    synth_osc_set_vosim(&direct, 900.0f, 3, 0.5f);
    synth_osc_set_freq(&direct, 220.0f);

    synth_osc_init(&switched, SR);
    synth_osc_set_wave(&switched, SYNTH_WAVE_VOSIM);
    synth_osc_set_vosim(&switched, 400.0f, 6, 0.1f);
    synth_osc_set_freq(&switched, 110.0f);
    synth_osc_set_wave(&switched, SYNTH_WAVE_SINE);
    synth_osc_set_vosim(&switched, 900.0f, 3, 0.5f);
    synth_osc_set_freq(&switched, 220.0f);
    synth_osc_set_wave(&switched, SYNTH_WAVE_VOSIM);

    CHECK(switched.pulse_rate == direct.pulse_rate);
    CHECK(switched.vosim_dc == direct.vosim_dc);
    CHECK(switched.vosim_fitting == direct.vosim_fitting);

    /* And the knee, which is widened by the pitch rather than taken as asked. */
    synth_osc_init(&direct, SR);
    synth_osc_set_wave(&direct, SYNTH_WAVE_PD);
    synth_osc_set_pd_knee(&direct, 0.06f);
    synth_osc_set_freq(&direct, 1500.0f);

    synth_osc_init(&switched, SR);
    synth_osc_set_wave(&switched, SYNTH_WAVE_PD);
    synth_osc_set_pd_knee(&switched, 0.5f);
    synth_osc_set_freq(&switched, 110.0f);
    synth_osc_set_wave(&switched, SYNTH_WAVE_SAW);
    synth_osc_set_pd_knee(&switched, 0.06f);
    synth_osc_set_freq(&switched, 1500.0f);
    synth_osc_set_wave(&switched, SYNTH_WAVE_PD);

    CHECK(switched.pd_knee == direct.pd_knee);
    CHECK(switched.pd_rise == direct.pd_rise);
    CHECK(switched.pd_fall == direct.pd_fall);
    CHECK(switched.pd_scale == direct.pd_scale);
    CHECK(switched.pd_offset == direct.pd_offset);
    CHECK(direct.pd_knee > 0.06f); /* the pitch really did widen it */
}

static void test_terrain_is_centred_and_bounded(void)
{
    static float buf[ALIAS_FRAMES];
    synth_osc_t osc;
    float radii[] = { 0.1f, 0.4f, 0.7f, 1.0f };
    int ratios[] = { 1, 2, 3 };
    int r, q, i;

    /* An arbitrary surface has no reason to be centred or to peak at 1, and
       both depend on the orbit, so the oscillator measures one lap at setup. */
    for (r = 0; r < 4; ++r) {
        for (q = 0; q < 3; ++q) {
            int period = (int)(SR / 100.0f);
            int whole = (ALIAS_FRAMES / period) * period;
            float mean = 0.0f;
            float top = 0.0f;

            synth_osc_init(&osc, SR);
            synth_osc_set_wave(&osc, SYNTH_WAVE_TERRAIN);
            synth_osc_set_terrain(&osc, radii[r], ratios[q]);
            synth_osc_set_freq(&osc, 100.0f);
            for (i = 0; i < ALIAS_FRAMES; ++i) {
                buf[i] = synth_osc_next(&osc);
            }
            for (i = 0; i < whole; ++i) {
                mean += buf[i];
                if (fabsf(buf[i]) > top) {
                    top = fabsf(buf[i]);
                }
            }
            CHECK_NEAR(mean / (float)whole, 0.0f, 0.02f);
            CHECK(top <= 1.001f);
            CHECK(top > 0.5f); /* normalised to actually use the range */
        }
    }
}

static void test_terrain_radius_changes_the_spectrum(void)
{
    static float narrow[ALIAS_FRAMES];
    static float wide[ALIAS_FRAMES];
    synth_osc_t osc;
    float narrow_high = 0.0f, wide_high = 0.0f;
    int i;

    /* The same surface read on a wider orbit is a different waveform, not a
       louder one: the orbit radius is a timbre control. */
    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_TERRAIN);
    synth_osc_set_terrain(&osc, 0.15f, 1);
    synth_osc_set_freq(&osc, 200.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        narrow[i] = synth_osc_next(&osc);
    }

    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_TERRAIN);
    synth_osc_set_terrain(&osc, 0.95f, 1);
    synth_osc_set_freq(&osc, 200.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        wide[i] = synth_osc_next(&osc);
    }

    for (i = 2; i <= 8; ++i) {
        narrow_high += goertzel(narrow, ALIAS_FRAMES, 200.0f * (float)i);
        wide_high += goertzel(wide, ALIAS_FRAMES, 200.0f * (float)i);
    }
    CHECK(wide_high > narrow_high * 1.5f);
}

/* Every voice of an instance orbits the same cross-section, so deriving it once
   and copying it has to leave every voice with exactly what deriving it in place
   would have. Not "close": the same floats, or the voices are detuned from each
   other in a way no spectrum test would separate from the waveform itself. */
static void test_every_voice_shares_one_terrain_cross_section(void)
{
    synth_t s;
    synth_osc_t reference;
    float buf[64];
    int round, i;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_TERRAIN / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_osc_init(&reference, SR);

    /* Every slot sounding, because an idle one holds nothing: a parameter change
       reaches the voices that can be heard, and the note_on that claims a slot is
       what puts the current sound into it. */
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_note_on(&s, 48 + i, 0.9f);
    }
    synth_render(&s, buf, 64);

    /* Twice, with different values: a voice left holding the first answer would
       pass a single round. */
    for (round = 0; round < 2; ++round) {
        float radius_norm = round ? 0.25f : 0.85f;
        float ratio_norm = round ? 0.9f : 0.3f;

        synth_set_param(&s, SYNTH_PARAM_TERRAIN_RADIUS, radius_norm);
        synth_set_param(&s, SYNTH_PARAM_TERRAIN_RATIO, ratio_norm);

        /* Derived from the parameters, not from what the engine ended up with,
           so a stale cross-section cannot agree with itself. */
        synth_osc_set_terrain(&reference,
                              synth_param_denorm(SYNTH_PARAM_TERRAIN_RADIUS, radius_norm),
                              (int)synth_param_denorm(SYNTH_PARAM_TERRAIN_RATIO, ratio_norm));
        for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
            CHECK(s.voices[i].osc.terrain_radius == reference.terrain_radius);
            CHECK(s.voices[i].osc.terrain_ratio == reference.terrain_ratio);
            CHECK(s.voices[i].osc.terrain_scale == reference.terrain_scale);
            CHECK(s.voices[i].osc.terrain_dc == reference.terrain_dc);
        }
    }
}

static void test_terrain_ratio_changes_the_waveform(void)
{
    static float a[ALIAS_FRAMES];
    static float b[ALIAS_FRAMES];
    synth_osc_t osc;
    float diff = 0.0f;
    int i;

    /* A Lissajous orbit closes on a different path across the same surface. */
    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_TERRAIN);
    synth_osc_set_terrain(&osc, 0.8f, 1);
    synth_osc_set_freq(&osc, 200.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        a[i] = synth_osc_next(&osc);
    }

    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_TERRAIN);
    synth_osc_set_terrain(&osc, 0.8f, 3);
    synth_osc_set_freq(&osc, 200.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        b[i] = synth_osc_next(&osc);
    }

    for (i = 0; i < ALIAS_FRAMES; ++i) {
        diff += fabsf(a[i] - b[i]);
    }
    CHECK(diff / (float)ALIAS_FRAMES > 0.1f);
}

/* Cutoff a voice settles on, read back from the engine rather than guessed. */
static float voice_cutoff_octaves(const synth_t *s)
{
    return s->voices[0].filter.a2 / s->voices[0].filter.a1;
}

static void test_filter_env_opens_and_closes_the_filter(void)
{
    synth_t s;
    static float buf[512];
    float at_attack, at_sustain;

    /* A positive amount must open the filter at the peak of the envelope and
       let it fall back to the sustain level. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.25f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 1.0f);   /* +4 octaves */
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_ATTACK, 0.0f);   /* effectively instant */
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_DECAY, 0.3f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_SUSTAIN, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 48, 1.0f);

    render_seconds(&s, buf, 512, 0.02f); /* still near the peak of the envelope */
    at_attack = voice_cutoff_octaves(&s);
    render_seconds(&s, buf, 512, 1.5f);  /* decayed all the way to sustain 0 */
    at_sustain = voice_cutoff_octaves(&s);

    CHECK(at_attack > at_sustain * 2.0f);
}

static void test_filter_env_amount_is_bipolar(void)
{
    synth_t s;
    static float buf[512];
    float up, down;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.4f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 1.0f);
    synth_note_on(&s, 60, 1.0f);
    render_seconds(&s, buf, 512, 0.2f);
    up = voice_cutoff_octaves(&s);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.4f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.0f); /* -4 octaves */
    synth_note_on(&s, 60, 1.0f);
    render_seconds(&s, buf, 512, 0.2f);
    down = voice_cutoff_octaves(&s);

    CHECK(up > down * 4.0f);
}

static void test_filter_key_track_follows_pitch(void)
{
    synth_t s;
    static float buf[512];
    float low, high, flat_low, flat_high;

    /* With full tracking the cutoff should climb an octave when the note does. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.3f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.5f); /* neutral */
    synth_set_param(&s, SYNTH_PARAM_FILTER_KEY_TRACK, 1.0f);
    synth_note_on(&s, 48, 1.0f);
    render_seconds(&s, buf, 512, 0.05f);
    low = voice_cutoff_octaves(&s);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.3f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_KEY_TRACK, 1.0f);
    synth_note_on(&s, 60, 1.0f);
    render_seconds(&s, buf, 512, 0.05f);
    high = voice_cutoff_octaves(&s);

    CHECK(high > low * 1.5f);

    /* With tracking off the note must make no difference at all. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.3f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_KEY_TRACK, 0.0f);
    synth_note_on(&s, 48, 1.0f);
    render_seconds(&s, buf, 512, 0.05f);
    flat_low = voice_cutoff_octaves(&s);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.3f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_KEY_TRACK, 0.0f);
    synth_note_on(&s, 72, 1.0f);
    render_seconds(&s, buf, 512, 0.05f);
    flat_high = voice_cutoff_octaves(&s);

    CHECK_NEAR(flat_low, flat_high, flat_low * 0.01f);
}

static void test_key_track_survives_note_off(void)
{
    synth_t s;
    static float buf[512];
    float held_cutoff, released_cutoff;

    /* The release still belongs to the note that was played, so the pitch a
       voice tracks must outlive the key being let go. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.3f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_KEY_TRACK, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_RELEASE, 0.6f);
    synth_note_on(&s, 84, 1.0f);
    render_seconds(&s, buf, 512, 0.1f);
    held_cutoff = voice_cutoff_octaves(&s);

    synth_note_off(&s, 84);
    render_seconds(&s, buf, 512, 0.1f);
    released_cutoff = voice_cutoff_octaves(&s);

    CHECK(synth_active_voices(&s) == 1);
    CHECK_NEAR(held_cutoff, released_cutoff, held_cutoff * 0.01f);
}

static void test_amp_shape_is_clean_at_zero_drive(void)
{
    int i;

    for (i = -100; i <= 100; ++i) {
        float x = (float)i / 100.0f;
        CHECK_NEAR(synth_amp_shape(x, 0.0f), x, 1e-6f);
    }
}

static void test_amp_shape_stays_bounded_and_monotone(void)
{
    float drives[] = { 0.0f, 0.5f, 3.0f, 12.0f, 100.0f };
    int d, i;

    for (d = 0; d < 5; ++d) {
        float previous = -2.0f;

        /* Full scale in must stay full scale out however hard it is driven,
           and the curve must never fold back on itself. */
        CHECK_NEAR(synth_amp_shape(1.0f, drives[d]), 1.0f, 1e-6f);
        CHECK_NEAR(synth_amp_shape(-1.0f, drives[d]), -1.0f, 1e-6f);
        CHECK_NEAR(synth_amp_shape(0.0f, drives[d]), 0.0f, 1e-9f);

        for (i = -200; i <= 200; ++i) {
            float y = synth_amp_shape((float)i / 200.0f, drives[d]);

            CHECK(fabsf(y) <= 1.0f + 1e-6f);
            CHECK(y > previous);
            previous = y;
        }
    }
}

static void test_amp_drive_adds_odd_harmonics_only(void)
{
    static float clean[ALIAS_FRAMES];
    static float driven[ALIAS_FRAMES];
    synth_osc_t osc;
    float third_clean, third_driven, second_driven, fundamental;
    int i;

    synth_osc_init(&osc, SR);
    synth_osc_set_freq(&osc, 500.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        float s = synth_osc_next(&osc);
        clean[i] = s;
        driven[i] = synth_amp_shape(s, 8.0f);
    }

    fundamental = goertzel(driven, ALIAS_FRAMES, 500.0f);
    second_driven = goertzel(driven, ALIAS_FRAMES, 1000.0f);
    third_clean = goertzel(clean, ALIAS_FRAMES, 1500.0f);
    third_driven = goertzel(driven, ALIAS_FRAMES, 1500.0f);

    CHECK(third_driven > third_clean * 20.0f);
    /* The curve is odd, so it cannot produce even harmonics. */
    CHECK(second_driven < fundamental * 0.01f);
}

static void test_amp_gates_regardless_of_drive(void)
{
    synth_amp_t amp;

    synth_amp_init(&amp);
    amp.drive = 12.0f;
    amp.level = 1.0f;

    /* A closed envelope must mean silence, not a quiet distorted signal. */
    CHECK_NEAR(synth_amp_next(&amp, 1.0f, 0.0f), 0.0f, 1e-9f);
    CHECK_NEAR(synth_amp_next(&amp, -1.0f, 0.0f), 0.0f, 1e-9f);
    CHECK(synth_amp_next(&amp, 1.0f, 1.0f) > 0.5f);
}

static void test_amp_velocity_sensitivity(void)
{
    synth_t s;
    static float soft[4096];
    static float hard[4096];

    /* At full sensitivity a light touch is quieter. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_VELOCITY, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 60, 0.25f);
    synth_render(&s, soft, 4096);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_VELOCITY, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, hard, 4096);
    CHECK(peak(soft, 4096) < peak(hard, 4096) * 0.5f);

    /* With sensitivity off the same two touches must be indistinguishable. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_VELOCITY, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 60, 0.25f);
    synth_render(&s, soft, 4096);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_VELOCITY, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, hard, 4096);
    CHECK(memcmp(soft, hard, sizeof(soft)) == 0);
}

static void test_amp_sits_after_the_filter(void)
{
    synth_t s;
    static float clean[8192];
    static float driven[8192];
    float clean_high = 0.0f, driven_high = 0.0f;
    int i;

    /* With the filter shut down hard, drive can only put high harmonics back if
       it runs after the filter. This is what fixes the chain order in place. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.12f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_DRIVE, 0.0f);
    synth_note_on(&s, 40, 1.0f);
    synth_render(&s, clean, 8192);
    synth_render(&s, clean, 8192);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.12f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_DRIVE, 1.0f);
    synth_note_on(&s, 40, 1.0f);
    synth_render(&s, driven, 8192);
    synth_render(&s, driven, 8192);

    for (i = 5; i <= 15; ++i) {
        float hz = synth_note_to_hz(40.0f) * (float)i;
        clean_high += goertzel(clean, 8192, hz);
        driven_high += goertzel(driven, 8192, hz);
    }
    CHECK(driven_high > clean_high * 2.0f);
}

static void test_new_waves_reach_the_engine(void)
{
    synth_t s;
    static float buf[4096];
    float wave_norm;

    /* osc_wave is a stepped parameter, so the two new modes are addressable
       from a host without any extra API. */
    wave_norm = (float)SYNTH_WAVE_PD / (float)(SYNTH_WAVE_COUNT - 1);
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, wave_norm);
    synth_set_param(&s, SYNTH_PARAM_PD_KNEE, 0.9f);
    synth_note_on(&s, 57, 1.0f);
    synth_render(&s, buf, 4096);
    CHECK(peak(buf, 4096) > 0.05f);

    wave_norm = (float)SYNTH_WAVE_VOSIM / (float)(SYNTH_WAVE_COUNT - 1);
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, wave_norm);
    synth_note_on(&s, 45, 1.0f);
    synth_render(&s, buf, 4096);
    CHECK(peak(buf, 4096) > 0.05f);
}

/* Loudest thing below the fundamental. A band-limited periodic waveform has
   nothing there at all, so whatever turns up is aliasing folded down, measured
   against a sine whose only content is its own fundamental. */
#define SPECTRUM_FRAMES 16384

static float subharmonic_floor(synth_wave_t wave, float f0, float knee)
{
    static float buf[SPECTRUM_FRAMES];
    synth_osc_t osc;
    float worst = 0.0f;
    float hz;
    int i;

    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, wave);
    synth_osc_set_pd_knee(&osc, knee);
    synth_osc_set_vosim(&osc, 3000.0f, 3, 0.7f);
    synth_osc_set_terrain(&osc, 0.9f, 3);
    synth_osc_set_freq(&osc, f0);
    for (i = 0; i < SPECTRUM_FRAMES; ++i) {
        buf[i] = synth_osc_next(&osc);
    }

    /* A long window matters: the probe's own leakage falls from 0.015 of the
       fundamental at 4096 samples to 0.003 at 16384, and a floor that high
       would hide the aliasing this is looking for. */
    for (hz = 120.0f; hz < f0 - 150.0f; hz += 60.0f) {
        float m = goertzel(buf, SPECTRUM_FRAMES, hz);

        if (m > worst) {
            worst = m;
        }
    }
    return worst / goertzel(buf, SPECTRUM_FRAMES, f0);
}

static void test_no_waveform_aliases_below_its_fundamental(void)
{
    static const synth_wave_t periodic[] = {
        SYNTH_WAVE_SINE, SYNTH_WAVE_SAW, SYNTH_WAVE_SQUARE,
        SYNTH_WAVE_VOSIM, SYNTH_WAVE_TERRAIN
    };
    float sine_floor = subharmonic_floor(SYNTH_WAVE_SINE, 1500.0f, 0.5f);
    int i;

    /* The sine cannot alias, so whatever it measures is the probe's own floor
       and nothing else may sit far above it. Noise is left out: it is broadband
       on purpose and has no harmonic series to be below. */
    CHECK(sine_floor < 0.005f);

    for (i = 0; i < (int)(sizeof(periodic) / sizeof(periodic[0])); ++i) {
        CHECK(subharmonic_floor(periodic[i], 1500.0f, 0.5f) < sine_floor * 3.0f);
        CHECK(subharmonic_floor(periodic[i], 3000.0f, 0.5f) < sine_floor * 3.0f);
    }
}

static void test_phase_distortion_stays_band_limited_up_high(void)
{
    float sine_floor = subharmonic_floor(SYNTH_WAVE_SINE, 1500.0f, 0.5f);

    /* The tightest knee is exactly where this used to fall apart: the fast
       segment crossed half a sine in under two samples, which no amount of
       correction at the corner could represent. The knee is widened with pitch
       instead, so the tone dulls rather than folding. */
    CHECK(subharmonic_floor(SYNTH_WAVE_PD, 440.0f, 0.02f) < sine_floor * 3.0f);
    CHECK(subharmonic_floor(SYNTH_WAVE_PD, 1500.0f, 0.02f) < sine_floor * 3.0f);
    CHECK(subharmonic_floor(SYNTH_WAVE_PD, 3000.0f, 0.02f) < sine_floor * 3.0f);
}

static void test_phase_distortion_still_bends_at_playable_pitches(void)
{
    static float bent[ALIAS_FRAMES];
    static float plain[ALIAS_FRAMES];
    synth_osc_t osc;
    int i;

    /* Widening the knee must not flatten the effect where it matters: at a bass
       note there is room for the tight knee the host asked for. */
    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_PD);
    synth_osc_set_pd_knee(&osc, 0.05f);
    synth_osc_set_freq(&osc, 110.0f);
    CHECK_NEAR(osc.pd_knee, 0.05f, 1e-6f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        bent[i] = synth_osc_next(&osc);
    }

    /* Against the same oscillator at a neutral knee, which is a plain sine, so
       the comparison is the effect itself rather than the probe's leakage. */
    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_PD);
    synth_osc_set_pd_knee(&osc, 0.5f);
    synth_osc_set_freq(&osc, 110.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        plain[i] = synth_osc_next(&osc);
    }

    CHECK(goertzel(bent, ALIAS_FRAMES, 880.0f) >
          goertzel(plain, ALIAS_FRAMES, 880.0f) * 5.0f);
}

static void test_oscillator_frequency(void)
{
    synth_osc_t osc;
    int crossings = 0;
    float prev = 0.0f;
    int i;

    synth_osc_init(&osc, SR);
    synth_osc_set_freq(&osc, 440.0f);

    for (i = 0; i < (int)SR; ++i) {
        float v = synth_osc_next(&osc);

        if (prev <= 0.0f && v > 0.0f) {
            ++crossings;
        }
        prev = v;
    }
    CHECK(crossings >= 439 && crossings <= 441);
}

/* Peak of a unit-amplitude sine after the filter has settled, i.e. the
   magnitude response at that frequency. */
static float filter_gain_at(float freq, float cutoff, float q, synth_filter_mode_t mode)
{
    synth_filter_t filter;
    synth_osc_t osc;
    float p = 0.0f;
    int i;

    synth_filter_init(&filter, SR);
    synth_filter_set(&filter, cutoff, q);
    synth_osc_init(&osc, SR);
    synth_osc_set_freq(&osc, freq);

    for (i = 0; i < (int)(SR * 0.2f); ++i) {
        synth_filter_next(&filter, synth_osc_next(&osc), mode);
    }
    for (i = 0; i < (int)(SR * 0.1f); ++i) {
        float v = fabsf(synth_filter_next(&filter, synth_osc_next(&osc), mode));
        if (v > p) {
            p = v;
        }
    }
    return p;
}

static void test_filter_lowpass_response(void)
{
    CHECK_NEAR(filter_gain_at(100.0f, 5000.0f, 0.707f, SYNTH_FILTER_LOWPASS), 1.0f, 0.05f);
    /* Butterworth Q, so the cutoff is the -3 dB point. */
    CHECK_NEAR(filter_gain_at(1000.0f, 1000.0f, 0.707f, SYNTH_FILTER_LOWPASS), 0.7071f, 0.01f);
    /* Two poles: an octave up should cost about 12 dB. */
    CHECK_NEAR(filter_gain_at(2000.0f, 1000.0f, 0.707f, SYNTH_FILTER_LOWPASS), 0.243f, 0.02f);
    CHECK(filter_gain_at(8000.0f, 400.0f, 0.707f, SYNTH_FILTER_LOWPASS) < 0.02f);

    /* The -3 dB point must land on the requested cutoff across the whole range,
       which is what the tan approximation in the prewarping has to get right. */
    CHECK_NEAR(filter_gain_at(50.0f, 50.0f, 0.707f, SYNTH_FILTER_LOWPASS), 0.7071f, 0.01f);
    CHECK_NEAR(filter_gain_at(15000.0f, 15000.0f, 0.707f, SYNTH_FILTER_LOWPASS), 0.7071f, 0.02f);
}

static void test_filter_highpass_response(void)
{
    CHECK_NEAR(filter_gain_at(8000.0f, 400.0f, 0.707f, SYNTH_FILTER_HIGHPASS), 1.0f, 0.05f);
    CHECK(filter_gain_at(100.0f, 5000.0f, 0.707f, SYNTH_FILTER_HIGHPASS) < 0.02f);
}

static void test_filter_bandpass_response(void)
{
    float centre = filter_gain_at(1000.0f, 1000.0f, 4.0f, SYNTH_FILTER_BANDPASS);

    CHECK(centre > filter_gain_at(100.0f, 1000.0f, 4.0f, SYNTH_FILTER_BANDPASS) * 4.0f);
    CHECK(centre > filter_gain_at(9000.0f, 1000.0f, 4.0f, SYNTH_FILTER_BANDPASS) * 4.0f);
}

static void test_filter_resonance(void)
{
    float flat = filter_gain_at(1000.0f, 1000.0f, 0.707f, SYNTH_FILTER_LOWPASS);
    float resonant = filter_gain_at(1000.0f, 1000.0f, 10.0f, SYNTH_FILTER_LOWPASS);

    CHECK(resonant > flat * 4.0f);
}

static void test_filter_is_stable_at_extremes(void)
{
    synth_filter_t filter;
    synth_osc_t osc;
    int i;

    synth_filter_init(&filter, SR);
    synth_filter_set(&filter, 1.0e6f, 100.0f); /* both clamped internally */
    synth_osc_init(&osc, SR);
    synth_osc_set_freq(&osc, 3000.0f);

    for (i = 0; i < (int)(SR * 2.0f); ++i) {
        float v = synth_filter_next(&filter, synth_osc_next(&osc), SYNTH_FILTER_LOWPASS);
        CHECK(isfinite(v));
        if (!isfinite(v)) {
            break;
        }
    }

    synth_filter_set(&filter, 0.0f, 0.0f);
    for (i = 0; i < 1024; ++i) {
        CHECK(isfinite(synth_filter_next(&filter, 1.0f, SYNTH_FILTER_LOWPASS)));
    }
}

static void test_filter_reset_clears_state(void)
{
    synth_filter_t filter;
    int i;

    synth_filter_init(&filter, SR);
    synth_filter_set(&filter, 800.0f, 8.0f);
    for (i = 0; i < 256; ++i) {
        synth_filter_next(&filter, 1.0f, SYNTH_FILTER_LOWPASS);
    }
    CHECK(filter.ic1eq != 0.0f || filter.ic2eq != 0.0f);

    synth_filter_reset(&filter);
    CHECK(filter.ic1eq == 0.0f);
    CHECK(filter.ic2eq == 0.0f);
    CHECK_NEAR(synth_filter_next(&filter, 0.0f, SYNTH_FILTER_LOWPASS), 0.0f, 1e-9f);
}

static void test_filter_shapes_engine_output(void)
{
    synth_t open, closed;
    float buf_open[8192], buf_closed[8192];

    synth_init(&open, SR);
    synth_init(&closed, SR);
    synth_set_param(&open, SYNTH_PARAM_OSC_WAVE, 1.0f);
    synth_set_param(&closed, SYNTH_PARAM_OSC_WAVE, 1.0f);
    synth_set_param(&open, SYNTH_PARAM_FILTER_CUTOFF, 1.0f);
    synth_set_param(&closed, SYNTH_PARAM_FILTER_CUTOFF, 0.05f);

    synth_note_on(&open, 72, 1.0f);
    synth_note_on(&closed, 72, 1.0f);
    synth_render(&open, buf_open, 8192);
    synth_render(&closed, buf_closed, 8192);

    CHECK(peak(buf_closed, 8192) < peak(buf_open, 8192) * 0.5f);
}

static void test_silence_when_idle(void)
{
    synth_t s;
    float buf[256];
    int i;

    synth_init(&s, SR);
    synth_render(&s, buf, 256);

    for (i = 0; i < 256; ++i) {
        CHECK(buf[i] == 0.0f);
    }
    CHECK(synth_active_voices(&s) == 0);
}

static void test_note_on_makes_sound(void)
{
    synth_t s;
    float buf[512];
    float loudest = 0.0f;
    int block;

    synth_init(&s, SR);
    synth_note_on(&s, 60, 1.0f);
    CHECK(synth_active_voices(&s) == 1);

    for (block = 0; block < 16; ++block) {
        float p;

        synth_render(&s, buf, 512);
        p = peak(buf, 512);
        if (p > loudest) {
            loudest = p;
        }
    }
    CHECK(loudest > 0.1f);
}

static void test_release_returns_to_silence(void)
{
    synth_t s;
    float buf[512];

    synth_init(&s, SR);
    synth_note_on(&s, 64, 1.0f);
    render_seconds(&s, buf, 512, 0.5f);

    synth_note_off(&s, 64);
    render_seconds(&s, buf, 512, 1.5f);

    CHECK(synth_active_voices(&s) == 0);
    synth_render(&s, buf, 512);
    CHECK(peak(buf, 512) == 0.0f);
}

static void test_polyphony_and_stealing(void)
{
    synth_t s;
    float buf[64];
    int i;

    synth_init(&s, SR);
    for (i = 0; i < SYNTH_MAX_VOICES + 3; ++i) {
        synth_note_on(&s, 48 + i, 1.0f);
        synth_render(&s, buf, 64);
    }
    CHECK(synth_active_voices(&s) == SYNTH_MAX_VOICES);
}

static void test_retrigger_reuses_voice(void)
{
    synth_t s;
    float buf[64];

    synth_init(&s, SR);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, buf, 64);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, buf, 64);

    CHECK(synth_active_voices(&s) == 1);
}

static void test_all_notes_off(void)
{
    synth_t s;
    float buf[64];
    int i;

    synth_init(&s, SR);
    for (i = 0; i < 4; ++i) {
        synth_note_on(&s, 60 + i, 1.0f);
    }
    synth_render(&s, buf, 64);
    CHECK(synth_active_voices(&s) == at_most_pool(4));

    synth_all_notes_off(&s);
    render_seconds(&s, buf, 64, 1.5f);
    CHECK(synth_active_voices(&s) == 0);
}

static void test_sample_rate_change_keeps_params_and_tuning(void)
{
    synth_t s;
    float buf[8192];
    int crossings = 0;
    float prev = 0.0f;
    int i;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_MASTER_GAIN, 0.33f);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, buf, 64);

    synth_set_sample_rate(&s, 16000.0f);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 0.33f, 1e-6f);
    CHECK(synth_active_voices(&s) == 0);

    /* A note at the new rate must still land on A4 = 440 Hz. */
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 1.0f);
    synth_note_on(&s, 69, 1.0f);
    synth_render(&s, buf, 8192);
    synth_render(&s, buf, 8192);

    for (i = 0; i < 8000; ++i) {
        if (prev <= 0.0f && buf[i] > 0.0f) {
            ++crossings;
        }
        prev = buf[i];
    }
    CHECK(crossings >= 217 && crossings <= 223); /* 440 Hz over half a second at 16 kHz */
}

static void test_output_is_finite_and_bounded(void)
{
    synth_t s;
    float buf[256];
    int i, block;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, 1.0f);
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_note_on(&s, 36 + i * 5, 1.0f);
    }

    for (block = 0; block < 64; ++block) {
        synth_render(&s, buf, 256);
        for (i = 0; i < 256; ++i) {
            CHECK(isfinite(buf[i]));
            CHECK(fabsf(buf[i]) <= (float)SYNTH_MAX_VOICES);
        }
    }
}

static void test_render_is_deterministic(void)
{
    synth_t a, b;
    float buf_a[1024], buf_b[1024];

    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_note_on(&a, 67, 0.8f);
    synth_note_on(&b, 67, 0.8f);
    synth_render(&a, buf_a, 1024);
    synth_render(&b, buf_b, 1024);

    CHECK(memcmp(buf_a, buf_b, sizeof(buf_a)) == 0);
}

static void test_param_mapping(void)
{
    synth_t s;

    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_MASTER_GAIN, 0.0f), 0.0f, 1e-6f);
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_MASTER_GAIN, 1.0f), 1.0f, 1e-6f);
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_MASTER_GAIN, 0.5f), 0.5f, 1e-6f);

    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_OSC_WAVE, 0.0f), 0.0f, 1e-6f);
    {
        /* A stepped parameter always lands on a whole step, and the midpoint
           lands within half a step of the middle whatever the wave count. */
        float mid = synth_param_denorm(SYNTH_PARAM_OSC_WAVE, 0.5f);

        CHECK(mid == (float)(int)mid);
        CHECK(fabsf(mid - 0.5f * (float)(SYNTH_WAVE_COUNT - 1)) <= 0.5f);
    }
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_OSC_WAVE, 1.0f),
               (float)(SYNTH_WAVE_COUNT - 1), 1e-6f);

    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_AMP_ATTACK, 0.0f), 0.001f, 1e-6f);
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_AMP_ATTACK, 1.0f), 8.0f, 1e-5f);
    CHECK(synth_param_denorm(SYNTH_PARAM_AMP_ATTACK, 0.5f) < 1.5f);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_MASTER_GAIN, 0.25f);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 0.25f, 1e-6f);
    synth_set_param(&s, SYNTH_PARAM_MASTER_GAIN, 4.0f);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 1.0f, 1e-6f);

    CHECK(synth_param_info(SYNTH_PARAM_COUNT) == 0);
    CHECK(synth_param_info(SYNTH_PARAM_MASTER_GAIN) != 0);
}

static void test_gain_scales_output(void)
{
    synth_t loud, quiet;
    float buf_loud[2048], buf_quiet[2048];

    synth_init(&loud, SR);
    synth_init(&quiet, SR);
    synth_set_param(&loud, SYNTH_PARAM_MASTER_GAIN, 1.0f);
    synth_set_param(&quiet, SYNTH_PARAM_MASTER_GAIN, 0.5f);
    synth_note_on(&loud, 60, 1.0f);
    synth_note_on(&quiet, 60, 1.0f);
    synth_render(&loud, buf_loud, 2048);
    synth_render(&quiet, buf_quiet, 2048);

    CHECK_NEAR(peak(buf_quiet, 2048), peak(buf_loud, 2048) * 0.5f, 1e-5f);
}

static void test_envelope_stages(void)
{
    synth_env_t env;
    int i;

    synth_env_init(&env, SR);
    /* Through the setter, not into the fields: each ramp keeps how far it
       travels per sample, and that is derived from the times here. */
    synth_env_set_times(&env, 0.0f, 0.1f, 0.0f, 0.1f, 0.4f, 0.1f);

    CHECK(!synth_env_is_active(&env));
    synth_env_gate_on(&env);
    CHECK(synth_env_is_active(&env));

    for (i = 0; i < (int)(SR * 0.1f); ++i) {
        synth_env_next(&env);
    }
    CHECK_NEAR(env.level, 1.0f, 0.01f);

    for (i = 0; i < (int)(SR * 0.15f); ++i) {
        synth_env_next(&env);
    }
    CHECK_NEAR(env.level, 0.4f, 0.01f);

    synth_env_gate_off(&env);
    for (i = 0; i < (int)(SR * 0.11f); ++i) {
        synth_env_next(&env);
    }
    CHECK(!synth_env_is_active(&env));
    CHECK_NEAR(env.level, 0.0f, 1e-6f);
}

/* Feeds a byte string and returns how many messages came out. */
static int feed_bytes(synth_midi_t *midi, synth_t *s, const unsigned char *bytes, int n)
{
    int emitted = 0;
    int i;

    for (i = 0; i < n; ++i) {
        emitted += synth_midi_feed(midi, s, bytes[i]);
    }
    return emitted;
}

static void test_midi_notes(void)
{
    static const unsigned char on[] = { 0x90, 60, 100 };
    static const unsigned char off[] = { 0x80, 60, 0 };
    synth_midi_t midi;
    synth_t s;
    float buf[64];

    synth_midi_init(&midi);
    synth_init(&s, SR);

    CHECK(feed_bytes(&midi, &s, on, 3) == 1);
    CHECK(synth_active_voices(&s) == 1);
    synth_render(&s, buf, 64);

    CHECK(feed_bytes(&midi, &s, off, 3) == 1);
    render_seconds(&s, buf, 64, 1.5f);
    CHECK(synth_active_voices(&s) == 0);
}

static void test_midi_note_on_at_zero_velocity_is_a_note_off(void)
{
    static const unsigned char stream[] = { 0x90, 64, 100, 0x90, 64, 0 };
    synth_midi_t midi;
    synth_t s;
    float buf[64];

    /* Keyboards send note off this way so a whole phrase can ride one status
       byte, so the parser has to treat it as a note off, not a silent note. */
    synth_midi_init(&midi);
    synth_init(&s, SR);
    CHECK(feed_bytes(&midi, &s, stream, 6) == 2);
    render_seconds(&s, buf, 64, 1.5f);
    CHECK(synth_active_voices(&s) == 0);
}

static void test_midi_running_status(void)
{
    static const unsigned char stream[] = { 0x90, 60, 100, 64, 100, 67, 100 };
    synth_midi_t midi;
    synth_t s;
    float buf[64];

    /* One status byte followed by three note pairs. */
    synth_midi_init(&midi);
    synth_init(&s, SR);
    CHECK(feed_bytes(&midi, &s, stream, 7) == 3);
    synth_render(&s, buf, 64);
    CHECK(synth_active_voices(&s) == at_most_pool(3));
}

static void test_midi_realtime_bytes_do_not_break_a_message(void)
{
    static const unsigned char stream[] = { 0x90, 0xF8, 60, 0xFE, 100, 0xF8, 64, 100 };
    synth_midi_t midi;
    synth_t s;
    float buf[64];

    /* Clock and active-sensing bytes are allowed to land between the bytes of
       another message, and must leave the parse and running status untouched. */
    synth_midi_init(&midi);
    synth_init(&s, SR);
    CHECK(feed_bytes(&midi, &s, stream, 8) == 2);
    synth_render(&s, buf, 64);
    CHECK(synth_active_voices(&s) == at_most_pool(2));
}

static void test_midi_sysex_is_skipped_and_cancels_running_status(void)
{
    static const unsigned char stream[] = {
        0x90, 60, 100,              /* a note, establishing running status */
        0xF0, 0x7D, 1, 2, 3, 0xF7,  /* sysex, which must produce nothing */
        62, 100                     /* orphan data: running status is gone */
    };
    synth_midi_t midi;
    synth_t s;
    float buf[64];

    synth_midi_init(&midi);
    synth_init(&s, SR);
    CHECK(feed_bytes(&midi, &s, stream, 11) == 1);
    synth_render(&s, buf, 64);
    CHECK(synth_active_voices(&s) == 1);
}

static void test_midi_ignores_orphan_data_and_unhandled_types(void)
{
    static const unsigned char orphans[] = { 60, 100, 40 };
    static const unsigned char program[] = { 0xC0, 5 };
    static const unsigned char aftertouch[] = { 0xD0, 64 };
    synth_midi_t midi;
    synth_t s;

    synth_midi_init(&midi);
    synth_init(&s, SR);
    CHECK(feed_bytes(&midi, &s, orphans, 3) == 0);
    CHECK(feed_bytes(&midi, &s, program, 2) == 0);
    CHECK(feed_bytes(&midi, &s, aftertouch, 2) == 0);
    CHECK(synth_active_voices(&s) == 0);
}

static void test_midi_control_change_moves_a_parameter(void)
{
    synth_midi_t midi;
    synth_t s;
    unsigned char cc[3];

    synth_midi_init(&midi);
    synth_init(&s, SR);

    /* The default map puts the parameters on controllers from 20 upwards. */
    cc[0] = 0xB0;
    cc[1] = (unsigned char)(20 + SYNTH_PARAM_MASTER_GAIN);
    cc[2] = 127;
    CHECK(feed_bytes(&midi, &s, cc, 3) == 1);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 1.0f, 1e-6f);

    cc[2] = 0;
    feed_bytes(&midi, &s, cc, 3);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 0.0f, 1e-6f);

    /* Remapping moves the parameter a controller drives. */
    synth_midi_clear_map(&midi);
    synth_midi_map_cc(&midi, 7, SYNTH_PARAM_FILTER_Q);
    cc[1] = 7;
    cc[2] = 127;
    feed_bytes(&midi, &s, cc, 3);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_FILTER_Q), 1.0f, 1e-6f);

    /* An unmapped controller must do nothing at all. */
    cc[1] = 9;
    cc[2] = 127;
    feed_bytes(&midi, &s, cc, 3);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 0.0f, 1e-6f);
}

static void test_midi_all_notes_off_works_whatever_the_map(void)
{
    static const unsigned char notes[] = { 0x90, 60, 100, 64, 100 };
    static const unsigned char panic[] = { 0xB0, 123, 0 };
    synth_midi_t midi;
    synth_t s;
    float buf[64];

    synth_midi_init(&midi);
    synth_midi_clear_map(&midi);
    synth_init(&s, SR);
    feed_bytes(&midi, &s, notes, 5);
    synth_render(&s, buf, 64);
    CHECK(synth_active_voices(&s) == at_most_pool(2));

    feed_bytes(&midi, &s, panic, 3);
    render_seconds(&s, buf, 64, 1.5f);
    CHECK(synth_active_voices(&s) == 0);
}

/* Frequency of a single sounding voice, by counting zero crossings. */
static float voice_frequency(synth_t *s, float seconds)
{
    static float buf[4096];
    int total = (int)(SR * seconds);
    int crossings = 0;
    float prev = 0.0f;
    int done = 0;

    while (done < total) {
        int n = (total - done > 4096) ? 4096 : total - done;
        int i;

        synth_render(s, buf, n);
        for (i = 0; i < n; ++i) {
            if (prev <= 0.0f && buf[i] > 0.0f) {
                ++crossings;
            }
            prev = buf[i];
        }
        done += n;
    }
    return (float)crossings / seconds;
}

static void test_midi_pitch_bend(void)
{
    static const unsigned char note[] = { 0x90, 69, 100 };  /* A4, 440 Hz */
    static const unsigned char up[] = { 0xE0, 0x00, 0x7F }; /* full bend up */
    static const unsigned char centre[] = { 0xE0, 0x00, 0x40 };
    synth_midi_t midi;
    synth_t s;
    float plain, bent, restored;

    synth_midi_init(&midi);
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 1.0f);
    feed_bytes(&midi, &s, note, 3);
    plain = voice_frequency(&s, 0.5f);
    CHECK_NEAR(plain, 440.0f, 3.0f);

    /* The default range is two semitones, so a full bend reaches B4. */
    CHECK(feed_bytes(&midi, &s, up, 3) == 1);
    bent = voice_frequency(&s, 0.5f);
    CHECK_NEAR(bent, 493.88f, 5.0f);

    feed_bytes(&midi, &s, centre, 3);
    restored = voice_frequency(&s, 0.5f);
    CHECK_NEAR(restored, 440.0f, 3.0f);
}

static void test_pitch_bend_applies_to_later_notes(void)
{
    synth_t s;
    float held;

    /* A bend already in force has to reach notes started after it, or a new
       note in the middle of a bend would jump back to concert pitch. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 1.0f);
    synth_set_pitch_bend(&s, 12.0f);
    synth_note_on(&s, 57, 1.0f); /* A3 bent an octave up is A4 */
    held = voice_frequency(&s, 0.5f);
    CHECK_NEAR(held, 440.0f, 3.0f);
}

/* Index of the first sample that is audibly non-zero. Every waveform here
   starts at zero by construction, so a note's first sample is silent and onset
   lands one sample after the event. */
static int first_onset(const float *buf, int n)
{
    int i;

    for (i = 0; i < n; ++i) {
        if (fabsf(buf[i]) > 1e-6f) {
            return i;
        }
    }
    return -1;
}

static void schedule_note_on(synth_t *s, uint64_t frame, int note, float velocity)
{
    synth_event_t ev;

    ev.frame = frame;
    ev.type = SYNTH_EVENT_NOTE_ON;
    ev.index = note;
    ev.value = velocity;
    CHECK(synth_schedule(s, &ev) == 1);
}

static void test_frame_time_tracks_rendering(void)
{
    synth_t s;
    float buf[256];

    synth_init(&s, SR);
    CHECK(synth_frame_time(&s) == 0);
    synth_render(&s, buf, 256);
    CHECK(synth_frame_time(&s) == 256);
    synth_render(&s, buf, 100);
    CHECK(synth_frame_time(&s) == 356);

    /* An empty block must not move the clock. */
    synth_render(&s, buf, 0);
    CHECK(synth_frame_time(&s) == 356);
}

static void test_scheduled_note_lands_on_its_own_frame(void)
{
    static float buf[512];
    synth_t s;
    int onset;

    /* The whole point: a step in the middle of a buffer has to sound there,
       not at the boundary. With 96-frame bursts the boundary would be 2 ms out
       and the groove would audibly quantize to it. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 1.0f);
    schedule_note_on(&s, 200, 60, 1.0f);
    synth_render(&s, buf, 512);

    onset = first_onset(buf, 512);
    CHECK(onset >= 200 && onset <= 202);
}

static void test_several_events_split_one_block(void)
{
    static float buf[512];
    synth_t s;
    int i, voices_at_100, voices_at_400;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    schedule_note_on(&s, 100, 60, 1.0f);
    schedule_note_on(&s, 300, 64, 1.0f);
    schedule_note_on(&s, 450, 67, 1.0f);

    synth_render(&s, buf, 512);
    CHECK(synth_active_voices(&s) == at_most_pool(3));
    CHECK(first_onset(buf, 512) >= 100);

    /* And nothing sounded before the first event. */
    for (i = 0; i < 100; ++i) {
        CHECK(buf[i] == 0.0f);
    }

    /* Re-run counting how many voices exist part way through the block. */
    synth_init(&s, SR);
    schedule_note_on(&s, 100, 60, 1.0f);
    schedule_note_on(&s, 300, 64, 1.0f);
    synth_render(&s, buf, 200);
    voices_at_100 = synth_active_voices(&s);
    synth_render(&s, buf, 200);
    voices_at_400 = synth_active_voices(&s);
    CHECK(voices_at_100 == at_most_pool(1));
    CHECK(voices_at_400 == at_most_pool(2));
}

static void test_late_event_is_played_not_dropped(void)
{
    static float buf[256];
    synth_t s;
    synth_event_t ev;

    /* A step that arrives late recovers; a step that is silent does not. */
    synth_init(&s, SR);
    synth_render(&s, buf, 1000); /* clock is now well past the event's frame */

    ev.frame = 10;
    ev.type = SYNTH_EVENT_NOTE_ON;
    ev.index = 60;
    ev.value = 1.0f;
    CHECK(synth_schedule(&s, &ev) == 1);

    synth_render(&s, buf, 256);
    CHECK(synth_active_voices(&s) == 1);
    CHECK(first_onset(buf, 256) <= 2); /* at the very start of the next block */
}

static void test_queue_reports_when_full(void)
{
    synth_t s;
    synth_event_t ev;
    static float buf[64];
    int accepted = 0;
    int i;

    synth_init(&s, SR);
    ev.frame = 0; /* due at once, so a render drains them */
    ev.type = SYNTH_EVENT_NOTE_ON;
    ev.index = 60;
    ev.value = 1.0f;

    for (i = 0; i < SYNTH_EVENT_QUEUE_LEN + 10; ++i) {
        accepted += synth_schedule(&s, &ev);
    }
    /* A ring keeps one slot free to tell full from empty. */
    CHECK(accepted == SYNTH_EVENT_QUEUE_LEN - 1);

    /* Refusing must leave the queue usable rather than wedged, so draining it
       lets scheduling resume. */
    synth_render(&s, buf, 64);
    CHECK(synth_schedule(&s, &ev) == 1);
}

static void test_scheduled_param_and_all_notes_off(void)
{
    static float buf[512];
    synth_t s;
    synth_event_t ev;

    synth_init(&s, SR);
    ev.frame = 128;
    ev.type = SYNTH_EVENT_PARAM;
    ev.index = SYNTH_PARAM_MASTER_GAIN;
    ev.value = 0.25f;
    CHECK(synth_schedule(&s, &ev) == 1);

    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 0.8f, 1e-6f);
    synth_render(&s, buf, 64); /* not due yet */
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 0.8f, 1e-6f);
    synth_render(&s, buf, 512); /* now it is */
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_MASTER_GAIN), 0.25f, 1e-6f);

    schedule_note_on(&s, synth_frame_time(&s), 60, 1.0f);
    synth_render(&s, buf, 64);
    CHECK(synth_active_voices(&s) == 1);

    ev.frame = synth_frame_time(&s);
    ev.type = SYNTH_EVENT_ALL_NOTES_OFF;
    CHECK(synth_schedule(&s, &ev) == 1);
    render_seconds(&s, buf, 512, 1.5f);
    CHECK(synth_active_voices(&s) == 0);
}

static void test_splitting_a_block_does_not_change_the_audio(void)
{
    static float whole[512];
    static float split[512];
    synth_t a, b;

    /* Rendering 512 frames in one go and in two pieces must be identical, or
       the event splitting would colour every block that carries an event. */
    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&b, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_note_on(&a, 60, 1.0f);
    synth_note_on(&b, 60, 1.0f);

    synth_render(&a, whole, 512);
    synth_render(&b, split, 137);
    synth_render(&b, split + 137, 512 - 137);

    CHECK(memcmp(whole, split, sizeof(whole)) == 0);
}

static void test_events_are_applied_in_order(void)
{
    static float buf[256];
    synth_t s;
    synth_event_t ev;

    /* Same frame, note on then note off: the order they were scheduled in
       decides, and the note must end up off. */
    synth_init(&s, SR);
    ev.frame = 50;
    ev.type = SYNTH_EVENT_NOTE_ON;
    ev.index = 60;
    ev.value = 1.0f;
    CHECK(synth_schedule(&s, &ev) == 1);
    ev.type = SYNTH_EVENT_NOTE_OFF;
    CHECK(synth_schedule(&s, &ev) == 1);

    synth_render(&s, buf, 256);
    CHECK(!s.voices[0].held);
    render_seconds(&s, buf, 256, 1.5f);
    CHECK(synth_active_voices(&s) == 0);
}

/*
 * Several independent parts, each with its own patch and voices, is several
 * synth_t rather than a channel field. These pin down what that relies on.
 */

static void test_instances_do_not_share_parameters(void)
{
    synth_t a, b;

    synth_init(&a, SR);
    synth_init(&b, SR);

    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_VOSIM / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&a, SYNTH_PARAM_FILTER_Q, 1.0f);

    CHECK_NEAR(synth_get_param(&b, SYNTH_PARAM_OSC_WAVE), 0.0f, 1e-6f);
    CHECK_NEAR(synth_get_param(&b, SYNTH_PARAM_FILTER_Q), 0.0f, 1e-6f);
    CHECK(b.voices[0].osc.wave == SYNTH_WAVE_SINE);
}

static void test_instances_do_not_share_voices(void)
{
    synth_t a, b;
    static float buf[64];
    int i;

    /* Filling one part's polyphony must not steal from another's. */
    synth_init(&a, SR);
    synth_init(&b, SR);
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_note_on(&a, 40 + i, 1.0f);
    }
    synth_note_on(&b, 60, 1.0f);
    synth_render(&a, buf, 64);
    synth_render(&b, buf, 64);

    CHECK(synth_active_voices(&a) == SYNTH_MAX_VOICES);
    CHECK(synth_active_voices(&b) == 1);
    CHECK(b.voices[0].note == 60);
}

static void test_instances_keep_their_clocks_in_step(void)
{
    synth_t parts[4];
    static float buf[128];
    int i, block;

    /* A sequencer plans one set of absolute frames for every part, so parts
       rendered the same number of frames have to agree on what frame it is. */
    for (i = 0; i < 4; ++i) {
        synth_init(&parts[i], SR);
    }
    for (block = 0; block < 10; ++block) {
        for (i = 0; i < 4; ++i) {
            synth_render(&parts[i], buf, 128);
        }
    }
    for (i = 0; i < 4; ++i) {
        CHECK(synth_frame_time(&parts[i]) == 1280);
    }
}

static void test_a_late_instance_can_join_the_timeline(void)
{
    synth_t running, added;
    static float buf[512];
    synth_event_t ev;

    /* A part created mid-session starts at frame zero, so without this it would
       be numbering frames its neighbours passed long ago. */
    synth_init(&running, SR);
    synth_render(&running, buf, 4096);

    synth_init(&added, SR);
    synth_set_frame_time(&added, synth_frame_time(&running));
    CHECK(synth_frame_time(&added) == synth_frame_time(&running));

    /* And an event placed on the shared timeline now lands where it should
       rather than arriving four thousand frames late. */
    ev.frame = synth_frame_time(&added) + 200;
    ev.type = SYNTH_EVENT_NOTE_ON;
    ev.index = 60;
    ev.value = 1.0f;
    CHECK(synth_schedule(&added, &ev) == 1);
    synth_set_param(&added, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_render(&added, buf, 512);
    CHECK(first_onset(buf, 512) >= 200 && first_onset(buf, 512) <= 202);
}

static void test_a_new_instance_starts_its_clock_at_zero(void)
{
    synth_t running, added;
    static float buf[128];

    /* The consequence of the clock being per instance: a part created after
       the others begins at frame 0, not at their frame. A host adding a track
       mid-session has to account for that, which is why a groovebox is better
       off creating every part up front. */
    synth_init(&running, SR);
    synth_render(&running, buf, 4096);

    synth_init(&added, SR);
    CHECK(synth_frame_time(&running) == 4096);
    CHECK(synth_frame_time(&added) == 0);
}

static void test_instances_do_not_share_scheduled_events(void)
{
    synth_t a, b;
    static float buf_a[512];
    static float buf_b[512];
    synth_event_t ev;

    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&b, SYNTH_PARAM_OSC_WAVE, 1.0f / (float)(SYNTH_WAVE_COUNT - 1));

    ev.frame = 100;
    ev.type = SYNTH_EVENT_NOTE_ON;
    ev.index = 60;
    ev.value = 1.0f;
    CHECK(synth_schedule(&a, &ev) == 1);

    ev.frame = 300;
    ev.index = 67;
    CHECK(synth_schedule(&b, &ev) == 1);

    synth_render(&a, buf_a, 512);
    synth_render(&b, buf_b, 512);

    /* Each part heard only its own event, each on its own frame. */
    CHECK(synth_active_voices(&a) == 1);
    CHECK(synth_active_voices(&b) == 1);
    CHECK(a.voices[0].note == 60);
    CHECK(b.voices[0].note == 67);
    CHECK(first_onset(buf_a, 512) >= 100 && first_onset(buf_a, 512) <= 102);
    CHECK(first_onset(buf_b, 512) >= 300 && first_onset(buf_b, 512) <= 302);
}

static void test_summed_instances_need_host_headroom(void)
{
    synth_t parts[4];
    static float mix[2048];
    static float buf[2048];
    float peak_one = 0.0f;
    float peak_all = 0.0f;
    int i, frame;

    /* Each part is bounded by 1 on its own, so a host summing parts has to
       scale them: four parts in unison reach four. Nothing in the library can
       decide that budget, so it must be stated rather than discovered. */
    for (i = 0; i < 4; ++i) {
        synth_init(&parts[i], SR);
        synth_set_param(&parts[i], SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
        synth_set_param(&parts[i], SYNTH_PARAM_MASTER_GAIN, 1.0f);
        synth_note_on(&parts[i], 57, 1.0f);
    }

    for (frame = 0; frame < 2048; ++frame) {
        mix[frame] = 0.0f;
    }
    for (i = 0; i < 4; ++i) {
        synth_render(&parts[i], buf, 2048);
        for (frame = 0; frame < 2048; ++frame) {
            mix[frame] += buf[frame];
        }
        if (i == 0) {
            peak_one = peak(buf, 2048);
        }
    }
    peak_all = peak(mix, 2048);

    CHECK(peak_one <= 1.001f);
    CHECK(peak_all > peak_one * 3.0f);
}

static void test_noise_bandwidth_follows_the_note(void)
{
    static float low[ALIAS_FRAMES];
    static float high[ALIAS_FRAMES];
    float low_top = 0.0f, high_top = 0.0f;
    int i;

    /* Values are drawn once a cycle and interpolated across it, so the note
       sets how fast the noise moves: low rumbles, high hisses. */
    render_osc(low, 80.0f, SYNTH_WAVE_NOISE);
    render_osc(high, 6000.0f, SYNTH_WAVE_NOISE);

    for (i = 4; i <= 12; ++i) {
        low_top += goertzel(low, ALIAS_FRAMES, 1000.0f * (float)i);
        high_top += goertzel(high, ALIAS_FRAMES, 1000.0f * (float)i);
    }
    CHECK(high_top > low_top * 4.0f);
}

static void test_noise_is_interpolated_not_stepped(void)
{
    synth_osc_t osc;
    float previous;
    float biggest_step = 0.0f;
    int moved = 0;
    int i;

    /* Interpolating across the cycle rather than holding a value is what makes
       this the prototype's RedNoise instead of sample-and-hold: at a low note
       every step is tiny, where holding would sit still and then jump. */
    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_NOISE);
    synth_osc_set_freq(&osc, 80.0f);
    previous = synth_osc_next(&osc);

    for (i = 0; i < 4096; ++i) {
        float value = synth_osc_next(&osc);
        float step = fabsf(value - previous);

        if (step > biggest_step) {
            biggest_step = step;
        }
        if (step > 1e-9f) {
            ++moved;
        }
        previous = value;
    }

    /* A cycle at 80 Hz spans about 551 samples, so a full-scale swing crosses
       it in steps of roughly 2/551. Sample-and-hold would step by up to 2. */
    CHECK(biggest_step < 0.05f);
    CHECK(moved > 4000);
}

static void test_noise_is_centred_and_bounded(void)
{
    static float buf[ALIAS_FRAMES];
    float mean = 0.0f;
    int i;

    render_osc(buf, 3000.0f, SYNTH_WAVE_NOISE);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        mean += buf[i];
        CHECK(fabsf(buf[i]) <= 1.0f);
    }
    CHECK_NEAR(mean / (float)ALIAS_FRAMES, 0.0f, 0.05f);
}

static void test_noise_is_deterministic_but_differs_per_voice(void)
{
    synth_t a, b;
    static float buf_a[2048];
    static float buf_b[2048];
    synth_osc_t one, two;
    int i, same = 0;

    /* Two runs of the same patch must render identically, or nothing in this
       suite could compare buffers. */
    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_NOISE / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&b, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_NOISE / (float)(SYNTH_WAVE_COUNT - 1));
    synth_note_on(&a, 72, 1.0f);
    synth_note_on(&b, 72, 1.0f);
    synth_render(&a, buf_a, 2048);
    synth_render(&b, buf_b, 2048);
    CHECK(memcmp(buf_a, buf_b, sizeof(buf_a)) == 0);

    /* And the engine must hand every voice its own stream, or voices stacked on
       one chord would sum into a louder copy of the same noise rather than a
       thicker sound. Checked on the voices the engine actually built. */
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        int j;

        for (j = i + 1; j < SYNTH_MAX_VOICES; ++j) {
            if (a.voices[i].osc.noise_state == a.voices[j].osc.noise_state) {
                ++same;
            }
        }
    }
    CHECK(same == 0);

    (void)one;
    (void)two;
}

static void test_noise_seed_never_gets_stuck(void)
{
    synth_osc_t osc;
    float energy = 0.0f;
    int i;

    /* xorshift stays at zero forever if it ever reaches it, so a zero seed has
       to be refused rather than accepted into silence. */
    synth_osc_init(&osc, SR);
    synth_osc_set_wave(&osc, SYNTH_WAVE_NOISE);
    synth_osc_set_noise_seed(&osc, 0u);
    synth_osc_set_freq(&osc, 4000.0f);
    for (i = 0; i < 2048; ++i) {
        energy += fabsf(synth_osc_next(&osc));
    }
    CHECK(energy > 100.0f);
}

static void test_patch_round_trips(void)
{
    synth_t a, b;
    float patch[SYNTH_PARAM_COUNT];
    static float buf_a[2048];
    static float buf_b[2048];
    int i;

    synth_init(&a, SR);
    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_VOSIM / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&a, SYNTH_PARAM_FILTER_Q, 0.7f);
    synth_set_param(&a, SYNTH_PARAM_AMP_DRIVE, 0.4f);
    synth_set_param(&a, SYNTH_PARAM_VOSIM_FORMANT, 0.8f);
    synth_save_patch(&a, patch);

    synth_init(&b, SR);
    synth_load_patch(&b, patch);

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        CHECK_NEAR(synth_get_param(&b, (synth_param_t)i),
                   synth_get_param(&a, (synth_param_t)i), 1e-6f);
    }

    /* Restoring the numbers is only half of it: the voices have to be carrying
       the restored settings, which means the two render the same. */
    synth_note_on(&a, 55, 0.9f);
    synth_note_on(&b, 55, 0.9f);
    synth_render(&a, buf_a, 2048);
    synth_render(&b, buf_b, 2048);
    CHECK(memcmp(buf_a, buf_b, sizeof(buf_a)) == 0);
}

static void test_patch_load_clamps_and_survives_rubbish(void)
{
    synth_t s;
    float patch[SYNTH_PARAM_COUNT];
    static float buf[512];
    int i;

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        patch[i] = (i & 1) ? 9.0f : -9.0f;
    }
    synth_init(&s, SR);
    synth_load_patch(&s, patch);

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        float v = synth_get_param(&s, (synth_param_t)i);
        CHECK(v >= 0.0f && v <= 1.0f);
    }

    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, buf, 512);
    for (i = 0; i < 512; ++i) {
        CHECK(isfinite(buf[i]));
    }
}

static void test_patches_keep_instances_apart(void)
{
    synth_t a, b;
    float patch[SYNTH_PARAM_COUNT];

    /* The point of patches here: one per track, loaded into its own instance. */
    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_NOISE / (float)(SYNTH_WAVE_COUNT - 1));
    synth_save_patch(&a, patch);
    synth_load_patch(&b, patch);

    /* On the parameters, which is what a patch carries, and on the voice a note
       claims — not on an idle voice's oscillator, which holds nothing until a
       note_on rebuilds it. */
    CHECK_NEAR(synth_get_param(&b, SYNTH_PARAM_OSC_WAVE),
               synth_get_param(&a, SYNTH_PARAM_OSC_WAVE), 1e-6f);
    synth_note_on(&b, 60, 1.0f);
    CHECK(b.voices[0].osc.wave == SYNTH_WAVE_NOISE);

    synth_set_param(&b, SYNTH_PARAM_OSC_WAVE, 0.0f);
    synth_note_on(&a, 60, 1.0f);
    CHECK(a.voices[0].osc.wave == SYNTH_WAVE_NOISE);
}

/* A groovebox track is one instance, and the whole point of the mix is that
   nothing about a part changes because another part is playing beside it. */
static void test_render_add_mixes_instances(void)
{
    synth_t a, b;
    float solo_a[256], solo_b[256], mix[256];
    int i;

    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE, 0.0f);
    synth_set_param(&b, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_SAW / (float)(SYNTH_WAVE_COUNT - 1));
    synth_note_on(&a, 48, 1.0f);
    synth_note_on(&b, 67, 0.6f);
    synth_render(&a, solo_a, 256);
    synth_render(&b, solo_b, 256);

    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_set_param(&a, SYNTH_PARAM_OSC_WAVE, 0.0f);
    synth_set_param(&b, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_SAW / (float)(SYNTH_WAVE_COUNT - 1));
    synth_note_on(&a, 48, 1.0f);
    synth_note_on(&b, 67, 0.6f);
    synth_render(&a, mix, 256);
    synth_render_add(&b, mix, 256);

    /* Bit-identical, not near: mixing is an addition and nothing else, so a
       part that came out different came out wrong. */
    for (i = 0; i < 256; ++i) {
        if (mix[i] != solo_a[i] + solo_b[i]) {
            CHECK(mix[i] == solo_a[i] + solo_b[i]);
            break;
        }
    }
}

/* Events split the render at their frames, and each instance splits at its own.
   Two parts whose steps land on different frames must still mix exactly. */
static void test_render_add_mixes_across_event_splits(void)
{
    synth_t a, b;
    synth_event_t event;
    float solo_a[256], solo_b[256], mix[256];
    int pass, i;

    for (pass = 0; pass < 2; ++pass) {
        synth_init(&a, SR);
        synth_init(&b, SR);
        event.frame = 37;
        event.type = SYNTH_EVENT_NOTE_ON;
        event.index = 60;
        event.value = 1.0f;
        CHECK(synth_schedule(&a, &event));
        event.frame = 150;
        event.index = 64;
        CHECK(synth_schedule(&b, &event));

        if (pass == 0) {
            synth_render(&a, solo_a, 256);
            synth_render(&b, solo_b, 256);
        } else {
            synth_render(&a, mix, 256);
            synth_render_add(&b, mix, 256);
        }
    }

    for (i = 0; i < 256; ++i) {
        if (mix[i] != solo_a[i] + solo_b[i]) {
            CHECK(mix[i] == solo_a[i] + solo_b[i]);
            break;
        }
    }
}

static void test_render_add_keeps_what_is_already_in_the_buffer(void)
{
    synth_t s;
    float solo[128], buf[128];
    int i;

    synth_init(&s, SR);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, solo, 128);

    synth_init(&s, SR);
    synth_note_on(&s, 60, 1.0f);
    for (i = 0; i < 128; ++i) {
        buf[i] = (float)i * 0.001f;
    }
    synth_render_add(&s, buf, 128);

    for (i = 0; i < 128; ++i) {
        if (buf[i] != (float)i * 0.001f + solo[i]) {
            CHECK(buf[i] == (float)i * 0.001f + solo[i]);
            break;
        }
    }
}

/* The other half of the same contract, and the one a wrong flag would break
   silently: the first part in the mix overwrites, so the host needs no clear. */
static void test_render_still_overwrites_the_buffer(void)
{
    synth_t s;
    float clean[128], dirty[128];
    int i;

    synth_init(&s, SR);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, clean, 128);

    synth_init(&s, SR);
    synth_note_on(&s, 60, 1.0f);
    for (i = 0; i < 128; ++i) {
        dirty[i] = 9.0f;
    }
    synth_render(&s, dirty, 128);

    for (i = 0; i < 128; ++i) {
        if (dirty[i] != clean[i]) {
            CHECK(dirty[i] == clean[i]);
            break;
        }
    }
}

/* A part with nothing sounding still has to be rendered, or its clock falls
   behind the others'. What it must not do is touch the mix. */
static void test_a_silent_part_costs_the_mix_nothing(void)
{
    synth_t s;
    float buf[64];
    int i;

    synth_init(&s, SR);
    for (i = 0; i < 64; ++i) {
        buf[i] = 0.25f;
    }
    synth_render_add(&s, buf, 64);

    for (i = 0; i < 64; ++i) {
        if (buf[i] != 0.25f) {
            CHECK(buf[i] == 0.25f);
            break;
        }
    }
    CHECK(synth_frame_time(&s) == 64);
}

static void test_render_add_advances_the_clock_like_render(void)
{
    synth_t a, b;
    float buf[96];

    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_note_on(&a, 60, 1.0f);
    synth_note_on(&b, 60, 1.0f);
    synth_render(&a, buf, 96);
    synth_render_add(&b, buf, 96);
    CHECK(synth_frame_time(&a) == synth_frame_time(&b));
    CHECK(synth_frame_time(&b) == 96);

    /* And an empty request is a no-op for both, clock included. */
    synth_render_add(&b, buf, 0);
    CHECK(synth_frame_time(&b) == 96);
}

static void test_lfo_rate_is_in_hertz(void)
{
    synth_lfo_t lfo;
    float previous;
    int cycles = 0;
    int i;

    /* The rate has to mean hertz however coarsely the host steps it, because
       the engine runs the LFO once every SYNTH_MOD_INTERVAL samples. */
    synth_lfo_init(&lfo, SR, 1u);
    synth_lfo_set_rate(&lfo, 4.0f, 8);
    previous = synth_lfo_next(&lfo);

    for (i = 0; i < (int)(SR / 8.0f); ++i) {
        float value = synth_lfo_next(&lfo);

        if (previous <= 0.0f && value > 0.0f) {
            ++cycles;
        }
        previous = value;
    }
    CHECK(cycles >= 3 && cycles <= 5);
}

static void test_lfo_shapes(void)
{
    synth_lfo_t lfo;
    float lowest = 2.0f, highest = -2.0f, mean = 0.0f;
    int shape, i;
    int held_runs = 0;

    for (shape = 0; shape < SYNTH_LFO_COUNT; ++shape) {
        lowest = 2.0f;
        highest = -2.0f;
        mean = 0.0f;

        synth_lfo_init(&lfo, SR, 12345u);
        lfo.shape = (synth_lfo_shape_t)shape;
        synth_lfo_set_rate(&lfo, 50.0f, 1);

        for (i = 0; i < 8192; ++i) {
            float v = synth_lfo_next(&lfo);

            if (v < lowest) {
                lowest = v;
            }
            if (v > highest) {
                highest = v;
            }
            mean += v;
        }
        /* Every shape is bipolar and roughly centred, so a depth control means
           the same thing whichever one is selected. */
        CHECK(lowest >= -1.001f && lowest < -0.5f);
        CHECK(highest <= 1.001f && highest > 0.5f);
        CHECK(fabsf(mean / 8192.0f) < 0.2f);
    }

    /* Random holds its value across a cycle rather than jittering per call. */
    synth_lfo_init(&lfo, SR, 999u);
    lfo.shape = SYNTH_LFO_RANDOM;
    synth_lfo_set_rate(&lfo, 20.0f, 1);
    {
        float last = synth_lfo_next(&lfo);

        for (i = 0; i < 4096; ++i) {
            float v = synth_lfo_next(&lfo);

            if (v == last) {
                ++held_runs;
            }
            last = v;
        }
    }
    CHECK(held_runs > 3500);
}

static void test_lfo_defaults_change_nothing(void)
{
    synth_t s;
    static float buf[2048];
    float first_cutoff, later_cutoff;

    /* Adding an LFO section must leave every existing patch sounding as it did,
       which means the shipped depths have to be exactly zero. */
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_LFO_TO_CUTOFF,
                                  synth_param_info(SYNTH_PARAM_LFO_TO_CUTOFF)->default_norm),
               0.0f, 1e-6f);
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_LFO_TO_PITCH,
                                  synth_param_info(SYNTH_PARAM_LFO_TO_PITCH)->default_norm),
               0.0f, 1e-6f);
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_LFO_TO_AMP,
                                  synth_param_info(SYNTH_PARAM_LFO_TO_AMP)->default_norm),
               0.0f, 1e-6f);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, buf, 64);
    first_cutoff = voice_cutoff_octaves(&s);
    render_seconds(&s, buf, 2048, 1.0f);
    later_cutoff = voice_cutoff_octaves(&s);
    CHECK_NEAR(first_cutoff, later_cutoff, first_cutoff * 0.001f);
}

static void test_lfo_wobbles_the_cutoff(void)
{
    synth_t s;
    static float buf[512];
    float lowest = 1e9f, highest = 0.0f;
    int block;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_LFO_RATE, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_LFO_TO_CUTOFF, 1.0f); /* +3 octaves */
    synth_note_on(&s, 60, 1.0f);

    for (block = 0; block < 200; ++block) {
        float g;

        synth_render(&s, buf, 512);
        g = voice_cutoff_octaves(&s);
        if (g < lowest) {
            lowest = g;
        }
        if (g > highest) {
            highest = g;
        }
    }
    CHECK(highest > lowest * 4.0f);
}

static void test_lfo_gives_vibrato(void)
{
    synth_t s;
    static float buf[4096];
    float lowest = 1e9f, highest = 0.0f;
    int block;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_LFO_RATE, 0.6f);
    synth_set_param(&s, SYNTH_PARAM_LFO_TO_PITCH, 1.0f); /* +12 semitones */
    synth_note_on(&s, 60, 1.0f);

    for (block = 0; block < 120; ++block) {
        float inc;

        synth_render(&s, buf, 512);
        inc = s.voices[0].osc.phase_inc;
        if (inc < lowest) {
            lowest = inc;
        }
        if (inc > highest) {
            highest = inc;
        }
    }
    /* A full swing of an octave either way, so the extremes differ by a lot
       more than rounding. */
    CHECK(highest > lowest * 1.5f);
}

static void test_lfo_tremolo_only_dips(void)
{
    synth_t s;
    static float buf[512];
    float loudest = 0.0f;
    int block;

    /* Depth must take level away rather than add it, or turning tremolo up
       would make a patch clip that did not before. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_LFO_RATE, 0.6f);
    synth_set_param(&s, SYNTH_PARAM_LFO_TO_AMP, 1.0f);
    synth_note_on(&s, 60, 1.0f);

    for (block = 0; block < 60; ++block) {
        synth_render(&s, buf, 512);
        CHECK(s.voices[0].amp.level <= s.voices[0].amp_base + 1e-6f);
        if (s.voices[0].amp.level > loudest) {
            loudest = s.voices[0].amp.level;
        }
    }
    CHECK(loudest > 0.5f * s.voices[0].amp_base);
}

static void test_lfo_retriggers_with_the_note(void)
{
    synth_t s;
    static float buf[512];

    /* Vibrato that starts wherever the LFO happened to be would make the same
       note sound different each time it is struck. A destination has to be set
       for there to be anything to hear: the engine does not run an LFO that
       reaches nothing, so with every depth centred its phase would not move. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_LFO_RATE, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_LFO_TO_CUTOFF, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, buf, 512);
    CHECK(s.voices[0].lfo.phase > 0.0f);

    /* Striking the same note again reuses that voice, so its LFO has to go back
       to the start rather than carry on from wherever it had drifted to. */
    synth_note_on(&s, 60, 1.0f);
    CHECK(synth_active_voices(&s) == 1);
    CHECK(s.voices[0].lfo.phase == 0.0f);
    CHECK(s.voices[0].lfo_value == 0.0f);
}

/* Frequency the engine is actually sounding, counted from its output rather
   than read out of the voice: upward zero crossings over the window. Only
   meaningful for a waveform that crosses zero twice a cycle, so the tests below
   leave the oscillator on its default sine. */
static float rendered_hz(synth_t *s, float *buf, int cap, float seconds)
{
    int total = (int)(SR * seconds);
    int crossings = 0;
    float prev = 0.0f;
    int done = 0;

    while (done < total) {
        int n = (total - done < cap) ? (total - done) : cap;
        int i;

        synth_render(s, buf, n);
        for (i = 0; i < n; ++i) {
            if (prev <= 0.0f && buf[i] > 0.0f) {
                ++crossings;
            }
            prev = buf[i];
        }
        done += n;
    }
    return (float)crossings / seconds;
}

/* A kick is a fast sweep downwards onto the note, so the note has to start high
   and settle. Measured in the audio, because that is the part a listener
   hears. */
static void test_pitch_env_sweeps_the_note_down(void)
{
    synth_t s;
    static float buf[512];
    float at_start, at_end;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.75f); /* +2 octaves */
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_ATTACK, 0.0f);  /* instant */
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_DECAY, 0.63f);  /* about a second */
    synth_note_on(&s, 48, 1.0f); /* 130.8 Hz */

    at_start = rendered_hz(&s, buf, 512, 0.05f);
    render_seconds(&s, buf, 512, 1.5f);
    at_end = rendered_hz(&s, buf, 512, 0.2f);

    /* Four times up at the peak, decaying through the first window, so the
       measured start lands somewhere near three times the note. */
    CHECK(at_start > at_end * 2.5f);
    CHECK_NEAR(at_end, 130.8f, 6.0f);
}

/* Negative amount is the siren, sweeping up onto the note instead. */
static void test_pitch_env_amount_is_bipolar(void)
{
    synth_t s;
    static float buf[512];
    float at_start, at_end;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.25f); /* -2 octaves */
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_DECAY, 0.63f);
    synth_note_on(&s, 60, 1.0f); /* 261.6 Hz */

    at_start = rendered_hz(&s, buf, 512, 0.05f);
    render_seconds(&s, buf, 512, 1.5f);
    at_end = rendered_hz(&s, buf, 512, 0.2f);

    CHECK(at_end > at_start * 2.5f);
    CHECK_NEAR(at_end, 261.6f, 8.0f);
}

/* The decay is what separates a drum from a siren, so it has to be the control
   that decides how long the sweep lasts. */
static void test_pitch_env_decay_sets_the_sweep_length(void)
{
    synth_t s;
    static float buf[512];
    float quick, slow;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.75f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_DECAY, 0.29f); /* about 100 ms */
    synth_note_on(&s, 48, 1.0f);
    render_seconds(&s, buf, 512, 0.15f);
    quick = rendered_hz(&s, buf, 512, 0.1f);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.75f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_DECAY, 0.79f); /* about two seconds */
    synth_note_on(&s, 48, 1.0f);
    render_seconds(&s, buf, 512, 0.15f);
    slow = rendered_hz(&s, buf, 512, 0.1f);

    /* At the same moment the quick one is already home and the slow one has
       barely moved. */
    CHECK_NEAR(quick, 130.8f, 8.0f);
    CHECK(slow > quick * 2.0f);
}

/* Compatibility: the section has to be silent in every patch written before it
   existed, which means its default amount is exactly no sweep. */
static void test_pitch_env_defaults_change_nothing(void)
{
    synth_t s;
    static float buf[512];

    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_PITCH_ENV_AMOUNT,
                                  synth_param_info(SYNTH_PARAM_PITCH_ENV_AMOUNT)->default_norm),
               0.0f, 1e-6f);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 69, 1.0f);
    CHECK_NEAR(rendered_hz(&s, buf, 512, 0.2f), 440.0f, 8.0f);
    CHECK_NEAR(rendered_hz(&s, buf, 512, 0.2f), 440.0f, 8.0f);
}

/* The new parameters have to sit past every old one, because a host stores a
   patch as the positional array synth_save_patch() writes and an index that
   moved would reinterpret every saved sound in silence. */
static void test_new_parameters_are_appended(void)
{
    /* Every index this library has published, spelled out, so that moving one
       fails here rather than at a host that already has patches on disk. A new
       parameter adds a line at the bottom and changes nothing above it. */
    CHECK(SYNTH_PARAM_MASTER_GAIN == 0);
    CHECK(SYNTH_PARAM_FILTER_ENV_AMOUNT == 17);
    CHECK(SYNTH_PARAM_AMP_VELOCITY == 24);
    CHECK(SYNTH_PARAM_LFO_TO_AMP == 29);
    CHECK(SYNTH_PARAM_PITCH_ENV_AMOUNT == 30);
    CHECK(SYNTH_PARAM_PITCH_ENV_ATTACK == 31);
    CHECK(SYNTH_PARAM_PITCH_ENV_DECAY == 32);
    CHECK(SYNTH_PARAM_GLIDE == 33);
}

/* A patch from the build before the pitch envelope existed. Filling what it
   does not carry with zeros would put that bipolar amount at its negative
   extreme, so the short load fills from the defaults instead and the old sound
   comes back unchanged. */
static void test_a_short_patch_loads_at_its_defaults(void)
{
    synth_t s;
    static float buf[512];
    float patch[SYNTH_PARAM_COUNT];
    int older = SYNTH_PARAM_PITCH_ENV_AMOUNT;
    int i;

    synth_init(&s, SR);
    synth_save_patch(&s, patch);
    for (i = older; i < SYNTH_PARAM_COUNT; ++i) {
        patch[i] = 0.0f; /* what a zero-filled buffer would hand the loader */
    }

    synth_load_patch_n(&s, patch, older);
    CHECK_NEAR(synth_get_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT), 0.5f, 1e-6f);

    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 69, 1.0f);
    CHECK_NEAR(rendered_hz(&s, buf, 512, 0.2f), 440.0f, 8.0f);

    /* And the same array loaded whole really is the broken case, so the test
       above is measuring something. */
    synth_load_patch(&s, patch);
    CHECK(synth_get_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT) < 0.5f);
}

/* Filling a patch with something distinguishable from the defaults, and not
   with 1.0f everywhere, which would make a stepped parameter land on its last
   value and a bipolar one on its extreme. */
static void fill_patch(float *patch, float value)
{
    int i;

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        patch[i] = value;
    }
}

/* Everything a patch load touches, and nothing else. Not the whole synth_t: the
   event queue's unused slots are never written by anyone — synth_init does not
   clear them either, and nothing reads one the indices do not cover — so two
   engines that agree completely still differ there by whatever was on the
   stack. */
static int same_sound(const synth_t *a, const synth_t *b)
{
    return memcmp(a->params, b->params, sizeof a->params) == 0
           && memcmp(a->voices, b->voices, sizeof a->voices) == 0
           && a->last_note == b->last_note;
}

static void test_a_sent_patch_loads_exactly_as_a_direct_load(void)
{
    synth_t a, b;
    synth_patch_queue_t q;
    float patch[SYNTH_PARAM_COUNT];

    fill_patch(patch, 0.3f);
    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_note_on(&a, 60, 1.0f);
    synth_note_on(&b, 60, 1.0f);

    synth_load_patch(&a, patch);

    synth_patch_queue_init(&q);
    CHECK(synth_patch_send(&q, patch, SYNTH_PARAM_COUNT) == 1);
    CHECK(synth_patch_apply(&q, &b) == 1);

    /* Every voice, not just the parameters: going through the queue must not be
       a different way of loading a patch, only a different thread. */
    CHECK(same_sound(&a, &b));
    CHECK(rendered_is_identical(&a, &b, 256));
}

static void test_applying_nothing_leaves_the_engine_alone(void)
{
    synth_t a, b;
    synth_patch_queue_t q;

    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_note_on(&a, 60, 1.0f);
    synth_note_on(&b, 60, 1.0f);

    synth_patch_queue_init(&q);
    CHECK(synth_patch_apply(&q, &b) == 0);
    CHECK(same_sound(&a, &b));
}

/* Two patches waiting in the same block: the first is never heard, so applying
   only the second has to leave exactly what applying both would. */
static void test_two_waiting_patches_collapse_to_the_newer(void)
{
    synth_t a, b;
    synth_patch_queue_t q;
    float first[SYNTH_PARAM_COUNT], second[SYNTH_PARAM_COUNT];

    fill_patch(first, 0.2f);
    fill_patch(second, 0.7f);
    synth_init(&a, SR);
    synth_init(&b, SR);

    synth_load_patch(&a, first);
    synth_load_patch(&a, second);

    synth_patch_queue_init(&q);
    CHECK(synth_patch_send(&q, first, SYNTH_PARAM_COUNT) == 1);
    CHECK(synth_patch_send(&q, second, SYNTH_PARAM_COUNT) == 1);
    CHECK(synth_patch_apply(&q, &b) == 1);
    CHECK(same_sound(&a, &b));
    CHECK(rendered_is_identical(&a, &b, 256));

    /* And the queue is empty afterwards, both slots free again. */
    CHECK(synth_patch_apply(&q, &b) == 0);
    CHECK(synth_patch_send(&q, first, SYNTH_PARAM_COUNT) == 1);
    CHECK(synth_patch_send(&q, first, SYNTH_PARAM_COUNT) == 1);
}

/* Three sends between two renders is the one case the two slots cannot take.
   Refusing keeps the slot the reader may be reading intact; the alternative is a
   patch torn down the middle, which would be a sound nobody asked for. */
static void test_a_third_patch_in_one_block_is_refused(void)
{
    synth_t a, b;
    synth_patch_queue_t q;
    float first[SYNTH_PARAM_COUNT], second[SYNTH_PARAM_COUNT], third[SYNTH_PARAM_COUNT];

    fill_patch(first, 0.2f);
    fill_patch(second, 0.7f);
    fill_patch(third, 0.9f);
    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_load_patch(&a, second);

    synth_patch_queue_init(&q);
    CHECK(synth_patch_send(&q, first, SYNTH_PARAM_COUNT) == 1);
    CHECK(synth_patch_send(&q, second, SYNTH_PARAM_COUNT) == 1);
    CHECK(synth_patch_send(&q, third, SYNTH_PARAM_COUNT) == 0);

    /* What arrives is the second, not the third: the refusal is honest about
       having dropped it rather than quietly keeping the newest. */
    CHECK(synth_patch_apply(&q, &b) == 1);
    CHECK(same_sound(&a, &b));
}

static void test_a_short_patch_through_the_queue_takes_the_defaults(void)
{
    synth_t a, b;
    synth_patch_queue_t q;
    float patch[SYNTH_PARAM_COUNT];
    int older = SYNTH_PARAM_PITCH_ENV_AMOUNT;

    fill_patch(patch, 0.4f);
    synth_init(&a, SR);
    synth_init(&b, SR);
    synth_load_patch_n(&a, patch, older);

    synth_patch_queue_init(&q);
    CHECK(synth_patch_send(&q, patch, older) == 1);
    CHECK(synth_patch_apply(&q, &b) == 1);
    CHECK(same_sound(&a, &b));
    CHECK_NEAR(synth_get_param(&b, SYNTH_PARAM_PITCH_ENV_AMOUNT), 0.5f, 1e-6f);

    /* An empty send is the same thing taken to its limit: every default, which
       is how a host resets a track. Compared against the same history rather
       than against a fresh instance — loading a patch tunes the oscillator, so
       a synth_init that has never seen a note keeps a phase increment of zero
       where this one has one, which is a difference no note_on survives. */
    synth_load_patch_n(&a, patch, 0);
    CHECK(synth_patch_send(&q, patch, 0) == 1);
    CHECK(synth_patch_apply(&q, &b) == 1);
    CHECK(same_sound(&a, &b));
    CHECK_NEAR(synth_get_param(&b, SYNTH_PARAM_AMP_ATTACK),
               synth_get_param(&a, SYNTH_PARAM_AMP_ATTACK), 1e-6f);
}

/* The indices are unsigned and compared by difference, so the queue has to keep
   working when they wrap rather than jamming after four billion patches. */
static void test_the_patch_queue_survives_its_counters_wrapping(void)
{
    synth_t s;
    synth_patch_queue_t q;
    float patch[SYNTH_PARAM_COUNT];
    int i;

    fill_patch(patch, 0.5f);
    synth_init(&s, SR);
    synth_patch_queue_init(&q);
    q.produced = 0xFFFFFFFEu;
    q.consumed = 0xFFFFFFFEu;

    for (i = 0; i < 8; ++i) {
        CHECK(synth_patch_send(&q, patch, SYNTH_PARAM_COUNT) == 1);
        CHECK(synth_patch_apply(&q, &s) == 1);
    }
}

/* Striking the same note again reuses its voice, so the sweep has to start over
   rather than carry on from wherever the last one had fallen to. */
static void test_pitch_env_restarts_on_a_retrigger(void)
{
    synth_t s;
    static float buf[512];
    float first, again;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.75f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_DECAY, 0.63f);

    synth_note_on(&s, 48, 1.0f);
    first = rendered_hz(&s, buf, 512, 0.05f);
    render_seconds(&s, buf, 512, 1.5f); /* sweep long over */

    synth_note_on(&s, 48, 1.0f);
    again = rendered_hz(&s, buf, 512, 0.05f);

    CHECK_NEAR(again, first, first * 0.1f);
    CHECK(again > 300.0f);
}

/* Turning the amount back to neutral while a note sounds has to put that note
   back in tune. The render loop stops retuning the oscillator once nothing
   modulates the pitch, so a control that is centred mid-sweep is the one moment
   a stale offset could stick. */
static void test_centring_the_amount_puts_the_note_back(void)
{
    synth_t s;
    static float buf[512];

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.75f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_DECAY, 0.79f); /* still high up */
    synth_note_on(&s, 69, 1.0f);
    render_seconds(&s, buf, 512, 0.05f);

    synth_set_param(&s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.5f);
    CHECK_NEAR(rendered_hz(&s, buf, 512, 0.2f), 440.0f, 8.0f);
}

/* The engine skips any modulation whose depth is centred, because work nothing
   listens to is pure cost. These three pin the consequence: silent when off,
   and awake the moment a depth is turned up. */
static void test_a_modulation_nothing_hears_changes_nothing(void)
{
    synth_t slow, fast;
    static float a[600], b[600];
    int i;

    /* Same patch, two LFO rates, every destination centred. If the LFO reached
       anything at all these would diverge. */
    synth_init(&slow, SR);
    synth_init(&fast, SR);
    synth_set_param(&slow, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&fast, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&slow, SYNTH_PARAM_LFO_RATE, 0.1f);
    synth_set_param(&fast, SYNTH_PARAM_LFO_RATE, 1.0f);
    synth_note_on(&slow, 60, 1.0f);
    synth_note_on(&fast, 60, 1.0f);
    synth_render(&slow, a, 600);
    synth_render(&fast, b, 600);

    for (i = 0; i < 600; ++i) {
        CHECK_NEAR(a[i], b[i], 0.0f);
    }
}

static void test_turning_a_depth_up_mid_note_wakes_the_lfo(void)
{
    synth_t s;
    static float buf[256];
    float lowest = 1e9f, highest = -1e9f;
    int block;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_LFO_RATE, 0.6f);
    synth_note_on(&s, 60, 1.0f);
    render_seconds(&s, buf, 256, 0.5f); /* nothing listening, so nothing moves */
    CHECK(s.voices[0].lfo.phase == 0.0f);

    synth_set_param(&s, SYNTH_PARAM_LFO_TO_CUTOFF, 1.0f);
    for (block = 0; block < 120; ++block) {
        float octaves;

        synth_render(&s, buf, 256);
        octaves = voice_cutoff_octaves(&s);
        if (octaves < lowest) {
            lowest = octaves;
        }
        if (octaves > highest) {
            highest = octaves;
        }
    }
    CHECK(highest > lowest * 1.5f);
}

static void test_a_filter_nothing_modulates_is_left_alone(void)
{
    synth_t s;
    static float buf[256];
    float settled, later, opened;

    /* Envelope amount centred, no key tracking, LFO centred: the cutoff has
       nothing to follow, so it must not move. */
    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_CUTOFF, 0.4f);
    synth_note_on(&s, 60, 1.0f);
    synth_render(&s, buf, 256);
    settled = voice_cutoff_octaves(&s);
    render_seconds(&s, buf, 256, 0.5f);
    later = voice_cutoff_octaves(&s);
    CHECK_NEAR(later, settled, 0.0f);

    /* And the moment the envelope is given an amount, it follows again. It
       starts from where it was frozen, which is the one thing this costs: an
       envelope that has not been running has not decayed either. */
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_ATTACK, 0.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_FILTER_ENV_AMOUNT, 1.0f); /* +4 octaves */
    render_seconds(&s, buf, 256, 0.05f);
    opened = voice_cutoff_octaves(&s);
    CHECK(opened > settled * 2.0f);
}

/* A parameter change re-applies only the parts of a voice that parameter
   reaches, because re-applying all of them cost more than three blocks of
   audio. This checks that shortcut against the long way round, for every
   parameter there is and under every waveform: synth_load_patch() re-applies
   everything, so a sounding voice edited through synth_set_param() has to come
   out identical. State rather than audio, because a parameter placed in the
   wrong bucket can leave a voice wrong in a way this patch happens not to
   sound — until the next note does.

   Only the sounding voice. An idle one is tuned lazily, by the note_on that
   claims it, so its oscillator frequency is deliberately undefined in between
   and the two paths leave it in different places. */
static void test_a_parameter_change_reaches_everything_it_should(void)
{
    static float quick[256], full[256];
    int p, w;

    for (w = 0; w < SYNTH_WAVE_COUNT; ++w) {
        for (p = 0; p < SYNTH_PARAM_COUNT; ++p) {
            synth_t a, b;
            float patch[SYNTH_PARAM_COUNT];
            float before = synth_param_info((synth_param_t)p)->default_norm;
            float after = (before > 0.5f) ? 0.2f : 0.8f;

            synth_init(&a, SR);
            synth_init(&b, SR);
            synth_set_param(&a, SYNTH_PARAM_OSC_WAVE, (float)w / (float)(SYNTH_WAVE_COUNT - 1));
            synth_set_param(&b, SYNTH_PARAM_OSC_WAVE, (float)w / (float)(SYNTH_WAVE_COUNT - 1));
            synth_set_param(&a, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
            synth_set_param(&b, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
            synth_note_on(&a, 55, 0.8f);
            synth_note_on(&b, 55, 0.8f);
            synth_render(&a, quick, 256);
            synth_render(&b, full, 256);

            /* The same edit, once through the shortcut and once through a
               whole patch load, which reapplies every unit of every voice. */
            synth_set_param(&a, (synth_param_t)p, after);
            synth_save_patch(&b, patch);
            patch[p] = after;
            synth_load_patch(&b, patch);

            /* synth_voice_t is all four-byte members, so this compares state
               and not padding. */
            if (memcmp(&a.voices[0], &b.voices[0], sizeof a.voices[0]) != 0) {
                printf("FAIL %s:%d: parameter %d leaves a voice wrong on wave %d\n",
                       __FILE__, __LINE__, p, w);
                ++g_failures;
            }

            /* And the audio that state produces, which is the point of it. */
            synth_render(&a, quick, 256);
            synth_render(&b, full, 256);
            if (memcmp(quick, full, sizeof quick) != 0) {
                printf("FAIL %s:%d: parameter %d diverges in the audio on wave %d\n",
                       __FILE__, __LINE__, p, w);
                ++g_failures;
            }
        }
    }
}

/* And the unit on its own, which is where synth_env_set_sample_rate() is the
   only thing that can put the derived rates right: the engine happens to reset
   every voice's times when a note starts, so it would paper over this. */
static int env_attack_samples(float sample_rate, int retune)
{
    synth_env_t env;
    int i;

    synth_env_init(&env, 44100.0f);
    synth_env_set_times(&env, 0.0f, 0.25f, 0.0f, 1.0f, 1.0f, 1.0f);
    if (retune) {
        synth_env_set_sample_rate(&env, sample_rate);
    } else {
        env.sample_rate = sample_rate; /* the field alone, which is not enough */
    }
    synth_env_gate_on(&env);
    for (i = 0; i < 400000; ++i) {
        synth_env_next(&env);
        if (env.stage != SYNTH_ENV_ATTACK && env.stage != SYNTH_ENV_DELAY) {
            break;
        }
    }
    return i;
}

/* What licenses skipping the idle voices on a parameter change: a note played
   after the change has to come out identical to one that was already sounding
   when it arrived. Both engines here do exactly one note_on and one
   set_param — only the order differs — so there is no history to explain away
   and a whole-voice comparison is fair. If any unit a parameter reaches were
   missing from the note_on path, this is where it would show: the parameter
   would be in the engine and not in the voice that played it. */
/* A waveform's derived state is kept current only while that waveform is the one
   selected — deriving both of the pitch-dependent ones on every retune cost 10%
   of a modulated render loop, and nothing reads the one that is not playing. So
   a voice that has passed through VOSIM carries its old pulse layout until VOSIM
   is selected again and synth_osc_set_wave() derives it afresh. Two voices that
   agree on everything audible can differ there, and this is what "everything
   audible" means. */
static void forget_unselected_waveforms(synth_voice_t *v)
{
    if (v->osc.wave != SYNTH_WAVE_VOSIM) {
        v->osc.pulse_rate = 0.0f;
        v->osc.vosim_dc = 0.0f;
        v->osc.vosim_fitting = 0;
    }
    if (v->osc.wave != SYNTH_WAVE_PD) {
        v->osc.pd_knee = 0.0f;
        v->osc.pd_rise = 0.0f;
        v->osc.pd_fall = 0.0f;
        v->osc.pd_scale = 0.0f;
        v->osc.pd_offset = 0.0f;
    }
}

static void test_a_note_played_after_an_edit_matches_one_playing_during_it(void)
{
    static float later[256], during[256];
    int p, w;

    for (w = 0; w < SYNTH_WAVE_COUNT; ++w) {
        for (p = 0; p < SYNTH_PARAM_COUNT; ++p) {
            synth_t a, b;
            synth_voice_t live_a, live_b;
            float before = synth_param_info((synth_param_t)p)->default_norm;
            float after = (before > 0.5f) ? 0.2f : 0.8f;

            synth_init(&a, SR);
            synth_init(&b, SR);
            synth_set_param(&a, SYNTH_PARAM_OSC_WAVE, (float)w / (float)(SYNTH_WAVE_COUNT - 1));
            synth_set_param(&b, SYNTH_PARAM_OSC_WAVE, (float)w / (float)(SYNTH_WAVE_COUNT - 1));
            synth_set_param(&a, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
            synth_set_param(&b, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);

            /* a: the edit lands on an idle slot and the note claims it after.
               b: the note is already sounding, so the edit is applied in place. */
            synth_set_param(&a, (synth_param_t)p, after);
            synth_note_on(&a, 55, 0.8f);
            synth_note_on(&b, 55, 0.8f);
            synth_set_param(&b, (synth_param_t)p, after);

            /* On copies: the engines are rendered below, and a test that edited
               them first would be comparing audio it had touched. */
            live_a = a.voices[0];
            live_b = b.voices[0];
            forget_unselected_waveforms(&live_a);
            forget_unselected_waveforms(&live_b);
            if (memcmp(&live_a, &live_b, sizeof live_a) != 0) {
                printf("FAIL %s:%d: parameter %d reaches a sounding voice but not"
                       " a note_on, on wave %d\n", __FILE__, __LINE__, p, w);
                ++g_failures;
            }

            synth_render(&a, later, 256);
            synth_render(&b, during, 256);
            if (memcmp(later, during, sizeof later) != 0) {
                printf("FAIL %s:%d: parameter %d diverges in the audio on wave %d\n",
                       __FILE__, __LINE__, p, w);
                ++g_failures;
            }
        }
    }
}

static void test_an_envelope_retimes_itself_for_a_new_rate(void)
{
    int at_44k = env_attack_samples(44100.0f, 1);
    int at_96k = env_attack_samples(96000.0f, 1);

    /* A quarter of a second, counted in each rate's own samples. */
    CHECK(at_44k > 11000 && at_44k < 11200);
    CHECK(at_96k > 23900 && at_96k < 24100);
}

/* Envelope times are in seconds, so they have to survive a host that only
   learns the device's real rate once the stream is open. Each ramp keeps how
   far it travels per sample, which is the thing a rate change invalidates. */
static void test_envelope_times_survive_a_sample_rate_change(void)
{
    synth_t s;
    static float buf[256];
    float at_44k, at_96k;
    int i;

    synth_init(&s, 44100.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_ATTACK, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 60, 1.0f);
    for (i = 0; i < 400; ++i) {
        synth_render(&s, buf, 256);
        if (s.voices[0].amp_env.stage != SYNTH_ENV_ATTACK) {
            break;
        }
    }
    at_44k = (float)(i * 256) / 44100.0f;

    synth_init(&s, 44100.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_ATTACK, 0.5f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_sample_rate(&s, 96000.0f);
    synth_note_on(&s, 60, 1.0f);
    for (i = 0; i < 800; ++i) {
        synth_render(&s, buf, 256);
        if (s.voices[0].amp_env.stage != SYNTH_ENV_ATTACK) {
            break;
        }
    }
    at_96k = (float)(i * 256) / 96000.0f;

    CHECK(at_44k > 0.05f); /* long enough that the comparison means something */
    CHECK_NEAR(at_96k, at_44k, at_44k * 0.05f);
}

/* Portamento: a note starts on the pitch of the one before it and travels. */
static void test_glide_starts_a_note_on_the_previous_pitch(void)
{
    synth_t s;
    static float buf[512];
    float at_start, at_end;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_GLIDE, 0.63f); /* about half a second */

    /* The first note has nothing to come from, so it must be in tune at once. */
    synth_note_on(&s, 45, 1.0f); /* 110 Hz */
    CHECK_NEAR(rendered_hz(&s, buf, 512, 0.1f), 110.0f, 6.0f);
    render_seconds(&s, buf, 512, 0.6f);
    synth_note_off(&s, 45);
    /* Right through the release: rendered_hz counts crossings of the sum, so a
       note still dying would be measured along with the new one. */
    render_seconds(&s, buf, 512, 0.6f);

    /* The second starts where the first was and arrives at its own pitch. */
    synth_note_on(&s, 57, 1.0f); /* 220 Hz */
    at_start = rendered_hz(&s, buf, 512, 0.03f);
    render_seconds(&s, buf, 512, 1.0f);
    at_end = rendered_hz(&s, buf, 512, 0.2f);

    CHECK(at_start < 140.0f);
    CHECK_NEAR(at_end, 220.0f, 8.0f);
}

/* The control is a time, so a longer one has the note further from home at the
   same moment. */
static void test_glide_time_sets_how_long_the_travel_takes(void)
{
    synth_t s;
    static float buf[512];
    float quick, slow;

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_GLIDE, 0.4f); /* about 130 ms */
    synth_note_on(&s, 45, 1.0f);
    render_seconds(&s, buf, 512, 0.3f);
    synth_note_off(&s, 45);
    render_seconds(&s, buf, 512, 0.6f);
    synth_note_on(&s, 69, 1.0f);
    render_seconds(&s, buf, 512, 0.2f); /* long over */
    quick = rendered_hz(&s, buf, 512, 0.1f);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_GLIDE, 0.9f); /* about 1.5 s */
    synth_note_on(&s, 45, 1.0f);
    render_seconds(&s, buf, 512, 0.3f);
    synth_note_off(&s, 45);
    render_seconds(&s, buf, 512, 0.6f);
    synth_note_on(&s, 69, 1.0f);
    render_seconds(&s, buf, 512, 0.2f); /* barely started */
    slow = rendered_hz(&s, buf, 512, 0.1f);

    CHECK_NEAR(quick, 440.0f, 12.0f);
    CHECK(slow < quick * 0.6f);
}

/* Off by default, and off means exactly off: a note is in tune from its first
   sample however many notes came before it. */
static void test_no_glide_by_default(void)
{
    synth_t s;
    static float buf[512];

    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_GLIDE,
                                  synth_param_info(SYNTH_PARAM_GLIDE)->default_norm),
               0.0f, 1e-6f);

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_note_on(&s, 45, 1.0f);
    render_seconds(&s, buf, 512, 0.3f);
    synth_note_off(&s, 45);
    render_seconds(&s, buf, 512, 0.6f); /* let it die, so only the new note is heard */
    synth_note_on(&s, 69, 1.0f);
    CHECK_NEAR(rendered_hz(&s, buf, 512, 0.05f), 440.0f, 20.0f);
}

/* Each voice carries its own travel, so a chord built one note at a time does
   not drag the notes already in it. */
#if SYNTH_MAX_VOICES > 1
static void test_a_glide_belongs_to_its_own_voice(void)
{
    synth_t s;
    static float buf[512];

    synth_init(&s, SR);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
    synth_set_param(&s, SYNTH_PARAM_GLIDE, 0.7f);
    synth_note_on(&s, 45, 1.0f);
    render_seconds(&s, buf, 512, 0.5f);   /* in tune, nothing to travel */
    CHECK(s.voices[0].glide == 0.0f);

    synth_note_on(&s, 69, 1.0f);          /* a second voice, travelling */
    CHECK(s.voices[0].glide == 0.0f);
    CHECK(s.voices[1].glide != 0.0f);

    render_seconds(&s, buf, 512, 2.0f);
    CHECK(s.voices[1].glide == 0.0f);     /* and it arrives */
}
#endif

/* The delay line. The buffer is the caller's, so these tests declare it the way
   an MCU would. */
#define DELAY_FRAMES 8192

/* A triangle, so the tests have a continuous signal without reaching into the
   oscillator's internals. Period is `period` frames, peak is 1. */
static float ramp_wave(int i, int period)
{
    float t = (float)(i % period) / (float)period;

    return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
}

static void test_delay_repeats_after_its_time(void)
{
    synth_delay_t d;
    static float line[DELAY_FRAMES];
    float out;
    int i;
    int heard = -1;

    synth_delay_init(&d, line, DELAY_FRAMES, SR);
    synth_delay_set(&d, 0.05f, 0.0f, 1.0f); /* 50 ms, wet only, no repeats */
    /* Long enough for the read point to have finished sliding to 50 ms. */
    for (i = 0; i < 8000; ++i) {
        synth_delay_next(&d, 0.0f);
    }

    out = synth_delay_next(&d, 1.0f); /* one full-scale frame in */
    CHECK_NEAR(out, 0.0f, 1e-6f);     /* nothing back yet */
    for (i = 1; i < 4000; ++i) {
        out = synth_delay_next(&d, 0.0f);
        if (out > 0.5f) {
            heard = i;
            break;
        }
    }
    /* 50 ms at this rate is 2205 frames. One either way for the interpolator. */
    CHECK(heard >= 2203 && heard <= 2207);
}

static void test_delay_feedback_decays(void)
{
    synth_delay_t d;
    static float line[DELAY_FRAMES];
    float peaks[3] = { 0.0f, 0.0f, 0.0f };
    int i, n;

    synth_delay_init(&d, line, DELAY_FRAMES, SR);
    synth_delay_set(&d, 0.02f, 0.5f, 1.0f);
    for (i = 0; i < 8000; ++i) {
        synth_delay_next(&d, 0.0f);
    }
    synth_delay_next(&d, 1.0f);

    /* Three echoes, each half the one before. */
    for (n = 0; n < 3; ++n) {
        for (i = 0; i < 882; ++i) {
            float v = synth_delay_next(&d, 0.0f);

            if (v > peaks[n]) {
                peaks[n] = v;
            }
        }
    }
    CHECK_NEAR(peaks[1], peaks[0] * 0.5f, 0.02f);
    CHECK_NEAR(peaks[2], peaks[0] * 0.25f, 0.02f);
}

/* A mix of zero has to be the identity, not "nearly" the identity, so a host
   can leave the unit in the signal path and switch it off. */
static void test_delay_is_transparent_at_zero_mix(void)
{
    synth_delay_t d;
    static float line[DELAY_FRAMES];
    int i;

    synth_delay_init(&d, line, DELAY_FRAMES, SR);
    synth_delay_set(&d, 0.1f, 0.9f, 0.0f);
    for (i = 0; i < 4000; ++i) {
        float in = ramp_wave(i, 128);

        CHECK_NEAR(synth_delay_next(&d, in), in, 0.0f);
    }
}

/* Feedback is the one place a bounded input could give an unbounded signal.
   Driven at full scale with the most feedback the control allows, the line and
   the output both have to stay inside 1. */
static void test_delay_stays_bounded_under_runaway_feedback(void)
{
    synth_delay_t d;
    static float line[DELAY_FRAMES];
    float top = 0.0f;
    int i;

    synth_delay_init(&d, line, DELAY_FRAMES, SR);
    synth_delay_set(&d, 0.01f, 1.5f, 1.0f); /* asks for more than is allowed */
    CHECK_NEAR(d.feedback, 0.95f, 1e-6f);

    for (i = 0; i < 200000; ++i) {
        float v = synth_delay_next(&d, (i & 64) ? 1.0f : -1.0f);

        if (v > top) {
            top = v;
        } else if (-v > top) {
            top = -v;
        }
    }
    CHECK(top <= 1.0f);
    for (i = 0; i < DELAY_FRAMES; ++i) {
        CHECK_NEAR(line[i], line[i], 0.0f); /* no NaN survives a self-compare */
        if (line[i] > 1.0f || line[i] < -1.0f) {
            printf("FAIL %s:%d: the line holds %g\n", __FILE__, __LINE__, (double)line[i]);
            ++g_failures;
            break;
        }
    }
}

/* A new time slides rather than jumping. Both halves are measured here, so the
   test proves the sliding is what does it rather than asserting that a number
   is small: the same change made by moving the read point at once puts a step
   in the output hundreds of times bigger. */
static float delay_biggest_step(int jump)
{
    synth_delay_t d;
    static float line[DELAY_FRAMES];
    float prev, biggest = 0.0f;
    int i;

    synth_delay_init(&d, line, DELAY_FRAMES, SR);
    synth_delay_set(&d, 0.08f, 0.0f, 1.0f);
    /* Slow enough that the signal's own slope is nothing beside a jump. */
    for (i = 0; i < 20000; ++i) {
        synth_delay_next(&d, ramp_wave(i, 4000) * 0.8f);
    }

    prev = synth_delay_next(&d, ramp_wave(20000, 4000) * 0.8f);
    synth_delay_set(&d, 0.03f, 0.0f, 1.0f);
    if (jump) {
        d.offset = d.target; /* what this design exists to avoid */
    }
    for (i = 1; i < 8000; ++i) {
        float v = synth_delay_next(&d, ramp_wave(20000 + i, 4000) * 0.8f);
        float step = v - prev;

        if (step < 0.0f) {
            step = -step;
        }
        if (step > biggest) {
            biggest = step;
        }
        prev = v;
    }
    return biggest;
}

static void test_delay_time_changes_do_not_click(void)
{
    float slid = delay_biggest_step(0);
    float jumped = delay_biggest_step(1);

    CHECK(slid < 0.01f);
    CHECK(jumped > 0.1f);
    CHECK(jumped > slid * 50.0f);
}

/* The read point lands between samples while it slides, so it has to read
   between them too. An impulse at a delay of 1000.5 frames must come back split
   across the two samples either side, not snapped onto one of them — snapping
   is a staircase, and a staircase on a moving read point is zipper noise. */
static void test_delay_reads_between_samples(void)
{
    synth_delay_t d;
    static float line[DELAY_FRAMES];
    int heard = 0;
    int i;

    synth_delay_init(&d, line, DELAY_FRAMES, SR);
    synth_delay_set(&d, 1000.5f / SR, 0.0f, 1.0f);

    synth_delay_next(&d, 1.0f);
    for (i = 1; i < 1500; ++i) {
        float v = synth_delay_next(&d, 0.0f);

        if (v > 0.1f) {
            ++heard;
            CHECK_NEAR(v, 0.5f, 0.01f);
        }
    }
    CHECK(heard == 2);
}

/* A time changed while audio runs has to be reached, not merely approached: the
   slide is exponential, so without a last step onto the target a delay set to a
   musical division would sit permanently just short of it. */
static void test_delay_arrives_at_a_time_it_is_changed_to(void)
{
    synth_delay_t d;
    static float line[DELAY_FRAMES];
    int heard = -1;
    int i;

    synth_delay_init(&d, line, DELAY_FRAMES, SR);
    synth_delay_set(&d, 0.05f, 0.0f, 1.0f);
    for (i = 0; i < 4000; ++i) {
        synth_delay_next(&d, 0.0f);
    }

    synth_delay_set(&d, 0.02f, 0.0f, 1.0f); /* 882 frames at this rate */
    for (i = 0; i < 20000; ++i) {
        synth_delay_next(&d, 0.0f);
    }
    CHECK_NEAR(d.offset, 882.0f, 0.0f);

    synth_delay_next(&d, 1.0f);
    for (i = 1; i < 4000; ++i) {
        if (synth_delay_next(&d, 0.0f) > 0.5f) {
            heard = i;
            break;
        }
    }
    CHECK(heard == 882);
}

/* No buffer is not a crash, it is a bypass: a host that has not given the unit
   memory still gets its audio through. */
static void test_delay_without_a_buffer_is_a_bypass(void)
{
    synth_delay_t d;
    int i;

    synth_delay_init(&d, 0, 0, SR);
    synth_delay_set(&d, 0.1f, 0.5f, 1.0f);
    for (i = 0; i < 100; ++i) {
        CHECK_NEAR(synth_delay_next(&d, 0.25f), 0.25f, 0.0f);
    }
}

int main(void)
{
    test_note_to_hz();
    test_sine_accuracy();
    test_no_waveform_aliases_below_its_fundamental();
    test_phase_distortion_stays_band_limited_up_high();
    test_phase_distortion_still_bends_at_playable_pitches();
    test_oscillator_frequency();
    test_polyblep_reduces_saw_aliasing();
    test_polyblep_reduces_square_aliasing();
    test_polyblep_leaves_low_notes_alone();
    test_phase_distortion_neutral_at_half();
    test_phase_distortion_adds_harmonics();
    test_phase_distortion_is_centred();
    test_vosim_formant_is_independent_of_pitch();
    test_vosim_formant_follows_pulse_width();
    test_vosim_is_centred_and_bounded();
    test_vosim_decay_shapes_the_burst();
    test_vosim_pulses_cannot_overflow_the_period();
    test_switching_to_a_waveform_derives_it_at_the_pitch_it_finds();
    test_terrain_is_centred_and_bounded();
    test_terrain_radius_changes_the_spectrum();
    test_every_voice_shares_one_terrain_cross_section();
    test_terrain_ratio_changes_the_waveform();
    test_filter_env_opens_and_closes_the_filter();
    test_filter_env_amount_is_bipolar();
    test_filter_key_track_follows_pitch();
    test_key_track_survives_note_off();
    test_amp_shape_is_clean_at_zero_drive();
    test_amp_shape_stays_bounded_and_monotone();
    test_amp_drive_adds_odd_harmonics_only();
    test_amp_gates_regardless_of_drive();
    test_amp_velocity_sensitivity();
    test_amp_sits_after_the_filter();
    test_new_waves_reach_the_engine();
    test_envelope_stages();
    test_filter_lowpass_response();
    test_filter_highpass_response();
    test_filter_bandpass_response();
    test_filter_resonance();
    test_filter_is_stable_at_extremes();
    test_filter_reset_clears_state();
    test_filter_shapes_engine_output();
    test_silence_when_idle();
    test_note_on_makes_sound();
    test_release_returns_to_silence();
    test_polyphony_and_stealing();
    test_retrigger_reuses_voice();
    test_all_notes_off();
    test_sample_rate_change_keeps_params_and_tuning();
    test_output_is_finite_and_bounded();
    test_render_is_deterministic();
    test_param_mapping();
    test_gain_scales_output();
    test_midi_notes();
    test_midi_note_on_at_zero_velocity_is_a_note_off();
    test_midi_running_status();
    test_midi_realtime_bytes_do_not_break_a_message();
    test_midi_sysex_is_skipped_and_cancels_running_status();
    test_midi_ignores_orphan_data_and_unhandled_types();
    test_midi_control_change_moves_a_parameter();
    test_midi_all_notes_off_works_whatever_the_map();
    test_midi_pitch_bend();
    test_pitch_bend_applies_to_later_notes();
    test_frame_time_tracks_rendering();
    test_scheduled_note_lands_on_its_own_frame();
    test_several_events_split_one_block();
    test_late_event_is_played_not_dropped();
    test_queue_reports_when_full();
    test_scheduled_param_and_all_notes_off();
    test_splitting_a_block_does_not_change_the_audio();
    test_events_are_applied_in_order();
    test_instances_do_not_share_parameters();
    test_instances_do_not_share_voices();
    test_instances_keep_their_clocks_in_step();
    test_a_new_instance_starts_its_clock_at_zero();
    test_a_late_instance_can_join_the_timeline();
    test_instances_do_not_share_scheduled_events();
    test_summed_instances_need_host_headroom();
    test_render_add_mixes_instances();
    test_render_add_mixes_across_event_splits();
    test_render_add_keeps_what_is_already_in_the_buffer();
    test_render_still_overwrites_the_buffer();
    test_a_silent_part_costs_the_mix_nothing();
    test_render_add_advances_the_clock_like_render();
    test_lfo_rate_is_in_hertz();
    test_lfo_shapes();
    test_lfo_defaults_change_nothing();
    test_lfo_wobbles_the_cutoff();
    test_lfo_gives_vibrato();
    test_lfo_tremolo_only_dips();
    test_lfo_retriggers_with_the_note();
    test_noise_bandwidth_follows_the_note();
    test_noise_is_interpolated_not_stepped();
    test_noise_is_centred_and_bounded();
    test_noise_is_deterministic_but_differs_per_voice();
    test_noise_seed_never_gets_stuck();
    test_patch_round_trips();
    test_patch_load_clamps_and_survives_rubbish();
    test_patches_keep_instances_apart();
    test_pitch_env_sweeps_the_note_down();
    test_pitch_env_amount_is_bipolar();
    test_pitch_env_decay_sets_the_sweep_length();
    test_pitch_env_defaults_change_nothing();
    test_new_parameters_are_appended();
    test_a_short_patch_loads_at_its_defaults();
    test_a_sent_patch_loads_exactly_as_a_direct_load();
    test_applying_nothing_leaves_the_engine_alone();
    test_two_waiting_patches_collapse_to_the_newer();
    test_a_third_patch_in_one_block_is_refused();
    test_a_short_patch_through_the_queue_takes_the_defaults();
    test_the_patch_queue_survives_its_counters_wrapping();
    test_pitch_env_restarts_on_a_retrigger();
    test_centring_the_amount_puts_the_note_back();
    test_a_modulation_nothing_hears_changes_nothing();
    test_turning_a_depth_up_mid_note_wakes_the_lfo();
    test_a_filter_nothing_modulates_is_left_alone();
    test_a_parameter_change_reaches_everything_it_should();
    test_a_note_played_after_an_edit_matches_one_playing_during_it();
    test_an_envelope_retimes_itself_for_a_new_rate();
    test_envelope_times_survive_a_sample_rate_change();
    test_glide_starts_a_note_on_the_previous_pitch();
    test_glide_time_sets_how_long_the_travel_takes();
    test_no_glide_by_default();
#if SYNTH_MAX_VOICES > 1
    test_a_glide_belongs_to_its_own_voice();
#endif
    test_delay_repeats_after_its_time();
    test_delay_feedback_decays();
    test_delay_is_transparent_at_zero_mix();
    test_delay_stays_bounded_under_runaway_feedback();
    test_delay_time_changes_do_not_click();
    test_delay_reads_between_samples();
    test_delay_arrives_at_a_time_it_is_changed_to();
    test_delay_without_a_buffer_is_a_bypass();

    if (g_failures == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_failures);
    return 1;
}
