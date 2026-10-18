/*
 * stub_platform.h - fake platform for the unit tests of the portable core: no network, packets sent by the
 * player are captured, packets from the "server" are fed in, the clock is set by the test.
 */
#ifndef STUB_PLATFORM_H
#define STUB_PLATFORM_H

#include <stdint.h>

extern uint8_t  stub_tx[8192];          /* bytes sent by the player */
extern int      stub_tx_len;
extern char     stub_out[8192];         /* text printed by the player (msg, msg_raw) */
extern uint32_t stub_now;               /* value of plat_now_ms() */
extern int      stub_chunk;             /* max bytes returned by one plat_recv() (0 = no limit) */

/* Clears everything sent, printed and still to be received */
void stub_reset(void);
/* Queues a server -> client packet: 2-byte length, then opcode and body */
void stub_feed_packet(const char *opcode, const void *body, int len);
/* Raw bytes to be received */
void stub_feed(const void *data, int len);
/* Bytes still to be received */
int stub_pending(void);

#endif
