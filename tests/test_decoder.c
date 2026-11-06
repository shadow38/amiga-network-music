/*
 * test_decoder.c - unit tests of decoder.c, flac.c, mp3.c (host build, see "make test"): the WAV header, a FLAC
 * track refused, the LAME header
 */
#include <unistd.h>

#include "test_core.h"

/* ---- WAV header (Music Assistant's wav output codec) ---- */

static uint8_t wav_buf[8192];

static void set_le16(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8);
}

static void set_le32(uint8_t *b, uint32_t v)
{
    set_le16(b, v); set_le16(b + 2, v >> 16);
}

/* A WAV header at b: a format chunk of fmt_len bytes (0: none; 16, or 40 with tag 0xfffe: WAVE_FORMAT_EXTENSIBLE,
 * sub-format PCM), a LIST chunk of list_len bytes (0: none; odd: followed by its pad byte), then the data chunk's
 * header. Returns its length. */
static uint32_t wav(uint8_t *b, uint32_t tag, uint32_t channels, uint32_t rate, uint32_t bits, uint32_t fmt_len,
                    uint32_t list_len, uint32_t data_len)
{
    static const uint8_t pcm_guid[16] = { 1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 };
    uint32_t n = 12, frame = channels * bits / 8;

    memcpy(b, "RIFF", 4);
    set_le32(b + 4, 0xffffffff);
    memcpy(b + 8, "WAVE", 4);
    if (fmt_len > 0) {
        memcpy(b + n, "fmt ", 4);
        set_le32(b + n + 4, fmt_len);
        n += 8;
        memset(b + n, 0, fmt_len);
        set_le16(b + n, tag);
        set_le16(b + n + 2, channels);
        set_le32(b + n + 4, rate);
        set_le32(b + n + 8, rate * frame);
        set_le16(b + n + 12, frame);
        set_le16(b + n + 14, bits);
        if (fmt_len >= 40) {
            set_le16(b + n + 16, 22);
            set_le16(b + n + 18, bits);
            set_le32(b + n + 20, 3);    /* front left and right */
            memcpy(b + n + 24, pcm_guid, 16);
        }
        n += fmt_len;
    }
    if (list_len > 0) {
        memcpy(b + n, "LIST", 4);
        set_le32(b + n + 4, list_len);
        memset(b + n + 8, 'x', list_len + (list_len & 1));
        n += 8 + list_len + (list_len & 1);
    }
    memcpy(b + n, "data", 4);
    set_le32(b + n + 4, data_len);
    return n + 8;
}

/* Music Assistant's header (seen in WinUAE on 2026-10-21): 24 bits, WAVE_FORMAT_EXTENSIBLE, a LIST chunk of 26
 * bytes, data to the end of the stream; 102 bytes */
static uint32_t wav_ma(uint8_t *b)
{
    return wav(b, 0xfffe, 2, 44100, 24, 40, 26, 0xffffffff);
}

/* A track of header + data on the virtual output, strm s announcing 16 bits as Music Assistant does: the header
 * coming in two reads cut at `cut`, the output running between them. Returns the bytes written to the file. */
static int play_wav(struct player *p, const uint8_t *h, uint32_t hlen, uint32_t cut, const uint8_t *data,
                    uint32_t n)
{
    setup_file(p);
    open_stream(p, '1', 0);
    stub_feed_stream(h, (int)cut);
    drain_stream(p);
    stub_now += 10;
    output_tick(p, stub_now);
    stub_feed_stream(h + cut, (int)(hlen - cut));
    stub_feed_stream(data, (int)n);
    drain_stream(p);
    stub_stream_end();
    stream_io(p, PLAT_READ);
    play_for(p, 2000);
    return stub_file_len;
}

/* Music Assistant's header, cut at every length: the data played in the header's format, the header dropped; the
 * track takes as long as its 24-bit data (6 bytes per frame) */
