/*
 * slimproto.c - control connection with the server (TCP 3483), part of the portable core
 *
 * References: squeezelite (slimproto.c/.h) and LMS (Slim/Networking/Slimproto.pm).
 * Asymmetric framing:
 *   server -> client: 2-byte length, then 4-byte opcode and data;
 *   client -> server: 4-byte opcode, 4-byte length, then data.
 * Fields are serialised one by one (no packed structures), big-endian (network order), whatever the CPU.
 *
 * HELO (with the formats decoded here: pcm, flc and mp3 as decided at start-up), STAT, player name (setd),
 * streams (strm s: raw PCM, FLAC, MP3, handled by stream.c and the decoders), stop, flush, pause and unpause,
 * volume (audg); RESP (stream response headers) and DSCO (stream disconnection).
 */
#include <string.h>

#include "platform.h"
#include "player.h"

#define DEVICE_ID_SQUEEZEPLAY 12

/* Advertised in HELO, followed by the formats played: only those really decoded (pcm always, flc and mp3 when
 * decoded here); the server converts the rest */
#define CAPS "Model=squeezelite,ModelName=AmiSqueeze,Firmware=0.4-dev,AccuratePlayPoints=0,HasDigitalOut=0," \
             "MaxSampleRate=48000,"

/* ---- serialisation ---- */

static uint8_t *put8(uint8_t *b, uint32_t v)  { *b++ = (uint8_t)v; return b; }
static uint8_t *put16(uint8_t *b, uint32_t v) { *b++ = (uint8_t)(v >> 8); *b++ = (uint8_t)v; return b; }
static uint8_t *put32(uint8_t *b, uint32_t v)
{
    *b++ = (uint8_t)(v >> 24); *b++ = (uint8_t)(v >> 16); *b++ = (uint8_t)(v >> 8); *b++ = (uint8_t)v;
    return b;
}
static uint8_t *putn(uint8_t *b, const void *s, uint32_t n) { memcpy(b, s, n); return b + n; }

static uint32_t get16(const uint8_t *b) { return (uint32_t)b[0] << 8 | b[1]; }
static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static int send_all(struct player *p, const uint8_t *buf, int len)
{
    while (len > 0) {
        int n = plat_send(p->sock, buf, len);
        if (n <= 0) {
            msg("amisqueeze: send: error %d\n", plat_error());
            return 0;
        }
        buf += n;
        len -= n;
    }
    return 1;
}

/* Client -> server header: opcode + length of the data that follows */
static uint8_t *put_header(uint8_t *b, const char *opcode, uint32_t len)
{
    b = putn(b, opcode, 4);
    return put32(b, len);
}

/* ---- outgoing packets ---- */

static int send_helo(struct player *p, int reconnect)
{
    static const char *const format_lists[4] = { "pcm", "flc,pcm", "pcm,mp3", "flc,pcm,mp3" };
    const char *formats = format_lists[(p->flac ? 1 : 0) | (p->mp3 ? 2 : 0)];
    uint8_t pkt[8 + 36 + sizeof(CAPS) + 12];
    uint8_t *b = put_header(pkt, "HELO", 36 + strlen(CAPS) + strlen(formats));

    b = put8(b, DEVICE_ID_SQUEEZEPLAY);
    b = put8(b, 0);                         /* revision */
    b = putn(b, p->mac, 6);
    memset(b, 0, 16); b += 16;              /* uuid */
    b = put16(b, reconnect ? 0x4000 : 0);   /* wlan_channellist; 0x4000 = reconnection */
    b = put32(b, 0);                        /* bytes received (high word) */
    b = put32(b, 0);                        /* bytes received (low word) */
    b = putn(b, "  ", 2);                   /* language */
    b = putn(b, CAPS, strlen(CAPS));
    b = putn(b, formats, strlen(formats));
    return send_all(p, pkt, (int)(b - pkt));
}

/* server_ts: echoed unchanged (reply to strm t). Single buffer: it is reported as both the stream buffer and
 * the output buffer. */
