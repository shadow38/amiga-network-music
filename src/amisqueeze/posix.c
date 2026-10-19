/*
 * posix.c - amisqueeze on Linux (or another POSIX system), for host-side tests: entry point and the system
 * services of platform.h. Same behaviour as the Amiga build; the arguments use the same KEY=value syntax:
 *
 *   amisqueeze [SERVER=]<ip|name> [PORT=3483] [NAME=<name>] [MAC=xx:xx:xx:xx:xx:xx] [OUTPUT=<file>] [UNIT=<n>]
 *              [NOAUDIO] [SWAP] [VERBOSE]
 *
 * No audio output: always the virtual output (UNIT, NOAUDIO and SWAP are accepted for the same command lines).
 *
 * Ctrl-C (SIGINT) or SIGTERM stops it, like Ctrl-C on the Amiga. No default options file.
 */
#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"
#include "player.h"

/* Not on every system (e.g. macOS): SIGPIPE is ignored anyway (see main) */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static volatile sig_atomic_t interrupted;
static int last_error;
static struct player player;

static void on_signal(int sig)
{
    (void)sig;
    interrupted = 1;
}

/* ---- platform.h ---- */

void msg(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void msg_raw(const void *buf, int len)
{
    fwrite(buf, 1, (size_t)len, stdout);
    fflush(stdout);
}

uint32_t plat_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000);
}

int plat_resolve(const char *host, uint32_t *ip)
{
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0)
        return 0;
    *ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return 1;
}

/* Socket and connection; non-blocking: the connection may still be in progress on return */
static int connect_socket(uint32_t ip, uint16_t port, int nonblocking)
{
    struct sockaddr_in sa;
    int s = socket(AF_INET, SOCK_STREAM, 0);

    if (s < 0) {
        last_error = errno;
        return -1;
    }
    if (nonblocking)
        fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        last_error = errno;
        if (!(nonblocking && last_error == EINPROGRESS)) {
            close(s);
            return -2;
        }
    }
    return s;
}

int plat_connect(uint32_t ip, uint16_t port)
{
    return connect_socket(ip, port, 0);
}

int plat_connect_start(uint32_t ip, uint16_t port)
{
    return connect_socket(ip, port, 1);
}

int plat_connect_finish(int sock)
{
    int err = 0;
    socklen_t len = sizeof(err);

    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        err = errno;
    if (err != 0) {
        last_error = err;
        return 0;
    }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) & ~O_NONBLOCK);
    return 1;
}

int plat_send(int sock, const void *buf, int len)
{
    /* MSG_NOSIGNAL: a connection closed by the server gives an error, not SIGPIPE */
    ssize_t n = send(sock, buf, (size_t)len, MSG_NOSIGNAL);

    if (n <= 0)
        last_error = errno;
    return (int)n;
}

int plat_recv(int sock, void *buf, int len)
{
    ssize_t n = recv(sock, buf, (size_t)len, 0);

    if (n < 0)
        last_error = errno;
    return (int)n;
}

void plat_close(int sock)
{
    close(sock);
}

/* No audio output on the host: the core keeps the virtual output (options.audio is always 0) */
int plat_audio_open(int unit)
{
    (void)unit;
    msg("amisqueeze: no audio output in the host build\n");
    return 0;
}

void plat_audio_close(void)
{
}

void plat_audio_send(int i, const void *data, uint32_t len, uint32_t rate, int channels, int link)
{
    (void)i; (void)data; (void)len; (void)rate; (void)channels; (void)link;
}

int plat_audio_done(int i)
{
    (void)i;
    return 1;
}

int32_t plat_audio_abort(int i)
{
    (void)i;
    return 0;
}

int plat_file_create(const char *name)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0)
        last_error = errno;
    return fd;
}

int plat_file_write(int file, const void *buf, int len)
{
    const char *b = buf;
    int done = 0;

    while (done < len) {
        ssize_t n = write(file, b + done, (size_t)(len - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            last_error = n < 0 ? errno : ENOSPC;
            break;
        }
        done += (int)n;
    }
    return done;
}

void plat_file_close(int file)
{
    close(file);
}

int plat_error(void)
{
    return last_error;
}

int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms)
{
    fd_set rfds, wfds;
    struct timeval tv;
    int i, r, maxfd = -1;

    if (interrupted)
        return -1;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    for (i = 0; i < n; i++) {
        fds[i].ready = 0;
        if (fds[i].events & PLAT_READ)
            FD_SET(fds[i].sock, &rfds);
        if (fds[i].events & PLAT_WRITE)
            FD_SET(fds[i].sock, &wfds);
        if (fds[i].sock > maxfd)
            maxfd = fds[i].sock;
    }
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    r = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
    if (interrupted)
        return -1;
    if (r < 0) {
        if (errno == EINTR)
            return 0;
        msg("amisqueeze: select: error %d\n", errno);
        return -2;
    }
    for (i = 0; i < n; i++) {
        if ((fds[i].events & PLAT_READ) && FD_ISSET(fds[i].sock, &rfds))
            fds[i].ready |= PLAT_READ;
        if ((fds[i].events & PLAT_WRITE) && FD_ISSET(fds[i].sock, &wfds))
            fds[i].ready |= PLAT_WRITE;
    }
    return r > 0 ? 1 : 0;
}

/* ---- entry point ---- */

/* KEY=value arguments like the Amiga template; the SERVER= keyword may be omitted */
static int parse_args(int argc, char **argv, struct options *o)
{
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *eq = strchr(a, '=');

        if (eq) {
            size_t k = (size_t)(eq - a);
            const char *v = eq + 1;
            if (k == 6 && !strncasecmp(a, "SERVER", 6))
                o->server = v;
            else if (k == 4 && !strncasecmp(a, "PORT", 4))
                o->port = (uint16_t)atoi(v);
            else if (k == 4 && !strncasecmp(a, "NAME", 4))
                o->name = v;
            else if (k == 3 && !strncasecmp(a, "MAC", 3))
                o->mac = v;
            else if (k == 6 && !strncasecmp(a, "OUTPUT", 6))
                o->output = v;
            else if (k == 4 && !strncasecmp(a, "UNIT", 4))
                o->unit = atoi(v);
            else {
                msg("amisqueeze: unknown option %s\n", a);
                return 0;
            }
        } else if (!strcasecmp(a, "VERBOSE"))
            o->verbose = 1;
        else if (!strcasecmp(a, "NOAUDIO"))
            o->audio = 0;
        else if (!strcasecmp(a, "SWAP"))
            o->swap = 1;
        else if (o->server == NULL)
            o->server = a;
        else {
            msg("amisqueeze: unexpected argument %s\n", a);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    struct options o;
    struct sigaction sa;

    memset(&o, 0, sizeof(o));
    if (!parse_args(argc, argv, &o))
        return PLAYER_FAIL;
    if (o.server == NULL) {
        msg("amisqueeze: no server: give SERVER=<ip|name>\n");
        return PLAYER_FAIL;
    }

    /* No SA_RESTART: select() returns on the signal, like WaitSelect() on Ctrl-C */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    return player_main(&player, &o);
}
