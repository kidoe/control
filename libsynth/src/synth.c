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
    SYNTH_PARAM_ENTRY("lfo_to_amp", 0.0f, 1.0f, 0.0f, SYNTH_CURVE_LINEAR),
    /* Bipolar, in octaves: positive sweeps down onto the note, which is what a
       synthesized drum is, and negative sweeps up off it. Centred, so the
       section is silent in every patch that predates it. */
    SYNTH_PARAM_ENTRY("pitch_env_amount", -4.0f, 4.0f, 0.5f, SYNTH_CURVE_LINEAR),
    SYNTH_PARAM_ENTRY("pitch_env_attack", 0.0f, 2.0f, 0.0f, SYNTH_CURVE_CUBIC),
    SYNTH_PARAM_ENTRY("pitch_env_decay", 0.001f, 4.0f, 0.3f, SYNTH_CURVE_CUBIC),
    /* Seconds for a note to travel from the one before it. Zero is off, which
       is where it ships, so no existing patch acquires a glide. */
    SYNTH_PARAM_ENTRY("glide", 0.0f, 2.0f, 0.0f, SYNTH_CURVE_CUBIC)
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

/* The controls the render loop reads on every control tick, denormalized once.
   Reading one means a bounds check, a clamp, a curve and a scale, and the loop
   was doing eight of those per voice per tick for values that cannot change
   while it runs: parameters only move between blocks, because synth_render()
   splits the block at every event it applies. */
typedef struct {
    float pitch_bend;
    float lfo_to_pitch;
    float pitch_env_amount;
    float cutoff;
    float filter_env_amount;
    float key_track;
    float filter_q;
    float lfo_to_cutoff;
    float lfo_to_amp;
} mod_t;

static void mod_load(const synth_t *s, mod_t *m)
{
    m->pitch_bend = s->pitch_bend;
    m->lfo_to_pitch = synth_param_denorm(SYNTH_PARAM_LFO_TO_PITCH, s->params[SYNTH_PARAM_LFO_TO_PITCH]);
    m->pitch_env_amount = synth_param_denorm(SYNTH_PARAM_PITCH_ENV_AMOUNT, s->params[SYNTH_PARAM_PITCH_ENV_AMOUNT]);
    m->cutoff = synth_param_denorm(SYNTH_PARAM_FILTER_CUTOFF, s->params[SYNTH_PARAM_FILTER_CUTOFF]);
    m->filter_env_amount = synth_param_denorm(SYNTH_PARAM_FILTER_ENV_AMOUNT, s->params[SYNTH_PARAM_FILTER_ENV_AMOUNT]);
    m->key_track = synth_param_denorm(SYNTH_PARAM_FILTER_KEY_TRACK, s->params[SYNTH_PARAM_FILTER_KEY_TRACK]);
    m->filter_q = synth_param_denorm(SYNTH_PARAM_FILTER_Q, s->params[SYNTH_PARAM_FILTER_Q]);
    m->lfo_to_cutoff = synth_param_denorm(SYNTH_PARAM_LFO_TO_CUTOFF, s->params[SYNTH_PARAM_LFO_TO_CUTOFF]);
    m->lfo_to_amp = synth_param_denorm(SYNTH_PARAM_LFO_TO_AMP, s->params[SYNTH_PARAM_LFO_TO_AMP]);
}

static void voice_apply_envelope(synth_t *s, synth_voice_t *v)
{
    synth_env_set_times(&v->amp_env,
                        synth_param_denorm(SYNTH_PARAM_AMP_DELAY, s->params[SYNTH_PARAM_AMP_DELAY]),
                        synth_param_denorm(SYNTH_PARAM_AMP_ATTACK, s->params[SYNTH_PARAM_AMP_ATTACK]),
                        synth_param_denorm(SYNTH_PARAM_AMP_HOLD, s->params[SYNTH_PARAM_AMP_HOLD]),
                        synth_param_denorm(SYNTH_PARAM_AMP_DECAY, s->params[SYNTH_PARAM_AMP_DECAY]),
                        synth_param_denorm(SYNTH_PARAM_AMP_SUSTAIN, s->params[SYNTH_PARAM_AMP_SUSTAIN]),
                        synth_param_denorm(SYNTH_PARAM_AMP_RELEASE, s->params[SYNTH_PARAM_AMP_RELEASE]));
}

