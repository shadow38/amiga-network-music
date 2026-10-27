/*
 * player.h - portable core of amisqueeze (slimproto client for Lyrion Music Server / Music Assistant):
 * player state and API. Standard C only, no Amiga or POSIX headers: system services come from platform.h.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

#include "decoder.h"
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
    const char *server;                 /* host name or dotted address, NULL = found by discovery */
    long        port;                   /* as given (SLIMPROTO_PORT if not), checked by player_main: 1 to 65535 */
    const char *name;                   /* NULL = "Amiga" */
    const char *mac;                    /* "xx:xx:xx:xx:xx:xx", NULL = derived from the name */
    const char *output;                 /* file receiving the audio played, NULL = none (implies no audio) */
    int         audio;                  /* 1 = play through the platform's audio output (AHI), 0 = virtual */
    int         unit;                   /* audio unit (AHI unit), checked by player_main: 0 to 3 */
    int         swap;                   /* swap left and right */
    int         pri;                    /* PRI: the task priority, checked by player_main: -10 to 10 */
    int         pri_given;              /* 0: the priority is left as it was */
    int         verbose;
    int         flac;                   /* enum decode_mode: whether FLAC is advertised to the server */
    int         mp3;                    /* enum decode_mode: the same for MP3 (builds with AMISQUEEZE_MP3) */
    int         bench;                  /* BENCH: measure the decoding speed, print it and quit */
    uint32_t    maxrate;                /* MAXRATE: 0 = not given, MAXRATE_AHI, MAXRATE_AHISTD, or Hz */
};

/* MAXRATE option: the highest sample rate declared to the server (MaxSampleRate in HELO). Without it, 48000: the
 * server keeps the source's rate. Below the source's rate, LMS needs a conversion rule that can resample, or it
 * refuses the track (plan step player-13). */
#define MAXRATE_DEFAULT 48000
#define MAXRATE_MIN     8000
#define MAXRATE_AHI     1                   /* MAXRATE=AHI: the AHI unit's rate as it is */
#define MAXRATE_AHISTD  2                   /* MAXRATE=AHISTD: the standard slimproto rate at or below it */

/* FLAC and MP3 options: AUTO = the format is advertised if this machine decodes it at least twice real time
 * (measured at start-up), ON = always, OFF = never (the server converts it to PCM) */
enum decode_mode { DECODE_AUTO, DECODE_ON, DECODE_OFF };
#define DECODE_MIN_SPEED 200                /* % of real time, for AUTO */

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
    uint32_t closes;                    /* sockets closed so far: a readiness seen before a close is not this one's */
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

/* Output: the buffer's bytes go to a sink, the virtual output (consumed at the PCM rate, as a sound card would;
 * with the OUTPUT option also written to a file, as received) or the platform's audio output (AHI) */
enum output_state {
    OUTPUT_STOPPED,                     /* nothing to play */
    OUTPUT_LOADED,                      /* autostart 0: ready, waiting for strm u */
    OUTPUT_RUNNING,
    OUTPUT_PAUSED
};

#define MAX_TRACKS 4

#define OUT_START_UNKNOWN UINT64_MAX

struct track {
    uint64_t                  start;     /* position of its first byte in the stream (ring total_in) */
    struct pcm_format         fmt;       /* as announced by strm s */
    const struct decoder_ops *ops;       /* its decoder */
    struct pcm_format         out_fmt;   /* what the decoder produces (set when it starts the track) */
    uint64_t                  out_start; /* position of its first decoded byte in the output (handed), or
                                            OUT_START_UNKNOWN until the decoder reaches the track */
};

/* The output asks the decoder for PCM and hands it to a sink (the virtual output, or the audio output). Positions
 * are counted from the last reset, in bytes of decoded PCM: handed over (handed); played <= that. Elapsed time,
 * STMs at a track boundary and STMu follow the bytes played. The decoder reads the ring buffer, whose positions
 * are bytes of the stream (track starts). */
struct output {
    int          state;                 /* enum output_state */
    struct track cur;                   /* track being played */
    uint32_t     last_tick;             /* virtual output: ms of the last consumption */
    uint32_t     carry;                 /* virtual output: remainder of bytes_per_sec * ms / 1000 */
    uint32_t     last_stmt;             /* ms of the last STMt */
    int          starved;               /* buffer empty while still streaming (STMo sent) */
    struct track queue[MAX_TRACKS];     /* tracks in the buffer, not started yet */
    int          queued;
    int          file;                  /* OUTPUT file, -1 = none */
    const char  *file_name;
    uint64_t     written;               /* bytes written to it */
    uint64_t     handed;                /* bytes of decoded PCM handed to the sink since the last reset */
    struct decoder dec;                 /* decoder of the track at the buffer's read position */
};

/* Audio output through the platform (AHI on the Amiga): blocks of converted PCM played one after the other, two at
 * most with the device and one filled in advance. About 0.6 s ahead: enough to ride out a busy moment, short
 * enough for the volume (player-5). */
