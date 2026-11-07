/*
 * test_player.c - unit tests of player.c (host build, see "make test"): start-up, the main loop, the Amiga
 * start-up's portable parts
 */
#include "test_core.h"

/* PRI: the priority set only when given, -10 to 10; out of range, refused before anything else */
static void test_priority(void)
{
    static const int ok[] = { -10, 0, 10 };
    struct player p;
    struct options o;
    unsigned i;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK && stub_priority == STUB_PRI_NONE);     /* not given: left as it is */
    o.pri_given = 1;
    for (i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        o.pri = ok[i];
        stub_reset();
        memset(&p, 0, sizeof(p));
        CHECK(player_main(&p, &o) == PLAYER_OK && stub_priority == ok[i]);
    }
    o.pri = 11;
    stub_reset();
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_FAIL && stub_priority == STUB_PRI_NONE &&
          strstr(stub_out, "amisqueeze: PRI=11: expected -10 to 10\n") != NULL);
    o.pri = -11;
    stub_reset();
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_FAIL && stub_priority == STUB_PRI_NONE);
}

/* A server not found and ahi.device that cannot be opened with AUDIO=AHI: PLAYER_FAIL with the message (with QUIET, the window
 * opens for it), before any connection */
static void test_start_fail(void)
{
    struct player p;
    struct options o;

    memset(&o, 0, sizeof(o));
    o.server = "nowhere";
    o.port = SLIMPROTO_PORT;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    stub_resolve_fails = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_FAIL && strstr(stub_out, "amisqueeze: server nowhere not found\n") != NULL);
    CHECK(stub_tx_len == 0);

    o.server = "127.0.0.1";
    o.audio = 1;
    o.audio_output = AUDIO_AHI;                 /* without AUDIO, Paula instead (test_start_paula_fallback) */
    stub_reset();
    stub_ahi_fails = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_FAIL && strstr(stub_out, "cannot open ahi.device unit 0") != NULL);
    CHECK(stub_tx_len == 0 && !stub_ahi_open);
    stub_ahi_fails = 0;
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
    stub_ahi_fails = 0;
    stub_ahi_mode_known = 1;
    memset(&stub_ahi_mode, 0, sizeof(stub_ahi_mode));
    strcpy(stub_ahi_mode.name, "Toccata:16 bit stereo");
    stub_ahi_mode.freq = 44100;
    stub_ahi_mode.stereo = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK && stub_ahi_unit == 3);
    CHECK(strstr(stub_out, "output: AHI unit 3 (Toccata:16 bit stereo, 44100 Hz)\n") != NULL);
    CHECK(strstr(stub_out, "AHI unit 3 uses \"Toccata:16 bit stereo\", a stereo mode without \"++\"") != NULL);

    stub_reset();
    stub_ahi_mode.panning = 1;                /* stereo++: the mode printed, no warning */
    o.swap = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(strstr(stub_out, "output: AHI unit 3 (Toccata:16 bit stereo, 44100 Hz), left and right swapped\n") &&
          !strstr(stub_out, "uses \""));

    stub_reset();
    stub_ahi_mode_known = 0;                  /* the mode unknown: the unit alone */
    o.swap = 0;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(strstr(stub_out, "output: AHI unit 3\n") && !strstr(stub_out, "uses \""));
}

/* MAXRATE=AHI and AHISTD with the AHI unit's mode known (Paula at 27429 Hz): the rate asked of the server in HELO,
 * and said */
static void test_start_maxrate(void)
{
    struct player p;
    struct options o;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.audio = 1;
    o.flac = o.mp3 = DECODE_OFF;
    o.maxrate = MAXRATE_AHI;
    stub_reset();
    stub_ahi_fails = 0;
    stub_ahi_mode_known = 1;
    memset(&stub_ahi_mode, 0, sizeof(stub_ahi_mode));
    strcpy(stub_ahi_mode.name, "Paula:14 bit stereo++");
    stub_ahi_mode.freq = 27429;
    stub_ahi_mode.stereo = stub_ahi_mode.panning = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK && helo_caps_have(",MaxSampleRate=27429,"));
    CHECK(strstr(stub_out, "MAXRATE: at most 27429 Hz asked of the server (AHI unit 0 at 27429 Hz)\n") != NULL);

    o.maxrate = MAXRATE_AHISTD;
    stub_reset();
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK && helo_caps_have(",MaxSampleRate=24000,"));
    CHECK(strstr(stub_out, "MAXRATE: at most 24000 Hz asked of the server (AHI unit 0 at 27429 Hz)\n") != NULL);
    stub_ahi_mode_known = 0;
}

