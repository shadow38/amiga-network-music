/*
 * test_audio.c - unit tests of audio.c (host build, see "make test"): the audio output's blocks (AHI), the volume,
 * the AHI unit's mode
 */
#include "test_core.h"

/* A whole track: blocks of 32 KB linked one to the other, converted, played in order; STMu after the last one */
static void test_audio_blocks(void)
{
    struct player p;
    int order[8], n, guard;

    fill(track_a, sizeof(track_a), 3);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));          /* 1 s, little-endian stereo */
    drain_stream(&p);
    CHECK(p.out.state == OUTPUT_RUNNING);
    output_tick(&p, stub_now);
    n = stub_audio_queue(order);
    CHECK(n == 2 && stub_audio_sends == 2);            /* two with the device, the third one filled */
    CHECK(stub_audio[order[0]].link == -1 && stub_audio[order[1]].link == order[0]);
    CHECK(stub_audio[order[0]].len == BLOCK && stub_audio[order[0]].rate == 44100 &&
          stub_audio[order[0]].channels == 2);
    CHECK(ring_used(&p.ring) == sizeof(track_a) - AUDIO_BLOCKS * BLOCK && audio_pending(&p) == AUDIO_BLOCKS);
    stub_now += 100;
    CHECK(output_elapsed_ms(&p) == 100);                /* estimated within the block playing */

    CHECK(finish(&p) == order[0]);                      /* the first block ends: the third one is sent, linked */
    n = stub_audio_queue(order);                        /* to the one now playing; the first one is refilled */
    CHECK(n == 2 && stub_audio_sends == 3 && stub_audio[order[1]].link == order[0]);
    CHECK(audio_pending(&p) == AUDIO_BLOCKS);

    stub_stream_end();
    stream_io(&p, PLAT_READ);
    clear_tx();
    for (guard = 0; guard < 20 && p.out.state == OUTPUT_RUNNING; guard++) {
        CHECK(strstr(trace(), "STMu") == NULL);         /* not before the last block ended */
        finish(&p);
    }
    CHECK(p.out.state == OUTPUT_STOPPED && strstr(trace(), "STAT:STMu") != NULL);
    CHECK(stub_audio_out_len == (int)sizeof(track_a));
    to_device(expect, track_a, sizeof(track_a), 0);
    CHECK(!memcmp(stub_audio_out, expect, sizeof(track_a)));
    CHECK(stub_audio_sends == 6);                       /* 5 full blocks, then the 12560 bytes left */
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

/* SWAP: left and right swapped after the byte swap; a big-endian mono track is sent as is */
static void test_audio_swap_mono(void)
{
    struct player p;
    char mono_be[7] = { '1', 'p', '1', '3', '1', '0', 1 };

    setup_audio(&p, 1);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, 4000);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    output_tick(&p, stub_now);
    while (finish(&p) >= 0)
        ;
    to_device(expect, track_a, 4000, 1);
    CHECK(stub_audio_out_len == 4000 && !memcmp(stub_audio_out, expect, 4000));
    teardown(&p);

    setup_audio(&p, 1);
    feed_strm('s', mono_be, 0, 9000, 0, request);
    drain(&p);
    stream_io(&p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    stub_feed_stream(track_a, 4000);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    output_tick(&p, stub_now);
    CHECK(stub_audio[0].channels == 1);
    while (finish(&p) >= 0)
        ;
    CHECK(stub_audio_out_len == 4000 && !memcmp(stub_audio_out, track_a, 4000));
    teardown(&p);
}

/* Pause: the queued blocks are aborted (sound cut at once), what they did not play is sent again on resume */
static void test_audio_pause(void)
{
    struct player p;
    int order[8];

    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);
    stub_audio_abort_played = 1000;

    clear_tx();
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMp"));
    CHECK(stub_audio_queue(order) == 0);                /* nothing left playing */
    stub_now += 2000;
    CHECK(output_elapsed_ms(&p) == 1000 * 1000 / PCM_BPS);   /* 1000 bytes played: time stands still */

    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(stub_audio_queue(order) == 2);
    CHECK(stub_audio[order[0]].len == BLOCK - 1000 && stub_audio[order[0]].link == -1);
    CHECK(stub_audio[order[1]].len == BLOCK && stub_audio[order[1]].link == order[0]);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    while (finish(&p) >= 0)
        ;
    to_device(expect, track_a, sizeof(track_a), 0);
    CHECK(stub_audio_out_len == (int)sizeof(track_a) && !memcmp(stub_audio_out, expect, sizeof(track_a)));
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