#define AUDIO_BLOCKS     3
#define AUDIO_BLOCK_SIZE 32768UL        /* about 0.19 s of 44.1 kHz 16-bit stereo; filled with whole frames only
                                           (a 24-bit frame, 3 or 6 bytes, does not divide it) */
#define AUDIO_VOLUME_FULL 0x10000UL     /* 16.16 gain 1.0: samples unchanged */

enum block_state {
    BLOCK_FREE,
    BLOCK_READY,                        /* filled, waiting for its turn (or taken back by a pause) */
    BLOCK_SENT                          /* with the device */
};

struct audio_block {
    uint8_t *buf;
    uint32_t len;                       /* bytes for the device (16-bit samples) once converted; done and sent too */
    uint32_t done;                      /* bytes already played (a pause stopped it there) */
    uint32_t sent;                      /* bytes sent with the last request (len - done) */
    int      sample_bytes;              /* 1 to 4: sample size in the stream (the buffer holds that until sent) */
    int      state;                     /* enum block_state */
    uint32_t rate;
    int      channels;
    int      big_endian;                /* byte order of the stream copied in */
    int      converted;                 /* 1 once converted for the device (when first sent) */
};

struct audio {
    int                enabled;
    int                swap;
    struct audio_block block[AUDIO_BLOCKS];
    int                order[AUDIO_BLOCKS];    /* non-free blocks, in playing order (the sent ones first) */
    int                count;
    uint64_t           played;         /* bytes played by finished (or paused) blocks since the last reset */
    uint32_t           head_ms;        /* when the first block started playing */
    int                paused;
    uint32_t           volume;         /* 16.16 gain (audg) applied to the samples, AUDIO_VOLUME_FULL at most */
    int                fixed;          /* volume fixed at 100 % in LMS (audg adjust 0): the gains are ignored */
    uint32_t           tables;         /* volume the tables below were built for */
    int32_t            high[256];      /* sample * volume = high[high byte] + low[low byte] */
    int32_t            low[256];
};

struct player {
    int      sock;                      /* control connection, -1 when closed */
    uint32_t server_ip;                 /* host byte order; 0 = to be found by discovery */
    uint16_t server_port;
    int      discover;                  /* no SERVER: the server is found on the local network */
    uint8_t  mac[6];
    char     name[PLAYER_NAME_LEN + 1];
    uint32_t last_rx;                   /* ms of the last packet received from the server */
    int      verbose;
    int      flac;                      /* FLAC advertised in HELO */
    int      mp3;                       /* MP3 advertised in HELO */
    uint32_t maxrate;                   /* MaxSampleRate in HELO, Hz; 0 = MAXRATE_DEFAULT */

    /* receiving: 2-byte length, then the packet (4-byte opcode + data) */
    uint16_t rx_expect;
    uint16_t rx_got;
    uint8_t  rx_len[2];
    uint8_t  rx_buf[4096];

    struct ring   ring;
    struct stream stream;
    struct output out;
    struct audio  audio;
};

/* player.c */
/* Sets up the player from the options, then runs until interrupted; returns PLAYER_OK or PLAYER_FAIL */
int player_main(struct player *p, const struct options *o);
/* BENCH option: measures how fast FLAC and MP3 are decoded here and prints it; returns PLAYER_OK */
int player_bench(void);
/* Value of the FLAC or MP3 option (AUTO, ON, OFF, any case); -1 if it is none of them */
int parse_decode_mode(const char *s);
int parse_mac(const char *s, uint8_t mac[6]);
/* Value of the MAXRATE option: MAXRATE_AHI or MAXRATE_AHISTD (AHI, AHISTD, any case), or a rate in Hz from
 * MAXRATE_MIN to MAXRATE_DEFAULT; 0 if it is none of them */
uint32_t parse_maxrate(const char *s);
/* The rate that MAXRATE_AHI or MAXRATE_AHISTD (which) asks for an AHI unit playing at freq Hz: freq itself, or
 * the standard slimproto rate at or below it (27429 gives 24000); kept from MAXRATE_MIN to MAXRATE_DEFAULT */
uint32_t maxrate_from_ahi(uint32_t which, uint32_t freq);
/* Workbench start: turns an icon's ToolTypes (n strings) into a command line for the template (an AmigaDOS
 * ReadArgs template, "SERVER,PORT/N/K,..."): the ToolTypes whose keyword is in the template, in order; a disabled
 * one "(KEY=...)" and any other keyword (DONOTWAIT, WINDOW, QUIET...) left out; a value with a space quoted (not a
 * number: a /N option may refuse quotes). Writes at most size
 * bytes, ended by a newline and a NUL; 0 if they do not fit. */
int tooltypes_line(const char *template, const char *const *tooltypes, int n, char *line, int size);