/* AUDIO's values, any case */
static void test_parse_audio_output(void)
{
    CHECK(parse_audio_output("AHI") == AUDIO_AHI && parse_audio_output("paula") == AUDIO_PAULA &&
          parse_audio_output("Paula8") == AUDIO_PAULA8);
    CHECK(parse_audio_output("") < 0 && parse_audio_output("PAULA14") < 0 && parse_audio_output("AHI ") < 0);
}

/* A start-up with these options; 1 if player_main() returned rc */
static int start_with(struct player *p, const struct options *o, int rc)
{
    stub_reset();
    stub_paula_volumes = 0;
    memset(p, 0, sizeof(*p));
    return player_main(p, o) == rc;
}

/* AUDIO=PAULA and PAULA8: Paula opened (its volume set), its highest rate asked of the server, PAL and NTSC; SWAP
 * said not to be needed; MAXRATE=AHI said and replaced, MAXRATE=<Hz> kept */
static void test_start_paula(void)
{
    struct player p;
    struct options o;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.audio = 1;
    o.audio_output = AUDIO_PAULA;
    o.unit = 3;                                 /* unused by Paula */
    o.verbose = 1;
    o.flac = o.mp3 = DECODE_OFF;
    CHECK(start_with(&p, &o, PLAYER_OK) && stub_paula_volumes > 0 && !stub_ahi_open);
    CHECK(strstr(stub_out, "output: Paula direct, 14 bits\n") && helo_caps_have(",MaxSampleRate=28603,") &&
          strstr(stub_out, "MAXRATE: at most 28603 Hz asked of the server, Paula's highest rate\n"));
    stub_paula_clock = 3579545;
    CHECK(start_with(&p, &o, PLAYER_OK) && helo_caps_have(",MaxSampleRate=28867,"));
    stub_paula_clock = 3546895;

    o.audio_output = AUDIO_PAULA8;
    o.swap = 1;
    o.maxrate = MAXRATE_AHI;
    CHECK(start_with(&p, &o, PLAYER_OK) && helo_caps_have(",MaxSampleRate=28603,"));
    CHECK(strstr(stub_out, "output: Paula direct, 8 bits, channels 2 and 3 left free, left and right swapped\n") &&
          strstr(stub_out, "SWAP is not needed with AUDIO=PAULA8, whose left and right are in place"));
    CHECK(strstr(stub_out, "MAXRATE=AHI: no AHI with Paula direct: at most 28603 Hz asked of the server"));
    o.swap = 0;
    o.maxrate = 22050;
    CHECK(start_with(&p, &o, PLAYER_OK) && helo_caps_have(",MaxSampleRate=22050,") && !strstr(stub_out, "SWAP"));

    stub_paula_fails = 1;                       /* a channel in use: the start-up fails */
    CHECK(start_with(&p, &o, PLAYER_FAIL) && stub_tx_len == 0);
    stub_paula_fails = 0;
}

/* Without AUDIO, ahi.device missing gives Paula with 14 bits, said; AUDIO=AHI keeps the error; NOAUDIO and OUTPUT
 * win over AUDIO */
static void test_start_paula_fallback(void)
{
    struct player p;
    struct options o;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.audio = 1;
    o.flac = o.mp3 = DECODE_OFF;
    stub_ahi_fails = 1;
    CHECK(start_with(&p, &o, PLAYER_OK) && stub_paula_volumes > 0 && helo_caps_have(",MaxSampleRate=28603,"));
    CHECK(strstr(stub_out, "cannot open ahi.device unit 0") &&
          strstr(stub_out, "amisqueeze: playing on Paula directly instead (AUDIO=PAULA)\n"));
    o.audio_output = AUDIO_AHI;
    CHECK(start_with(&p, &o, PLAYER_FAIL) && stub_paula_volumes == 0 && !strstr(stub_out, "Paula"));
    stub_ahi_fails = 0;

    o.audio_output = AUDIO_PAULA;
    o.audio = 0;                                /* NOAUDIO */
    CHECK(start_with(&p, &o, PLAYER_OK) && stub_paula_volumes == 0 && helo_caps_have(",MaxSampleRate=48000,"));
    o.audio = 1;
    o.output = "file";
    CHECK(start_with(&p, &o, PLAYER_OK) && stub_paula_volumes == 0 && helo_caps_have(",MaxSampleRate=48000,"));
}

