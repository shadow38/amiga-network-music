/*
 * test_stream.c - unit tests of stream.c (host build, see "make test"): the HTTP stream, strm f, a stream socket
 * reported readable with nothing to read
 */
#include "test_core.h"

static void test_http_header_end(void)
{
    CHECK(http_header_end(http_ok, (int)strlen(http_ok)) == (int)strlen(http_ok));
    CHECK(http_header_end("HTTP/1.0 200 OK\r\n\r\nDATA", 23) == 19);
    CHECK(http_header_end("HTTP/1.0 200 OK\n\nDATA", 21) == 17);
    CHECK(http_header_end("HTTP/1.0 200 OK\r\n\r", 18) == 0);   /* not complete yet */
    CHECK(http_header_end("", 0) == 0);
}

/* Response headers that fill their buffer without ending: the stream given up (DSCO 1), not read further */
static void test_headers_too_long(void)
{
    struct player p;
    static char headers[4200];

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    strm_pcm(&p, '1', 1);
    stream_io(&p, PLAT_WRITE);
    clear_tx();
    memset(headers, 'x', sizeof(headers));
    stub_feed_stream(headers, sizeof(headers));
    drain_stream(&p);
    CHECK(strstr(stub_out, "amisqueeze: stream: response headers too long") != NULL);
    CHECK(!strcmp(trace(), "DSCO:1"));
    CHECK(p.stream.state == STREAM_IDLE && !stub_stream_open);
    teardown(&p);
}

/* Response headers of 4000 bytes (a radio's long answer): passed on whole in RESP, the body read */
static void test_headers_long(void)
{
    static const char status[] = "HTTP/1.0 200 OK\r\nX-Pad: ";
    static char headers[4000];
    struct player p;
    int len = (int)sizeof(headers);

    memcpy(headers, status, strlen(status));
    memset(headers + strlen(status), 'x', len - strlen(status) - 4);
    memcpy(headers + len - 4, "\r\n\r\n", 4);
    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    strm_pcm(&p, '1', 1);
    stream_io(&p, PLAT_WRITE);
    clear_tx();
    stub_feed_stream(headers, len);
    stub_feed_stream(pcm, 100);
    drain_stream(&p);
    CHECK(!strcmp(trace(), "RESP"));
    CHECK(be32(stub_tx + 4) == (uint32_t)len && !memcmp(stub_tx + 8, headers, len));
    CHECK(p.stream.state == STREAM_BODY && ring_used(&p.ring) == 100);
    teardown(&p);
}

static void test_headers_limit(void)
{
    test_headers_too_long();
    test_headers_long();
}

/* VERBOSE shows the strm s request in ASCII: no CR, a control byte (0x1B, 0x7F) or a byte of UTF-8 as '?' (it could
 * act on the Amiga's console); past 64 bytes too */
static void test_strm_request_shown(void)
{
    static const char req[] = "GET /\033[2J\303\251\177_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa HTTP/1.0\r\n\r\n";
    char fmt[7] = { '1', 'p', '1', '3', '2', '1', 1 };
    struct player p;

    setup(&p, "Amiga");
    p.verbose = 1;
    CHECK(slim_connect(&p, 0));
    stub_out[0] = '\0';
    feed_strm('s', fmt, 0, 9000, 0, req);
    drain(&p);
    CHECK(strstr(stub_out, "GET /?[2J???_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa HTTP/1.0\n\n") != NULL);
    CHECK(strchr(stub_out, '\r') == NULL && strchr(stub_out, '\033') == NULL && strchr(stub_out, '\177') == NULL);
    teardown(&p);
}

