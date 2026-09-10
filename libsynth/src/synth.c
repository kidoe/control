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
    SYNTH_PARAM_ENTRY("filter_q", 0.5f, 15.0f, 0.0f, SYNTH_CURVE_LINEAR),
    /* Runs downwards: 0 leaves the sine undistorted, 1 bends it hardest. */
    SYNTH_PARAM_ENTRY("pd_knee", 0.5f, 0.02f, 0.0f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("vosim_formant", 100.0f, 4000.0f, 0.6f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("vosim_pulses", 1.0f, 8.0f, 0.3f, SYNTH_CURVE_STEPPED),
    SYNTH_PARAM_ENTRY("vosim_decay", 0.0f, 1.0f, 0.6f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("terrain_radius", 0.05f, 1.0f, 0.7f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("terrain_ratio", 1.0f, 4.0f, 0.0f, SYNTH_CURVE_STEPPED),
    /* Bipolar, in octaves: the envelope can open or close the filter. */
    SYNTH_PARAM_ENTRY("filter_env_amount", -4.0f, 4.0f, 0.5f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("filter_env_attack", 0.001f, 8.0f, 0.15f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("filter_env_decay", 0.001f, 8.0f, 0.35f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("filter_env_sustain", 0.0f, 1.0f, 0.3f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("filter_env_release", 0.001f, 8.0f, 0.35f, SYNTH_CURVE_CUBIC),
    /* 1.0 tracks the keyboard exactly, so the filter follows the pitch. */
    SYNTH_PARAM_ENTRY("filter_key_track", 0.0f, 1.0f, 0.0f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("amp_drive", 0.0f, 12.0f, 0.0f, SYNTH_CURVE_CUBIC),
    /* 0 ignores how hard the key was struck, 1 gives it full range. */
    SYNTH_PARAM_ENTRY("amp_velocity", 0.0f, 1.0f, 1.0f, SYNTH_CURVE_LINEAR)
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
    v->amp_env.delay = synth_param_denorm(SYNTH_PARAM_AMP_DELAY, s->params[SYNTH_PARAM_AMP_DELAY]);
    v->amp_env.attack = synth_param_denorm(SYNTH_PARAM_AMP_ATTACK, s->params[SYNTH_PARAM_AMP_ATTACK]);
    v->amp_env.hold = synth_param_denorm(SYNTH_PARAM_AMP_HOLD, s->params[SYNTH_PARAM_AMP_HOLD]);
    v->amp_env.decay = synth_param_denorm(SYNTH_PARAM_AMP_DECAY, s->params[SYNTH_PARAM_AMP_DECAY]);
    v->amp_env.sustain = synth_param_denorm(SYNTH_PARAM_AMP_SUSTAIN, s->params[SYNTH_PARAM_AMP_SUSTAIN]);
    v->amp_env.release = synth_param_denorm(SYNTH_PARAM_AMP_RELEASE, s->params[SYNTH_PARAM_AMP_RELEASE]);
}

static void voice_apply_osc(synth_t *s, synth_voice_t *v)
{
    v->osc.wave = (synth_wave_t)synth_param_denorm(SYNTH_PARAM_OSC_WAVE, s->params[SYNTH_PARAM_OSC_WAVE]);
    synth_osc_set_pd_knee(&v->osc,
                          synth_param_denorm(SYNTH_PARAM_PD_KNEE, s->params[SYNTH_PARAM_PD_KNEE]));
    synth_osc_set_vosim(&v->osc,
                        synth_param_denorm(SYNTH_PARAM_VOSIM_FORMANT, s->params[SYNTH_PARAM_VOSIM_FORMANT]),
                        (int)synth_param_denorm(SYNTH_PARAM_VOSIM_PULSES, s->params[SYNTH_PARAM_VOSIM_PULSES]),
                        synth_param_denorm(SYNTH_PARAM_VOSIM_DECAY, s->params[SYNTH_PARAM_VOSIM_DECAY]));
    synth_osc_set_terrain(&v->osc,
                          synth_param_denorm(SYNTH_PARAM_TERRAIN_RADIUS, s->params[SYNTH_PARAM_TERRAIN_RADIUS]),
                          (int)synth_param_denorm(SYNTH_PARAM_TERRAIN_RATIO, s->params[SYNTH_PARAM_TERRAIN_RATIO]));
}

static void voice_apply_amp(synth_t *s, synth_voice_t *v)
{
    float sensitivity = synth_param_denorm(SYNTH_PARAM_AMP_VELOCITY, s->params[SYNTH_PARAM_AMP_VELOCITY]);

    v->amp.drive = synth_param_denorm(SYNTH_PARAM_AMP_DRIVE, s->params[SYNTH_PARAM_AMP_DRIVE]);
    v->amp.level = 1.0f - sensitivity + sensitivity * v->velocity;
}

/* The filter's cutoff is set in octaves so the envelope and the keyboard move it
   musically: an equal number of octaves sounds like an equal move wherever the
   base cutoff sits. */
static void voice_tune_filter(synth_t *s, synth_voice_t *v, float env_level)
{
    float base = synth_param_denorm(SYNTH_PARAM_FILTER_CUTOFF, s->params[SYNTH_PARAM_FILTER_CUTOFF]);
    float amount = synth_param_denorm(SYNTH_PARAM_FILTER_ENV_AMOUNT, s->params[SYNTH_PARAM_FILTER_ENV_AMOUNT]);
    float track = synth_param_denorm(SYNTH_PARAM_FILTER_KEY_TRACK, s->params[SYNTH_PARAM_FILTER_KEY_TRACK]);
    float q = synth_param_denorm(SYNTH_PARAM_FILTER_Q, s->params[SYNTH_PARAM_FILTER_Q]);
    float octaves = amount * env_level + track * ((float)v->note - 60.0f) * (1.0f / 12.0f);

    synth_filter_set(&v->filter, base * synth_exp2f(octaves), q);
}

static void voice_apply_filter(synth_t *s, synth_voice_t *v)
{
    v->filter_env.delay = 0.0f;
    v->filter_env.hold = 0.0f;
    v->filter_env.attack = synth_param_denorm(SYNTH_PARAM_FILTER_ENV_ATTACK, s->params[SYNTH_PARAM_FILTER_ENV_ATTACK]);
    v->filter_env.decay = synth_param_denorm(SYNTH_PARAM_FILTER_ENV_DECAY, s->params[SYNTH_PARAM_FILTER_ENV_DECAY]);
    v->filter_env.sustain = synth_param_denorm(SYNTH_PARAM_FILTER_ENV_SUSTAIN, s->params[SYNTH_PARAM_FILTER_ENV_SUSTAIN]);
    v->filter_env.release = synth_param_denorm(SYNTH_PARAM_FILTER_ENV_RELEASE, s->params[SYNTH_PARAM_FILTER_ENV_RELEASE]);
    voice_tune_filter(s, v, v->filter_env.level);
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

        synth_osc_init(&v->osc, s->sample_rate);
        synth_env_init(&v->amp_env, s->sample_rate);
        synth_env_init(&v->filter_env, s->sample_rate);
        synth_filter_init(&v->filter, s->sample_rate);
        synth_amp_init(&v->amp);
        v->note = 60;
        v->held = 0;
        v->velocity = 0.0f;
        v->age = 0;
        voice_apply_osc(s, v);
        voice_apply_amp(s, v);
        voice_apply_envelope(s, v);
        voice_apply_filter(s, v);
    }
    s->mod_counter = 0;
}

void synth_reset(synth_t *s)
{
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        synth_osc_reset(&v->osc);
        synth_filter_reset(&v->filter);
        v->amp_env.stage = SYNTH_ENV_IDLE;
        v->amp_env.level = 0.0f;
        v->amp_env.time = 0.0f;
        v->filter_env.stage = SYNTH_ENV_IDLE;
        v->filter_env.level = 0.0f;
        v->filter_env.time = 0.0f;
        v->held = 0;
        v->velocity = 0.0f;
        v->age = 0;
    }
    s->age_counter = 0;
    s->mod_counter = 0;
}

void synth_set_sample_rate(synth_t *s, float sample_rate)
{
    int i;

    if (sample_rate <= 0.0f) {
        return;
    }
    s->sample_rate = sample_rate;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        v->amp_env.sample_rate = sample_rate;
        v->filter_env.sample_rate = sample_rate;
        v->filter.sample_rate = sample_rate;
        v->osc.sample_rate = sample_rate;
        voice_apply_filter(s, v); /* filter coefficients are rate dependent */
        voice_apply_osc(s, v);    /* so is the VOSIM pulse layout */
        voice_apply_amp(s, v);
    }
    synth_reset(s);
}

static synth_voice_t *allocate_voice(synth_t *s, int note)
{
    synth_voice_t *oldest = &s->voices[0];
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        if (v->note == note && v->held) {
            return v; /* retrigger rather than stacking a second voice */
        }
        if (!synth_env_is_active(&v->amp_env)) {
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
    v->held = 1;
    v->velocity = clamp01(velocity);
    v->age = ++s->age_counter;

    synth_osc_reset(&v->osc);
    synth_osc_set_freq(&v->osc, synth_note_to_hz((float)note));
    synth_filter_reset(&v->filter); /* a stolen voice must not ring on into the new note */
    voice_apply_osc(s, v);
    voice_apply_amp(s, v);
    voice_apply_envelope(s, v);
    synth_env_gate_on(&v->amp_env);
    synth_env_gate_on(&v->filter_env);
    voice_apply_filter(s, v);
}

void synth_note_off(synth_t *s, int note)
{
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        if (v->note == note && v->held) {
            v->held = 0;
            synth_env_gate_off(&v->amp_env);
            synth_env_gate_off(&v->filter_env);
        }
    }
}

void synth_all_notes_off(synth_t *s)
{
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        s->voices[i].held = 0;
        synth_env_gate_off(&s->voices[i].amp_env);
        synth_env_gate_off(&s->voices[i].filter_env);
    }
}

int synth_active_voices(const synth_t *s)
{
    int count = 0;
    int i;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        if (synth_env_is_active(&s->voices[i].amp_env)) {
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
        voice_apply_osc(s, &s->voices[i]);
        voice_apply_amp(s, &s->voices[i]);
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
    const synth_filter_mode_t mode =
        (synth_filter_mode_t)synth_param_denorm(SYNTH_PARAM_FILTER_MODE, s->params[SYNTH_PARAM_FILTER_MODE]);
    int i, v;

    for (i = 0; i < n_frames; ++i) {
        float sum = 0.0f;
        int retune = (s->mod_counter == 0);

        s->mod_counter = retune ? (SYNTH_MOD_INTERVAL - 1) : (s->mod_counter - 1);

        for (v = 0; v < SYNTH_MAX_VOICES; ++v) {
            synth_voice_t *voice = &s->voices[v];
            float level;
            float sample;

            if (!synth_env_is_active(&voice->amp_env)) {
                continue;
            }

            /* The envelope runs per sample so its timing stays exact, but
               retuning the filter costs far more than one sample of audio, so
               that happens at control rate. */
            level = synth_env_next(&voice->filter_env);
            if (retune) {
                voice_tune_filter(s, voice, level);
            }

            sample = synth_osc_next(&voice->osc);
            sample = synth_filter_next(&voice->filter, sample, mode);
            sum += synth_amp_next(&voice->amp, sample, synth_env_next(&voice->amp_env));
        }

        out[i] = sum * gain;
    }
}
