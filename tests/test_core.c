/*
 * test_core.c - unit tests of the amisqueeze portable core (host build, see "make test")
 *
 * The core runs on stub_platform.c: no network, the packets the player sends are captured and checked byte by
 * byte against the protocol definitions (squeezelite slimproto.h, LMS Slim/Networking/Slimproto.pm), and the
 * clock is set by the tests (virtual output timing). Expected values marked "seen in WinUAE" come from the Amiga
 * build talking to a real LMS 9.1.1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    CHECK(ring_peek(&r, 8, out, 4) == 4 && !memcmp(out, "efgh", 4));      /* across the end */
    CHECK(ring_peek(&r, 10, out, 4) == 2 && !memcmp(out, "gh", 2));       /* from the start of the buffer */
    CHECK(ring_peek(&r, 11, out, 4) == 1 && out[0] == 'h');               /* from past the start */
    CHECK(ring_peek(&r, 0, out, 3) == 3 && !memcmp(out, "678", 3));
    CHECK(ring_peek(&r, 12, out, 4) == 0 && ring_used(&r) == 12 && r.total_out == 6);
    CHECK(ring_read(&r, out, 16) == 12 && !memcmp(out, "6789abcdefgh", 12));
    CHECK(r.total_in == 18 && r.total_out == 18 && ring_used(&r) == 0);
    ring_clear(&r);
    CHECK(r.total_in == 0 && r.total_out == 0);
    ring_free(&r);
}

