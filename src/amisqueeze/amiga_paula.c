/*
 * amiga_paula.c - amisqueeze on AmigaOS 3.x: the direct Paula output of platform.h (plat_paula_*), through
 * audio.device
 *
 * audio.c (portable) converts each block into its channels' chip RAM buffers; here each block is one CMD_WRITE per
 * channel. The rules this follows: docs/development/architecture.md, Paula direct.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/errors.h>
#include <devices/audio.h>
#include <graphics/gfxbase.h>
#include <hardware/cia.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>          /* BeginIO(), from amiga.lib */

#include "platform.h"
#include "player.h"

#define PAULA_CLOCK_PAL  3546895
#define PAULA_CLOCK_NTSC 3579545

/* Port A of CIA A: its LED bit cleared lights the power LED and turns Paula's low-pass filter on */
#define CIAA_PRA ((volatile UBYTE *)0xbfe001)

#define CHANNELS_HIGH 3                     /* channels 0 and 1 */
#define CHANNELS_LOW  12                    /* channels 2 and 3 */

static struct MsgPort *paula_port;          /* every request is replied here */
static struct IOAudio *paula_ctl;           /* opened the device and allocated the channels: the other commands */
static struct IOAudio *paula_req[AUDIO_BLOCKS][4];  /* block b's write to channel c */
static UBYTE *paula_chip;                   /* the buffers, block after block, channel after channel */
static int paula_channels;
static int paula_filter;                    /* 1: the filter turned off here, 2: and it was on before */
static UWORD paula_vol[4];                  /* each channel's volume, for the next writes */
static int paula_fifo[2], paula_queued;     /* the blocks with the device, in the order sent */
static uint32_t block_len[AUDIO_BLOCKS];
static UWORD block_period[AUDIO_BLOCKS];
static int block_aborted[AUDIO_BLOCKS];     /* its writes were stopped here: an error is no channel taken */
static int paula_taken;                     /* a write ended by another program: said once */
static int paula_resyncs;                   /* blocks sent when the one before had ended on some channels only */

static ULONG chip_size(void)
{
    return (ULONG)AUDIO_BLOCKS * paula_channels * PAULA_BLOCK_SAMPLES;
}

static int all_channels(void)
{
    return paula_channels == 4 ? CHANNELS_HIGH | CHANNELS_LOW : CHANNELS_HIGH;
}

/* A request used again comes back as a reply (NT_REPLYMSG), which CheckIO() and WaitIO() take for done: marked as
 * a message again first, as SendIO() would */
static void begin(struct IOAudio *r)
{
    r->ioa_Request.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    BeginIO((struct IORequest *)r);
}

/* A command for several channels, waited for; DoIO() clears io_Flags, which these commands do not need */
static void ctl_command(UWORD command, int mask)
{
    paula_ctl->ioa_Request.io_Command = command;
    paula_ctl->ioa_Request.io_Unit = (struct Unit *)(ULONG)mask;
    paula_ctl->ioa_Request.io_Flags = 0;
    paula_ctl->ioa_Request.io_Message.mn_Node.ln_Type = NT_MESSAGE;     /* see begin() */
    DoIO((struct IORequest *)paula_ctl);
}

/* Allocates the channels at the lowest precedence, without waiting: taken from no one, and refused if another
 * program holds one. Then raised to the highest, so that nothing (the system's beep included) takes them. */
static int allocate(void)
{
    static UBYTE combination[1];

    combination[0] = (UBYTE)all_channels();
    paula_ctl->ioa_Request.io_Message.mn_Node.ln_Pri = ADALLOC_MINPREC;
    paula_ctl->ioa_Request.io_Command = ADCMD_ALLOCATE;
    paula_ctl->ioa_Request.io_Flags = ADIOF_NOWAIT | IOF_QUICK;
    paula_ctl->ioa_AllocKey = 0;
    paula_ctl->ioa_Data = combination;
    paula_ctl->ioa_Length = sizeof(combination);
    begin(paula_ctl);
    WaitIO((struct IORequest *)paula_ctl);
    if (paula_ctl->ioa_Request.io_Error == 0) {
        paula_ctl->ioa_Request.io_Message.mn_Node.ln_Pri = ADALLOC_MAXPREC;
        ctl_command(ADCMD_SETPREC, all_channels());
        if (paula_ctl->ioa_Request.io_Error == 0)
            return 1;
    }
    msg("amisqueeze: Paula's channels %s are used by another program%s\n", paula_channels == 4 ? "0 to 3" : "0 and 1",
        paula_channels == 4 ? " (AUDIO=PAULA8 needs only channels 0 and 1)" : "");
    return 0;
}

