/*
 * test_core.c - unit tests of the amisqueeze portable core (host build, see "make test")
 *
 * The core runs on stub_platform.c: no network, the packets the player sends are captured and checked byte by
 * byte against the protocol definitions (squeezelite slimproto.h, LMS Slim/Networking/Slimproto.pm), and the
 * clock is set by the tests (virtual output timing). Expected values marked "seen in WinUAE" come from the Amiga
 * build talking to a real LMS 9.1.1.
 */
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "player.h"
#include "stub_platform.h"

static int checks, failures;

#define CHECK(cond) do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/* STAT packet offsets (8-byte header included) */
#define STAT_EVENT       8
#define STAT_BUF_SIZE    15
#define STAT_BUF_FULL    19
#define STAT_BYTES_LOW   27
#define STAT_JIFFIES     33
#define STAT_ELAPSED_S   45
#define STAT_ELAPSED_MS  51
#define STAT_SERVER_TS   55

#define PCM_BPS 176400                  /* 44.1 kHz, 16 bits, stereo */

static const char http_ok[] = "HTTP/1.0 200 OK\r\nContent-Type: audio/L16;rate=44100;channels=2\r\n\r\n";
static const char request[] = "GET /stream.mp3?player=02:41:4d:d4:ca:84 HTTP/1.0\r\n\r\n";
static uint8_t pcm[PCM_BPS];            /* one second of "audio" */

static uint32_t be16(const uint8_t *b) { return (uint32_t)b[0] << 8 | b[1]; }
static uint32_t be32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static void set32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
}

/* Packets sent on the control connection since the last clear, e.g. "STAT:STMf RESP DSCO:0" */
static const char *trace(void)
{
    static char t[2048];
    int pos = 0;

    t[0] = '\0';
    while (pos + 8 <= stub_tx_len) {
        char item[32];
        if (!memcmp(stub_tx + pos, "STAT", 4))
            snprintf(item, sizeof(item), "STAT:%.4s", (const char *)stub_tx + pos + 8);
        else if (!memcmp(stub_tx + pos, "DSCO", 4))
            snprintf(item, sizeof(item), "DSCO:%d", stub_tx[pos + 8]);
        else
            snprintf(item, sizeof(item), "%.4s", (const char *)stub_tx + pos);
        if (t[0])
            strcat(t, " ");
        strcat(t, item);
        pos += 8 + (int)be32(stub_tx + pos + 4);
    }
    return t;
}

/* Last STAT packet with this event, NULL if none */
static const uint8_t *last_stat(const char *event)
{
    const uint8_t *found = NULL;
    int pos = 0;

    while (pos + 8 <= stub_tx_len) {
        if (!memcmp(stub_tx + pos, "STAT", 4) && !memcmp(stub_tx + pos + 8, event, 4))
            found = stub_tx + pos;
        pos += 8 + (int)be32(stub_tx + pos + 4);
    }
    return found;
}

static void clear_tx(void)
{
    stub_tx_len = 0;
    stub_out[0] = '\0';
}

static void setup(struct player *p, const char *name)
{
    memset(p, 0, sizeof(*p));
    p->sock = -1;
    strcpy(p->name, name);
    mac_from_name(name, p->mac);
    p->server_ip = 0x7f000001;          /* 127.0.0.1 */
    p->server_port = SLIMPROTO_PORT;
    stub_reset();
    stub_chunk = 0;
    stub_connect_fails = 0;
    stub_now = 1000;
    CHECK(stream_init(p));
}

static void teardown(struct player *p)
{
    slim_disconnect(p);
    audio_close(p);
    stream_free(p);
}

/* Lets the player read everything queued on the control connection */
static void drain(struct player *p)
{
    while (stub_pending() > 0)
        if (!slim_receive(p)) {
            CHECK(!"slim_receive failed");
            break;
        }
}

/* Lets the player read everything queued on the stream connection */
static void drain_stream(struct player *p)
{
    int guard = 1000;

    while (stub_stream_pending() > 0 && stream_events(p) && guard--)
        stream_io(p, PLAT_READ);
}

