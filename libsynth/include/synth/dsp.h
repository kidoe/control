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
    SYNTH_WAVE_COUNT
} synth_wave_t;

typedef struct {
    float phase;     /* [0, 1) */
    float phase_inc;
} synth_osc_t;

void  synth_osc_reset(synth_osc_t *osc);
void  synth_osc_set_freq(synth_osc_t *osc, float hz, float sample_rate);
float synth_osc_next(synth_osc_t *osc, synth_wave_t wave);

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

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_DSP_H */