/* The device opened without channels (ioa_Length 0), then allocated */
static int device_open(void)
{
    paula_port = CreateMsgPort();
    if (paula_port != NULL)
        paula_ctl = (struct IOAudio *)CreateIORequest(paula_port, sizeof(struct IOAudio));
    if (paula_ctl == NULL) {
        msg("amisqueeze: not enough memory for the audio output\n");
        return 0;
    }
    if (OpenDevice((STRPTR)AUDIONAME, 0, (struct IORequest *)paula_ctl, 0) != 0) {
        msg("amisqueeze: cannot open audio.device\n");
        DeleteIORequest((struct IORequest *)paula_ctl);
        paula_ctl = NULL;
        return 0;
    }
    return allocate();
}

int plat_paula_open(int channels)
{
    int b, c;

    paula_channels = channels;
    paula_queued = 0;
    paula_taken = 0;
    if (!device_open()) {
        plat_paula_close();
        return 0;
    }
    paula_chip = AllocMem(chip_size(), MEMF_CHIP | MEMF_CLEAR);
    if (paula_chip == NULL) {
        msg("amisqueeze: not enough chip memory for Paula: %lu KB\n", (unsigned long)(chip_size() / 1024));
        plat_paula_close();
        return 0;
    }
    for (b = 0; b < AUDIO_BLOCKS; b++)
        for (c = 0; c < channels; c++) {    /* copies of the allocating request: same device and key */
            paula_req[b][c] = AllocVec(sizeof(struct IOAudio), MEMF_PUBLIC);
            if (paula_req[b][c] == NULL) {
                msg("amisqueeze: not enough memory for the audio output\n");
                plat_paula_close();
                return 0;
            }
            CopyMem(paula_ctl, paula_req[b][c], sizeof(struct IOAudio));
        }
    /* The filter off while playing, as paula.audio does: it cuts above about 3.3 kHz */
    paula_filter = (*CIAA_PRA & CIAF_LED) ? 1 : 2;
    *CIAA_PRA |= CIAF_LED;
    return 1;
}

/* The core has aborted every block before (audio_reset). Also after a failed open. */
void plat_paula_close(void)
{
    int b, c;

    if (paula_resyncs > 0)                  /* expected to stay at 0: the start in step, tried under load */
        msg("Paula: %d blocks restarted on every channel to keep them in step\n", paula_resyncs);
    paula_resyncs = 0;
    if (paula_filter == 2)
        *CIAA_PRA &= ~CIAF_LED;
    paula_filter = 0;
    for (b = 0; b < AUDIO_BLOCKS; b++)
        for (c = 0; c < 4; c++) {
            FreeVec(paula_req[b][c]);
            paula_req[b][c] = NULL;
        }
    if (paula_chip != NULL)
        FreeMem(paula_chip, chip_size());
    paula_chip = NULL;
    if (paula_ctl != NULL) {
        CloseDevice((struct IORequest *)paula_ctl);     /* frees the channels allocated with its key */
        DeleteIORequest((struct IORequest *)paula_ctl);
        paula_ctl = NULL;
    }
    if (paula_port != NULL)
        DeleteMsgPort(paula_port);
    paula_port = NULL;
    paula_queued = 0;
}

/* From the machine's crystal, whatever mode the boot menu set: REALLY_PAL from graphics V39, the PAL flag before */
uint32_t plat_paula_clock(void)
{
    struct GfxBase *gfx = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 33);
    int pal = 1;

    if (gfx != NULL) {
        pal = (gfx->DisplayFlags & (gfx->LibNode.lib_Version >= 39 ? REALLY_PAL : PAL)) != 0;
        CloseLibrary((struct Library *)gfx);
    }
    return pal ? PAULA_CLOCK_PAL : PAULA_CLOCK_NTSC;
}

uint8_t *plat_paula_buffer(int b, int c)
{
    return paula_chip + ((ULONG)b * paula_channels + c) * PAULA_BLOCK_SAMPLES;
}

/* On how many channels block b has ended */
static int ended_on(int b)
{
    int c, n = 0;

    for (c = 0; c < paula_channels; c++)
        if (CheckIO((struct IORequest *)paula_req[b][c]) != NULL)
            n++;
    return n;
}

