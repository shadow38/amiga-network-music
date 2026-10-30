/*
 * amiga.c - amisqueeze on AmigaOS 3.x: entry point and the system services of platform.h, but the audio output
 * (amiga_ahi.c)
 *
 * Usage: amisqueeze [SERVER=<ip|name>] [PORT=3483] [NAME=<name>] [MAC=xx:xx:xx:xx:xx:xx] [OUTPUT=<file>]
 *                   [LOG=<file>] [UNIT=<n>] [NOAUDIO] [SWAP] [FLAC=AUTO|ON|OFF] [MP3=AUTO|ON|OFF]
 *                   [MAXRATE=AHI|AHISTD|<Hz>] [PRI=<n>] [BENCH] [VERBOSE]
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
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>
#include <proto/icon.h>
#include <proto/commodities.h>
#include <libraries/commodities.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <sys/filio.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "player.h"

/* Read by the Version command: "Version amisqueeze". Printed with VERBOSE: a string nothing reads would be dropped
 * at link time (--gc-sections), and Version would find nothing. Not const: in the data hunk. In the code hunk,
 * AmigaOS 3.2's Version read a garbled name ("amiseeze"), though the bytes were right. */
#ifdef AMISQUEEZE_FPU
static char version[] = "$VER: amisqueeze.fpu 0.9-dev (30.10.2026)";
#else
static char version[] = "$VER: amisqueeze 0.9-dev (30.10.2026)";
#endif

#ifdef AMISQUEEZE_FPU
/* Replaces libnix's start-up check, linked in by -m68881: without an FPU it shows an alert and quits before main(),
 * without a word in the Shell. main() checks instead, with a message. */
void __fpucheck(void)
{
}
#endif

/* The decoders need more than the Shell's 4 KB: dr_flac about 5.5 KB (4.6 KB in drflac_open alone), minimp3 about
 * 17.5 KB (most of it in mp3dec_decode_frame). libnix's start-up code (__stkinit, linked in by the Makefile) gives the program a stack of
 * this size when the Shell's is smaller */
#ifdef AMISQUEEZE_MP3
unsigned long __stack = 32768;
#else
unsigned long __stack = 16384;
#endif

/* Used for the command line and for the prefs variable */
#define TEMPLATE "SERVER,PORT/N/K,NAME/K,MAC/K,OUTPUT/K,LOG/K,UNIT/N/K,NOAUDIO/S,SWAP/S,FLAC/K,MP3/K,MAXRATE/K," \
                 "PRI/N/K,BENCH/S,VERBOSE/S"
enum { ARG_SERVER, ARG_PORT, ARG_NAME, ARG_MAC, ARG_OUTPUT, ARG_LOG, ARG_UNIT, ARG_NOAUDIO, ARG_SWAP, ARG_FLAC,
       ARG_MP3, ARG_MAXRATE, ARG_PRI, ARG_BENCH, ARG_VERBOSE, ARG_COUNT };

/* Global variable ENV:amisqueeze.prefs (saved in ENVARC: to survive a reboot) */
#define PREFS_VAR "amisqueeze.prefs"

struct Library *SocketBase;
struct Device *TimerBase;
static struct timerequest timer_io;
static int last_error;
static int stop_asked;                      /* a socket call was interrupted by Ctrl-C (EINTR) */

/* amiga_ahi.c: the signal of the port its requests are replied to (0 while closed) */
ULONG audio_signal(void);

/* static: holds a 4 KB buffer, and the default stack is small */
static struct player player;

/* Prefs line; the parsed strings point into it until the end */
static char prefs_line[512];

/* Formatted message (static: the default stack is small) */
static char msg_line[512];

/* LOG option: every message also appended to this file (0 = none) */
static BPTR log_file;
static const char *log_name;

/* Workbench start: libnix's start-up code sets _WBenchMsg (NULL from a Shell). It would open the
 * console named by __stdiowin before main(); none here: amisqueeze opens its own once it has read its icon's
 * ToolTypes (WINDOW, QUIET). */
