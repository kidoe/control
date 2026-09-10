#ifndef SYNTH_DEMO_H
#define SYNTH_DEMO_H

#include "synth/synth.h"

/*
 * A step sequencer small enough to read in one sitting, shared by the offline
 * renderer and the realtime player. It also shows the pattern a host should
 * follow: split the render block at event boundaries so notes land on the
 * right sample rather than on the block edge.
 */

typedef struct {
    synth_t *synth;
    int step_frames;
    int frames_to_next;
    int step;
    int held_note;
} demo_t;

static const int k_demo_notes[] = {
    45, 57, 60, 64, 67, 64, 60, 57,
    43, 55, 58, 62, 65, 62, 58, 55
};

#define DEMO_STEPS ((int)(sizeof(k_demo_notes) / sizeof(k_demo_notes[0])))

static void demo_init(demo_t *d, synth_t *s, float sample_rate, float bpm)
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

    d->synth = s;
    d->step_frames = (int)(sample_rate * 30.0f / bpm); /* eighth notes */
    d->frames_to_next = 0;
    d->step = 0;
    d->held_note = -1;
}

static void demo_step(demo_t *d)
{
    float sweep;

    if (d->held_note >= 0) {
        synth_note_off(d->synth, d->held_note);
    }
    d->held_note = k_demo_notes[d->step % DEMO_STEPS];
    synth_note_on(d->synth, d->held_note, 0.9f);

    /* Sweep the cutoff across the pattern so the filter is actually audible. */
    sweep = 0.25f + 0.55f * (float)(d->step % DEMO_STEPS) / (float)(DEMO_STEPS - 1);
    synth_set_param(d->synth, SYNTH_PARAM_FILTER_CUTOFF, sweep);

    d->step++;
    d->frames_to_next = d->step_frames;
}

static void demo_render(demo_t *d, float *out, int frames)
{
    int done = 0;

    while (done < frames) {
        int chunk = frames - done;

        if (d->frames_to_next <= 0) {
            demo_step(d);
        }
        if (chunk > d->frames_to_next) {
            chunk = d->frames_to_next;
        }
        synth_render(d->synth, out + done, chunk);
        d->frames_to_next -= chunk;
        done += chunk;
    }
}

#endif /* SYNTH_DEMO_H */
