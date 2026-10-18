/*
 * platform.h - what the portable core of amisqueeze needs from the system.
 *
 * Implemented by amiga.c (AmigaOS: bsdsocket.library, timer.device, dos.library) and posix.c (Linux and other
 * POSIX systems, for host-side tests). Addresses and ports are in host byte order: conversions to network order
 * happen in the implementations, never in the core.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/* Monotonic clock in milliseconds (wraps around) */
uint32_t plat_now_ms(void);

/* Standard printf format, written and flushed at once: output redirected to a file is readable while the
 * program runs. */
void msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Raw bytes (not NUL-terminated), written and flushed at once */
void msg_raw(const void *buf, int len);

/* Host name or dotted address; returns 1 and *ip, or 0 if it cannot be resolved */
int plat_resolve(const char *host, uint32_t *ip);

/* TCP connection: returns a socket (>= 0), -1 if the socket cannot be created, -2 if the connection fails.
 * After a failure, plat_error() gives the system error code. */
int plat_connect(uint32_t ip, uint16_t port);
int plat_send(int sock, const void *buf, int len);     /* bytes sent, <= 0 on error */
int plat_recv(int sock, void *buf, int len);           /* bytes received, 0 = closed, < 0 = error */
void plat_close(int sock);
int plat_error(void);

/* Waits until one of the n sockets is readable, the timeout expires or the user interrupts (Ctrl-C);
 * ready[i] is set to 1 for each readable socket. n may be 0 (plain delay that can be interrupted).
 * Returns 1 = something readable, 0 = timeout, -1 = interrupted, -2 = error (already reported). */
int plat_wait(const int *socks, int n, int *ready, uint32_t timeout_ms);

#endif
