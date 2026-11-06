/*
 * test_icy.c - unit tests of icy.c and stream.c (host build, see "make test"): internet radios (cont, the HTTP
 * status, metadata blocks)
 */
#include "test_core.h"

/* cont: the blocks' interval, the loop byte, no GUID */
static void feed_cont(uint32_t interval, int loop)
{
    uint8_t b[7] = { 0 };

    set32(b, interval);
    b[4] = (uint8_t)loop;
    stub_feed_packet("cont", b, sizeof(b));
}

/* The bodies of the META packets sent, back to back in buf; returns how many */
static int metas(uint8_t *buf, uint32_t *len)
{
    int pos = 0, count = 0;

    *len = 0;
    while (pos + 8 <= stub_tx_len) {
        uint32_t n = be32(stub_tx + pos + 4);
        if (!memcmp(stub_tx + pos, "META", 4)) {
            memcpy(buf + *len, stub_tx + pos + 8, n);
            *len += n;
            count++;
        }
        pos += 8 + (int)n;
    }
    return count;
}

static void test_http_status(void)
{
#define STATUS(s) http_status(s, (int)strlen(s))
    CHECK(STATUS("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n\r\n") == 200);
    CHECK(STATUS("ICY 200 OK\r\nicy-metaint:16000\r\n\r\n") == 200);
    CHECK(STATUS("HTTP/1.1 206 Partial Content\r\n\r\n") == 206);   /* Music Assistant asks for a range */
    CHECK(STATUS("HTTP/1.1 302 Found\r\nLocation: http://x/\r\n\r\n") == 302);
    CHECK(STATUS(http_404) == 404);
    CHECK(STATUS("HTTP/1.0 200\n\n") == 200);                        /* no reason phrase */
    CHECK(STATUS("HTTP/1.0 200") == 200);
    CHECK(STATUS("HTTP/1.0 20 OK\r\n\r\n") == -1);                   /* two digits */
    CHECK(STATUS("HTTP/1.0 2000 OK\r\n\r\n") == -1);                 /* four */
    CHECK(STATUS("HTTP/1.0200 OK\r\n\r\n") == -1);                   /* no space */
    CHECK(STATUS("HTTP/1.0\r\n200 OK\r\n\r\n") == -1);               /* not on the first line */
    CHECK(STATUS("\r\nHTTP/1.0 200 OK\r\n\r\n") == -1);              /* an empty first line */
    CHECK(STATUS(" 200 OK\r\n\r\n") == -1);                          /* no protocol */
    CHECK(STATUS("") == -1);
#undef STATUS
}

static void test_icy_title(void)
{
    static const uint8_t padded[48] = "StreamTitle='Artist - Song';StreamUrl='';";   /* NULs to the end */
    char t[64], small[5];

#define TITLE(s, buf) icy_title((const uint8_t *)(s), (uint32_t)strlen(s), buf, sizeof(buf))
    CHECK(icy_title(padded, sizeof(padded), t, sizeof(t)) && !strcmp(t, "Artist - Song"));
    CHECK(TITLE("StreamTitle='Guns N' Roses - Yesterdays';", t) && !strcmp(t, "Guns N' Roses - Yesterdays"));
    CHECK(TITLE("StreamUrl='http://x/';StreamTitle='Last';", t) && !strcmp(t, "Last"));
    CHECK(TITLE("StreamTitle='At the end'", t) && !strcmp(t, "At the end"));
    CHECK(TITLE("StreamTitle='';", t) && !strcmp(t, ""));
    CHECK(TITLE("StreamTitle='Caf\xc3\xa9\r\n';", t) && !strcmp(t, "Caf????"));     /* UTF-8, a line end */
    CHECK(TITLE("StreamTitle='Artist - Song';", small) && !strcmp(small, "Arti"));
    CHECK(!TITLE("StreamUrl='';", t));
    CHECK(!TITLE("StreamTitle=", t));
    CHECK(!icy_title((const uint8_t *)"StreamUrl='';\0StreamTitle='x';", 30, t, sizeof(t)));  /* after a NUL */
#undef TITLE
}

