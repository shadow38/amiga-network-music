/*
 * test_core.c - unit tests of the amisqueeze portable core (host build, see "make test")
 *
 * The core runs on stub_platform.c: no network, the packets the player sends are captured and checked byte by
 * byte against the protocol definitions (squeezelite slimproto.h, LMS Slim/Networking/Slimproto.pm). Expected
 * values marked "seen in WinUAE" come from the Amiga build talking to a real LMS 9.1.1.
 */
#include <stdio.h>
#include <string.h>

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

static uint32_t be16(const uint8_t *b) { return (uint32_t)b[0] << 8 | b[1]; }
static uint32_t be32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static void set32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
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
    stub_now = 1000;
}

/* Lets the player read everything queued */
static void drain(struct player *p)
{
    while (stub_pending() > 0)
        if (!slim_receive(p)) {
            CHECK(!"slim_receive failed");
            break;
        }
}

/* strm packet body (after the opcode): 24-byte header, then the HTTP request */
static void feed_strm(char cmd, const char *fmt7, uint32_t replay_gain, uint16_t port, uint32_t ip,
                      const char *request)
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
    if (request) {
        memcpy(b + 24, request, strlen(request));
        n += (int)strlen(request);
    }
    stub_feed_packet("strm", b, n);
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
    stub_reset();
    CHECK(slim_connect(&p, 1));
    CHECK(be16(stub_tx + 32) == 0x4000);                /* reconnection */
}

static void test_strm_t(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    stub_reset();
    stub_now = 0x01020304;
    feed_strm('t', NULL, 0xDEADBEEF, 0, 0, NULL);
    drain(&p);
    CHECK(stub_tx_len == 8 + 53);
    CHECK(!memcmp(stub_tx, "STAT", 4) && be32(stub_tx + 4) == 53);
    CHECK(!memcmp(stub_tx + 8, "STMt", 4));
    CHECK(be16(stub_tx + 31) == 0xffff);                /* signal strength: wired */
    CHECK(be32(stub_tx + 33) == 0x01020304);            /* jiffies */
    CHECK(be32(stub_tx + 55) == 0xDEADBEEF);            /* server timestamp echoed */
    CHECK(be16(stub_tx + 59) == 0);                     /* error code */
}

static void test_strm_s(void)
{
    struct player p;
    const char *req = "GET /stream.mp3?player=02:41:4d:d4:ca:84 HTTP/1.0\r\n\r\n";

    setup(&p, "Amiga");
    p.verbose = 1;
    CHECK(slim_connect(&p, 0));
    stub_reset();
    /* As seen in WinUAE for an MP3 transcoded by LMS: PCM 16 bits, 44.1 kHz, stereo, little-endian */
    feed_strm('s', "1p1321\xff", 0, 9002, 0, req);
    drain(&p);
    CHECK(strstr(stub_out, "stream: format p, size 1, rate 3, channels 2, endian 1, autostart 1\n") != NULL);
    CHECK(strstr(stub_out, "stream: http://127.0.0.1:9002\n") != NULL);   /* address 0 = control connection */
    CHECK(strstr(stub_out, req) != NULL);
    CHECK(stub_tx_len == 2 * (8 + 53));
    CHECK(!memcmp(stub_tx + 8, "STMf", 4));
    CHECK(!memcmp(stub_tx + 61 + 8, "STMn", 4));
}

static void test_setd(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    stub_reset();
    stub_feed_packet("setd", "\0Renamed", 8);           /* id 0 = player name */
    drain(&p);
    CHECK(!strcmp(p.name, "Renamed"));
    CHECK(!memcmp(stub_tx, "SETD", 4) && be32(stub_tx + 4) == 1 + 8);
    CHECK(stub_tx[8] == 0 && !memcmp(stub_tx + 9, "Renamed", 8));   /* NUL-terminated */

    stub_reset();
    stub_feed_packet("setd", "\0", 1);                  /* query: the player answers with its name */
    drain(&p);
    CHECK(!memcmp(stub_tx, "SETD", 4) && !memcmp(stub_tx + 9, "Renamed", 8));
}

static void test_quiet_packets(void)
{
    struct player p;
    uint8_t audg[18];

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    stub_reset();
    memset(audg, 0, sizeof(audg));
    stub_feed_packet("audg", audg, sizeof(audg));
    stub_feed_packet("aude", "\1\1", 2);
    drain(&p);
    CHECK(stub_tx_len == 0);                            /* nothing to answer */
    CHECK(stub_out[0] == '\0');                         /* nothing printed without VERBOSE */
}

/* Packets split across reads, one byte at a time */
static void test_fragmented(void)
{
    struct player p;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    stub_reset();
    stub_chunk = 1;
    feed_strm('t', NULL, 0x11223344, 0, 0, NULL);
    feed_strm('t', NULL, 0x55667788, 0, 0, NULL);
    drain(&p);
    CHECK(stub_tx_len == 2 * (8 + 53));
    CHECK(be32(stub_tx + 55) == 0x11223344);
    CHECK(be32(stub_tx + 61 + 55) == 0x55667788);
}

static void test_bad_length(void)
{
    struct player p;
    static const uint8_t zero[2] = { 0, 0 };
    static const uint8_t huge[2] = { 0xff, 0xff };

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    stub_reset();
    stub_feed(zero, 2);
    CHECK(slim_receive(&p) == 0);                       /* length 0: invalid */

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    stub_reset();
    stub_feed(huge, 2);
    CHECK(slim_receive(&p) == 0);                       /* larger than the receive buffer */
    CHECK(strstr(stub_out, "invalid packet size") != NULL);
}

int main(void)
{
    test_mac();
    test_helo();
    test_strm_t();
    test_strm_s();
    test_setd();
    test_quiet_packets();
    test_fragmented();
    test_bad_length();
    printf("test_core: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
