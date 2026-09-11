#ifndef SYNTH_H
#define SYNTH_H

#include "synth/config.h"
#include "synth/dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable polyphonic synthesis core.
 *
 * Contract with the host:
 *   - The engine never allocates, blocks, or calls into the OS. The caller owns
 *     the synth_t storage (`static synth_t s;` on an MCU, malloc elsewhere).
 *   - synth_render() is the only function meant for the audio callback, and it
 *     is real-time safe.
 *   - Control calls (note on/off, parameters) are not internally synchronized.
 *     Call them from the audio thread, or marshal them there from the host's
 *     own queue.
 */

typedef enum {
    SYNTH_PARAM_MASTER_GAIN = 0,
    SYNTH_PARAM_OSC_WAVE,
    SYNTH_PARAM_AMP_DELAY,
    SYNTH_PARAM_AMP_ATTACK,
    SYNTH_PARAM_AMP_HOLD,
    SYNTH_PARAM_AMP_DECAY,
    SYNTH_PARAM_AMP_SUSTAIN,
    SYNTH_PARAM_AMP_RELEASE,
    SYNTH_PARAM_FILTER_MODE,
    SYNTH_PARAM_FILTER_CUTOFF,
    SYNTH_PARAM_FILTER_Q,
    SYNTH_PARAM_PD_KNEE,
    SYNTH_PARAM_VOSIM_FORMANT,
    SYNTH_PARAM_VOSIM_PULSES,
    SYNTH_PARAM_VOSIM_DECAY,
    SYNTH_PARAM_TERRAIN_RADIUS,
    SYNTH_PARAM_TERRAIN_RATIO,
    SYNTH_PARAM_FILTER_ENV_AMOUNT,
    SYNTH_PARAM_FILTER_ENV_ATTACK,
    SYNTH_PARAM_FILTER_ENV_DECAY,
    SYNTH_PARAM_FILTER_ENV_SUSTAIN,
    SYNTH_PARAM_FILTER_ENV_RELEASE,
    SYNTH_PARAM_FILTER_KEY_TRACK,
    SYNTH_PARAM_AMP_DRIVE,
    SYNTH_PARAM_AMP_VELOCITY,
    SYNTH_PARAM_COUNT
} synth_param_t;

typedef enum {
    SYNTH_CURVE_LINEAR = 0,
    SYNTH_CURVE_CUBIC,   /* knob-style taper, replaces the prototype's exponential slider model */
    SYNTH_CURVE_STEPPED
} synth_curve_t;

typedef struct {
#if SYNTH_PARAM_NAMES
    const char *name;
#endif
    float min;
    float max;
    float default_norm; /* normalized default, [0, 1] */
    synth_curve_t curve;
} synth_param_info_t;

typedef struct {
    synth_osc_t osc;
    synth_filter_t filter;
    synth_amp_t amp;
    synth_env_t amp_env;
    synth_env_t filter_env;
    int note;          /* MIDI note number; stays valid through the release */
    int held;          /* 1 while the key is down */
    float velocity;    /* [0, 1] */
    unsigned age;      /* allocation order, drives voice stealing */
} synth_voice_t;

/* Fields are private. They live in the header only so the caller can place the
   instance in static storage on targets without a heap. */
typedef struct {
    float sample_rate;
    float params[SYNTH_PARAM_COUNT]; /* normalized, [0, 1] */
    synth_voice_t voices[SYNTH_MAX_VOICES];
    unsigned age_counter;
    int mod_counter;    /* paces filter retuning, see SYNTH_MOD_INTERVAL */
    float pitch_bend;   /* semitones, applied on top of every note */
} synth_t;

/* Lifecycle */
void synth_init(synth_t *s, float sample_rate);
void synth_reset(synth_t *s); /* silences every voice, keeps parameters */

/* Retune to a new rate, for hosts that only learn the device's rate once the
   device is open. Silences every voice; parameters are kept. */
void synth_set_sample_rate(synth_t *s, float sample_rate);

/* Audio. Writes n_frames of mono samples, overwriting `out`. Real-time safe. */
void synth_render(synth_t *s, float *out, int n_frames);

/* Bends every sounding voice, and every voice started afterwards, by this many
   semitones. Fractional and signed; 0 is no bend. */
void synth_set_pitch_bend(synth_t *s, float semitones);

/* Notes. velocity is [0, 1]; note is a MIDI note number. */
void synth_note_on(synth_t *s, int note, float velocity);
void synth_note_off(synth_t *s, int note);
void synth_all_notes_off(synth_t *s);
int  synth_active_voices(const synth_t *s);

/* Parameters, always normalized to [0, 1] so a MIDI CC (v/127), an ADC read or
   a UI slider all map onto them directly. */
void  synth_set_param(synth_t *s, synth_param_t param, float norm);
float synth_get_param(const synth_t *s, synth_param_t param);

/* Descriptors, for hosts that build UI or MIDI maps from the parameter list. */
const synth_param_info_t *synth_param_info(synth_param_t param);
float synth_param_denorm(synth_param_t param, float norm);

/* Equal temperament, A4 = 440 Hz. */
float synth_note_to_hz(float note);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_H */
