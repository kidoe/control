#include "synth/dsp.h"

/* Polynomial sine, ~0.1% error, so the core needs no libm and no wavetable RAM.
   Argument is the angle in units of pi, over [-1, 1]. */
static float synth_sin_pi(float p)
{
    const float refine = 0.225f;

    /* 4p - 4p|p|: the usual 4/pi and 4/pi^2 constants, folded for an argument
       measured in units of pi rather than radians. */
    float y = 4.0f * p * (1.0f - (p < 0.0f ? -p : p));
    return refine * (y * (y < 0.0f ? -y : y) - y) + y;
}

float synth_exp2f(float x)
{
    union { float f; unsigned u; } pow2i;
    float xi, frac, poly;

    if (x < -126.0f) {
        return 0.0f;
    }
    if (x > 126.0f) {
        x = 126.0f;
    }

    xi = (float)(int)x;
    if (xi > x) {
        xi -= 1.0f; /* (int) truncates toward zero, we need floor */
    }
    frac = x - xi;

    poly = 1.0f + frac * (0.6931472f +
           frac * (0.2402265f +
           frac * (0.0555041f +
           frac *  0.0096181f)));

    pow2i.u = (unsigned)(((int)xi + 127) << 23);
    return poly * pow2i.f;
}

/* Polynomial approximation of a band-limited step. A naive saw or square jumps
   between two samples, which scatters energy all over the spectrum; this adds a
   two-sample correction either side of each jump and folds most of that energy
   back where it belongs. Returns 0 away from a discontinuity, and safely 0 when
   phase_inc is 0. */
static float poly_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* sin(2*pi*turns), with turns wrapped into one revolution. */
static float sin_turn(float turns)
{
    turns -= (float)(int)turns;
    if (turns < 0.0f) {
        turns += 1.0f;
    }
    return -synth_sin_pi(2.0f * turns - 1.0f);
}

/* The terrain itself: z = (x-y)(x^2-1)(y^2-1), the surface Roads uses to
   introduce the technique. The orbit's radius picks out a different cross
   section of it, and each cross section has its own spectrum. */
static float terrain_height(float x, float y)
{
    return (x - y) * (x * x - 1.0f) * (y * y - 1.0f);
}

static void terrain_point(const synth_osc_t *osc, float phase, float *x, float *y)
{
    *x = osc->terrain_radius * sin_turn(phase + 0.25f);
    *y = osc->terrain_radius * sin_turn(phase * (float)osc->terrain_ratio);
}

/* An arbitrary surface has no reason to be centred or to peak at 1, and both
   depend on the orbit. Walking one lap at setup time is cheaper than guessing,
   and keeps this waveform as well behaved as the others.

   One lap, not two. The mean is not known until the lap ends, so finding the
   largest |z - dc| looks like it needs a second pass — but that maximum is
   reached at an extreme of z, so the two extremes are all the second pass would
   have been looking for, and they cost a comparison each on the way past. */
static void terrain_update(synth_osc_t *osc)
{
    const int steps = 256;
    float sum = 0.0f;
    float lowest = 0.0f;
    float highest = 0.0f;
    float peak;
    float above;
    float below;
    float x, y, z;
    int i;

    for (i = 0; i < steps; ++i) {
        terrain_point(osc, (float)i / (float)steps, &x, &y);
        z = terrain_height(x, y);
        sum += z;
        if (i == 0 || z < lowest) {
            lowest = z;
        }
        if (i == 0 || z > highest) {
            highest = z;
        }
    }
    osc->terrain_dc = sum / (float)steps;

    above = highest - osc->terrain_dc;
    below = osc->terrain_dc - lowest;
    peak = (above > below) ? above : below;
    osc->terrain_scale = (peak > 1e-6f) ? 1.0f / peak : 0.0f;
}