void slim_send_stat(struct player *p, const char *event, uint32_t server_ts)
{
    uint8_t pkt[8 + 53];
    uint8_t *b = put_header(pkt, "STAT", 53);
    uint32_t elapsed = output_elapsed_ms(p);

    if (p->sock < 0)
        return;
    b = putn(b, event, 4);
    b = put8(b, 0);                         /* num_crlf */
    b = put8(b, 0);                         /* mas_initialized */
    b = put8(b, 0);                         /* mas_mode */
    b = put32(b, p->ring.size);             /* stream buffer size */
    b = put32(b, ring_used(&p->ring));      /* stream buffer fullness */
    b = put32(b, (uint32_t)(p->stream.bytes >> 32));    /* bytes received (high word) */
    b = put32(b, (uint32_t)p->stream.bytes);            /* bytes received (low word) */
    b = put16(b, 0xffff);                   /* signal strength: wired */
    b = put32(b, plat_now_ms());            /* jiffies */
    b = put32(b, p->ring.size);             /* output buffer size */
    b = put32(b, ring_used(&p->ring));      /* output buffer fullness */
    b = put32(b, elapsed / 1000);           /* elapsed seconds */
    b = put16(b, 0);                        /* voltage */
    b = put32(b, elapsed);                  /* elapsed milliseconds */
    b = put32(b, server_ts);
    b = put16(b, 0);                        /* error code */
    if (p->verbose && strncmp(event, "STMt", 4))
        msg("-> STAT %.4s\n", event);
    send_all(p, pkt, (int)(b - pkt));
}

/* Response headers of the stream, passed on to the server */
void slim_send_resp(struct player *p, const char *headers, int len)
{
    uint8_t pkt[8];

    if (p->sock < 0)
        return;
    put_header(pkt, "RESP", (uint32_t)len);
    if (send_all(p, pkt, 8))
        send_all(p, (const uint8_t *)headers, len);
}

/* Stream disconnected: reason DSCO_OK (end of the data), DSCO_LOCAL, DSCO_REMOTE, DSCO_UNREACHABLE */
void slim_send_dsco(struct player *p, int reason)
{
    uint8_t pkt[8 + 1];
    uint8_t *b = put_header(pkt, "DSCO", 1);

    if (p->sock < 0)
        return;
    b = put8(b, (uint32_t)reason);
    if (p->verbose)
        msg("-> DSCO %d\n", reason);
    send_all(p, pkt, (int)(b - pkt));
}

static int send_setd_name(struct player *p)
{
    uint8_t pkt[8 + 1 + PLAYER_NAME_LEN + 1];
    uint32_t n = strlen(p->name) + 1;
    uint8_t *b = put_header(pkt, "SETD", 1 + n);

    b = put8(b, 0);                         /* id 0 = player name */
    b = putn(b, p->name, n);
    return send_all(p, pkt, (int)(b - pkt));
}

/* ---- incoming packets (pkt = opcode + data, len = total length) ---- */

/* PCM sample rates by strm code '0'..'9' (squeezelite pcm.c) */
static const uint32_t pcm_rates[10] = { 11025, 22050, 32000, 44100, 48000, 8000, 12000, 16000, 24000, 96000 };

/* Format and decoder of a strm s; NULL if the format is not supported. Raw PCM ('p') comes with its parameters;
 * FLAC ('f') carries them in its own header (the decoder reads them there). */
