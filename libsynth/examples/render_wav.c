#include "demo.h"

#include <stdio.h>
#include <stdlib.h>

/* Offline renderer: no audio device, no dependencies, runs anywhere the core
   runs. Useful as a smoke test on a machine with no sound and as the reference
   output when checking a new platform backend against the host. */

#define SAMPLE_RATE 44100
#define SECONDS 8
#define BLOCK 512

static void put_u32(FILE *f, unsigned v)
{
    fputc((int)(v & 0xFFu), f);
    fputc((int)((v >> 8) & 0xFFu), f);
    fputc((int)((v >> 16) & 0xFFu), f);
    fputc((int)((v >> 24) & 0xFFu), f);
}

static void put_u16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xFFu), f);
    fputc((int)((v >> 8) & 0xFFu), f);
}

/* Written byte by byte so the file is identical whatever the host endianness. */
static void write_wav_header(FILE *f, unsigned frames, unsigned sample_rate)
{
    unsigned data_bytes = frames * 2u;

    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36u + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16u);            /* fmt chunk size  */
    put_u16(f, 1u);             /* PCM             */
    put_u16(f, 1u);             /* mono            */
    put_u32(f, sample_rate);
    put_u32(f, sample_rate * 2u); /* byte rate      */
    put_u16(f, 2u);             /* block align     */
    put_u16(f, 16u);            /* bits per sample */
    fwrite("data", 1, 4, f);
    put_u32(f, data_bytes);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "demo.wav";
    unsigned total_frames = (unsigned)(SAMPLE_RATE * SECONDS);
    unsigned written = 0;
    int clipped = 0;
    synth_t synth;
    demo_t demo;
    float block[BLOCK];
    FILE *f = fopen(path, "wb");

    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }

    write_wav_header(f, total_frames, SAMPLE_RATE);
    demo_init(&demo, &synth, (float)SAMPLE_RATE, 110.0f);

    while (written < total_frames) {
        unsigned remaining = total_frames - written;
        int frames = (remaining < BLOCK) ? (int)remaining : BLOCK;
        int i;

        demo_render(&demo, block, frames);

        for (i = 0; i < frames; ++i) {
            float v = block[i];
            int sample;

            if (v > 1.0f) {
                v = 1.0f;
                clipped = 1;
            } else if (v < -1.0f) {
                v = -1.0f;
                clipped = 1;
            }
            sample = (int)(v * 32767.0f);
            put_u16(f, (unsigned)(sample & 0xFFFF));
        }
        written += (unsigned)frames;
    }

    fclose(f);
    printf("wrote %s: %u frames, %d Hz, mono%s\n", path, total_frames, SAMPLE_RATE,
           clipped ? " (clipped)" : "");
    return 0;
}
