#ifndef SYNTH_DSP_H
#define SYNTH_DSP_H

#include "synth/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standalone DSP units. Usable on their own; the engine in synth.h wires them
   into voices. No allocation, no libm, no global state. */

/* Base-2 exponential, ~1e-5 relative error. Provided so pitch and curve maths
   stay libm-free on targets where linking libm is inconvenient. */
float synth_exp2f(float x);

typedef enum {
    SYNTH_WAVE_SINE = 0,
    SYNTH_WAVE_SAW,
    SYNTH_WAVE_SQUARE,
    SYNTH_WAVE_PD,       /* phase distortion, after the Casio CZ */
    SYNTH_WAVE_VOSIM,    /* Kaegi & Tempelaars sin^2 pulse train */
    SYNTH_WAVE_TERRAIN,  /* Mitsuhashi wave terrain: an orbit over a surface */
    SYNTH_WAVE_NOISE,    /* interpolated random values, after the prototype's RedNoise */
    SYNTH_WAVE_COUNT
} synth_wave_t;

#define SYNTH_VOSIM_MAX_PULSES 16

/* Every waveform here but noise is a pure function of the phase, so apart from
   the noise generator's own state the oscillator holds settings rather than
   running state. */
typedef struct {
    float sample_rate;
    float phase;     /* [0, 1) */
    float phase_inc;

    float pd_wanted; /* the knee the host asked for */
    float pd_knee;   /* derived: that knee, widened if the pitch demands it */
    float pd_rise;   /* derived warp slopes, kept out of the sample loop */
    float pd_fall;
    float pd_scale;  /* derived: centres the warped wave and bounds it to 1 */
    float pd_offset;

    float formant_hz;
    float vosim_decay;
    float pulse_rate;  /* derived: pulses per period */
    float vosim_dc;    /* derived: mean of the train, removed to centre it */
    int vosim_pulses;
    int vosim_fitting; /* derived: pulses that fit inside one period */

    float terrain_radius;
    float terrain_scale;  /* derived: normalises the orbit to a peak of 1 */
    float terrain_dc;
    int terrain_ratio;    /* y advances this many times per x turn */

    /* Noise is the one waveform that cannot be a function of the phase alone.
       The phase still sets its bandwidth: a new random value is drawn each
       cycle and interpolated across it, so a low note rumbles and a high one
       hisses. */
    unsigned noise_state;
    float noise_from;
    float noise_to;

    synth_wave_t wave;
} synth_osc_t;

void  synth_osc_init(synth_osc_t *osc, float sample_rate);
void  synth_osc_reset(synth_osc_t *osc); /* phase only, keeps tuning and timbre */
/* Selects the waveform, and derives whatever that waveform needs. Assigning
   `osc->wave` directly is not the same thing and will sound wrong: the phase
   distortion's warp and the VOSIM pulse layout are derived from the pitch and
   from their own controls, and each is computed only while its waveform is the
   one selected — a sine pays for neither. Switching is what brings the new
   waveform's derivation up to date. */
void  synth_osc_set_wave(synth_osc_t *osc, synth_wave_t wave);

void  synth_osc_set_freq(synth_osc_t *osc, float hz);
void  synth_osc_set_pd_knee(synth_osc_t *osc, float knee);
void  synth_osc_set_vosim(synth_osc_t *osc, float formant_hz, int pulses, float decay);
void  synth_osc_set_terrain(synth_osc_t *osc, float radius, int ratio);

/* The wave terrain's cross-section, which is what walking the orbit derives, and
   the one oscillator setting that costs real work to compute: a lap to find the
   surface's mean and peak along that orbit, since an arbitrary surface is
   neither centred nor bounded by 1. It depends on the radius and the ratio and
   on nothing else, so it belongs to whatever owns those — an instance, not a
   voice. Deriving it per voice cost a whole audio deadline for one move of
   either control. */
typedef struct {
    float radius;
    float scale;
    float dc;
    int ratio;
} synth_terrain_t;

/* Derives into `t`, or returns at once if it already holds this cross-section.
   Clamps as synth_osc_set_terrain() does, so an instance and an oscillator
   asked for the same numbers end up with the same answer. */
void  synth_terrain_set(synth_terrain_t *t, float radius, int ratio);

/* Points an oscillator at a cross-section already derived. */
void  synth_osc_set_terrain_from(synth_osc_t *osc, const synth_terrain_t *t);

/* Decorrelates the noise between voices. Any non-zero value will do; the engine
   seeds each voice from its index so a patch still renders identically twice. */
void  synth_osc_set_noise_seed(synth_osc_t *osc, unsigned seed);
float synth_osc_next(synth_osc_t *osc);

typedef enum {
    SYNTH_LFO_SINE = 0,
    SYNTH_LFO_TRIANGLE,
    SYNTH_LFO_SQUARE,
    SYNTH_LFO_RANDOM,   /* a new level each cycle, held: stepped modulation */
    SYNTH_LFO_COUNT
} synth_lfo_shape_t;