/* VOSIM packs a burst of pulses into each period of the fundamental, so how many
   fit and how much mean level they carry both follow from the pitch. Recomputed
   whenever either the pitch or the pulse settings move. */
static void vosim_update(synth_osc_t *osc)
{
    float f0 = osc->phase_inc * osc->sample_rate;
    float width;
    float sum = 0.0f;
    float term = 1.0f;
    int fits;
    int n;

    if (f0 <= 0.0f || osc->formant_hz <= 0.0f) {
        osc->pulse_rate = 0.0f;
        osc->vosim_fitting = 0;
        osc->vosim_dc = 0.0f;
        return;
    }

    /* One pulse lasts 1/formant seconds, which is this fraction of a period. */
    width = f0 / osc->formant_hz;
    if (width > 1.0f) {
        width = 1.0f;
    }

    fits = (int)(1.0f / width);
    if (fits < 1) {
        fits = 1;
    }
    osc->vosim_fitting = (osc->vosim_pulses < fits) ? osc->vosim_pulses : fits;
    if (osc->vosim_fitting < 1) {
        osc->vosim_fitting = 1;
    }

    for (n = 0; n < osc->vosim_fitting; ++n) {
        sum += term;
        term *= osc->vosim_decay;
    }

    osc->pulse_rate = 1.0f / width;
    osc->vosim_dc = 0.5f * width * sum; /* sin^2 averages 0.5 across its pulse */
}

static void pd_update(synth_osc_t *osc);

/* xorshift32: three shifts and three xors, no multiply, which matters on a core
   that has no multiplier worth the name. */
static float noise_draw(synth_osc_t *osc)
{
    unsigned x = osc->noise_state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    osc->noise_state = x;

    return (float)(int)x * (1.0f / 2147483648.0f);
}

void synth_osc_set_noise_seed(synth_osc_t *osc, unsigned seed)
{
    osc->noise_state = seed ? seed : 1u; /* xorshift is stuck at zero */
    osc->noise_from = noise_draw(osc);
    osc->noise_to = noise_draw(osc);
}

void synth_osc_init(synth_osc_t *osc, float sample_rate)
{
    osc->sample_rate = (sample_rate > 0.0f) ? sample_rate : 44100.0f;
    osc->phase = 0.0f;
    osc->phase_inc = 0.0f;
    osc->formant_hz = 800.0f;
    osc->vosim_decay = 0.6f;
    osc->vosim_pulses = 3;
    osc->wave = SYNTH_WAVE_SINE;
    osc->pd_wanted = 0.5f;
    /* Outside the clamped range either control can take, so the first real
       call below cannot mistake this for a setting already in force. */
    osc->terrain_radius = 0.0f;
    osc->terrain_ratio = 0;
    synth_osc_set_pd_knee(osc, 0.5f);
    synth_osc_set_terrain(osc, 0.7f, 1);
    synth_osc_set_noise_seed(osc, 0x9E3779B9u);
    vosim_update(osc);
}

void synth_osc_reset(synth_osc_t *osc)
{
    osc->phase = 0.0f;
}

void synth_osc_set_freq(synth_osc_t *osc, float hz)
{
    float inc = (osc->sample_rate > 0.0f) ? hz / osc->sample_rate : 0.0f;

    if (inc < 0.0f) {
        inc = 0.0f;
    } else if (inc > 0.49f) {
        inc = 0.49f;
    }
    osc->phase_inc = inc;
    vosim_update(osc);
    pd_update(osc); /* the usable knee depends on the pitch */
}

/* The warp squeezes a half cycle of sine into the fraction of the period before
   the knee, so the fastest frequency it generates is roughly f0 / knee. Past
   Nyquist that folds back as aliasing, and no correction at the corner can help:
   at 1.5 kHz with a knee of 0.06 the fast segment covers half a sine in under
   two samples, which is simply not representable. So the knee is widened as the
   pitch rises. High notes lose some brightness, which is what a band-limited
   instrument does; the alternative is that they gain a spray of inharmonic
   tones, which is not. */