/* Brings the instance's terrain cross-section up to date with its parameters.
   Cheap when they have not moved — one comparison — and the only place the orbit
   is ever walked, so it belongs beside every parameter write rather than inside
   the voice loop that follows it.

   A cross-section nothing orbits is not derived: the walk is 21,545 instructions
   and every other waveform ignores its result, so a patch that is not a terrain
   patch does not pay for one. What makes that safe is that every path which can
   select the terrain calls this — the waveform reaches UNIT_OSC, and a patch load
   refreshes unconditionally — so switching to it derives then, against whatever
   the radius and the ratio have become in the meantime. */
static void terrain_refresh(synth_t *s)
{
    if ((synth_wave_t)synth_param_denorm(SYNTH_PARAM_OSC_WAVE, s->params[SYNTH_PARAM_OSC_WAVE])
        != SYNTH_WAVE_TERRAIN) {
        return;
    }
    synth_terrain_set(&s->terrain,
                      synth_param_denorm(SYNTH_PARAM_TERRAIN_RADIUS, s->params[SYNTH_PARAM_TERRAIN_RADIUS]),
                      (int)synth_param_denorm(SYNTH_PARAM_TERRAIN_RATIO, s->params[SYNTH_PARAM_TERRAIN_RATIO]));
}

static void voice_apply_osc(synth_t *s, synth_voice_t *v)
{
    /* The settings first and the waveform last, because each setter derives only
       while its own waveform is the selected one: this way a switch derives the
       waveform being switched to, once, from settings already in place. */
    synth_osc_set_pd_knee(&v->osc,
                          synth_param_denorm(SYNTH_PARAM_PD_KNEE, s->params[SYNTH_PARAM_PD_KNEE]));
    synth_osc_set_vosim(&v->osc,
                        synth_param_denorm(SYNTH_PARAM_VOSIM_FORMANT, s->params[SYNTH_PARAM_VOSIM_FORMANT]),
                        (int)synth_param_denorm(SYNTH_PARAM_VOSIM_PULSES, s->params[SYNTH_PARAM_VOSIM_PULSES]),
                        synth_param_denorm(SYNTH_PARAM_VOSIM_DECAY, s->params[SYNTH_PARAM_VOSIM_DECAY]));
    synth_osc_set_wave(&v->osc,
                       (synth_wave_t)synth_param_denorm(SYNTH_PARAM_OSC_WAVE,
                                                        s->params[SYNTH_PARAM_OSC_WAVE]));
    /* Four floats copied, not a lap of the orbit: the instance derived it once in
       terrain_refresh(). Deriving it per voice was the same answer computed
       SYNTH_MAX_VOICES times, 328,000 instructions for one move of either
       control, which is a whole 2 ms deadline on an M4F. */
    synth_osc_set_terrain_from(&v->osc, &s->terrain);
}

/* Tremolo dips from the level rather than lifting past it, so turning the depth
   up cannot make a patch louder than the one it started from. */
static void voice_apply_tremolo(const mod_t *m, synth_voice_t *v)
{
    v->amp.level = v->amp_base * (1.0f - m->lfo_to_amp * 0.5f * (1.0f - v->lfo_value));
}