static void test_wav_header(void)
{
    struct player p;
    uint32_t h = wav_ma(wav_buf), n = 6 * 10000, cut;

    fill(track_a, sizeof(track_a), 21);
    CHECK(h == 102);
    for (cut = 1; cut <= h; cut++) {
        int len = play_wav(&p, wav_buf, h, cut, track_a, n);

        CHECK(len == (int)n && !memcmp(stub_file, track_a, n) && p.out.state == OUTPUT_STOPPED);
        CHECK(p.out.cur.out_fmt.sample_bytes == 3 && p.out.cur.out_fmt.big_endian == 0);
        if (cut == 1)
            CHECK(strstr(stub_out, "stream: WAV header: 24 bits, 44100 Hz, 2 channel(s), to the end of the "
                                   "stream") != NULL && strstr(stub_out, "wav:") == NULL);
        teardown(&p);
    }
    n = 132300;                                         /* 0.5 s of 24-bit stereo at 44.1 kHz */
    setup_file(&p);
    open_stream(&p, '1', 0);
    stub_feed_stream(wav_buf, (int)h);
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_now += 1;
    output_tick(&p, stub_now);                          /* the header read */
    play_for(&p, 400);
    CHECK(p.out.state == OUTPUT_RUNNING && output_elapsed_ms(&p) >= 395 && output_elapsed_ms(&p) <= 405);
    play_for(&p, 200);                                  /* as 16 bits, it would last 0.75 s */
    CHECK(p.out.state == OUTPUT_STOPPED && stub_file_len == (int)n && !memcmp(stub_file, track_a, n));
    teardown(&p);
}

/* A track of the first len bytes of wav_buf (then n bytes of track_a if with_data), then a track of n bytes of
 * track_b, raw PCM; played on the virtual output */
static void play_after_wav(struct player *p, uint32_t len, int with_data, uint32_t n)
{
    setup_file(p);
    open_stream(p, '1', 0);
    stub_feed_stream(wav_buf, (int)len);
    if (with_data)
        stub_feed_stream(track_a, (int)n);
    drain_stream(p);
    stub_stream_end();
    stream_io(p, PLAT_READ);
    stub_reset();
    open_stream(p, '1', 0);                             /* the next track, raw PCM */
    stub_feed_stream(track_b, (int)n);
    drain_stream(p);
    stub_stream_end();
    stream_io(p, PLAT_READ);
    play_for(p, 2000);
}

/* A plain format chunk, a LIST chunk of odd size (and its pad byte), a data size that is not a whole number of
 * frames, a LIST chunk after the data: only the whole frames of the data are played */
static void test_wav_limited(void)
{
    struct player p;
    uint32_t h = wav(wav_buf, 1, 2, 48000, 16, 16, 25, 1001), n = 1001;

    fill(track_a, sizeof(track_a), 22);
    fill(track_b, sizeof(track_b), 26);
    memcpy(track_a + n, "LIST\x1e\0\0\0", 8);           /* 30 bytes of tags after the data */
    CHECK(play_wav(&p, wav_buf, h, h, track_a, n + 38) == 1000 && !memcmp(stub_file, track_a, 1000));
    CHECK(strstr(stub_out, "stream: WAV header: 16 bits, 48000 Hz, 2 channel(s), 1001 bytes") != NULL);
    CHECK(p.out.state == OUTPUT_STOPPED && p.out.cur.out_fmt.rate == 48000);
    teardown(&p);
    /* the next track, raw PCM, is not limited by the WAV's data size: played whole */
    h = wav(wav_buf, 1, 2, 44100, 16, 16, 0, 1000);
    n = PCM_BPS / 10;
    play_after_wav(&p, h, 1, n);
    CHECK(stub_file_len == (int)(1000 + n) && !memcmp(stub_file, track_a, 1000));
    CHECK(!memcmp(stub_file + 1000, track_b, n) && p.out.state == OUTPUT_STOPPED);
    teardown(&p);
}

/* Headers amisqueeze cannot play: the track skipped with a message, nothing played (no noise); a header that is
 * not a WAV one: raw PCM, played as announced */
