/*
 * platform.h - what the portable core of amisqueeze needs from the system.
 *
 * Implemented by amiga.c, amiga_ahi.c and amiga_paula.c (AmigaOS: bsdsocket.library, timer.device, dos.library,
 * ahi.device, audio.device) and posix.c (Linux and other POSIX systems, for host-side tests). Portable means tested
 * on the host and able to follow the other systems with ahi.device, not a player for PCs: the audio functions
 * follow ahi.device (plat_ahi_*) and audio.device (plat_paula_*). Addresses and ports are in host byte order:
 * conversions to network order happen in the implementations, never in the core.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/* Monotonic clock in milliseconds (wraps around) */
uint32_t plat_now_ms(void);

/* The program's task priority (PRI), set back as it was when the program ends; nothing on the host */
void plat_set_priority(int pri);

/* Standard printf format, written and flushed at once: output redirected to a file is readable while the
 * program runs. */
void msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Raw bytes (not NUL-terminated), written and flushed at once */
void msg_raw(const void *buf, int len);

/* Host name or dotted address; returns 1 and *ip, or 0 if it cannot be resolved */
int plat_resolve(const char *host, uint32_t *ip);

/* Connected TCP sockets are non-blocking: a TCP/IP stack may report a socket readable when
 * nothing can be read yet (AmiNetXDuo counts segments that arrived out of order), and a blocking receive would
 * then wait, forever if the server is gone. */

/* Blocking TCP connection: returns a socket (>= 0), non-blocking once connected; -1 if the socket cannot be
 * created or made non-blocking, -2 if the connection fails (or is interrupted: plat_interrupted()). After a
 * failure, plat_error() gives the system error code. */
int plat_connect(uint32_t ip, uint16_t port);

/* Non-blocking TCP connection: returns a socket (>= 0) whose connection is in progress, -1 if the socket cannot
 * be created or made non-blocking, -2 if the connection fails at once. Wait until the socket is writable (PLAT_WRITE), then call
 * plat_connect_finish(): 1 = connected, 0 = failed (plat_error(); the socket is still open). */
int plat_connect_start(uint32_t ip, uint16_t port);
int plat_connect_finish(int sock);

/* 1 once the user asked to stop (Ctrl-C, Break) during a socket call, which failed for it: the core stops at once,
 * without reporting that failure. plat_wait() then returns -1. */
int plat_interrupted(void);

/* UDP socket allowed to broadcast (server discovery): returns a socket (>= 0), or -1 (plat_error()) */
int plat_udp_open(void);
/* Sends len bytes to every host of the local network (255.255.255.255) on the given port; 0 on error
 * (plat_error()) */
int plat_udp_broadcast(int sock, uint16_t port, const void *buf, int len);
/* Reads one datagram, at most len bytes of it, and the address it came from; bytes read, < 0 on error. The UDP
 * socket stays blocking: a datagram reported readable is there. */
int plat_udp_recv(int sock, void *buf, int len, uint32_t *ip);

#define PLAT_AGAIN (-2)                 /* plat_recv(): nothing to read yet, not an error */

int plat_send(int sock, const void *buf, int len);     /* bytes sent, <= 0 on error (a full buffer included) */
int plat_recv(int sock, void *buf, int len);           /* bytes received, 0 = closed, PLAT_AGAIN, -1 = error */
void plat_close(int sock);
int plat_error(void);

/* Sockets to wait for: events = PLAT_READ and/or PLAT_WRITE; plat_wait() sets ready to the events that occurred */
#define PLAT_READ  1
#define PLAT_WRITE 2

struct plat_fd {
    int sock;
    int events;
    int ready;
};

/* Output file (OUTPUT option): creates or replaces the file; returns a handle (>= 0), or -1 (plat_error()) */
int plat_file_create(const char *name);
/* Bytes written, fewer than len on error (plat_error()) */
int plat_file_write(int file, const void *buf, int len);
void plat_file_close(int file);

/* AHI output (ahi.device on the Amiga): AUDIO_BLOCKS requests, numbered 0 to AUDIO_BLOCKS - 1, playing 16-bit
 * big-endian PCM. A request linked to another (link >= 0) starts when that one ends, at once if it already
 * ended. plat_wait() also returns when a request ends. */
/* Opens the given unit; 0 if impossible (message printed) */
int plat_ahi_open(int unit);
void plat_ahi_close(void);
/* The audio mode the opened unit is set to (AHI preferences), as the device describes it */
struct ahi_mode {
    char     name[64];                      /* e.g. "Paula:14 bit stereo++" */
    uint32_t freq;                          /* Hz */
    int      stereo;                        /* 1: two output channels */
    int      panning;                       /* 1: a sound can be placed anywhere between them ("stereo++") */
};
/* Fills m for the given unit; 0 if unknown (no preferences for it: the device's defaults) */
int plat_ahi_mode(int unit, struct ahi_mode *m);
/* Queues request i: len bytes at data, rate in Hz, 1 or 2 channels, linked to request link (-1 = none), at full
 * volume (the core scales the samples) */
void plat_ahi_send(int i, const void *data, uint32_t len, uint32_t rate, int channels, int link);
/* 1 if request i has ended (it is then free again) */
int plat_ahi_done(int i);
/* Aborts request i and waits for it; returns the bytes it played, or -1 if the device cannot tell (AHI before
 * version 6) */
int32_t plat_ahi_abort(int i);

/* Direct Paula output (audio.device on the Amiga): Paula's four channels (14 bits) or channels 0 and 1 only (8
 * bits), playing signed 8-bit samples from buffers in chip RAM that the platform layer allocates: AUDIO_BLOCKS
 * blocks, each with one buffer of PAULA_BLOCK_SAMPLES bytes per channel. Channels 0 and 3 play on the left, 1 and 2
 * on the right. A block is played by every channel at once, after the blocks already sent. plat_wait() also
 * returns when one ends. */
#define PAULA_BLOCK_SAMPLES 8192
#define PAULA_PERIOD_MIN    124             /* Paula's shortest period (clock cycles per sample) */
#define PAULA_PERIOD_MAX    65535
/* Opens Paula with 4 or 2 channels; 0 if impossible (message printed) */
int plat_paula_open(int channels);
void plat_paula_close(void);
/* Paula's clock in Hz, PAL or NTSC: a period of n plays clock / n samples a second */
uint32_t plat_paula_clock(void);
/* Buffer of block b for channel c */
uint8_t *plat_paula_buffer(int b, int c);
/* Plays samples [start, start + len) of block b's buffers, at the given period; start and len are even */
void plat_paula_send(int b, uint32_t start, uint32_t len, uint32_t period);
/* 1 if block b has ended (it is then free again) */
int plat_paula_done(int b);
/* Aborts block b and waits for it; returns the samples it played, or -1 if the device cannot tell */
int32_t plat_paula_abort(int b);
/* Volumes, 0 to 64, of the high channels (0 and 1) and the low ones (3 and 2), applied at once */
void plat_paula_volume(int high, int low);

/* Waits until one of the n sockets is ready, the timeout expires or the user interrupts (Ctrl-C). n may be 0
 * (plain delay that can be interrupted). Returns 1 = something ready, 0 = timeout, -1 = interrupted,
 * -2 = error (already reported). */
int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms);

#endif
