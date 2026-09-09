#ifndef SYNTH_CONFIG_H
#define SYNTH_CONFIG_H

/* Compile-time budget. Override per target with -DSYNTH_MAX_VOICES=n.
   Fixed at compile time so an MCU build can size its RAM statically. */
#ifndef SYNTH_MAX_VOICES
#define SYNTH_MAX_VOICES 8
#endif

/* Set to 0 to drop parameter name strings from the binary (saves flash). */
#ifndef SYNTH_PARAM_NAMES
#define SYNTH_PARAM_NAMES 1
#endif

#endif /* SYNTH_CONFIG_H */
