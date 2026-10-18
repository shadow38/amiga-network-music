/*
 * stub_platform.c - fake implementation of platform.h for the unit tests (see stub_platform.h)
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "stub_platform.h"

uint8_t  stub_tx[8192];
int      stub_tx_len;
char     stub_out[8192];
uint32_t stub_now;
int      stub_chunk;

static uint8_t rx[8192];
static int rx_len, rx_pos;

void stub_reset(void)
{
    stub_tx_len = 0;
    stub_out[0] = '\0';
    rx_len = rx_pos = 0;
}

void stub_feed(const void *data, int len)
{
    memcpy(rx + rx_len, data, (size_t)len);
    rx_len += len;
}

void stub_feed_packet(const char *opcode, const void *body, int len)
{
    uint8_t head[6];
    int n = 4 + len;

    head[0] = (uint8_t)(n >> 8);
    head[1] = (uint8_t)n;
    memcpy(head + 2, opcode, 4);
    stub_feed(head, 6);
    stub_feed(body, len);
}

int stub_pending(void)
{
    return rx_len - rx_pos;
}

void msg(const char *fmt, ...)
{
    size_t used = strlen(stub_out);
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(stub_out + used, sizeof(stub_out) - used, fmt, ap);
    va_end(ap);
}

void msg_raw(const void *buf, int len)
{
    size_t used = strlen(stub_out);

    if (used + (size_t)len < sizeof(stub_out)) {
        memcpy(stub_out + used, buf, (size_t)len);
        stub_out[used + (size_t)len] = '\0';
    }
}

uint32_t plat_now_ms(void)
{
    return stub_now;
}

int plat_resolve(const char *host, uint32_t *ip)
{
    (void)host;
    *ip = 0x7f000001;
    return 1;
}

int plat_connect(uint32_t ip, uint16_t port)
{
    (void)ip;
    (void)port;
    return 3;
}

int plat_send(int sock, const void *buf, int len)
{
    (void)sock;
    memcpy(stub_tx + stub_tx_len, buf, (size_t)len);
    stub_tx_len += len;
    return len;
}

int plat_recv(int sock, void *buf, int len)
{
    int n = rx_len - rx_pos;

    (void)sock;
    if (n > len)
        n = len;
    if (stub_chunk > 0 && n > stub_chunk)
        n = stub_chunk;
    memcpy(buf, rx + rx_pos, (size_t)n);
    rx_pos += n;
    return n;                           /* 0 when nothing is left = connection closed */
}

void plat_close(int sock)
{
    (void)sock;
}

int plat_error(void)
{
    return 0;
}

int plat_wait(const int *socks, int n, int *ready, uint32_t timeout_ms)
{
    (void)socks;
    (void)n;
    (void)ready;
    (void)timeout_ms;
    return -1;
}
