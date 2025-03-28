#include <stdint.h>

#define SAMPLE_RATE 11025

#define SYNTH_NODES 8
#define SYNTH_VOICES 2

typedef int16_t q15_t;
#define Q15_MAX 0x7FFF
#define Q15_MIN 0x8000

typedef int8_t q7_t;
#define Q7_MAX 0x7F
#define Q7_MIN 0x80

/* Use generic pointer-connected nodes that can be mostly configured by the
 * engine. When a note is triggered, the state for oscillators, Low-Frequency
 * Oscillator (LFO), and envelopes is reset. For instance, envelopes might
 * provide two outputs (one being the inverse), or operate in a special mode.
 * Oscillators accept both pitch and gain inputs.
 */

/* Oscillators generate waveforms using a phase accumulator.
 * This approach is efficient because it only requires an addition per sample.
 * The phase value remains positive and wraps around at Q15_MAX (15-bit range).
 * 'phase_incr2' (for FM) is added to the phase increment to adjust the base
 * frequency.
 */
typedef struct {
    q15_t *phase_incr;             /* Derived from the oscillator frequency */
    q15_t *detune;                 /* Offset for frequency modulation (FM) */
    q15_t (*wavegen)(q15_t input); /* waveform generator */
} synth_oscillator_t;

/* Simple envelope generator structure */
typedef struct {
    q15_t attack;
    q15_t decay;
    q15_t sustain;
    q15_t release;
} synth_envelope_t;

/* basic filter */
typedef struct {
    q15_t *input;
    int32_t accum;
    int32_t factor;
} synth_filter_t;

// basic mixer
typedef struct {
    q15_t *inputs[3];
} synth_mixer_t;

typedef enum {
    SYNTH_NODE_NONE = 0,
    SYNTH_NODE_OSCILLATOR,
    SYNTH_NODE_ENVELOPE,
    SYNTH_NODE_FILTER_LP,
    SYNTH_NODE_FILTER_HP,
    SYNTH_NODE_MIXER,
    SYNTH_NODE_END
} synth_node_type_t;

typedef struct {
    /* Holds node-specific state (e.g., phase or envelope state).
                       This is reset when a note is triggered (gate event). */
    int32_t state;

    q15_t *gain;  /* Pointer to the gain input value. */
    q15_t output; /* Output value of the node. */
    synth_node_type_t type;
    uint8_t param1; /* Optional: potentially used for controlling gain range. */
    union {
        synth_oscillator_t osc;
        synth_envelope_t env;
        synth_filter_t filter;
        synth_mixer_t mixer;
    };
} synth_node_t;

typedef struct {
    uint8_t note;     /* MIDI note value */
    uint8_t gate : 1; /* Gate flag: 1 for on, 0 for off */
    q15_t phase_incr; /* Phase increment calculated from frequency */
    synth_node_t nodes[SYNTH_NODES];
} synth_voice_t;

/* convert time in milliseconds to number of samples */
#define SYNTH_MS(ms) ((ms * SAMPLE_RATE) / 1000)

#include <string.h>

synth_voice_t synth_voices[SYNTH_VOICES];
synth_node_t synthNodes[SYNTH_VOICES][SYNTH_NODES];

/* Pre-calculate phase increments for the highest octave (starting at C8) where
 * the phase increments are highest. For each subsequent octave below, these
 * values will be halved.
 */

#define BASE_OCTAVE 8
#define SYNTH_HZ_TO_PHASE(frequency) ((frequency * Q15_MAX) / SAMPLE_RATE)
static const q15_t octave_phases[12] = {
    // start at C8
    SYNTH_HZ_TO_PHASE(4186.01),  // C
    SYNTH_HZ_TO_PHASE(4434.92),  // C#
    SYNTH_HZ_TO_PHASE(4698.63),  // D
    SYNTH_HZ_TO_PHASE(4978.03),  // D#
    SYNTH_HZ_TO_PHASE(5274.04),  // E
    SYNTH_HZ_TO_PHASE(5587.65),  // F
    SYNTH_HZ_TO_PHASE(5919.91),  // F#
    SYNTH_HZ_TO_PHASE(6271.93),  // G
    SYNTH_HZ_TO_PHASE(6644.88),  // G#
    SYNTH_HZ_TO_PHASE(7040.00),  // A
    SYNTH_HZ_TO_PHASE(7458.62),  // A#
    SYNTH_HZ_TO_PHASE(7902.13),  // B
};