/* The player's start-up: the AHI mode, Paula, MAXRATE, PRI, the errors */
static void test_start(void)
{
    test_start_mode();
    test_parse_audio_output();
    test_start_paula();
    test_start_paula_fallback();
    test_start_maxrate();
    test_priority();
    test_start_fail();
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

/* The control connection reported readable with nothing to read (AmiNetXDuo counts segments
 * that arrived out of order): neither an error nor read again at once, but left out of the waits for
 * AGAIN_WAIT_MS, said once with VERBOSE; then read as usual */
static void test_run_again(void)
{
    static const char said[] = "control: reported readable, nothing to read yet\n";
    struct player p;
    struct options o;
    const char *m;
    static const int turns[][2] = {
        { PLAT_READ, 0 },               /* readable, nothing to read */
        { PLAT_READ, 0 },               /* the control connection left out of this wait (100 ms) */
        { PLAT_READ, 0 },               /* readable, nothing to read again */
        { PLAT_READ, 0 },               /* left out again */
        { PLAT_READ, 0 },               /* strm t: its length */
        { PLAT_READ, 0 },               /* its body: STMt */
    };

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.verbose = 1;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    feed_strm('t', NULL, 0, 0, 0, NULL);
    memcpy(stub_wait, turns, sizeof(turns));
    stub_waits = 6;
    stub_wait_advances = 1;
    stub_again = 2;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(stub_waits == 0 && stub_waits_without_control == 2 && stub_again == 0);
    CHECK(strstr(trace(), "STAT:STMt") != NULL);
    CHECK((m = strstr(stub_out, said)) != NULL && strstr(m + 1, said) == NULL);
    CHECK(!strstr(stub_out, "recv: error") && !strstr(stub_out, "connection closed") && !strstr(stub_out, "retrying"));
}

/* The control connection reported readable with nothing to read at every wait (AmiNetXDuo after a cable cut):
 * after RX_TIMEOUT_MS without a packet, "no news from the server" and a new connection all the
 * same. 80 waits of 1 s and 100 ms in turn: 44 s. */
static void test_run_again_no_news(void)
{
    static const char said[] = "amisqueeze: no news from the server, reconnecting\n";
    struct player p;
    struct options o;
    const char *m;
    int i;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    for (i = 0; i < 8; i++) {
        stub_wait[i][0] = PLAT_READ;
        stub_wait[i][1] = 0;
    }
    stub_waits = 80;
    stub_wait_advances = 1;
    stub_again = 1000;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(stub_waits == 0 && stub_again > 0);
    CHECK((m = strstr(stub_out, said)) != NULL && strstr(m + 1, said) == NULL);
    CHECK(count_in_trace("HELO") == 2);
}

/* A send that fails on the control connection (the packet may be cut short): nothing more is sent on it, and the
 * main loop takes it as lost: a new connection, after the wait of a short session */
static void test_send_failed(void)
{
    struct player p;
    struct options o;
    int i;

    setup(&p, "Amiga");
    CHECK(slim_connect(&p, 0));
    clear_tx();
    stub_send_fail_in = 1;
    slim_send_stat(&p, "STMt", 0);
    slim_send_dsco(&p, DSCO_OK);
    CHECK(p.send_failed && stub_tx_len == 0 && strstr(stub_out, "amisqueeze: send: error") != NULL);
    teardown(&p);

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    feed_strm('t', NULL, 0, 0, 0, NULL);                /* its STMt fails */
    memset(stub_wait, 0, sizeof(stub_wait));
    for (i = 0; i < 2; i++)
        stub_wait[i][0] = PLAT_READ;                    /* strm t: its length, its body */
    stub_waits = 2 + 50 + 1;                            /* then "retrying in 5 s", then the new connection */
    stub_wait_advances = 1;
    stub_send_fail_in = 2;                              /* HELO, then STMt */
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK && stub_waits == 0);
    CHECK(strstr(stub_out, "amisqueeze: send: error") && strstr(stub_out, "retrying in 5 s"));
    CHECK(!strstr(stub_out, "no news"));                /* at once, not after the 35 s without news */
    CHECK(count_in_trace("HELO") == 2 && count_in_trace("STAT") == 0);
}

/* Ctrl-C while connecting, the call interrupted (EINTR): the player stops at once, without a
 * failure reported nor a retry */
static void test_run_interrupted(void)
{
    struct player p;
    struct options o;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.flac = o.mp3 = DECODE_OFF;
    stub_reset();
    stub_connect_interrupted = 1;
    memset(&p, 0, sizeof(p));
    CHECK(player_main(&p, &o) == PLAYER_OK);
    CHECK(strstr(stub_out, "stopped\n") != NULL);
    CHECK(!strstr(stub_out, "cannot connect") && !strstr(stub_out, "retrying"));
}

/* Ctrl-C during a receive, then during a send on the control connection (EINTR): the player stops at once, without
 * an error said nor a new connection */