extern struct WBStartup *_WBenchMsg;
char *__stdiowin = NULL;
#define DEFAULT_WINDOW "CON:0/20/640/120/amisqueeze/CLOSE"
struct Library *IconBase;
static struct DiskObject *wb_icon;
static char tt_line[512];                   /* the icon's ToolTypes as a command line */

/* The message window, which Exchange shows and hides: started from the Workbench, or from a Shell into NIL:
 * (Run >NIL: in S:User-Startup). It is in RAW mode: in the usual mode, a key typed without Return holds every
 * write to the window until Return, and the one task of amisqueeze with it (seen in WinUAE). An asynchronous read
 * always waits on it, for the keys (ignored) and the event of its close gadget, which hides it. */
#ifndef ACTION_FORCE
#define ACTION_FORCE 2001                   /* text into a console's input; not in the NDK's dosextens.h */
#endif
static char win_spec[256];                  /* its specification ("" = no window: a Shell start) */
static BPTR win;                            /* the window, 0 while hidden */
static BPTR win_out;                        /* the output while it is hidden: none (Workbench), or NIL: */
static int win_on_message;                  /* hidden during start-up (QUIET, NIL:): a message opens it */
static struct MsgPort *win_port;            /* the read's reply port and packet (NULL: no Show and Hide) */
static struct DosPacket *win_pkt;
static int win_reading;                     /* the read is pending */
static char win_input[64];                  /* what it reads: keys, ignored, and events */
static int win_csi;                         /* console_closed()'s state */
static struct history history;              /* the messages, shown again when the window opens */

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

/* Asks the window for what is typed: the read ends at a key, or an event */
static void win_read(void)
{
    struct FileHandle *fh = BADDR(win);

    if (win_pkt == NULL || fh->fh_Type == NULL)
        return;
    win_pkt->dp_Type = ACTION_READ;
    win_pkt->dp_Arg1 = fh->fh_Arg1;
    win_pkt->dp_Arg2 = (LONG)win_input;
    win_pkt->dp_Arg3 = sizeof(win_input);
    SendPkt(win_pkt, fh->fh_Type, win_port);
    win_reading = 1;
}

/* Opens the window in RAW mode, the output going there, and shows the messages kept. "CSI 11{" asks for its close
 * gadget's event (AmigaOS 3.2 sends it in RAW mode without being asked). */
static void win_show(void)
{
    const char *part[2];
    int len[2], i;

    if (win != 0 || win_spec[0] == '\0')
        return;
    win = Open((STRPTR)win_spec, MODE_NEWFILE);
    if (win == 0)
        win = Open((STRPTR)DEFAULT_WINDOW, MODE_NEWFILE);  /* a WINDOW that cannot be opened */
    if (win == 0)
        return;
    win_on_message = 0;                     /* once hidden, it opens again from Exchange only */
    SetMode(win, 1);
    Write(win, "\x9b" "11{", 4);
    win_csi = 0;
    SelectOutput(win);
    history_text(&history, part, len);
    for (i = 0; i < 2; i++)
        if (len[i] > 0)
            Write(win, (APTR)part[i], len[i]);
    win_read();
}

static void win_close(void)
{
    SelectOutput(win_out);
    Close(win);
    win = 0;
}

/* Exchange's Hide. A read still pending would never come back once the window is closed: ACTION_FORCE (a byte into
 * its input) ends it first. A console that refuses it (a replacement of CON:) keeps its window. */
static void win_hide(void)
{
    struct FileHandle *fh = BADDR(win);

    if (win == 0)
        return;
    if (win_reading) {
        if (!DoPkt(fh->fh_Type, ACTION_FORCE, fh->fh_Arg1, (LONG)"\n", 1, 0, 0)) {
            msg("amisqueeze: this window cannot be hidden (error %ld)\n", (long)IoErr());
            return;
        }
        WaitPort(win_port);
        GetMsg(win_port);
        win_reading = 0;
    }
    win_close();
}

/* The window's read came back: at the close gadget's event (or an EOF, a console without RAW mode), the window
 * hides; keys are ignored */
