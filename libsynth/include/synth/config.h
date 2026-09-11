#ifndef SYNTH_CONFIG_H
#define SYNTH_CONFIG_H

/* Compile-time budget. Override per target with -DSYNTH_MAX_VOICES=n.
   Fixed at compile time so an MCU build can size its RAM statically. */
#ifndef SYNTH_MAX_VOICES
#define SYNTH_MAX_VOICES 8
#endif

/* How often, in samples, a modulated filter is retuned. Recomputing the
   coefficients costs far more than a sample of audio, so it runs at control
   rate. Measured against retuning every sample, on an 8-voice sweep: 4 samples
   costs 45% of the per-sample CPU for -46 dB of error, 16 samples costs 31%
   for -32 dB. Raise it on a target where the filter maths hurts. */
#ifndef SYNTH_MOD_INTERVAL
#define SYNTH_MOD_INTERVAL 4
#endif

/* Events a host can have in flight at once. Must be a power of two. Sizing is a
   lookahead budget: a sequencer scheduling one block ahead needs a handful, one
   scheduling a whole bar ahead needs steps * tracks. Costs 24 bytes each. */
#ifndef SYNTH_EVENT_QUEUE_LEN
#define SYNTH_EVENT_QUEUE_LEN 64
#endif

/* Set to 0 to drop parameter name strings from the binary (saves flash). */
#ifndef SYNTH_PARAM_NAMES
#define SYNTH_PARAM_NAMES 1
#endif

#endif /* SYNTH_CONFIG_H */