static q15_t midi_to_phase_incr(uint8_t note)
{
    int octave = note / 12;
    int note_index = note - (octave * 12);
    q15_t phaseIncr = octave_phases[note_index];

    phaseIncr >>= (BASE_OCTAVE - octave + 1);
    return phaseIncr;
}

static void synth_voice_note_on(synth_voice_t *voice, uint8_t note)
{
    voice->note = note;
    voice->gate = 1;
    voice->phase_incr = midi_to_phase_incr(note);
    /* reset the state of all nodes */
    for (int i = 0; i < SYNTH_NODES; i++) {
        synth_node_t *node = &voice->nodes[i];
        node->state = 0;
    }
}

static void synth_voice_note_off(synth_voice_t *voice)
{
    voice->gate = 0;
}

void synth_init_osc_node(synth_node_t *node,
                         q15_t *gain,
                         q15_t *phase_incr,
                         q15_t *detune,
                         q15_t (*wavegen)(q15_t input))
{
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_OSCILLATOR;
    node->osc.phase_incr = phase_incr;
    node->osc.detune = detune;
    node->osc.wavegen = wavegen;
}

void synth_init_envelope_node(synth_node_t *node,
                              q15_t *gain,
                              q15_t attack,
                              q15_t decay,
                              q15_t sustain,
                              q15_t release)
{
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_ENVELOPE;
    node->env.attack = attack;
    node->env.decay = decay;
    node->env.sustain = sustain;
    node->env.release = release;
}

void synth_init_filter_lp_node(synth_node_t *node,
                               q15_t *gain,
                               q15_t *input,
                               q15_t factor)
{
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_FILTER_LP;
    node->filter.input = input;
    node->filter.factor = factor;
}

/* synth_process - Process one audio frame for all active synth voices.
 *
 * This function computes the audio output for the current sample by iterating
 * through each voice and processing its constituent nodes. It operates in two
 * passes:
 * 1. Calculation Pass: Each node's output is computed based on its current
 *    state. The outputs are stored temporarily in an array.
 *
 * 2. Update Pass: Each node's state is updated based on its type (oscillator,
 *    envelope, filter, mixer, etc.) using the outputs calculated in the first
 *    pass.
 *
 * For oscillators, the phase is advanced using the base phase increment and any
 * detuning. Envelope nodes are processed based on whether the gate is active
 * (attack/decay) or inactive (release), with mode switching handled via the
 * high bit in the state. Filter nodes update their accumulator based on the
 * difference between the input and filtered output. Mixer nodes sum the inputs
 * from their designated sources.
 *
 * The primary output for each voice (assumed to be at index 0 in the node
 * array) is accumulated and then normalized by applying a mixer gain based on
 * the number of voices.
 */