/* bench.c */
/* FLAC decoding speed on this machine, in % of real time (300 = three times faster than playing): a built-in
 * level 8 sample decoded for about 0.3 s, at most max_ms; 0 if it cannot be measured */
uint32_t flac_speed(uint32_t max_ms);
#ifdef AMISQUEEZE_MP3
/* The same for MP3: a built-in 320 kbit/s sample */
uint32_t mp3_speed(uint32_t max_ms);
#endif
void mac_from_name(const char *name, uint8_t mac[6]);

/* slimproto.c */
int slim_connect(struct player *p, int reconnect);
void slim_disconnect(struct player *p);
/* Reads whatever is available on the socket; 0 if the connection is lost. */
int slim_receive(struct player *p);
void slim_send_stat(struct player *p, const char *event, uint32_t server_ts);
void slim_send_refusal(struct player *p);
void slim_send_resp(struct player *p, const char *headers, int len);
void slim_send_dsco(struct player *p, int reason);
/* Server discovery: broadcasts a request on UDP port p->server_port, then takes replies for DISCOVERY_WAIT_MS.
 * Returns 1 with p->server_ip set to the first server that answered, 0 if none did, -1 if interrupted, -2 on a
 * network error (said). */
int slim_discover(struct player *p);
#define DISCOVERY_WAIT_MS 2000
/* A discovery reply ('E', then fields: a 4-byte tag, a 1-byte length, the value): 1 if buf is one, with the value
 * of NAME in name, in ASCII (any other byte as '?'), cut at the packet's end and to size - 1 bytes; empty without
 * NAME. 0 if buf is no reply. */
int discovery_reply(const uint8_t *buf, int len, char *name, int size);

/* stream.c */
/* Initial state (and buffer allocation); 0 if there is not enough memory */
int stream_init(struct player *p);
void stream_free(struct player *p);
/* strm s: starts receiving a track from ip:port with the given HTTP request, decoded by ops */
void stream_start(struct player *p, uint32_t ip, uint16_t port, const uint8_t *request, int request_len,
                  uint32_t threshold, int autostart, const struct pcm_format *fmt, const struct decoder_ops *ops);
uint32_t pcm_bytes_per_sec(const struct pcm_format *f);
/* strm q: closes the stream and empties the buffer */
void stream_stop(struct player *p);
/* strm f: drops the track being streamed (the last one queued), the track playing goes on; a stop when nothing
 * plays */
void stream_flush(struct player *p);
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

/* audio.c: the audio output's blocks (portable; the device itself is behind platform.h) */
/* Opens the platform's audio output and allocates the blocks; 0 on failure (message printed) */
int audio_open(struct player *p, int unit, int swap);
void audio_close(struct player *p);
/* Aborts everything queued; positions start again from 0 */
void audio_reset(struct player *p);
/* Collects the blocks the device has finished */
void audio_poll(struct player *p, uint32_t now);
/* A free block, or -1 */
int audio_free_block(const struct player *p);
/* Blocks queued or held: 0 = nothing left to play */
int audio_pending(const struct player *p);
/* Buffer of block b (AUDIO_BLOCK_SIZE bytes), where the output writes the PCM before audio_submit() */
uint8_t *audio_block_data(struct player *p, int b);
/* Queues block b, holding n bytes of PCM in format f (whole frames; n <= AUDIO_BLOCK_SIZE, half of it for 8-bit
 * samples, which double in size); it is converted for the device (16-bit big-endian, volume, left/right swapped
 * with SWAP) when sent */
void audio_submit(struct player *p, int b, uint32_t n, const struct pcm_format *f, uint32_t now);
/* Bytes played since the last reset: exact (finished blocks), or with an estimate of the block playing now */
uint64_t audio_played(const struct player *p, uint32_t now, int estimate);
void audio_pause(struct player *p, uint32_t now);
void audio_resume(struct player *p, uint32_t now);
/* audg: 16.16 gains of left and right, adjust 0 = fixed volume (full); applied to the samples of the next blocks
 * sent. Returns 1 if the volume, or whether it is fixed, changed. */
int audio_set_volume(struct player *p, uint32_t left, uint32_t right, int adjust);
/* Audio mode and frequency of an AHI unit in the bytes of its preferences file (ENV:Sys/ahi.prefs: an IFF FORM
 * PREF with one AHIU chunk per unit); 0 if that unit is not in it */
int ahi_prefs_unit(const uint8_t *prefs, uint32_t size, int unit, uint32_t *mode, uint32_t *freq);
struct audio_mode;
/* Warns when the unit's mode would not play a stereo stream right: a stereo mode that cannot pan (each sound on
 * one side) or a mono mode. Returns 1 if it warned. */
int audio_check_mode(int unit, const struct audio_mode *m);
/* Offset just after the blank line ending HTTP headers ("\r\n\r\n" or "\n\n"), 0 if not there yet */
int http_header_end(const char *buf, int len);

#endif