/* strm packet body (after the opcode): 24-byte header, then the HTTP request */
static void feed_strm(char cmd, const char *fmt7, uint32_t replay_gain, uint16_t port, uint32_t ip,
                      const char *req)
{
    uint8_t b[24 + 256];
    int n = 24;

    memset(b, 0, sizeof(b));
    b[0] = (uint8_t)cmd;
    if (fmt7)                           /* autostart, format, size, rate, channels, endianness, threshold */
        memcpy(b + 1, fmt7, 7);
    set32(b + 14, replay_gain);
    b[18] = (uint8_t)(port >> 8);
    b[19] = (uint8_t)port;
    set32(b + 20, ip);
    if (req) {
        memcpy(b + 24, req, strlen(req));
        n += (int)strlen(req);
    }
    stub_feed_packet("strm", b, n);
}

/* strm s for 44.1 kHz 16-bit stereo little-endian PCM, as seen in WinUAE, with the given autostart and
 * threshold (KB) */
static void strm_pcm(struct player *p, char autostart, int threshold_kb)
{
    char fmt[7] = { autostart, 'p', '1', '3', '2', '1', (char)threshold_kb };

    feed_strm('s', fmt, 0, 9000, 0, request);
    drain(p);
}

/* strm s, connection, response headers: the stream is ready to receive the body */
static void open_stream(struct player *p, char autostart, int threshold_kb)
{
    strm_pcm(p, autostart, threshold_kb);
    stream_io(p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    drain_stream(p);
}

static void test_ring(void)
{
    struct ring r;
    uint8_t *w, out[16];
    const uint8_t *rp;
    uint32_t n;

    CHECK(ring_init(&r, 16));
    n = ring_write_space(&r, &w);
    CHECK(n == 16);
    memcpy(w, "0123456789", 10);
    ring_commit(&r, 10);
    ring_consume(&r, 6);                /* "6789" left, write position 10 */
    n = ring_write_space(&r, &w);
    CHECK(n == 6);                      /* contiguous up to the end */
    memcpy(w, "abcdef", 6);
    ring_commit(&r, 6);
    n = ring_write_space(&r, &w);
    CHECK(n == 6 && w == r.buf);        /* wrapped around: free up to the read position (6) */
    memcpy(w, "gh", 2);
    ring_commit(&r, 2);
    CHECK(ring_used(&r) == 12 && ring_space(&r) == 4);
    n = ring_read_space(&r, &rp);
    CHECK(n == 10 && rp == r.buf + 6);  /* contiguous data up to the end, the rest is at the start */
    CHECK(ring_read(&r, out, 16) == 12 && !memcmp(out, "6789abcdefgh", 12));
    CHECK(r.total_in == 18 && r.total_out == 18 && ring_used(&r) == 0);
    ring_clear(&r);
    CHECK(r.total_in == 0 && r.total_out == 0);
    ring_free(&r);
}

static void test_http_header_end(void)
{
    CHECK(http_header_end(http_ok, (int)strlen(http_ok)) == (int)strlen(http_ok));
    CHECK(http_header_end("HTTP/1.0 200 OK\r\n\r\nDATA", 23) == 19);
    CHECK(http_header_end("HTTP/1.0 200 OK\n\nDATA", 21) == 17);
    CHECK(http_header_end("HTTP/1.0 200 OK\r\n\r", 18) == 0);   /* not complete yet */
    CHECK(http_header_end("", 0) == 0);
}

static void test_mac(void)
{
    uint8_t mac[6];
    static const uint8_t amiga[6] = { 0x02, 0x41, 0x4D, 0xD4, 0xCA, 0x84 };     /* seen in WinUAE */
    static const uint8_t other[6] = { 0x02, 0x41, 0x4D, 0xD5, 0xF3, 0xC7 };     /* seen in WinUAE */

    mac_from_name("Amiga", mac);
    CHECK(!memcmp(mac, amiga, 6));
    mac_from_name("Other", mac);
    CHECK(!memcmp(mac, other, 6));
    CHECK(parse_mac("00:11:22:aa:BB:ff", mac) && mac[0] == 0x00 && mac[3] == 0xaa && mac[5] == 0xff);
    CHECK(!parse_mac("00:11:22:aa:bb", mac));
    CHECK(!parse_mac("00:11:22:aa:bb:ff:00", mac));
    CHECK(!parse_mac("00-11-22-aa-bb-ff", mac));
}

static void test_helo(void)
{
    struct player p;
    const char *caps;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    CHECK(!memcmp(stub_tx, "HELO", 4));
    CHECK(be32(stub_tx + 4) == (uint32_t)stub_tx_len - 8);
    CHECK(stub_tx[8] == 12);                            /* device id: squeezeplay */
    CHECK(stub_tx[9] == 0);                             /* revision */
    CHECK(!memcmp(stub_tx + 10, p.mac, 6));
    CHECK(be16(stub_tx + 32) == 0);                     /* wlan_channellist: first connection */
    CHECK(be32(stub_tx + 34) == 0 && be32(stub_tx + 38) == 0);  /* bytes received */
    caps = (const char *)stub_tx + 44;
    CHECK(stub_tx_len > 44 && !strncmp(caps, "Model=squeezelite,", 18));
    CHECK(stub_tx_len - 44 >= 4 && !memcmp(stub_tx + stub_tx_len - 4, ",pcm", 4));
    CHECK(strstr(stub_out, "connected to the server") != NULL);

    slim_disconnect(&p);
    clear_tx();
    CHECK(slim_connect(&p, 1));
    CHECK(be16(stub_tx + 32) == 0x4000);                /* reconnection */
    teardown(&p);
}

static void test_strm_t(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_now = 0x01020304;
    feed_strm('t', NULL, 0xDEADBEEF, 0, 0, NULL);
    drain(&p);
    CHECK(stub_tx_len == 8 + 53);
    CHECK(!memcmp(stub_tx, "STAT", 4) && be32(stub_tx + 4) == 53);
    CHECK(!memcmp(stub_tx + STAT_EVENT, "STMt", 4));
    CHECK(be32(stub_tx + STAT_BUF_SIZE) == STREAM_BUFFER_SIZE);
    CHECK(be16(stub_tx + 31) == 0xffff);                /* signal strength: wired */
    CHECK(be32(stub_tx + STAT_JIFFIES) == 0x01020304);
    CHECK(be32(stub_tx + STAT_SERVER_TS) == 0xDEADBEEF);        /* server timestamp echoed */
    CHECK(be16(stub_tx + 59) == 0);                     /* error code */
    teardown(&p);
}

static void test_setd(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_feed_packet("setd", "\0Renamed", 8);           /* id 0 = player name */
    drain(&p);
    CHECK(!strcmp(p.name, "Renamed"));
    CHECK(!memcmp(stub_tx, "SETD", 4) && be32(stub_tx + 4) == 1 + 8);
    CHECK(stub_tx[8] == 0 && !memcmp(stub_tx + 9, "Renamed", 8));   /* NUL-terminated */

    clear_tx();
    stub_feed_packet("setd", "\0", 1);                  /* query: the player answers with its name */
    drain(&p);
    CHECK(!memcmp(stub_tx, "SETD", 4) && !memcmp(stub_tx + 9, "Renamed", 8));
    teardown(&p);
}

static void test_quiet_packets(void)
{
    struct player p;
    uint8_t audg[18];

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    memset(audg, 0, sizeof(audg));
    stub_feed_packet("audg", audg, sizeof(audg));
    stub_feed_packet("aude", "\1\1", 2);
    drain(&p);
    CHECK(stub_tx_len == 0);                            /* nothing to answer */
    CHECK(stub_out[0] == '\0');                         /* nothing printed without VERBOSE */
    teardown(&p);
}

/* Packets split across reads, one byte at a time */
static void test_fragmented(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_chunk = 1;
    feed_strm('t', NULL, 0x11223344, 0, 0, NULL);
    feed_strm('t', NULL, 0x55667788, 0, 0, NULL);
    drain(&p);
    CHECK(stub_tx_len == 2 * (8 + 53));
    CHECK(be32(stub_tx + STAT_SERVER_TS) == 0x11223344);
    CHECK(be32(stub_tx + 61 + STAT_SERVER_TS) == 0x55667788);
    teardown(&p);
}

static void test_bad_length(void)
{
    struct player p;
    static const uint8_t zero[2] = { 0, 0 };
    static const uint8_t huge[2] = { 0xff, 0xff };

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_feed(zero, 2);
    CHECK(slim_receive(&p) == 0);                       /* length 0: invalid */
    teardown(&p);

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_feed(huge, 2);
    CHECK(slim_receive(&p) == 0);                       /* larger than the receive buffer */
    CHECK(strstr(stub_out, "invalid packet size") != NULL);
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
    char fmt[7] = { '1', 'f', '?', '?', '?', '?', 10 };     /* FLAC: not decoded yet */

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    feed_strm('s', fmt, 0, 9000, 0, request);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf STAT:STMn"));
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

/* ---- OUTPUT file ---- */

static uint8_t track_a[PCM_BPS];        /* 1 s, then 0.5 s of non-periodic bytes: a slip would show */
static uint8_t track_b[PCM_BPS / 2];

static void fill(uint8_t *b, uint32_t len, uint32_t seed)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        b[i] = (uint8_t)(seed >> 16);
    }
}