q15_t synth_process()
{
    int32_t main_output = 0;
    for (int vi = 0; vi < SYNTH_VOICES; vi++) {
        synth_voice_t *voice = &synth_voices[vi];
        synth_node_t *nodes = voice->nodes;
        /* First iteration: compute each node's output based on its current
         * state. Temporarily store these calculated outputs before updating the
         * node state.
         */
        int32_t outputs[SYNTH_NODES];
        for (int i = 0; i < SYNTH_NODES && nodes[i].type != SYNTH_NODE_NONE;
             i++) {
            synth_node_t *node = &nodes[i];
            q15_t tmp;
            switch (node->type) {
            case SYNTH_NODE_OSCILLATOR:
                tmp = node->state & 0x7FFF; /* Limit to positive q15 range */
                outputs[i] = node->osc.wavegen(tmp);
                break;
            case SYNTH_NODE_ENVELOPE:
                outputs[i] = (node->state & 0x7FFFFF) >> 4;
                /* Apply squaring to the envelope */
                outputs[i] = (outputs[i] * outputs[i]) >> 15;
                /* If the sustain value is negative, invert the envelope output
                 */
                if (node->env.sustain < 0)
                    outputs[i] = -outputs[i];
                break;
            case SYNTH_NODE_FILTER_LP:
                /* Compute low-pass filter output by scaling the accumulator
                 * with a filter factor. The accumulator holds the previous
                 * state, and this equation applies the filter's smoothing
                 * effect.
                 */
                outputs[i] = (node->filter.accum * node->filter.factor) >> 15;
                break;
            case SYNTH_NODE_FILTER_HP:
                /* Compute as with low-pass filter then subtract from the input
                 * signal.
                 */
                outputs[i] = (node->filter.accum * node->filter.factor) >> 15;
                outputs[i] = *node->filter.input - outputs[i];
                break;
            case SYNTH_NODE_MIXER: {
                int32_t sum = 0;
                for (int j = 0; j < 3; j++) {
                    if (node->mixer.inputs[j])
                        sum += *node->mixer.inputs[j];
                }
                outputs[i] = sum;
                break;
            }
            default:
                break;
            }

            if (node->gain) {
                /* For now, apply a simple linear gain.
                 * Positive gain amplifies while negative gain attenuates the
                 * signal, ensuring compatibility with envelope generators.
                 */
                outputs[i] = (outputs[i] * *node->gain) >> 15;
            }
        }

        /* Second iteration: update each node's state based on the computed
         * outputs.
         */
        for (int i = 0; i < SYNTH_NODES && nodes[i].type != SYNTH_NODE_NONE;
             i++) {
            synth_node_t *node = &nodes[i];
            node->output = outputs[i];
            switch (node->type) {
            case SYNTH_NODE_OSCILLATOR:
                /* Increment the phase by the base value and adjust for any
                 * detuning.
                 */
                node->state += *node->osc.phase_incr;
                if (node->osc.detune)
                    node->state += *node->osc.detune;
                node->state &= 0x7ffff;  /* wrap around q15 */
                break;
            case SYNTH_NODE_ENVELOPE:
                /* The highest bit in the state indicates decay mode
                 * (post-attack).
                 */
                if (voice->gate) {
                    /* When the gate is active, process attack followed by decay
                     */
                    int mode_bit = node->state & 0x80000000;
                    int32_t value = node->state & 0x7fffffff;
                    if (mode_bit) {
                        /* Decay phase: decrement the state by the decay rate */
                        value -= node->env.decay;
                        const q15_t sustainAbs = node->env.sustain < 0
                                                     ? -node->env.sustain
                                                     : node->env.sustain;
                        if (value < sustainAbs << 4)
                            value = sustainAbs << 4;
                    } else {
                        /* Attack phase: increment the state by the attack rate
                         */
                        value += node->env.attack;
                        if (value > (int32_t) Q15_MAX << 4) {
                            value = (int32_t) Q15_MAX << 4;
                            /* Switch to decay mode once the peak is reached */
                            mode_bit = 0x80000000;
                        }
                    }
                    node->state = value | mode_bit;
                } else {
                    /* When the gate is inactive, perform the release phase */
                    /* Clear the decay mode bit so that the next gate triggers a
                     * fresh attack.
                     */
                    node->state &= 0x7fffffff; 
                    node->state -= node->env.release;
                    if (node->state < 0)
                        node->state = 0;
                }
                break;
            case SYNTH_NODE_FILTER_LP:
                /* Update the accumulator for the low-pass filter based on the
                 * input difference.
                 */
                node->filter.accum += (*node->filter.input - node->output);
                break;
            case SYNTH_NODE_FILTER_HP:
                /* Update the accumulator for the high-pass filter in a similar
                 * fashion.
                 */
                node->filter.accum += (*node->filter.input - node->output);
                break;
            case SYNTH_NODE_MIXER:
                /* Mixer nodes do not require state updates in this pass */
                break;
            default:
                break;
            }
        }

        /* Accumulate the output from the primary node of the voice into the
         * main output.
         */
        main_output += voice->nodes[0].output;
    }

    const q15_t main_mixer_gain = Q15_MAX / SYNTH_VOICES;
    return (main_output * main_mixer_gain) >> 15;
}