/* A radio's stream: pcm[] as the sound, a block every `interval` bytes of it, of 0, 1, 0, 255 (the longest) and 2
 * times 16 bytes, then `interval` bytes of sound again. Returns its length; the sound's in *sound, the blocks that
 * are not empty back to back in blocks (*blocks_len). */
static uint32_t icy_stream(uint32_t interval, uint8_t *buf, uint32_t *sound, uint8_t *blocks, uint32_t *blocks_len)
{
    static const uint8_t lengths[] = { 0, 1, 0, 255, 2 };
    uint32_t len = 0, i, j;

    *sound = 0;
    *blocks_len = 0;
    for (i = 0; i <= sizeof(lengths); i++) {
        memcpy(buf + len, pcm + *sound, interval);
        len += interval;
        *sound += interval;
        if (i == sizeof(lengths))
            break;
        buf[len++] = lengths[i];
        for (j = 0; j < lengths[i] * 16U; j++)  /* a title, then bytes of every value but NUL */
            blocks[*blocks_len + j] = (uint8_t)(j < 13 ? (uint32_t)"StreamTitle='"[j] : 1 + (i + j) % 255);
        memcpy(buf + len, blocks + *blocks_len, lengths[i] * 16U);
        len += lengths[i] * 16U;
        *blocks_len += lengths[i] * 16U;
    }
    return len;
}

/* Blocks at every cut, through stream.c: each recv limited to `chunk` bytes, so that a block, its length byte or
 * the sound around them falls across two reads, at the end of one, or among the bytes held with the headers until
 * cont. The ring receives the sound alone, byte for byte; the server, each block that is not empty, as received. */
static void test_icy_cuts(void)
{
    static const uint32_t intervals[] = { 1, 16, 1000 };
    static const int chunks[] = { 0, 999, 1000, 1001, 1017, 2047, 4080, 4096 };
    static const char headers[] = "ICY 200 OK\r\nicy-metaint:16\r\n\r\n";
    static uint8_t stream[16384], blocks[8192], got[8192];
    struct player p;
    unsigned i;
    int c;

    for (i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++)
        for (c = 1; c <= 64 + (int)(sizeof(chunks) / sizeof(chunks[0])); c++) {
            int chunk = c <= 64 ? c : chunks[c - 65], guard = 100000, ok;
            uint32_t sound, blocks_len, len = icy_stream(intervals[i], stream, &sound, blocks, &blocks_len), n;

            setup(&p, "Amiga");
            CHECK(slim_connect(&p, 0));
            stub_chunk = chunk;
            strm_pcm(&p, '2', 255);
            stream_io(&p, PLAT_WRITE);
            stub_feed_stream(headers, (int)strlen(headers));
            stub_feed_stream(stream, (int)len);
            while (stub_stream_pending() > 0 && stream_events(&p) && guard--)
                stream_io(&p, PLAT_READ);
            CHECK(p.stream.state == STREAM_CONT && ring_used(&p.ring) == 0);
            feed_cont(intervals[i], 0);
            drain(&p);
            while (stub_stream_pending() > 0 && stream_events(&p) && guard--)
                stream_io(&p, PLAT_READ);
            ok = p.stream.bytes == sound && ring_used(&p.ring) == sound;
            ok = ok && ring_peek(&p.ring, 0, got, sound) == sound && !memcmp(got, pcm, sound);
            ok = ok && metas(got, &n) == 3 && n == blocks_len && !memcmp(got, blocks, n);
            if (!ok)
                printf("ICY: interval %lu, reads of %d bytes\n", (unsigned long)intervals[i], chunk);
            CHECK(ok);
            teardown(&p);
        }
}

/* autostart 3: nothing of the body before cont, not even what came with the headers, nor counted in STAT; cont
 * with an interval of 0 then reads the body as it is */
