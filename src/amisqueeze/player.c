/*
 * player.c - portable core of amisqueeze: player setup and main loop.
 *
 * One loop around plat_wait(): the control socket and the stream socket, interruption by the user (Ctrl-C), and
 * a timeout that also serves as a timer (100 ms while streaming or playing, for the virtual output and STMt;
 * one second otherwise). Reconnects every RECONNECT_DELAY_S seconds while the server is unreachable, or while it
 * closes the connection at once.
 */
#include <string.h>

#include "platform.h"
#include "player.h"

/* Server silence after which the connection is considered dead (the server sends strm t every 5 s) */
#define RX_TIMEOUT_MS 35000
#define RECONNECT_DELAY_S 5
/* A connection lost sooner was no real session: a proxy accepting while its server is down, a server restarting */
#define SESSION_MIN_MS 10000

int parse_mac(const char *s, uint8_t mac[6])
{
    int i;

    for (i = 0; i < 6; i++) {
        int j, v = 0;
        for (j = 0; j < 2; j++) {
            char c = *s++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return 0;
        }
        mac[i] = (uint8_t)v;
        if (i < 5 && *s++ != ':')
            return 0;
    }
    return *s == '\0';
}

/* Case-insensitive comparison with an upper-case word */
static int same_word(const char *s, const char *upper)
{
    for (; *s && *upper; s++, upper++)
        if ((*s >= 'a' && *s <= 'z' ? *s - 'a' + 'A' : *s) != *upper)
            return 0;
    return *s == '\0' && *upper == '\0';
}

int parse_flac_mode(const char *s)
{
    if (same_word(s, "AUTO"))
        return FLAC_AUTO;
    if (same_word(s, "ON"))
        return FLAC_ON;
    if (same_word(s, "OFF"))
        return FLAC_OFF;
    return -1;
}

/* "3.2x real time" */
static void print_speed(const char *what, uint32_t speed)
{
    msg("%s decoded at %lu.%lux real time", what, (unsigned long)(speed / 100), (unsigned long)(speed % 100 / 10));
}

int player_bench(void)
{
    uint32_t speed = flac_speed(5000);

    if (speed == 0) {
        msg("amisqueeze: the decoding speed cannot be measured\n");
        return PLAYER_FAIL;
    }
    print_speed("FLAC level 8 (the slowest)", speed);
    msg(": %s\n", speed >= FLAC_MIN_SPEED ? "FLAC=AUTO lets this machine decode FLAC" :
        "FLAC=AUTO leaves FLAC to the server (converted to PCM)");
    return PLAYER_OK;
}

/* Stable MAC address derived from the name: the server identifies players by their MAC.
 * 02:xx = locally administered address. 32-bit hash, so every platform derives the same MAC. */
void mac_from_name(const char *name, uint8_t mac[6])
{
    uint32_t h = 5381;

    while (*name)
        h = h * 33 + (uint8_t)*name++;
    mac[0] = 0x02; mac[1] = 'A'; mac[2] = 'M';
    mac[3] = (uint8_t)(h >> 16); mac[4] = (uint8_t)(h >> 8); mac[5] = (uint8_t)h;
}

/* Wait that the user can interrupt; 0 if interrupted */
static int wait_seconds(uint32_t s)
{
    uint32_t t;

    for (t = 0; t < s * 10; t++)
        if (plat_wait(NULL, 0, 100) == -1)
            return 0;
    return 1;
}

/* The connection is lost: reconnect at once after a real session, otherwise wait as after a failed connection
 * (without it, a proxy whose server is down gets dozens of connections a second). 0 if interrupted. */
static int connection_lost(struct player *p, uint32_t since)
{
    slim_disconnect(p);
    if ((uint32_t)(plat_now_ms() - since) >= SESSION_MIN_MS)
        return 1;
    msg("retrying in %d s\n", RECONNECT_DELAY_S);
    return wait_seconds(RECONNECT_DELAY_S);
}

