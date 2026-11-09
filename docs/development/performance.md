# Performance

For a contributor: what costs the CPU when amisqueeze plays, what was done to lower it, and how it is measured.
How each figure was checked is in the message of the commit that brought the change.

## What costs the CPU

On the slowest machine tried, an A1200 with a 68EC020 at 26.67 MHz, amisqueeze needs about a third to three
quarters of the CPU. Three things share it:

- **The network reception**: the TCP/IP stack and the network card's driver, which copies every byte received.
  On the A1200 it is the largest share: 25 % of the CPU for 16-bit stereo at 28.6 kHz, 37 % at 44.1 kHz.
- **The sound output**: AHI's conversion and mixing for Paula, about 30 points on the A1200; Paula without AHI,
  5 to 10 points.
- **Decoding** FLAC or MP3: only on a machine that `BENCH` finds fast enough. Otherwise the server decodes, and
  the Amiga receives raw PCM, the heaviest stream for the network.

The core's own work (the main loop, the ring, the statuses) is a few percent: 4.6 % on the slow WinUAE profile,
where the network costs nothing.

So the means that matter are, in this order: less to receive (`MAXRATE`), a lighter output (`AUDIO=PAULA`), a
lighter network driver. The volume left at 100, or fixed by the server, keeps the samples from being scaled.

## How it is measured

