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
    SYNTH_PARAM_ENTRY("amp_velocity", 0.0f, 1.0f, 1.0f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("lfo_rate", 0.02f, 40.0f, 0.3f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("lfo_shape", 0.0f, (float)(SYNTH_LFO_COUNT - 1), 0.0f, SYNTH_CURVE_STEPPED),
    /* Bipolar depths, so the centre of each control is no modulation at all. */
    SYNTH_PARAM_ENTRY("lfo_to_cutoff", -3.0f, 3.0f, 0.5f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("lfo_to_pitch", -12.0f, 12.0f, 0.5f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("lfo_to_amp", 0.0f, 1.0f, 0.0f, SYNTH_CURVE_LINEAR)
};

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    return (v > 1.0f) ? 1.0f : v;
}

/* One unsigned comparison rather than two signed ones: a negative value wraps
   to something huge and fails the same test. The ARM EABI stores an enum in the
   smallest type that fits it, so on those targets the enum is unsigned and a
   `>= 0` test is not just redundant, it is a warning. */
static int param_is_valid(synth_param_t param)
{
    return (unsigned)param < (unsigned)SYNTH_PARAM_COUNT;
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

/* Tremolo dips from the level rather than lifting past it, so turning the depth
   up cannot make a patch louder than the one it started from. */
static void voice_apply_tremolo(synth_t *s, synth_voice_t *v)
{
    float depth = synth_param_denorm(SYNTH_PARAM_LFO_TO_AMP, s->params[SYNTH_PARAM_LFO_TO_AMP]);

    v->amp.level = v->amp_base * (1.0f - depth * 0.5f * (1.0f - v->lfo_value));
}

static void voice_apply_amp(synth_t *s, synth_voice_t *v)
{
    float sensitivity = synth_param_denorm(SYNTH_PARAM_AMP_VELOCITY, s->params[SYNTH_PARAM_AMP_VELOCITY]);

    v->amp.drive = synth_param_denorm(SYNTH_PARAM_AMP_DRIVE, s->params[SYNTH_PARAM_AMP_DRIVE]);
    v->amp_base = 1.0f - sensitivity + sensitivity * v->velocity;
    voice_apply_tremolo(s, v);
}

static void voice_apply_lfo(synth_t *s, synth_voice_t *v)
{
    v->lfo.shape = (synth_lfo_shape_t)synth_param_denorm(SYNTH_PARAM_LFO_SHAPE,
                                                         s->params[SYNTH_PARAM_LFO_SHAPE]);
    synth_lfo_set_rate(&v->lfo,
                       synth_param_denorm(SYNTH_PARAM_LFO_RATE, s->params[SYNTH_PARAM_LFO_RATE]),
                       SYNTH_MOD_INTERVAL);
}

/* Pitch is the oscillator's note, the bend in force and the LFO together, so
   every one of the three moves it without the others noticing. */
static void voice_tune_osc(synth_t *s, synth_voice_t *v)
{
    float depth = synth_param_denorm(SYNTH_PARAM_LFO_TO_PITCH, s->params[SYNTH_PARAM_LFO_TO_PITCH]);

    synth_osc_set_freq(&v->osc,
                       synth_note_to_hz((float)v->note + s->pitch_bend + depth * v->lfo_value));
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
    float wobble = synth_param_denorm(SYNTH_PARAM_LFO_TO_CUTOFF, s->params[SYNTH_PARAM_LFO_TO_CUTOFF]);
    float octaves = amount * env_level + track * ((float)v->note - 60.0f) * (1.0f / 12.0f)
                    + wobble * v->lfo_value;

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
    s->pitch_bend = 0.0f;
    atomic_store_explicit(&s->queue.head, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->queue.tail, 0u, memory_order_relaxed);
    s->frame_time = 0;
    atomic_store_explicit(&s->clock.sequence, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->clock.low, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->clock.high, 0u, memory_order_relaxed);

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        s->params[i] = k_param_info[i].default_norm;
    }

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        synth_osc_init(&v->osc, s->sample_rate);
        /* Distinct per voice so unison notes do not share one noise stream, and
           derived from the index so two runs of the same patch still match. */
        synth_osc_set_noise_seed(&v->osc, 0x9E3779B9u + (unsigned)i * 0x85EBCA6Bu);
        synth_env_init(&v->amp_env, s->sample_rate);
        synth_env_init(&v->filter_env, s->sample_rate);
        synth_filter_init(&v->filter, s->sample_rate);
        synth_amp_init(&v->amp);
        synth_lfo_init(&v->lfo, s->sample_rate, 0xC2B2AE35u + (unsigned)i * 0x27D4EB2Fu);
        v->lfo_value = 0.0f;
        v->amp_base = 0.0f;
        v->note = 60;
        v->held = 0;
        v->velocity = 0.0f;
        v->age = 0;
        voice_apply_osc(s, v);
        voice_apply_lfo(s, v);
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
        v->lfo.sample_rate = sample_rate;
        voice_apply_lfo(s, v);    /* the LFO's rate is in hertz, so it is too */
        voice_apply_filter(s, v); /* filter coefficients are rate dependent */
        voice_apply_osc(s, v);    /* so is the VOSIM pulse layout */
        voice_apply_amp(s, v);
    }
    synth_reset(s);
}

void synth_set_pitch_bend(synth_t *s, float semitones)
{
    int i;

    s->pitch_bend = semitones;

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        /* Voices in their release are still sounding and still belong to the
           note that was played, so they bend too. */
        if (synth_env_is_active(&v->amp_env)) {
            voice_tune_osc(s, v);
        }
    }
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
    synth_lfo_retrigger(&v->lfo);
    v->lfo_value = 0.0f;
    synth_filter_reset(&v->filter); /* a stolen voice must not ring on into the new note */
    voice_apply_osc(s, v);
    voice_apply_lfo(s, v);
    voice_apply_amp(s, v);
    voice_tune_osc(s, v);
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
        voice_apply_lfo(s, &s->voices[i]);
        voice_apply_amp(s, &s->voices[i]);
        voice_apply_envelope(s, &s->voices[i]);
        voice_apply_filter(s, &s->voices[i]);
    }
}