/* Plays until the output stops (at most `ms`), in steps of 100 ms like the main loop */
static void play_for(struct player *p, uint32_t ms)
{
    for (; ms > 0 && p->out.state == OUTPUT_RUNNING; ms -= 100) {
        stub_now += 100;
        output_tick(p, stub_now);
    }
}

static void setup_file(struct player *p)
{
    setup(p, "Amiga");
    stub_file_fails = 0;
    stub_file_limit = 0;
    stub_file_len = 0;
    p->verbose = 1;
    CHECK(output_file_open(p, "out.pcm"));
    CHECK(stub_file_open && strstr(stub_out, "output: writing the audio to out.pcm") != NULL);
    CHECK(slim_connect(p, 0));
}

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

/* ---- audio output (AHI) ---- */

#define BLOCK ((uint32_t)AUDIO_BLOCK_SIZE)

static uint8_t expect[2 * 1024 * 1024];

/* What the device must receive: 16-bit samples made big-endian, left and right swapped if asked */
static void to_device(uint8_t *dst, const uint8_t *src, uint32_t n, int swap_lr)
{
    uint32_t i;

    for (i = 0; i + 1 < n; i += 2) {
        dst[i] = src[i + 1];
        dst[i + 1] = src[i];
    }
    if (swap_lr)
        for (i = 0; i + 3 < n; i += 4) {
            uint8_t t0 = dst[i], t1 = dst[i + 1];
            dst[i] = dst[i + 2];
            dst[i + 1] = dst[i + 3];
            dst[i + 2] = t0;
            dst[i + 3] = t1;
        }
}

