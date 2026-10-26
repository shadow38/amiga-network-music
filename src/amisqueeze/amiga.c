/*
 * amiga.c - amisqueeze on AmigaOS 3.x: entry point and the system services of platform.h
 *
 * Usage: amisqueeze [SERVER=<ip|name>] [PORT=3483] [NAME=<name>] [MAC=xx:xx:xx:xx:xx:xx] [OUTPUT=<file>]
 *                   [LOG=<file>] [UNIT=<n>] [NOAUDIO] [SWAP] [FLAC=AUTO|ON|OFF] [MP3=AUTO|ON|OFF]
 *                   [MAXRATE=AHI|AHISTD|<Hz>] [BENCH] [VERBOSE]
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
#include <dos/datetime.h>
#include <devices/timer.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>
#include <proto/ahi.h>
#include <proto/icon.h>
#include <proto/commodities.h>
#include <libraries/commodities.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <utility/tagitem.h>
#include <sys/filio.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "player.h"

/* Read by the Version command: "Version amisqueeze". Printed with VERBOSE: a string nothing reads would be dropped
 * at link time (--gc-sections), and Version would find nothing. Not const: in the data hunk. In the code hunk,
 * AmigaOS 3.2's Version read "amiseeze 0.3" out of the 0.3 build, though the bytes were right. */
#ifdef AMISQUEEZE_FPU
static char version[] = "$VER: amisqueeze.fpu 0.8-dev (26.10.2026)";
#else
static char version[] = "$VER: amisqueeze 0.8-dev (26.10.2026)";
#endif

#ifdef AMISQUEEZE_FPU
/* Replaces libnix's start-up check, linked in by -m68881: without an FPU it shows an alert and quits before main(),
 * without a word in the Shell. main() checks instead, with a message. */
void __fpucheck(void)
{
}
#endif

/* The decoders need more than the Shell's 4 KB: dr_flac about 5 KB (4.6 KB to open a stream), minimp3 about
 * 17 KB of work space. libnix's start-up code (__stkinit, linked in by the Makefile) gives the program a stack of
 * this size when the Shell's is smaller */
#ifdef AMISQUEEZE_MP3
unsigned long __stack = 32768;
#else
unsigned long __stack = 16384;
#endif

/* Used for the command line and for the prefs variable */
#define TEMPLATE "SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,LOG/K,UNIT/N/K,NOAUDIO/S,SWAP/S,FLAC/K,MP3/K,MAXRATE/K," \
                 "BENCH/S,VERBOSE/S"
enum { ARG_SERVER, ARG_PORT, ARG_NAME, ARG_MAC, ARG_OUTPUT, ARG_LOG, ARG_UNIT, ARG_NOAUDIO, ARG_SWAP, ARG_FLAC,
       ARG_MP3, ARG_MAXRATE, ARG_BENCH, ARG_VERBOSE, ARG_COUNT };

/* Global variable ENV:amisqueeze.prefs (saved in ENVARC: to survive a reboot) */
#define PREFS_VAR "amisqueeze.prefs"

struct Library *SocketBase;
struct Device *TimerBase;
static struct timerequest timer_io;
static int last_error;

/* ahi.device as a library, for AHI_GetAudioAttrsA (the device's base, while the unit is open) */
struct Library *AHIBase;

/* The preferences ahi.device reads: one AHIU chunk per unit, with its audio mode */
#define AHI_PREFS "ENV:Sys/ahi.prefs"

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

/* LOG option: every message also appended to this file (0 = none) */
static BPTR log_file;
static const char *log_name;

/* Workbench start (plan step player-10): libnix's start-up code sets _WBenchMsg (NULL from a Shell). It would open the
 * console named by __stdiowin before main(); none here: amisqueeze opens its own once it has read its icon's
 * ToolTypes (WINDOW, QUIET). */
extern struct WBStartup *_WBenchMsg;
char *__stdiowin = NULL;
#define DEFAULT_WINDOW "CON:0/20/640/120/amisqueeze/AUTO/CLOSE/WAIT/SMART"
struct Library *IconBase;
static struct DiskObject *wb_icon;
static BPTR wb_console;                     /* the message window; with QUIET, 0 unless start-up failed */
static const char *wb_window;               /* its specification: WINDOW, or DEFAULT_WINDOW */
static int wb_quiet;                        /* QUIET, during start-up: a message then opens the window */
static char tt_line[512];                   /* the icon's ToolTypes as a command line */

/* Commodity: amisqueeze shows in Exchange, which can stop it; one at a time */
struct Library *CxBase;
static struct MsgPort *cx_port;
static CxObj *broker;

/* ---- platform.h ---- */

