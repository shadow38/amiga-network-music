/*
 * slimproto.c - control connection with the server (TCP 3483)
 *
 * References: squeezelite (slimproto.c/.h) and LMS (Slim/Networking/Slimproto.pm).
 * Asymmetric framing:
 *   server -> client: 2-byte length, then 4-byte opcode and data;
 *   client -> server: 4-byte opcode, 4-byte length, then data.
 * Fields are serialised one by one (no packed structures); everything is big-endian, like the 68k.
 *
 * Plan step 1.1: HELO, STAT (STMt, STMf), player name (setd). Streams (strm s) are refused (STMn).
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>

#include <string.h>

#include "amisqueeze.h"

#define DEVICE_ID_SQUEEZEPLAY 12

/* Advertised in HELO. Only list formats actually played: pcm only, the server transcodes the rest. */
#define CAPS "Model=squeezelite,ModelName=AmiSqueeze,Firmware=0.1,AccuratePlayPoints=0,HasDigitalOut=0," \
             "MaxSampleRate=48000,pcm"

/* ---- serialisation ---- */

static UBYTE *put8(UBYTE *b, ULONG v)  { *b++ = v; return b; }
static UBYTE *put16(UBYTE *b, ULONG v) { *b++ = v >> 8; *b++ = v; return b; }
static UBYTE *put32(UBYTE *b, ULONG v) { *b++ = v >> 24; *b++ = v >> 16; *b++ = v >> 8; *b++ = v; return b; }
static UBYTE *putn(UBYTE *b, const void *s, ULONG n) { memcpy(b, s, n); return b + n; }

static ULONG get16(const UBYTE *b) { return (ULONG)b[0] << 8 | b[1]; }
static ULONG get32(const UBYTE *b) { return (ULONG)b[0] << 24 | (ULONG)b[1] << 16 | (ULONG)b[2] << 8 | b[3]; }

static BOOL send_all(struct player *p, const UBYTE *buf, LONG len)
{
    while (len > 0) {
        LONG n = send(p->sock, (APTR)buf, len, 0);
        if (n <= 0) {
            Printf("amisqueeze: send: error %ld\n", Errno());
            return FALSE;
        }
        buf += n;
        len -= n;
    }
    return TRUE;
}

/* Client -> server header: opcode + length of the data that follows */
static UBYTE *put_header(UBYTE *b, const char *opcode, ULONG len)
{
    b = putn(b, opcode, 4);
    return put32(b, len);
}

/* ---- outgoing packets ---- */

static BOOL send_helo(struct player *p, BOOL reconnect)
{
    UBYTE pkt[8 + 36 + sizeof(CAPS)];
    UBYTE *b = put_header(pkt, "HELO", 36 + strlen(CAPS));

    b = put8(b, DEVICE_ID_SQUEEZEPLAY);
    b = put8(b, 0);                         /* revision */
    b = putn(b, p->mac, 6);
    memset(b, 0, 16); b += 16;              /* uuid */
    b = put16(b, reconnect ? 0x4000 : 0);   /* wlan_channellist; 0x4000 = reconnection */
    b = put32(b, 0);                        /* bytes received (high word) */
    b = put32(b, 0);                        /* bytes received (low word) */
    b = putn(b, "  ", 2);                   /* language */
    b = putn(b, CAPS, strlen(CAPS));
    return send_all(p, pkt, b - pkt);
}

/* server_ts: echoed unchanged (reply to strm t) */
static BOOL send_stat(struct player *p, const char *event, ULONG server_ts)
{
    UBYTE pkt[8 + 53];
    UBYTE *b = put_header(pkt, "STAT", 53);

    b = putn(b, event, 4);
    b = put8(b, 0);                         /* num_crlf */
    b = put8(b, 0);                         /* mas_initialized */
    b = put8(b, 0);                         /* mas_mode */
    b = put32(b, 0);                        /* stream buffer size */
    b = put32(b, 0);                        /* stream buffer fullness */
    b = put32(b, 0);                        /* bytes received (high word) */
    b = put32(b, 0);                        /* bytes received (low word) */
    b = put16(b, 0xffff);                   /* signal strength: wired */
    b = put32(b, now_ms());                 /* jiffies */
    b = put32(b, 0);                        /* output buffer size */
    b = put32(b, 0);                        /* output buffer fullness */
    b = put32(b, 0);                        /* elapsed seconds */
    b = put16(b, 0);                        /* voltage */
    b = put32(b, 0);                        /* elapsed milliseconds */
    b = put32(b, server_ts);
    b = put16(b, 0);                        /* error code */
    if (p->verbose)
        Printf("-> STAT %.4s\n", (LONG)event);
    return send_all(p, pkt, b - pkt);
}

static BOOL send_setd_name(struct player *p)
{
    UBYTE pkt[8 + 1 + PLAYER_NAME_LEN + 1];
    ULONG n = strlen(p->name) + 1;
    UBYTE *b = put_header(pkt, "SETD", 1 + n);

    b = put8(b, 0);                         /* id 0 = player name */
    b = putn(b, p->name, n);
    return send_all(p, pkt, b - pkt);
}

/* ---- incoming packets (pkt = opcode + data, len = total length) ---- */

