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

    float pd_knee;   /* breakpoint of the phase warp; 0.5 reproduces a sine */
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
void  synth_osc_set_freq(synth_osc_t *osc, float hz);
void  synth_osc_set_pd_knee(synth_osc_t *osc, float knee);
void  synth_osc_set_vosim(synth_osc_t *osc, float formant_hz, int pulses, float decay);
void  synth_osc_set_terrain(synth_osc_t *osc, float radius, int ratio);

/* Decorrelates the noise between voices. Any non-zero value will do; the engine
   seeds each voice from its index so a patch still renders identically twice. */
void  synth_osc_set_noise_seed(synth_osc_t *osc, unsigned seed);
float synth_osc_next(synth_osc_t *osc);

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
   Times are in seconds, sustain is a level in [0, 1]. */
typedef struct {
    float delay;
    float attack;
    float hold;
    float decay;
    float sustain;
    float release;

    float sample_rate;
    float level;
    float time;
    float release_from;
    synth_env_stage_t stage;
} synth_env_t;

void  synth_env_init(synth_env_t *env, float sample_rate);
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

void  synth_filter_init(synth_filter_t *filter, float sample_rate);
void  synth_filter_set(synth_filter_t *filter, float cutoff_hz, float q);
void  synth_filter_reset(synth_filter_t *filter); /* clears state, keeps tuning */
float synth_filter_next(synth_filter_t *filter, float in, synth_filter_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_DSP_H */
