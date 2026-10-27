/* cpucount: free CPU meter for the tests of amisqueeze (docs/development/testing.md, "CPU load"); a developer
 * tool, built by "make cpucount", not part of the releases.
 * Runs at priority -128, so it only gets the time no other task wants, counts loop rounds for SECS seconds of the
 * Amiga's clock and prints the count. Load of a program = 1 - (count while it runs) / (count when idle), same
 * SECS, same machine. Start it with Run, never in the foreground of a Shell that is driven from the host.
 * Usage: cpucount [SECS] (default 10). */
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>

static volatile unsigned long spin;

/* Ticks of the Amiga's clock since 1978, modulo 2^32 (unsigned: it wraps every 994 days, a difference stays right) */
static unsigned long ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ((unsigned long)ds.ds_Days * 1440UL + (unsigned long)ds.ds_Minute) * 60UL * TICKS_PER_SECOND
           + (unsigned long)ds.ds_Tick;
}

int main(int argc, char **argv)
{
    long secs = 10;
    unsigned long start;
    unsigned long rounds = 0;
    int i;

    if (argc > 1) {
        secs = 0;
        for (i = 0; argv[1][i] >= '0' && argv[1][i] <= '9' && secs < 86400; i++)   /* a day at most */
            secs = secs * 10 + (argv[1][i] - '0');
        if (secs <= 0)
            secs = 10;
    }
    SetTaskPri(FindTask(NULL), -128);
    start = ticks();
    while (ticks() - start < (unsigned long)secs * TICKS_PER_SECOND) {
        for (i = 0; i < 1000; i++)
            spin++;
        rounds++;
    }
    Printf("cpucount: %lu rounds in %ld s\n", rounds, secs);
    return 0;
}
