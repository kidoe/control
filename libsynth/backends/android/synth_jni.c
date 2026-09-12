#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>

#include "synth/synth.h"

/* Android backend: an AAudio stream whose data callback feeds the core. As on
   every other target, nothing below this file knows what platform it is on.

   One stream, several engines. A groovebox wants a patch and a voice pool per
   track, and the core has no channel argument for that: it has instances. So
   this file holds SYNTH_TRACKS of them, gives every control call a track index,
   and sums their outputs into the stream's buffer. The engines are independent
   by construction — the only global in the library is a const table — so the
   only thing shared here is the buffer they mix into. */

#ifndef SYNTH_TRACKS
#define SYNTH_TRACKS 4
#endif

#define LOG_TAG "libsynth"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* The mix starts by writing with track 0, so there has to be one. Checked here
   rather than left to whatever a zero-length array does. */
typedef char synth_tracks_must_be_at_least_one[(SYNTH_TRACKS >= 1) ? 1 : -1];

static synth_t g_tracks[SYNTH_TRACKS];
static AAudioStream *g_stream;
static int g_channels = 1;

/* A track index arrives from app code, so it is checked rather than trusted:
   out of range answers "no such track" instead of reading past the array. */
static synth_t *track_at(jint track)
{
    return (track >= 0 && track < SYNTH_TRACKS) ? &g_tracks[track] : 0;
}

static aaudio_data_callback_result_t audio_callback(AAudioStream *stream, void *user_data,
                                                    void *audio_data, int32_t num_frames)
{
    float *out = (float *)audio_data;
    int channels = g_channels;
    int frame;
    int channel;
    int track;

    (void)stream;
    (void)user_data;

    /* The first track writes and the rest add, so the mix needs no scratch
       buffer and no clearing of this one. Every track renders, silent ones
       included: each engine's clock advances only while it is rendering, so a
       track skipped here would fall behind the timeline the sequencer is
       scheduling all of them against. */
    synth_render(&g_tracks[0], out, (int)num_frames);
    for (track = 1; track < SYNTH_TRACKS; ++track) {
        synth_render_add(&g_tracks[track], out, (int)num_frames);
    }

    /* The engines are mono and the device decides how many channels it grants.
       Expanding from the back lets the same buffer hold both, with no scratch
       memory and no allocation on the audio thread. It has to come after the
       mix, not per track: a track added to an already-expanded buffer would
       land on the left channel alone. */
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
    int track;

    (void)vm;
    (void)reserved;

    /* Valid engines before any stream exists, so notes and parameters set
       ahead of start() are never applied to uninitialised memory. */
    for (track = 0; track < SYNTH_TRACKS; ++track) {
        synth_init(&g_tracks[track], 48000.0f);
    }
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_start(JNIEnv *env, jclass clazz)
{
    AAudioStreamBuilder *builder = 0;
    aaudio_result_t result;
    int32_t burst;
    int attempt;
    int track;

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
       This keeps parameters the app already set. Every track, and before
       requestStart(), because the callback reads all of them. */
    for (track = 0; track < SYNTH_TRACKS; ++track) {
        synth_set_sample_rate(&g_tracks[track], (float)AAudioStream_getSampleRate(g_stream));
    }

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
static jboolean enqueue(jint track, synth_event_type_t type, uint64_t frame,
                        int index, float value)
{
    synth_t *s = track_at(track);
    synth_event_t event;

    if (!s) {
        return JNI_FALSE;
    }
    event.frame = frame;
    event.type = type;
    event.index = index;
    event.value = value;
    return synth_schedule(s, &event) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_com_kidoe_synth_SynthEngine_trackCount(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    return (jint)SYNTH_TRACKS;
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_noteOn(JNIEnv *env, jclass clazz,
                                                               jint track, jint note,
                                                               jfloat velocity)
{
    (void)env;
    (void)clazz;

    enqueue(track, SYNTH_EVENT_NOTE_ON, 0, (int)note, (float)velocity);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_noteOff(JNIEnv *env, jclass clazz,
                                                                jint track, jint note)
{
    (void)env;
    (void)clazz;

    enqueue(track, SYNTH_EVENT_NOTE_OFF, 0, (int)note, 0.0f);
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_allNotesOff(JNIEnv *env, jclass clazz,
                                                                    jint track)
{
    (void)env;
    (void)clazz;

    enqueue(track, SYNTH_EVENT_ALL_NOTES_OFF, 0, 0, 0.0f);
}

JNIEXPORT jint JNICALL Java_com_kidoe_synth_SynthEngine_activeVoices(JNIEnv *env, jclass clazz,
                                                                     jint track)
{
    const synth_t *s = track_at(track);

    (void)env;
    (void)clazz;

    /* Reads voice state the audio thread owns. Nothing here can tear a value
       that matters: the worst case is a count taken across a block boundary,
       which is what a meter is anyway. */
    return s ? (jint)synth_active_voices(s) : 0;
}

JNIEXPORT void JNICALL Java_com_kidoe_synth_SynthEngine_setParam(JNIEnv *env, jclass clazz,
                                                                 jint track, jint param,
                                                                 jfloat norm)
{
    (void)env;
    (void)clazz;

    enqueue(track, SYNTH_EVENT_PARAM, 0, (int)param, (float)norm);
}

JNIEXPORT jlong JNICALL Java_com_kidoe_synth_SynthEngine_frameTime(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    /* One clock for every track, because the callback renders all of them every
       time and each advances by the same frame count. Track 0 answers for all;
       if one of them ever stopped being rendered, this would stop being true
       and every step scheduled after it would be aimed at the wrong frame. */
    return (jlong)synth_frame_time(&g_tracks[0]);
}

JNIEXPORT jint JNICALL Java_com_kidoe_synth_SynthEngine_framesPerBurst(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;

    return g_stream ? (jint)AAudioStream_getFramesPerBurst(g_stream) : 0;
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_scheduleNoteOn(
    JNIEnv *env, jclass clazz, jlong frame, jint track, jint note, jfloat velocity)
{
    (void)env;
    (void)clazz;

    return enqueue(track, SYNTH_EVENT_NOTE_ON, (uint64_t)frame, (int)note, (float)velocity);
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_scheduleNoteOff(
    JNIEnv *env, jclass clazz, jlong frame, jint track, jint note)
{
    (void)env;
    (void)clazz;

    return enqueue(track, SYNTH_EVENT_NOTE_OFF, (uint64_t)frame, (int)note, 0.0f);
}

JNIEXPORT jboolean JNICALL Java_com_kidoe_synth_SynthEngine_scheduleParam(
    JNIEnv *env, jclass clazz, jlong frame, jint track, jint param, jfloat norm)
{
    (void)env;
    (void)clazz;

    return enqueue(track, SYNTH_EVENT_PARAM, (uint64_t)frame, (int)param, (float)norm);
}

JNIEXPORT jfloat JNICALL Java_com_kidoe_synth_SynthEngine_getParam(JNIEnv *env, jclass clazz,
                                                                   jint track, jint param)
{
    const synth_t *s = track_at(track);

    (void)env;
    (void)clazz;

    return s ? (jfloat)synth_get_param(s, (synth_param_t)param) : 0.0f;
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
