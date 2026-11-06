/*
 * test_core.h - what the unit tests of the portable core share (host build, see "make test"): CHECK, the packets
 * of the fake platform, and the tracks, audio output and decoder that several test files use. Each is defined,
 * with its comment, in test_core.c.
 */
#ifndef TEST_CORE_H
#define TEST_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "player.h"
#include "stub_platform.h"

extern int checks, failures;

#define CHECK(cond) do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/* STAT packet offsets (8-byte header included) */
#define STAT_EVENT       8
#define STAT_BUF_SIZE    15
#define STAT_BUF_FULL    19
#define STAT_BYTES_LOW   27
#define STAT_JIFFIES     33
#define STAT_ELAPSED_S   45
#define STAT_ELAPSED_MS  51
#define STAT_SERVER_TS   55

#define PCM_BPS 176400                  /* 44.1 kHz, 16 bits, stereo */
#define BLOCK ((uint32_t)AUDIO_BLOCK_SIZE)

/* The player, the control connection and the stream (the strings in each file, for sizeof) */
static const char http_ok[] = "HTTP/1.0 200 OK\r\nContent-Type: audio/L16;rate=44100;channels=2\r\n\r\n";
static const char http_404[] = "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n";
static const char request[] = "GET /stream.mp3?player=02:41:4d:d4:ca:84 HTTP/1.0\r\n\r\n";
extern uint8_t pcm[PCM_BPS];
uint32_t be16(const uint8_t *b);
uint32_t be32(const uint8_t *b);
void set32(uint8_t *b, uint32_t v);
const char *trace(void);
const uint8_t *last_stat(const char *event);
void clear_tx(void);
void setup(struct player *p, const char *name);
void teardown(struct player *p);
void drain(struct player *p);
void drain_stream(struct player *p);
void feed_strm(char cmd, const char *fmt7, uint32_t replay_gain, uint16_t port, uint32_t ip,
               const char *req);
void strm_pcm(struct player *p, char autostart, int threshold_kb);
void open_stream(struct player *p, char autostart, int threshold_kb);
int helo_caps_have(const char *s);
void answer_stream(struct player *p, char autostart, int threshold_kb, const void *response, int len);

/* Tracks, the OUTPUT file and the audio output */
extern uint8_t track_a[PCM_BPS];
extern uint8_t track_b[PCM_BPS / 2];
void fill(uint8_t *b, uint32_t len, uint32_t seed);
void play_for(struct player *p, uint32_t ms);
void setup_file(struct player *p);
extern uint8_t expect[2 * 1024 * 1024];
void to_device(uint8_t *dst, const uint8_t *src, uint32_t n, int swap_lr);
void setup_audio(struct player *p, int swap);
int finish(struct player *p);
int count_in_trace(const char *name);
void play_out(struct player *p);
uint32_t to_device_fmt(uint8_t *dst, const uint8_t *src, uint32_t n, int sample_bytes, int big_endian);

/* The widening decoder */
int widen_start(struct decoder *d, const struct pcm_format *in);
void widen_stop(struct decoder *d);
uint32_t widen_input_for(const struct decoder *d, uint32_t out);
void widen(uint8_t *dst, const uint8_t *src, uint32_t n);
void use_widen(struct player *p);
extern uint8_t decoded[2 * PCM_BPS];

/* The tests of each file, called by main() in this order */
void tests_ring(void);
void tests_slimproto(void);
void tests_stream(void);
void tests_output(void);
void tests_icy(void);
void tests_audio(void);
void tests_decoder(void);
void tests_tracks(void);
void tests_player(void);

#endif