static void voice_apply_amp(synth_t *s, const mod_t *m, synth_voice_t *v)
{
    float sensitivity = synth_param_denorm(SYNTH_PARAM_AMP_VELOCITY, s->params[SYNTH_PARAM_AMP_VELOCITY]);

    v->amp.drive = synth_param_denorm(SYNTH_PARAM_AMP_DRIVE, s->params[SYNTH_PARAM_AMP_DRIVE]);
    v->amp_base = 1.0f - sensitivity + sensitivity * v->velocity;
    voice_apply_tremolo(m, v);
}

static void voice_apply_lfo(synth_t *s, synth_voice_t *v)
{
    v->lfo.shape = (synth_lfo_shape_t)synth_param_denorm(SYNTH_PARAM_LFO_SHAPE,
                                                         s->params[SYNTH_PARAM_LFO_SHAPE]);
    synth_lfo_set_rate(&v->lfo,
                       synth_param_denorm(SYNTH_PARAM_LFO_RATE, s->params[SYNTH_PARAM_LFO_RATE]),
                       SYNTH_MOD_INTERVAL);
}

/* Pitch is the oscillator's note, the bend in force, the LFO and the pitch
   envelope together, so every one of the four moves it without the others
   noticing. The envelope's amount is in octaves, like the filter's, because a
   sweep is heard as a ratio rather than as a number of hertz; twelve semitones
   to the octave is the only conversion. */
static void voice_tune_osc(const mod_t *m, synth_voice_t *v)
{
    synth_osc_set_freq(&v->osc,
                       synth_note_to_hz((float)v->note + m->pitch_bend + v->glide
                                        + m->lfo_to_pitch * v->lfo_value
                                        + 12.0f * m->pitch_env_amount * v->pitch_env.level));
}

/* A glide is held as the distance still to travel rather than as a position, so
   it needs no target of its own: it decays to zero and the note is simply in
   tune again. Linear in semitones, so the time is the same whatever the
   interval, which is what a glide control is taken to mean. */
static void voice_glide_step(synth_voice_t *v)
{
    v->glide -= v->glide_step;
    if ((v->glide_step > 0.0f) ? (v->glide <= 0.0f) : (v->glide >= 0.0f)) {
        v->glide = 0.0f;
        v->glide_step = 0.0f;
    }
}

/* Starts a voice on the pitch of the note before it, with the distance to make
   up over the glide time. Nothing to glide from — the first note of a session,
   or a glide time of zero — leaves it in tune from the first sample. */
static void voice_start_glide(synth_t *s, synth_voice_t *v, int note)
{
    float seconds = synth_param_denorm(SYNTH_PARAM_GLIDE, s->params[SYNTH_PARAM_GLIDE]);
    float ticks;

    v->glide = 0.0f;
    v->glide_step = 0.0f;
    if (seconds > 0.0f && s->last_note >= 0.0f && s->sample_rate > 0.0f) {
        ticks = seconds * s->sample_rate * (1.0f / (float)SYNTH_MOD_INTERVAL);
        if (ticks >= 1.0f) {
            v->glide = s->last_note - (float)note;
            v->glide_step = v->glide / ticks;
        }
    }
    s->last_note = (float)note;
}

/* The filter's cutoff is set in octaves so the envelope and the keyboard move it
   musically: an equal number of octaves sounds like an equal move wherever the
   base cutoff sits. */
static void voice_tune_filter(const mod_t *m, synth_voice_t *v, float env_level)
{
    float octaves = m->filter_env_amount * env_level
                    + m->key_track * ((float)v->note - 60.0f) * (1.0f / 12.0f)
                    + m->lfo_to_cutoff * v->lfo_value;

    synth_filter_set(&v->filter, m->cutoff * synth_exp2f(octaves), m->filter_q);
}