/* Dropping the last bytes written (strm f): across the end of the buffer, never what was consumed */
static void test_ring_truncate(void)
{
    struct ring r;
    uint8_t *w, out[16];

    CHECK(ring_init(&r, 16));
    ring_write_space(&r, &w);
    memcpy(w, "0123456789abcdef", 16);
    ring_commit(&r, 16);
    ring_consume(&r, 6);
    ring_write_space(&r, &w);
    memcpy(w, "gh", 2);                 /* "6789abcdefgh", the write position wrapped around to 2 */
    ring_commit(&r, 2);
    ring_truncate(&r, 20);              /* past what was written: nothing to drop */
    CHECK(ring_used(&r) == 12 && r.total_in == 18);
    ring_truncate(&r, 14);              /* back across the end of the buffer: "6789abcd" left */
    CHECK(ring_used(&r) == 8 && r.total_in == 14 && r.wpos == 14);
    CHECK(ring_write_space(&r, &w) == 2 && w == r.buf + 14);
    memcpy(w, "XY", 2);
    ring_commit(&r, 2);
    CHECK(ring_peek(&r, 0, out, 16) == 10 && !memcmp(out, "6789abcdXY", 10));
    ring_consume(&r, 4);
    ring_truncate(&r, 8);               /* below what was consumed (10): everything left is dropped */
    CHECK(ring_used(&r) == 0 && r.total_in == 10 && r.total_out == 10 && r.wpos == r.rpos);
    ring_write_space(&r, &w);
    memcpy(w, "ij", 2);
    ring_commit(&r, 2);
    CHECK(ring_read(&r, out, 16) == 2 && !memcmp(out, "ij", 2) && r.total_out == 12);
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

static void test_maxrate(void)
{
    CHECK(parse_maxrate("AHI") == MAXRATE_AHI && parse_maxrate("ahi") == MAXRATE_AHI);
    CHECK(parse_maxrate("AHISTD") == MAXRATE_AHISTD && parse_maxrate("ahistd") == MAXRATE_AHISTD);
    CHECK(parse_maxrate("22050") == 22050 && parse_maxrate("8000") == 8000 && parse_maxrate("48000") == 48000);
    CHECK(parse_maxrate("7999") == 0 && parse_maxrate("48001") == 0 && parse_maxrate("") == 0);
    CHECK(parse_maxrate("22k") == 0 && parse_maxrate("-22050") == 0 && parse_maxrate("99999999999") == 0);
    CHECK(maxrate_from_ahi(MAXRATE_AHI, 27429) == 27429 && maxrate_from_ahi(MAXRATE_AHISTD, 27429) == 24000);
    CHECK(maxrate_from_ahi(MAXRATE_AHISTD, 22050) == 22050 && maxrate_from_ahi(MAXRATE_AHISTD, 22049) == 16000);
    CHECK(maxrate_from_ahi(MAXRATE_AHISTD, 44099) == 32000 && maxrate_from_ahi(MAXRATE_AHISTD, 8000) == 8000);
    CHECK(maxrate_from_ahi(MAXRATE_AHISTD, 48000) == 48000 && maxrate_from_ahi(MAXRATE_AHI, 44100) == 44100);
    CHECK(maxrate_from_ahi(MAXRATE_AHI, 5000) == 8000 && maxrate_from_ahi(MAXRATE_AHISTD, 5000) == 8000);
    CHECK(maxrate_from_ahi(MAXRATE_AHI, 96000) == 48000 && maxrate_from_ahi(MAXRATE_AHISTD, 96000) == 48000);
}

/* The capabilities of the HELO in stub_tx contain s */
static int helo_caps_have(const char *s)
{
    static char caps[sizeof(stub_tx) + 1];
    int n = stub_tx_len > 44 ? stub_tx_len - 44 : 0;

    memcpy(caps, stub_tx + 44, (size_t)n);
    caps[n] = '\0';
    return strstr(caps, s) != NULL;
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
    CHECK(helo_caps_have(",MaxSampleRate=48000,pcm"));  /* without MAXRATE */
    CHECK(strstr(stub_out, "connected to the server") != NULL);

    slim_disconnect(&p);
    clear_tx();
    p.maxrate = 22050;                                  /* MAXRATE */
    CHECK(slim_connect(&p, 1));
    CHECK(be16(stub_tx + 32) == 0x4000);                /* reconnection */
    CHECK(be32(stub_tx + 4) == (uint32_t)stub_tx_len - 8);
    CHECK(helo_caps_have(",MaxSampleRate=22050,pcm"));
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

/* What Music Assistant sends and amisqueeze ignores: printed with VERBOSE only; an unknown opcode always */
static void test_ignored_packets(void)
{
    static const char *const ignored[] = { "vers", "grfb", "grfe", "codc", "cont" };
    struct player p;
    char line[40];
    int i;

    for (i = 0; i < 5; i++) {
        setup(&p, "Amiga");
        CHECK(slim_connect(&p, 0));
        clear_tx();
        stub_feed_packet(ignored[i], "0123", 4);
        drain(&p);
        CHECK(stub_out[0] == '\0');
        p.verbose = 1;
        stub_feed_packet(ignored[i], "0123", 4);
        drain(&p);
        snprintf(line, sizeof(line), "<- %s ignored (8 bytes)\n", ignored[i]);
        CHECK(!strcmp(stub_out, line));
        CHECK(stub_tx_len == 0);                        /* nothing to answer */
        teardown(&p);
    }

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_feed_packet("serv", "0123", 4);
    drain(&p);
    CHECK(!strcmp(stub_out, "amisqueeze: serv not handled (8 bytes)\n"));
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

/* Packets too short for what their opcode carries: ignored or said, never read past their end. Past it lie the
 * previous packet's bytes (the receive buffer is reused), so each short packet follows one that would make a read
 * past its end visible. */
static void test_short_packets(void)
{
    struct player p;
    static const uint8_t str[5] = { 0, 3, 's', 't', 'r' };
    uint8_t audg[18], b[20];
    uint32_t volume;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    p.verbose = 1;
    feed_strm('t', NULL, 0x11223344, 0, 0, NULL);       /* leaves "strm" in the receive buffer */
    drain(&p);
    clear_tx();
    stub_feed(str, 5);                                  /* 3 bytes: not even an opcode */
    drain(&p);
    CHECK(stub_tx_len == 0 && !strcmp(stub_out, "amisqueeze: packet too short (3)\n"));

    clear_tx();
    memset(b, 0, sizeof(b));
    b[0] = 't';
    stub_feed_packet("strm", b, sizeof(b));             /* 24 bytes of the 28 a strm has */
    drain(&p);
    CHECK(stub_tx_len == 0 && !strcmp(stub_out, "amisqueeze: strm too short (24)\n"));

    memset(audg, 0, sizeof(audg));                      /* old gains, adjust, preamp, new left and right gains */
    audg[8] = 1;                                        /* adjust: the volume follows the gains */
    audg[9] = 255;
    set32(audg + 10, 0x4000);                           /* 25 % */
    set32(audg + 14, 0x4000);
    stub_feed_packet("audg", audg, sizeof(audg));
    drain(&p);
    volume = p.audio.volume;
    clear_tx();
    set32(audg + 10, 0x8000);
    stub_feed_packet("audg", audg, 17);                 /* 21 bytes of the 22 an audg has */
    drain(&p);
    CHECK(stub_out[0] == '\0' && p.audio.volume == volume);

    stub_feed_packet("setd", "\0", 1);                  /* leaves id 0 (the name) in the receive buffer */
    drain(&p);
    clear_tx();
    stub_feed_packet("setd", "", 0);                    /* no id */
    drain(&p);
    CHECK(stub_tx_len == 0 && stub_out[0] == '\0');
    teardown(&p);
}

/* A name longer than the player keeps (PLAYER_NAME_LEN): cut, and the cut name confirmed to the server */
static void test_setd_long_name(void)
{
    struct player p;
    char b[1 + PLAYER_NAME_LEN + 10];

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    b[0] = 0;
    memset(b + 1, 'N', sizeof(b) - 1);
    stub_feed_packet("setd", b, sizeof(b));
    drain(&p);
    CHECK(strlen(p.name) == PLAYER_NAME_LEN && p.name[0] == 'N');
    CHECK(!memcmp(stub_tx, "SETD", 4) && be32(stub_tx + 4) == 1 + PLAYER_NAME_LEN + 1);
    teardown(&p);
}

/* A name the server sends in UTF-8 ("Sejour" with an e acute), or with a control byte: shown with '?' for each byte that is not
 * printable ASCII, kept and sent back as received (the server would rename the player otherwise) */
static void test_setd_not_ascii(void)
{
    struct player p;
    static const char name[] = "\0S\xc3\xa9jour\x1b[2J";

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_feed_packet("setd", name, sizeof(name) - 1);
    drain(&p);
    CHECK(!strcmp(stub_out, "player name: S??jour?[2J\n"));
    CHECK(!strcmp(p.name, name + 1));
    CHECK(!memcmp(stub_tx, "SETD", 4) && be32(stub_tx + 4) == sizeof(name));
    CHECK(!memcmp(stub_tx + 8, name, sizeof(name)));
    teardown(&p);
}

/* Raw PCM fields out of range in a strm s (size, rate, channels): refused as a track it cannot play */
static void test_strm_pcm_fields(void)
{
    static const char bad[][7] = {
        { '1', 'p', '4', '3', '2', '1', 10 },          /* size '4': 5 bytes */
        { '1', 'p', '/', '3', '2', '1', 10 },          /* size '/': 0 bytes */
        { '1', 'p', '1', ':', '2', '1', 10 },          /* rate ':': past the table (88.2 kHz, refused: plan 0.9) */
        { '1', 'p', '1', '/', '2', '1', 10 },          /* rate '/': before it */
        { '1', 'p', '1', '3', '0', '1', 10 },          /* no channel */
        { '1', 'p', '1', '3', '3', '1', 10 },          /* 3 channels */
    };
    struct player p;
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        setup(&p, "Amiga");
        CHECK(slim_connect(&p, 0));
        clear_tx();
        feed_strm('s', bad[i], 0, 9000, 0, request);
        drain(&p);
        CHECK(!strcmp(trace(), "STAT:STMf STAT:STMc STAT:STMn"));
        CHECK(strstr(stub_out, "stream format p not supported") != NULL);
        CHECK(p.stream.state == STREAM_IDLE && !stub_stream_open);
        teardown(&p);
    }
}

/* An autostart out of range ('0' to '3'): taken as 1, without the message of a direct stream (2 or 3) */
static void test_strm_autostart(void)
{
    static const char autostart[] = { '7', '/' };
    struct player p;
    unsigned i;

    for (i = 0; i < sizeof(autostart); i++) {
        setup(&p, "Amiga");
        CHECK(slim_connect(&p, 0));
        p.verbose = 1;
        clear_tx();
        strm_pcm(&p, autostart[i], 1);
        CHECK(p.stream.autostart == 1 && strstr(stub_out, "handled as") == NULL);
        teardown(&p);
    }
}

/* Response headers that fill their buffer without ending: the stream given up (DSCO 1), not read further */
static void test_headers_too_long(void)
{
    struct player p;
    static char headers[2100];

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

static int count_in_trace(const char *name)
{
    const char *t = trace();
    int n = 0;

    while ((t = strstr(t, name)) != NULL) {
        n++;
        t += strlen(name);
    }
    return n;
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

/* Plays the rest of the track to its end */
static void play_out(struct player *p)
{
    int guard;

    stub_stream_end();
    stream_io(p, PLAT_READ);
    for (guard = 0; guard < 20 && p->out.state == OUTPUT_RUNNING; guard++)
        finish(p);
}

/* What the device must receive for a stream of sample_bytes-byte samples: 16-bit big-endian, made here
 * independently of audio.c (8 bits: unsigned, centred on 0x80; 24 and 32 bits: the two most significant bytes).
 * Returns the bytes written. */
static uint32_t to_device_fmt(uint8_t *dst, const uint8_t *src, uint32_t n, int sample_bytes, int big_endian)
{
    uint32_t i, o = 0;

    for (i = 0; i + (uint32_t)sample_bytes <= n; i += (uint32_t)sample_bytes) {
        const uint8_t *s = src + i;
        int msb = big_endian ? 0 : sample_bytes - 1;

        if (sample_bytes == 1) {
            dst[o++] = (uint8_t)(s[0] - 0x80);
            dst[o++] = 0;
        } else {
            dst[o++] = s[msb];
            dst[o++] = s[big_endian ? 1 : msb - 1];
        }
    }
    return o;
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

/* ---- WAV header (Music Assistant's wav output codec, plan step player-7b) ---- */

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

/* ---- decoders (plan step player-8a) ---- */

/* "Widening" decoder, for the tests only: each byte of input becomes a 16-bit little-endian sample
 * ((byte - 128) * 256), so the output is twice the input. With raw PCM, input and output have the same size and a
 * position of the stream used for the output (or the reverse) would go unnoticed; here it shows. */
static int widen_start(struct decoder *d, const struct pcm_format *in)
{
    d->out.rate = in->rate;
    d->out.channels = in->channels;
    d->out.sample_bytes = 2;
    d->out.big_endian = 0;
    d->in_frame = in->channels;             /* one byte per sample */
    return 1;
}

static uint32_t widen_run(struct decoder *d, struct ring *r, uint32_t avail, int complete, uint8_t *out,
                          uint32_t out_max)
{
    uint8_t in[256];
    uint32_t frames = avail / d->in_frame, n, i = 0;

    if (frames > out_max / (2 * d->in_frame))
        frames = out_max / (2 * d->in_frame);
    if (frames == 0) {
        if (complete && avail > 0 && avail < d->in_frame)
            ring_consume(r, avail);
        return 0;
    }
    n = frames * d->in_frame;
    while (i < n) {
        uint32_t k = n - i < sizeof(in) ? n - i : sizeof(in), j;

        ring_read(r, in, k);
        for (j = 0; j < k; j++) {
            out[2 * (i + j)] = 0;
            out[2 * (i + j) + 1] = (uint8_t)(in[j] - 128);
        }
        i += k;
    }
    return 2 * n;
}

static void widen_stop(struct decoder *d)
{
    (void)d;
}

static uint32_t widen_input_for(const struct decoder *d, uint32_t out)
{
    (void)d;
    return out / 2;
}

static uint32_t widen_buffered(const struct decoder *d)
{
    (void)d;
    return 0;
}

static int widen_finished(const struct decoder *d)
{
    (void)d;
    return 1;
}

static const struct decoder_ops decoder_widen = { "widen", widen_start, widen_stop, widen_run,
                                                  widen_input_for, widen_buffered, widen_finished };

/* What the widening decoder makes of n bytes */
static void widen(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        dst[2 * i] = 0;
        dst[2 * i + 1] = (uint8_t)(src[i] - 128);
    }
}

/* The track strm s just queued is decoded by the widening decoder */
static void use_widen(struct player *p)
{
    p->out.queue[p->out.queued - 1].ops = &decoder_widen;
}

static uint8_t decoded[2 * PCM_BPS];

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
    CHECK(stub_audio[0].len == BLOCK);                  /* 16384 bytes of input */
    finish(&p);
    CHECK(audio_played(&p, stub_now, 0) == BLOCK);
    CHECK(output_elapsed_ms(&p) == BLOCK * 1000 / PCM_BPS);
    play_out(&p);
    widen(decoded, track_a, n);
    to_device(expect, decoded, 2 * n, 0);
    CHECK(stub_audio_out_len == (int)(2 * n) && !memcmp(stub_audio_out, expect, 2 * n));
    CHECK(audio_played(&p, stub_now, 0) == 2 * n && p.out.state == OUTPUT_STOPPED);
    CHECK(stub_audio_bad == 0);
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
    CHECK(stub_audio_out_len == (int)(2 * na + nb) && !memcmp(stub_audio_out, expect, 2 * na + nb));
    CHECK(stub_audio_bad == 0);
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
    stub_audio_abort_played = 1000;
    feed_strm('p', NULL, 0, 0, 0, NULL);
    drain(&p);
    CHECK(audio_played(&p, stub_now, 0) == 1000 && output_elapsed_ms(&p) == 1000 * 1000 / PCM_BPS);
    feed_strm('u', NULL, 0, 0, 0, NULL);
    drain(&p);
    play_out(&p);
    widen(decoded, track_a, n);
    to_device(expect, decoded, 2 * n, 0);
    CHECK(stub_audio_out_len == (int)(2 * n) && !memcmp(stub_audio_out, expect, 2 * n));
    CHECK(stub_audio_bad == 0);
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
    CHECK(nostart_starts == 1 && nostart_used == 0 && stub_audio_out_len == 0 &&
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
    CHECK(nostart_used == 0 && stub_audio_out_len == (int)nb && !memcmp(stub_audio_out, expect, nb));
    CHECK(stub_audio_bad == 0);
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
    CHECK(stub_audio_out_len == (int)(sizeof(track_a) + nc) && !memcmp(stub_audio_out, expect, sizeof(track_a) + nc));
    CHECK(stub_audio_bad == 0);
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
    CHECK(stub_audio_out_len == (int)(na + BLOCK + nc) && !memcmp(stub_audio_out, expect, na + BLOCK + nc));
    CHECK(stub_audio_bad == 0);
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

/* ENV:Sys/ahi.prefs as saved by the AHI 6 preferences in WinUAE (plan step player-10): the Music unit (255), then units
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

/* The player's start-up with the audio output: the unit's mode printed with VERBOSE, the warning for a mode that
 * cannot play stereo right; nothing but the unit when the platform cannot tell the mode (plat_wait() of the stub
 * returns at once, as for a Ctrl-C) */
static void test_start_mode(void)
{
    struct player p;
    struct options o;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.audio = 1;
    o.unit = 3;
    o.verbose = 1;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    stub_audio_fails = 0;
    stub_audio_mode_known = 1;
    memset(&stub_audio_mode, 0, sizeof(stub_audio_mode));
    strcpy(stub_audio_mode.name, "Toccata:16 bit stereo");
    stub_audio_mode.freq = 44100;
    stub_audio_mode.stereo = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK && stub_audio_unit == 3);
    CHECK(strstr(stub_out, "output: AHI unit 3 (Toccata:16 bit stereo, 44100 Hz)\n") != NULL);
    CHECK(strstr(stub_out, "AHI unit 3 uses \"Toccata:16 bit stereo\", a stereo mode without \"++\"") != NULL);

    stub_reset();
    stub_audio_mode.panning = 1;                /* stereo++: the mode printed, no warning */
    o.swap = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(strstr(stub_out, "output: AHI unit 3 (Toccata:16 bit stereo, 44100 Hz), left and right swapped\n") &&
          !strstr(stub_out, "uses \""));

    stub_reset();
    stub_audio_mode_known = 0;                  /* the mode unknown: the unit alone */
    o.swap = 0;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(strstr(stub_out, "output: AHI unit 3\n") && !strstr(stub_out, "uses \""));
}

/* The main loop (run(), through player_main()) when a strm s replaces a stream still connecting, in the turn where
 * the old socket is reported writable: the new socket takes the same number (as on every system that reuses the
 * lowest free one), but that readiness belongs to the old one, so the new stream must not be taken as connected
 * (its HTTP request not sent) before a wait reports it */
static void test_run_stale_ready(void)
{
    struct player p;
    struct options o;
    char fmt[7] = { '1', 'p', '1', '3', '2', '1', 1 };
    static const int turns[][2] = {
        { PLAT_READ, 0 },               /* strm s of A: its length */
        { PLAT_READ, 0 },               /* its body: A connecting */
        { PLAT_READ, 0 },               /* strm s of B: its length */
        { PLAT_READ, PLAT_WRITE },      /* its body: A closed, B connecting; A's socket was writable */
    };

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    feed_strm('s', fmt, 0, 9000, 0, "GET /a HTTP/1.0\r\n\r\n");
    feed_strm('s', fmt, 0, 9000, 0, "GET /b HTTP/1.0\r\n\r\n");
    memcpy(stub_wait, turns, sizeof(turns));
    stub_waits = 4;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(stub_waits == 0 && stub_stream_tx_len == 0);  /* neither request sent: no wait said B connected */
}

/* Workbench start: an icon's ToolTypes turned into a command line for the template */
static void test_tooltypes(void)
{
    static const char *const tt[] = {
        "SERVER=192.168.1.10", "(NAME=Disabled)", "DONOTWAIT", "name=Living room", "QUIET",
        "WINDOW=CON:0/20/640/120/amisqueeze", "verbose", "UNIT=1", "", "LOG=bad\"quote",
    };
    const char *template = "SERVER,PORT/N/K,NAME/K,LOG/K,UNIT/N/K,VERBOSE/S";
    char line[128], tiny[16];

    CHECK(tooltypes_line(template, tt, 10, line, sizeof(line)));
    CHECK(!strcmp(line, "SERVER=192.168.1.10 name=\"Living room\" verbose UNIT=1 \n"));
    CHECK(!tooltypes_line(template, tt, 10, tiny, sizeof(tiny)));             /* does not fit */
    CHECK(tooltypes_line(template, tt, 0, line, sizeof(line)) && !strcmp(line, "\n"));
}

/* Server discovery: the replies of LMS and Music Assistant, and malformed ones */
static int reply(const char *pkt, int len, char *name, int size)
{
    memset(name, 'x', (size_t)size);
    return discovery_reply((const uint8_t *)pkt, len, name, size);
}

static void test_discovery_reply(void)
{
    char name[PLAYER_NAME_LEN + 1], small[4], ma[5 + 2 + 130];

    CHECK(reply("ENAME\005myLMS", 11, name, sizeof(name)) && !strcmp(name, "myLMS"));
    CHECK(reply("EIPAD\00310.NAME\003lms", 17, name, sizeof(name)) && !strcmp(name, "lms"));  /* after a field */
    CHECK(reply("EVERS\0059.1.1", 11, name, sizeof(name)) && !strcmp(name, ""));             /* no NAME */
    CHECK(reply("E", 1, name, sizeof(name)) && !strcmp(name, ""));
    CHECK(reply("ENAM", 4, name, sizeof(name)) && !strcmp(name, ""));                         /* cut in a tag */
    CHECK(reply("ENAME\020abc", 9, name, sizeof(name)) && !strcmp(name, "abc"));             /* past the end */
    CHECK(reply("EIPAD\377NAME\003lms", 14, name, sizeof(name)) && !strcmp(name, ""));        /* skipped past it */
    CHECK(reply("ENAME\004\033[2J", 10, name, sizeof(name)) && !strcmp(name, "?[2J"));         /* a control byte */
    CHECK(reply("ENAME\006abcdef", 12, small, sizeof(small)) && !strcmp(small, "abc"));       /* cut to size */
    /* Music Assistant: the length in characters ("Muesic", 5), the packet in UTF-8 (6 bytes) */
    CHECK(reply("ENAME\005M\303\274sic", 12, name, sizeof(name)) && !strcmp(name, "M??si"));
    /* ... and a length of 128 or more is itself 2 bytes in UTF-8: chr(130) is C2 82 */
    memcpy(ma, "ENAME\302\202", 7);
    memset(ma + 7, 'a', 130);
    CHECK(reply(ma, (int)sizeof(ma), name, sizeof(name)) && strlen(name) == PLAYER_NAME_LEN && name[0] == '?' &&
          name[1] == 'a');
    CHECK(!reply("eNAME\0", 6, name, sizeof(name)));    /* a request, not a reply */
    CHECK(!reply("D", 1, name, sizeof(name)));          /* the old discovery's reply */
    CHECK(!reply("", 0, name, sizeof(name)));
}

int main(void)
{
    test_ring();
    test_ring_truncate();
    test_http_header_end();
    test_mac();
    test_maxrate();
    test_helo();
    test_strm_t();
    test_setd();
    test_quiet_packets();
    test_ignored_packets();
    test_fragmented();
    test_bad_length();
    test_short_packets();
    test_setd_long_name();
    test_setd_not_ascii();
    test_strm_pcm_fields();
    test_strm_autostart();
    test_headers_too_long();
    test_stream_sequence();
    test_read_min_space();
    test_gapless();
    test_gapless_format();
    test_pause();
    test_stop();
    test_connect_failure();
    test_not_pcm();
    test_request_too_long();
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
    test_audio_starved();
    test_audio_errors();
    test_audio_formats();
    test_audio_24bit_pause();
    test_audio_gapless_24bit();
    test_wav_header();
    test_wav_limited();
    test_wav_refused();
    test_wav_audio_8bit();
    test_audio_volume();
    test_audio_volume_pause();
    test_decoder_audio();
    test_decoder_audio_gapless();
    test_decoder_audio_pause();
    test_decoder_virtual();
    test_decoder_no_start();
    test_decoder_no_start_audio();
    test_flush();
    test_flush_paused();
    test_flush_audio();
    test_flush_audio_reached();
    test_flush_current();
    test_request_too_long_playing();
    test_flush_stop();
    test_flush_loaded();
    test_ahi_mode();
    test_start_mode();
    test_run_stale_ready();
    test_lame_header();
    test_tooltypes();
    test_discovery_reply();
    printf("test_core: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