/* A whole track: strm s, connection, headers, body, end of data, end of playback */
static void test_stream_sequence(void)
{
    struct player p;
    const uint8_t *st;
    uint32_t i;

    setup(&p, "Amiga");
    p.verbose = 1;
    CHECK(slim_connect(&p, 0));
    clear_tx();
    strm_pcm(&p, '1', 1);                               /* threshold: 1 KB */
    CHECK(!strcmp(trace(), "STAT:STMf"));
    CHECK(strstr(stub_out, "stream: format p, size 1, rate 3, channels 2, endian 1, autostart 1") != NULL);
    CHECK(strstr(stub_out, "stream: http://127.0.0.1:9000\n") != NULL);   /* address 0 = control connection */
    CHECK(p.stream.state == STREAM_CONNECTING && stream_events(&p) == PLAT_WRITE);

    clear_tx();
    stream_io(&p, PLAT_WRITE);
    CHECK(!strcmp(trace(), "STAT:STMc"));
    CHECK(stub_stream_tx_len == (int)strlen(request) && !memcmp(stub_stream_tx, request, strlen(request)));
    CHECK(p.stream.state == STREAM_HEADERS);

    for (i = 0; i < sizeof(pcm); i++)
        pcm[i] = (uint8_t)i;
    clear_tx();
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    stub_feed_stream(pcm, 500);                         /* body bytes in the same read as the headers */
    drain_stream(&p);
    CHECK(!strcmp(trace(), "RESP"));
    CHECK(be32(stub_tx + 4) == strlen(http_ok) && !memcmp(stub_tx + 8, http_ok, strlen(http_ok)));
    CHECK(p.stream.state == STREAM_BODY && ring_used(&p.ring) == 500);
    CHECK(p.out.state == OUTPUT_STOPPED);               /* below the threshold */

    clear_tx();
    stub_feed_stream(pcm + 500, 1000);
    drain_stream(&p);
    CHECK(!strcmp(trace(), "STAT:STMs"));               /* threshold reached: the output starts */
    CHECK(p.out.state == OUTPUT_RUNNING);
    CHECK(p.out.cur.fmt.rate == 44100 && p.out.cur.fmt.channels == 2 && p.out.cur.fmt.sample_bytes == 2 &&
          !p.out.cur.fmt.big_endian);

    stub_feed_stream(pcm + 1500, sizeof(pcm) - 1500);
    drain_stream(&p);
    CHECK(p.stream.bytes == sizeof(pcm) && ring_used(&p.ring) == sizeof(pcm));

    clear_tx();
    stub_now += 500;
    output_tick(&p, stub_now);
    CHECK(output_elapsed_ms(&p) == 500 && ring_used(&p.ring) == sizeof(pcm) / 2);
    CHECK(stub_tx_len == 0);                            /* no STMt before one second */

    stub_stream_end();
    stream_io(&p, PLAT_READ);
    CHECK(!strcmp(trace(), "DSCO:0 STAT:STMd"));        /* end of the data: next track welcome */
    CHECK(p.stream.state == STREAM_DONE && !stub_stream_open);

    clear_tx();
    stub_now += 500;
    output_tick(&p, stub_now);
    CHECK(!strcmp(trace(), "STAT:STMt"));               /* one second since the start */
    st = last_stat("STMt");
    CHECK(st && be32(st + STAT_ELAPSED_MS) == 1000 && be32(st + STAT_ELAPSED_S) == 1);
    CHECK(st && be32(st + STAT_BYTES_LOW) == sizeof(pcm));
    CHECK(st && be32(st + STAT_BUF_FULL) == 0);

    clear_tx();
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(!strcmp(trace(), "STAT:STMu"));               /* everything played */
    CHECK(p.out.state == OUTPUT_STOPPED);
    teardown(&p);
}

/* While the body streams, the socket is read only once 8 KB of the buffer are free: an output that frees a few
 * bytes at a time does not make a read each time */
