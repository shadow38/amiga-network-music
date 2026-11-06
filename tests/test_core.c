/*
 * test_core.c - unit tests of the amisqueeze portable core (host build, see "make test"): what the tests share,
 * and main(), which runs the tests of each test_<module>.c
 *
 * The core runs on stub_platform.c: no network, the packets the player sends are captured and checked byte by
 * byte against the protocol definitions (squeezelite slimproto.h, LMS Slim/Networking/Slimproto.pm), and the
 * clock is set by the tests (virtual output timing). Expected values marked "seen in WinUAE" come from the Amiga
 * build talking to a real LMS 9.1.1.
 */
#include "test_core.h"

int checks, failures;

/* ---- the player, the control connection and the stream ---- */

uint8_t pcm[PCM_BPS];                   /* one second of "audio" */

uint32_t be16(const uint8_t *b) { return (uint32_t)b[0] << 8 | b[1]; }
uint32_t be32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
void set32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16); b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)v;
}

/* Packets sent on the control connection since the last clear, e.g. "STAT:STMf RESP DSCO:0" */
const char *trace(void)
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
const uint8_t *last_stat(const char *event)
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

void clear_tx(void)
{
    stub_tx_len = 0;
    stub_out[0] = '\0';
}

void setup(struct player *p, const char *name)
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

void teardown(struct player *p)
{
    slim_disconnect(p);
    audio_close(p);
    stream_free(p);
}

/* Lets the player read everything queued on the control connection */
void drain(struct player *p)
{
    while (stub_pending() > 0)
        if (!slim_receive(p)) {
            CHECK(!"slim_receive failed");
            break;
        }
}

/* Lets the player read everything queued on the stream connection */
void drain_stream(struct player *p)
{
    int guard = 1000;

    while (stub_stream_pending() > 0 && stream_events(p) && guard--)
        stream_io(p, PLAT_READ);
}

/* strm packet body (after the opcode): 24-byte header, then the HTTP request */
void feed_strm(char cmd, const char *fmt7, uint32_t replay_gain, uint16_t port, uint32_t ip,
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
void strm_pcm(struct player *p, char autostart, int threshold_kb)
{
    char fmt[7] = { autostart, 'p', '1', '3', '2', '1', (char)threshold_kb };

    feed_strm('s', fmt, 0, 9000, 0, request);
    drain(p);
}

/* strm s, connection, response headers: the stream is ready to receive the body */
void open_stream(struct player *p, char autostart, int threshold_kb)
{
    strm_pcm(p, autostart, threshold_kb);
    stream_io(p, PLAT_WRITE);
    stub_feed_stream(http_ok, (int)strlen(http_ok));
    drain_stream(p);
}

/* The capabilities of the HELO in stub_tx contain s */
int helo_caps_have(const char *s)
{
    static char caps[sizeof(stub_tx) + 1];
    int n = stub_tx_len > 44 ? stub_tx_len - 44 : 0;

    memcpy(caps, stub_tx + 44, (size_t)n);
    caps[n] = '\0';
    return strstr(caps, s) != NULL;
}

/* strm s with the given autostart, connected, then the response given (headers and the body's first bytes) */
void answer_stream(struct player *p, char autostart, int threshold_kb, const void *response, int len)
{
    strm_pcm(p, autostart, threshold_kb);
    stream_io(p, PLAT_WRITE);
    clear_tx();
    stub_feed_stream(response, len);
    drain_stream(p);
}

/* ---- tracks, the OUTPUT file and the audio output ---- */

uint8_t track_a[PCM_BPS];               /* 1 s, then 0.5 s of non-periodic bytes: a slip would show */
uint8_t track_b[PCM_BPS / 2];

void fill(uint8_t *b, uint32_t len, uint32_t seed)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        b[i] = (uint8_t)(seed >> 16);
    }
}

/* Plays until the output stops (at most `ms`), in steps of 100 ms like the main loop */
void play_for(struct player *p, uint32_t ms)
{
    for (; ms > 0 && p->out.state == OUTPUT_RUNNING; ms -= 100) {
        stub_now += 100;
        output_tick(p, stub_now);
    }
}

void setup_file(struct player *p)
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

uint8_t expect[2 * 1024 * 1024];

/* What the device must receive: 16-bit samples made big-endian, left and right swapped if asked */
void to_device(uint8_t *dst, const uint8_t *src, uint32_t n, int swap_lr)
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

void setup_audio(struct player *p, int swap)
{
    setup(p, "Amiga");
    stub_audio_fails = 0;
    stub_audio_abort_played = 0;
    CHECK(audio_open(p, AUDIO_AHI, 2, swap) && stub_audio_open && stub_audio_unit == 2);
    stub_audio_clear();
    CHECK(slim_connect(p, 0));
}

/* audg body (after the opcode): old gains (2 x 4 bytes), adjust, preamp, then the 16.16 gains of left and right */
void feed_audg(int adjust, uint32_t left, uint32_t right)
{
    uint8_t b[18];

    memset(b, 0, sizeof(b));
    b[8] = (uint8_t)adjust;
    set32(b + 10, left);
    set32(b + 14, right);
    stub_feed_packet("audg", b, sizeof(b));
}

/* Ends the request playing now, then lets the output collect it and refill */
int finish(struct player *p)
{
    int b = stub_audio_finish();

    stub_now += 10;
    output_tick(p, stub_now);
    return b;
}

int count_in_trace(const char *name)
{
    const char *t = trace();
    int n = 0;

    while ((t = strstr(t, name)) != NULL) {
        n++;
        t += strlen(name);
    }
    return n;
}

/* Plays the rest of the track to its end */
void play_out(struct player *p)
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
uint32_t to_device_fmt(uint8_t *dst, const uint8_t *src, uint32_t n, int sample_bytes, int big_endian)
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

/* ---- the widening decoder ---- */

/* "Widening" decoder, for the tests only: each byte of input becomes a 16-bit little-endian sample
 * ((byte - 128) * 256), so the output is twice the input. With raw PCM, input and output have the same size and a
 * position of the stream used for the output (or the reverse) would go unnoticed; here it shows. */
int widen_start(struct decoder *d, const struct pcm_format *in)
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

void widen_stop(struct decoder *d)
{
    (void)d;
}

uint32_t widen_input_for(const struct decoder *d, uint32_t out)
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
void widen(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        dst[2 * i] = 0;
        dst[2 * i + 1] = (uint8_t)(src[i] - 128);
    }
}

/* The track strm s just queued is decoded by the widening decoder */
void use_widen(struct player *p)
{
    p->out.queue[p->out.queued - 1].ops = &decoder_widen;
}

uint8_t decoded[2 * PCM_BPS];

int main(void)
{
    tests_ring();
    tests_slimproto();
    tests_stream();
    tests_output();
    tests_icy();
    tests_audio();
    tests_paula();
    tests_decoder();
    tests_tracks();
    tests_player();
    printf("test_core: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
