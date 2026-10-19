/*
 * amiga.c - amisqueeze on AmigaOS 3.x: entry point and the system services of platform.h
 *
 * Usage: amisqueeze [SERVER=<ip|name>] [PORT=3483] [NAME=<name>] [MAC=xx:xx:xx:xx:xx:xx] [OUTPUT=<file>]
 *                   [UNIT=<n>] [NOAUDIO] [SWAP] [VERBOSE]
 * Ctrl-C to quit. Default options come from ENV:amisqueeze.prefs (same syntax as the command line, which
 * takes precedence); see docs/user-guide.md.
 *
 * Needs a TCP/IP stack (Roadshow, AmiTCP, AmiNetXDuo, or WinUAE's bsdsocket emulation) and, for the sound, AHI
 * (ahi.device, version 4 or later). Everything runs in one
 * task: the SocketBase belongs to the task that opened bsdsocket.library. Printed strings stay ASCII: the
 * Amiga uses ISO-8859-1.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <devices/timer.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>
#include <sys/filio.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "player.h"

/* Read by the Version command: "Version amisqueeze" */
static const char version[] __attribute__((used)) = "$VER: amisqueeze 0.1 (18.10.2026)";

/* Used for the command line and for the prefs variable */
#define TEMPLATE "SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,UNIT/N/K,NOAUDIO/S,SWAP/S,VERBOSE/S"
enum { ARG_SERVER, ARG_PORT, ARG_NAME, ARG_MAC, ARG_OUTPUT, ARG_UNIT, ARG_NOAUDIO, ARG_SWAP, ARG_VERBOSE, ARG_COUNT };

/* Global variable ENV:amisqueeze.prefs (saved in ENVARC: to survive a reboot) */
#define PREFS_VAR "amisqueeze.prefs"

struct Library *SocketBase;
struct Device *TimerBase;
static struct timerequest timer_io;
static int last_error;

/* ahi.device: one request per audio block, all replied to audio_port */
static struct MsgPort *audio_port;
static struct AHIRequest *audio_req[AUDIO_BLOCKS];
static int audio_v6;                        /* io_Actual valid for an aborted request from version 6 */

/* static: holds a 4 KB buffer, and the default stack is small */
static struct player player;

/* Prefs line; the parsed strings point into it until the end */
static char prefs_line[512];

/* Formatted message (static: the default stack is small) */
static char msg_line[512];

/* ---- platform.h ---- */

void msg(const char *fmt, ...)
{
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(msg_line, sizeof(msg_line), fmt, ap);
    va_end(ap);
    if (len < 0)
        return;
    if (len >= (int)sizeof(msg_line))
        len = sizeof(msg_line) - 1;
    /* Write() is unbuffered: the text reaches a redirected file at once */
    Write(Output(), msg_line, len);
}

void msg_raw(const void *buf, int len)
{
    Write(Output(), (APTR)buf, len);
}

/* Monotonic clock (E-Clock, since boot): the system time can jump (clock set, SNTP) */
uint32_t plat_now_ms(void)
{
    struct EClockVal ev;
    ULONG freq = ReadEClock(&ev);

    return (uint32_t)((((unsigned long long)ev.ev_hi << 32 | ev.ev_lo) * 1000) / freq);
}

int plat_resolve(const char *host, uint32_t *ip)
{
    struct hostent *he;
    ULONG a = inet_addr((STRPTR)host);

    if (a != INADDR_NONE) {
        *ip = ntohl(a);
        return 1;
    }
    he = gethostbyname((STRPTR)host);
    if (he == NULL || he->h_addrtype != AF_INET)
        return 0;
    memcpy(&a, he->h_addr_list[0], 4);
    *ip = ntohl(a);
    return 1;
}