static void pd_update(synth_osc_t *osc)
{
    float knee = osc->pd_wanted;
    float floor_knee = 6.0f * osc->phase_inc;
    float dc;
    float scale;

    if (floor_knee > 0.5f) {
        floor_knee = 0.5f; /* never duller than the plain sine the knee starts as */
    }
    if (knee < floor_knee) {
        knee = floor_knee;
    }
    osc->pd_knee = knee;
    osc->pd_rise = 0.5f / knee;
    osc->pd_fall = 0.5f / (1.0f - knee);

    /* Bending the phase squeezes the positive half-cycle into a fraction `knee`
       of the period and stretches the negative half over the rest, which leaves
       a mean of (2/pi)(2*knee - 1). Left in, it would ride the knee as a moving
       DC offset. Removing it pushes the peak past 1, so rescale to keep the
       oscillator's output bounded like every other waveform here. */
    dc = 0.63661977f * (2.0f * knee - 1.0f);
    scale = 1.0f / (1.0f + ((dc < 0.0f) ? -dc : dc));
    osc->pd_scale = scale;
    osc->pd_offset = dc * scale;
}

void synth_osc_set_pd_knee(synth_osc_t *osc, float knee)
{
    if (knee < 0.02f) {
        knee = 0.02f;
    } else if (knee > 0.98f) {
        knee = 0.98f;
    }
    osc->pd_wanted = knee;
    pd_update(osc);
}

void synth_osc_set_vosim(synth_osc_t *osc, float formant_hz, int pulses, float decay)
{
    if (pulses < 1) {
        pulses = 1;
    } else if (pulses > SYNTH_VOSIM_MAX_PULSES) {
        pulses = SYNTH_VOSIM_MAX_PULSES;
    }
    if (decay < 0.0f) {
        decay = 0.0f;
    } else if (decay > 1.0f) {
        decay = 1.0f;
    }

    osc->formant_hz = (formant_hz > 0.0f) ? formant_hz : 1.0f;
    osc->vosim_pulses = pulses;
    osc->vosim_decay = decay;
    vosim_update(osc);
}

void synth_osc_set_terrain(synth_osc_t *osc, float radius, int ratio)
{
    if (radius < 0.05f) {
        radius = 0.05f;
    } else if (radius > 1.0f) {
        radius = 1.0f;
    }
    if (ratio < 1) {
        ratio = 1;
    } else if (ratio > 8) {
        ratio = 8;
    }

    /* Walking the orbit costs about as much as a third of a block of audio for
       eight voices, and it depends on nothing but these two numbers. Setting
       them to what they already are is the common case — every note_on
       reapplies the whole voice — so it is worth one comparison to find out. */
    if (radius == osc->terrain_radius && ratio == osc->terrain_ratio) {
        return;
    }

    osc->terrain_radius = radius;
    osc->terrain_ratio = ratio;
    terrain_update(osc);
}

void synth_osc_share_terrain(synth_osc_t *osc, const synth_osc_t *from)
{
    if (osc->terrain_radius == from->terrain_radius
        && osc->terrain_ratio == from->terrain_ratio) {
        return;
    }
    osc->terrain_radius = from->terrain_radius;
    osc->terrain_ratio = from->terrain_ratio;
    osc->terrain_scale = from->terrain_scale;
    osc->terrain_dc = from->terrain_dc;
}

