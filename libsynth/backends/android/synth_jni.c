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
static int g_channels = 1;

static aaudio_data_callback_result_t audio_callback(AAudioStream *stream, void *user_data,
                                                    void *audio_data, int32_t num_frames)
{
    float *out = (float *)audio_data;
    int channels = g_channels;
    int frame;
    int channel;

    (void)stream;
    (void)user_data;

    synth_render(&g_synth, out, (int)num_frames);

    /* The engine is mono but the device decides how many channels it grants.
       Expanding from the back lets the same buffer hold both, with no scratch
       memory and no allocation on the audio thread. */
    for (frame = (int)num_frames - 1; channels > 1 && frame >= 0; --frame) {
        float sample = out[frame];

        for (channel = 0; channel < channels; ++channel) {
            out[frame * channels + channel] = sample;
        }
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Fires on another thread when the device goes away, headphones unplugged being
   the usual case. AAudio forbids closing the stream from inside this callback,
   so recovery cannot happen here: the stream stays dead, silently, until the
   app calls stop() and then start() again. That is the app's job and start()
   says so. */
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
    int attempt;

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
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, audio_callback, 0);
    AAudioStreamBuilder_setErrorCallback(builder, error_callback, 0);

    /* Exclusive mono is what a groovebox wants, but it is not available
       everywhere: emulators in particular downgrade exclusive mode internally
       and then reject the configuration outright, which reads to the app as
       silence with nothing in the log. So ask for the best case, then give up
       one constraint at a time rather than failing hard. */
    result = AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    for (attempt = 0; attempt < 3 && result != AAUDIO_OK; ++attempt) {
        AAudioStreamBuilder_setSharingMode(builder, (attempt == 0) ? AAUDIO_SHARING_MODE_EXCLUSIVE
                                                                  : AAUDIO_SHARING_MODE_SHARED);
        /* 0 means "whatever the device prefers"; the callback adapts. */
        AAudioStreamBuilder_setChannelCount(builder, (attempt < 2) ? 1 : 0);

        result = AAudioStreamBuilder_openStream(builder, &g_stream);
        if (result != AAUDIO_OK) {
            LOGE("openStream attempt %d: %s", attempt, AAudio_convertResultToText(result));
            g_stream = 0;
        }
    }
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        return JNI_FALSE;
    }

    g_channels = AAudioStream_getChannelCount(g_stream);
    if (g_channels < 1) {
        g_channels = 1;
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

/* Every control call goes through the queue rather than touching the engine.
   These are invoked on whichever thread called the Java method, usually the UI
   one, while the audio callback is rendering; synth_schedule is the only entry
   point in the library safe to call from there. Frame 0 is always in the past,
   so such an event is applied at the top of the next block. */
static jboolean enqueue(synth_event_type_t type, uint64_t frame, int index, float value)
{
    synth_event_t event;

    event.frame = frame;
    event.type = type;
    event.index = index;
    event.value = value;
    return synth_schedule(&g_synth, &event) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_noteOn(JNIEnv *env, jclass clazz,
                                                               jint note, jfloat velocity)
{
    (void)env;
    (void)clazz;

    enqueue(SYNTH_EVENT_NOTE_ON, 0, (int)note, (float)velocity);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_noteOff(JNIEnv *env, jclass clazz, jint note)
{
    (void)env;
    (void)clazz;

    enqueue(SYNTH_EVENT_NOTE_OFF, 0, (int)note, 0.0f);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_allNotesOff(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    enqueue(SYNTH_EVENT_ALL_NOTES_OFF, 0, 0, 0.0f);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_setParam(JNIEnv *env, jclass clazz,
                                                                 jint param, jfloat norm)
{
    (void)env;
    (void)clazz;

    enqueue(SYNTH_EVENT_PARAM, 0, (int)param, (float)norm);
}

JNIEXPORT jlong JNICALL Java_com_kidoe_synth_SynthEngine_frameTime(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    return (jlong)synth_frame_time(&g_synth);
}

JNIEXPORT jint JNICALL Java_com_kidoe_synth_SynthEngine_framesPerBurst(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    return g_stream ? (jint)AAudioStream_getFramesPerBurst(g_stream) : 0;
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_scheduleNoteOn(
    JNIEnv *env, jclass clazz, jlong frame, jint note, jfloat velocity)
{
    (void)env;
    (void)clazz;

    return enqueue(SYNTH_EVENT_NOTE_ON, (uint64_t)frame, (int)note, (float)velocity);
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_scheduleNoteOff(
    JNIEnv *env, jclass clazz, jlong frame, jint note)
{
    (void)env;
    (void)clazz;

    return enqueue(SYNTH_EVENT_NOTE_OFF, (uint64_t)frame, (int)note, 0.0f);
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_scheduleParam(
    JNIEnv *env, jclass clazz, jlong frame, jint param, jfloat norm)
{
    (void)env;
    (void)clazz;

    return enqueue(SYNTH_EVENT_PARAM, (uint64_t)frame, (int)param, (float)norm);
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
