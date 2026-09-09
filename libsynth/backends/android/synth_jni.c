#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>

#include "synth/synth.h"

/* Android backend: an AAudio stream whose data callback feeds the core. As on
   every other target, nothing below this file knows what platform it is on. */

#define LOG_TAG "libsynth"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static synth_t g_synth;
static AAudioStream *g_stream;

static aaudio_data_callback_result_t audio_callback(AAudioStream *stream, void *user_data,
                                                    void *audio_data, int32_t num_frames)
{
    (void)stream;
    (void)user_data;

    synth_render(&g_synth, (float *)audio_data, (int)num_frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Fires on another thread when the device goes away (headphones unplugged, for
   instance). Closing the stream from in here would deadlock, so the app is
   expected to call stop() then start() again. */
static void error_callback(AAudioStream *stream, void *user_data, aaudio_result_t error)
{
    (void)stream;
    (void)user_data;

    LOGE("audio stream error: %s", AAudio_convertResultToText(error));
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved;

    /* Valid engine before any stream exists, so notes and parameters set
       ahead of start() are never applied to uninitialised memory. */
    synth_init(&g_synth, 48000.0f);
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_start(JNIEnv *env, jclass clazz)
{
    AAudioStreamBuilder *builder = 0;
    aaudio_result_t result;
    int32_t burst;

    (void)env;
    (void)clazz;

    if (g_stream) {
        return JNI_TRUE;
    }

    result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) {
        LOGE("createStreamBuilder: %s", AAudio_convertResultToText(result));
        return JNI_FALSE;
    }

    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
    AAudioStreamBuilder_setDataCallback(builder, audio_callback, 0);
    AAudioStreamBuilder_setErrorCallback(builder, error_callback, 0);

    result = AAudioStreamBuilder_openStream(builder, &g_stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        LOGE("openStream: %s", AAudio_convertResultToText(result));
        g_stream = 0;
        return JNI_FALSE;
    }

    /* The device grants whatever rate it likes, so retune before it can run.
       This keeps parameters the app already set. */
    synth_set_sample_rate(&g_synth, (float)AAudioStream_getSampleRate(g_stream));

    burst = AAudioStream_getFramesPerBurst(g_stream);
    AAudioStream_setBufferSizeInFrames(g_stream, burst * 2);

    result = AAudioStream_requestStart(g_stream);
    if (result != AAUDIO_OK) {
        LOGE("requestStart: %s", AAudio_convertResultToText(result));
        AAudioStream_close(g_stream);
        g_stream = 0;
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_stop(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    if (!g_stream) {
        return;
    }
    AAudioStream_requestStop(g_stream);
    AAudioStream_close(g_stream);
    g_stream = 0;
}

JNIEXPORT jint JNICALL Java_com_kidoe_synth_SynthEngine_sampleRate(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    return g_stream ? (jint)AAudioStream_getSampleRate(g_stream) : 0;
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_noteOn(JNIEnv *env, jclass clazz,
                                                               jint note, jfloat velocity)
{
    (void)env;
    (void)clazz;

    synth_note_on(&g_synth, (int)note, (float)velocity);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_noteOff(JNIEnv *env, jclass clazz, jint note)
{
    (void)env;
    (void)clazz;

    synth_note_off(&g_synth, (int)note);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_allNotesOff(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    synth_all_notes_off(&g_synth);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_setParam(JNIEnv *env, jclass clazz,
                                                                 jint param, jfloat norm)
{
    (void)env;
    (void)clazz;

    synth_set_param(&g_synth, (synth_param_t)param, (float)norm);
}

JNIEXPORT jfloat JNICALL Java_com_kidoe_synth_SynthEngine_getParam(JNIEnv *env, jclass clazz, jint param)
{
    (void)env;
    (void)clazz;

    return (jfloat)synth_get_param(&g_synth, (synth_param_t)param);
}

JNIEXPORT jint JNICALL Java_com_kidoe_synth_SynthEngine_paramCount(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    return (jint)SYNTH_PARAM_COUNT;
}

JNIEXPORT jstring JNICALL Java_com_kidoe_synth_SynthEngine_paramName(JNIEnv *env, jclass clazz, jint param)
{
    const synth_param_info_t *info = synth_param_info((synth_param_t)param);

    (void)clazz;

#if SYNTH_PARAM_NAMES
    return info ? (*env)->NewStringUTF(env, info->name) : 0;
#else
    (void)info;
    (void)env;
    return 0;
#endif
}
