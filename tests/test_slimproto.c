/*
 * test_slimproto.c - unit tests of slimproto.c (host build, see "make test"): the control connection's packets,
 * HELO, setd, strm s's fields, the server discovery
 */
#include "test_core.h"

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
    CHECK(parse_maxrate("4294989346") == 0);            /* 2^32 + 22050: refused, not wrapped to 22050 */
    CHECK(maxrate_from_ahi(MAXRATE_AHI, 27429) == 27429 && maxrate_from_ahi(MAXRATE_AHISTD, 27429) == 24000);
    CHECK(maxrate_from_ahi(MAXRATE_AHISTD, 22050) == 22050 && maxrate_from_ahi(MAXRATE_AHISTD, 22049) == 16000);
    CHECK(maxrate_from_ahi(MAXRATE_AHISTD, 44099) == 32000 && maxrate_from_ahi(MAXRATE_AHISTD, 8000) == 8000);
    CHECK(maxrate_from_ahi(MAXRATE_AHISTD, 48000) == 48000 && maxrate_from_ahi(MAXRATE_AHI, 44100) == 44100);
    CHECK(maxrate_from_ahi(MAXRATE_AHI, 5000) == 8000 && maxrate_from_ahi(MAXRATE_AHISTD, 5000) == 8000);
    CHECK(maxrate_from_ahi(MAXRATE_AHI, 96000) == 48000 && maxrate_from_ahi(MAXRATE_AHISTD, 96000) == 48000);
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
    static const char *const ignored[] = { "vers", "grfb", "grfe", "codc" };
    struct player p;
    char line[40];
    int i;

    for (i = 0; i < 4; i++) {
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

    stub_feed_packet("aude", "\1\1", 2);
    drain(&p);
    CHECK(!strcmp(stub_out, "<- aude spdif 1 dac 1\n"));
    clear_tx();
    stub_feed_packet("aude", "\0", 1);                  /* 1 byte of the 2 an aude has; the previous one's follows */
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

/* A name the server sends in UTF-8 ("Sejour" with an e acute), or with a control byte (0x1B, 0x7F): shown with '?'
 * for each byte that is not printable ASCII, kept and sent back as received (the server would rename the player
 * otherwise) */
static void test_setd_not_ascii(void)
{
    struct player p;
    static const char name[] = "\0S\xc3\xa9jour\x1b[2J\x7f";

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_feed_packet("setd", name, sizeof(name) - 1);
    drain(&p);
    CHECK(!strcmp(stub_out, "player name: S??jour?[2J?\n"));
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
        { '1', 'p', '1', ';', '2', '1', 10 },          /* rate ';': past the table (176.4 kHz) */
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

/* Every rate code of raw PCM, '0' to ':' (LMS's and aioslimproto's tables) */
static void test_strm_pcm_rates(void)
{
    static const uint32_t rates[] = { 11025, 22050, 32000, 44100, 48000, 8000, 12000, 16000, 24000, 96000, 88200 };
    struct player p;
    unsigned i;

    for (i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        char fmt[7] = { '1', 'p', '1', '3', '2', '1', 10 };

        fmt[3] = (char)('0' + i);
        setup(&p, "Amiga");
        CHECK(slim_connect(&p, 0));
        clear_tx();
        feed_strm('s', fmt, 0, 9000, 0, request);
        drain(&p);
        CHECK(!strcmp(trace(), "STAT:STMf") && p.stream.state == STREAM_CONNECTING);
        CHECK(p.out.queued == 1 && p.out.queue[0].fmt.rate == rates[i]);
        teardown(&p);
    }
}

/* An autostart out of range ('0' to '3'): taken as 1 */
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
        CHECK(p.stream.autostart == 1);
        teardown(&p);
    }
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

/* Nine servers answering, each from its own address: the first eight kept (the table on the stack holds eight), the
 * ninth left out, the first one chosen */
static void test_discovery_nine(void)
{
    struct player p;

    stub_reset();
    memset(&p, 0, sizeof(p));
    p.server_port = SLIMPROTO_PORT;
    stub_udp_replies = 9;
    CHECK(slim_discover(&p) == 1 && p.server_ip == 0x7f000002);
    CHECK(strstr(stub_out, "server \"A\" found at 127.0.0.2\n") &&
          strstr(stub_out, "server \"H\" found at 127.0.0.9\n"));
    CHECK(!strstr(stub_out, "127.0.0.10") && !strstr(stub_out, "error"));
    CHECK(strstr(stub_out, "8 servers found: connecting to the first") != NULL);
}

void tests_slimproto(void)
{
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
    test_strm_pcm_rates();
    test_strm_autostart();
    test_discovery_reply();
    test_discovery_nine();
}
