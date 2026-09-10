#include "synth/synth.h"

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
    osc.wave = wave;
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
    osc.wave = SYNTH_WAVE_PD;
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
    osc.wave = SYNTH_WAVE_PD;
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
        osc.wave = SYNTH_WAVE_PD;
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
    osc.wave = SYNTH_WAVE_VOSIM;
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
            osc.wave = SYNTH_WAVE_TERRAIN;
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
    osc.wave = SYNTH_WAVE_TERRAIN;
    synth_osc_set_terrain(&osc, 0.15f, 1);
    synth_osc_set_freq(&osc, 200.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        narrow[i] = synth_osc_next(&osc);
    }

    synth_osc_init(&osc, SR);
    osc.wave = SYNTH_WAVE_TERRAIN;
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

static void test_terrain_ratio_changes_the_waveform(void)
{
    static float a[ALIAS_FRAMES];
    static float b[ALIAS_FRAMES];
    synth_osc_t osc;
    float diff = 0.0f;
    int i;

    /* A Lissajous orbit closes on a different path across the same surface. */
    synth_osc_init(&osc, SR);
    osc.wave = SYNTH_WAVE_TERRAIN;
    synth_osc_set_terrain(&osc, 0.8f, 1);
    synth_osc_set_freq(&osc, 200.0f);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        a[i] = synth_osc_next(&osc);
    }

    synth_osc_init(&osc, SR);
    osc.wave = SYNTH_WAVE_TERRAIN;
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
    CHECK(synth_active_voices(&s) == 4);

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
    env.delay = 0.0f;
    env.attack = 0.1f;
    env.hold = 0.0f;
    env.decay = 0.1f;
    env.sustain = 0.4f;
    env.release = 0.1f;

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

int main(void)
{
    test_note_to_hz();
    test_sine_accuracy();
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
    test_terrain_is_centred_and_bounded();
    test_terrain_radius_changes_the_spectrum();
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

    if (g_failures == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_failures);
    return 1;
}
