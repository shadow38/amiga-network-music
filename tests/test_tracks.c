/*
 * test_tracks.c - unit tests of tracks.c (host build, see "make test"): the queue of tracks and their decoders,
 * with test-only decoders
 */
#include "test_core.h"

/* Audio output: blocks of decoded PCM; elapsed time and bytes played in bytes of decoded PCM */
static void test_decoder_audio(void)
{
    struct player p;
    uint32_t n = 40000;

    fill(track_a, sizeof(track_a), 11);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    use_widen(&p);
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    output_tick(&p, stub_now);
    CHECK(stub_ahi[0].len == BLOCK);                  /* 16384 bytes of input */
    finish(&p);
    CHECK(audio_played(&p, stub_now, 0) == BLOCK);
    CHECK(output_elapsed_ms(&p) == BLOCK * 1000 / PCM_BPS);
    play_out(&p);
    widen(decoded, track_a, n);
    to_device(expect, decoded, 2 * n, 0);
    CHECK(stub_ahi_out_len == (int)(2 * n) && !memcmp(stub_ahi_out, expect, 2 * n));
    CHECK(audio_played(&p, stub_now, 0) == 2 * n && p.out.state == OUTPUT_STOPPED);
    CHECK(stub_ahi_bad == 0);
    teardown(&p);
}

/* Audio output, gapless from a decoded track to a raw one: STMs once the decoded track has been played (its
 * decoded size, not its stream size), then both reach the device in order */
static void test_decoder_audio_gapless(void)
{
    struct player p;
    uint32_t na = 20000, nb = 30000;
    int guard, seen = 0;

    fill(track_a, sizeof(track_a), 12);
    fill(track_b, sizeof(track_b), 13);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    use_widen(&p);
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    clear_tx();
    output_tick(&p, stub_now);
    for (guard = 0; guard < 20 && p.out.state == OUTPUT_RUNNING; guard++) {
        finish(&p);
        if (!seen && count_in_trace("STAT:STMs") == 1) {
            seen = 1;
            CHECK(audio_played(&p, stub_now, 0) == 2 * na);
        }
    }
    CHECK(seen && count_in_trace("STAT:STMu") == 1);
    widen(decoded, track_a, na);
    to_device(expect, decoded, 2 * na, 0);
    to_device(expect + 2 * na, track_b, nb, 0);
    CHECK(stub_ahi_out_len == (int)(2 * na + nb) && !memcmp(stub_ahi_out, expect, 2 * na + nb));
    CHECK(stub_ahi_bad == 0);
    teardown(&p);
}

/* A pause in the middle of a block of decoded PCM: the bytes played are counted in decoded PCM */
static void test_decoder_audio_pause(void)
{
    struct player p;
    uint32_t n = 40000;

    fill(track_a, sizeof(track_a), 14);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    use_widen(&p);
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    output_tick(&p, stub_now);
    stub_ahi_abort_played = 1000;
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(audio_played(&p, stub_now, 0) == 1000 && output_elapsed_ms(&p) == 1000 * 1000 / PCM_BPS);
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    play_out(&p);
    widen(decoded, track_a, n);
    to_device(expect, decoded, 2 * n, 0);
    CHECK(stub_ahi_out_len == (int)(2 * n) && !memcmp(stub_ahi_out, expect, 2 * n));
    CHECK(stub_ahi_bad == 0);
    teardown(&p);
}

/* Virtual output and OUTPUT file: the decoded PCM is played at its own rate and written to the file; the next
 * (raw) track starts when the decoded one has been played */
static void test_decoder_virtual(void)
{
    struct player p;
    uint32_t na = PCM_BPS / 20, nb = PCM_BPS / 10;      /* 100 ms each, once decoded */

    fill(track_a, sizeof(track_a), 15);
    fill(track_b, sizeof(track_b), 16);
    setup_file(&p);
    open_stream(&p, '1', 1);
    use_widen(&p);
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    clear_tx();
    stub_now += 50;
    output_tick(&p, stub_now);
    CHECK(output_elapsed_ms(&p) == 50 && count_in_trace("STAT:STMs") == 0);
    stub_now += 50;
    output_tick(&p, stub_now);
    CHECK(count_in_trace("STAT:STMs") == 1 && output_elapsed_ms(&p) == 0);
    stub_now += 50;
    output_tick(&p, stub_now);
    CHECK(output_elapsed_ms(&p) == 50);
    play_for(&p, 300);
    CHECK(p.out.state == OUTPUT_STOPPED && count_in_trace("STAT:STMu") == 1);
    widen(decoded, track_a, na);
    memcpy(decoded + 2 * na, track_b, nb);
    CHECK(stub_file_len == (int)(2 * na + nb) && !memcmp(stub_file, decoded, 2 * na + nb));
    teardown(&p);
}