/* use 8 bit 128 sample LUT for sine wave */
static const q7_t sine_lut[128 + 1] = {
    0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60, 65, 71, 76, 81, 85, 90, 94, 98, 102, 106, 109, 112, 115, 117, 120, 122, 123, 125, 126, 126, 127, 127, 127, 126, 126, 125, 123, 122, 120, 117, 115, 112, 109, 106, 102, 98, 94, 90, 85, 81, 76, 71, 65, 60, 54, 49, 43, 37, 31, 25, 19, 12, 6, 0, -6, -12, -19, -25, -31, -37, -43, -49, -54, -60, -65, -71, -76, -81, -85, -90, -94, -98, -102, -106, -109, -112, -115, -117, -120, -122, -123, -125, -126, -126, -127, -127, -127, -126, -126, -125, -123, -122, -120, -117, -115, -112, -109, -106, -102, -98, -94, -90, -85, -81, -76, -71, -65, -60, -54, -49, -43, -37, -31, -25, -19, -12, -6, 0
};

/*
 * All of these wave generator functions expect a basic ramp sawtooth signal
 * ranging from 0 to 1 as input and produce a corresponding waveform that
 * oscillates between -1 and 1.
 */

q15_t sawtooth_wave(q15_t input)
{
    return input * 2 - Q15_MAX;
}

q15_t sine_wave(q15_t input)
{
    int index = (input >> 8) & 0x7F;
    /* Scale by 258 to span the full output range */
    q15_t res = sine_lut[index] * 258;
    /* Perform interpolation between adjacent lookup table values */
    q15_t next = sine_lut[index + 1] * 258;
    res += ((next - res) * (0x7F) >> 8); 

    return res;
}

q15_t square_wave(q15_t input)
{
    return input < Q15_MAX / 2 ? Q15_MIN : Q15_MAX;
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Phase increment for a 5 Hz LFO */
static q15_t lfo_phase_inc = SYNTH_HZ_TO_PHASE(5);

/* Phase increment for vibrato modulation (10 Hz frequency) */
static q15_t vibra_to_inc = SYNTH_HZ_TO_PHASE(10);

static int write_wav(const char *filename,
                     const int16_t *audio_buffer,
                     uint32_t sample_count)
{
    FILE *f = fopen(filename, "wb");
    if (!f) {
        printf("Failed to open output file\n");
        return 1;
    }

    /* Calculate header values */
    uint32_t sampleRate = SAMPLE_RATE;
    uint32_t fileSize = sample_count * 2 + 36;
    uint32_t byteRate = SAMPLE_RATE * 2;
    uint32_t blockAlign = 2;
    uint32_t bitsPerSample = 16;
    uint32_t dataSize = sample_count * 2;

    /* Write RIFF header */
    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    /* Write 'fmt ' chunk */
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    uint16_t format = 1;
    fwrite(&format, 2, 1, f);
    uint16_t channels = 1;
    fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f);
    fwrite(&bitsPerSample, 2, 1, f);

    /* Write 'data' chunk header */
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);

    /* Write audio data */
    fwrite(audio_buffer, 2, sample_count, f);

    fclose(f);
    return 0;
}