float synth_get_param(const synth_t *s, synth_param_t param)
{
    return param_is_valid(param) ? s->params[param] : 0.0f;
}

static void render_block(synth_t *s, float *out, int n_frames)
{
    const float gain = synth_param_denorm(SYNTH_PARAM_MASTER_GAIN, s->params[SYNTH_PARAM_MASTER_GAIN]);
    const synth_filter_mode_t mode =
        (synth_filter_mode_t)synth_param_denorm(SYNTH_PARAM_FILTER_MODE, s->params[SYNTH_PARAM_FILTER_MODE]);
    const float pitch_depth =
        synth_param_denorm(SYNTH_PARAM_LFO_TO_PITCH, s->params[SYNTH_PARAM_LFO_TO_PITCH]);
    const int bends_pitch = (pitch_depth > 0.001f || pitch_depth < -0.001f);
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
                voice->lfo_value = synth_lfo_next(&voice->lfo);
                voice_tune_filter(s, voice, level);
                voice_apply_tremolo(s, voice);
                /* Retuning the oscillator recomputes the VOSIM pulse layout, so
                   it is worth skipping when nothing asks for vibrato. */
                if (bends_pitch) {
                    voice_tune_osc(s, voice);
                }
            }

            sample = synth_osc_next(&voice->osc);
            sample = synth_filter_next(&voice->filter, sample, mode);
            sum += synth_amp_next(&voice->amp, sample, synth_env_next(&voice->amp_env));
        }

        out[i] = sum * gain;
    }
}

/* Masking the index is only valid for a power of two, and a host is free to set
   this, so it is checked rather than assumed. */
typedef char synth_event_queue_len_must_be_a_power_of_two[
    ((SYNTH_EVENT_QUEUE_LEN & (SYNTH_EVENT_QUEUE_LEN - 1)) == 0 &&
     SYNTH_EVENT_QUEUE_LEN >= 2) ? 1 : -1];

int synth_schedule(synth_t *s, const synth_event_t *event)
{
    synth_event_queue_t *q = &s->queue;
    unsigned tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    unsigned next = (tail + 1u) & (SYNTH_EVENT_QUEUE_LEN - 1u);

    if (next == atomic_load_explicit(&q->head, memory_order_acquire)) {
        return 0; /* full: refuse rather than block the caller or overwrite */
    }

    q->events[tail] = *event;
    /* Release pairs with the consumer's acquire, so the event is visible before
       the index that publishes it. */
    atomic_store_explicit(&q->tail, next, memory_order_release);
    return 1;
}