/* Appends to the LOG file, if any; a write error closes it (said in the window only), playback goes on */
static void log_write(const void *buf, int len)
{
    LONG error;
    int n;

    if (log_file == 0 || len <= 0)
        return;
    if (Write(log_file, (APTR)buf, len) == len)
        return;
    error = IoErr();
    Close(log_file);
    log_file = 0;
    n = snprintf(msg_line, sizeof(msg_line), "amisqueeze: cannot write to the log %s: error %ld, log closed\n",
                 log_name, (long)error);
    if (n > 0 && Output() != 0)
        Write(Output(), msg_line, n < (int)sizeof(msg_line) ? n : (int)sizeof(msg_line) - 1);
}

/* LOG option: opens the file to append to (created if missing; readable by others while it runs), and writes a
 * line marking the start of the run. A file that cannot be opened is said, and amisqueeze goes on without it. */
static void log_open(const char *name)
{
    struct DateTime dt;
    char date[LEN_DATSTRING], time[LEN_DATSTRING];
    int n;

    log_file = Open((STRPTR)name, MODE_READWRITE);
    if (log_file == 0) {
        msg("amisqueeze: cannot open the log %s: error %ld, going on without it\n", name, (long)IoErr());
        return;
    }
    log_name = name;
    Seek(log_file, 0, OFFSET_END);
    memset(&dt, 0, sizeof(dt));
    DateStamp(&dt.dat_Stamp);
    dt.dat_Format = FORMAT_DOS;
    dt.dat_StrDate = (STRPTR)date;
    dt.dat_StrTime = (STRPTR)time;
    date[0] = time[0] = '\0';
    DateToStr(&dt);
    n = snprintf(msg_line, sizeof(msg_line), "--- %s, started %s %s\n", version + 6, date, time);
    if (n > 0)
        log_write(msg_line, n < (int)sizeof(msg_line) ? n : (int)sizeof(msg_line) - 1);
}

static void log_close(void)
{
    if (log_file != 0)
        Close(log_file);
    log_file = 0;
}

/* Opens the message window of a Workbench start; the output goes there */
static void wb_open_window(void)
{
    wb_console = Open((STRPTR)wb_window, MODE_NEWFILE);
    if (wb_console == 0)
        wb_console = Open((STRPTR)DEFAULT_WINDOW, MODE_NEWFILE);    /* a WINDOW that cannot be opened */
    if (wb_console != 0)
        SelectOutput(wb_console);
}

/* QUIET: a message during start-up (an error in the options, a log that cannot be opened, a missing library) would
 * go nowhere, or to the log only; the window opens for it (and stays until it is closed) */
static void quiet_start_message(void)
{
    if (Output() == 0 && wb_quiet) {
        wb_quiet = 0;
        wb_open_window();
    }
}

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
    /* Write() is unbuffered: the text reaches a redirected file at once. No output: started from the Workbench
     * with QUIET, once started (the LOG file only) */
    quiet_start_message();
    if (Output() != 0)
        Write(Output(), msg_line, len);
    log_write(msg_line, len);
}