static void test_run_interrupted_io(void)
{
    struct player p;
    struct options o;
    int send;

    memset(&o, 0, sizeof(o));
    o.server = "127.0.0.1";
    o.port = SLIMPROTO_PORT;
    o.flac = o.mp3 = DECODE_OFF;
    for (send = 0; send < 2; send++) {
        stub_reset();
        feed_strm('t', NULL, 0, 0, 0, NULL);
        memset(stub_wait, 0, sizeof(stub_wait));
        stub_wait[0][0] = stub_wait[1][0] = PLAT_READ;
        stub_waits = 8;
        if (send) {
            stub_send_fail_in = 2;                      /* HELO, then STMt */
            stub_send_interrupted = 1;
        } else
            stub_recv_interrupted = 1;
        memset(&p, 0, sizeof(p));
        CHECK(player_main(&p, &o) == PLAYER_OK && strstr(stub_out, "stopped\n") != NULL);
        CHECK(!strstr(stub_out, "error") && !strstr(stub_out, "retrying") && count_in_trace("HELO") == 1);
    }
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

/* The window that Exchange shows and hides: its specification without AUTO and WAIT */
static void test_window_spec(void)
{
    char spec[64], tiny[18];

    CHECK(window_spec("CON:0/20/640/120/amisqueeze/AUTO/CLOSE/WAIT/SMART", spec, sizeof(spec)));
    CHECK(!strcmp(spec, "CON:0/20/640/120/amisqueeze/CLOSE/SMART"));
    CHECK(window_spec("CON:0/0/640/100/Wait/wait/auto/Autox", spec, sizeof(spec)));   /* the title stays */
    CHECK(!strcmp(spec, "CON:0/0/640/100/Wait/Autox"));
    CHECK(window_spec("CON:////x/AUTO/", spec, sizeof(spec)) && !strcmp(spec, "CON:////x"));
    CHECK(window_spec("CON:", spec, sizeof(spec)) && !strcmp(spec, "CON:"));
    CHECK(window_spec("", spec, sizeof(spec)) && !strcmp(spec, ""));
    CHECK(window_spec("CON:0/0/640/100/t/WAIT", tiny, sizeof(tiny)) && !strcmp(tiny, "CON:0/0/640/100/t"));
    CHECK(!window_spec("CON:0/0/640/100/ti", tiny, sizeof(tiny)));                  /* one too many */
    CHECK(!window_spec("", tiny, 0) && window_spec("", tiny, 1) && !strcmp(tiny, ""));     /* room for the NUL */
}

/* The messages kept for the window: in order, and from a line start once the ring went round */
static void test_history(void)
{
    static struct history h;
    static char all[4096], got[HISTORY_SIZE];
    const char *part[2];
    int len[2], n = 0, i;

    history_text(&h, part, len);
    CHECK(len[0] == 0 && len[1] == 0);
    history_add(&h, "one\ntwo\n", 8);
    history_text(&h, part, len);
    CHECK(len[0] == 8 && len[1] == 0 && !memcmp(part[0], "one\ntwo\n", 8));
    memset(&h, 0, sizeof(h));
    for (i = 0; n < 3000; i++) {        /* "line 0001\n" ...: the ring goes round, the text kept across its end */
        int k = sprintf(all + n, "line %04d\n", i);
        history_add(&h, all + n, k);
        n += k;
    }
    history_text(&h, part, len);
    CHECK(h.full && len[1] > 0 && len[0] + len[1] <= HISTORY_SIZE && len[0] + len[1] >= HISTORY_SIZE - 10);
    memcpy(got, part[0], (size_t)len[0]);
    memcpy(got + len[0], part[1], (size_t)len[1]);
    CHECK(!memcmp(got, all + n - (len[0] + len[1]), (size_t)(len[0] + len[1])));   /* the newest bytes */
    CHECK(!memcmp(got, "line ", 5));                                               /* a whole line first */
}

/* The close gadget's event read from a console window in RAW mode, as AmigaOS 3.2 sends it, cut anywhere */
static void test_console_closed(void)
{
    static const char ev[] = "\x9b" "11;0;0;32768;0;0;1540921724;254127|";
    static const char other[] = "\x9b" "1;0|\x9b" "111;0|\x9b" "2;11;0|11;x\x03";
    int len = (int)sizeof(ev) - 1, cut, state;

    for (cut = 0; cut <= len; cut++) {
        int got;

        state = 0;
        got = console_closed(&state, "x", 1);
        got |= console_closed(&state, ev, cut);
        got |= console_closed(&state, ev + cut, len - cut);
        CHECK(got);
    }
    state = 0;
    CHECK(console_closed(&state, "\x1b[11;0|", 7));                             /* ESC [ */
    state = 0;
    CHECK(!console_closed(&state, other, (int)sizeof(other) - 1));                /* other events, keys */
    CHECK(!console_closed(&state, "\x9b" "11", 3) && console_closed(&state, "\x9b" "11;", 4));
}

static void test_workbench(void)
{
    test_tooltypes();
    test_window_spec();
    test_history();
    test_console_closed();
}

void tests_player(void)
{
    test_start();
    test_run_stale_ready();
    test_run_again();
    test_run_again_no_news();
    test_send_failed();
    test_run_interrupted();
    test_run_interrupted_io();
    test_workbench();
}
