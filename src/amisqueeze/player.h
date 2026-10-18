/*
 * player.h - portable core of amisqueeze (slimproto client for Lyrion Music Server / Music Assistant):
 * player state and API. Standard C only, no Amiga or POSIX headers: system services come from platform.h.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

#include "ring.h"

#define SLIMPROTO_PORT 3483
#define PLAYER_NAME_LEN 64

/* Stream buffer: about 6 s of 44.1 kHz 16-bit stereo PCM */
#define STREAM_BUFFER_SIZE (1024UL * 1024UL)

/* Exit codes, same values as AmigaDOS RETURN_OK and RETURN_FAIL */
#define PLAYER_OK   0
#define PLAYER_FAIL 20

/* Reasons sent in DSCO (squeezelite disconnect_code) */
#define DSCO_OK         0
#define DSCO_LOCAL      1
#define DSCO_REMOTE     2
#define DSCO_UNREACHABLE 3

/* Options, filled by the entry point of each platform (command line, default options) */
struct options {
    const char *server;                 /* host name or dotted address (required) */
    uint16_t    port;                   /* 0 = SLIMPROTO_PORT */
    const char *name;                   /* NULL = "Amiga" */
    const char *mac;                    /* "xx:xx:xx:xx:xx:xx", NULL = derived from the name */
    const char *output;                 /* file receiving the audio played, NULL = none */
    int         verbose;
};

/* HTTP connection carrying the audio of the current track */
enum stream_state {
    STREAM_IDLE,                        /* no connection */
    STREAM_CONNECTING,                  /* non-blocking connection in progress */
    STREAM_HEADERS,                     /* request sent, reading the response headers */
    STREAM_BODY,                        /* receiving audio data into the ring buffer */
    STREAM_DONE                         /* the server closed the connection: everything is in the buffer */
};

struct stream {
    int      sock;
    int      state;                     /* enum stream_state */
    uint32_t ip;
    uint16_t port;
    uint8_t  request[1024];             /* HTTP request from strm s, sent as is */
    int      request_len;
    char     headers[2048];             /* HTTP response headers, passed on in RESP */
    int      headers_len;
    uint64_t bytes;                     /* body bytes received */
    uint32_t threshold;                 /* bytes to receive before the output may start */
    int      autostart;                 /* 1 = start by itself, 0 = wait for strm u */
};

/* Virtual output: consumes the buffer at the PCM rate, as a sound card would (replaced by AHI later); with the
 * OUTPUT option, every byte consumed is also written to a file, as received (no conversion) */
enum output_state {
    OUTPUT_STOPPED,                     /* nothing to play */
    OUTPUT_LOADED,                      /* autostart 0: ready, waiting for strm u */
    OUTPUT_RUNNING,
    OUTPUT_PAUSED
};

#define MAX_TRACKS 4

struct track {                          /* a track whose data is in the buffer, not started yet */
    uint64_t start;                     /* ring total_in where its data begins */
    uint32_t bytes_per_sec;
};

struct output {
    int          state;                 /* enum output_state */
    uint32_t     bytes_per_sec;         /* of the track being played */
    uint64_t     played;                /* bytes of the current track consumed */
    uint32_t     last_tick;             /* ms of the last consumption */
    uint32_t     carry;                 /* remainder of bytes_per_sec * ms / 1000 */
    uint32_t     last_stmt;             /* ms of the last STMt */
    int          starved;               /* buffer empty while still streaming (STMo sent) */
    struct track queue[MAX_TRACKS];
    int          queued;
    int          file;                  /* OUTPUT file, -1 = none */
    const char  *file_name;
    uint64_t     written;               /* bytes written to it */
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

    struct ring   ring;
    struct stream stream;
    struct output out;
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
void slim_send_stat(struct player *p, const char *event, uint32_t server_ts);
void slim_send_resp(struct player *p, const char *headers, int len);
void slim_send_dsco(struct player *p, int reason);

/* stream.c */
/* Initial state (and buffer allocation); 0 if there is not enough memory */
int stream_init(struct player *p);
void stream_free(struct player *p);
/* strm s: starts receiving a PCM track (bytes_per_sec > 0) from ip:port with the given HTTP request */
void stream_start(struct player *p, uint32_t ip, uint16_t port, const uint8_t *request, int request_len,
                  uint32_t threshold, int autostart, uint32_t bytes_per_sec);
/* strm q / f: closes the stream and empties the buffer */
void stream_stop(struct player *p);
/* The stream socket became readable / writable (PLAT_READ, PLAT_WRITE) */
void stream_io(struct player *p, int ready);
/* Events wanted on the stream socket (0 = none: idle, or buffer full) */
int stream_events(const struct player *p);
/* OUTPUT file: created (or replaced) before playing anything, closed at the end; 0 if it cannot be created */
int output_file_open(struct player *p, const char *name);
void output_file_close(struct player *p);
/* Virtual output: consumes the buffer up to now, sends STMs, STMt, STMo, STMu as needed */
void output_tick(struct player *p, uint32_t now);
void output_pause(struct player *p);
void output_resume(struct player *p);
/* Milliseconds of the current track played */
uint32_t output_elapsed_ms(const struct player *p);
/* 1 if the loop must wake up often (stream or output active) */
int player_busy(const struct player *p);
/* Offset just after the blank line ending HTTP headers ("\r\n\r\n" or "\n\n"), 0 if not there yet */
int http_header_end(const char *buf, int len);

#endif