static void clock_publish(synth_frame_clock_t *clock, uint64_t value)
{
    unsigned sequence = atomic_load_explicit(&clock->sequence, memory_order_relaxed);

    atomic_store_explicit(&clock->sequence, sequence + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&clock->low, (unsigned)(value & 0xFFFFFFFFu), memory_order_relaxed);
    atomic_store_explicit(&clock->high, (unsigned)(value >> 32), memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&clock->sequence, sequence + 2u, memory_order_relaxed);
}

uint64_t synth_frame_time(const synth_t *s)
{
    const synth_frame_clock_t *clock = &s->clock;
    unsigned before;
    unsigned after;
    unsigned low;
    unsigned high;

    do {
        before = atomic_load_explicit(&clock->sequence, memory_order_acquire);
        low = atomic_load_explicit(&clock->low, memory_order_relaxed);
        high = atomic_load_explicit(&clock->high, memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        after = atomic_load_explicit(&clock->sequence, memory_order_relaxed);
    } while ((before & 1u) != 0u || before != after);

    return ((uint64_t)high << 32) | (uint64_t)low;
}

static const synth_event_t *queue_peek(synth_event_queue_t *q)
{
    unsigned head = atomic_load_explicit(&q->head, memory_order_relaxed);

    if (head == atomic_load_explicit(&q->tail, memory_order_acquire)) {
        return 0;
    }
    return &q->events[head];
}

static void queue_pop(synth_event_queue_t *q)
{
    unsigned head = atomic_load_explicit(&q->head, memory_order_relaxed);

    atomic_store_explicit(&q->head, (head + 1u) & (SYNTH_EVENT_QUEUE_LEN - 1u),
                          memory_order_release);
}

static void apply_event(synth_t *s, const synth_event_t *event)
{
    switch (event->type) {
    case SYNTH_EVENT_NOTE_ON:
        synth_note_on(s, event->index, event->value);
        break;
    case SYNTH_EVENT_NOTE_OFF:
        synth_note_off(s, event->index);
        break;
    case SYNTH_EVENT_PARAM:
        synth_set_param(s, (synth_param_t)event->index, event->value);
        break;
    case SYNTH_EVENT_PITCH_BEND:
        synth_set_pitch_bend(s, event->value);
        break;
    case SYNTH_EVENT_ALL_NOTES_OFF:
        synth_all_notes_off(s);
        break;
    default:
        break;
    }
}

void synth_save_patch(const synth_t *s, float patch[SYNTH_PARAM_COUNT])
{
    int i;

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        patch[i] = s->params[i];
    }
}

void synth_load_patch(synth_t *s, const float patch[SYNTH_PARAM_COUNT])
{
    int i;

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        s->params[i] = clamp01(patch[i]);
    }

    /* One pass over the voices rather than one per parameter, which is what
       calling synth_set_param in a loop would cost. */
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        voice_apply_osc(s, &s->voices[i]);
        voice_apply_lfo(s, &s->voices[i]);
        voice_apply_amp(s, &s->voices[i]);
        voice_apply_envelope(s, &s->voices[i]);
        voice_apply_filter(s, &s->voices[i]);
    }
}

void synth_render(synth_t *s, float *out, int n_frames)
{
    uint64_t start = s->frame_time; /* only the audio thread touches this */
    int done = 0;

    if (n_frames <= 0) {
        return;
    }

    while (done < n_frames) {
        int chunk = n_frames - done;
        const synth_event_t *next;

        /* Apply everything already due, then shorten the chunk so the next
           event lands on its own frame instead of the block boundary. */
        while ((next = queue_peek(&s->queue)) != 0) {
            uint64_t now = start + (uint64_t)done;

            if (next->frame <= now) {
                synth_event_t due = *next;

                queue_pop(&s->queue);
                apply_event(s, &due);
                continue;
            }
            if (next->frame - now < (uint64_t)chunk) {
                chunk = (int)(next->frame - now);
            }
            break;
        }

        render_block(s, out + done, chunk);
        done += chunk;
    }

    s->frame_time = start + (uint64_t)n_frames;
    clock_publish(&s->clock, s->frame_time);
}