/* AHI 4 does not tell how much an aborted request played: the clock says (frame-aligned), and the resumed data
 * follows on */
static void test_audio_pause_ahi4(void)
{
    struct player p;
    int order[8];

    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);
    stub_audio_abort_played = -1;
    stub_audio_abort_real = PCM_BPS / 10;               /* what the device really played in 100 ms */
    stub_now += 100;
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(p.audio.block[p.audio.order[0]].done == PCM_BPS / 10);     /* whole frames */
    CHECK(audio_played(&p, stub_now, 1) == PCM_BPS / 10 && output_elapsed_ms(&p) == 100);
    stub_audio_abort_played = 0;
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(stub_audio_queue(order) == 2 && stub_audio[order[0]].len == BLOCK - PCM_BPS / 10);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    while (finish(&p) >= 0)
        ;
    to_device(expect, track_a, sizeof(track_a), 0);
    CHECK(stub_audio_out_len == (int)sizeof(track_a) && !memcmp(stub_audio_out, expect, sizeof(track_a)));
    CHECK(stub_audio_bad == 0);
    stub_audio_abort_played = 0;
    stub_audio_abort_real = 0;
    teardown(&p);
}

/* Stop: everything queued is aborted, positions start again from 0 */
static void test_audio_stop(void)
{
    struct player p;
    int order[8];

    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);
    feed_strm('q', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(stub_audio_queue(order) == 0 && audio_pending(&p) == 0 && audio_played(&p, stub_now, 1) == 0);
    CHECK(p.out.state == OUTPUT_STOPPED && ring_used(&p.ring) == 0);
    CHECK(stub_audio_bad == 0);                         /* the one playing aborted first */

    stub_reset();                                       /* play again: the blocks are used again */
    stub_audio_clear();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, sizeof(track_b));
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    output_tick(&p, stub_now);
    while (finish(&p) >= 0)
        ;
    to_device(expect, track_b, sizeof(track_b), 0);
    CHECK(stub_audio_out_len == (int)sizeof(track_b) && !memcmp(stub_audio_out, expect, sizeof(track_b)));
    CHECK(p.out.state == OUTPUT_STOPPED && stub_audio_bad == 0);
    teardown(&p);
}

/* Two tracks: no block spans the boundary; STMs for the second one once the first has been played */
static void test_audio_gapless(void)
{
    struct player p;
    int order[8];

    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, 40000);                   /* track A: 32768 + 7232 bytes */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, 30000);                   /* track B */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);

    clear_tx();
    output_tick(&p, stub_now);
    CHECK(stub_audio_queue(order) == 2 && audio_pending(&p) == 3);
    CHECK(stub_audio[order[0]].len == BLOCK && stub_audio[order[1]].len == 40000 - BLOCK);
    CHECK(strstr(trace(), "STMs") == NULL);             /* B handed over, not played yet */
    finish(&p);
    stub_audio_queue(order);
    CHECK(stub_audio[order[1]].len == 30000);           /* B, in a block of its own */
    CHECK(strstr(trace(), "STMs") == NULL);
    finish(&p);                                         /* end of A */
    CHECK(strstr(trace(), "STAT:STMs") != NULL && output_elapsed_ms(&p) == 0);
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

/* While the stream goes on, a block is sent only when full, unless the device is about to run dry */
static void test_audio_wait_full_block(void)
{
    struct player p;

    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, 100000);                  /* 3 blocks + 1696 bytes, more to come */
    drain_stream(&p);
    output_tick(&p, stub_now);
    CHECK(stub_audio_sends == 2 && audio_pending(&p) == 3);
    finish(&p);
    CHECK(stub_audio_sends == 3 && audio_pending(&p) == 2);   /* 1696 bytes, 2 blocks left: wait */
    finish(&p);
    CHECK(stub_audio_sends == 4 && audio_pending(&p) == 2);   /* 1 block left: sent as it is */
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