With `cpucount`, a loop at the lowest priority that counts its rounds; [testing.md](testing.md#cpu-load) says
how to run it.

- **The load** is 1 - count while playing / count when idle, over three windows of 10 s each, the first a few
  seconds after playback starts. The windows of one run usually agree within about 1 point.
- **The idle count** is taken in the same session: about 13,500 on the A1200, about 8,300 on WinUAE's slow profile,
  about 8,030 on the slow profile with a real network stack running.
- **Compare only within a session.** On the slow profile, AHI's Paula 14-bit mode measured 65 % in one session of
  0.7 and 53 % in another: not explained.
- **A probe build** gives the ceiling of an optimization: the same code with one cost removed, such as the writes
  to chip RAM sent to fast RAM. It plays wrong, but its load is the best the optimization can reach.
- **The server sends** `song-44k.flac`, a 44.1 kHz 16-bit song, as raw PCM at volume 100, unless a row says
  otherwise. No figure below had a `STMo` (the buffer never ran dry).

The machines:

| Machine | What it is | What it tells |
|---|---|---|
| A1200 | ACA1221lc accelerator (68EC020 at 26.67 MHz, no FPU, 32-bit fast memory), AmigaOS 3.1, a Xircom CE3B-100 PCMCIA card with `xircomce2.device` 1.1 fixed (below), AmiNetXDuo 1.0.0-rc2, AHI 4.18 with `paula.audio` 4.23, LMS 9.1.1 with the [`MAXRATE` rules](../user-guide.md#maxrate) | the real figures |
| WinUAE, slow profile | a 68020 at 28.4 MHz, WinUAE's approximate mode ([testing.md](testing.md#profiles)), its TCP/IP emulation on the host | the core and the output alone: the network costs the Amiga nothing |
| WinUAE, slow profile, real network | the same with AmiNetXDuo and `cnet.device` on WinUAE's NE2000 PCMCIA card ([testing.md](testing.md#real-network)) | an estimate of the network's cost |
| WinUAE, JIT profile | about 230 times the slow profile, the order of a PiStorm | decoding on the Amiga |

WinUAE's slow profile is not cycle-exact: its gains are not the A1200's, in either direction. The rate divided by
2 made lighter gained 15 points there and 14 on the A1200; the instruction-cache variant below, 6 points there and 1.2 on the A1200.
Speeds measured in WinUAE are not a real machine's ([testing.md](testing.md#profiles)).

## The figures

### On the A1200

Three builds of 1.0-dev, each in its own session. AHI's path did not change between them.

- **A**: the first Paula output without AHI;
- **B**: A with chip RAM written 4 samples at a time;
- **C**: B with the rate divided by 2 made lighter (both [below](#paula-without-ahi)).

Music Assistant was 2.10.9 with a local fix, not published, for rates below 44.1 kHz.

| Output | Stream received | Load | Build |
|---|---|---|---|
| `NOAUDIO` (reception only) | 16 bits, 28,603 Hz | 25 % | A |
| `NOAUDIO`, `MAXRATE=48000` | 16 bits, 44.1 kHz | 37 % | B |
| `AUDIO=PAULA8` | 16 bits, 28,603 Hz | 30 % | B |
| `AUDIO=PAULA` | 16 bits, 28,603 Hz | 35 % | B |
| `AUDIO=PAULA` at volume 50 | 16 bits, 28,603 Hz | 35 %, as at 100 | B |
| `AUDIO=PAULA`, Music Assistant (`wav`) | 16 bits, 24 kHz | 30 % | B |
| `AUDIO=PAULA`, `MAXRATE=48000` | 16 bits, 44.1 kHz, played at 22,030 Hz | 53 % | C |
| AHI, `Paula:14 bit stereo++` at 27,429 Hz, `MAXRATE=AHI` | 16 bits, 27,429 Hz | 57 % | A |
| AHI, `Paula:14 bit stereo++` at 27,429 Hz, no `MAXRATE`, Music Assistant (`wav`) | 16 bits, 44.1 kHz | 77 % | B |

`PAULA` and `PAULA8` cost the same at volume 50 and 100: the volume is Paula's. Through AHI, the samples are
scaled below 100: about 100 % of the CPU at volume 50, with crackles and gaps (0.8).

### On WinUAE's slow profile

Without a real network (the reception costs about 5 %):

| Output | Stream received | Load | Build |
|---|---|---|---|
| `NOAUDIO` | 16 bits, 44.1 kHz | 4.6 % | 0.7 |
| AHI, `Paula:14 bit stereo++`, 27,429 Hz | 16 bits, 44.1 kHz | 65 % | 0.7 |
| AHI, `Paula:14 bit stereo++`, 22,050 Hz | 16 bits, 44.1 kHz | 59 % | 0.7 |
| AHI, `Paula:8 bit stereo++`, 27,429 Hz | 16 bits, 44.1 kHz | 60.5 % | 0.7 |
| AHI, `Paula:8 bit stereo++`, 22,050 Hz | 16 bits, 44.1 kHz | 55 % | 0.7 |
| AHI, `Paula:14 bit stereo++`, 27,429 Hz | 24 bits, 48 kHz (Music Assistant) | 75 % | 0.7 |
| AHI, `Paula:14 bit stereo++`, 27,429 Hz | 16 bits, 28,603 Hz | 54 % | 1.0-dev, A |
| `AUDIO=PAULA8` | 16 bits, 28,603 Hz | 9 % | 1.0-dev, B |
| `AUDIO=PAULA` | 16 bits, 28,603 Hz | 15 % | 1.0-dev, B |

Each row is its own session. Each lighter AHI setting saves 5 to 6 points, both together about 10.

With a real network (AmiNetXDuo, `cnet.device`), each column in its own session:

| Output | Stream received | Without the stack | With it | Build |
|---|---|---|---|---|
| `NOAUDIO` | 16 bits, 44.1 kHz | 4.5 % | 45 % | 0.7 |
| AHI, `Paula:14 bit stereo++`, 27,429 Hz | 16 bits, 44.1 kHz | 53 % | 92 % | 0.7 |

The A1200 did better: the reception alone at 44.1 kHz took 47 % with Aminet's driver (0.8), 37 % with the fixed
one (1.0-dev). WinUAE has another driver and card, and is not cycle-exact.

### Decoding

`BENCH` decodes a built-in FLAC level 8 sample and, in `amisqueeze.fpu`, an MP3 at 320 kbit/s:

| Machine | FLAC level 8 | MP3 320 kbit/s |
|---|---|---|
| A1200 | 0.2x real time: left to the server | no FPU |
| WinUAE, 68020 at about 45 MHz, no JIT | 0.2x | not measured |
| WinUAE, JIT profile with a 68882 (`compfpu`) | 77x | 74x |
| the same without `compfpu` | | 1.8x |

A format is decoded on the Amiga only at twice real time or more ([user guide](../user-guide.md#flac-mp3-and-bench)).

## What was done, and what it gained

### The main loop reads in large pieces

The stream is read only once 8 KB of the ring are free (`READ_MIN_SPACE` in `stream.c`). Before, the loop read
as soon as one byte was free, a few hundred bytes at a time: 470 to 875 turns a second, each reading the clock.
Now 18. WinUAE's slow profile, `NOAUDIO`, 44.1 kHz (0.7): 40 % to 4.6 %. Through AHI, where the loop already
read whole blocks: unchanged.

### The volume

Below 100, the AHI output scales the samples itself (`ahir_Volume` stays full: [architecture.md](architecture.md#ahi)).
Two 256-entry tables replace a 32-bit multiplication per sample (about 43 cycles on a 68020). Slow profile,
`Paula:14 bit stereo++`, volume 50 (0.1): 100 % to 88 % (65 % at volume 100). At 100, or fixed by LMS, nothing is
scaled. Paula without AHI never scales: the volume is Paula's own.

### The network driver and the stack

`xircomce2.device` 1.1 handles every frame waiting in one software interrupt, at the same priority as the one
in which `paula.audio` fills its next block. A large burst makes AHI late, and the sound crackles. AmiNetXDuo's
`RXBUFFER` caps the TCP window, and so the bursts, at a cost. The fix: at most 4 frames per pass, then the
interrupt caused again. A1200, AHI `Paula:14 bit stereo++`, 44.1 kHz (0.8, three sessions: the first row, the
next two, the last):

| Driver | `RXBUFFER` | Load | Crackles |
|---|---|---|---|
| Aminet's 1.1 | none | 73 % | occasional |
| Aminet's 1.1 | 8192 | 92 % | none |
| the same sources, built with GCC 16 | 8192 | 80 % | none |
| fixed (4 frames per pass) | none | 77 % | none |

The fixed driver is not published.

The stack, the same machine (0.8):

| Stack | `NOAUDIO`, 44.1 kHz | AHI, 44.1 kHz |
|---|---|---|
| Miami 3.2b, Aminet's driver | 84 % | about 100 %, gaps |
| AmiNetXDuo 1.0.0-rc2, Aminet's driver | 47 % | 73 %; 69 % in `Paula:Fast 14 bit stereo++` |
| AmiNetXDuo, fixed driver | | 77 % |
| Roadshow 1.15 demo (its 68000 build), fixed driver | | 86 % |

### Less to receive: `MAXRATE`

The server lowers the rate ([user guide](../user-guide.md#maxrate)), so the Amiga receives less and AHI converts
less. A1200, AHI `Paula:14 bit stereo++` at 27,429 Hz (0.8):

| Server and option | Stream received | Load |
|---|---|---|
| LMS, none | 16 bits, 44.1 kHz, 176 KB/s | 77 % |
| LMS, `MAXRATE=32000` | 16 bits, 32 kHz, 128 KB/s | 62 % |
| LMS, `MAXRATE=22050` | 16 bits, 22.05 kHz, 88 KB/s | 50 % |
| Music Assistant, `MAXRATE=32000` | 24 bits, 32 kHz, 192 KB/s | 77 % |
| Music Assistant, `MAXRATE=22050` | 24 bits, 22.05 kHz, 132 KB/s | 60 % |

Music Assistant was 2.10.7 with the local fix above. The load follows the bytes received, whatever the server. Music Assistant sends 16 bits once its volume
normalization is off: 62 % at 32 kHz, as LMS.

### Paula without AHI

`AUDIO=PAULA` and `PAULA8` write straight into chip RAM, without AHI's mixing ([architecture.md](architecture.md#paula-direct)).
A1200, 1.0-dev, the stream at Paula's rate: AHI 57 %, `PAULA` 35 %, `PAULA8` 30 %, the reception alone 25 %
([the figures](#on-the-a1200)). On the slow profile: 54 %, 15 % and 9 %.

Its conversion has fast paths. Each was measured against the code before it:

| Change | Machine | Stream | Before | After |
|---|---|---|---|---|
| The bytes taken as they are when the rate is not divided (in A) | slow profile | 28,603 Hz, `PAULA` | 85 % | 20 % |
| The rate divided by 2 by an addition and a shift (in A) | slow profile | 44.1 kHz, `PAULA` | 84 % | 45 % |
| Chip RAM written 4 samples at a time (B) | A1200 | 28,603 Hz, `PAULA` | 39 % | 35 % |
| The same | A1200 | 28,603 Hz, `PAULA8` | 31 % | 30 % |
| The same | slow profile | 28,603 Hz, `PAULA` | 20 % | 15 % |
| Divided by 2, the mean without rebuilding the samples (C) | A1200 | 44.1 kHz, `PAULA` | 66.8 % | 53.1 % |
| The same | slow profile, real network | 44.1 kHz, `PAULA` | 82 % | 67 % |

The probes, with the chip RAM writes sent to fast RAM: 32 % for B, 51.3 % for C. So the
writes now cost 2 to 3 points; the rest of the output is the computing.

## What was tried and not kept

- **The rate divided by 2 with a loop that fits the 68020's instruction cache**. The
  cache holds 256 bytes; C's loop takes 484. The variant took 240: two means a round, each channel's 4
  bytes gathered over two rounds, the samples' bytes swapped by `ror.w` and `swap`, then the two words' mean
  `(a & b) + ((a ^ b) >> 1)`, its sign fixed by one XOR. It needed a copy of the loop per byte order and per 8 or
  14 bits. Bit for bit the same, the sound the same. On the A1200, 53.1 % to 51.9 %; on the slow profile with a
  real network, 67.3 % to 61.3 %. Not kept: 1.2 points on a path that is not the default one (the server lowers
  the rate), for four copies of the loop. The gain would be larger with slower memory, on machines too slow for
  that path anyway.
- **A 32-bit multiplication per sample for the volume**: 100 % on the slow profile at volume 50; replaced by the
  tables above.
- **The general conversion path for Paula**, a function and a loop per sample: 85 % on the slow profile, more than
  AHI. Kept for the rare divisors 3 and 4 only.
- **`RXBUFFER`** against the crackles: clean at 8192, at 92 %. Not needed with the fixed driver.
- **Other stacks**: Miami and the demo of Roadshow cost more than AmiNetXDuo (above). The commercial build of
  Roadshow, compiled for the 68020, was not measured.

## What is left to gain

None of these is measured; each is a lead, with what is known of it.

- **The reception**, the largest share on the A1200. The Xircom's driver reads each frame 2 bytes at a time. A
  faster path through the card's shared memory exists, but only when no system memory covers the PCMCIA range
  `$600000`-`$9FFFFF`; the ACA1221lc's memory does. The driver is a separate project.
- **AHI's Paula driver 4.33**: whether a larger AHI buffer (`AHIpaulaBufferLength`) stops the crackles with the
  PCMCIA network card, and at what load, on the A1200.
- **A lighter system**: a trimmed install, MapROM. A few points each, estimated.
- **The clock read at every turn of the main loop** (`ReadEClock` and a 64-bit division): a build with one more
  read per turn went much slower, a hint that it is costly.
- **The byte swap into AHI's blocks**: estimated at a few percent.
- **Slower machines**: a stock A1200 (68EC020 at 14 MHz, trapdoor memory) is estimated at 60 to 85 % at
  22.05 kHz through AHI. There, Paula without AHI would matter most.
- **Costs to come**: a calibrated 14-bit output and a rate conversion with a real filter would add work to Paula's
  path (left for later).