static void test_read_min_space(void)
{
    struct player p;
    uint8_t *w;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    strm_pcm(&p, '1', 1);
    stream_io(&p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    drain_stream(&p);
    CHECK(p.stream.state == STREAM_BODY && stream_events(&p) == PLAT_READ);
    while (ring_space(&p.ring) > 0)
        ring_commit(&p.ring, ring_write_space(&p.ring, &w));   /* the buffer full, as the server keeps it */
    CHECK(stream_events(&p) == 0);
    ring_consume(&p.ring, 8 * 1024 - 1);
    CHECK(stream_events(&p) == 0);                      /* 1 byte short of 8 KB free */
    ring_consume(&p.ring, 1);
    CHECK(stream_events(&p) == PLAT_READ);
    teardown(&p);
}

static void test_connect_failure(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    stub_connect_fails = 1;
    strm_pcm(&p, '1', 1);
    clear_tx();
    stream_io(&p, PLAT_WRITE);
    CHECK(!strcmp(trace(), "DSCO:3"));                  /* unreachable */
    CHECK(p.stream.state == STREAM_IDLE && p.out.queued == 0 && !stub_stream_open);
    teardown(&p);
}

static void test_not_pcm(void)
{
    struct player p;
    char fmt[7] = { '1', 'a', '?', '?', '?', '?', 10 };     /* AAC: not decoded */

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    feed_strm('s', fmt, 0, 9000, 0, request);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf STAT:STMc STAT:STMn"));     /* STMc first: LMS ignores STMn before it */
    CHECK(p.stream.state == STREAM_IDLE && !stub_stream_open);
    teardown(&p);
}

/* A strm s whose HTTP request is longer than the 1 KB kept for it: refused, no stream opened (copied, it would
 * overwrite the fields after the request, which the sanitizers do not see) */
static void feed_strm_too_long(void)
{
    static uint8_t b[24 + 1025];

    memset(b, 'x', sizeof(b));
    memset(b, 0, 24);
    b[0] = 's';
    memcpy(b + 1, "1p1321\001", 7);
    b[18] = 9000 >> 8;
    b[19] = 9000 & 0xff;
    stub_feed_packet("strm", b, (int)sizeof(b));
}

/* ...and reported to the server as a track it cannot play (STMc + STMn), so that LMS moves on to the next one */
static void test_request_too_long(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    feed_strm_too_long();
    drain(&p);
    CHECK(strstr(stub_out, "stream: request too long (1025 bytes)") != NULL);
    CHECK(!strcmp(trace(), "STAT:STMf STAT:STMc STAT:STMn"));
    CHECK(p.stream.state == STREAM_IDLE && !stub_stream_open);
    teardown(&p);
}

/* autostart 0: STMl once buffered, playback starts on strm u */
static void test_autostart_0(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '0', 1);
    clear_tx();
    stub_feed_stream(pcm, 2000);
    drain_stream(&p);
    CHECK(!strcmp(trace(), "STAT:STMl") && p.out.state == OUTPUT_LOADED);

    clear_tx();
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    output_tick(&p, stub_now);
    CHECK(!strcmp(trace(), "STAT:STMr STAT:STMs") && p.out.state == OUTPUT_RUNNING);
    teardown(&p);
}

/* ---- strm f (flush) ---- */

static uint8_t track_c[PCM_BPS / 2];

/* strm f while the next track streams (LMS: the next track removed, moved or reshuffled): that track is dropped,
 * the one playing goes on, and the next strm s follows it. A goes through the widening decoder: a stream position
 * taken for an output one (or the reverse) would show. */
