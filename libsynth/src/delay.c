#include "synth/dsp.h"

/*
 * The delay line, in its own translation unit for the same reason the MIDI
 * parser is in one: nothing in the core refers to it, so a target that does not
 * want an echo never links it. It costs about 550 bytes of flash on ARM.
 */

void synth_delay_init(synth_delay_t *d, float *buffer, int frames, float sample_rate)
{
    d->buffer = (frames > 1) ? buffer : 0;
    d->len = (frames > 1) ? frames : 0;
    d->sample_rate = (sample_rate > 0.0f) ? sample_rate : 44100.0f;
    d->write = 0;
    /* "Unset": the first time asked for is taken up at once, since there is
       nothing in the line yet to click. Every later change slides. */
    d->offset = -1.0f;
    d->target = 1.0f;
    d->feedback = 0.0f;
    d->mix = 0.0f;
    d->step = 0.0f;
    synth_delay_clear(d);
}

void synth_delay_clear(synth_delay_t *d)
{
    int i;

    for (i = 0; i < d->len; ++i) {
        d->buffer[i] = 0.0f;
    }
    d->write = 0;
}

void synth_delay_set(synth_delay_t *d, float seconds, float feedback, float mix)
{
    float frames = seconds * d->sample_rate;
    float longest = (float)(d->len - 1);

    if (frames < 1.0f) {
        frames = 1.0f;
    } else if (frames > longest) {
        frames = longest;
    }
    /* Any change takes the same 50 ms, whatever the distance: short enough to
       feel immediate, long enough that the read point slides rather than
       jumping. Held as a step per frame, like the voice glide, so it arrives
       on the time asked for rather than approaching it forever. */
    if (d->offset < 0.0f) {
        d->offset = frames; /* nothing in the line yet, so nothing to click */
        d->step = 0.0f;
    } else {
        d->step = (frames - d->offset) / (0.05f * d->sample_rate);
    }
    d->target = frames;

    /* Strictly below 1, so the line decays on its own rather than sustaining
       whatever is in it forever. */
    if (feedback < 0.0f) {
        feedback = 0.0f;
    } else if (feedback > 0.95f) {
        feedback = 0.95f;
    }
    d->feedback = feedback;
    d->mix = (mix < 0.0f) ? 0.0f : ((mix > 1.0f) ? 1.0f : mix);
}

/*
 * One frame in, one frame out.
 *
 * The value written into the line is clamped rather than saturated. Feedback
 * makes this the one place in the library where a bounded input does not give a
 * bounded signal — with feedback f the line would settle at 1/(1-f), which is
 * 20 at the maximum — so it is held to full scale on the way in. Everything
 * downstream then follows: the line is bounded by 1, and the output is a
 * crossfade between two things bounded by 1, so an instance with an echo on it
 * is bounded by 1 like any other. The clamp is only reachable by a patch that
 * has asked the echo to run away, and it will sound like it.
 */
float synth_delay_next(synth_delay_t *d, float in)
{
    float wet, written, pos, frac;
    int i0, i1;

    if (!d->buffer) {
        return in;
    }

    /* The read point slides toward a new time rather than jumping to it. That
       is what stops a click, and it is also where a tape delay's bend comes
       from: while it is moving, what comes out is resampled. */
    if (d->step != 0.0f) {
        d->offset += d->step;
        if ((d->step > 0.0f) ? (d->offset >= d->target) : (d->offset <= d->target)) {
            d->offset = d->target;
            d->step = 0.0f;
        }
    }

    pos = (float)d->write - d->offset;
    if (pos < 0.0f) {
        pos += (float)d->len;
    }
    i0 = (int)pos;
    if (i0 >= d->len) {
        i0 = d->len - 1;
    }
    frac = pos - (float)i0;
    i1 = (i0 + 1 >= d->len) ? 0 : (i0 + 1);
    wet = d->buffer[i0] + (d->buffer[i1] - d->buffer[i0]) * frac;

    written = in + wet * d->feedback;
    if (written > 1.0f) {
        written = 1.0f;
    } else if (written < -1.0f) {
        written = -1.0f;
    }
    d->buffer[d->write] = written;
    d->write = (d->write + 1 >= d->len) ? 0 : (d->write + 1);

    return in + (wet - in) * d->mix;
}