float synth_osc_next(synth_osc_t *osc)
{
    float phase = osc->phase;
    float dt = osc->phase_inc;
    float half;
    float warped;
    float pulse;
    float amp;
    float tx;
    float ty;
    int index;
    int i;
    float out;

    switch (osc->wave) {
    case SYNTH_WAVE_SAW:
        out = 2.0f * phase - 1.0f;
        out -= poly_blep(phase, dt);
        break;
    case SYNTH_WAVE_SQUARE:
        half = phase + 0.5f;
        if (half >= 1.0f) {
            half -= 1.0f;
        }
        out = (phase < 0.5f) ? 1.0f : -1.0f;
        out += poly_blep(phase, dt);  /* rising edge at 0 */
        out -= poly_blep(half, dt);   /* falling edge at 0.5 */
        break;

    /* Phase distortion: the sine is untouched, the clock that reads it is not.
       The phase races through the first half-cycle and crawls through the
       second, which grows upper harmonics without any filter. */
    case SYNTH_WAVE_PD:
        warped = (phase < osc->pd_knee)
                     ? phase * osc->pd_rise
                     : 0.5f + (phase - osc->pd_knee) * osc->pd_fall;
        out = -synth_sin_pi(2.0f * warped - 1.0f) * osc->pd_scale - osc->pd_offset;
        break;

    /* VOSIM: a burst of sin^2 pulses, each quieter than the last, then silence
       until the period ends. Pulse duration sets the formant, the period sets
       the pitch, and the two are independent. */
    case SYNTH_WAVE_VOSIM:
        index = (int)(phase * osc->pulse_rate);
        if (index >= osc->vosim_fitting) {
            out = -osc->vosim_dc;
        } else {
            pulse = synth_sin_pi(phase * osc->pulse_rate - (float)index);
            amp = 1.0f;
            for (i = 0; i < index; ++i) {
                amp *= osc->vosim_decay;
            }
            out = pulse * pulse * amp - osc->vosim_dc;
        }
        break;

    /* Wave terrain: the phase drives a closed orbit across a fixed surface and
       the height under it is the sample. Widening the orbit or making it a
       Lissajous figure changes the timbre in ways that have no description in
       terms of harmonics. */
    case SYNTH_WAVE_TERRAIN:
        terrain_point(osc, phase, &tx, &ty);
        out = (terrain_height(tx, ty) - osc->terrain_dc) * osc->terrain_scale;
        break;

    case SYNTH_WAVE_NOISE:
        out = osc->noise_from + (osc->noise_to - osc->noise_from) * phase;
        break;
    case SYNTH_WAVE_SINE:
    default:
        out = -synth_sin_pi(2.0f * phase - 1.0f);
        break;
    }

    phase += osc->phase_inc;
    if (phase >= 1.0f) {
        phase -= 1.0f;
        osc->noise_from = osc->noise_to;
        osc->noise_to = noise_draw(osc);
    }
    osc->phase = phase;

    return out;
}

void synth_lfo_init(synth_lfo_t *lfo, float sample_rate, unsigned seed)
{
    lfo->sample_rate = (sample_rate > 0.0f) ? sample_rate : 44100.0f;
    lfo->phase = 0.0f;
    lfo->phase_inc = 0.0f;
    lfo->random_state = seed ? seed : 1u;
    lfo->random_value = 0.0f;
    lfo->shape = SYNTH_LFO_SINE;
}

/* frames_per_step is how many samples pass between calls to synth_lfo_next, so
   the rate stays in hertz however coarsely the host chooses to run it. */
void synth_lfo_set_rate(synth_lfo_t *lfo, float hz, int frames_per_step)
{
    float inc;

    if (hz < 0.0f) {
        hz = 0.0f;
    }
    if (frames_per_step < 1) {
        frames_per_step = 1;
    }

    inc = hz * (float)frames_per_step / lfo->sample_rate;
    if (inc > 0.49f) {
        inc = 0.49f;
    }
    lfo->phase_inc = inc;
}

void synth_lfo_retrigger(synth_lfo_t *lfo)
{
    lfo->phase = 0.0f;
}

