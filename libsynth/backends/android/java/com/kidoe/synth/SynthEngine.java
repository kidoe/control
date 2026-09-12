package com.kidoe.synth;

/**
 * Bridge to the native synthesis core, driving an AAudio stream.
 *
 * <p>One stream, {@link #trackCount()} engines. Each track is an independent
 * engine with its own patch, its own voice pool and its own event queue, and the
 * native side sums them into the stream. So the first argument of every control
 * call is a track index, and a parameter set on one track leaves the others
 * alone — which is the whole point, because the core propagates a parameter
 * change to the voices already sounding, and a single engine playing a kick and
 * a hi-hat would turn the decaying kick into the hi-hat.
 *
 * <p>Level per track is that track's own {@link #PARAM_MASTER_GAIN}. Headroom
 * across tracks is the app's: each engine is bounded by 1 on its own, so four in
 * unison reach four, and nothing in the library can pick that budget.
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
    public static final int PARAM_LFO_RATE = 25;
    public static final int PARAM_LFO_SHAPE = 26;
    public static final int PARAM_LFO_TO_CUTOFF = 27;
    public static final int PARAM_LFO_TO_PITCH = 28;
    public static final int PARAM_LFO_TO_AMP = 29;
    /**
     * Pitch envelope: an attack-decay sweep of the oscillator's frequency,
     * which is what makes a drum a drum. The amount is bipolar and in octaves,
     * so its centre (0.5f) is no sweep at all, positive falls onto the note and
     * negative rises onto it. A kick is a positive amount with a short decay.
     */
    public static final int PARAM_PITCH_ENV_AMOUNT = 30;
    public static final int PARAM_PITCH_ENV_ATTACK = 31;
    public static final int PARAM_PITCH_ENV_DECAY = 32;
    /**
     * Portamento: seconds for a note to travel from the pitch of the one
     * played before it. Zero, where it ships, is off. Each voice carries its
     * own travel, so a chord built one note at a time does not drag the notes
     * already in it.
     */
    public static final int PARAM_GLIDE = 33;

    /** Shape values for {@link #PARAM_LFO_SHAPE}, which is a stepped parameter. */
    public static final int LFO_SINE = 0;
    public static final int LFO_TRIANGLE = 1;
    public static final int LFO_SQUARE = 2;
    public static final int LFO_RANDOM = 3;

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
     * Tracks this build was compiled with, which is the valid range of every
     * track argument. Fixed at build time, like polyphony: the engines are
     * static storage, because the library never allocates. Change it with
     * -DSYNTH_TRACKS=n in the backend's CMakeLists.
     *
     * <p>A track index outside it is refused rather than obeyed: the scheduling
     * calls return false, the others do nothing.
     */
    public static native int trackCount();

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
     * Sounds a note on one track as soon as the engine next renders, within one
     * buffer.
     *
     * <p>This and the other control calls place an event on that track's
     * lock-free queue rather than touching the engine, so they are safe to call
     * from the UI thread while audio is rendering. The cost is that they take
     * effect on the next block rather than instantly, and that
     * {@link #getParam(int, int)} keeps returning the old value until then.
     */
    public static native void noteOn(int track, int note, float velocity);

    public static native void noteOff(int track, int note);

    public static native void allNotesOff(int track);

    /**
     * Voices sounding right now on one track, out of the fixed pool the core was
     * built with. A meter rather than a synchronisation point: it reads state the
     * audio thread owns, so the answer is a snapshot that may already be a block
     * old. That is enough to see voice stealing, which with a small pool happens
     * constantly and is otherwise invisible.
     */
    public static native int activeVoices(int track);

    /** norm is [0, 1] and maps onto the parameter's own range and curve. */
    public static native void setParam(int track, int param, float norm);

    /**
     * Frames rendered since the engine started. This is the clock a sequencer
     * schedules against: it advances only as audio is produced, so unlike a
     * wall clock it cannot drift from the stream.
     *
     * <p>One clock for every track. Each engine keeps its own, but the callback
     * renders all of them every time and by the same frame count, so they stay
     * equal and this answers for all.
     */
    public static native long frameTime();

    /** Frames the device hands over per callback; the unit to size lookahead in. */
    public static native int framesPerBurst();

    /**
     * Places a note at an exact frame on the {@link #frameTime()} clock, so a
     * step lands on its own frame instead of on a buffer boundary. Returns
     * false when the queue is full, which means either scheduling less far
     * ahead or building the library with a larger SYNTH_EVENT_QUEUE_LEN. Each
     * track has its own queue, so one busy track cannot crowd out another.
     *
     * <p>A frame already in the past is played at the start of the next block
     * rather than dropped: a late step recovers, a silent one does not.
     */
    public static native boolean scheduleNoteOn(long frame, int track, int note, float velocity);

    public static native boolean scheduleNoteOff(long frame, int track, int note);

    public static native boolean scheduleParam(long frame, int track, int param, float norm);

    public static native float getParam(int track, int param);

    /**
     * Sends a whole patch to one track, to be adopted at the top of the next
     * audio block. Returns false when the track's two-slot queue is still full,
     * which takes three calls on the same track inside one block and is a signal
     * to slow down rather than an error to ignore.
     *
     * <p>This is how a kit change should happen. The alternative — a
     * {@link #setParam(int, int, float)} per parameter — puts
     * {@link #paramCount()} events on that track's queue, and four tracks
     * changing kit at once is 136 events against a queue of 64: the ones past
     * the end are refused, so what the app gets is half a kit and no way to tell
     * which half. A patch is one slot however many parameters it carries, and it
     * arrives whole or not at all. It is also less work, though less
     * dramatically than the queue argument suggests: on a 96-frame block with
     * eight voices sounding, 240,760 instructions against 306,903 for the same
     * change as 34 events.
     *
     * <p>The array is positional, exactly as {@link #savePatch(int)} returns it.
     * A shorter one — a kit saved by a build with fewer parameters — is accepted
     * and the rest take their defaults, which is the value that kit was
     * implicitly using; an empty array therefore resets the track. Do not
     * zero-fill the tail yourself: every bipolar control is neutral at its
     * centre, so a zero would load full negative and a saved sound would come
     * back four octaves down.
     *
     * <p>It lands on a block boundary rather than an exact frame, unlike
     * {@link #scheduleParam(long, int, int, float)}. At 96 frames that is 2 ms.
     * It also reaches the notes already sounding on that track, so a kit change
     * under a decaying note re-voices it: change kits between steps.
     */
    public static native boolean loadPatch(int track, float[] patch);

    /**
     * One track's sound as a plain float array, or null for a track that does
     * not exist. A snapshot read off the audio thread's parameters, which is
     * what the engine is playing now — not a patch sent with
     * {@link #loadPatch(int, float[])} and not yet adopted.
     *
     * <p>Positional and tied to this build's parameter list. To survive a
     * version change, store {@link #paramName(int)} beside each value and match
     * on the names when loading.
     */
    public static native float[] savePatch(int track);

    /** Number of parameters the core exposes, for building UI generically. */
    public static native int paramCount();

    /** Parameter name, or null if names were compiled out of the core. */
    public static native String paramName(int param);

    /*
     * The single-engine calls this bridge had before it held several, each one
     * exactly its track 0 form. An app written against the old signatures keeps
     * working, on track 0, and nothing here needs the app to know about tracks.
     * There is no exception to that rule: allNotesOff() silences track 0 and
     * leaves the rest sounding, so a panic button wants the loop over
     * trackCount() rather than this.
     */

    /** @deprecated use {@link #noteOn(int, int, float)}; this is track 0. */
    @Deprecated
    public static void noteOn(int note, float velocity) {
        noteOn(0, note, velocity);
    }

    /** @deprecated use {@link #noteOff(int, int)}; this is track 0. */
    @Deprecated
    public static void noteOff(int note) {
        noteOff(0, note);
    }

    /** @deprecated use {@link #allNotesOff(int)}; this is track 0 alone. */
    @Deprecated
    public static void allNotesOff() {
        allNotesOff(0);
    }

    /** @deprecated use {@link #activeVoices(int)}; this is track 0. */
    @Deprecated
    public static int activeVoices() {
        return activeVoices(0);
    }

    /** @deprecated use {@link #setParam(int, int, float)}; this is track 0. */
    @Deprecated
    public static void setParam(int param, float norm) {
        setParam(0, param, norm);
    }

    /** @deprecated use {@link #getParam(int, int)}; this is track 0. */
    @Deprecated
    public static float getParam(int param) {
        return getParam(0, param);
    }

    /** @deprecated use {@link #scheduleNoteOn(long, int, int, float)}; track 0. */
    @Deprecated
    public static boolean scheduleNoteOn(long frame, int note, float velocity) {
        return scheduleNoteOn(frame, 0, note, velocity);
    }

    /** @deprecated use {@link #scheduleNoteOff(long, int, int)}; track 0. */
    @Deprecated
    public static boolean scheduleNoteOff(long frame, int note) {
        return scheduleNoteOff(frame, 0, note);
    }

    /** @deprecated use {@link #scheduleParam(long, int, int, float)}; track 0. */
    @Deprecated
    public static boolean scheduleParam(long frame, int param, float norm) {
        return scheduleParam(frame, 0, param, norm);
    }
}