/* Read-ahead decoder, for the tests only: the widening decoder's output, but it first takes all the input there is
 * into a stage of its own, as mp3.c does. Less than a frame left at the track's end (as an MP3 file's ID3v1 tag) is
 * dropped by the call that ends its decoding, which writes nothing when it has no PCM left. Until then it has not
 * finished, though the reading has passed its track and the next track may wait in the buffer. */
static uint8_t ahead_stage[65536];
static uint32_t ahead_len, ahead_pos;
static int ahead_done;

static int ahead_start(struct decoder *d, const struct pcm_format *in)
{
    ahead_len = ahead_pos = 0;
    ahead_done = 0;
    return widen_start(d, in);
}

static uint32_t ahead_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                          uint32_t out_max)
{
    uint32_t n;

    if (avail > sizeof(ahead_stage) - ahead_len)
        avail = sizeof(ahead_stage) - ahead_len;    /* the tests stay below */
    ahead_len += ring_read(r, ahead_stage + ahead_len, avail);
    n = ahead_len - ahead_pos;
    if (n > out_max / 2)
        n = out_max / 2;
    n -= n % d->in_frame;
    widen(out, ahead_stage + ahead_pos, n);
    ahead_pos += n;
    if (complete && ahead_len - ahead_pos < d->in_frame && out_max - 2 * n >= 2 * d->in_frame)
        ahead_done = 1;                             /* room left: the end is known in this call */
    return 2 * n;
}

static uint32_t ahead_buffered(const struct decoder *d)
{
    return 2 * ((ahead_len - ahead_pos) - (ahead_len - ahead_pos) % d->in_frame);
}

static int ahead_finished(const struct decoder *d)
{
    (void)d;
    return ahead_done;
}

static const struct decoder_ops decoder_ahead = { "ahead", ahead_start, widen_stop, ahead_run,
                                                  widen_input_for, ahead_buffered, ahead_finished };

/* Track A through the read-ahead decoder: its first na bytes */
static void open_ahead(struct player *p, uint32_t na)
{
    fill(track_a, sizeof(track_a), 21);
    fill(track_b, sizeof(track_b), 22);
    open_stream(p, '1', 1);
    p->out.queue[p->out.queued - 1].ops = &decoder_ahead;
    stub_feed_stream(track_a, (int)na);
    drain_stream(p);
}

/* A's stream ends, then a raw track B (nb bytes) comes whole */
static void end_then_raw(struct player *p, uint32_t nb)
{
    stub_stream_end();
    stream_io(p, PLAT_READ);
    stub_reset();
    open_stream(p, '1', 1);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(p);
    stub_stream_end();
    stream_io(p, PLAT_READ);
    clear_tx();
}

/* The call that only ends a complete track's decoding, while the next track waits in the buffer, its stream ended:
 * not an empty output (no STMu), the next track plays (31bab90). A ends with a byte that is not a whole frame.
 * Audio output: A's PCM fills two blocks, then A's and B's streams end and the device plays both blocks before the
 * next tick (the main loop late), so it has nothing left at that call. */
static void test_decoder_end_call_audio(void)
{
    struct player p;
    uint32_t na = BLOCK + 1, nb = 30000;
    int guard;

    setup_audio(&p, 0);
    open_ahead(&p, na);
    output_tick(&p, stub_now);
    CHECK(stub_ahi_sends == 2 && audio_pending(&p) == 2 && !ahead_done);    /* more of A to come: waits */
    end_then_raw(&p, nb);
    stub_ahi_finish();
    stub_ahi_finish();
    stub_now += 400;
    output_tick(&p, stub_now);
    CHECK(audio_pending(&p) == 0 && ahead_done && count_in_trace("STAT:STMu") == 0);
    for (guard = 0; guard < 20 && p.out.state == OUTPUT_RUNNING; guard++)
        finish(&p);
    widen(decoded, track_a, na - 1);
    to_device(expect, decoded, 2 * (na - 1), 0);
    to_device(expect + 2 * (na - 1), track_b, nb, 0);
    CHECK(stub_ahi_out_len == (int)(2 * (na - 1) + nb) && !memcmp(stub_ahi_out, expect, 2 * (na - 1) + nb));
    CHECK(count_in_trace("STMo") == 0 && count_in_trace("STAT:STMs") == 1 && count_in_trace("STAT:STMu") == 1);
    CHECK(stub_ahi_bad == 0);
    teardown(&p);
}

/* The same on the virtual output and OUTPUT file: A's PCM fills the decoding buffer exactly (four times 4 KB), with
 * time left in the tick for the call that ends it */
