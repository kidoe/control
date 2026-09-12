/*
 * Not AAudio. A stand-in with the same names and types, so a machine with no
 * Android NDK can still put the bridge through a compiler.
 *
 * What this buys: every line of synth_jni.c that is ours — the mix, the track
 * bounds check, the channel expansion, the types flowing through them — is
 * compiled, warned about and type-checked on every push. What it cannot buy:
 * agreement with the real AAudio. If a signature here is wrong the build fails
 * loudly, which is a false alarm to fix in this file rather than a false pass,
 * and the real check is still a build against the NDK.
 *
 * Kept deliberately thin: only what backends/android/synth_jni.c refers to.
 */
#ifndef SYNTH_STUB_AAUDIO_H
#define SYNTH_STUB_AAUDIO_H

#include <stdint.h>

typedef int32_t aaudio_result_t;
typedef int32_t aaudio_format_t;
typedef int32_t aaudio_performance_mode_t;
typedef int32_t aaudio_sharing_mode_t;
typedef int32_t aaudio_data_callback_result_t;

enum {
    AAUDIO_OK = 0,
    AAUDIO_ERROR_ILLEGAL_ARGUMENT = -898,
    AAUDIO_FORMAT_PCM_FLOAT = 2,
    AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12,
    AAUDIO_SHARING_MODE_EXCLUSIVE = 0,
    AAUDIO_SHARING_MODE_SHARED = 1,
    AAUDIO_CALLBACK_RESULT_CONTINUE = 0,
    AAUDIO_CALLBACK_RESULT_STOP = 1
};

typedef struct AAudioStream AAudioStream;
typedef struct AAudioStreamBuilder AAudioStreamBuilder;

typedef aaudio_data_callback_result_t (*AAudioStream_dataCallback)(
    AAudioStream *stream, void *user_data, void *audio_data, int32_t num_frames);
typedef void (*AAudioStream_errorCallback)(AAudioStream *stream, void *user_data,
                                          aaudio_result_t error);

const char *AAudio_convertResultToText(aaudio_result_t result);
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **builder);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *builder, aaudio_format_t format);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *builder,
                                           aaudio_performance_mode_t mode);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *builder,
                                        aaudio_sharing_mode_t mode);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *builder, int32_t channel_count);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *builder,
                                         AAudioStream_dataCallback callback, void *user_data);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *builder,
                                          AAudioStream_errorCallback callback, void *user_data);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *builder,
                                               AAudioStream **stream);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *builder);
int32_t AAudioStream_getChannelCount(AAudioStream *stream);
int32_t AAudioStream_getSampleRate(AAudioStream *stream);
int32_t AAudioStream_getFramesPerBurst(AAudioStream *stream);
aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream *stream, int32_t num_frames);
aaudio_result_t AAudioStream_requestStart(AAudioStream *stream);
aaudio_result_t AAudioStream_requestStop(AAudioStream *stream);
aaudio_result_t AAudioStream_close(AAudioStream *stream);

#endif
