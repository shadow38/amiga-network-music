/*
 * stub_platform.h - fake platform for the unit tests of the portable core: no network, packets sent by the
 * player are captured, packets from the "server" are fed in, the clock is set by the test.
 * Socket 3 is the control connection, socket 4 the stream (HTTP) connection; handle 5 is the OUTPUT file, captured
 * in memory; socket 6 is server discovery's (UDP), when stub_udp_replies asks for it. The audio device plays requests in the order sent; the test ends them (stub_audio_finish) or a
 * pause aborts them; what they "played" is captured in stub_audio_out.
 */
#ifndef STUB_PLATFORM_H
#define STUB_PLATFORM_H

#include <stdint.h>

#include "platform.h"

#define STUB_CONTROL 3
#define STUB_STREAM  4
#define STUB_FILE    5
#define STUB_UDP     6

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
/* The next stub_waits calls of plat_wait() say ready what stub_wait[i] gives, for the control connection then the
 * stream one (PLAT_READ, PLAT_WRITE, within what is waited for); afterwards plat_wait() returns -1, as on a Ctrl-C */
extern int      stub_wait[8][2];
extern int      stub_waits;
extern int      stub_wait_advances;     /* 1: each plat_wait() moves stub_now on by its timeout */
extern int      stub_waits_without_control;  /* plat_wait() calls that left the control connection out */
extern int      stub_again;             /* the next stub_again plat_recv() on the control connection: PLAT_AGAIN */
extern int      stub_connect_interrupted;    /* 1: plat_connect() fails as interrupted by a Ctrl-C (EINTR) */
/* Server discovery: with 0, no UDP socket (the fake server tries discovery); otherwise the socket gets that many
 * replies, each from its own address (127.0.0.2, 127.0.0.3...), then plat_wait() on it times out, moving stub_now on */
extern int      stub_udp_replies;

struct stub_audio_req {
    const uint8_t *data;
    uint32_t       len, rate;
    int            channels, link, queued, ended;
};
extern struct stub_audio_req stub_audio[8];
extern int      stub_audio_fails;       /* 1: plat_audio_open() fails */
extern int      stub_audio_unit;        /* unit given to plat_audio_open() */
extern int      stub_priority;          /* given to plat_set_priority(); STUB_PRI_NONE after stub_reset() */
#define STUB_PRI_NONE 1000
extern int      stub_audio_mode_known;  /* 1: plat_audio_mode() returns stub_audio_mode */
extern struct audio_mode stub_audio_mode;
extern int      stub_audio_open;
extern int32_t  stub_audio_abort_played;  /* bytes the playing request has played when aborted (-1 = unknown) */
extern int32_t  stub_audio_abort_real;  /* with -1 (AHI 4): bytes it really played, captured but not returned */
extern uint8_t  stub_audio_out[2 * 1024 * 1024];  /* what the device played, in order */
extern int      stub_audio_out_len;
extern int      stub_audio_sends;       /* requests sent since stub_audio_clear() */
extern int      stub_audio_bad;         /* requests sent against ahi.device's rules: linked to a request that is not
                                           playing, unlinked while one is queued, or a third one at once */
/* Clears the captured output and the counters (not the requests) */
void stub_audio_clear(void);
/* The request playing now ends: its data goes to stub_audio_out; returns its number, or -1 if none is queued */
int stub_audio_finish(void);
/* Requests queued, in playing order */
int stub_audio_queue(int *order);

/* Clears everything sent, printed and still to be received */
void stub_reset(void);
/* Queues a server -> client packet on the control connection: 2-byte length, then opcode and body */
void stub_feed_packet(const char *opcode, const void *body, int len);
/* Raw bytes to be received on the control connection */
void stub_feed(const void *data, int len);
/* Bytes still to be received on the control connection */
int stub_pending(void);
/* Bytes to be received on the stream connection; once they are read, plat_recv() returns PLAT_AGAIN, and 0
 * (closed) after stub_stream_end() */
void stub_feed_stream(const void *data, int len);
void stub_stream_end(void);
int stub_stream_pending(void);

#endif