static void voice_apply_filter(synth_t *s, const mod_t *m, synth_voice_t *v)
{
    synth_env_set_times(&v->filter_env, 0.0f,
                        synth_param_denorm(SYNTH_PARAM_FILTER_ENV_ATTACK, s->params[SYNTH_PARAM_FILTER_ENV_ATTACK]),
                        0.0f,
                        synth_param_denorm(SYNTH_PARAM_FILTER_ENV_DECAY, s->params[SYNTH_PARAM_FILTER_ENV_DECAY]),
                        synth_param_denorm(SYNTH_PARAM_FILTER_ENV_SUSTAIN, s->params[SYNTH_PARAM_FILTER_ENV_SUSTAIN]),
                        synth_param_denorm(SYNTH_PARAM_FILTER_ENV_RELEASE, s->params[SYNTH_PARAM_FILTER_ENV_RELEASE]));
    voice_tune_filter(m, v, v->filter_env.level);
}

/* Attack and decay only. synth_env_t carries a sustain and a release as well,
   but a sweep that sustained would leave the note permanently out of tune, so
   this one always falls back to nothing and stays there. */
static void voice_apply_pitch(synth_t *s, synth_voice_t *v)
{
    synth_env_set_times(&v->pitch_env, 0.0f,
                        synth_param_denorm(SYNTH_PARAM_PITCH_ENV_ATTACK, s->params[SYNTH_PARAM_PITCH_ENV_ATTACK]),
                        0.0f,
                        synth_param_denorm(SYNTH_PARAM_PITCH_ENV_DECAY, s->params[SYNTH_PARAM_PITCH_ENV_DECAY]),
                        0.0f, 0.0f);
}

void synth_init(synth_t *s, float sample_rate)
{
    mod_t m;
    int i;

    s->sample_rate = (sample_rate > 0.0f) ? sample_rate : 44100.0f;
    s->age_counter = 0;
    s->pitch_bend = 0.0f;
    s->last_note = -1.0f;
    atomic_store_explicit(&s->queue.head, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->queue.tail, 0u, memory_order_relaxed);
    s->frame_time = 0;
    atomic_store_explicit(&s->clock.sequence, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->clock.low, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->clock.high, 0u, memory_order_relaxed);

    for (i = 0; i < SYNTH_PARAM_COUNT; ++i) {
        s->params[i] = k_param_info[i].default_norm;
    }
    mod_load(s, &m); /* after the defaults are in place, before any voice reads them */
    /* Defined before any voice copies it, and out of the clamped range so the
       first refresh that finds the terrain selected cannot mistake it for an
       answer already derived. A patch that never selects the terrain leaves
       these zeros in every voice, where nothing reads them. */
    s->terrain.radius = 0.0f;
    s->terrain.ratio = 0;
    s->terrain.scale = 0.0f;
    s->terrain.dc = 0.0f;
    terrain_refresh(s);

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        synth_osc_init(&v->osc, s->sample_rate);
        /* Distinct per voice so unison notes do not share one noise stream, and
           derived from the index so two runs of the same patch still match. */
        synth_osc_set_noise_seed(&v->osc, 0x9E3779B9u + (unsigned)i * 0x85EBCA6Bu);
        synth_env_init(&v->amp_env, s->sample_rate);
        synth_env_init(&v->filter_env, s->sample_rate);
        synth_env_init(&v->pitch_env, s->sample_rate);
        synth_filter_init(&v->filter, s->sample_rate);
        synth_amp_init(&v->amp);
        synth_lfo_init(&v->lfo, s->sample_rate, 0xC2B2AE35u + (unsigned)i * 0x27D4EB2Fu);
        v->lfo_value = 0.0f;
        v->glide = 0.0f;
        v->glide_step = 0.0f;
        v->amp_base = 0.0f;
        v->note = 60;
        v->held = 0;
        v->velocity = 0.0f;
        v->age = 0;
        voice_apply_osc(s, v);
        voice_apply_lfo(s, v);
        voice_apply_amp(s, &m, v);
        voice_apply_envelope(s, v);
        voice_apply_filter(s, &m, v);
        voice_apply_pitch(s, v);
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
        v->pitch_env.stage = SYNTH_ENV_IDLE;
        v->pitch_env.level = 0.0f;
        v->pitch_env.time = 0.0f;
        v->glide = 0.0f;
        v->glide_step = 0.0f;
        v->held = 0;
        v->velocity = 0.0f;
        v->age = 0;
    }
    s->age_counter = 0;
    s->mod_counter = 0;
    s->last_note = -1.0f; /* nothing sounded, so the next note has nothing to glide from */
}

