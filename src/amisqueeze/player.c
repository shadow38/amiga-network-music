/*
 * player.c - portable core of amisqueeze: player setup and main loop.
 *
 * One loop around plat_wait(): the control socket, with a one-second timeout that also serves as a timer, and
 * interruption by the user (Ctrl-C). Reconnects every RECONNECT_DELAY_S seconds while the server is unreachable.
 */
#include <string.h>

#include "platform.h"
#include "player.h"

/* Server silence after which the connection is considered dead (the server sends strm t every 5 s) */
#define RX_TIMEOUT_MS 35000
#define RECONNECT_DELAY_S 5

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
        if (plat_wait(NULL, 0, NULL, 100) == -1)
            return 0;
    return 1;
}

static void run(struct player *p)
{
    int reconnect = 0;

    for (;;) {
        int ready, r;

        if (p->sock < 0) {
            if (!slim_connect(p, reconnect)) {
                msg("retrying in %d s\n", RECONNECT_DELAY_S);
                if (!wait_seconds(RECONNECT_DELAY_S))
                    return;
                continue;
            }
            reconnect = 1;
        }

        r = plat_wait(&p->sock, 1, &ready, 1000);
        if (r == -1)
            return;
        if (r == -2) {
            slim_disconnect(p);
            continue;
        }
        if (r > 0 && ready && !slim_receive(p)) {
            slim_disconnect(p);
            continue;
        }
        if ((uint32_t)(plat_now_ms() - p->last_rx) > RX_TIMEOUT_MS) {
            msg("amisqueeze: no news from the server, reconnecting\n");
            slim_disconnect(p);
        }
    }
}

int player_main(struct player *p, const struct options *o)
{
    p->sock = -1;
    p->verbose = o->verbose;
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
    msg("player \"%s\" (%02X:%02X:%02X:%02X:%02X:%02X), server %s port %u\n",
        p->name, p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5],
        o->server, (unsigned)p->server_port);

    run(p);
    slim_disconnect(p);
    msg("stopped\n");
    return PLAYER_OK;
}