static void win_event(void)
{
    LONG n;

    if (!win_reading || GetMsg(win_port) == NULL)
        return;
    win_reading = 0;
    n = win_pkt->dp_Res1;
    if (n > 0 && !console_closed(&win_csi, win_input, (int)n))
        win_read();
    else
        win_close();
}

/* Sets up the window: spec without AUTO and WAIT (the default if too long). It opens at the first message
 * (on_message 1), or stays hidden (2: QUIET, NIL:), but a message during start-up opens it. Without the read's port
 * and packet, it cannot be hidden. */
static void win_begin(const char *spec, int on_message)
{
    if (!window_spec(spec, win_spec, sizeof(win_spec)))
        strcpy(win_spec, DEFAULT_WINDOW);
    win_out = Output();
    win_on_message = on_message;
    win_port = CreateMsgPort();
    win_pkt = AllocDosObject(DOS_STDPKT, NULL);
    if (win_port == NULL || win_pkt == NULL) {
        if (win_port != NULL)
            DeleteMsgPort(win_port);
        if (win_pkt != NULL)
            FreeDosObject(DOS_STDPKT, win_pkt);
        win_port = NULL;
        win_pkt = NULL;
    }
}

/* QUIET, NIL:: from here on (the player starting), a message no longer opens the window; Show does */
static void win_started(void)
{
    if (win_on_message == 2)
        win_on_message = 0;
}

/* At the end, a window shown stays until it is closed, so that the last messages can be read: what WAIT does, which
 * a window that hides cannot have */
static void win_end(void)
{
    while (win != 0 && win_reading) {
        WaitPort(win_port);
        win_event();
    }
    if (win != 0)
        win_close();
    if (win_pkt != NULL)
        FreeDosObject(DOS_STDPKT, win_pkt);
    if (win_port != NULL)
        DeleteMsgPort(win_port);
}

/* A message to the output: the window (opened for it, if a message should open it) or the Shell; kept for the
 * window. Write() is unbuffered: the text reaches a redirected file at once. No output: the window hidden, from the
 * Workbench (the LOG file only). */
static void out_write(const void *buf, int len)
{
    if (win == 0 && win_on_message)
        win_show();
    if (Output() != 0)
        Write(Output(), (APTR)buf, len);
    if (win_spec[0] != '\0')
        history_add(&history, buf, len);
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
    out_write(msg_line, len);
    log_write(msg_line, len);
}

void msg_raw(const void *buf, int len)
{
    out_write(buf, len);
    log_write(buf, len);
}

/* Monotonic clock (E-Clock, since boot): the system time can jump (clock set, SNTP) */
uint32_t plat_now_ms(void)
{
    struct EClockVal ev;
    ULONG freq = ReadEClock(&ev);

    return (uint32_t)((((unsigned long long)ev.ev_hi << 32 | ev.ev_lo) * 1000) / freq);
}

/* PRI: the priority the task had before, set back by main() at the end */
static BYTE pri_saved;
static int pri_set;

void plat_set_priority(int pri)
{
    BYTE old = SetTaskPri(FindTask(NULL), pri);

    if (!pri_set)
        pri_saved = old;
    pri_set = 1;
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
    if (he == NULL || he->h_addrtype != AF_INET || he->h_length != 4 || he->h_addr_list[0] == NULL)
        return 0;                           /* an answer without an IPv4 address: not found */
    memcpy(&a, he->h_addr_list[0], 4);
    *ip = ntohl(a);
    return 1;
}

/* The error of the socket call that just failed. EINTR: the stack's break mask (Ctrl-C by default) interrupted a
 * blocking call, leaving the signal set or not depending on the stack: taken as the stop request itself. */
static int sock_error(void)
{
    last_error = Errno();
    if (last_error == EINTR)
        stop_asked = 1;
    return last_error;
}

/* Makes socket s non-blocking; 0 if the stack refuses (the socket closed): a receive could then wait forever
 * (see platform.h) */