static void test_wav_refused(void)
{
    static const struct { const char *why; uint32_t tag, channels, rate, bits, fmt_len, list_len, cut; } cases[] = {
        { "wav: not integer PCM, track skipped", 3, 2, 44100, 32, 16, 0, 0 },         /* floating point */
        { "wav: not integer PCM, track skipped", 0xfffe, 2, 44100, 32, 40, 0, 0 },    /* sub-format: float */
        { "wav: number of channels not supported, track skipped", 1, 6, 44100, 16, 16, 0, 0 },
        { "wav: sample size not supported, track skipped", 1, 2, 44100, 12, 16, 0, 0 },
        { "wav: sample size not supported, track skipped", 1, 2, 44100, 0, 16, 0, 0 },   /* a frame of 0 bytes */
        { "wav: sample size not supported, track skipped", 1, 2, 44100, 40, 16, 0, 0 },
        { "wav: number of channels not supported, track skipped", 1, 0, 44100, 16, 16, 0, 0 }, /* a frame of 0 bytes */
        { "wav: sample rate not supported, track skipped", 1, 2, 0, 16, 16, 0, 0 },
        { "wav: sample rate not supported, track skipped", 1, 2, 0x40000000, 16, 16, 0, 0 },   /* 2^32 bytes a second */
        { "wav: format chunk too short, track skipped", 1, 2, 44100, 16, 14, 0, 0 },
        { "wav: no format chunk, track skipped", 1, 2, 44100, 16, 0, 0, 0 },
        { "wav: header too long, track skipped", 1, 2, 44100, 16, 16, 5000, 0 },
        { "wav: the track ends in its header, track skipped", 1, 2, 44100, 16, 16, 0, 30 },
        { "wav: the track ends in its header, track skipped", 1, 2, 44100, 16, 16, 0, 36 },  /* between chunks */
    };
    struct player p;
    uint32_t c, h, n = PCM_BPS / 10;

    fill(track_a, sizeof(track_a), 23);
    fill(track_b, sizeof(track_b), 24);
    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        h = wav(wav_buf, cases[c].tag, cases[c].channels, cases[c].rate, cases[c].bits, cases[c].fmt_len,
                cases[c].list_len, 0xffffffff);
        if (cases[c].tag == 0xfffe)
            wav_buf[12 + 8 + 24] = 3;                   /* KSDATAFORMAT_SUBTYPE_IEEE_FLOAT */
        play_after_wav(&p, cases[c].cut ? cases[c].cut : h, !cases[c].cut, n);
        CHECK(strstr(stub_out, cases[c].why) != NULL);
        CHECK(stub_file_len == (int)n && !memcmp(stub_file, track_b, n) && p.out.state == OUTPUT_STOPPED);
        teardown(&p);
    }
    /* a chunk length near 2^32, which would wrap the header's end round to the chunk's start: refused (a loop
     * without end turned into a failure by the alarm) */
    h = wav(wav_buf, 1, 2, 44100, 16, 16, 0, 0xffffffff);
    memcpy(wav_buf + h - 8, "LIST", 4);
    set_le32(wav_buf + h - 4, 0xfffffff8);
    alarm(30);
    play_after_wav(&p, h, 1, n);
    alarm(0);
    CHECK(strstr(stub_out, "wav: header too long, track skipped") != NULL);
    CHECK(stub_file_len == (int)n && !memcmp(stub_file, track_b, n) && p.out.state == OUTPUT_STOPPED);
    teardown(&p);
    /* a whole track shorter than a RIFF header: raw PCM, played as announced */
    memcpy(wav_buf, track_a, 8);
    play_after_wav(&p, 8, 0, n);
    CHECK(stub_file_len == (int)(8 + n) && !memcmp(stub_file, track_a, 8) && !memcmp(stub_file + 8, track_b, n));
    CHECK(strstr(stub_out, "wav:") == NULL && p.out.state == OUTPUT_STOPPED);
    teardown(&p);
    memcpy(track_a, "RIFF\0\0\0\0AIFF", 12);           /* not a WAV header: raw PCM */
    CHECK(play_wav(&p, track_a, 0, 0, track_a, n) == (int)n && !memcmp(stub_file, track_a, n));
    CHECK(strstr(stub_out, "WAV") == NULL && strstr(stub_out, "wav:") == NULL);
    teardown(&p);
}

