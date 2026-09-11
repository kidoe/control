#ifndef SYNTH_DEMO_H
#define SYNTH_DEMO_H

#include "synth/synth.h"

/*
 * A step sequencer small enough to read in one sitting, shared by the offline
 * renderer and the realtime player. It also shows the two patterns a host
 * should follow: split the render block at event boundaries so notes land on
 * the right sample rather than on the block edge, and give each part its own
 * synth_t rather than looking for a channel argument that does not exist.
 *
 * There are two parts here, a melodic line and a kick drum, because they are
 * two different sounds and a patch belongs to an instance. The host sums them
 * and owns the headroom: each instance is bounded by 1 on its own, so two of
 * them at full level would reach 2.
 */

typedef struct {
    synth_t *lead;
    synth_t *drums;
    int step_frames;
    int frames_to_next;
    int step;
    int held_note;
} demo_t;

static const int k_demo_notes[] = {
    45, 57, 60, 64, 67, 64, 60, 57,
    43, 55, 58, 62, 65, 62, 58, 55
};

/* Eighth notes, so this is two bars: a kick on every downbeat and a pickup
   before the second one. */
static const int k_demo_kicks[] = {
    1, 0, 0, 1, 0, 0, 1, 0,
    1, 0, 0, 1, 0, 0, 1, 1
};

#define DEMO_STEPS ((int)(sizeof(k_demo_notes) / sizeof(k_demo_notes[0])))

/* The kick's note. It is swept onto from an octave and a half above, which is
   the whole reason it reads as a drum and not as a low sine beep. */
#define DEMO_KICK_NOTE 33

/* Frames the two parts are summed in at a time. Any size works; a fixed one
   keeps the mixing buffer off the heap, which is the point. */
#define DEMO_MIX_CHUNK 128

/* A synthesized kick: a sine swept quickly down onto a low note, with an
   amplitude envelope short enough that it is over before the note is. */
static void demo_init_kick(synth_t *s, float sample_rate)
{
    synth_init(s, sample_rate);
    synth_set_param(s, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_SINE / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(s, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.6875f); /* +1.5 octaves */
    synth_set_param(s, SYNTH_PARAM_PITCH_ENV_ATTACK, 0.0f);    /* instant */
    synth_set_param(s, SYNTH_PARAM_PITCH_ENV_DECAY, 0.215f);   /* about 40 ms */
    synth_set_param(s, SYNTH_PARAM_AMP_ATTACK, 0.0f);
    synth_set_param(s, SYNTH_PARAM_AMP_DECAY, 0.315f);         /* about 250 ms */
    synth_set_param(s, SYNTH_PARAM_AMP_SUSTAIN, 0.0f);
    synth_set_param(s, SYNTH_PARAM_AMP_RELEASE, 0.0f);
    synth_set_param(s, SYNTH_PARAM_MASTER_GAIN, 0.55f);
}

static void demo_init(demo_t *d, synth_t *s, synth_t *drums, float sample_rate, float bpm)
{
    synth_init(s, sample_rate);
    /* osc_wave is a stepped parameter over the whole wave list, so the
       normalised value has to be derived from the enum. A hardcoded constant
       silently selects a different waveform as soon as a wave is added. */
    synth_set_param(s, SYNTH_PARAM_OSC_WAVE,
                    (float)SYNTH_WAVE_SAW / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(s, SYNTH_PARAM_AMP_ATTACK, 0.15f);
    synth_set_param(s, SYNTH_PARAM_AMP_DECAY, 0.35f);
    synth_set_param(s, SYNTH_PARAM_AMP_SUSTAIN, 0.3f);
    synth_set_param(s, SYNTH_PARAM_AMP_RELEASE, 0.3f);
    synth_set_param(s, SYNTH_PARAM_FILTER_Q, 0.35f);
    /* Low, because a Q of ~5 lets the filter's resonant peak add well over
       10 dB whenever the sweep crosses a harmonic of the saw. */
    synth_set_param(s, SYNTH_PARAM_MASTER_GAIN, 0.18f);

    demo_init_kick(drums, sample_rate);

    d->lead = s;
    d->drums = drums;
    d->step_frames = (int)(sample_rate * 30.0f / bpm); /* eighth notes */
    d->frames_to_next = 0;
    d->step = 0;
    d->held_note = -1;
}

static void demo_step(demo_t *d)
{
    float sweep;

    int index = d->step % DEMO_STEPS;

    if (d->held_note >= 0) {
        synth_note_off(d->lead, d->held_note);
    }
    d->held_note = k_demo_notes[index];
    synth_note_on(d->lead, d->held_note, 0.9f);

    /* The kick retriggers rather than being released: its amplitude envelope
       is what ends it, so holding the key would change nothing. */
    if (k_demo_kicks[index]) {
        synth_note_on(d->drums, DEMO_KICK_NOTE, 1.0f);
    }

    /* Sweep the cutoff across the pattern so the filter is actually audible. */
    sweep = 0.25f + 0.55f * (float)index / (float)(DEMO_STEPS - 1);
    synth_set_param(d->lead, SYNTH_PARAM_FILTER_CUTOFF, sweep);

    d->step++;
    d->frames_to_next = d->step_frames;
}

static void demo_render(demo_t *d, float *out, int frames)
{
    int done = 0;

    while (done < frames) {
        float drums[DEMO_MIX_CHUNK];
        int chunk = frames - done;
        int i;

        if (d->frames_to_next <= 0) {
            demo_step(d);
        }
        if (chunk > d->frames_to_next) {
            chunk = d->frames_to_next;
        }
        if (chunk > DEMO_MIX_CHUNK) {
            chunk = DEMO_MIX_CHUNK;
        }
        synth_render(d->lead, out + done, chunk);
        synth_render(d->drums, drums, chunk);
        for (i = 0; i < chunk; ++i) {
            out[done + i] += drums[i];
        }
        d->frames_to_next -= chunk;
        done += chunk;
    }
}

#endif /* SYNTH_DEMO_H */
