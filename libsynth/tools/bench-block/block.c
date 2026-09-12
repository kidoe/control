/*
 * Cost of ONE 96-frame block, in instructions, with callgrind's instrumentation
 * switched on for exactly that call. Everything else — setup, warm-up, teardown
 * — runs with it off, so the number reported is the block and nothing else.
 *
 * Throughput averages hide the number that decides whether audio glitches. A
 * block has a deadline: at 48 kHz, 96 frames is 2 ms, which on a 168 MHz
 * Cortex-M4F is 336,000 cycles. What matters is the worst block, not the mean
 * one, and the worst block is where the events land.
 *
 * Built and run by run.sh; see there for the scenario list.
 */
#include "synth/synth.h"

#include <valgrind/callgrind.h>
#include <stdio.h>
#include <string.h>

/* Four tracks is what a groovebox asks for, and what the Android backend now
   holds. Each is an instance with its own patch, voices and queue; the host
   sums them. */
#define TRACKS 4

static synth_t s;
static synth_t tracks[TRACKS];
static float buf[96];

static void schedule(synth_event_type_t type, uint64_t frame, int index, float value)
{
    synth_event_t e;

    e.frame = frame;
    e.type = type;
    e.index = index;
    e.value = value;
    synth_schedule(&s, &e);
}

/* The mix cases render every track on every callback, silent ones included,
   because a track skipped is a track whose clock stops. */
static void render_mix(int n_tracks)
{
    int t;

    synth_render(&tracks[0], buf, 96);
    for (t = 1; t < n_tracks; ++t) {
        synth_render_add(&tracks[t], buf, 96);
    }
}

static int run_mix(const char *what)
{
    int voices_each = 2;

    if (strcmp(what, "mix4idle") == 0) {
        voices_each = 0;
    } else if (strcmp(what, "mix4full") == 0) {
        voices_each = SYNTH_MAX_VOICES;
    }
    int t, i;

    for (t = 0; t < TRACKS; ++t) {
        synth_init(&tracks[t], 48000.0f);
        synth_set_param(&tracks[t], SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
        for (i = 0; i < voices_each; ++i) {
            synth_note_on(&tracks[t], 32 + t * 3 + i * 2, 0.9f);
        }
    }
    for (i = 0; i < 50; ++i) {
        render_mix(TRACKS);
    }

    CALLGRIND_START_INSTRUMENTATION;
    render_mix(TRACKS);
    CALLGRIND_STOP_INSTRUMENTATION;

    printf("%g\n", (double)buf[0]);
    return 0;
}

int main(int argc, char **argv)
{
    const char *what = (argc > 1) ? argv[1] : "steady";
    uint64_t now;
    int i;

    if (strncmp(what, "mix", 3) == 0) {
        return run_mix(what);
    }

    synth_init(&s, 48000.0f);
    synth_set_param(&s, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);

    if (strcmp(what, "silent") != 0) {
        for (i = 0; i < 8; ++i) {
            synth_note_on(&s, 40 + i * 4, 0.9f);
        }
    }
    for (i = 0; i < 50; ++i) { /* past the attack, into the steady state */
        synth_render(&s, buf, 96);
    }
    now = synth_frame_time(&s);

    if (strcmp(what, "notes8") == 0) {
        for (i = 0; i < 8; ++i) {
            schedule(SYNTH_EVENT_NOTE_ON, now + (uint64_t)(i * 11), 52 + i * 3, 0.9f);
        }
    } else if (strcmp(what, "notes16") == 0) {
        for (i = 0; i < 16; ++i) {
            schedule(SYNTH_EVENT_NOTE_ON, now + (uint64_t)(i * 5), 52 + i, 0.9f);
        }
    } else if (strcmp(what, "param1") == 0) {
        schedule(SYNTH_EVENT_PARAM, now + 40u, SYNTH_PARAM_FILTER_CUTOFF, 0.6f);
    } else if (strcmp(what, "param16") == 0) {
        /* A knob being moved, or one bar of parameter automation arriving at
           once: the case a groovebox hits constantly. */
        for (i = 0; i < 16; ++i) {
            schedule(SYNTH_EVENT_PARAM, now + (uint64_t)(i * 5),
                     SYNTH_PARAM_FILTER_CUTOFF, 0.4f + 0.01f * (float)i);
        }
    }

    CALLGRIND_START_INSTRUMENTATION;
    synth_render(&s, buf, 96);
    CALLGRIND_STOP_INSTRUMENTATION;

    printf("%g\n", (double)buf[0]); /* so nothing above is optimized away */
    return 0;
}
