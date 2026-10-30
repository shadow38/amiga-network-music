# Code rules

For contributors who change the code: the rules it follows, and its limits on stack and memory. How amisqueeze
works is in [architecture.md](architecture.md); building and testing in [building.md](building.md) and
[testing.md](testing.md).

amisqueeze targets classic Amigas with a 68020 or better, of two kinds: **fast** (a PiStorm with Emu68: a 68040
with FPU) and **slow** (a 68020 accelerator without FPU), with sound through AHI: Paula (`paula.audio`, on Amigas
without a sound card) or another AHI driver (a sound card, or the PiStorm's own outputs: the I2S DAC of the
PiStorm32-lite FPGA firmware, HDMI with a Pi 4). The code uses only the `ahi.device` features of AHI 4 (tested
with AHI 6.6; 4.x is expected to work, not checked).

## The rules

- **Portable core**: every file of `src/amisqueeze/` but `amiga*.c` and `posix.c` is standard C (`<stdint.h>`, no
  Amiga or POSIX header: `tools/check-core.py`, run by `make test`, accepts only the C library's headers, the
  program's own and the third-party ones); everything system-related goes through `platform.h`. Keep the core
  free of system calls and the platform layers thin; the main loop (timeouts, reconnection) lives in the core.
- **One task, one loop** around `plat_wait()`: `WaitSelect()` on the Amiga, where a `SocketBase` belongs to the
  task that opened it; `select()` on POSIX, with SIGINT and SIGTERM set without `SA_RESTART`, so that Ctrl-C stops
  the wait.
- **Byte order**: addresses and ports in host byte order in the core; `htonl`/`ntohl` only in the platform layers
  (no-ops on 68k, essential on x86).
- **Serialisation**: fields byte by byte, no packed structures; 32-bit values are `uint32_t` (the MAC hash too, so
  that both builds derive the same MAC).
- **`HELO`** advertises only the formats the build actually decodes.
- **Positions**: the ring's (bytes of the stream) and the output's (bytes of decoded PCM) are never mixed
  ([two kinds of position](architecture.md#two-kinds-of-position)). Raw PCM cannot reveal a mix-up: the test-only
  "widening" decoder of `tests/test_core.c` can.
- **`OUTPUT`** writes from `hand_over()` in `stream.c` only, the one place where the output hands PCM over.
- **Messages** go through `msg()`, with the standard printf format (`format(printf)` attribute, `-Werror=format`
  on both builds; on the Amiga, libnix `vsnprintf` into a static buffer, then `Write()` to `Output()`,
  unbuffered, so that a log redirected to a file can be read while the program runs), raw bytes through
  `msg_raw()`. Never `Printf`, `PutStr` or `printf` directly, except inside the platform layers' own
  implementation of `msg()`, `msg_raw()` and the `LOG` file. Cast 32-bit values to the type the specifier names
  (`(unsigned long)` with `%lX`); `%c` for chars (not `%lc`). `STMt` (one a second) is not printed with `VERBOSE`.
- **Text for the Amiga**: strings shown on the Amiga and files it reads are ASCII only (the Amiga uses
  ISO-8859-1, the sources are UTF-8), with LF line endings. The README and the user guide are among them, since
  they ship in the archives: `...`, `-`, `|` rather than their typographic forms.
- **ahi.device**: keep the rules of [AHI](architecture.md#ahi) (a request chained only to one already playing,
  the playing one aborted first, `CMD_STOP` is no pause). **Never send `ahir_Volume` below full**: the core scales
  the samples instead.
- **Third-party code** goes in `third_party/`, unmodified, with its version, commit and licence in a `README.txt`,
  each library built in a file of its own without our warnings (`THIRD_PARTY` in the `Makefile`). A file that
  includes a third-party header and reads its structures must use the same configuration macros as the file that
  builds it: otherwise the structures' layout differs, without a warning. What each decoder needs from its
  caller: [decoders](architecture.md#decoders).
- **Size and complexity**, measured by [lizard](https://github.com/terryyin/lizard) on the C sources of `src/`,
  `tests/` and `tools/amiga/`, but `third_party/` and the generated `bench_*.c`:
  - per function: a cyclomatic complexity of 15 at most, 60 lines of code and 6 parameters. The complexity is
    lizard's `-m` count: a whole `switch` counts once, so that a flat list of cases is not taken for a complex
    function;
  - per file: 800 lines of code, the tests' files excepted;
  - lines of code are lizard's NLOC: neither comments nor blank lines.

  `tools/check-quality.py`, run by `make test`, enforces them. The functions over a limit today are listed in it
  (`OVER`) with their values: a listed function may not grow, and leaves the list once it fits. Raising a listed
  value needs the maintainer's agreement, with the reason in the commit.
- **Comments** explain the code; they do not point to the project's history. No step name, no "plan step", no
  review or version, no "decided with the user": give the reason in a sentence, or link to `architecture.md`. A fact
  worth keeping stays, said plainly ("seen in WinUAE"). This holds for all the code: C, Python, shell, the
  `Makefile`, the CI's workflows and the files of `tests/data/`. `tools/check-quality.py` fails on a step name.
  Commit messages keep the step names.

## Stack and memory

- The default Shell stack is 4 KB, and AmigaOS does not detect an overflow (silent corruption). Big buffers are
  static or allocated, never on the stack.
- Both executables set `__stack` (16 KB `amisqueeze`, 32 KB `amisqueeze.fpu`, in `amiga.c`) and link libnix's
  `__stkinit`, which gives them that stack when the Shell's is smaller: dr_flac needs about 5.5 KB (4.6 KB in
  `drflac_open` alone), minimp3 about 17.5 KB (most of it in `mp3dec_decode_frame`). After each link,
  `tools/check-stack.py` reads the linker map (`obj/<prog>.map`): the build fails if `__stkinit` is not linked,
  `__stack` is not defined in `amiga.c`, or its value is not the one the `Makefile` expects (`<prog>_STACK`, to
  change with it).
- Measured with the probe of [Checks the host tests cannot make](testing.md#checks-the-host-tests-cannot-make),
  from a script with `Stack 4096`: `amisqueeze` on 2026-10-29 (0.8-dev, on an A1200 with AmiNetXDuo), 5688 of
  16384 bytes with the server discovery, raw PCM and a seek, 5632 with FLAC and a seek; `amisqueeze.fpu` on
  2026-10-23 (0.7, in WinUAE), 17544 of 32768 with FLAC and MP3. Redo it when `-fstack-usage` shows the deepest
  path growing by more than 1 KB, or when a new decoder, library or start-up path is linked; a few bytes more do
  not call for it.
- Memory: see `Avail` in [Checks the host tests cannot make](testing.md#checks-the-host-tests-cannot-make).