static void test_flush(void)
{
    struct player p;
    uint32_t na = PCM_BPS / 10, nb = PCM_BPS / 4, nc = PCM_BPS / 10;   /* A: 200 ms once decoded; C: 100 ms */

    fill(track_a, sizeof(track_a), 21);
    fill(track_b, sizeof(track_b), 22);
    fill(track_c, sizeof(track_c), 23);
    setup_file(&p);
    open_stream(&p, '1', 1);
    use_widen(&p);
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_now += 50;
    output_tick(&p, stub_now);
    stub_reset();
    open_stream(&p, '1', 1);                            /* B, still streaming */
    use_widen(&p);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);

    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf"));               /* no DSCO nor STMd for the dropped stream */
    CHECK(!stub_stream_open && p.stream.state == STREAM_IDLE && stream_events(&p) == 0);
    CHECK(p.out.state == OUTPUT_RUNNING && p.out.queued == 0 && output_elapsed_ms(&p) == 50);
    CHECK(ring_used(&p.ring) == na * 3 / 4);            /* A's 150 ms left, nothing of B */

    stub_reset();
    open_stream(&p, '1', 1);                            /* C, the new next track: raw PCM */
    stub_feed_stream(track_c, (int)nc);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    clear_tx();
    stub_now += 140;
    output_tick(&p, stub_now);
    CHECK(count_in_trace("STAT:STMs") == 0 && output_elapsed_ms(&p) == 190);  /* still A */
    stub_now += 20;
    output_tick(&p, stub_now);
    CHECK(count_in_trace("STAT:STMs") == 1 && output_elapsed_ms(&p) == 10);   /* C, timed from its start */
    play_for(&p, 300);
    CHECK(p.out.state == OUTPUT_STOPPED && count_in_trace("STAT:STMu") == 1 && count_in_trace("STAT:STMs") == 1);
    widen(decoded, track_a, na);
    memcpy(decoded + 2 * na, track_c, nc);
    CHECK(stub_file_len == (int)(2 * na + nc) && !memcmp(stub_file, decoded, 2 * na + nc));
    teardown(&p);
}

/* strm f while paused, the next track received whole: still paused where it was; A then plays to its end */
static void test_flush_paused(void)
{
    struct player p;
    uint32_t na = PCM_BPS / 5, nb = PCM_BPS / 10;       /* A: 200 ms */

    fill(track_a, sizeof(track_a), 24);
    fill(track_b, sizeof(track_b), 25);
    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_now += 100;
    output_tick(&p, stub_now);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);

    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf"));
    CHECK(p.out.state == OUTPUT_PAUSED && p.out.queued == 0 && output_elapsed_ms(&p) == 100);
    CHECK(ring_used(&p.ring) == na / 2);
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    play_for(&p, 300);
    CHECK(p.out.state == OUTPUT_STOPPED && count_in_trace("STAT:STMu") == 1 && count_in_trace("STAT:STMs") == 0);
    CHECK(stub_file_len == (int)na && !memcmp(stub_file, track_a, na));
    teardown(&p);
}

/* Plays the blocks queued to the end; STMs once, when the bytes played reach `boundary` */
static void play_out_boundary(struct player *p, uint64_t boundary)
{
    int guard, seen = 0;

    clear_tx();
    for (guard = 0; guard < 20 && p->out.state == OUTPUT_RUNNING; guard++) {
        finish(p);
        if (!seen && count_in_trace("STAT:STMs") == 1) {
            seen = 1;
            CHECK(audio_played(p, stub_now, 0) == boundary && output_elapsed_ms(p) == 0);
        }
    }
    CHECK(seen && count_in_trace("STAT:STMs") == 1 && count_in_trace("STAT:STMu") == 1);
}

/* Audio output, strm f before the decoding reaches the next track: none of it reaches the device, the new next
 * track follows A */
static void test_flush_audio(void)
{
    struct player p;
    uint32_t nb = 30000, nc = 20000;

    fill(track_a, sizeof(track_a), 26);
    fill(track_b, sizeof(track_b), 27);
    fill(track_c, sizeof(track_c), 28);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, sizeof(track_a));          /* A: 1 s */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    output_tick(&p, stub_now);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, (int)nb);                 /* B, still streaming */
    drain_stream(&p);

    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf") && !stub_stream_open);
    CHECK(p.out.state == OUTPUT_RUNNING && p.out.queued == 0 && audio_pending(&p) == AUDIO_BLOCKS);
    CHECK(ring_used(&p.ring) == sizeof(track_a) - AUDIO_BLOCKS * BLOCK);

    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_c, (int)nc);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_out_boundary(&p, sizeof(track_a));
    to_device(expect, track_a, sizeof(track_a), 0);
    to_device(expect + sizeof(track_a), track_c, nc, 0);
    CHECK(stub_ahi_out_len == (int)(sizeof(track_a) + nc) && !memcmp(stub_ahi_out, expect, sizeof(track_a) + nc));
    CHECK(stub_ahi_bad == 0);
    teardown(&p);
}

