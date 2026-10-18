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

/* Blocking TCP connection: returns a socket (>= 0), -1 if the socket cannot be created, -2 if the connection
 * fails. After a failure, plat_error() gives the system error code. */
int plat_connect(uint32_t ip, uint16_t port);

/* Non-blocking TCP connection: returns a socket (>= 0) whose connection is in progress, -1 if the socket cannot
 * be created, -2 if the connection fails at once. Wait until the socket is writable (PLAT_WRITE), then call
 * plat_connect_finish(): 1 = connected (the socket is blocking again), 0 = failed (plat_error(); the socket is
 * still open). */
int plat_connect_start(uint32_t ip, uint16_t port);
int plat_connect_finish(int sock);

int plat_send(int sock, const void *buf, int len);     /* bytes sent, <= 0 on error */
int plat_recv(int sock, void *buf, int len);           /* bytes received, 0 = closed, < 0 = error */
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

/* Waits until one of the n sockets is ready, the timeout expires or the user interrupts (Ctrl-C). n may be 0
 * (plain delay that can be interrupted). Returns 1 = something ready, 0 = timeout, -1 = interrupted,
 * -2 = error (already reported). */
int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms);

#endif