/* Socket and connection; non-blocking: the connection may still be in progress on return */
static int connect_socket(uint32_t ip, uint16_t port, int nonblocking)
{
    struct sockaddr_in sa;
    LONG one = 1;
    LONG s = socket(AF_INET, SOCK_STREAM, 0);

    if (s < 0) {
        last_error = Errno();
        return -1;
    }
    if (nonblocking)
        IoctlSocket(s, FIONBIO, (char *)&one);
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        last_error = Errno();
        if (!(nonblocking && last_error == EINPROGRESS)) {
            CloseSocket(s);
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
    LONG err = 0, len = sizeof(err), zero = 0;

    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        err = Errno();
    if (err != 0) {
        last_error = err;
        return 0;
    }
    IoctlSocket(sock, FIONBIO, (char *)&zero);
    return 1;
}

int plat_send(int sock, const void *buf, int len)
{
    LONG n = send(sock, (APTR)buf, len, 0);

    if (n <= 0)
        last_error = Errno();
    return n;
}

int plat_recv(int sock, void *buf, int len)
{
    LONG n = recv(sock, buf, len, 0);

    if (n < 0)
        last_error = Errno();
    return n;
}

void plat_close(int sock)
{
    CloseSocket(sock);
}

/* File handles (BPTR) are passed as int: 32 bits, and positive (a BCPL pointer is an address divided by 4) */
int plat_file_create(const char *name)
{
    BPTR fh = Open((STRPTR)name, MODE_NEWFILE);

    if (fh == 0) {
        last_error = IoErr();
        return -1;
    }
    return (int)fh;
}

int plat_file_write(int file, const void *buf, int len)
{
    LONG n = Write((BPTR)file, (APTR)buf, len);

    if (n != len)
        last_error = IoErr();
    return n;
}

void plat_file_close(int file)
{
    Close((BPTR)file);
}

int plat_error(void)
{
    return last_error;
}

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

void plat_audio_send(int i, const void *data, uint32_t len, uint32_t rate, int channels, int link,
                     uint32_t volume)
{
    struct AHIRequest *r = audio_req[i];

    r->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
    r->ahir_Std.io_Command = CMD_WRITE;
    r->ahir_Std.io_Data = (APTR)data;
    r->ahir_Std.io_Length = len;
    r->ahir_Std.io_Offset = 0;
    r->ahir_Frequency = rate;
    r->ahir_Type = channels == 2 ? AHIST_S16S : AHIST_M16S;
    r->ahir_Volume = volume;
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

int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms)
{
    fd_set rfds, wfds;
    struct timeval tv;
    ULONG audio_sig = audio_port != NULL ? 1UL << audio_port->mp_SigBit : 0;
    ULONG sigs = SIGBREAKF_CTRL_C | audio_sig;
    LONG r;
    int i, maxfd = -1;

    if (n == 0) {
        /* No socket: a plain delay (WaitSelect without any socket is not reliable with every stack) */
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            return -1;
        Delay((timeout_ms + 19) / 20);
        return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) ? -1 : 0;
    }

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
    r = WaitSelect(maxfd + 1, &rfds, &wfds, NULL, &tv, &sigs);
    if (sigs & SIGBREAKF_CTRL_C)
        return -1;
    if (r >= 0 && (sigs & audio_sig))       /* an audio block ended */
        r++;
    if (r < 0) {
        msg("amisqueeze: WaitSelect: error %ld\n", (long)Errno());
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

/* Parses ENV:amisqueeze.prefs with the command line template into args. Returns the RDArgs to free at the end,
 * or NULL if the variable does not exist; *error is set if it cannot be parsed. */
static struct RDArgs *read_prefs(LONG args[], BOOL *error)
{
    struct RDArgs *rda;
    LONG len;

    *error = FALSE;
    len = GetVar(PREFS_VAR, prefs_line, sizeof(prefs_line) - 1, GVF_GLOBAL_ONLY);
    if (len < 0)
        return NULL;
    prefs_line[len++] = '\n';               /* ReadArgs parses a line */
    prefs_line[len] = '\0';

    rda = AllocDosObject(DOS_RDARGS, NULL);
    if (rda == NULL) {
        *error = TRUE;
        return NULL;
    }
    rda->RDA_Source.CS_Buffer = prefs_line;
    rda->RDA_Source.CS_Length = len;
    rda->RDA_Source.CS_CurChr = 0;
    rda->RDA_Flags |= RDAF_NOPROMPT;
    if (ReadArgs(TEMPLATE, args, rda) == NULL) {
        PrintFault(IoErr(), "amisqueeze: ENV:" PREFS_VAR);
        FreeDosObject(DOS_RDARGS, rda);
        *error = TRUE;
        return NULL;
    }
    return rda;
}

int main(void)
{
    LONG args[ARG_COUNT] = { 0 }, prefs[ARG_COUNT] = { 0 };
    struct RDArgs *rda, *prefs_rda;
    struct options o;
    BOOL prefs_error;
    int i, rc = RETURN_FAIL;

    prefs_rda = read_prefs(prefs, &prefs_error);
    if (prefs_error)
        return RETURN_FAIL;
    rda = ReadArgs(TEMPLATE, args, NULL);
    if (rda == NULL) {
        PrintFault(IoErr(), "amisqueeze");
        goto out_prefs;
    }
    /* The command line wins; a switch (VERBOSE) set in the prefs cannot be turned off from it */
    for (i = 0; i < ARG_COUNT; i++)
        if (args[i] == 0)
            args[i] = prefs[i];
    if (args[ARG_SERVER] == 0) {
        msg("amisqueeze: no server: give SERVER=<ip|name>, or set it in ENV:" PREFS_VAR "\n");
        goto out_args;
    }

    memset(&o, 0, sizeof(o));
    o.server = (const char *)args[ARG_SERVER];
    o.port = args[ARG_PORT] ? (uint16_t)*(LONG *)args[ARG_PORT] : 0;
    o.name = (const char *)args[ARG_NAME];
    o.mac = (const char *)args[ARG_MAC];
    o.output = (const char *)args[ARG_OUTPUT];
    o.audio = args[ARG_NOAUDIO] == 0;
    o.unit = args[ARG_UNIT] ? (int)*(LONG *)args[ARG_UNIT] : 0;
    o.swap = args[ARG_SWAP] != 0;
    o.verbose = args[ARG_VERBOSE] != 0;

    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&timer_io, 0) != 0) {
        msg("amisqueeze: timer.device unavailable\n");
        goto out_args;
    }
    TimerBase = timer_io.tr_node.io_Device;

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (SocketBase == NULL) {
        msg("amisqueeze: bsdsocket.library not found (is a TCP/IP stack running?)\n");
        goto out_timer;
    }

    rc = player_main(&player, &o);

    CloseLibrary(SocketBase);
out_timer:
    CloseDevice((struct IORequest *)&timer_io);
out_args:
    FreeArgs(rda);
out_prefs:
    if (prefs_rda != NULL) {
        FreeArgs(prefs_rda);
        FreeDosObject(DOS_RDARGS, prefs_rda);
    }
    return rc;
}