static void process_strm(struct player *p, const UBYTE *pkt, ULONG len)
{
    char cmd;

    if (len < 28) {
        Printf("amisqueeze: strm too short (%ld)\n", len);
        return;
    }
    cmd = pkt[4];
    if (p->verbose && cmd != 't')
        Printf("<- strm %lc\n", (LONG)cmd);

    switch (cmd) {
    case 't':                               /* timer: the server expects STMt */
        send_stat(p, "STMt", get32(pkt + 18));
        break;
    case 'q':                               /* stop */
        send_stat(p, "STMf", 0);
        break;
    case 'f':                               /* flush */
        send_stat(p, "STMf", 0);
        break;
    case 'p':                               /* pause */
        send_stat(p, "STMp", 0);
        break;
    case 'u':                               /* unpause */
        send_stat(p, "STMr", 0);
        break;
    case 's': {                             /* start a stream */
        ULONG ip = get32(pkt + 24);
        ULONG port = get16(pkt + 22);

        if (ip == 0)
            ip = p->server_ip;
        Printf("stream: format %lc, size %lc, rate %lc, channels %lc, endian %lc, autostart %lc\n",
               (LONG)pkt[6], (LONG)pkt[7], (LONG)pkt[8], (LONG)pkt[9], (LONG)pkt[10], (LONG)pkt[5]);
        Printf("stream: http://%ld.%ld.%ld.%ld:%ld\n",
               ip >> 24, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, port);
        /* HTTP request to send as is (RawDoFmt has no %.*s) */
        Write(Output(), (APTR)(pkt + 28), len - 28);
        /* Playback not implemented yet */
        send_stat(p, "STMf", 0);
        send_stat(p, "STMn", 0);
        break;
    }
    default:
        Printf("amisqueeze: strm %lc not handled\n", (LONG)cmd);
        break;
    }
}

static void process_audg(struct player *p, const UBYTE *pkt, ULONG len)
{
    if (len < 22)
        return;
    /* 16.16 gain, 0x10000 = 1.0; adjust = 0: fixed volume */
    if (p->verbose)
        Printf("<- audg adjust %ld left $%08lx right $%08lx\n", (LONG)pkt[12], get32(pkt + 14), get32(pkt + 18));
}

static void process_setd(struct player *p, const UBYTE *pkt, ULONG len)
{
    if (len < 5 || pkt[4] != 0)
        return;
    if (len > 5) {                          /* new name chosen on the server */
        ULONG n = len - 5;
        if (n > PLAYER_NAME_LEN)
            n = PLAYER_NAME_LEN;
        memcpy(p->name, pkt + 5, n);
        p->name[n] = '\0';
        Printf("player name: %s\n", (LONG)p->name);
    }
    send_setd_name(p);                      /* answer to the query, or confirmation */
}

static void process(struct player *p, const UBYTE *pkt, ULONG len)
{
    if (len < 4) {
        Printf("amisqueeze: packet too short (%ld)\n", len);
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
            Printf("<- aude spdif %ld dac %ld\n", (LONG)pkt[4], (LONG)pkt[5]);
    } else
        Printf("amisqueeze: %.4s not handled (%ld bytes)\n", (LONG)pkt, len);
}

/* ---- connection ---- */

BOOL slim_connect(struct player *p, BOOL reconnect)
{
    struct sockaddr_in sa;

    p->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (p->sock < 0) {
        Printf("amisqueeze: socket: error %ld\n", Errno());
        return FALSE;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = p->server_port;
    sa.sin_addr.s_addr = p->server_ip;
    if (connect(p->sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        Printf("amisqueeze: cannot connect: error %ld\n", Errno());
        slim_disconnect(p);
        return FALSE;
    }
    p->rx_expect = 0;
    p->rx_got = 0;
    p->last_rx = now_ms();
    Printf("connected to the server\n");
    if (!send_helo(p, reconnect)) {
        slim_disconnect(p);
        return FALSE;
    }
    return TRUE;
}

void slim_disconnect(struct player *p)
{
    if (p->sock >= 0) {
        CloseSocket(p->sock);
        p->sock = -1;
    }
}

BOOL slim_receive(struct player *p)
{
    LONG n;

    if (p->rx_expect == 0) {                /* packet length */
        n = recv(p->sock, p->rx_len + p->rx_got, 2 - p->rx_got, 0);
        if (n <= 0)
            goto lost;
        p->rx_got += n;
        if (p->rx_got == 2) {
            p->rx_expect = get16(p->rx_len);
            p->rx_got = 0;
            if (p->rx_expect == 0 || p->rx_expect > sizeof(p->rx_buf)) {
                Printf("amisqueeze: invalid packet size (%ld)\n", (LONG)p->rx_expect);
                return FALSE;
            }
        }
        return TRUE;
    }

    n = recv(p->sock, p->rx_buf + p->rx_got, p->rx_expect - p->rx_got, 0);
    if (n <= 0)
        goto lost;
    p->rx_got += n;
    if (p->rx_got == p->rx_expect) {
        p->last_rx = now_ms();
        process(p, p->rx_buf, p->rx_expect);
        p->rx_expect = 0;
        p->rx_got = 0;
    }
    return TRUE;

lost:
    if (n == 0)
        Printf("amisqueeze: connection closed by the server\n");
    else
        Printf("amisqueeze: recv: error %ld\n", Errno());
    return FALSE;
}
