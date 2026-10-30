/*
 * amiga_ahi.c - amisqueeze on AmigaOS 3.x: the audio output of platform.h, through ahi.device
 *
 * audio.c (portable) fills the blocks; here they are sent to ahi.device, one request per block. The rules this
 * follows: docs/development/architecture.md, AHI.
 */
#include <exec/types.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <utility/tagitem.h>

#include "platform.h"
#include "player.h"

/* ahi.device as a library, for AHI_GetAudioAttrsA (the device's base, while the unit is open) */
struct Library *AHIBase;

/* The preferences ahi.device reads: one AHIU chunk per unit, with its audio mode */
#define AHI_PREFS "ENV:Sys/ahi.prefs"

/* ahi.device: one request per audio block, all replied to audio_port */
static struct MsgPort *audio_port;
static struct AHIRequest *audio_req[AUDIO_BLOCKS];
static int audio_v6;                        /* io_Actual valid for an aborted request from version 6 */

int plat_audio_open(int unit)
{
    int i;

    audio_port = CreateMsgPort();
    if (audio_port != NULL)
        audio_req[0] = (struct AHIRequest *)CreateIORequest(audio_port, sizeof(struct AHIRequest));
    if (audio_req[0] == NULL) {
        msg("amisqueeze: not enough memory for the audio output\n");
        plat_audio_close();
        return 0;
    }
    audio_req[0]->ahir_Version = 4;
    if (OpenDevice(AHINAME, (ULONG)unit, (struct IORequest *)audio_req[0], 0) != 0) {
        msg("amisqueeze: cannot open ahi.device unit %d (is AHI installed? NOAUDIO plays without sound)\n", unit);
        DeleteIORequest((struct IORequest *)audio_req[0]);
        audio_req[0] = NULL;
        plat_audio_close();
        return 0;
    }
    audio_v6 = audio_req[0]->ahir_Std.io_Device->dd_Library.lib_Version >= 6;
    for (i = 1; i < AUDIO_BLOCKS; i++) {    /* copies of the opened request: same device and unit */
        audio_req[i] = AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC);
        if (audio_req[i] == NULL) {
            msg("amisqueeze: not enough memory for the audio output\n");
            plat_audio_close();
            return 0;
        }
        CopyMem(audio_req[0], audio_req[i], sizeof(struct AHIRequest));
    }
    return 1;
}

int plat_audio_mode(int unit, struct audio_mode *m)
{
    static uint8_t prefs[2048];             /* static: the default stack is small; the file is about 300 bytes */
    struct TagItem tags[5];
    ULONG stereo = FALSE, panning = FALSE;
    uint32_t mode, freq;
    BPTR f;
    LONG n;

    if (audio_req[0] == NULL || (f = Open((STRPTR)AHI_PREFS, MODE_OLDFILE)) == 0)
        return 0;
    n = Read(f, prefs, sizeof(prefs));
    Close(f);
    if (n <= 0 || !ahi_prefs_unit(prefs, (uint32_t)n, unit, &mode, &freq))
        return 0;
    AHIBase = (struct Library *)audio_req[0]->ahir_Std.io_Device;
    m->name[0] = '\0';
    tags[0].ti_Tag = AHIDB_Name;
    tags[0].ti_Data = (ULONG)m->name;
    tags[1].ti_Tag = AHIDB_BufferLen;
    tags[1].ti_Data = sizeof(m->name);
    tags[2].ti_Tag = AHIDB_Stereo;
    tags[2].ti_Data = (ULONG)&stereo;
    tags[3].ti_Tag = AHIDB_Panning;
    tags[3].ti_Data = (ULONG)&panning;
    tags[4].ti_Tag = TAG_DONE;
    if (!AHI_GetAudioAttrsA(mode, NULL, tags))
        return 0;                           /* a mode the installed drivers do not know */
    m->freq = freq;
    m->stereo = stereo != FALSE;
    m->panning = panning != FALSE;
    return 1;
}

/* The core has aborted every request before (audio_reset) */
void plat_audio_close(void)
{
    int i;

    for (i = 1; i < AUDIO_BLOCKS; i++) {
        FreeVec(audio_req[i]);
        audio_req[i] = NULL;
    }
    if (audio_req[0] != NULL) {
        CloseDevice((struct IORequest *)audio_req[0]);
        DeleteIORequest((struct IORequest *)audio_req[0]);
        audio_req[0] = NULL;
    }
    if (audio_port != NULL) {
        DeleteMsgPort(audio_port);
        audio_port = NULL;
    }
}

void plat_audio_send(int i, const void *data, uint32_t len, uint32_t rate, int channels, int link)
{
    struct AHIRequest *r = audio_req[i];

    r->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = (APTR)data;
    r->ahir_Std.io_Length = len;
    r->ahir_Std.io_Offset = 0;
    r->ahir_Frequency = rate;
    r->ahir_Type = channels == 2 ? AHIST_S16S : AHIST_M16S;
    r->ahir_Volume = 0x10000;               /* always full: a low volume on requests sent again after an abort
                                               corrupted memory (AHI 6.6, paula.audio); audio.c scales instead */
    r->ahir_Position = 0x8000;              /* centre */
    r->ahir_Link = link >= 0 ? audio_req[link] : NULL;
    SendIO((struct IORequest *)r);
}

int plat_audio_done(int i)
{
    if (CheckIO((struct IORequest *)audio_req[i]) == NULL)
        return 0;
    WaitIO((struct IORequest *)audio_req[i]);
    return 1;
}

int32_t plat_audio_abort(int i)
{
    struct AHIRequest *r = audio_req[i];

    if (CheckIO((struct IORequest *)r) == NULL)
        AbortIO((struct IORequest *)r);
    WaitIO((struct IORequest *)r);
    if (r->ahir_Std.io_Error != 0 && !audio_v6)
        return -1;
    return (int32_t)r->ahir_Std.io_Actual;
}

ULONG audio_signal(void)
{
    return audio_port != NULL ? 1UL << audio_port->mp_SigBit : 0;
}
