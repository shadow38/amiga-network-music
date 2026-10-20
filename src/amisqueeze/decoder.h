/*
 * decoder.h - portable core: decoders turn the stream of a track (bytes waiting in the ring buffer) into PCM for
 * the output (plan step 1.8).
 *
 * The output asks the decoder of the track being decoded for PCM, as much as it can take; the decoder reads what
 * it needs from the ring buffer, never past the track's end (the next track may follow in the buffer). Two kinds
 * of position follow from it: the ring's are bytes of the stream, the output's are bytes of decoded PCM (struct
 * track: start and out_start). A decoder that needs more input than is there returns 0 without consuming more; one
 * that reads ahead or keeps decoded PCM between calls holds it itself, and its track stays the one being decoded
 * until it has finished. State is allocated, never on the stack (the Shell's 4 KB stack).
 */
#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>

#include "ring.h"

/* PCM format of a track, announced by strm s (or found by its decoder) */
struct pcm_format {
    uint32_t rate;                      /* Hz */
    uint8_t  channels;                  /* 1 or 2 */
    uint8_t  sample_bytes;              /* 1 to 4 (8 to 32 bits) */
    uint8_t  big_endian;
};

struct decoder;

struct decoder_ops {
    const char *name;
    /* A new track begins (in = the format announced by the server): sets d->out, the PCM format it produces, or
     * its best guess when that is only known from the stream itself. 0 if the track cannot be decoded. */
    int (*start)(struct decoder *d, const struct pcm_format *in);
    /* The track is left (end, stop, flush): frees what start allocated */
    void (*stop)(struct decoder *d);
    /* Reads at most avail bytes of the track from r (complete: no more bytes of this track will come) and writes at
     * most out_max bytes of PCM (whole frames, in d->out, which it may update first) at out. Returns the bytes
     * written: 0 = more input needed, or the track is used up (what could not be decoded at its end is dropped). */
    uint32_t (*run)(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out, uint32_t out_max);
    /* Bytes of input wanted to produce out bytes of PCM (the output waits for them rather than send a short block) */
    uint32_t (*input_for)(const struct decoder *d, uint32_t out);
    /* Bytes of decoded PCM it holds, ready to be handed over */
    uint32_t (*buffered)(const struct decoder *d);
    /* 1 once everything it took from the buffer has been handed over (it may read ahead): only then may the next
     * track begin */
    int (*finished)(const struct decoder *d);
};

struct decoder {
    const struct decoder_ops *ops;     /* NULL: no track started */
    uint64_t track;                    /* start (ring position) of the track being decoded */
    struct pcm_format out;             /* what it produces */
    uint32_t in_frame;                 /* bytes per frame of the input, for the PCM decoder */
    void *state;                       /* allocated by start, freed by stop */
};

/* Raw PCM (strm format 'p'): copied as is */
extern const struct decoder_ops decoder_pcm;
/* FLAC (strm format 'f'), with dr_flac */
extern const struct decoder_ops decoder_flac;
#ifdef AMISQUEEZE_MP3
/* MP3 (strm format 'm'), with minimp3: only in the builds with floating point in hardware (and the host's) */
extern const struct decoder_ops decoder_mp3;
/* The LAME header of an MP3 file's first frame (f, size bytes), for gapless playback: 0 if f is an audio frame (or
 * a header cut too short to read); 1 if it is a header, which holds no audio: then *lead (PCM frames to drop at the
 * start) and *left (PCM frames of the track) are set when it gives them, *left only for a non-zero frame count */
int mp3_lame_header(const uint8_t *f, uint32_t size, uint32_t *lead, uint64_t *left);
#endif

/* 1 on a big-endian machine (the Amiga), 0 on a little-endian one (most hosts): decoders write native samples */
int host_big_endian(void);

#endif
