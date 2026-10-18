/*
 * stub_platform.h - fake platform for the unit tests of the portable core: no network, packets sent by the
 * player are captured, packets from the "server" are fed in, the clock is set by the test.
 * Socket 3 is the control connection, socket 4 the stream (HTTP) connection; handle 5 is the OUTPUT file, captured
 * in memory.
 */
#ifndef STUB_PLATFORM_H
#define STUB_PLATFORM_H

#include <stdint.h>

#define STUB_CONTROL 3
#define STUB_STREAM  4
#define STUB_FILE    5

extern uint8_t  stub_tx[8192];          /* bytes sent by the player on the control connection */
extern int      stub_tx_len;
extern uint8_t  stub_stream_tx[2048];   /* bytes sent on the stream connection (the HTTP request) */
extern int      stub_stream_tx_len;
extern char     stub_out[16384];        /* text printed by the player (msg, msg_raw) */
extern uint32_t stub_now;               /* value of plat_now_ms() */
extern int      stub_chunk;             /* max bytes returned by one plat_recv() (0 = no limit) */
extern int      stub_connect_fails;     /* 1: plat_connect_finish() fails (stream server unreachable) */
extern int      stub_stream_open;       /* 1 while the player keeps the stream socket open */
extern uint8_t  stub_file[2 * 1024 * 1024];  /* bytes written to the OUTPUT file (not cleared by stub_reset) */
extern int      stub_file_len;
extern int      stub_file_open;         /* 1 while the file is open */
extern int      stub_file_fails;        /* 1: plat_file_create() fails */
extern int      stub_file_limit;        /* writes stop at this file size, like a full disk (0 = no limit) */

/* Clears everything sent, printed and still to be received */
void stub_reset(void);
/* Queues a server -> client packet on the control connection: 2-byte length, then opcode and body */
void stub_feed_packet(const char *opcode, const void *body, int len);
/* Raw bytes to be received on the control connection */
void stub_feed(const void *data, int len);
/* Bytes still to be received on the control connection */
int stub_pending(void);
/* Bytes to be received on the stream connection; once they are read, plat_recv() returns 0 (closed) only after
 * stub_stream_end() */
void stub_feed_stream(const void *data, int len);
void stub_stream_end(void);
int stub_stream_pending(void);

#endif