/* Audio output, strm f once the decoding has reached the next track (in the last blocks of A): the block of it
 * already filled plays as the end of A, without STMs; the rest of it is dropped, the new next track follows */
static void test_flush_audio_reached(void)
{
    struct player p;
    uint32_t na = 40000, nb = 60000, nc = 20000;

    fill(track_a, sizeof(track_a), 29);
    fill(track_b, sizeof(track_b), 30);
    fill(track_c, sizeof(track_c), 31);
    setup_audio(&p, 0);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, (int)na);                 /* A: 32768 + 7232 bytes */
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_b, (int)nb);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    output_tick(&p, stub_now);
    CHECK(audio_pending(&p) == AUDIO_BLOCKS && ring_used(&p.ring) == nb - BLOCK);   /* B's first block filled */

    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf"));
    CHECK(p.out.state == OUTPUT_RUNNING && p.out.queued == 0 && ring_used(&p.ring) == 0 && p.out.dec.ops == NULL);

    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_c, (int)nc);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_out_boundary(&p, na + BLOCK);
    to_device(expect, track_a, na, 0);
    to_device(expect + na, track_b, BLOCK, 0);
    to_device(expect + na + BLOCK, track_c, nc, 0);
    CHECK(stub_ahi_out_len == (int)(na + BLOCK + nc) && !memcmp(stub_ahi_out, expect, na + BLOCK + nc));
    CHECK(stub_ahi_bad == 0);
    teardown(&p);
}

/* strm f while the track playing is still streaming (no next track; LMS does not send it then): the stream is
 * closed without DSCO, what was received plays to its end */
static void test_flush_current(void)
{
    struct player p;
    uint32_t n = 50000;

    fill(track_a, sizeof(track_a), 32);
    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    play_for(&p, 100);
    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf") && !stub_stream_open && p.stream.state == STREAM_IDLE);
    CHECK(p.out.state == OUTPUT_RUNNING && ring_used(&p.ring) == n - PCM_BPS / 10);
    play_for(&p, 1000);
    CHECK(p.out.state == OUTPUT_STOPPED && !strcmp(trace(), "STAT:STMf STAT:STMu"));
    CHECK(stub_file_len == (int)n && !memcmp(stub_file, track_a, n));
    teardown(&p);
}

/* The next track's request too long while a track plays (its stream done): refused, the track playing untouched */
static void test_request_too_long_playing(void)
{
    struct player p;
    uint32_t n = 50000;

    fill(track_a, sizeof(track_a), 33);
    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 100);
    clear_tx();
    feed_strm_too_long();
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf STAT:STMc STAT:STMn"));
    CHECK(p.out.state == OUTPUT_RUNNING && p.out.queued == 0 && ring_used(&p.ring) == n - PCM_BPS / 10);
    play_for(&p, 1000);
    CHECK(p.out.state == OUTPUT_STOPPED && !strcmp(trace(), "STAT:STMf STAT:STMc STAT:STMn STAT:STMu"));
    CHECK(stub_file_len == (int)n && !memcmp(stub_file, track_a, n));
    teardown(&p);
}

/* A track queued after the one playing: strm s, its 20000 bytes, end of its stream */
#define QUEUED_LEN 20000
static uint8_t queued_tracks[MAX_TRACKS * QUEUED_LEN];

static void queue_track(struct player *p, int i)
{
    open_stream(p, '1', 1);
    stub_feed_stream(queued_tracks + i * QUEUED_LEN, QUEUED_LEN);
    drain_stream(p);
    stub_stream_end();
    stream_io(p, PLAT_READ);
}