/* strm s says 16 bits, the header 8: the audio output's first block is sized for 8-bit samples (half a block,
 * doubled when converted), not overrun */
static void test_wav_audio_8bit(void)
{
    struct player p;
    uint32_t h = wav(wav_buf, 1, 2, 44100, 8, 16, 0, 0xffffffff), n = 100000, dev;

    fill(track_a, sizeof(track_a), 25);
    setup_audio(&p, 0);
    open_stream(&p, '1', 0);
    stub_feed_stream(wav_buf, (int)h);
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    output_tick(&p, stub_now);
    CHECK(stub_audio[0].len == BLOCK && stub_audio[0].channels == 2);
    play_out(&p);
    dev = to_device_fmt(expect, track_a, n, 1, 0);
    CHECK(stub_audio_out_len == (int)dev && !memcmp(stub_audio_out, expect, dev));
    CHECK(audio_played(&p, stub_now, 0) == n && stub_audio_bad == 0);
    teardown(&p);
}

/* ---- FLAC ---- */

/* The FLAC decoder on a whole track of len bytes it cannot play: the reason said, the track dropped, its decoding
 * ended */
static void flac_refused(const uint8_t *data, uint32_t len, const char *said)
{
    static const struct pcm_format in = { 44100, 2, 2, 0 };
    struct ring r;
    struct decoder d;
    uint8_t *w, out[256];

    CHECK(ring_init(&r, 4096) && ring_write_space(&r, &w) >= len);
    memcpy(w, data, len);
    ring_commit(&r, len);
    memset(&d, 0, sizeof(d));
    stub_reset();
    CHECK(decoder_flac.start(&d, &in));
    CHECK(decoder_flac.run(&d, &r, len, 1, out, sizeof(out)) == 0);
    CHECK(strstr(stub_out, said) != NULL && ring_used(&r) == 0 && decoder_flac.finished(&d));
    decoder_flac.stop(&d);
    ring_free(&r);
}

/* The checks of flac.c: not FLAC, no STREAMINFO block, more than 2 channels. "cannot open the stream" is left out:
 * dr_flac opens any STREAMINFO, and flac.c always gives it one, so only a lack of memory leads there. */
static void test_flac_refused(void)
{
    /* "fLaC", the last metadata block: STREAMINFO (34 bytes), 4096 PCM frames per block, 44100 Hz, 3 channels,
     * 16 bits */
    static const uint8_t three[42] = { 'f', 'L', 'a', 'C', 0x80, 0, 0, 34, 0x10, 0x00, 0x10, 0x00, 0, 0, 0, 0, 0, 0,
                                       0x0A, 0xC4, 0x44, 0xF0 };
    /* "fLaC", then a PADDING block of 4 bytes, the last one */
    static const uint8_t no_info[12] = { 'f', 'L', 'a', 'C', 0x81, 0, 0, 4 };

    flac_refused((const uint8_t *)"RIFF\0\0\0\0WAVE", 12, "amisqueeze: flac: not a FLAC stream, track skipped\n");
    flac_refused(no_info, sizeof(no_info), "amisqueeze: flac: no STREAMINFO block, track skipped\n");
    flac_refused(three, sizeof(three), "amisqueeze: flac: more than 2 channels, track skipped\n");
}

/* ---- MP3: the LAME header ---- */

/* A first MP3 frame holding a Xing/Info header (b1, b3: bytes 1 and 3 of the frame header: version, CRC,
 * channels), with a frame count if frames >= 0 and a LAME extension if ext; returns the bytes written */
static uint32_t lame_frame(uint8_t *f, uint8_t b1, uint8_t b3, const char *tag, int32_t frames, int ext,
                           uint32_t delay, uint32_t padding)
{
    int mpeg1 = (b1 & 0x18) == 0x18, mono = (b3 >> 6) == 3;
    uint32_t off = 4 + ((b1 & 1) ? 0 : 2) + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));

    memset(f, 0, 400);
    f[0] = 0xFF;
    f[1] = b1;
    f[2] = 0x90;
    f[3] = b3;
    memcpy(f + off, tag, 4);
    f[off + 7] = frames >= 0 ? 1 : 0;
    off += 8;
    if (frames >= 0) {
        f[off] = (uint8_t)(frames >> 24);
        f[off + 1] = (uint8_t)(frames >> 16);
        f[off + 2] = (uint8_t)(frames >> 8);
        f[off + 3] = (uint8_t)frames;
        off += 4;
    }
    if (ext) {
        memcpy(f + off, "LAME3.100", 9);
        f[off + 21] = (uint8_t)(delay >> 4);
        f[off + 22] = (uint8_t)((delay & 15) << 4 | padding >> 8);
        f[off + 23] = (uint8_t)padding;
    }
    return off + 36;
}