/* Deliberately not a synth_osc_t: an oscillator is 76 bytes of settings a
   modulator has no use for, and this runs at control rate rather than per
   sample. Output is bipolar, [-1, 1]. */
typedef struct {
    float sample_rate;
    float phase;
    float phase_inc;
    unsigned random_state;
    float random_value;
    synth_lfo_shape_t shape;
} synth_lfo_t;

void  synth_lfo_init(synth_lfo_t *lfo, float sample_rate, unsigned seed);
void  synth_lfo_set_rate(synth_lfo_t *lfo, float hz, int frames_per_step);
void  synth_lfo_retrigger(synth_lfo_t *lfo);
float synth_lfo_next(synth_lfo_t *lfo);

typedef enum {
    SYNTH_ENV_IDLE = 0,
    SYNTH_ENV_DELAY,
    SYNTH_ENV_ATTACK,
    SYNTH_ENV_HOLD,
    SYNTH_ENV_DECAY,
    SYNTH_ENV_SUSTAIN,
    SYNTH_ENV_RELEASE
} synth_env_stage_t;

/* DAHDSR, matching the envelope shape of the JSyn prototype.
   Times are in seconds, sustain is a level in [0, 1].

   The times are set through synth_env_set_times() rather than written into the
   struct, because each ramp also keeps how far it travels per sample. Dividing
   by a time once per sample was 20% of every instruction this library executed
   on a core without an FPU. */
typedef struct {
    float delay;
    float attack;
    float hold;
    float decay;
    float sustain;
    float release;

    float sample_rate;
    float dt;            /* derived: seconds per sample */
    float attack_rate;   /* derived: the fraction of each ramp a sample covers */
    float decay_rate;
    float release_rate;

    float level;
    float time;          /* seconds through delay and hold, a fraction elsewhere */
    float release_from;
    synth_env_stage_t stage;
} synth_env_t;

void  synth_env_init(synth_env_t *env, float sample_rate);
void  synth_env_set_times(synth_env_t *env, float delay, float attack, float hold,
                          float decay, float sustain, float release);
void  synth_env_set_sample_rate(synth_env_t *env, float sample_rate);
void  synth_env_gate_on(synth_env_t *env);
void  synth_env_gate_off(synth_env_t *env);
float synth_env_next(synth_env_t *env);

static inline int synth_env_is_active(const synth_env_t *env)
{
    return env->stage != SYNTH_ENV_IDLE;
}

typedef enum {
    SYNTH_FILTER_LOWPASS = 0,
    SYNTH_FILTER_HIGHPASS,
    SYNTH_FILTER_BANDPASS,
    SYNTH_FILTER_COUNT
} synth_filter_mode_t;

/* Topology-preserving state-variable filter: 12 dB/oct, stable at any cutoff,
   resonance independent of cutoff, and all three responses from one run. */
typedef struct {
    float sample_rate;
    float a1;
    float a2;
    float a3;
    float k;
    float ic1eq;
    float ic2eq;
} synth_filter_t;

/* The output stage: the envelope rides the level going in, so a note distorts
   hardest at its attack and cleans up as it decays, the way an overdriven amp
   behaves. */
typedef struct {
    float drive; /* 0 is exactly transparent */
    float level; /* set per note, carries velocity */
} synth_amp_t;

void  synth_amp_init(synth_amp_t *amp);
float synth_amp_shape(float x, float drive);
float synth_amp_next(const synth_amp_t *amp, float in, float env_level);

/*
 * A delay line, and the first unit here that needs more memory than its own
 * struct. The buffer belongs to the caller, like the synth_t does and for the
 * same reason: an MCU declares `static float line[24000];` and nothing in this
 * library ever allocates. Its length is the longest delay available.
 *
 * It is deliberately not part of synth_t. A groovebox wants one echo on a
 * track, or one on the whole mix, and that is the host's arrangement to make;
 * putting it in the engine would fix the answer and make a patch carry a
 * buffer size. The host sums its parts and runs this over the result.
 */
typedef struct {
    float *buffer;
    int len;
    int write;
    float sample_rate;
    float offset;    /* where the read point is, in frames behind the write */
    float target;    /* where it is heading, after a time change */
    float step;      /* frames it moves per frame, signed; zero when arrived */
    float feedback;
    float mix;       /* 0 is exactly the dry signal */
} synth_delay_t;

void  synth_delay_init(synth_delay_t *d, float *buffer, int frames, float sample_rate);
void  synth_delay_set(synth_delay_t *d, float seconds, float feedback, float mix);
void  synth_delay_clear(synth_delay_t *d); /* silences the line, keeps the settings */
float synth_delay_next(synth_delay_t *d, float in);

void  synth_filter_init(synth_filter_t *filter, float sample_rate);
void  synth_filter_set(synth_filter_t *filter, float cutoff_hz, float q);
void  synth_filter_reset(synth_filter_t *filter); /* clears state, keeps tuning */
float synth_filter_next(synth_filter_t *filter, float in, synth_filter_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_DSP_H */