static void test_decoder_end_call_virtual(void)
{
    struct player p;
    uint32_t na = 8193, nb = PCM_BPS / 10;

    setup_file(&p);
    open_ahead(&p, na);
    end_then_raw(&p, nb);
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(ahead_done && count_in_trace("STAT:STMu") == 0);
    play_for(&p, 400);
    CHECK(p.out.state == OUTPUT_STOPPED);
    CHECK(count_in_trace("STMo") == 0 && count_in_trace("STAT:STMs") == 1 && count_in_trace("STAT:STMu") == 1);
    widen(decoded, track_a, na - 1);
    memcpy(decoded + 2 * (na - 1), track_b, nb);
    CHECK(stub_file_len == (int)(2 * (na - 1) + nb) && !memcmp(stub_file, decoded, 2 * (na - 1) + nb));
    teardown(&p);
}

/* Decoder that cannot start (as without memory), for the tests only: counts its starts, and any later call */
static int nostart_starts, nostart_used;

static int nostart_start(struct decoder *d, const struct pcm_format *in)
{
    (void)d;
    (void)in;
    nostart_starts++;
    return 0;
}

static uint32_t nostart_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                            uint32_t out_max)
{
    (void)d;
    (void)r;
    (void)avail;
    (void)complete;
    (void)out;
    (void)out_max;
    nostart_used++;
    return 0;
}

static uint32_t nostart_input_for(const struct decoder *d, uint32_t out)
{
    (void)d;
    (void)out;
    nostart_used++;
    return 0;
}

static uint32_t nostart_buffered(const struct decoder *d)
{
    (void)d;
    nostart_used++;
    return 0;
}

static int nostart_finished(const struct decoder *d)
{
    (void)d;
    nostart_used++;
    return 1;
}

static const struct decoder_ops decoder_nostart = { "nostart", nostart_start, widen_stop, nostart_run,
                                                    nostart_input_for, nostart_buffered, nostart_finished };

/* A decoder that cannot start: its track is skipped as its data comes, the decoder is never used on it (it has no
 * state), and the next track plays */
static void test_decoder_no_start(void)
{
    struct player p;
    uint32_t na = PCM_BPS / 10, nb = PCM_BPS / 10;

    fill(track_a, sizeof(track_a), 17);
    fill(track_b, sizeof(track_b), 18);
    nostart_starts = nostart_used = 0;
    setup_file(&p);
    open_stream(&p, '1', 1);
    p.out.queue[p.out.queued - 1].ops = &decoder_nostart;
    stub_feed_stream(track_a, (int)(na / 2));
    drain_stream(&p);
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(nostart_starts == 1 && strstr(stub_out, "nostart: cannot decode this track, skipped") != NULL);
    clear_tx();
    stub_feed_stream(track_a + na / 2, (int)(na - na / 2));       /* more of the same track */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 200);
    CHECK(nostart_starts == 1 && nostart_used == 0 && strstr(stub_out, "cannot decode") == NULL);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 400);
    CHECK(nostart_used == 0 && stub_file_len == (int)nb && !memcmp(stub_file, track_b, nb));
    teardown(&p);
}

/* The same on the audio output, the skipped track coming in four reads with the output running between them (as
 * from the network): the skip decoder keeps the track's format, or the output's frame size would be 0 (a division
 * by zero) */
static void test_decoder_no_start_audio(void)
{
    struct player p;
    uint32_t na = PCM_BPS / 10, nb = PCM_BPS / 10, i;
    int guard;

    fill(track_a, sizeof(track_a), 19);
    fill(track_b, sizeof(track_b), 20);
    nostart_starts = nostart_used = 0;
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    p.out.queue[p.out.queued - 1].ops = &decoder_nostart;
    for (i = 0; i < 4; i++) {
        stub_feed_stream(track_a + i * (na / 4), (int)(na / 4));
        drain_stream(&p);
        stub_now += 100;
        output_tick(&p, stub_now);
    }
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(nostart_starts == 1 && nostart_used == 0 && stub_ahi_out_len == 0 &&
          strstr(stub_out, "nostart: cannot decode this track, skipped") != NULL);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    output_tick(&p, stub_now);
    for (guard = 0; guard < 20 && p.out.state == OUTPUT_RUNNING; guard++)
        finish(&p);
    to_device(expect, track_b, nb, 0);
    CHECK(nostart_used == 0 && stub_ahi_out_len == (int)nb && !memcmp(stub_ahi_out, expect, nb));
    CHECK(stub_ahi_bad == 0);
    teardown(&p);
}

void tests_tracks(void)
{
    test_decoder_audio();
    test_decoder_audio_gapless();
    test_decoder_audio_pause();
    test_decoder_virtual();
    test_decoder_end_call_audio();
    test_decoder_end_call_virtual();
    test_decoder_no_start();
    test_decoder_no_start_audio();
}
