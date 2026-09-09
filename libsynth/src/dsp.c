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

void synth_osc_reset(synth_osc_t *osc)
{
    osc->phase = 0.0f;
    osc->phase_inc = 0.0f;
}

void synth_osc_set_freq(synth_osc_t *osc, float hz, float sample_rate)
{
    float inc = (sample_rate > 0.0f) ? hz / sample_rate : 0.0f;

    if (inc < 0.0f) {
        inc = 0.0f;
    } else if (inc > 0.49f) {
        inc = 0.49f;
    }
    osc->phase_inc = inc;
}

float synth_osc_next(synth_osc_t *osc, synth_wave_t wave)
{
    float phase = osc->phase;
    float out;

    switch (wave) {
    case SYNTH_WAVE_SAW:
        out = 2.0f * phase - 1.0f;
        break;
    case SYNTH_WAVE_SQUARE:
        out = (phase < 0.5f) ? 1.0f : -1.0f;
        break;
    case SYNTH_WAVE_SINE:
    default:
        out = -synth_sin_pi(2.0f * phase - 1.0f);
        break;
    }

    phase += osc->phase_inc;
    if (phase >= 1.0f) {
        phase -= 1.0f;
    }
    osc->phase = phase;

    return out;
}

void synth_env_init(synth_env_t *env, float sample_rate)
{
    env->delay = 0.0f;
    env->attack = 0.01f;
    env->hold = 0.0f;
    env->decay = 0.2f;
    env->sustain = 0.5f;
    env->release = 0.3f;

    env->sample_rate = sample_rate;
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
    const float dt = (env->sample_rate > 0.0f) ? 1.0f / env->sample_rate : 0.0f;
    float t;

    switch (env->stage) {
    case SYNTH_ENV_IDLE:
        env->level = 0.0f;
        break;

    case SYNTH_ENV_DELAY:
        env->level = 0.0f;
        env->time += dt;
        if (env->time >= env->delay) {
            env->time = 0.0f;
            env->stage = SYNTH_ENV_ATTACK;
        }
        break;

    case SYNTH_ENV_ATTACK:
        env->time += dt;
        env->level = (env->attack > 0.0f) ? env->time / env->attack : 1.0f;
        if (env->level >= 1.0f) {
            env->level = 1.0f;
            env->time = 0.0f;
            env->stage = SYNTH_ENV_HOLD;
        }
        break;

    case SYNTH_ENV_HOLD:
        env->level = 1.0f;
        env->time += dt;
        if (env->time >= env->hold) {
            env->time = 0.0f;
            env->stage = SYNTH_ENV_DECAY;
        }
        break;

    case SYNTH_ENV_DECAY:
        env->time += dt;
        t = (env->decay > 0.0f) ? env->time / env->decay : 1.0f;
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
        env->time += dt;
        t = (env->release > 0.0f) ? env->time / env->release : 1.0f;
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
