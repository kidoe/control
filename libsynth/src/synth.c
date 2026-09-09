#include "synth/synth.h"

#if SYNTH_PARAM_NAMES
#define SYNTH_PARAM_ENTRY(name, min, max, def, curve) { name, min, max, def, curve }
#else
#define SYNTH_PARAM_ENTRY(name, min, max, def, curve) { min, max, def, curve }
#endif

static const synth_param_info_t k_param_info[SYNTH_PARAM_COUNT] = {
    SYNTH_PARAM_ENTRY("master_gain", 0.0f, 1.0f, 0.8f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("osc_wave", 0.0f, (float)(SYNTH_WAVE_COUNT - 1), 0.0f, SYNTH_CURVE_STEPPED),
    SYNTH_PARAM_ENTRY("amp_delay", 0.0f, 2.0f, 0.0f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("amp_attack", 0.001f, 8.0f, 0.2f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("amp_hold", 0.0f, 2.0f, 0.0f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("amp_decay", 0.001f, 8.0f, 0.35f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("amp_sustain", 0.0f, 1.0f, 0.5f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("amp_release", 0.001f, 8.0f, 0.35f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("filter_mode", 0.0f, (float)(SYNTH_FILTER_COUNT - 1), 0.0f, SYNTH_CURVE_STEPPED),
    SYNTH_PARAM_ENTRY("filter_cutoff", 20.0f, 18000.0f, 1.0f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("filter_q", 0.5f, 15.0f, 0.0f, SYNTH_CURVE_LINEAR)
};

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    return (v > 1.0f) ? 1.0f : v;
}

static int param_is_valid(synth_param_t param)
{
    return (int)param >= 0 && (int)param < SYNTH_PARAM_COUNT;
}

const synth_param_info_t *synth_param_info(synth_param_t param)
{
    return param_is_valid(param) ? &k_param_info[param] : 0;
}

float synth_param_denorm(synth_param_t param, float norm)
{
    const synth_param_info_t *info = synth_param_info(param);
    float shaped;

    if (!info) {
        return 0.0f;
    }
    norm = clamp01(norm);

    switch (info->curve) {
    case SYNTH_CURVE_CUBIC:
        shaped = norm * norm * norm;
        break;
    case SYNTH_CURVE_STEPPED:
        return (float)(int)(info->min + norm * (info->max - info->min) + 0.5f);
    case SYNTH_CURVE_LINEAR:
    default:
        shaped = norm;
        break;
    }

    return info->min + shaped * (info->max - info->min);
}

float synth_note_to_hz(float note)
{
    return 440.0f * synth_exp2f((note - 69.0f) * (1.0f / 12.0f));
}

static void voice_apply_envelope(synth_t *s, synth_voice_t *v)
{
    v->env.delay = synth_param_denorm(SYNTH_PARAM_AMP_DELAY, s->params[SYNTH_PARAM_AMP_DELAY]);
    v->env.attack = synth_param_denorm(SYNTH_PARAM_AMP_ATTACK, s->params[SYNTH_PARAM_AMP_ATTACK]);
    v->env.hold = synth_param_denorm(SYNTH_PARAM_AMP_HOLD, s->params[SYNTH_PARAM_AMP_HOLD]);
    v->env.decay = synth_param_denorm(SYNTH_PARAM_AMP_DECAY, s->params[SYNTH_PARAM_AMP_DECAY]);
    v->env.sustain = synth_param_denorm(SYNTH_PARAM_AMP_SUSTAIN, s->params[SYNTH_PARAM_AMP_SUSTAIN]);
    v->env.release = synth_param_denorm(SYNTH_PARAM_AMP_RELEASE, s->params[SYNTH_PARAM_AMP_RELEASE]);
}

static void voice_apply_filter(synth_t *s, synth_voice_t *v)
{
    synth_filter_set(&v->filter,
                     synth_param_denorm(SYNTH_PARAM_FILTER_CUTOFF, s->params[SYNTH_PARAM_FILTER_CUTOFF]),
                     synth_param_denorm(SYNTH_PARAM_FILTER_Q, s->params[SYNTH_PARAM_FILTER_Q]));
}

void synth_init(synth_t *s, float sample_rate)
{
    int i;

    s->sample_rate = (sample_rate > 0.0f) ? sample_rate : 44100.0f;
    s->age_counter = 0;

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        s->params[i] = k_param_info[i].default_norm;
    }

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        synth_osc_reset(&v->osc);
        synth_env_init(&v->env, s->sample_rate);
        synth_filter_init(&v->filter, s->sample_rate);
        voice_apply_envelope(s, v);
        voice_apply_filter(s, v);
        v->note = -1;
        v->velocity = 0.0f;
        v->age = 0;
    }
}

void synth_reset(synth_t *s)
{
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        synth_osc_reset(&v->osc);
        synth_filter_reset(&v->filter);
        v->env.stage = SYNTH_ENV_IDLE;
        v->env.level = 0.0f;
        v->env.time = 0.0f;
        v->note = -1;
        v->velocity = 0.0f;
        v->age = 0;
    }
    s->age_counter = 0;
}

static synth_voice_t *allocate_voice(synth_t *s, int note)
{
    synth_voice_t *oldest = &s->voices[0];
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        if (v->note == note && synth_env_is_active(&v->env)) {
            return v; /* retrigger rather than stacking a second voice */
        }
        if (!synth_env_is_active(&v->env)) {
            return v;
        }
        if (v->age < oldest->age) {
            oldest = v;
        }
    }

    return oldest;
}

void synth_note_on(synth_t *s, int note, float velocity)
{
    synth_voice_t *v = allocate_voice(s, note);

    v->note = note;
    v->velocity = clamp01(velocity);
    v->age = ++s->age_counter;

    synth_osc_reset(&v->osc);
    synth_osc_set_freq(&v->osc, synth_note_to_hz((float)note), s->sample_rate);
    synth_filter_reset(&v->filter); /* a stolen voice must not ring on into the new note */
    voice_apply_envelope(s, v);
    voice_apply_filter(s, v);
    synth_env_gate_on(&v->env);
}

void synth_note_off(synth_t *s, int note)
{
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        if (v->note == note) {
            v->note = -1;
            synth_env_gate_off(&v->env);
        }
    }
}

void synth_all_notes_off(synth_t *s)
{
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        s->voices[i].note = -1;
        synth_env_gate_off(&s->voices[i].env);
    }
}

int synth_active_voices(const synth_t *s)
{
    int count = 0;
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        if (synth_env_is_active(&s->voices[i].env)) {
            ++count;
        }
    }
    return count;
}