static void setup_audio(struct player *p, int swap)
{
    setup(p, "Amiga");
    stub_audio_fails = 0;
    stub_audio_abort_played = 0;
    CHECK(audio_open(p, 2, swap) && stub_audio_open && stub_audio_unit == 2);
    stub_audio_clear();
    CHECK(slim_connect(p, 0));
}

/* Ends the request playing now, then lets the output collect it and refill */
static int finish(struct player *p)
{
    int b = stub_audio_finish();

    stub_now += 10;
    output_tick(p, stub_now);
    return b;
}

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

/* No AHI; a sample size the audio output does not take */
static void test_audio_errors(void)
{
    struct player p;
    char pcm24[7] = { '1', 'p', '2', '3', '2', '1', 1 };

    setup(&p, "Amiga");
    stub_audio_fails = 1;
    CHECK(!audio_open(&p, 0, 0) && !p.audio.enabled);
    CHECK(strstr(stub_out, "cannot open ahi.device unit 0") != NULL);
    teardown(&p);

    setup_audio(&p, 0);
    clear_tx();
    feed_strm('s', pcm24, 0, 9000, 0, request);
    drain(&p);
    CHECK(!strcmp(trace(), "STAT:STMf STAT:STMn"));
    CHECK(strstr(stub_out, "24-bit samples not supported") != NULL);
    teardown(&p);
}

int main(void)
{
    test_ring();
    test_http_header_end();
    test_mac();
    test_helo();
    test_strm_t();
    test_setd();
    test_quiet_packets();
    test_fragmented();
    test_bad_length();
    test_stream_sequence();
    test_gapless();
    test_gapless_format();
    test_pause();
    test_stop();
    test_connect_failure();
    test_not_pcm();
    test_autostart_0();
    test_output_file();
    test_output_file_stop();
    test_output_file_errors();
    test_audio_blocks();
    test_audio_swap_mono();
    test_audio_pause();
    test_audio_pause_ahi4();
    test_audio_stop();
    test_audio_gapless();
    test_audio_wait_full_block();
    test_audio_errors();
    printf("test_core: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
