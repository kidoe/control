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

    synth_osc_reset(&osc);
    synth_osc_set_freq(&osc, SR / 1024.0f, SR);

    for (i = 0; i < 1024; ++i) {
        float got = synth_osc_next(&osc, SYNTH_WAVE_SINE);
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

    synth_osc_reset(&osc);
    synth_osc_set_freq(&osc, freq, SR);
    for (i = 0; i < ALIAS_FRAMES; ++i) {
        buf[i] = synth_osc_next(&osc, wave);
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

static void test_oscillator_frequency(void)
{
    synth_osc_t osc;
    int crossings = 0;
    float prev = 0.0f;
    int i;

    synth_osc_reset(&osc);
    synth_osc_set_freq(&osc, 440.0f, SR);

    for (i = 0; i < (int)SR; ++i) {
        float v = synth_osc_next(&osc, SYNTH_WAVE_SINE);

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
    synth_osc_reset(&osc);
    synth_osc_set_freq(&osc, freq, SR);

    for (i = 0; i < (int)(SR * 0.2f); ++i) {
        synth_filter_next(&filter, synth_osc_next(&osc, SYNTH_WAVE_SINE), mode);
    }
    for (i = 0; i < (int)(SR * 0.1f); ++i) {
        float v = fabsf(synth_filter_next(&filter, synth_osc_next(&osc, SYNTH_WAVE_SINE), mode));
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
    synth_osc_reset(&osc);
    synth_osc_set_freq(&osc, 3000.0f, SR);

    for (i = 0; i < (int)(SR * 2.0f); ++i) {
        float v = synth_filter_next(&filter, synth_osc_next(&osc, SYNTH_WAVE_SQUARE), SYNTH_FILTER_LOWPASS);
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
    CHECK_NEAR(synth_param_denorm(SYNTH_PARAM_OSC_WAVE, 0.5f), 1.0f, 1e-6f);
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
