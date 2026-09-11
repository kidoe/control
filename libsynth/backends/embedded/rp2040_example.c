/*
 * Reference shape for a bare-metal target, written against the Raspberry Pi
 * Pico (RP2040, Cortex-M0+). It is not firmware: there is no Pico SDK here, no
 * I2S driver and no clock setup. What it is for is to be compiled with a real
 * cross toolchain and measured, so the library's embedded claims are numbers
 * rather than intentions.
 *
 *   arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os \
 *       -Ilibsynth/include -c libsynth/backends/embedded/rp2040_example.c
 *   arm-none-eabi-size rp2040_example.o
 *
 * The pattern to copy is the storage: every object below is static, so the
 * linker accounts for all of it up front and nothing ever reaches a heap.
 */

#include "synth/midi.h"
#include "synth/synth.h"

/* One DMA half-buffer's worth. A real driver would double-buffer these and
   render into whichever half the DMA is not reading. */
#define FRAMES_PER_BLOCK 64

static synth_t g_synth;
static synth_midi_t g_midi;
static float g_block[FRAMES_PER_BLOCK];
static int16_t g_i2s[FRAMES_PER_BLOCK];

void synth_board_init(float sample_rate)
{
    synth_init(&g_synth, sample_rate);
    synth_midi_init(&g_midi);
}

/* Call from the UART or USB interrupt as bytes arrive. The parser keeps no
   state beyond what it is handed, so this costs a few instructions a byte. */
void synth_board_midi_byte(unsigned char byte)
{
    synth_midi_feed(&g_midi, &g_synth, byte);
}

/* Call from the I2S DMA completion interrupt. Converts to the 16-bit stereo
   frames most codecs want, duplicating the mono core across both channels. */
const int16_t *synth_board_render(void)
{
    int i;

    synth_render(&g_synth, g_block, FRAMES_PER_BLOCK);

    for (i = 0; i < FRAMES_PER_BLOCK; ++i) {
        float sample = g_block[i];

        if (sample > 1.0f) {
            sample = 1.0f;
        } else if (sample < -1.0f) {
            sample = -1.0f;
        }
        g_i2s[i] = (int16_t)(sample * 32767.0f);
    }

    return g_i2s;
}

/* A sequencer on the board schedules against the same frame clock a host would,
   so a step lands on its frame rather than on the DMA block boundary. */
int synth_board_schedule_note(uint64_t frame, int note, float velocity)
{
    synth_event_t event;

    event.frame = frame;
    event.type = SYNTH_EVENT_NOTE_ON;
    event.index = note;
    event.value = velocity;

    return synth_schedule(&g_synth, &event);
}

uint64_t synth_board_frame_time(void)
{
    return synth_frame_time(&g_synth);
}