static int make_nonblocking(LONG s)
{
    LONG one = 1;

    if (IoctlSocket(s, FIONBIO, (char *)&one) < 0) {
        sock_error();
        CloseSocket(s);
        return 0;
    }
    return 1;
}

/* Socket and connection, non-blocking once connected; nonblocking: from the start, the connection may still be in
 * progress on return */
static int connect_socket(uint32_t ip, uint16_t port, int nonblocking)
{
    struct sockaddr_in sa;
    LONG s = socket(AF_INET, SOCK_STREAM, 0);

    if (s < 0) {
        sock_error();
        return -1;
    }
    if (nonblocking && !make_nonblocking(s))
        return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        if (!(sock_error() == EINPROGRESS && nonblocking)) {
            CloseSocket(s);
            return -2;
        }
    }
    if (!nonblocking && !make_nonblocking(s))
        return -1;
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
    LONG err = 0, len = sizeof(err);

    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        err = Errno();
    if (err != 0) {
        last_error = err;
        return 0;
    }
    return 1;
}

int plat_interrupted(void)
{
    return stop_asked;
}

int plat_send(int sock, const void *buf, int len)
{
    LONG n = send(sock, (APTR)buf, len, 0);

    if (n <= 0)
        sock_error();
    return n;
}

int plat_recv(int sock, void *buf, int len)
{
    LONG n = recv(sock, buf, len, 0);

    if (n < 0 && sock_error() == EWOULDBLOCK)
        return PLAT_AGAIN;
    return n < 0 ? -1 : n;
}

int plat_udp_open(void)
{
    LONG one = 1;
    LONG s = socket(AF_INET, SOCK_DGRAM, 0);

    if (s < 0) {
        sock_error();
        return -1;
    }
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0) {
        sock_error();
        CloseSocket(s);
        return -1;
    }
    return s;
}

int plat_udp_broadcast(int sock, uint16_t port, const void *buf, int len)
{
    struct sockaddr_in sa;

    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    if (sendto(sock, (APTR)buf, len, 0, (struct sockaddr *)&sa, sizeof(sa)) != len) {
        sock_error();
        return 0;
    }
    return 1;
}

int plat_udp_recv(int sock, void *buf, int len, uint32_t *ip)
{
    struct sockaddr_in sa;
    LONG salen = sizeof(sa);
    LONG n = recvfrom(sock, buf, len, 0, (struct sockaddr *)&sa, &salen);

    if (n < 0)
        sock_error();
    else
        *ip = ntohl(sa.sin_addr.s_addr);
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

/* Commodity: amisqueeze shows in Exchange, whose "Remove" stops it, and whose Show and Hide open and close its
 * window (COF_SHOW_HIDE, with a window only), and runs once (NBU_UNIQUE). Started again while it runs: nothing
 * happens to the running one (CXCMD_UNIQUE ignored), the new one quits. Returns 0 if another amisqueeze runs;
 * without commodities.library, 1 and no commodity. */
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
    nb.nb_Title = (STRPTR)(version + 6);   /* "amisqueeze x.y (dd.mm.yyyy)": within CBD_TITLELEN */
    nb.nb_Descr = (STRPTR)"Squeezebox network music player";
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags = win_pkt != NULL ? COF_SHOW_HIDE : 0;
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
        else if (id == CXCMD_APPEAR)
            win_show();
        else if (id == CXCMD_DISAPPEAR)
            win_hide();
    }                                           /* CXCMD_UNIQUE: started again, nothing to do */
    return kill;
}

/* The signals of the window's read and of Exchange */
static ULONG ui_signals(void)
{
    return (win_port != NULL ? 1UL << win_port->mp_SigBit : 0) | (cx_port != NULL ? 1UL << cx_port->mp_SigBit : 0);
}

/* What the window and Exchange ask; 1 if asked to stop */
static int ui_events(void)
{
    win_event();
    return cx_killed();
}