/* Buffer empty while the stream goes on (slow network): STMo once, when the device has played everything; the
 * data that comes back is played, nothing lost. The stream stops after `first` bytes: past a whole frame, the ring
 * keeps the start of a frame the decoder cannot use yet, and is not empty. */
static void audio_starved(int first)
{
    struct player p;
    int guard;

    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, first);
    drain_stream(&p);
    clear_tx();
    output_tick(&p, stub_now);
    CHECK(stub_audio_sends == 2);                       /* a full block, and the rest: the device would run dry */
    finish(&p);
    CHECK(count_in_trace("STMo") == 0);                 /* one block still playing */
    finish(&p);
    for (guard = 0; guard < 5; guard++)
        finish(&p);
    CHECK(count_in_trace("STMo") == 1 && p.out.state == OUTPUT_RUNNING);

    stub_feed_stream(track_a + first, 100000 - first);
    drain_stream(&p);
    output_tick(&p, stub_now);
    CHECK(audio_pending(&p) > 0);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    for (guard = 0; guard < 20 && p.out.state == OUTPUT_RUNNING; guard++)
        finish(&p);
    to_device(expect, track_a, 100000, 0);
    CHECK(stub_audio_out_len == 100000 && !memcmp(stub_audio_out, expect, 100000));
    CHECK(count_in_trace("STMo") == 1 && count_in_trace("STAT:STMu") == 1);
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

static void test_audio_starved(void)
{
    audio_starved(40000);
    audio_starved(40002);                               /* 2 bytes into a frame of 4 */
}

/* No AHI */
static void test_audio_errors(void)
{
    struct player p;

    setup(&p, "Amiga");
    stub_audio_fails = 1;
    CHECK(!audio_open(&p, 0, 0) && !p.audio.enabled);
    CHECK(strstr(stub_out, "cannot open ahi.device unit 0") != NULL);
    teardown(&p);
}

/* strm s with the given size code ('0'..'3' = 8 to 32 bits), channels ('1', '2') and endianness ('0' = big,
 * '1' = little), 44.1 kHz; then connection and response headers */
static void open_stream_fmt(struct player *p, char size, char channels, char endian)
{
    char fmt[7] = { '1', 'p', size, '3', channels, endian, 1 };

    feed_strm('s', fmt, 0, 9000, 0, request);
    drain(p);
    stream_io(p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    drain_stream(p);
}

/* 8, 24 and 32-bit samples, both byte orders, mono and stereo: converted to 16-bit big-endian for the device, the
 * bytes played counted in bytes of the stream (elapsed time, end of the track) */
static void test_audio_formats(void)
{
    static const struct { char size, channels, endian; int bytes; } cases[] = {
        { '0', '2', '1', 1 },               /* 8-bit unsigned stereo (WAV) */
        { '2', '2', '1', 3 },               /* 24-bit little-endian stereo (FLAC, WAV) */
        { '2', '1', '0', 3 },               /* 24-bit big-endian mono (AIFF) */
        { '3', '2', '1', 4 },               /* 32-bit little-endian stereo */
        { '1', '2', '0', 2 },               /* 16-bit big-endian stereo (AIFF) */
    };
    uint32_t c;

    fill(track_a, sizeof(track_a), 7);
    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        struct player p;
        int channels = cases[c].channels - '0', sb = cases[c].bytes;
        uint32_t frame = (uint32_t)(channels * sb), n = 100000 - 100000 % frame;
        uint32_t full = (sb == 1 ? BLOCK / 2 : BLOCK) / frame * frame, dev;

        setup_audio(&p, 0);
        open_stream_fmt(&p, cases[c].size, cases[c].channels, cases[c].endian);
        stub_feed_stream(track_a, (int)n);
        drain_stream(&p);
        output_tick(&p, stub_now);
        CHECK(stub_audio[0].len == full / (uint32_t)sb * 2 && stub_audio[0].channels == channels);
        finish(&p);                                     /* the first block played: its stream bytes */
        CHECK(audio_played(&p, stub_now, 0) == full);
        CHECK(output_elapsed_ms(&p) == (uint32_t)((uint64_t)full * 1000 / (44100 * frame)));
        play_out(&p);
        dev = to_device_fmt(expect, track_a, n, sb, cases[c].endian == '0');
        CHECK(stub_audio_out_len == (int)dev && !memcmp(stub_audio_out, expect, dev));
        CHECK(audio_played(&p, stub_now, 0) == n && p.out.state == OUTPUT_STOPPED);
        CHECK(stub_audio_bad == 0);
        teardown(&p);
    }
}

