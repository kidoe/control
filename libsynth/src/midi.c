#include "synth/midi.h"

#define MIDI_NOTE_OFF 0x80
#define MIDI_NOTE_ON 0x90
#define MIDI_CONTROL_CHANGE 0xB0
#define MIDI_PROGRAM_CHANGE 0xC0
#define MIDI_CHANNEL_PRESSURE 0xD0
#define MIDI_PITCH_BEND 0xE0

#define MIDI_SYSEX_START 0xF0
#define MIDI_SYSEX_END 0xF7
#define MIDI_REALTIME_FIRST 0xF8

#define MIDI_CC_ALL_SOUND_OFF 120
#define MIDI_CC_ALL_NOTES_OFF 123

void synth_midi_parser_init(synth_midi_parser_t *parser)
{
    parser->status = 0;
    parser->data[0] = 0;
    parser->data[1] = 0;
    parser->pending = 0;
    parser->in_sysex = 0;
}

static int message_length(unsigned char status)
{
    unsigned char type = (unsigned char)(status & 0xF0u);

    return (type == MIDI_PROGRAM_CHANGE || type == MIDI_CHANNEL_PRESSURE) ? 1 : 2;
}

static int decode(const synth_midi_parser_t *parser, synth_midi_msg_t *out)
{
    unsigned char type = (unsigned char)(parser->status & 0xF0u);

    out->type = SYNTH_MIDI_NONE;
    out->channel = parser->status & 0x0F;
    out->data1 = parser->data[0];
    out->data2 = parser->data[1];
    out->bend = 0;

    switch (type) {
    case MIDI_NOTE_ON:
        /* A note on at zero velocity is a note off; keyboards send it that way
           so that running status can carry a whole phrase. */
        out->type = (parser->data[1] == 0) ? SYNTH_MIDI_NOTE_OFF : SYNTH_MIDI_NOTE_ON;
        return 1;
    case MIDI_NOTE_OFF:
        out->type = SYNTH_MIDI_NOTE_OFF;
        return 1;
    case MIDI_CONTROL_CHANGE:
        out->type = SYNTH_MIDI_CONTROL_CHANGE;
        return 1;
    case MIDI_PITCH_BEND:
        out->type = SYNTH_MIDI_PITCH_BEND;
        out->bend = (parser->data[0] | (parser->data[1] << 7)) - 8192;
        return 1;
    default:
        return 0; /* aftertouch and program change carry nothing the engine uses */
    }
}

int synth_midi_parse(synth_midi_parser_t *parser, unsigned char byte, synth_midi_msg_t *out)
{
    /* Realtime bytes are allowed to appear between the bytes of another
       message, so they must leave every piece of parser state alone. */
    if (byte >= MIDI_REALTIME_FIRST) {
        return 0;
    }

    if (parser->in_sysex) {
        if (byte == MIDI_SYSEX_END) {
            parser->in_sysex = 0;
        }
        return 0;
    }

    if (byte >= 0x80) {
        if (byte == MIDI_SYSEX_START) {
            parser->in_sysex = 1;
            parser->status = 0;
        } else if (byte > MIDI_SYSEX_START) {
            parser->status = 0; /* system common cancels running status */
        } else {
            parser->status = byte;
        }
        parser->pending = 0;
        return 0;
    }

    if (parser->status == 0) {
        return 0; /* data with no status to belong to */
    }

    parser->data[parser->pending] = byte;
    ++parser->pending;
    if (parser->pending < message_length(parser->status)) {
        return 0;
    }
    parser->pending = 0; /* status stays, so the next bytes run on without it */

    return decode(parser, out);
}

void synth_midi_init(synth_midi_t *midi)
{
    int i;

    synth_midi_parser_init(&midi->parser);
    midi->bend_semitones = 2.0f;

    for (i = 0; i < SYNTH_MIDI_CC_COUNT; ++i) {
        midi->cc_map[i] = SYNTH_MIDI_CC_UNMAPPED;
    }
    for (i = 0; i < SYNTH_PARAM_COUNT && (20 + i) < SYNTH_MIDI_CC_COUNT; ++i) {
        midi->cc_map[20 + i] = (signed char)i;
    }
}

void synth_midi_map_cc(synth_midi_t *midi, int cc, synth_param_t param)
{
    if (cc < 0 || cc >= SYNTH_MIDI_CC_COUNT) {
        return;
    }
    if ((int)param < 0 || (int)param >= SYNTH_PARAM_COUNT) {
        return;
    }
    midi->cc_map[cc] = (signed char)param;
}

void synth_midi_unmap_cc(synth_midi_t *midi, int cc)
{
    if (cc >= 0 && cc < SYNTH_MIDI_CC_COUNT) {
        midi->cc_map[cc] = SYNTH_MIDI_CC_UNMAPPED;
    }
}

void synth_midi_clear_map(synth_midi_t *midi)
{
    int i;

    for (i = 0; i < SYNTH_MIDI_CC_COUNT; ++i) {
        midi->cc_map[i] = SYNTH_MIDI_CC_UNMAPPED;
    }
}

void synth_midi_apply(synth_midi_t *midi, synth_t *s, const synth_midi_msg_t *msg)
{
    switch (msg->type) {
    case SYNTH_MIDI_NOTE_ON:
        synth_note_on(s, msg->data1, (float)msg->data2 * (1.0f / 127.0f));
        break;

    case SYNTH_MIDI_NOTE_OFF:
        synth_note_off(s, msg->data1);
        break;

    case SYNTH_MIDI_CONTROL_CHANGE:
        /* The two channel-mode messages every host expects to work whatever
           the controller map says. */
        if (msg->data1 == MIDI_CC_ALL_NOTES_OFF || msg->data1 == MIDI_CC_ALL_SOUND_OFF) {
            synth_all_notes_off(s);
        } else if (midi->cc_map[msg->data1] != SYNTH_MIDI_CC_UNMAPPED) {
            synth_set_param(s, (synth_param_t)midi->cc_map[msg->data1],
                            (float)msg->data2 * (1.0f / 127.0f));
        }
        break;

    case SYNTH_MIDI_PITCH_BEND:
        synth_set_pitch_bend(s, (float)msg->bend * (1.0f / 8192.0f) * midi->bend_semitones);
        break;

    case SYNTH_MIDI_NONE:
    default:
        break;
    }
}

int synth_midi_feed(synth_midi_t *midi, synth_t *s, unsigned char byte)
{
    synth_midi_msg_t msg;

    if (!synth_midi_parse(&midi->parser, byte, &msg)) {
        return 0;
    }
    synth_midi_apply(midi, s, &msg);
    return 1;
}
