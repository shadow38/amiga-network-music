/*
 * icy.c - portable core: the parts of an internet radio's stream that need no system
 *
 * The HTTP status line, and the metadata blocks (ICY) some radios mix into the sound: every `interval` bytes of
 * sound, a length byte (times 16), then a block of that many bytes, usually StreamTitle='...';. The server gives the
 * interval in cont, having read icy-metaint in RESP. stream.c reads the sound straight into the ring and the
 * metadata into the block buffer, each recv stopping where the other begins.
 */
#include <string.h>

#include "player.h"

int http_status(const char *buf, int len)
{
    int i = 0, code = 0, digits;

    while (i < len && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n')
        i++;
    if (i == 0 || i >= len || buf[i] != ' ')
        return -1;                      /* no protocol, or no space on the first line */
    i++;
    for (digits = 0; digits < 3; digits++, i++) {
        if (i >= len || buf[i] < '0' || buf[i] > '9')
            return -1;
        code = code * 10 + (buf[i] - '0');
    }
    if (i < len && buf[i] >= '0' && buf[i] <= '9')
        return -1;                      /* more than three digits */
    return code;
}

void icy_start(struct icy *m, uint32_t interval)
{
    m->interval = interval;
    m->sound_left = interval;
    m->state = ICY_SOUND;
    m->block_len = 0;
    m->block_got = 0;
}

uint32_t icy_sound(const struct icy *m, uint32_t n)
{
    if (m->interval == 0)
        return n;
    if (m->state != ICY_SOUND)
        return 0;
    return n < m->sound_left ? n : m->sound_left;
}

void icy_took_sound(struct icy *m, uint32_t n)
{
    if (m->interval == 0)
        return;
    m->sound_left -= n;
    if (m->sound_left == 0)
        m->state = ICY_LENGTH;
}

uint8_t *icy_meta_space(struct icy *m, uint32_t *n)
{
    if (m->state == ICY_LENGTH) {
        *n = 1;
        return &m->length;
    }
    *n = m->block_len - m->block_got;
    return m->block + m->block_got;
}

int icy_took_meta(struct icy *m, uint32_t n)
{
    if (m->state == ICY_LENGTH) {
        m->block_len = (uint32_t)m->length * 16;
        m->block_got = 0;
        m->state = ICY_BLOCK;
    } else
        m->block_got += n;
    if (m->block_got < m->block_len)
        return 0;
    m->state = ICY_SOUND;               /* an empty block (length 0) ends at once */
    m->sound_left = m->interval;
    return m->block_len > 0;
}

int icy_title(const uint8_t *block, uint32_t len, char *title, int size)
{
    static const char key[] = "StreamTitle='";
    uint32_t end = 0, start, i;
    int n = 0;

    while (end < len && block[end] != 0)    /* the block is padded with NULs */
        end++;
    for (start = 0; start + sizeof(key) - 1 <= end; start++)
        if (!memcmp(block + start, key, sizeof(key) - 1))
            break;
    if (start + sizeof(key) - 1 > end)
        return 0;
    start += sizeof(key) - 1;
    /* The title ends at "';" or at a quote closing the block, as LMS reads it: a quote alone is part of it */
    for (i = start; i < end; i++)
        if (block[i] == '\'' && (i + 1 == end || block[i + 1] == ';'))
            break;
    for (; start < i && n < size - 1; start++)
        title[n++] = block[start] >= ' ' && block[start] <= '~' ? (char)block[start] : '?';
    title[n] = '\0';
    return 1;
}
