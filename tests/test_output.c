/*
 * test_output.c - unit tests of output.c (host build, see "make test"): gapless playback, pause and stop on the
 * virtual output, the OUTPUT file
 */
#include "test_core.h"

/* The next track is appended while the previous one plays: STMs again at the boundary */
static void test_gapless(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    stub_feed_stream(pcm, sizeof(pcm));                 /* track A: 1 s */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_now += 300;
    output_tick(&p, stub_now);
    CHECK(output_elapsed_ms(&p) == 300);

    clear_tx();
    stub_reset();
    open_stream(&p, '1', 1);                            /* track B, received while A plays */
    CHECK(p.out.state == OUTPUT_RUNNING && p.out.queued == 1);
    stub_feed_stream(pcm, sizeof(pcm) / 2);             /* track B: 0.5 s */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    CHECK(output_elapsed_ms(&p) == 300);                /* still in track A */

    clear_tx();
    stub_now += 700;                                    /* end of track A */
    output_tick(&p, stub_now);
    CHECK(strstr(trace(), "STAT:STMs") != NULL && p.out.queued == 0);
    CHECK(output_elapsed_ms(&p) == 0);
    stub_now += 250;
    output_tick(&p, stub_now);
    CHECK(output_elapsed_ms(&p) == 250);                /* time of track B */

    clear_tx();
    stub_now += 300;
    output_tick(&p, stub_now);
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(strstr(trace(), "STAT:STMu") != NULL && p.out.state == OUTPUT_STOPPED);
    teardown(&p);
}

/* The next track has another format (22.05 kHz mono, big-endian): its time counts at its own rate */
static void test_gapless_format(void)
{
    struct player p;
    char fmt_b[7] = { '1', 'p', '1', '1', '1', '0', 1 };

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    stub_feed_stream(pcm, sizeof(pcm));                 /* track A: 1 s at 176400 bytes/s */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);

    stub_reset();
    feed_strm('s', fmt_b, 0, 9000, 0, request);         /* track B: 44100 bytes/s */
    drain(&p);
    stream_io(&p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    stub_feed_stream(pcm, 44100);                       /* 1 s of B */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);

    clear_tx();
    stub_now += 1000;                                   /* end of A */
    output_tick(&p, stub_now);
    CHECK(strstr(trace(), "STAT:STMs") != NULL);
    CHECK(p.out.cur.fmt.rate == 22050 && p.out.cur.fmt.channels == 1 && p.out.cur.fmt.big_endian);
    stub_now += 500;
    output_tick(&p, stub_now);
    CHECK(output_elapsed_ms(&p) == 500 && ring_used(&p.ring) == 22050);
    teardown(&p);
}

static void test_pause(void)
{
    struct player p;
    uint32_t used;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    stub_feed_stream(pcm, sizeof(pcm));
    drain_stream(&p);
    stub_now += 100;
    output_tick(&p, stub_now);

    clear_tx();
    feed_strm('p', NULL, 0, 0, 0, NULL);                /* interval 0: pause now */
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMp") && p.out.state == OUTPUT_PAUSED);
    used = ring_used(&p.ring);
    stub_now += 1000;
    output_tick(&p, stub_now);
    CHECK(ring_used(&p.ring) == used && output_elapsed_ms(&p) == 100);  /* time stands still */

    clear_tx();
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMr") && p.out.state == OUTPUT_RUNNING);
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(output_elapsed_ms(&p) == 200);
    teardown(&p);
}

static void test_stop(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    stub_feed_stream(pcm, 50000);
    drain_stream(&p);
    CHECK(p.out.state == OUTPUT_RUNNING && stub_stream_open);

    clear_tx();
    feed_strm('q', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf"));
    CHECK(!stub_stream_open && p.stream.state == STREAM_IDLE && stream_events(&p) == 0);
    CHECK(ring_used(&p.ring) == 0 && p.out.state == OUTPUT_STOPPED && p.out.queued == 0);
    teardown(&p);
}

/* ---- OUTPUT file ---- */

/* The file gets what the output plays: nothing while paused, the next track right after the previous one */
static void test_output_file(void)
{
    struct player p;

    fill(track_a, sizeof(track_a), 1);
    fill(track_b, sizeof(track_b), 2);
    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    CHECK(stub_file_len == 0);                          /* nothing played yet */
    play_for(&p, 300);
    CHECK(stub_file_len == PCM_BPS * 3 / 10 && !memcmp(stub_file, track_a, (size_t)stub_file_len));

    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    stub_now += 1000;
    output_tick(&p, stub_now);
    CHECK(stub_file_len == PCM_BPS * 3 / 10);           /* paused: nothing written */
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);

    stub_reset();
    open_stream(&p, '1', 1);                            /* track B, gapless */
    stub_feed_stream(track_b, sizeof(track_b));
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 3000);
    CHECK(p.out.state == OUTPUT_STOPPED);
    CHECK(stub_file_len == (int)(sizeof(track_a) + sizeof(track_b)));
    CHECK(!memcmp(stub_file, track_a, sizeof(track_a)));
    CHECK(!memcmp(stub_file + sizeof(track_a), track_b, sizeof(track_b)));

    clear_tx();
    output_file_close(&p);
    CHECK(!stub_file_open && p.out.file < 0);
    CHECK(strstr(stub_out, "output: 264600 bytes written to out.pcm") != NULL);
    teardown(&p);
}

/* Data flushed by a stop is never written */
static void test_output_file_stop(void)
{
    struct player p;

    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, 50000);
    drain_stream(&p);
    play_for(&p, 100);
    CHECK(stub_file_len == PCM_BPS / 10);
    feed_strm('q', NULL, 0, 0, 0, NULL);
    drain(&p);
    play_for(&p, 1000);
    CHECK(stub_file_len == PCM_BPS / 10 && !memcmp(stub_file, track_a, (size_t)stub_file_len));
    output_file_close(&p);
    teardown(&p);
}

/* File that cannot be created; write error (disk full): one message, file closed, playback goes on */
static void test_output_file_errors(void)
{
    struct player p;

    setup(&p, "Amiga");
    stub_file_fails = 1;
    CHECK(!output_file_open(&p, "out.pcm") && p.out.file < 0);
    CHECK(strstr(stub_out, "amisqueeze: cannot create out.pcm") != NULL);
    teardown(&p);

    setup_file(&p);
    stub_file_limit = 10000;
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));
    drain_stream(&p);
    clear_tx();
    play_for(&p, 100);
    CHECK(stub_file_len == 10000 && !stub_file_open && p.out.file < 0);
    CHECK(strstr(stub_out, "amisqueeze: cannot write to out.pcm") != NULL);
    CHECK(p.out.state == OUTPUT_RUNNING && output_elapsed_ms(&p) == 100);
    clear_tx();
    play_for(&p, 100);
    CHECK(stub_file_len == 10000 && strstr(stub_out, "cannot write") == NULL);  /* reported once */
    CHECK(output_elapsed_ms(&p) == 200);
    teardown(&p);
}

void tests_output(void)
{
    test_gapless();
    test_gapless_format();
    test_pause();
    test_stop();
    test_output_file();
    test_output_file_stop();
    test_output_file_errors();
}