void synth_set_param(synth_t *s, synth_param_t param, float norm)
{
    int i;

    if (!param_is_valid(param)) {
        return;
    }
    s->params[param] = clamp01(norm);

    /* Edits reach sounding voices, as they did in the JSyn prototype. */
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        voice_apply_envelope(s, &s->voices[i]);
        voice_apply_filter(s, &s->voices[i]);
    }
}

float synth_get_param(const synth_t *s, synth_param_t param)
{
    return param_is_valid(param) ? s->params[param] : 0.0f;
}

void synth_render(synth_t *s, float *out, int n_frames)
{
    const float gain = synth_param_denorm(SYNTH_PARAM_MASTER_GAIN, s->params[SYNTH_PARAM_MASTER_GAIN]);
    const synth_wave_t wave =
        (synth_wave_t)synth_param_denorm(SYNTH_PARAM_OSC_WAVE, s->params[SYNTH_PARAM_OSC_WAVE]);
    const synth_filter_mode_t mode =
        (synth_filter_mode_t)synth_param_denorm(SYNTH_PARAM_FILTER_MODE, s->params[SYNTH_PARAM_FILTER_MODE]);
    int i, v;

    for (i = 0; i < n_frames; ++i) {
        float sum = 0.0f;

        for (v = 0; v < SYNTH_MAX_VOICES; ++v) {
            synth_voice_t *voice = &s->voices[v];
            float sample;

            if (!synth_env_is_active(&voice->env)) {
                continue;
            }
            sample = synth_osc_next(&voice->osc, wave) * synth_env_next(&voice->env) * voice->velocity;
            sum += synth_filter_next(&voice->filter, sample, mode);
        }

        out[i] = sum * gain;
    }
}