void synth_set_sample_rate(synth_t *s, float sample_rate)
{
    mod_t m;
    int i;

    if (sample_rate <= 0.0f) {
        return;
    }
    s->sample_rate = sample_rate;
    mod_load(s, &m);

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        synth_env_set_sample_rate(&v->amp_env, sample_rate);
        synth_env_set_sample_rate(&v->filter_env, sample_rate);
        synth_env_set_sample_rate(&v->pitch_env, sample_rate);
        v->filter.sample_rate = sample_rate;
        v->osc.sample_rate = sample_rate;
        v->lfo.sample_rate = sample_rate;
        voice_apply_lfo(s, v);    /* the LFO's rate is in hertz, so it is too */
        voice_apply_filter(s, &m, v); /* filter coefficients are rate dependent */
        voice_apply_osc(s, v);        /* so is the VOSIM pulse layout */
        voice_apply_amp(s, &m, v);
    }
    synth_reset(s);
}

void synth_set_pitch_bend(synth_t *s, float semitones)
{
    mod_t m;
    int i;

    s->pitch_bend = semitones;
    mod_load(s, &m);

    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        /* Voices in their release are still sounding and still belong to the
           note that was played, so they bend too. */
        if (synth_env_is_active(&v->amp_env)) {
            voice_tune_osc(&m, v);
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
    mod_t m;

    mod_load(s, &m);

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
    voice_apply_amp(s, &m, v);
    voice_apply_envelope(s, v);
    voice_apply_pitch(s, v);
    voice_start_glide(s, v, note);
    synth_env_gate_on(&v->amp_env);
    synth_env_gate_on(&v->filter_env);
    synth_env_gate_on(&v->pitch_env);
    /* After the gate, not before: gating clears the envelope's level, and a
       stolen voice would otherwise start the new note on the old one's sweep. */
    voice_tune_osc(&m, v);
    voice_apply_filter(s, &m, v);
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
            synth_env_gate_off(&v->pitch_env);
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
        synth_env_gate_off(&s->voices[i].pitch_env);
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

/* Which parts of a voice a parameter reaches. Re-applying all of them costs
   about three blocks of audio, and a host moving a knob or playing back
   automation sends one parameter change per frame, so the block a change lands
   in used to overrun its deadline on an MCU. The switch has no default, so
   -Wswitch makes the compiler refuse a parameter nobody has placed here. */
enum {
    UNIT_OSC         = 1u << 0, /* waveform settings: knee, VOSIM, terrain */
    UNIT_OSC_TUNE    = 1u << 1, /* the oscillator's frequency */
    UNIT_AMP         = 1u << 2,
    UNIT_AMP_ENV     = 1u << 3,
    UNIT_FILTER_ENV  = 1u << 4, /* the filter envelope's times, which also tunes */
    UNIT_FILTER_TUNE = 1u << 5,
    UNIT_LFO         = 1u << 6,
    UNIT_PITCH_ENV   = 1u << 7
};

static unsigned param_reaches(synth_param_t param)
{
    switch (param) {
    /* Read straight out of render_block every time, never stored in a voice. */
    case SYNTH_PARAM_MASTER_GAIN:
    case SYNTH_PARAM_FILTER_MODE:
    case SYNTH_PARAM_GLIDE: /* read by the next note_on, never held in a voice */
        return 0u;

    case SYNTH_PARAM_OSC_WAVE:
    case SYNTH_PARAM_PD_KNEE:
    case SYNTH_PARAM_VOSIM_FORMANT:
    case SYNTH_PARAM_VOSIM_PULSES:
    case SYNTH_PARAM_VOSIM_DECAY:
    case SYNTH_PARAM_TERRAIN_RADIUS:
    case SYNTH_PARAM_TERRAIN_RATIO:
        return UNIT_OSC;

    case SYNTH_PARAM_AMP_DELAY:
    case SYNTH_PARAM_AMP_ATTACK:
    case SYNTH_PARAM_AMP_HOLD:
    case SYNTH_PARAM_AMP_DECAY:
    case SYNTH_PARAM_AMP_SUSTAIN:
    case SYNTH_PARAM_AMP_RELEASE:
        return UNIT_AMP_ENV;

    case SYNTH_PARAM_FILTER_CUTOFF:
    case SYNTH_PARAM_FILTER_Q:
    case SYNTH_PARAM_FILTER_ENV_AMOUNT:
    case SYNTH_PARAM_FILTER_KEY_TRACK:
    case SYNTH_PARAM_LFO_TO_CUTOFF:
        return UNIT_FILTER_TUNE;

    case SYNTH_PARAM_FILTER_ENV_ATTACK:
    case SYNTH_PARAM_FILTER_ENV_DECAY:
    case SYNTH_PARAM_FILTER_ENV_SUSTAIN:
    case SYNTH_PARAM_FILTER_ENV_RELEASE:
        return UNIT_FILTER_ENV;

    case SYNTH_PARAM_AMP_DRIVE:
    case SYNTH_PARAM_AMP_VELOCITY:
    case SYNTH_PARAM_LFO_TO_AMP: /* the tremolo lives in the amplifier */
        return UNIT_AMP;

    case SYNTH_PARAM_LFO_RATE:
    case SYNTH_PARAM_LFO_SHAPE:
        return UNIT_LFO;

    /* Centring a pitch depth has to put a sounding note back where it belongs,
       so both of these retune even though neither is a frequency. */
    case SYNTH_PARAM_LFO_TO_PITCH:
        return UNIT_OSC_TUNE;
    case SYNTH_PARAM_PITCH_ENV_AMOUNT:
        return UNIT_PITCH_ENV | UNIT_OSC_TUNE;
    case SYNTH_PARAM_PITCH_ENV_ATTACK:
    case SYNTH_PARAM_PITCH_ENV_DECAY:
        return UNIT_PITCH_ENV;

    case SYNTH_PARAM_COUNT:
        break;
    }
    return ~0u;
}

static void voice_apply(synth_t *s, const mod_t *m, synth_voice_t *v, unsigned units)
{
    if (units & UNIT_OSC) {
        voice_apply_osc(s, v);
    }
    if (units & UNIT_LFO) {
        voice_apply_lfo(s, v);
    }
    if (units & UNIT_AMP) {
        voice_apply_amp(s, m, v);
    }
    if (units & UNIT_AMP_ENV) {
        voice_apply_envelope(s, v);
    }
    if (units & UNIT_FILTER_ENV) {
        voice_apply_filter(s, m, v);
    } else if (units & UNIT_FILTER_TUNE) {
        voice_tune_filter(m, v, v->filter_env.level);
    }
    if (units & UNIT_PITCH_ENV) {
        voice_apply_pitch(s, v);
    }
    if (units & UNIT_OSC_TUNE) {
        voice_tune_osc(m, v);
    }
}

void synth_set_param(synth_t *s, synth_param_t param, float norm)
{
    unsigned units;
    mod_t m;
    int i;

    if (!param_is_valid(param)) {
        return;
    }
    s->params[param] = clamp01(norm);
    units = param_reaches(param);
    if (units == 0u) {
        return;
    }
    mod_load(s, &m);
    if (units & UNIT_OSC) {
        terrain_refresh(s);
    }

    /* Edits reach sounding voices, as they did in the JSyn prototype — and only
       those. An idle slot is rebuilt from the parameters by the note_on that
       claims it, every unit of it, so applying to one here is work whose result
       is overwritten before it can be heard. It is not a small saving: with one
       voice of eight sounding, every parameter of a kit change costs 34,000
       instructions instead of 202,000. */
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        if (!synth_env_is_active(&v->amp_env)) {
            continue;
        }
        voice_apply(s, &m, v, units);
    }
}

float synth_get_param(const synth_t *s, synth_param_t param)
{
    return param_is_valid(param) ? s->params[param] : 0.0f;
}

/* Modulation depths are bipolar and sit at zero when centred, so "does this
   reach anything" is one comparison against a hair either side of zero. */
static int nonzero(float v)
{
    return (v > 0.001f) || (v < -0.001f);
}

static int any_voice_sounds(const synth_t *s)
{
    int v;

    for (v = 0; v < SYNTH_MAX_VOICES; ++v) {
        if (synth_env_is_active(&s->voices[v].amp_env)) {
            return 1;
        }
    }
    return 0;
}

static void render_block(synth_t *s, float *out, int n_frames, int add)
{
    const float gain = synth_param_denorm(SYNTH_PARAM_MASTER_GAIN, s->params[SYNTH_PARAM_MASTER_GAIN]);
    const synth_filter_mode_t mode =
        (synth_filter_mode_t)synth_param_denorm(SYNTH_PARAM_FILTER_MODE, s->params[SYNTH_PARAM_FILTER_MODE]);
    mod_t m;
    int sweeps_pitch, moves_pitch, moves_cutoff, hears_lfo;
    int i, v;

    mod_load(s, &m);
    /* One rule, applied to every modulation: what reaches nothing is not
       computed. Each of these is loop-invariant, so the decision is made once
       per block rather than per sample, and each guards real arithmetic —
       retuning the oscillator relays out the VOSIM pulses, retuning the filter
       is an exponential and a set of coefficients, and an envelope is a switch
       and a divide per voice per sample. The defaults leave all four off, so a
       patch that has not asked for modulation does not pay for any of it. */
    sweeps_pitch = nonzero(m.pitch_env_amount);
    moves_pitch = nonzero(m.lfo_to_pitch) || sweeps_pitch;
    moves_cutoff = nonzero(m.filter_env_amount) || nonzero(m.key_track)
                   || nonzero(m.lfo_to_cutoff);
    hears_lfo = nonzero(m.lfo_to_cutoff) || nonzero(m.lfo_to_pitch)
                || nonzero(m.lfo_to_amp);

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

            /* Both envelopes run per sample so their timing stays exact
               however the block is cut up. What they drive is set at control
               rate, because retuning costs far more than a sample of audio. */
            if (moves_cutoff) {
                level = synth_env_next(&voice->filter_env);
            } else {
                level = voice->filter_env.level;
            }
            if (sweeps_pitch) {
                synth_env_next(&voice->pitch_env);
            }
            if (retune) {
                if (hears_lfo) {
                    voice->lfo_value = synth_lfo_next(&voice->lfo);
                    voice_apply_tremolo(&m, voice);
                }
                if (moves_cutoff) {
                    voice_tune_filter(&m, voice, level);
                }
                if (voice->glide != 0.0f) {
                    voice_glide_step(voice);
                    voice_tune_osc(&m, voice);
                } else if (moves_pitch) {
                    voice_tune_osc(&m, voice);
                }
            }

            sample = synth_osc_next(&voice->osc);
            sample = synth_filter_next(&voice->filter, sample, mode);
            sum += synth_amp_next(&voice->amp, sample, synth_env_next(&voice->amp_env));
        }

        /* One branch a frame on a value that cannot change inside the block,
           which is what buys mixing without a scratch buffer per part. */
        if (add) {
            out[i] += sum * gain;
        } else {
            out[i] = sum * gain;
        }
    }
}