static const struct decoder_ops *parse_format(const uint8_t *pkt, struct pcm_format *f)
{
    int size = pkt[7] - '0' + 1;            /* '0'..'3' = 8, 16, 24, 32 bits */
    int rate = pkt[8] - '0';
    int channels = pkt[9] - '0';

    if (pkt[6] == 'f' || pkt[6] == 'm') {   /* a guess until the decoder reads the stream's header */
        f->rate = 44100;
        f->channels = 2;
        f->sample_bytes = 2;
        f->big_endian = (uint8_t)host_big_endian();
#ifdef AMISQUEEZE_MP3
        if (pkt[6] == 'm')
            return &decoder_mp3;
#else
        if (pkt[6] == 'm')
            return NULL;
#endif
        return &decoder_flac;
    }
    if (pkt[6] != 'p' || size < 1 || size > 4 || rate < 0 || rate > 9 || channels < 1 || channels > 2)
        return NULL;
    f->rate = pcm_rates[rate];
    f->channels = (uint8_t)channels;
    f->sample_bytes = (uint8_t)size;
    f->big_endian = pkt[10] == '0';         /* '0' = big-endian, '1' = little-endian */
    return &decoder_pcm;
}

static void process_strm_s(struct player *p, const uint8_t *pkt, int len)
{
    uint32_t ip = get32(pkt + 24);
    uint32_t port = get16(pkt + 22);
    struct pcm_format fmt;
    const struct decoder_ops *ops = parse_format(pkt, &fmt);
    int autostart = pkt[5] - '0';

    if (ip == 0)                            /* 0 = the address of the control connection */
        ip = p->server_ip;
    if (p->verbose) {
        msg("stream: format %c, size %c, rate %c, channels %c, endian %c, autostart %c, threshold %u KB\n",
            pkt[6], pkt[7], pkt[8], pkt[9], pkt[10], pkt[5], (unsigned)pkt[11]);
        msg("stream: http://%u.%u.%u.%u:%u\n",
            (unsigned)(ip >> 24), (unsigned)((ip >> 16) & 0xff), (unsigned)((ip >> 8) & 0xff),
            (unsigned)(ip & 0xff), (unsigned)port);
        msg_raw(pkt + 28, len - 28);        /* HTTP request, sent as is (not NUL-terminated) */
    }
    slim_send_stat(p, "STMf", 0);
    if (ops == NULL) {                      /* raw PCM, FLAC, and MP3 in the builds that have its decoder */
        msg("amisqueeze: stream format %c not supported\n", pkt[6]);
        slim_send_stat(p, "STMn", 0);
        return;
    }
    if (autostart < 0 || autostart > 3)
        autostart = 1;
    if (autostart >= 2) {                   /* would wait for "cont" (radio metadata): not supported yet */
        if (p->verbose)
            msg("stream: autostart %d (cont) handled as %d\n", autostart, autostart - 2);
        autostart -= 2;
    }
    stream_start(p, ip, (uint16_t)port, pkt + 28, len - 28, (uint32_t)pkt[11] * 1024, autostart, &fmt, ops);
}

static void process_strm(struct player *p, const uint8_t *pkt, int len)
{
    char cmd;

    if (len < 28) {
        msg("amisqueeze: strm too short (%d)\n", len);
        return;
    }
    cmd = (char)pkt[4];
    if (p->verbose && cmd != 't')
        msg("<- strm %c\n", cmd);

    switch (cmd) {
    case 't':                               /* timer: the server expects STMt */
        slim_send_stat(p, "STMt", get32(pkt + 18));
        break;
    case 'q':                               /* stop */
    case 'f':                               /* flush */
        stream_stop(p);
        slim_send_stat(p, "STMf", 0);
        break;
    case 'p':                               /* pause; a non-zero interval (synchronised players) is ignored */
        if (get32(pkt + 18) == 0) {
            output_pause(p);
            slim_send_stat(p, "STMp", 0);
        }
        break;
    case 'u':                               /* unpause (or start after STMl) */
        output_resume(p);
        slim_send_stat(p, "STMr", 0);
        break;
    case 'a':                               /* skip ahead (synchronised players): ignored */
        break;
    case 's':                               /* start a stream */
        process_strm_s(p, pkt, len);
        break;
    default:
        msg("amisqueeze: strm %c not handled\n", cmd);
        break;
    }
}

