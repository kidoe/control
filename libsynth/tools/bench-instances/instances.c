/*
 * What several engines cost, per second of audio, in instructions.
 *
 * A groovebox is not one synth with a channel argument: it is several synth_t,
 * one per track, summed by the host. That arrangement has two costs worth
 * knowing before choosing it — what a sounding voice costs, which is the same
 * wherever it sits, and what an instance costs merely for existing, which is
 * paid by every track on every block whether or not it makes a sound.
 *
 * One second is 500 blocks of 96 frames at 48 kHz. Instrumentation is on for
 * exactly the render loop, so setup and teardown are not in the count.
 *
 * Built and run by run.sh; see there for the case list.
 */
#include "synth/synth.h"

#include <valgrind/callgrind.h>
#include <stdio.h>
#include <string.h>

#define BLOCK 96
#define BLOCKS 500
#define MAX_INSTANCES 8

static synth_t parts[MAX_INSTANCES];
static float buf[BLOCK];

int main(int argc, char **argv)
{
    const char *what = (argc > 1) ? argv[1] : "one8";
    int instances = 8;
    int notes_each = 1;
    int i, n, p;

    if (strcmp(what, "one8") == 0) {
        instances = 1;
        notes_each = 8;
    } else if (strcmp(what, "eight1") == 0) {
        instances = 8;
        notes_each = 1;
    } else if (strcmp(what, "eight0") == 0) {
        instances = 8;
        notes_each = 0;
    } else if (strcmp(what, "eight8") == 0) {
        instances = 8;
        notes_each = 8;
    } else {
        fprintf(stderr, "unknown case: %s\n", what);
        return 2;
    }
    if (notes_each > SYNTH_MAX_VOICES) {
        notes_each = SYNTH_MAX_VOICES; /* more notes than slots is voice stealing,
                                          which measures something else */
    }

    for (p = 0; p < instances; ++p) {
        synth_init(&parts[p], 48000.0f);
        /* A note that has decayed is a silent voice, and then the measurement
           would be of the release rather than of a second of sound. */
        synth_set_param(&parts[p], SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
        for (i = 0; i < notes_each; ++i) {
            synth_note_on(&parts[p], 40 + p * 3 + i * 2, 0.9f);
        }
    }
    for (n = 0; n < 20; ++n) { /* past the attack, into the steady state */
        synth_render(&parts[0], buf, BLOCK);
        for (p = 1; p < instances; ++p) {
            synth_render_add(&parts[p], buf, BLOCK);
        }
    }

    CALLGRIND_START_INSTRUMENTATION;
    for (n = 0; n < BLOCKS; ++n) {
        /* The first part writes, the rest add: one buffer, no scratch, and the
           arrangement a host actually uses. */
        synth_render(&parts[0], buf, BLOCK);
        for (p = 1; p < instances; ++p) {
            synth_render_add(&parts[p], buf, BLOCK);
        }
    }
    CALLGRIND_STOP_INSTRUMENTATION;

    printf("%g\n", (double)buf[0]); /* so nothing above is optimized away */
    return 0;
}