static void test_cont_late(void)
{
    static uint8_t response[sizeof(http_ok) - 1 + 1500], got[5000];
    struct player p;
    const uint8_t *st;

    memcpy(response, http_ok, sizeof(http_ok) - 1);
    memcpy(response + sizeof(http_ok) - 1, pcm, 1500);
    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    p.verbose = 1;
    answer_stream(&p, '3', 1, response, (int)sizeof(response));
    CHECK(!strcmp(trace(), "RESP") && p.stream.state == STREAM_CONT && stream_events(&p) == 0);
    CHECK(strstr(stub_out, "stream: waiting for cont\n") != NULL);
    feed_strm('t', NULL, 0, 0, 0, NULL);
    drain(&p);
    st = last_stat("STMt");
    CHECK(st && be32(st + STAT_BYTES_LOW) == 0 && be32(st + STAT_BUF_FULL) == 0);
    CHECK(p.out.state == OUTPUT_STOPPED && ring_used(&p.ring) == 0);

    clear_tx();
    feed_cont(0, 0);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMs") && p.out.state == OUTPUT_RUNNING);   /* the threshold, 1 KB, reached */
    CHECK(strstr(stub_out, "<- cont, metadata interval 0\n") != NULL);
    CHECK(p.stream.state == STREAM_BODY && p.stream.bytes == 1500 && ring_used(&p.ring) == 1500);
    stub_feed_stream(pcm + 1500, 3500);
    drain_stream(&p);
    CHECK(p.stream.bytes == 5000 && ring_peek(&p.ring, 0, got, 5000) == 5000 && !memcmp(got, pcm, 5000));
    teardown(&p);

    /* autostart 2: STMl once cont has come */
    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    answer_stream(&p, '2', 1, response, (int)sizeof(response));
    CHECK(!strcmp(trace(), "RESP") && p.out.state == OUTPUT_STOPPED);
    clear_tx();
    feed_cont(0, 1);                                    /* the loop byte is ignored */
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMl") && p.out.state == OUTPUT_LOADED);
    teardown(&p);
}

/* A status outside 2xx: the body never enters the buffer, the message always shown. Autostart 1: DSCO 2 at once.
 * Autostart 3: RESP only, then the server decides: strm q, or a cont (Music Assistant), answered DSCO 2. */