float synth_lfo_next(synth_lfo_t *lfo)
{
    float phase = lfo->phase;
    float out;

    switch (lfo->shape) {
    case SYNTH_LFO_TRIANGLE:
        out = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
        break;
    case SYNTH_LFO_SQUARE:
        out = (phase < 0.5f) ? 1.0f : -1.0f;
        break;
    case SYNTH_LFO_RANDOM:
        out = lfo->random_value;
        break;
    case SYNTH_LFO_SINE:
    default:
        out = -synth_sin_pi(2.0f * phase - 1.0f);
        break;
    }

    phase += lfo->phase_inc;
    if (phase >= 1.0f) {
        unsigned x = lfo->random_state;

        phase -= 1.0f;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        lfo->random_state = x;
        lfo->random_value = (float)(int)x * (1.0f / 2147483648.0f);
    }
    lfo->phase = phase;

    return out;
}

/* A time of zero means the stage is over in one sample, which is what a rate of
   1 gives: the ramp's progress runs from 0 to 1 whatever it measures. */
static float env_rate(float dt, float seconds)
{
    return (seconds > 0.0f) ? (dt / seconds) : 1.0f;
}

static void env_derive(synth_env_t *env)
{
    env->dt = (env->sample_rate > 0.0f) ? (1.0f / env->sample_rate) : 0.0f;
    env->attack_rate = env_rate(env->dt, env->attack);
    env->decay_rate = env_rate(env->dt, env->decay);
    env->release_rate = env_rate(env->dt, env->release);
}

void synth_env_set_times(synth_env_t *env, float delay, float attack, float hold,
                         float decay, float sustain, float release)
{
    env->delay = delay;
    env->attack = attack;
    env->hold = hold;
    env->decay = decay;
    env->sustain = sustain;
    env->release = release;
    env_derive(env);
}

void synth_env_set_sample_rate(synth_env_t *env, float sample_rate)
{
    env->sample_rate = sample_rate;
    env_derive(env);
}

void synth_env_init(synth_env_t *env, float sample_rate)
{
    env->sample_rate = sample_rate;
    synth_env_set_times(env, 0.0f, 0.01f, 0.0f, 0.2f, 0.5f, 0.3f);

    env->level = 0.0f;
    env->time = 0.0f;
    env->release_from = 0.0f;
    env->stage = SYNTH_ENV_IDLE;
}

void synth_env_gate_on(synth_env_t *env)
{
    env->time = 0.0f;
    env->level = 0.0f;
    env->stage = SYNTH_ENV_DELAY;
}

void synth_env_gate_off(synth_env_t *env)
{
    if (env->stage == SYNTH_ENV_IDLE) {
        return;
    }
    env->release_from = env->level;
    env->time = 0.0f;
    env->stage = SYNTH_ENV_RELEASE;
}

float synth_env_next(synth_env_t *env)
{
    float t;

    switch (env->stage) {
    case SYNTH_ENV_IDLE:
        env->level = 0.0f;
        break;

    case SYNTH_ENV_DELAY:
        env->level = 0.0f;
        env->time += env->dt;
        if (env->time >= env->delay) {
            env->time = 0.0f;
            env->stage = SYNTH_ENV_ATTACK;
        }
        break;

    case SYNTH_ENV_ATTACK:
        env->time += env->attack_rate;
        env->level = env->time;
        if (env->level >= 1.0f) {
            env->level = 1.0f;
            env->time = 0.0f;
            env->stage = SYNTH_ENV_HOLD;
        }
        break;

    case SYNTH_ENV_HOLD:
        env->level = 1.0f;
        env->time += env->dt;
        if (env->time >= env->hold) {
            env->time = 0.0f;
            env->stage = SYNTH_ENV_DECAY;
        }
        break;

    case SYNTH_ENV_DECAY:
        env->time += env->decay_rate;
        t = env->time;
        if (t >= 1.0f) {
            t = 1.0f;
            env->time = 0.0f;
            env->stage = SYNTH_ENV_SUSTAIN;
        }
        env->level = 1.0f + t * (env->sustain - 1.0f);
        break;

    case SYNTH_ENV_SUSTAIN:
        env->level = env->sustain;
        break;

    case SYNTH_ENV_RELEASE:
        env->time += env->release_rate;
        t = env->time;
        if (t >= 1.0f) {
            env->level = 0.0f;
            env->time = 0.0f;
            env->stage = SYNTH_ENV_IDLE;
        } else {
            env->level = env->release_from * (1.0f - t);
        }
        break;
    }

    return env->level;
}