int main()
{
    /* Configure voice 0:
     * This voice chains an envelope, LFO, oscillator, and low-pass filter.
     * Convention: the first node in the chain (nodes[0]) is the final output.
     */
    synth_voice_t *voice = &synth_voices[0];

    synth_init_envelope_node(&voice->nodes[1], NULL, /* gain */
                             500,                    /* attack */
                             150,                    /* decay */
                             Q15_MAX * .8,           /* sustain */
                             150                     /* release */
    );

    /* Initialize oscillator */
    synth_init_osc_node(
        &voice->nodes[3], &voice->nodes[1].output, /* gain - from envelope */
        &voice->phase_incr,      /* phase increment - from MIDI note */
        &voice->nodes[2].output, /* detune - from envelope */
        sawtooth_wave);

    /* Low-Frequency Oscillator */
    synth_init_osc_node(&voice->nodes[2], &vibra_to_inc, /* gain */
                        &lfo_phase_inc,                  /* phase increment */
                        NULL,                            /* detune */
                        sine_wave);

    /* Initialize low-pass filter */
    synth_init_filter_lp_node(&voice->nodes[0], NULL,  /* gain */
                              &voice->nodes[3].output, /* input */
                              8000                     /* factor */
    );

    /* Configure voice 1 with alternative settings:
     * This voice uses a similar chain with different envelope and filter
     * parameters.
     */
    voice = &synth_voices[1];

    synth_init_envelope_node(&voice->nodes[1], NULL, /* gain */
                             100,                    /* attack */
                             500,                    /* decay */
                             Q15_MAX * 0.5,          /* sustain */
                             15                      /* release */
    );

    synth_init_osc_node(
        &voice->nodes[2], &voice->nodes[1].output, /* gain - from envelope */
        &voice->phase_incr, /* phase increment - from MIDI note */
        NULL, square_wave);

    synth_init_filter_lp_node(&voice->nodes[0], NULL,  /* gain */
                              &voice->nodes[2].output, /* input */
                              4000                     /* factor */
    );

    /* fill up a buffer with audio */
    int16_t *audio_buffer = malloc(SAMPLE_RATE * 60); /* 60 seconds */
    assert(audio_buffer);
    uint32_t sample_count = 0;

    /* Define the melody for "Twinkle Twinkle Little Star" using MIDI note
     * numbers. A note value of 0 indicates a rest.
     */
    const uint8_t twinkleTwinkle[] = {
        60, 60, 67, 67, 69, 69, 67, 0, 65, 65, 64, 64, 62, 62, 60, 0,
        67, 67, 65, 65, 64, 64, 62, 0, 67, 67, 65, 65, 64, 64, 62, 0,
        60, 60, 67, 67, 69, 69, 67, 0, 65, 65, 64, 64, 62, 62, 60, 0,
    };

    /* Define the rhythmic values (beats) corresponding to each note.
     * These values determine the duration of each note.
     */
    const uint8_t twinkleTwinkleBeats[] = {
        4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2,
        4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2,
    };

    /* Counter for the remaining duration (in samples) of the current note */
    uint32_t note_duration = 0;

    /* Index to the current note in the melody arrays */
    uint32_t note_index = 0;

    /* Process audio until the entire melody is played */
    for (;;) {
        if (note_duration == 0) {
            /* Calculate note duration in samples.
             * The duration is determined by the beat value: shorter beats
             * result in longer note durations.
             */
            note_duration = SYNTH_MS(2000 / twinkleTwinkleBeats[note_index]);

            /* Retrieve the MIDI note for the current position */
            uint8_t note = twinkleTwinkle[note_index];
            if (note) {
                /* Voice 0 plays the note as given, and Voice 1 plays two
                 * octaves lower.
                 */
                synth_voice_note_on(&synth_voices[0], note);
                synth_voice_note_on(&synth_voices[1], note - 24);
            }
            note_index++;
            if (note_index >= sizeof(twinkleTwinkle))
                break;
        } else if (note_duration < 500) {
            /* When the note duration is almost over, cut the note short to
             * allow for a natural decay.
             */
            synth_voice_note_off(&synth_voices[0]);
            synth_voice_note_off(&synth_voices[1]);
        }
        note_duration--;

        /* Process a single audio sample and store it in the buffer */
        q15_t v = synth_process();
        audio_buffer[sample_count++] = v;
    }

    /* write the audio buffer to a wav file */
    int res = write_wav("out.wav", audio_buffer, sample_count);
    free(audio_buffer);
    return res;
}