/* Gapless MP3: what the LAME header of the first frame gives, and its fallbacks (no frame count, no extension,
 * CRC, MPEG-2 mono, cut short, a count of 0) */
static void test_lame_header(void)
{
    uint8_t f[400];
    uint32_t lead, size, n;
    uint64_t left;
    int cut_ok;

    /* MPEG-1 stereo, LAME: delay 576 + 529 of the decoder, padding 1000 - 529, out of 100 frames of 1152 */
    size = lame_frame(f, 0xFB, 0x00, "Info", 100, 1, 576, 1000);
    lead = 0;
    left = UINT64_MAX;
    CHECK(mp3_lame_header(f, size, &lead, &left) && lead == 1105 && left == 115200 - 1105 - 471);
    /* With a CRC (2 more bytes before the side information): the same */
    size = lame_frame(f, 0xFA, 0x00, "Xing", 100, 1, 576, 1000);
    lead = 0;
    left = UINT64_MAX;
    CHECK(mp3_lame_header(f, size, &lead, &left) && lead == 1105 && left == 115200 - 1105 - 471);
    /* MPEG-2 mono: 9 bytes of side information, 576 samples per frame */
    size = lame_frame(f, 0xF3, 0xC0, "Xing", 100, 1, 576, 1000);
    lead = 0;
    left = UINT64_MAX;
    CHECK(mp3_lame_header(f, size, &lead, &left) && lead == 1105 && left == 57600 - 1105 - 471);
    /* No frame count, or no LAME extension: a header (no audio), nothing to trim */
    size = lame_frame(f, 0xFB, 0x00, "Xing", -1, 0, 0, 0);
    lead = 0;
    left = UINT64_MAX;
    CHECK(mp3_lame_header(f, size, &lead, &left) && lead == 0 && left == UINT64_MAX);
    size = lame_frame(f, 0xFB, 0x00, "Info", 100, 0, 0, 0);
    CHECK(mp3_lame_header(f, size, &lead, &left) && lead == 0 && left == UINT64_MAX);
    /* A frame count of 0: the delay is dropped, but no limit (the track would be dropped whole) */
    size = lame_frame(f, 0xFB, 0x00, "Info", 0, 1, 576, 1000);
    CHECK(mp3_lame_header(f, size, &lead, &left) && lead == 1105 && left == UINT64_MAX);
    /* Cut short, before the extension's delay: taken as an audio frame, nothing set */
    size = lame_frame(f, 0xFB, 0x00, "Info", 100, 1, 576, 1000);
    lead = 0;
    CHECK(!mp3_lame_header(f, size - 20, &lead, &left) && lead == 0 && left == UINT64_MAX);
    /* Cut at any length (each of its bounds: tag, frame count, extension), in a block of that size, so that a read
     * past it shows under ASan: never a header, nothing set */
    for (n = 4, cut_ok = 1; n < size; n++) {
        uint8_t *cut = malloc(n);

        memcpy(cut, f, n);
        if (mp3_lame_header(cut, n, &lead, &left) || lead != 0 || left != UINT64_MAX)
            cut_ok = 0;
        free(cut);
    }
    CHECK(cut_ok);
    /* An audio frame: no header */
    memset(f + 4, 0, 60);
    CHECK(!mp3_lame_header(f, size, &lead, &left) && lead == 0);
}

void tests_decoder(void)
{
    test_wav_header();
    test_wav_limited();
    test_wav_refused();
    test_wav_audio_8bit();
    test_flac_refused();
    test_lame_header();
}