/* A strm s while MAX_TRACKS tracks wait after the one playing: refused, the buffer untouched; each track waiting
 * then plays and starts with its STMs */
static void test_queue_full(void)
{
    struct player p;
    uint32_t n = 50000, used;
    const char *t;
    int i, starts = 0;

    fill(track_a, sizeof(track_a), 34);
    fill(queued_tracks, sizeof(queued_tracks), 35);
    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, (int)n);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 100);
    for (i = 0; i < MAX_TRACKS; i++)
        queue_track(&p, i);
    CHECK(p.out.queued == MAX_TRACKS);
    used = ring_used(&p.ring);
    clear_tx();
    strm_pcm(&p, '1', 1);
    CHECK(!strcmp(trace(), "STAT:STMf STAT:STMc STAT:STMn") && !stub_stream_open);
    CHECK(strstr(stub_out, "amisqueeze: stream: 16 tracks already waiting, this one skipped\n") != NULL);
    CHECK(p.out.queued == MAX_TRACKS && ring_used(&p.ring) == used);
    clear_tx();
    play_for(&p, 3000);
    for (t = trace(); (t = strstr(t, "STAT:STMs")) != NULL; t++)
        starts++;
    CHECK(p.out.state == OUTPUT_STOPPED && starts == MAX_TRACKS && strstr(trace(), "STAT:STMu") != NULL);
    CHECK(stub_file_len == (int)(n + sizeof(queued_tracks)) && !memcmp(stub_file, track_a, n) &&
          !memcmp(stub_file + n, queued_tracks, sizeof(queued_tracks)));
    teardown(&p);
}

/* A plays, B is received whole, C's strm s is then refused (autostart 3, 404: nothing played) or fails (DSCO 3):
 * C's track is already gone, so a strm f drops nothing, and B still plays after A, with its STMs */
static void flush_after_gone(int fail)
{
    struct player p;
    uint32_t na = 50000;

    fill(track_a, sizeof(track_a), 36);
    fill(queued_tracks, QUEUED_LEN, 37);
    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 100);
    stub_reset();
    queue_track(&p, 0);
    stub_reset();
    if (fail) {
        stub_connect_fails = 1;
        strm_pcm(&p, '1', 1);
        clear_tx();
        stream_io(&p, PLAT_WRITE);
        stub_connect_fails = 0;
        CHECK(!strcmp(trace(), "DSCO:3"));
    } else {
        answer_stream(&p, '3', 1, http_404, (int)strlen(http_404));
        CHECK(!strcmp(trace(), "RESP") && p.stream.state == STREAM_REFUSED);
    }
    CHECK(p.out.queued == 1 && !p.stream.in_queue);
    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf") && p.out.state == OUTPUT_RUNNING && p.out.queued == 1);
    clear_tx();
    play_for(&p, 1000);
    CHECK(p.out.state == OUTPUT_STOPPED && count_in_trace("STAT:STMs") == 1 && count_in_trace("STAT:STMu") == 1);
    CHECK(stub_file_len == (int)(na + QUEUED_LEN) && !memcmp(stub_file, track_a, na) &&
          !memcmp(stub_file + na, queued_tracks, QUEUED_LEN));
    teardown(&p);
}

/* A plays; B's strm s is replaced by C's before any data of B: still connecting, or waiting for its cont (Music
 * Assistant re-queuing). B's track goes with its stream: one STMs (C's) at the end of A, and C plays. */