static void run(struct player *p)
{
    int reconnect = 0;
    uint32_t since = 0;                     /* when the connection was established */

    for (;;) {
        struct plat_fd fds[2];
        int n = 0, r, events;

        if (p->sock < 0) {
            if (!slim_connect(p, reconnect)) {
                msg("retrying in %d s\n", RECONNECT_DELAY_S);
                if (!wait_seconds(RECONNECT_DELAY_S))
                    return;
                continue;
            }
            reconnect = 1;
            since = plat_now_ms();
        }

        fds[n].sock = p->sock;
        fds[n].events = PLAT_READ;
        n++;
        events = stream_events(p);
        if (events) {
            fds[n].sock = p->stream.sock;
            fds[n].events = events;
            n++;
        }
        r = plat_wait(fds, n, player_busy(p) ? 100 : 1000);
        if (r == -1)
            return;
        if (r == -2 || ((fds[0].ready & PLAT_READ) && !slim_receive(p))) {
            if (!connection_lost(p, since))
                return;
            continue;
        }
        /* The control packet may have closed or replaced the stream: only use a ready state that is still valid */
        if (n > 1 && fds[1].ready && fds[1].sock == p->stream.sock)
            stream_io(p, fds[1].ready);
        output_tick(p, plat_now_ms());
        if ((uint32_t)(plat_now_ms() - p->last_rx) > RX_TIMEOUT_MS) {
            msg("amisqueeze: no news from the server, reconnecting\n");
            if (!connection_lost(p, since))
                return;
        }
    }
}

int player_main(struct player *p, const struct options *o)
{
    p->sock = -1;
    p->verbose = o->verbose;
    p->audio.volume = AUDIO_VOLUME_FULL;    /* until the server's audg (no effect without the audio output) */
    p->server_port = o->port ? o->port : SLIMPROTO_PORT;
    strncpy(p->name, o->name ? o->name : "Amiga", PLAYER_NAME_LEN);
    p->name[PLAYER_NAME_LEN] = '\0';
    if (o->mac) {
        if (!parse_mac(o->mac, p->mac)) {
            msg("amisqueeze: invalid MAC (expected xx:xx:xx:xx:xx:xx)\n");
            return PLAYER_FAIL;
        }
    } else
        mac_from_name(p->name, p->mac);

    if (!plat_resolve(o->server, &p->server_ip)) {
        msg("amisqueeze: server %s not found\n", o->server);
        return PLAYER_FAIL;
    }
    if (!stream_init(p)) {
        msg("amisqueeze: not enough memory for the %lu KB stream buffer\n",
            (unsigned long)(STREAM_BUFFER_SIZE / 1024));
        return PLAYER_FAIL;
    }
    if (o->output && !output_file_open(p, o->output)) {
        stream_free(p);
        return PLAYER_FAIL;
    }
    /* OUTPUT checks the data: it keeps the virtual output (what AHI aborts on a stop would be in the file) */
    if (o->audio && !o->output) {
        if (!audio_open(p, o->unit, o->swap)) {
            output_file_close(p);
            stream_free(p);
            return PLAYER_FAIL;
        }
        if (p->verbose)
            msg("output: AHI unit %d%s\n", o->unit, o->swap ? ", left and right swapped" : "");
    } else if (p->verbose)
        msg("output: virtual (no sound)\n");
    /* What the server may send as is: decided once, at start-up (HELO) */
    p->flac = o->flac == FLAC_ON;
    if (o->flac == FLAC_AUTO) {
        uint32_t speed = flac_speed(1000);

        p->flac = speed >= FLAC_MIN_SPEED;
        print_speed("FLAC: level 8", speed);
        msg(": %s\n", p->flac ? "FLAC decoded here" : "FLAC converted to PCM by the server");
    } else if (p->verbose)
        msg("FLAC: %s (FLAC=%s)\n", p->flac ? "decoded here" : "converted to PCM by the server",
            p->flac ? "ON" : "OFF");
    msg("player \"%s\" (%02X:%02X:%02X:%02X:%02X:%02X), server %s port %u\n",
        p->name, p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5],
        o->server, (unsigned)p->server_port);

    run(p);
    slim_disconnect(p);
    audio_close(p);
    output_file_close(p);
    stream_free(p);
    msg("stopped\n");
    return PLAYER_OK;
}
