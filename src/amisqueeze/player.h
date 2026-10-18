/*
 * player.h - portable core of amisqueeze (slimproto client for Lyrion Music Server / Music Assistant):
 * player state and API. Standard C only, no Amiga or POSIX headers: system services come from platform.h.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

#define SLIMPROTO_PORT 3483
#define PLAYER_NAME_LEN 64

/* Exit codes, same values as AmigaDOS RETURN_OK and RETURN_FAIL */
#define PLAYER_OK   0
#define PLAYER_FAIL 20

/* Options, filled by the entry point of each platform (command line, default options) */
struct options {
    const char *server;                 /* host name or dotted address (required) */
    uint16_t    port;                   /* 0 = SLIMPROTO_PORT */
    const char *name;                   /* NULL = "Amiga" */
    const char *mac;                    /* "xx:xx:xx:xx:xx:xx", NULL = derived from the name */
    int         verbose;
};

struct player {
    int      sock;                      /* control connection, -1 when closed */
    uint32_t server_ip;                 /* host byte order */
    uint16_t server_port;
    uint8_t  mac[6];
    char     name[PLAYER_NAME_LEN + 1];
    uint32_t last_rx;                   /* ms of the last packet received from the server */
    int      verbose;

    /* receiving: 2-byte length, then the packet (4-byte opcode + data) */
    uint16_t rx_expect;
    uint16_t rx_got;
    uint8_t  rx_len[2];
    uint8_t  rx_buf[4096];
};

/* player.c */
/* Sets up the player from the options, then runs until interrupted; returns PLAYER_OK or PLAYER_FAIL */
int player_main(struct player *p, const struct options *o);
int parse_mac(const char *s, uint8_t mac[6]);
void mac_from_name(const char *name, uint8_t mac[6]);

/* slimproto.c */
int slim_connect(struct player *p, int reconnect);
void slim_disconnect(struct player *p);
/* Reads whatever is available on the socket; 0 if the connection is lost. */
int slim_receive(struct player *p);

#endif
