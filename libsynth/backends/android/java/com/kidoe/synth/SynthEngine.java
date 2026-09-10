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

    /** Indices mirror synth_param_t in include/synth/synth.h. */
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

    private SynthEngine() {
    }

    /** Opens and starts the audio stream. Returns false if no stream could be opened. */
    public static native boolean start();

    /** Stops and closes the stream. Safe to call when already stopped. */
    public static native void stop();

    /** Rate the device actually granted, or 0 when stopped. */
    public static native int sampleRate();

    public static native void noteOn(int note, float velocity);

    public static native void noteOff(int note);

    public static native void allNotesOff();

    /** norm is [0, 1] and maps onto the parameter's own range and curve. */
    public static native void setParam(int param, float norm);

    public static native float getParam(int param);

    /** Number of parameters the core exposes, for building UI generically. */
    public static native int paramCount();

    /** Parameter name, or null if names were compiled out of the core. */
    public static native String paramName(int param);
}