/* A part with nothing sounding is the common case on a groovebox — most tracks
   are silent most of the time — and every one of them is rendered on every
   callback, because a part skipped falls off the shared timeline. So the
   question "is there anything to do" is worth asking once for the block rather
   than once per frame per slot: eight empty slots across 96 frames is 768 times
   to answer it the same way. A silent block goes from 7,369 instructions to 199,
   and four silent tracks from 8.9% of a 2 ms deadline to 0.2%.

   Events cannot make this wrong. They are applied between chunks and never
   inside one, so a voice that is not sounding at the top of a chunk cannot start
   inside it — and one that is sounding can only stop, which the loop checks for
   every frame anyway. */
static void render_chunk(synth_t *s, float *out, int n_frames, int add)
{
    int i;

    if (any_voice_sounds(s)) {
        render_block(s, out, n_frames, add);
        return;
    }
    if (!add) {
        for (i = 0; i < n_frames; ++i) {
            out[i] = 0.0f;
        }
    }
    /* Adding nothing is how a silent part joins a mix, which for every value but
       a negative zero is what adding zero would have done, and for a negative
       zero is the same silence.

       The counter that paces retuning runs off the frame clock rather than off
       the voices, so it still has to arrive where the loop would have left it: a
       note starting in the next chunk must retune on the frame it would have. It
       counts down and wraps, so this is that countdown in closed form. */
    s->mod_counter = (int)(((unsigned)s->mod_counter + (unsigned)SYNTH_MOD_INTERVAL
                            - (unsigned)n_frames % (unsigned)SYNTH_MOD_INTERVAL)
                           % (unsigned)SYNTH_MOD_INTERVAL);
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

void synth_set_frame_time(synth_t *s, uint64_t frame)
{
    s->frame_time = frame;
    clock_publish(&s->clock, frame);
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
    synth_load_patch_n(s, patch, SYNTH_PARAM_COUNT);
}

void synth_load_patch_n(synth_t *s, const float *patch, int count)
{
    mod_t m;
    int i;

    if (count > SYNTH_PARAM_COUNT) {
        count = SYNTH_PARAM_COUNT;
    }
    for (i = 0; i < count; ++i) {
        s->params[i] = clamp01(patch[i]);
    }
    for (; i < SYNTH_PARAM_COUNT; ++i) {
        s->params[i] = k_param_info[i].default_norm;
    }

    mod_load(s, &m);
    terrain_refresh(s);

    /* One pass over the voices rather than one per parameter, which is what
       calling synth_set_param in a loop would cost — and only over the voices
       that are sounding, because an idle one is rebuilt from these same
       parameters by the note_on that claims it. */
    for (i = 0; i < SYNTH_MAX_VOICES; ++i) {
        synth_voice_t *v = &s->voices[i];

        if (!synth_env_is_active(&v->amp_env)) {
            continue;
        }
        voice_apply_osc(s, v);
        voice_apply_lfo(s, v);
        voice_apply_amp(s, &m, v);
        voice_apply_envelope(s, v);
        voice_apply_filter(s, &m, v);
        voice_apply_pitch(s, v);
        /* A modulation depth turned back down has to put the note where it
           belongs. The render loop stops retuning the oscillator once nothing
           moves the pitch, so without this the voice would hold whatever offset
           it had when the control was centred. */
        voice_tune_osc(&m, v);
    }
}

static void render_frames(synth_t *s, float *out, int n_frames, int add)
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

        render_chunk(s, out + done, chunk, add);
        done += chunk;
    }

    s->frame_time = start + (uint64_t)n_frames;
    clock_publish(&s->clock, s->frame_time);
}

void synth_render(synth_t *s, float *out, int n_frames)
{
    render_frames(s, out, n_frames, 0);
}

void synth_render_add(synth_t *s, float *out, int n_frames)
{
    render_frames(s, out, n_frames, 1);
}
