#ifndef SYNTH_H
#define SYNTH_H

#include "synth/config.h"
#include "synth/dsp.h"

#include <stdint.h>

/* The event queue's indices are read and written by two threads, so they have
   to be real atomics. C and C++ spell that differently and this header has to
   compile as both. */
#ifdef __cplusplus
#include <atomic>
#define SYNTH_ATOMIC_UINT std::atomic<unsigned>
#else
#include <stdatomic.h>
#define SYNTH_ATOMIC_UINT _Atomic unsigned
#endif

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
 *   - Control calls (note on/off, parameters) are not internally synchronized
 *     and belong to the audio thread. A host driving the engine from a UI or
 *     sequencer thread goes through synth_schedule(), which is the one entry
 *     point here that is safe to call from anywhere.
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
    SYNTH_PARAM_LFO_RATE,
    SYNTH_PARAM_LFO_SHAPE,
    SYNTH_PARAM_LFO_TO_CUTOFF,
    SYNTH_PARAM_LFO_TO_PITCH,
    SYNTH_PARAM_LFO_TO_AMP,
    /* New parameters go on the end, never in the middle: a host that stores a
       patch as the positional float array synth_save_patch() writes would
       silently load every old patch wrong if an index moved. */
    SYNTH_PARAM_PITCH_ENV_AMOUNT,
    SYNTH_PARAM_PITCH_ENV_ATTACK,
    SYNTH_PARAM_PITCH_ENV_DECAY,
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
    synth_env_t pitch_env;
    synth_lfo_t lfo;
    float amp_base;    /* the velocity part of the amp level, before tremolo */
    float lfo_value;   /* held between control-rate updates */
    int note;          /* MIDI note number; stays valid through the release */
    int held;          /* 1 while the key is down */
    float velocity;    /* [0, 1] */
    unsigned age;      /* allocation order, drives voice stealing */
} synth_voice_t;

typedef enum {
    SYNTH_EVENT_NOTE_ON = 0,
    SYNTH_EVENT_NOTE_OFF,
    SYNTH_EVENT_PARAM,
    SYNTH_EVENT_PITCH_BEND,
    SYNTH_EVENT_ALL_NOTES_OFF
} synth_event_type_t;

typedef struct {
    uint64_t frame;          /* absolute, on the synth_frame_time() clock */
    synth_event_type_t type;
    int index;               /* MIDI note, or synth_param_t */
    float value;             /* velocity or normalized parameter, [0, 1];
                                semitones for a pitch bend */
} synth_event_t;

/* Publishes the 64-bit frame counter to other threads using only 32-bit
   atomics, which are lock-free on every target this library runs on. A 64-bit
   atomic would not be: on 32-bit ARM it becomes a call into libatomic, and on a
   Cortex-M0+ there is no instruction to build one from at all.

   The writer bumps the sequence to an odd value, writes the halves, then bumps
   it to the next even one. A reader that sees an odd sequence, or a different
   one either side of its read, tries again. */
typedef struct {
    SYNTH_ATOMIC_UINT sequence;
    SYNTH_ATOMIC_UINT low;
    SYNTH_ATOMIC_UINT high;
} synth_frame_clock_t;

/* Single producer, single consumer. The producer only ever advances tail, the
   consumer only ever advances head, so neither needs a read-modify-write and
   the queue works on targets with no atomic RMW at all. */
typedef struct {
    synth_event_t events[SYNTH_EVENT_QUEUE_LEN];
    SYNTH_ATOMIC_UINT head;
    SYNTH_ATOMIC_UINT tail;
} synth_event_queue_t;

/* Fields are private. They live in the header only so the caller can place the
   instance in static storage on targets without a heap. */
typedef struct {
    float sample_rate;
    float params[SYNTH_PARAM_COUNT]; /* normalized, [0, 1] */
    synth_voice_t voices[SYNTH_MAX_VOICES];
    unsigned age_counter;
    int mod_counter;    /* paces filter retuning, see SYNTH_MOD_INTERVAL */
    float pitch_bend;   /* semitones, applied on top of every note */
    synth_event_queue_t queue;
    uint64_t frame_time;          /* the audio thread's own copy */
    synth_frame_clock_t clock;    /* the copy other threads may read */
} synth_t;

/* Lifecycle */
void synth_init(synth_t *s, float sample_rate);
void synth_reset(synth_t *s); /* silences every voice, keeps parameters */

/* Retune to a new rate, for hosts that only learn the device's rate once the
   device is open. Silences every voice; parameters are kept. */
void synth_set_sample_rate(synth_t *s, float sample_rate);

/* Audio. Writes n_frames of mono samples, overwriting `out`. Real-time safe.
   Also drains every event that falls due inside the block, splitting the render
   at their frame offsets so a step lands on its own frame rather than on the
   buffer boundary. */
void synth_render(synth_t *s, float *out, int n_frames);

/*
 * Scheduling.
 *
 * synth_schedule() is the one function that may be called from a thread other
 * than the audio one, and the only safe way to drive the engine from a UI or
 * sequencer thread: everything else in this header assumes the caller is the
 * audio thread. It never blocks and never allocates; it returns 0 when the
 * queue is full, which is a signal to schedule less far ahead or to raise
 * SYNTH_EVENT_QUEUE_LEN.
 *
 * Events are applied in the order they were scheduled. An event whose frame has
 * already passed is applied at the start of the next block rather than dropped:
 * a step that is late recovers, a step that is silent does not.
 *
 * One queue belongs to one synth_t. A host wanting several independent parts,
 * each with its own patch and voices, uses several synth_t and schedules into
 * whichever one owns the part.
 */
int synth_schedule(synth_t *s, const synth_event_t *event);

/* Frames rendered since synth_init(). The clock a host's sequencer plans
   against; it advances only inside synth_render(), so it never drifts from the
   audio stream the way a wall clock does. Safe to read from another thread. */
uint64_t synth_frame_time(const synth_t *s);

/* Moves this instance's clock onto a timeline already in progress. Each clock
   starts at zero, so a part created mid-session would otherwise be numbering
   frames its neighbours passed long ago and every event scheduled for it would
   arrive late. Call it once, from the audio thread, before the new part renders
   anything. */
void synth_set_frame_time(synth_t *s, uint64_t frame);

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

/*
 * Patches. A patch is exactly the normalized parameters, so it is a plain array
 * of floats with no format of its own: a groovebox giving each track its own
 * sound stores one of these per track.
 *
 * The array is positional, tied to the parameter list this build was compiled
 * with. That is fine to keep in memory or in a session file written and read by
 * the same binary. To survive a version change, store the names from
 * synth_param_info() alongside the values and match on those.
 */
void synth_save_patch(const synth_t *s, float patch[SYNTH_PARAM_COUNT]);
void synth_load_patch(synth_t *s, const float patch[SYNTH_PARAM_COUNT]);

/* Loads a patch that a build with fewer parameters wrote. `count` is how many
   floats the array holds; the rest take their defaults, which is the value that
   patch was implicitly using. Zero-filling them instead would be wrong for
   every bipolar control, where the centre is neutral and zero is full negative:
   a saved sound would come back four octaves down rather than unchanged. Extra
   values, from a build with more parameters than this one, are ignored. */
void synth_load_patch_n(synth_t *s, const float *patch, int count);

/* Descriptors, for hosts that build UI or MIDI maps from the parameter list. */
const synth_param_info_t *synth_param_info(synth_param_t param);
float synth_param_denorm(synth_param_t param, float norm);

/* Equal temperament, A4 = 440 Hz. */
float synth_note_to_hz(float note);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_H */