static void test_status_refused(void)
{
    static char response[sizeof(http_404) - 1 + 100];
    struct player p;

    memcpy(response, http_404, sizeof(http_404) - 1);
    memset(response + sizeof(http_404) - 1, 'x', 100);
    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    answer_stream(&p, '1', 1, response, (int)sizeof(response));
    CHECK(!strcmp(trace(), "RESP DSCO:2"));
    CHECK(!strcmp(stub_out, "amisqueeze: stream: HTTP status 404, nothing played from this address\n"));
    CHECK(p.stream.state == STREAM_IDLE && !stub_stream_open && p.out.queued == 0 && ring_used(&p.ring) == 0);
    teardown(&p);

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    answer_stream(&p, '3', 1, response, (int)sizeof(response));
    CHECK(!strcmp(trace(), "RESP") && strstr(stub_out, "HTTP status 404") != NULL);
    CHECK(p.stream.state == STREAM_REFUSED && !stub_stream_open && p.out.queued == 0 && !player_busy(&p));
    clear_tx();
    feed_strm('q', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf") && p.stream.state == STREAM_IDLE);
    clear_tx();
    feed_cont(0, 0);                                    /* too late: no stream waits for it */
    drain(&p);
    CHECK(stub_tx_len == 0);
    teardown(&p);

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    answer_stream(&p, '3', 1, response, (int)sizeof(response));
    clear_tx();
    feed_cont(0, 0);
    drain(&p);
    CHECK(!strcmp(trace(), "DSCO:2") && p.stream.state == STREAM_IDLE && ring_used(&p.ring) == 0);
    teardown(&p);

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    answer_stream(&p, '1', 1, "SOMETHING ELSE\r\n\r\n", 18);
    CHECK(!strcmp(trace(), "RESP DSCO:2"));
    CHECK(!strcmp(stub_out, "amisqueeze: stream: no HTTP status in the response, nothing played\n"));
    teardown(&p);
}

/* A redirection (3xx) is not sound either: LMS follows a radio's redirection itself */
static void test_status_redirect(void)
{
    static const char redirect[] = "HTTP/1.1 302 Found\r\nLocation: http://x/\r\n\r\nxxxx";
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    answer_stream(&p, '3', 1, redirect, (int)strlen(redirect));
    CHECK(!strcmp(trace(), "RESP") && strstr(stub_out, "HTTP status 302") != NULL);
    CHECK(p.stream.state == STREAM_REFUSED && p.out.queued == 0 && ring_used(&p.ring) == 0);
    teardown(&p);
}

/* The next track refused while one plays: the track playing ends with STMu, without an STMs for the refused one */
static void test_status_refused_next(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    stub_feed_stream(pcm, 2000);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    answer_stream(&p, '3', 1, http_404, (int)strlen(http_404));
    CHECK(!strcmp(trace(), "RESP") && p.out.queued == 0);
    clear_tx();
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(!strcmp(trace(), "STAT:STMu"));
    teardown(&p);
}

/* Music Assistant, gapless: the track playing runs out while the next one waits for its cont. That one is still
 * streaming: its STMs at the boundary (as for any next track not received yet), then STMo, not STMu; it plays once
 * cont has come. */
static void test_cont_next(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    open_stream(&p, '1', 1);
    stub_feed_stream(pcm, 2000);
    drain_stream(&p);
    stub_stream_end();
    stream_io(&p, PLAT_READ);
    answer_stream(&p, '3', 1, http_ok, (int)strlen(http_ok));
    CHECK(!strcmp(trace(), "RESP") && p.stream.state == STREAM_CONT && p.out.queued == 1);
    clear_tx();
    stub_now += 100;
    output_tick(&p, stub_now);
    CHECK(!strcmp(trace(), "STAT:STMs STAT:STMo") && p.out.state == OUTPUT_RUNNING && p.out.starved);
    clear_tx();
    feed_cont(0, 0);
    drain(&p);
    stub_feed_stream(pcm, 2000);
    drain_stream(&p);
    stub_now += 5;
    output_tick(&p, stub_now);
    CHECK(stub_tx_len == 0 && !p.out.starved && p.out.handed > 2000);
    teardown(&p);
}

/* cont with no stream waiting for it, said with VERBOSE; a cont too short */
static void test_cont_alone(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    feed_cont(16000, 0);
    drain(&p);
    CHECK(stub_tx_len == 0 && stub_out[0] == '\0');
    p.verbose = 1;
    open_stream(&p, '1', 1);                            /* autostart 1: no cont expected */
    clear_tx();
    feed_cont(16000, 1);
    drain(&p);
    CHECK(stub_tx_len == 0 && p.stream.icy.interval == 0);
    CHECK(!strcmp(stub_out, "<- cont, metadata interval 16000\nstream: loop asked by cont, ignored\n"
                            "stream: cont with no stream waiting for it, ignored\n"));
    clear_tx();
    stub_feed_packet("cont", "\0\0\0", 3);
    drain(&p);
    CHECK(!strcmp(stub_out, "amisqueeze: cont too short (7)\n"));
    clear_tx();
    stub_feed_packet("cont", "\0\0\0\0\0\0", 6);       /* no room for the count of GUIDs */
    drain(&p);
    CHECK(!strcmp(stub_out, "amisqueeze: cont too short (10)\n"));
    clear_tx();
    p.verbose = 0;
    stub_feed_packet("cont", "\0\0\0\0\0\0\0", 7);     /* the shortest whole cont */
    drain(&p);
    CHECK(stub_out[0] == '\0');
    teardown(&p);
}

/* With VERBOSE, a radio's metadata block without a title is said, and passed on as any other */
static void test_icy_no_title(void)
{
    static const char headers[] = "ICY 200 OK\r\nicy-metaint:16\r\n\r\n";
    uint8_t body[16 + 1 + 16 + 16];
    struct player p;

    memcpy(body, pcm, 16);
    body[16] = 1;                                       /* one block of 16 bytes */
    memcpy(body + 17, "StreamUrl='x';\0\0", 16);
    memcpy(body + 33, pcm + 16, 16);
    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    p.verbose = 1;
    strm_pcm(&p, '3', 255);
    stream_io(&p, PLAT_WRITE);
    stub_feed_stream(headers, (int)strlen(headers));
    stub_feed_stream(body, (int)sizeof(body));
    drain_stream(&p);
    clear_tx();
    feed_cont(16, 0);
    drain(&p);
    CHECK(strstr(stub_out, "stream: metadata without a title (16 bytes)\n") != NULL);
    CHECK(!strcmp(trace(), "META") && ring_used(&p.ring) == 32);
    teardown(&p);
}

/* RESP and META go nowhere once the control connection is closed (the stream may still be read) */
static void test_send_closed(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    slim_disconnect(&p);
    clear_tx();
    slim_send_resp(&p, "x", 1);
    slim_send_meta(&p, (const uint8_t *)"x", 1);
    CHECK(stub_tx_len == 0 && !p.send_failed);
    teardown(&p);
}

/* A cont that does not come: said once after CONT_SAY_MS, without VERBOSE; the stream still waits, and plays
 * once it comes */
static void test_cont_missing(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    answer_stream(&p, '3', 1, http_ok, (int)strlen(http_ok));
    CHECK(p.stream.state == STREAM_CONT && stub_out[0] == '\0');
    stub_now += CONT_SAY_MS - 1;
    output_tick(&p, stub_now);
    CHECK(stub_out[0] == '\0');
    stub_now += 1;
    output_tick(&p, stub_now);
    CHECK(!strcmp(stub_out, "amisqueeze: stream: no cont from the server after 10 s, still waiting\n"));
    clear_tx();
    stub_now += 2 * CONT_SAY_MS;
    output_tick(&p, stub_now);
    CHECK(stub_out[0] == '\0' && stub_tx_len == 0 && p.stream.state == STREAM_CONT);
    feed_cont(0, 0);
    drain(&p);
    stub_feed_stream(pcm, 2000);
    drain_stream(&p);
    CHECK(p.out.state == OUTPUT_RUNNING && strstr(trace(), "STAT:STMs") != NULL);
    teardown(&p);
}

/* What the server sends that is not quite a stream: a cont alone, short or missing, a redirection, a block without
 * a title, a connection already closed */
static void test_stream_edges(void)
{
    test_cont_alone();
    test_cont_missing();
    test_status_redirect();
    test_icy_no_title();
    test_send_closed();
}

/* Reported readable with nothing to read while a radio's metadata is due: before its length byte, before the block,
 * in the middle of the block. No error, no DSCO, left out of the waits; the rest read once it comes, the sound into
 * the ring and the block to the server as without the pause */
static void test_icy_again(void)
{
    static const int cuts[] = { 16, 17, 25 };           /* the sound read; the length byte too; half the block */
    static uint8_t stream[49], got[32];
    struct player p;
    uint32_t n;
    unsigned i;

    memcpy(stream, pcm, 16);
    stream[16] = 1;
    memcpy(stream + 17, "StreamTitle='a';", 16);
    memcpy(stream + 33, pcm + 16, 16);
    for (i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        setup(&p, "Amiga");
        CHECK(slim_connect(&p, 0));
        answer_stream(&p, '3', 1, http_ok, (int)strlen(http_ok));
        feed_cont(16, 0);
        drain(&p);
        CHECK(p.stream.state == STREAM_BODY);
        clear_tx();
        stub_feed_stream(stream, cuts[i]);
        drain_stream(&p);
        stream_io(&p, PLAT_READ);                       /* nothing to read */
        CHECK(p.stream.state == STREAM_BODY && stub_tx_len == 0 && stream_events(&p) == 0);
        stub_now += AGAIN_WAIT_MS;
        stub_feed_stream(stream + cuts[i], (int)sizeof(stream) - cuts[i]);
        drain_stream(&p);
        CHECK(p.stream.state == STREAM_BODY && p.stream.bytes == 32);
        CHECK(ring_peek(&p.ring, 0, got, 32) == 32 && !memcmp(got, pcm, 32));
        CHECK(metas(got, &n) == 1 && n == 16 && !memcmp(got, "StreamTitle='a';", 16));
        teardown(&p);
    }
}

void tests_icy(void)
{
    test_http_status();
    test_icy_title();
    test_icy_cuts();
    test_cont_late();
    test_status_refused();
    test_status_refused_next();
    test_cont_next();
    test_stream_edges();
    test_icy_again();
}