void msg_raw(const void *buf, int len)
{
    quiet_start_message();
    if (Output() != 0)
        Write(Output(), (APTR)buf, len);
    log_write(buf, len);
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

/* Commodity (plan step player-10, decided with the user): amisqueeze shows in Exchange, whose "Remove" stops it, and
 * runs once (NBU_UNIQUE). Started again while it runs: nothing happens to the running one (CXCMD_UNIQUE ignored),
 * the new one quits. Returns 0 if another amisqueeze runs; without commodities.library, 1 and no commodity. */
static int cx_open(void)
{
    struct NewBroker nb;
    LONG error = 0;

    CxBase = OpenLibrary("commodities.library", 37);
    if (CxBase == NULL || (cx_port = CreateMsgPort()) == NULL)
        return 1;
    memset(&nb, 0, sizeof(nb));
    nb.nb_Version = NB_VERSION;
    nb.nb_Name = (STRPTR)"amisqueeze";
    nb.nb_Title = (STRPTR)(version + 6);   /* "amisqueeze 0.5 (20.10.2026)": within CBD_TITLELEN */
    nb.nb_Descr = (STRPTR)"Squeezebox network music player";
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Port = cx_port;
    broker = CxBroker(&nb, &error);
    if (broker == NULL)
        return error != CBERR_DUP;
    ActivateCxObj(broker, TRUE);
    return 1;
}

static void cx_close(void)
{
    struct Message *m;

    if (broker != NULL)
        DeleteCxObjAll(broker);
    broker = NULL;
    if (cx_port != NULL) {
        while ((m = GetMsg(cx_port)) != NULL)
            ReplyMsg(m);
        DeleteMsgPort(cx_port);
        cx_port = NULL;
    }
    if (CxBase != NULL)
        CloseLibrary(CxBase);
    CxBase = NULL;
}

/* Commands from Exchange; 1 if asked to stop ("Remove") */
static int cx_killed(void)
{
    CxMsg *m;
    int kill = 0;

    if (cx_port == NULL)
        return 0;
    while ((m = (CxMsg *)GetMsg(cx_port)) != NULL) {
        ULONG id = CxMsgID(m), type = CxMsgType(m);

        ReplyMsg((struct Message *)m);
        if (type != CXM_COMMAND)
            continue;
        if (id == CXCMD_KILL)
            kill = 1;
        else if (id == CXCMD_DISABLE || id == CXCMD_ENABLE)
            ActivateCxObj(broker, id == CXCMD_ENABLE);  /* shown in Exchange; the music is not affected */
    }                                           /* CXCMD_UNIQUE: started again, nothing to do */
    return kill;
}

int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms)
{
    fd_set rfds, wfds;
    struct timeval tv;
    ULONG audio_sig = audio_port != NULL ? 1UL << audio_port->mp_SigBit : 0;
    ULONG cx_sig = cx_port != NULL ? 1UL << cx_port->mp_SigBit : 0;
    ULONG sigs = SIGBREAKF_CTRL_C | audio_sig | cx_sig;
    LONG r;
    int i, maxfd = -1;

    if (n == 0) {
        /* No socket: a plain delay (WaitSelect without any socket is not reliable with every stack) */
        if ((SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) || cx_killed())
            return -1;
        Delay((timeout_ms + 19) / 20);
        return ((SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) || cx_killed()) ? -1 : 0;
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
    if ((sigs & SIGBREAKF_CTRL_C) || ((sigs & cx_sig) && cx_killed()))
        return -1;                          /* Ctrl-C, Break, or "Remove" in Exchange */
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

/* Parses a line (ended by a newline) with the command line template into args. Returns the RDArgs to free at the
 * end (FreeArgs, FreeDosObject), or NULL with a message naming where the line came from. */
static struct RDArgs *parse_line(char *line, LONG args[], const char *where)
{
    struct RDArgs *rda = AllocDosObject(DOS_RDARGS, NULL);
    char fault[100];

    if (rda == NULL)
        return NULL;
    rda->RDA_Source.CS_Buffer = (STRPTR)line;
    rda->RDA_Source.CS_Length = strlen(line);
    rda->RDA_Source.CS_CurChr = 0;
    rda->RDA_Flags |= RDAF_NOPROMPT;
    if (ReadArgs(TEMPLATE, args, rda) == NULL) {
        Fault(IoErr(), NULL, (STRPTR)fault, sizeof(fault));
        msg("amisqueeze: %s: %s\n", where, fault);
        FreeDosObject(DOS_RDARGS, rda);
        return NULL;
    }
    return rda;
}

/* Parses ENV:amisqueeze.prefs into args. Returns the RDArgs to free at the end, or NULL if the variable does not
 * exist; *error is set if it cannot be parsed. */
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
    rda = parse_line(prefs_line, args, "ENV:" PREFS_VAR);
    *error = rda == NULL;
    return rda;
}

/* Workbench start: reads the icon's ToolTypes, the options into tt_line (for the template), WINDOW and QUIET for
 * the message window, which it opens unless QUIET (AUTO: it shows at the first message). Returns 0 if the
 * ToolTypes are too long to be read. */
static int wb_start(void)
{
    struct WBArg *wa = _WBenchMsg->sm_ArgList;
    const char *window = DEFAULT_WINDOW;
    int quiet = 0, ok = 1;

    strcpy(tt_line, "\n");
    IconBase = OpenLibrary("icon.library", 36);
    if (IconBase != NULL) {
        BPTR old = CurrentDir(wa->wa_Lock);

        wb_icon = GetDiskObject(wa->wa_Name);
        CurrentDir(old);
    }
    if (wb_icon != NULL && wb_icon->do_ToolTypes != NULL) {
        STRPTR *tt = wb_icon->do_ToolTypes;
        STRPTR w;
        int n = 0;

        while (tt[n] != NULL)
            n++;
        quiet = FindToolType(tt, (STRPTR)"QUIET") != NULL;
        w = FindToolType(tt, (STRPTR)"WINDOW");
        if (w != NULL && *w != '\0')
            window = (const char *)w;
        ok = tooltypes_line(TEMPLATE, (const char *const *)tt, n, tt_line, sizeof(tt_line));
    }
    wb_window = window;
    if (quiet)
        wb_quiet = 1;                       /* no window, unless a message comes during start-up */
    else
        wb_open_window();
    if (!ok)
        msg("amisqueeze: the icon's ToolTypes are too long\n");
    return ok;
}

/* Closes what wb_start() opened; the window stays until the user closes it if something was written (WAIT) */
static void wb_end(void)
{
    if (wb_console != 0) {
        SelectOutput(0);
        Close(wb_console);
    }
    if (wb_icon != NULL)
        FreeDiskObject(wb_icon);
    if (IconBase != NULL)
        CloseLibrary(IconBase);
}

int main(void)
{
    LONG args[ARG_COUNT] = { 0 }, prefs[ARG_COUNT] = { 0 };
    struct RDArgs *rda = NULL, *prefs_rda = NULL;
    struct options o;
    BOOL prefs_error;
    int i, rc = RETURN_FAIL, wb = _WBenchMsg != NULL;

    if (wb && !wb_start())
        goto out_wb;
#ifdef AMISQUEEZE_FPU
    /* Before anything uses floating point: built for a 68881/68882, this one would crash without an FPU */
    if (!(SysBase->AttnFlags & (AFF_68881 | AFF_68882 | AFF_FPU40))) {
        msg("amisqueeze.fpu needs an FPU (68881, 68882, or a 68040/68060 with FPU): use amisqueeze instead\n");
        goto out_wb;
    }
#endif
    prefs_rda = read_prefs(prefs, &prefs_error);
    if (prefs_error)
        goto out_wb;
    /* From a Shell the command line, from the Workbench the icon's ToolTypes, read with the same template */
    if (wb)
        rda = parse_line(tt_line, args, "the icon's ToolTypes");
    else if ((rda = ReadArgs(TEMPLATE, args, NULL)) == NULL)
        PrintFault(IoErr(), "amisqueeze");
    if (rda == NULL)
        goto out_prefs;
    /* They win over the prefs; a switch (VERBOSE) set in the prefs cannot be turned off from them */
    for (i = 0; i < ARG_COUNT; i++)
        if (args[i] == 0)
            args[i] = prefs[i];

    if (!args[ARG_BENCH] && !cx_open()) {
        /* Another amisqueeze runs: from the Workbench nothing happens (decided with the user), from a Shell a word */
        if (!wb)
            msg("amisqueeze: amisqueeze is already running (Exchange lists it and can stop it)\n");
        goto out_args;
    }
    if (args[ARG_LOG])
        log_open((const char *)args[ARG_LOG]);
    memset(&o, 0, sizeof(o));
    o.bench = args[ARG_BENCH] != 0;
    if (args[ARG_FLAC] && (o.flac = parse_decode_mode((const char *)args[ARG_FLAC])) < 0) {
        msg("amisqueeze: FLAC=%s: expected AUTO, ON or OFF\n", (const char *)args[ARG_FLAC]);
        goto out_args;
    }
    if (args[ARG_MP3] && (o.mp3 = parse_decode_mode((const char *)args[ARG_MP3])) < 0) {
        msg("amisqueeze: MP3=%s: expected AUTO, ON or OFF\n", (const char *)args[ARG_MP3]);
        goto out_args;
    }
    if (args[ARG_MAXRATE] && (o.maxrate = parse_maxrate((const char *)args[ARG_MAXRATE])) == 0) {
        msg("amisqueeze: MAXRATE=%s: expected AHI, AHISTD, or a rate from %lu to %lu Hz\n",
            (const char *)args[ARG_MAXRATE], (unsigned long)MAXRATE_MIN, (unsigned long)MAXRATE_DEFAULT);
        goto out_args;
    }
    if (args[ARG_SERVER] == 0 && !o.bench) {
        msg("amisqueeze: no server: give SERVER=<ip|name>, or set it in ENV:" PREFS_VAR "\n");
        goto out_args;
    }
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
    if (!o.bench) {                         /* BENCH needs neither the network nor a server */
        SocketBase = OpenLibrary("bsdsocket.library", 4);
        if (SocketBase == NULL) {
            msg("amisqueeze: bsdsocket.library not found (is a TCP/IP stack running?)\n");
            goto out_timer;
        }
    }
    wb_quiet = 0;                           /* QUIET: from here on, the messages go to the log only */
    if (args[ARG_VERBOSE])
        msg("%s\n", version + 6);           /* without "$VER: " */
    if (o.bench) {
        rc = player_bench();
        goto out_timer;
    }

    rc = player_main(&player, &o);

    CloseLibrary(SocketBase);
out_timer:
    CloseDevice((struct IORequest *)&timer_io);
out_args:
    cx_close();
    log_close();                            /* before FreeArgs: its name lives in the arguments */
    FreeArgs(rda);
    if (wb)
        FreeDosObject(DOS_RDARGS, rda);     /* allocated by parse_line */
out_prefs:
    if (prefs_rda != NULL) {
        FreeArgs(prefs_rda);
        FreeDosObject(DOS_RDARGS, prefs_rda);
    }
out_wb:
    if (wb)
        wb_end();
    return rc;
}