/* 24 bits: a pause in the middle of a block counts the stream bytes of what was played; the rest follows on */
static void test_audio_24bit_pause(void)
{
    struct player p;
    uint32_t n = 100002, dev;

    fill(track_a, sizeof(track_a), 8);
    setup_audio(&p, 0);
    open_stream_fmt(&p, '2', '2', '1');
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    output_tick(&p, stub_now);
    stub_audio_abort_played = 1000;                     /* device bytes: 250 frames */
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(audio_played(&p, stub_now, 0) == 1500);       /* 250 frames of 6 bytes */
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    play_out(&p);
    dev = to_device_fmt(expect, track_a, n, 3, 0);
    CHECK(stub_audio_out_len == (int)dev && !memcmp(stub_audio_out, expect, dev));
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

/* Gapless from a 16-bit track to a 24-bit one: each block holds one track, the boundary (STMs) comes when the
 * 16-bit track has been played, and the device gets both, converted */
static void test_audio_gapless_24bit(void)
{
    struct player p;
    uint32_t na = 40000, nb = 30000, dev;

    fill(track_a, sizeof(track_a), 9);
    fill(track_b, sizeof(track_b), 10);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);                            /* 16-bit little-endian stereo */
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_reset();
    open_stream_fmt(&p, '2', '2', '1');                 /* then 24-bit */
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);
    clear_tx();
    play_out(&p);
    CHECK(count_in_trace("STAT:STMs") == 1 && count_in_trace("STAT:STMu") == 1);
    to_device(expect, track_a, na, 0);
    dev = to_device_fmt(expect + na, track_b, nb, 3, 0);
    CHECK(stub_audio_out_len == (int)(na + dev) && !memcmp(stub_audio_out, expect, na + dev));
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

/* ---- volume ---- */

/* audg body (after the opcode): old gains (2 x 4 bytes), adjust, preamp, then the 16.16 gains of left and right */
static void feed_audg(int adjust, uint32_t left, uint32_t right)
{
    uint8_t b[18];

    memset(b, 0, sizeof(b));
    b[8] = (uint8_t)adjust;
    set32(b + 10, left);
    set32(b + 14, right);
    stub_feed_packet("audg", b, sizeof(b));
}

/* Like to_device(), with the samples scaled by a 16.16 volume first */
static void to_device_vol(uint8_t *dst, const uint8_t *src, uint32_t n, int swap_lr, uint32_t volume)
{
    uint32_t i;

    to_device(dst, src, n, swap_lr);
    if (volume >= 0x10000)
        return;
    for (i = 0; i + 1 < n; i += 2) {
        int32_t s = (int16_t)(dst[i] << 8 | dst[i + 1]);

        s = s * (int32_t)volume >> 16;
        dst[i] = (uint8_t)(s >> 8);
        dst[i + 1] = (uint8_t)s;
    }
}

/* Volume (audg): the samples are scaled when a block is sent, the device always gets full volume; the blocks
 * already with the device keep theirs; the louder side, never above full; fixed volume = full */
