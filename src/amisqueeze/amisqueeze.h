/*
 * amisqueeze - slimproto client (Lyrion Music Server / Music Assistant) for AmigaOS 3.x
 *
 * State shared between the main loop (main.c) and the protocol (slimproto.c).
 * Everything runs in one task: the SocketBase belongs to the task that opened bsdsocket.library.
 */
#ifndef AMISQUEEZE_H
#define AMISQUEEZE_H

#include <exec/types.h>

#define SLIMPROTO_PORT 3483
#define PLAYER_NAME_LEN 64

struct player {
    LONG  sock;                         /* control connection, -1 when closed */
    ULONG server_ip;                    /* network order = Amiga order (big-endian) */
    UWORD server_port;
    UBYTE mac[6];
    char  name[PLAYER_NAME_LEN + 1];
    ULONG last_rx;                      /* ms of the last packet received from the server */
    BOOL  verbose;

    /* receiving: 2-byte length, then the packet (4-byte opcode + data) */
    UWORD rx_expect;
    UWORD rx_got;
    UBYTE rx_len[2];
    UBYTE rx_buf[4096];
};

/* main.c */
ULONG now_ms(void);

/* slimproto.c */
BOOL slim_connect(struct player *p, BOOL reconnect);
void slim_disconnect(struct player *p);
/* Reads whatever is available on the socket; FALSE if the connection is lost. */
BOOL slim_receive(struct player *p);

#endif