void synth_amp_init(synth_amp_t *amp)
{
    amp->drive = 0.0f;
    amp->level = 1.0f;
}

/* Soft saturation as x(1+d)/(1+d|x|). Picked over the usual cubic or tanh
   because it is exactly the identity at d = 0, so "clean" really is clean, and
   because |x| <= 1 guarantees |y| <= 1 for any drive: full scale in stays full
   scale out however hard it is pushed, with no branch and no clamp. */
float synth_amp_shape(float x, float drive)
{
    float magnitude;

    /* At zero drive the whole expression collapses to x. Saying so costs a
       comparison and saves a divide, which on a core without an FPU is two
       hundred instructions per sample per voice for the patches — most of
       them — that never asked to be driven. */
    if (drive <= 0.0f) {
        return x;
    }
    magnitude = (x < 0.0f) ? -x : x;

    return x * (1.0f + drive) / (1.0f + drive * magnitude);
}

float synth_amp_next(const synth_amp_t *amp, float in, float env_level)
{
    return synth_amp_shape(in * env_level * amp->level, amp->drive);
}

/* Pade approximant of tan, exact to ~1e-5 over [0, pi/2), which is the whole
   usable cutoff range. Keeps the filter's prewarping libm-free. */
static float synth_tan_pade(float x)
{
    float x2 = x * x;
    float num = 945.0f + x2 * (-105.0f + x2);
    float den = 945.0f + x2 * (-420.0f + x2 * 15.0f);

    return x * num / den;
}

void synth_filter_init(synth_filter_t *filter, float sample_rate)
{
    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : 44100.0f;
    filter->ic1eq = 0.0f;
    filter->ic2eq = 0.0f;
    synth_filter_set(filter, 1000.0f, 0.707f);
}

void synth_filter_set(synth_filter_t *filter, float cutoff_hz, float q)
{
    const float max_hz = 0.49f * filter->sample_rate;
    float g;

    if (cutoff_hz < 1.0f) {
        cutoff_hz = 1.0f;
    } else if (cutoff_hz > max_hz) {
        cutoff_hz = max_hz;
    }
    if (q < 0.5f) {
        q = 0.5f;
    } else if (q > 40.0f) {
        q = 40.0f;
    }

    g = synth_tan_pade(3.14159265f * cutoff_hz / filter->sample_rate);
    filter->k = 1.0f / q;
    filter->a1 = 1.0f / (1.0f + g * (g + filter->k));
    filter->a2 = g * filter->a1;
    filter->a3 = g * filter->a2;
}

void synth_filter_reset(synth_filter_t *filter)
{
    filter->ic1eq = 0.0f;
    filter->ic2eq = 0.0f;
}

float synth_filter_next(synth_filter_t *filter, float in, synth_filter_mode_t mode)
{
    float v3 = in - filter->ic2eq;
    float v1 = filter->a1 * filter->ic1eq + filter->a2 * v3;
    float v2 = filter->ic2eq + filter->a2 * filter->ic1eq + filter->a3 * v3;

    filter->ic1eq = 2.0f * v1 - filter->ic1eq;
    filter->ic2eq = 2.0f * v2 - filter->ic2eq;

    switch (mode) {
    case SYNTH_FILTER_HIGHPASS:
        return in - filter->k * v1 - v2;
    case SYNTH_FILTER_BANDPASS:
        return v1;
    case SYNTH_FILTER_LOWPASS:
    default:
        return v2;
    }
}
