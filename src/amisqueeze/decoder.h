/*
 * decoder.h - portable core: decoders turn the stream of a track (bytes waiting in the ring buffer) into PCM for
 * the output (plan step 1.8).
 *
 * The output asks the decoder of the track at the buffer's read position for PCM, as much as it can take; the
 * decoder reads what it needs from the ring buffer, never past the track's end (the next track may follow in the
 * buffer). Two kinds of position follow from it: the ring's are bytes of the stream, the output's are bytes of
 * decoded PCM (struct track: start and out_start). A decoder that needs more input than is there returns 0 without
 * consuming anything; one that keeps decoded data between calls holds it itself. State is allocated, never on the
 * stack (the Shell's 4 KB stack).
 */
#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>

#include "ring.h"

struct pcm_format;
struct decoder;

struct decoder_ops {
    const char *name;
    /* A new track begins: in = the format announced by the server; sets *out to the PCM format it produces. 0 if
     * the track cannot be decoded. */
    int (*start)(struct decoder *d, const struct pcm_format *in, struct pcm_format *out);
    /* Reads at most avail bytes of the track from r (complete: no more bytes of this track will come) and writes at
     * most out_max bytes of PCM (whole frames) at out. Returns the bytes written: 0 = more input needed, or the
     * track's input is used up (whatever could not be decoded at its end is dropped). */
    uint32_t (*run)(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out, uint32_t out_max);
    /* Bytes of input wanted to produce out bytes of PCM (the output waits for them rather than send a short block) */
    uint32_t (*input_for)(const struct decoder *d, uint32_t out);
    /* Bytes of decoded PCM it still holds (decoded from input already consumed): the next track's output starts
     * after them */
    uint32_t (*buffered)(const struct decoder *d);
};

struct decoder {
    const struct decoder_ops *ops;     /* NULL: no track started */
    uint64_t track;                    /* start (ring position) of the track being decoded */
    uint32_t in_frame;                 /* bytes per frame of the input, for the PCM decoder */
};

/* Raw PCM (strm format 'p'): copied as is */
extern const struct decoder_ops decoder_pcm;

#endif
