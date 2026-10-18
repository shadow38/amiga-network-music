/*
 * amisqueeze - slimproto client (Lyrion Music Server / Music Assistant) for AmigaOS 3.x
 *
 * Usage: amisqueeze [SERVER=<ip|name>] [PORT=3483] [NAME=<name>] [MAC=xx:xx:xx:xx:xx:xx] [VERBOSE]
 * Ctrl-C to quit. Default options come from ENV:amisqueeze.prefs (same syntax as the command line, which
 * takes precedence); see docs/user-guide.md.
 *
 * One task, one loop around WaitSelect(): sockets + signals (Ctrl-C), with a one-second timeout that also
 * serves as a timer. Needs a TCP/IP stack (Roadshow, AmiTCP, or WinUAE's bsdsocket emulation). Printed
 * strings stay ASCII: the Amiga uses ISO-8859-1.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>

#include <string.h>

#include "amisqueeze.h"

/* Read by the Version command: "Version amisqueeze" */
static const char version[] __attribute__((used)) = "$VER: amisqueeze 0.1 (18.10.2026)";

/* Used for the command line and for the prefs variable */
#define TEMPLATE "SERVER,PORT/N/K,NAME/K,MAC/K,VERBOSE/S"
enum { ARG_SERVER, ARG_PORT, ARG_NAME, ARG_MAC, ARG_VERBOSE, ARG_COUNT };

/* Global variable ENV:amisqueeze.prefs (saved in ENVARC: to survive a reboot) */
#define PREFS_VAR "amisqueeze.prefs"

/* Server silence after which the connection is considered dead (the server sends strm t every 5 s) */
#define RX_TIMEOUT_MS 35000
#define RECONNECT_DELAY_S 5

struct Library *SocketBase;
struct Device *TimerBase;
static struct timerequest timer_io;

/* static: holds a 4 KB buffer, and the default stack is small */
static struct player player;

/* Prefs line; the parsed strings point into it until the end */
static char prefs_line[512];

/* Monotonic clock (E-Clock, since boot): the system time can jump (clock set, SNTP) */
ULONG now_ms(void)
{
    struct EClockVal ev;
    ULONG freq = ReadEClock(&ev);

    return (ULONG)((((unsigned long long)ev.ev_hi << 32 | ev.ev_lo) * 1000) / freq);
}

static BOOL parse_mac(const char *s, UBYTE mac[6])
{
    int i;

    for (i = 0; i < 6; i++) {
        int j, v = 0;
        for (j = 0; j < 2; j++) {
            char c = *s++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return FALSE;
        }
        mac[i] = v;
        if (i < 5 && *s++ != ':')
            return FALSE;
    }
    return *s == '\0';
}

/* Stable MAC address derived from the name: the server identifies players by their MAC.
 * 02:xx = locally administered address. */
static void mac_from_name(const char *name, UBYTE mac[6])
{
    ULONG h = 5381;

    while (*name)
        h = h * 33 + (UBYTE)*name++;
    mac[0] = 0x02; mac[1] = 'A'; mac[2] = 'M';
    mac[3] = h >> 16; mac[4] = h >> 8; mac[5] = h;
}

static BOOL resolve(const char *host, ULONG *ip)
{
    struct hostent *he;

    *ip = inet_addr((STRPTR)host);
    if (*ip != INADDR_NONE)
        return TRUE;
    he = gethostbyname((STRPTR)host);
    if (he == NULL || he->h_addrtype != AF_INET)
        return FALSE;
    memcpy(ip, he->h_addr_list[0], 4);
    return TRUE;
}

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

/* Wait that Ctrl-C can interrupt; FALSE on Ctrl-C */
static BOOL wait_seconds(ULONG s)
{
    ULONG t;

    for (t = 0; t < s * 10; t++) {
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            return FALSE;
        Delay(5);
    }
    return TRUE;
}

static void run(struct player *p)
{
    BOOL reconnect = FALSE;

    for (;;) {
        fd_set rfds;
        struct timeval tv;
        ULONG sigs;
        LONG n;

        if (p->sock < 0) {
            if (!slim_connect(p, reconnect)) {
                Printf("retrying in %ld s\n", (LONG)RECONNECT_DELAY_S);
                if (!wait_seconds(RECONNECT_DELAY_S))
                    return;
                continue;
            }
            reconnect = TRUE;
        }

        FD_ZERO(&rfds);
        FD_SET(p->sock, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        sigs = SIGBREAKF_CTRL_C;
        n = WaitSelect(p->sock + 1, &rfds, NULL, NULL, &tv, &sigs);
        if (sigs & SIGBREAKF_CTRL_C)
            return;
        if (n < 0) {
            Printf("amisqueeze: WaitSelect: error %ld\n", Errno());
            slim_disconnect(p);
            continue;
        }
        if (n > 0 && FD_ISSET(p->sock, &rfds) && !slim_receive(p)) {
            slim_disconnect(p);
            continue;
        }
        if (now_ms() - p->last_rx > RX_TIMEOUT_MS) {
            Printf("amisqueeze: no news from the server, reconnecting\n");
            slim_disconnect(p);
        }
    }
}

int main(void)
{
    LONG args[ARG_COUNT] = { 0 }, prefs[ARG_COUNT] = { 0 };
    struct RDArgs *rda, *prefs_rda;
    struct player *p = &player;
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
        PutStr("amisqueeze: no server: give SERVER=<ip|name>, or set it in ENV:" PREFS_VAR "\n");
        goto out_args;
    }

    p->sock = -1;
    p->verbose = args[ARG_VERBOSE] != 0;
    p->server_port = args[ARG_PORT] ? *(LONG *)args[ARG_PORT] : SLIMPROTO_PORT;
    strncpy(p->name, args[ARG_NAME] ? (char *)args[ARG_NAME] : "Amiga", PLAYER_NAME_LEN);
    p->name[PLAYER_NAME_LEN] = '\0';
    if (args[ARG_MAC]) {
        if (!parse_mac((char *)args[ARG_MAC], p->mac)) {
            PutStr("amisqueeze: invalid MAC (expected xx:xx:xx:xx:xx:xx)\n");
            goto out_args;
        }
    } else
        mac_from_name(p->name, p->mac);

    if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&timer_io, 0) != 0) {
        PutStr("amisqueeze: timer.device unavailable\n");
        goto out_args;
    }
    TimerBase = timer_io.tr_node.io_Device;

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr("amisqueeze: bsdsocket.library not found (is a TCP/IP stack running?)\n");
        goto out_timer;
    }

    if (!resolve((char *)args[ARG_SERVER], &p->server_ip)) {
        Printf("amisqueeze: server %s not found\n", args[ARG_SERVER]);
        goto out_socket;
    }
    Printf("player \"%s\" (%02lx:%02lx:%02lx:%02lx:%02lx:%02lx), server %s port %ld\n",
           (LONG)p->name, (LONG)p->mac[0], (LONG)p->mac[1], (LONG)p->mac[2],
           (LONG)p->mac[3], (LONG)p->mac[4], (LONG)p->mac[5], args[ARG_SERVER], (LONG)p->server_port);

    run(p);
    slim_disconnect(p);
    PutStr("stopped\n");
    rc = RETURN_OK;

out_socket:
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