int plat_wait(struct plat_fd *fds, int n, uint32_t timeout_ms)
{
    fd_set rfds, wfds;
    struct timeval tv;
    ULONG audio_sig = audio_signal();
    ULONG ui_sig = ui_signals();
    ULONG sigs = SIGBREAKF_CTRL_C | audio_sig | ui_sig;
    LONG r;
    int i, maxfd = -1;

    if (stop_asked)
        return -1;
    if (n == 0) {
        /* No socket: a plain delay (WaitSelect without any socket is not reliable with every stack) */
        if ((SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) || ui_events())
            return -1;
        Delay((timeout_ms + 19) / 20);
        return ((SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) || ui_events()) ? -1 : 0;
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
    if ((sigs & SIGBREAKF_CTRL_C) || ((sigs & ui_sig) && ui_events()))
        return -1;                          /* Ctrl-C, Break, or "Remove" in Exchange */
    if (r >= 0 && (sigs & audio_sig))       /* an audio block ended */
        r++;
    if (r < 0) {
        /* A Ctrl-C already pending when WaitSelect() starts: EINTR, without the signal in sigs (AmiNetXDuo) */
        if (sock_error() == EINTR)
            return -1;
        msg("amisqueeze: WaitSelect: error %ld\n", (long)last_error);
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
    if (len >= (LONG)sizeof(prefs_line) - 2) {  /* the buffer full: GetVar() cut the rest off */
        msg("amisqueeze: ENV:" PREFS_VAR " is too long: at most %d characters\n", (int)sizeof(prefs_line) - 3);
        *error = TRUE;
        return NULL;
    }
    prefs_line[len++] = '\n';               /* ReadArgs parses a line */
    prefs_line[len] = '\0';
    rda = parse_line(prefs_line, args, "ENV:" PREFS_VAR);
    *error = rda == NULL;
    return rda;
}

/* Workbench start: reads the icon's ToolTypes, the options into tt_line (for the template), WINDOW and QUIET for
 * the message window, which opens at the first message unless QUIET. Returns 0 if the ToolTypes are too long to
 * be read. */
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
    win_begin(window, quiet ? 2 : 1);
    if (!ok)
        msg("amisqueeze: the icon's ToolTypes are too long\n");
    return ok;
}

/* The message window: from the Workbench, or from a Shell into NIL: (Run >NIL:), where it is hidden until a
 * message during start-up or Show. From another Shell, the messages go to that Shell. */
static int window_start(int wb)
{
    struct FileHandle *out = Output() != 0 ? BADDR(Output()) : NULL;

    if (wb)
        return wb_start();
    if (out != NULL && out->fh_Type == NULL)  /* NIL: has no handler */
        win_begin(DEFAULT_WINDOW, 2);
    return 1;
}

/* Frees what wb_start() opened, if anything */
static void wb_end(void)
{
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

    if (!window_start(wb))
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
        /* Another amisqueeze runs: from the Workbench nothing happens, from a Shell a word */
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
    o.server = (const char *)args[ARG_SERVER];
    o.port = args[ARG_PORT] ? *(LONG *)args[ARG_PORT] : SLIMPROTO_PORT;
    o.name = (const char *)args[ARG_NAME];
    o.mac = (const char *)args[ARG_MAC];
    o.output = (const char *)args[ARG_OUTPUT];
    o.audio = args[ARG_NOAUDIO] == 0;
    o.unit = args[ARG_UNIT] ? (int)*(LONG *)args[ARG_UNIT] : 0;
    o.swap = args[ARG_SWAP] != 0;
    o.pri_given = args[ARG_PRI] != 0;
    o.pri = args[ARG_PRI] ? (int)*(LONG *)args[ARG_PRI] : 0;
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
    win_started();
    if (args[ARG_VERBOSE])
        msg("%s\n", version + 6);           /* without "$VER: " */
    if (o.bench) {
        rc = player_bench();
        goto out_timer;
    }

    rc = player_main(&player, &o);
    if (rc == PLAYER_FAIL)                  /* an error in the player's start-up: shown in a hidden window */
        win_show();

    if (pri_set)                            /* started from a Shell, this is the Shell's own process */
        SetTaskPri(FindTask(NULL), pri_saved);
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
    win_end();
    wb_end();
    return rc;
}