static void test_audio_volume(void)
{
    struct player p;

    fill(track_a, sizeof(track_a), 5);
    setup_audio(&p, 1);
    p.verbose = 1;
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);                          /* two blocks sent at full volume, the third one filled */
    CHECK(stub_audio_sends == 2);

    stub_out[0] = '\0';
    feed_audg(1, 0x4000, 0x4000);
    drain(&p);
    CHECK(strstr(stub_out, "volume: 25.0 % of full amplitude") != NULL);
    play_out(&p);
    to_device_vol(expect, track_a, 2 * BLOCK, 1, 0x10000);
    to_device_vol(expect + 2 * BLOCK, track_a + 2 * BLOCK, sizeof(track_a) - 2 * BLOCK, 1, 0x4000);
    CHECK(stub_audio_out_len == (int)sizeof(track_a) && !memcmp(stub_audio_out, expect, sizeof(track_a)));

    stub_out[0] = '\0';
    feed_audg(1, 0x4000, 0x4000);                       /* the same again (the server repeats it): quiet */
    drain(&p);
    CHECK(strstr(stub_out, "volume:") == NULL);
    feed_audg(1, 0x0F00, 0x2000);                       /* the louder side */
    drain(&p);
    CHECK(p.audio.volume == 0x2000);
    feed_audg(1, 0x30000, 0x30000);                     /* never above full */
    drain(&p);
    CHECK(p.audio.volume == 0x10000);
    feed_audg(1, 0, 0);                                 /* muted */
    drain(&p);
    CHECK(p.audio.volume == 0 && strstr(stub_out, "volume: 0.0 %") != NULL);
    feed_audg(0, 0x0F00, 0x0F00);                       /* fixed volume: full, whatever the gains */
    drain(&p);
    CHECK(p.audio.volume == 0x10000 && p.audio.fixed && strstr(stub_out, "volume: fixed at 100 % by LMS") != NULL);
    stub_out[0] = '\0';
    feed_audg(1, 0x10000, 0x10000);                     /* back to the slider, at 100: reported */
    drain(&p);
    CHECK(p.audio.volume == 0x10000 && !p.audio.fixed && strstr(stub_out, "volume: 100.0 %") != NULL);
    stub_out[0] = '\0';
    feed_audg(0, 0x10000, 0x10000);                     /* fixed again from 100: reported too */
    drain(&p);
    CHECK(p.audio.fixed && strstr(stub_out, "volume: fixed at 100 % by LMS") != NULL);
    CHECK(stub_audio_bad == 0);
    teardown(&p);
}

/* Pause, volume change, resume: the rest of a block already sent goes again as it was converted (not scaled
 * twice); the block filled but not sent yet takes the new volume; muted = silence */
static void test_audio_volume_pause(void)
{
    struct player p;
    uint32_t i;
    int zero;

    fill(track_a, sizeof(track_a), 6);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    output_tick(&p, stub_now);
    stub_audio_abort_played = 1000;
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    feed_audg(1, 0x8000, 0x8000);
    drain(&p);
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    play_out(&p);
    to_device_vol(expect, track_a, 2 * BLOCK, 0, 0x10000);
    to_device_vol(expect + 2 * BLOCK, track_a + 2 * BLOCK, sizeof(track_a) - 2 * BLOCK, 0, 0x8000);
    CHECK(stub_audio_out_len == (int)sizeof(track_a) && !memcmp(stub_audio_out, expect, sizeof(track_a)));
    CHECK(stub_audio_bad == 0);
    teardown(&p);

    setup_audio(&p, 0);
    feed_audg(1, 0, 0);
    drain(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, 40000);
    drain_stream(&p);
    output_tick(&p, stub_now);
    play_out(&p);
    for (i = 0, zero = 1; i < (uint32_t)stub_audio_out_len; i++)
        zero &= stub_audio_out[i] == 0;
    CHECK(stub_audio_out_len == 40000 && zero);
    teardown(&p);
}

/* ---- the AHI unit's mode ---- */

/* ENV:Sys/ahi.prefs as saved by the AHI 6 preferences in WinUAE: the Music unit (255), then units
 * 0 (Paula 14-bit stereo++, 27429 Hz), 1 (Toccata stereo++, 44100 Hz), 2 (Paula 8-bit stereo++) and 3 (Toccata
 * plain stereo) */
