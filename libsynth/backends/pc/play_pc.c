#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "demo.h"

#include <stdio.h>

/* Desktop backend: open a device, hand its callback straight to the core.
   Everything platform-specific lives in miniaudio (ALSA/PulseAudio on Linux,
   CoreAudio on macOS, WASAPI on Windows); there is no DSP below this line. */

static synth_t g_synth;
static demo_t g_demo;

static void data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
    (void)device;
    (void)input;
    demo_render(&g_demo, (float *)output, (int)frame_count);
}

int main(void)
{
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    ma_device device;

    config.playback.format = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate = 48000;
    config.dataCallback = data_callback;

    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        fprintf(stderr, "no audio playback device available\n");
        return 1;
    }

    /* The device may not have granted the rate we asked for, so the engine is
       configured from what it actually opened. */
    demo_init(&g_demo, &g_synth, (float)device.sampleRate, 110.0f);

    if (ma_device_start(&device) != MA_SUCCESS) {
        fprintf(stderr, "could not start playback\n");
        ma_device_uninit(&device);
        return 1;
    }

    printf("playing through %s at %u Hz, press enter to stop\n",
           device.playback.name, device.sampleRate);
    (void)getchar();

    ma_device_uninit(&device);
    return 0;
}
