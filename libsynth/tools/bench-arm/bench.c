/* Renders BENCH_BLOCKS blocks of 96 frames on a bare-metal Cortex-M model.
   The instruction count comes from a QEMU TCG plugin outside, so this program
   only has to run the workload and stop; BENCH_BLOCKS=0 measures the fixed
   overhead to subtract. */
#include "synth/synth.h"

#ifndef BENCH_BLOCKS
#define BENCH_BLOCKS 500          /* 500 * 96 = one second at 48 kHz */
#endif
#ifndef BENCH_WAVE
#define BENCH_WAVE SYNTH_WAVE_SINE
#endif
#ifndef BENCH_VOICES
#define BENCH_VOICES 8
#endif
#ifndef BENCH_SWEEP
#define BENCH_SWEEP 0
#endif
#ifndef BENCH_LFO_CUTOFF
#define BENCH_LFO_CUTOFF 0
#endif
#ifndef BENCH_LFO_PITCH
#define BENCH_LFO_PITCH 0
#endif

static synth_t g_synth;
static float g_block[96];

static int semihost(int op, void *arg)
{
    register int r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;

    __asm__ volatile ("bkpt #0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

int main(void)
{
    unsigned exit_block[2];
    int i;

    synth_init(&g_synth, 48000.0f);
    synth_set_param(&g_synth, SYNTH_PARAM_OSC_WAVE,
                    (float)BENCH_WAVE / (float)(SYNTH_WAVE_COUNT - 1));
    synth_set_param(&g_synth, SYNTH_PARAM_AMP_SUSTAIN, 1.0f);
#if BENCH_SWEEP
    synth_set_param(&g_synth, SYNTH_PARAM_PITCH_ENV_AMOUNT, 0.75f);
    synth_set_param(&g_synth, SYNTH_PARAM_PITCH_ENV_DECAY, 0.6f);
#endif
#if BENCH_LFO_CUTOFF
    synth_set_param(&g_synth, SYNTH_PARAM_LFO_TO_CUTOFF, 0.85f);
#endif
#if BENCH_LFO_PITCH
    synth_set_param(&g_synth, SYNTH_PARAM_LFO_TO_PITCH, 0.75f);
#endif
    for (i = 0; i < BENCH_VOICES; ++i) {
        synth_note_on(&g_synth, 40 + i * 4, 0.9f);
    }

    for (i = 0; i < BENCH_BLOCKS; ++i) {
        synth_render(&g_synth, g_block, 96);
    }

    exit_block[0] = 0x20026u; /* ADP_Stopped_ApplicationExit */
    exit_block[1] = 0u;
    semihost(0x18, exit_block);
    for (;;) { }
}
