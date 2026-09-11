package com.kidoe.synth;

/**
 * Bridge to the native synthesis core, driving an AAudio stream.
 *
 * <p>Control calls are not synchronised: issue them from a single thread. The
 * audio callback runs on a real-time thread owned by AAudio, so never block it
 * and never allocate on it.
 */
public final class SynthEngine {

    static {
        System.loadLibrary("synthjni");
    }

    /** Indices mirror synth_param_t in include/synth/synth.h, in order. */
    public static final int PARAM_MASTER_GAIN = 0;
    public static final int PARAM_OSC_WAVE = 1;
    public static final int PARAM_AMP_DELAY = 2;
    public static final int PARAM_AMP_ATTACK = 3;
    public static final int PARAM_AMP_HOLD = 4;
    public static final int PARAM_AMP_DECAY = 5;
    public static final int PARAM_AMP_SUSTAIN = 6;
    public static final int PARAM_AMP_RELEASE = 7;
    public static final int PARAM_FILTER_MODE = 8;
    public static final int PARAM_FILTER_CUTOFF = 9;
    public static final int PARAM_FILTER_Q = 10;
    public static final int PARAM_PD_KNEE = 11;
    public static final int PARAM_VOSIM_FORMANT = 12;
    public static final int PARAM_VOSIM_PULSES = 13;
    public static final int PARAM_VOSIM_DECAY = 14;
    public static final int PARAM_TERRAIN_RADIUS = 15;
    public static final int PARAM_TERRAIN_RATIO = 16;
    public static final int PARAM_FILTER_ENV_AMOUNT = 17;
    public static final int PARAM_FILTER_ENV_ATTACK = 18;
    public static final int PARAM_FILTER_ENV_DECAY = 19;
    public static final int PARAM_FILTER_ENV_SUSTAIN = 20;
    public static final int PARAM_FILTER_ENV_RELEASE = 21;
    public static final int PARAM_FILTER_KEY_TRACK = 22;
    public static final int PARAM_AMP_DRIVE = 23;
    public static final int PARAM_AMP_VELOCITY = 24;

    /** Waveform values for {@link #PARAM_OSC_WAVE}, which is a stepped parameter. */
    public static final int WAVE_SINE = 0;
    public static final int WAVE_SAW = 1;
    public static final int WAVE_SQUARE = 2;
    public static final int WAVE_PD = 3;
    public static final int WAVE_VOSIM = 4;
    public static final int WAVE_TERRAIN = 5;
    public static final int WAVE_NOISE = 6;
    public static final int WAVE_COUNT = 7;

    private SynthEngine() {
    }

    /**
     * Opens and starts the audio stream, preferring an exclusive mono
     * low-latency stream and giving up one constraint at a time if the device
     * refuses, which emulators do. Returns false only if nothing opened.
     *
     * <p>Recovery after the device disappears is the caller's job: AAudio
     * forbids closing a stream from inside its own error callback, so when
     * headphones are unplugged the stream goes silent and stays that way until
     * the app calls {@link #stop()} and then {@link #start()} again. Doing that
     * from onPause/onResume covers most of it.
     */
    public static native boolean start();

    /** Stops and closes the stream. Safe to call when already stopped. */
    public static native void stop();

    /** Rate the device actually granted, or 0 when stopped. */
    public static native int sampleRate();

    /**
     * Sounds a note as soon as the engine next renders, within one buffer.
     *
     * <p>This and the other control calls place an event on a lock-free queue
     * rather than touching the engine, so they are safe to call from the UI
     * thread while audio is rendering. The cost is that they take effect on the
     * next block rather than instantly, and that {@link #getParam(int)} keeps
     * returning the old value until then.
     */
    public static native void noteOn(int note, float velocity);

    public static native void noteOff(int note);

    public static native void allNotesOff();

    /** norm is [0, 1] and maps onto the parameter's own range and curve. */
    public static native void setParam(int param, float norm);

    /**
     * Frames rendered since the engine started. This is the clock a sequencer
     * schedules against: it advances only as audio is produced, so unlike a
     * wall clock it cannot drift from the stream.
     */
    public static native long frameTime();

    /** Frames the device hands over per callback; the unit to size lookahead in. */
    public static native int framesPerBurst();

    /**
     * Places a note at an exact frame on the {@link #frameTime()} clock, so a
     * step lands on its own frame instead of on a buffer boundary. Returns
     * false when the queue is full, which means either scheduling less far
     * ahead or building the library with a larger SYNTH_EVENT_QUEUE_LEN.
     *
     * <p>A frame already in the past is played at the start of the next block
     * rather than dropped: a late step recovers, a silent one does not.
     */
    public static native boolean scheduleNoteOn(long frame, int note, float velocity);

    public static native boolean scheduleNoteOff(long frame, int note);

    public static native boolean scheduleParam(long frame, int param, float norm);

    public static native float getParam(int param);

    /** Number of parameters the core exposes, for building UI generically. */
    public static native int paramCount();

    /** Parameter name, or null if names were compiled out of the core. */
    public static native String paramName(int param);
}
