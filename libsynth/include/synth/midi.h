#ifndef SYNTH_MIDI_H
#define SYNTH_MIDI_H

#include "synth/synth.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MIDI input, kept deliberately outside the core: this file depends on
 * synth.h and nothing in the core depends on it, so a target that does not
 * want MIDI never links it.
 *
 * The parser takes one byte at a time and holds no assumptions about where
 * the bytes came from. The same stream arrives from USB on a desktop, a UART
 * on a microcontroller and MidiManager on Android, which is the whole reason
 * to parse bytes rather than accept decoded events.
 */

typedef enum {
    SYNTH_MIDI_NONE = 0,
    SYNTH_MIDI_NOTE_ON,
    SYNTH_MIDI_NOTE_OFF,
    SYNTH_MIDI_CONTROL_CHANGE,
    SYNTH_MIDI_PITCH_BEND
} synth_midi_type_t;

typedef struct {
    synth_midi_type_t type;
    int channel;  /* 0-15 */
    int data1;    /* note number, or controller number */
    int data2;    /* velocity, or controller value */
    int bend;     /* pitch bend, -8192 to 8191, centred on 0 */
} synth_midi_msg_t;

typedef struct {
    unsigned char status; /* running status, 0 when none is in force */
    unsigned char data[2];
    int pending;
    int in_sysex;
} synth_midi_parser_t;

void synth_midi_parser_init(synth_midi_parser_t *parser);

/* Feeds one byte. Returns 1 and fills `out` when a message completes. */
int synth_midi_parse(synth_midi_parser_t *parser, unsigned char byte, synth_midi_msg_t *out);

#define SYNTH_MIDI_CC_COUNT 128
#define SYNTH_MIDI_CC_UNMAPPED (-1)

typedef struct {
    synth_midi_parser_t parser;
    signed char cc_map[SYNTH_MIDI_CC_COUNT];
    float bend_semitones; /* range of a full bend, 2 by convention */
} synth_midi_t;

/* Starts with controllers 20 upwards mapped onto the parameters in order, so a
   controller surface does something useful before anything is configured. */
void synth_midi_init(synth_midi_t *midi);

void synth_midi_map_cc(synth_midi_t *midi, int cc, synth_param_t param);
void synth_midi_unmap_cc(synth_midi_t *midi, int cc);
void synth_midi_clear_map(synth_midi_t *midi);

/* Parses the byte and applies whatever it completes to the synth. Returns 1
   when a message was acted on. */
int synth_midi_feed(synth_midi_t *midi, synth_t *s, unsigned char byte);

void synth_midi_apply(synth_midi_t *midi, synth_t *s, const synth_midi_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_MIDI_H */