/* Block b's writes: ADIOF_PERVOL, without which the volume would be 0 (so never DoIO(), which clears it) */
static void prepare(int b, uint32_t start, uint32_t len, UWORD period)
{
    int c;

    for (c = 0; c < paula_channels; c++) {
        struct IOAudio *r = paula_req[b][c];

        r->ioa_Request.io_Command = CMD_WRITE;
        r->ioa_Request.io_Flags = ADIOF_PERVOL;
        r->ioa_Request.io_Unit = (struct Unit *)(1UL << c);
        r->ioa_Data = plat_paula_buffer(b, c) + start;
        r->ioa_Length = len;
        r->ioa_Period = period;
        r->ioa_Volume = paula_vol[c];
        r->ioa_Cycles = 1;
    }
    block_len[b] = len;
    block_period[b] = period;
    block_aborted[b] = 0;
}

static void begin_all(int b)
{
    int c;

    for (c = 0; c < paula_channels; c++)
        begin(paula_req[b][c]);
}

/* The two channels of a side must stay in step, or the 14 bits are wrong. A block added while one plays follows it
 * on every channel if its writes all arrive before that one ends: sent with the interrupts off. Otherwise, and from
 * idle, the channels are stopped, given their writes, and started together (CMD_START). */
void plat_paula_send(int b, uint32_t start, uint32_t len, uint32_t period)
{
    int joined = 0;

    prepare(b, start, len, (UWORD)period);
    if (paula_queued > 0) {
        int last = paula_fifo[paula_queued - 1], ended;

        Disable();
        ended = ended_on(last);
        if (ended == 0) {
            begin_all(b);
            joined = 1;
        }
        Enable();
        if (!joined)
            block_aborted[last] = 1;        /* what it had left is lost to CMD_STOP */
        if (ended > 0 && ended < paula_channels)
            paula_resyncs++;
    }
    if (!joined) {
        ctl_command(CMD_STOP, all_channels());
        begin_all(b);
        ctl_command(CMD_START, all_channels());
    }
    paula_fifo[paula_queued++] = b;
}

/* Takes block b's writes back, and b out of the blocks with the device. A write that ended in error without having
 * been stopped here was stolen: the player stops, as on Ctrl-C. */
static void finish(int b)
{
    int c, k;

    for (c = 0; c < paula_channels; c++) {
        WaitIO((struct IORequest *)paula_req[b][c]);
        if (paula_req[b][c]->ioa_Request.io_Error != 0 && !block_aborted[b] && !paula_taken) {
            msg("amisqueeze: another program took Paula's channel %d\n", c);
            paula_taken = 1;
            Signal(FindTask(NULL), SIGBREAKF_CTRL_C);
        }
    }
    for (k = 0; k < paula_queued && paula_fifo[k] != b; k++)
        ;
    if (k < paula_queued) {
        paula_queued--;
        for (; k < paula_queued; k++)
            paula_fifo[k] = paula_fifo[k + 1];
    }
}

int plat_paula_done(int b)
{
    if (ended_on(b) < paula_channels)
        return 0;
    finish(b);
    return 1;
}

/* CMD_STOP first, on every channel: aborting the block playing would otherwise start the next one, maybe on some
 * channels only. audio.device does not tell how much a write played: -1, unless the block had ended. */
int32_t plat_paula_abort(int b)
{
    int c, ended = ended_on(b) == paula_channels;

    if (!ended) {
        ctl_command(CMD_STOP, all_channels());
        block_aborted[b] = 1;
        for (c = 0; c < paula_channels; c++)
            if (CheckIO((struct IORequest *)paula_req[b][c]) == NULL)
                AbortIO((struct IORequest *)paula_req[b][c]);
    }
    finish(b);
    return ended ? (int32_t)block_len[b] : -1;
}

/* For the writes not started yet (they load their volume when they start), and at once for those playing
 * (ADCMD_PERVOL, which loads the period too: the playing block's) */
void plat_paula_volume(int high, int low)
{
    int k, c;

    paula_vol[0] = paula_vol[1] = (UWORD)high;
    paula_vol[2] = paula_vol[3] = (UWORD)low;
    if (paula_queued == 0)
        return;
    Disable();
    for (k = 0; k < paula_queued; k++)
        for (c = 0; c < paula_channels; c++)
            paula_req[paula_fifo[k]][c]->ioa_Volume = paula_vol[c];
    Enable();
    paula_ctl->ioa_Period = block_period[paula_fifo[0]];
    paula_ctl->ioa_Volume = (UWORD)high;
    ctl_command(ADCMD_PERVOL, CHANNELS_HIGH);
    if (paula_channels == 4) {
        paula_ctl->ioa_Volume = (UWORD)low;
        ctl_command(ADCMD_PERVOL, CHANNELS_LOW);
    }
}

ULONG paula_signal(void)
{
    return paula_port != NULL ? 1UL << paula_port->mp_SigBit : 0;
}