static const uint8_t ahi_prefs[256] = {
    0x46, 0x4F, 0x52, 0x4D, 0x00, 0x00, 0x00, 0xF8, 0x50, 0x52, 0x45, 0x46, 0x50, 0x52, 0x48, 0x44,
    0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x48, 0x49, 0x47, 0x00, 0x00,
    0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE6, 0x66, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x41, 0x48, 0x49, 0x55, 0x00, 0x00, 0x00, 0x20,
    0xFF, 0x33, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x09, 0x00, 0x00, 0x1F, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x48, 0x49, 0x55, 0x00, 0x00, 0x00, 0x20, 0x00, 0x33, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01,
    0x00, 0x00, 0x6B, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x48, 0x49, 0x55, 0x00, 0x00, 0x00, 0x20,
    0x01, 0x33, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x01, 0x00, 0x00, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x41, 0x48, 0x49, 0x55, 0x00, 0x00, 0x00, 0x20, 0x02, 0x33, 0x00, 0x01, 0x00, 0x02, 0x00, 0x07,
    0x00, 0x00, 0x6B, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x48, 0x49, 0x55, 0x00, 0x00, 0x00, 0x20,
    0x03, 0x33, 0x00, 0x01, 0x00, 0x0A, 0x00, 0x03, 0x00, 0x00, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* The AHI unit's mode: read from its preferences, a warning for a mode that cannot play stereo right */
static void test_ahi_mode(void)
{
    uint32_t mode = 0, freq = 0;
    uint8_t *cut, odd[12 + 14 + 40];
    struct audio_mode m;

    CHECK(ahi_prefs_unit(ahi_prefs, sizeof(ahi_prefs), 0, &mode, &freq) && mode == 0x00020001 && freq == 27429);
    CHECK(ahi_prefs_unit(ahi_prefs, sizeof(ahi_prefs), 1, &mode, &freq) && mode == 0x000A0001 && freq == 44100);
    CHECK(ahi_prefs_unit(ahi_prefs, sizeof(ahi_prefs), 3, &mode, &freq) && mode == 0x000A0003);
    CHECK(ahi_prefs_unit(ahi_prefs, sizeof(ahi_prefs), 255, &mode, &freq) && mode == 0x000A0009 && freq == 8000);
    CHECK(!ahi_prefs_unit(ahi_prefs, sizeof(ahi_prefs), 4, &mode, &freq));      /* no such unit */
    /* The file cut short within unit 0's chunk, after its unit number but before its mode: an exact-size copy, so
     * that AddressSanitizer catches a read past it */
    cut = malloc(110);
    memcpy(cut, ahi_prefs, 110);
    CHECK(!ahi_prefs_unit(cut, 110, 0, &mode, &freq));
    free(cut);
    CHECK(!ahi_prefs_unit((const uint8_t *)"not an IFF file at all", 22, 0, &mode, &freq));
    /* A chunk of odd length (5 bytes, padded to 6) before unit 1's */
    memcpy(odd, ahi_prefs, 12);
    odd[7] = sizeof(odd) - 8;
    memcpy(odd + 12, "PRHD\0\0\0\5abcde\0", 14);
    memcpy(odd + 26, ahi_prefs + 136, 40);
    CHECK(ahi_prefs_unit(odd, sizeof(odd), 1, &mode, &freq) && mode == 0x000A0001 && freq == 44100);

    stub_reset();
    memset(&m, 0, sizeof(m));
    strcpy(m.name, "Paula:14 bit stereo++");
    m.stereo = m.panning = 1;
    CHECK(!audio_check_mode(0, &m) && stub_out[0] == '\0');                    /* stereo++: nothing to say */
    strcpy(m.name, "Toccata:16 bit stereo");
    m.panning = 0;
    CHECK(audio_check_mode(3, &m) && strstr(stub_out, "AHI unit 3 uses \"Toccata:16 bit stereo\"") &&
          strstr(stub_out, "one side only") && strstr(stub_out, "stereo++"));
    stub_reset();
    strcpy(m.name, "Paula:8 bit mono");
    m.stereo = 0;
    m.panning = 1;
    CHECK(audio_check_mode(2, &m) && strstr(stub_out, "a mono mode: the music will play in mono"));
}

void tests_audio(void)
{
    test_audio_blocks();
    test_audio_swap_mono();
    test_audio_pause();
    test_audio_pause_ahi4();
    test_audio_stop();
    test_audio_gapless();
    test_audio_wait_full_block();
    test_audio_starved();
    test_audio_errors();
    test_audio_formats();
    test_audio_24bit_pause();
    test_audio_gapless_24bit();
    test_audio_volume();
    test_audio_volume_pause();
    test_ahi_mode();
}