static void replaced_before_data(int connected)
{
    struct player p;
    uint32_t na = 50000;

    fill(track_a, sizeof(track_a), 38);
    fill(queued_tracks, QUEUED_LEN, 39);
    setup_file(&p);
    open_stream(&p, '1', 1);
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 100);
    stub_reset();
    if (connected) {
        answer_stream(&p, '3', 1, http_ok, (int)strlen(http_ok));
        CHECK(p.stream.state == STREAM_CONT);
    } else {
        strm_pcm(&p, '1', 1);
        CHECK(p.stream.state == STREAM_CONNECTING);
    }
    CHECK(p.out.queued == 1);
    stub_reset();
    queue_track(&p, 0);
    CHECK(!strstr(trace(), "DSCO:1") && !strstr(trace(), "DSCO:2") && !strstr(trace(), "DSCO:3"));
    CHECK(p.out.queued == 1);
    clear_tx();
    play_for(&p, 1000);
    CHECK(p.out.state == OUTPUT_STOPPED && count_in_trace("STAT:STMs") == 1 && count_in_trace("STAT:STMu") == 1);
    CHECK(stub_file_len == (int)(na + QUEUED_LEN) && !memcmp(stub_file, track_a, na) &&
          !memcmp(stub_file + na, queued_tracks, QUEUED_LEN));
    teardown(&p);
}

/* The next track refused, failed or replaced while one plays */
static void test_refused_playing(void)
{
    test_request_too_long_playing();
    test_queue_full();
    flush_after_gone(0);
    flush_after_gone(1);
    replaced_before_data(0);
    replaced_before_data(1);
}

/* strm f with nothing playing yet (the only track is the one dropped), and strm f then q (Music Assistant, before
 * each new track): a stop */
static void test_flush_stop(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 255);
    stub_feed_stream(pcm, 50000);                       /* below the threshold: not started */
    drain_stream(&p);
    CHECK(p.out.state == OUTPUT_STOPPED && p.out.queued == 1);
    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf") && !stub_stream_open && p.stream.state == STREAM_IDLE);
    CHECK(p.out.state == OUTPUT_STOPPED && p.out.queued == 0 && ring_used(&p.ring) == 0);
    teardown(&p);

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    stub_feed_stream(pcm, 50000);
    drain_stream(&p);
    CHECK(p.out.state == OUTPUT_RUNNING);
    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    feed_strm('q', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf STAT:STMf") && !stub_stream_open);
    CHECK(p.out.state == OUTPUT_STOPPED && p.out.queued == 0 && ring_used(&p.ring) == 0);
    teardown(&p);
}

/* strm f with the only track loaded, waiting for strm u (autostart 0): a stop, strm u then starts nothing; the next
 * track plays from its start */