static void process_audg(struct player *p, const uint8_t *pkt, int len)
{
    uint32_t tenths;

    if (len < 22)
        return;
    /* 16.16 gains (0x10000 = 1.0), already following the server's volume curve; adjust = 0: fixed volume */
    if (p->verbose)
        msg("<- audg adjust %d left $%08lX right $%08lX\n", pkt[12],
            (unsigned long)get32(pkt + 14), (unsigned long)get32(pkt + 18));
    if (audio_set_volume(p, get32(pkt + 14), get32(pkt + 18), pkt[12]) && p->verbose) {
        tenths = (p->audio.volume * 1000 + 0x8000) >> 16;  /* volume <= 0x10000: no overflow */
        msg("volume: %lu.%lu %% of full amplitude\n", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
    }
}

static void process_setd(struct player *p, const uint8_t *pkt, int len)
{
    if (len < 5 || pkt[4] != 0)
        return;
    if (len > 5) {                          /* new name chosen on the server */
        int n = len - 5;
        if (n > PLAYER_NAME_LEN)
            n = PLAYER_NAME_LEN;
        memcpy(p->name, pkt + 5, n);
        p->name[n] = '\0';
        msg("player name: %s\n", p->name);
    }
    send_setd_name(p);                      /* answer to the query, or confirmation */
}

static void process(struct player *p, const uint8_t *pkt, int len)
{
    if (len < 4) {
        msg("amisqueeze: packet too short (%d)\n", len);
        return;
    }
    if (!memcmp(pkt, "strm", 4))
        process_strm(p, pkt, len);
    else if (!memcmp(pkt, "audg", 4))
        process_audg(p, pkt, len);
    else if (!memcmp(pkt, "setd", 4))
        process_setd(p, pkt, len);
    else if (!memcmp(pkt, "aude", 4)) {
        if (p->verbose && len >= 6)
            msg("<- aude spdif %d dac %d\n", pkt[4], pkt[5]);
    } else
        msg("amisqueeze: %.4s not handled (%d bytes)\n", (const char *)pkt, len);
}

/* ---- connection ---- */

int slim_connect(struct player *p, int reconnect)
{
    int s = plat_connect(p->server_ip, p->server_port);

    if (s == -1) {
        msg("amisqueeze: socket: error %d\n", plat_error());
        return 0;
    }
    if (s < 0) {
        msg("amisqueeze: cannot connect: error %d\n", plat_error());
        return 0;
    }
    p->sock = s;
    p->rx_expect = 0;
    p->rx_got = 0;
    p->last_rx = plat_now_ms();
    msg("connected to the server\n");
    if (!send_helo(p, reconnect)) {
        slim_disconnect(p);
        return 0;
    }
    return 1;
}

/* Closes the control connection; the current stream stops with it (the server restarts it after HELO) */
void slim_disconnect(struct player *p)
{
    if (p->sock >= 0) {
        plat_close(p->sock);
        p->sock = -1;
    }
    stream_stop(p);
}

int slim_receive(struct player *p)
{
    int n;

    if (p->rx_expect == 0) {                /* packet length */
        n = plat_recv(p->sock, p->rx_len + p->rx_got, 2 - p->rx_got);
        if (n <= 0)
            goto lost;
        p->rx_got += n;
        if (p->rx_got == 2) {
            p->rx_expect = (uint16_t)get16(p->rx_len);
            p->rx_got = 0;
            if (p->rx_expect == 0 || p->rx_expect > sizeof(p->rx_buf)) {
                msg("amisqueeze: invalid packet size (%d)\n", (int)p->rx_expect);
                return 0;
            }
        }
        return 1;
    }

    n = plat_recv(p->sock, p->rx_buf + p->rx_got, p->rx_expect - p->rx_got);
    if (n <= 0)
        goto lost;
    p->rx_got += n;
    if (p->rx_got == p->rx_expect) {
        p->last_rx = plat_now_ms();
        process(p, p->rx_buf, p->rx_expect);
        p->rx_expect = 0;
        p->rx_got = 0;
    }
    return 1;

lost:
    if (n == 0)
        msg("amisqueeze: connection closed by the server\n");
    else
        msg("amisqueeze: recv: error %d\n", plat_error());
    return 0;
}
