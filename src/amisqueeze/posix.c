/*
 * posix.c - amisqueeze on Linux (or another POSIX system), for host-side tests: entry point and the system
 * services of platform.h. Same behaviour as the Amiga build; the arguments use the same KEY=value syntax:
 *
 *   amisqueeze [SERVER=]<ip|name> [PORT=3483] [NAME=<name>] [MAC=xx:xx:xx:xx:xx:xx] [VERBOSE]
 *
 * Ctrl-C (SIGINT) or SIGTERM stops it, like Ctrl-C on the Amiga. No default options file.
 */
#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
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

int plat_connect(uint32_t ip, uint16_t port)
{
    struct sockaddr_in sa;
    int s = socket(AF_INET, SOCK_STREAM, 0);

    if (s < 0) {
        last_error = errno;
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        last_error = errno;
        close(s);
        return -2;
    }
    return s;
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

int plat_error(void)
{
    return last_error;
}

int plat_wait(const int *socks, int n, int *ready, uint32_t timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    int i, r, maxfd = -1;

    if (interrupted)
        return -1;
    FD_ZERO(&rfds);
    for (i = 0; i < n; i++) {
        FD_SET(socks[i], &rfds);
        if (socks[i] > maxfd)
            maxfd = socks[i];
    }
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    if (interrupted)
        return -1;
    if (r < 0) {
        if (errno == EINTR)
            return 0;
        msg("amisqueeze: select: error %d\n", errno);
        return -2;
    }
    for (i = 0; i < n; i++)
        ready[i] = FD_ISSET(socks[i], &rfds) ? 1 : 0;
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
            else {
                msg("amisqueeze: unknown option %s\n", a);
                return 0;
            }
        } else if (!strcasecmp(a, "VERBOSE"))
            o->verbose = 1;
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