static void test_flush_loaded(void)
{
    struct player p;
    uint32_t na = 20000, nc = PCM_BPS / 10;

    fill(track_a, sizeof(track_a), 34);
    fill(track_c, sizeof(track_c), 35);
    setup_file(&p);
    open_stream(&p, '0', 1);
    stub_feed_stream(track_a, (int)na);
    drain_stream(&p);
    CHECK(p.out.state == OUTPUT_LOADED);
    clear_tx();
    feed_strm('f', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf") && !stub_stream_open && p.stream.state == STREAM_IDLE);
    CHECK(p.out.state == OUTPUT_STOPPED && p.out.queued == 0 && ring_used(&p.ring) == 0);
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    output_tick(&p, stub_now);
    CHECK(p.out.state == OUTPUT_STOPPED && count_in_trace("STAT:STMs") == 0);

    stub_reset();
    open_stream(&p, '1', 1);
    stub_feed_stream(track_c, (int)nc);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    play_for(&p, 1000);
    CHECK(p.out.state == OUTPUT_STOPPED && count_in_trace("STAT:STMs") == 1 && count_in_trace("STAT:STMu") == 1);
    CHECK(stub_file_len == (int)nc && !memcmp(stub_file, track_c, nc));
    teardown(&p);
}

/* ---- a stream socket reported readable, nothing to read ---- */

/* The stream reported readable with nothing to read: no error, no DSCO, left out of the waits
 * for AGAIN_WAIT_MS, said once with VERBOSE; then read as usual */
static void test_stream_again(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    CHECK(p.stream.state == STREAM_BODY && stream_events(&p) == PLAT_READ);
    clear_tx();
    p.verbose = 1;
    stream_io(&p, PLAT_READ);                           /* nothing to read */
    CHECK(p.stream.state == STREAM_BODY && stub_tx_len == 0 && stream_events(&p) == 0);
    CHECK(!strcmp(stub_out, "stream: reported readable, nothing to read yet\n"));
    stub_now += AGAIN_WAIT_MS - 1;
    CHECK(stream_events(&p) == 0);
    stub_now++;
    CHECK(stream_events(&p) == PLAT_READ);
    stub_feed_stream(pcm, 1000);
    drain_stream(&p);
    CHECK(p.stream.bytes == 1000 && p.stream.state == STREAM_BODY && stream_events(&p) == PLAT_READ);
    teardown(&p);
}

/* Reported readable with nothing to read in a control packet's body, then in the stream's response headers: no
 * error, the packet and the headers read once they come */
static void test_again_body_headers(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    p.verbose = 1;
    clear_tx();
    feed_strm('t', NULL, 0, 0, 0, NULL);
    CHECK(slim_receive(&p) == 1);                       /* its length */
    stub_again = 1;
    CHECK(slim_receive(&p) == -1 && strstr(stub_out, "control: reported readable, nothing to read yet\n"));
    CHECK(slim_receive(&p) == 1 && !strcmp(trace(), "STAT:STMt"));

    strm_pcm(&p, '1', 1);
    stream_io(&p, PLAT_WRITE);                          /* connected, the request sent */
    CHECK(p.stream.state == STREAM_HEADERS);
    stub_out[0] = '\0';
    stream_io(&p, PLAT_READ);                           /* nothing to read */
    CHECK(p.stream.state == STREAM_HEADERS && stream_events(&p) == 0);
    CHECK(!strcmp(stub_out, "stream: reported readable, nothing to read yet\n"));
    stub_now += AGAIN_WAIT_MS;
    clear_tx();
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    drain_stream(&p);
    CHECK(p.stream.state == STREAM_BODY && !strcmp(trace(), "RESP"));
    teardown(&p);
}

static int count_in_out(const char *s)
{
    const char *m = stub_out;
    int n = 0;

    while ((m = strstr(m, s)) != NULL) {
        n++;
        m += strlen(s);
    }
    return n;
}

/* "Nothing to read yet" is said once per connection and once per stream (VERBOSE); a new stream is not left out of
 * the waits because the one it replaces was */
static void test_again_said_each(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    p.verbose = 1;
    stub_again = 2;
    CHECK(slim_receive(&p) == -1 && slim_receive(&p) == -1);
    slim_disconnect(&p);
    CHECK(slim_connect(&p, 1));
    stub_again = 1;
    CHECK(slim_receive(&p) == -1 && count_in_out("control: reported readable, nothing to read yet\n") == 2);

    open_stream(&p, '1', 1);
    stream_io(&p, PLAT_READ);
    stream_io(&p, PLAT_READ);
    CHECK(stream_events(&p) == 0);                      /* left out of the waits */
    strm_pcm(&p, '1', 1);                               /* the next track, its stream replacing that one */
    CHECK(stream_events(&p) == PLAT_WRITE);
    stream_io(&p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    drain_stream(&p);
    stream_io(&p, PLAT_READ);
    CHECK(p.stream.state == STREAM_BODY && count_in_out("stream: reported readable, nothing to read yet\n") == 2);
    teardown(&p);
}

void tests_stream(void)
{
    test_http_header_end();
    test_headers_limit();
    test_strm_request_shown();
    test_stream_sequence();
    test_read_min_space();
    test_connect_failure();
    test_not_pcm();
    test_request_too_long();
    test_autostart_0();
    test_flush();
    test_flush_paused();
    test_flush_audio();
    test_flush_audio_reached();
    test_flush_current();
    test_refused_playing();
    test_flush_stop();
    test_flush_loaded();
    test_stream_again();
    test_again_body_headers();
    test_again_said_each();
}
