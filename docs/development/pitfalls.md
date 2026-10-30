# Pitfalls

For contributors who are stuck: what has already gone wrong, and how to debug. How amisqueeze works is in
[architecture.md](architecture.md).

## The protocol

- **The references** are squeezelite (`slimproto.c`, `slimproto.h`, `stream.c`, `pcm.c`,
  https://github.com/ralph-irving/squeezelite) and LMS (`Slim/Networking/Slimproto.pm`). They are GPL: read them
  to understand the protocol's behaviour (message layouts, when to send which status), never copy them. The
  project is MIT.
- **LMS drops every status after a `strm s` until `STMc`**. So a refusal must come after a `STMc`, or LMS waits
  for that track forever ([a track's life](protocol.md#a-tracks-life-the-statuses)).

## The toolchain

- **The compiler**: gcc 16.1.1b generated wrong code, hence
  [the safeguard](building.md#the-safeguard-against-a-compiler-bug).
- **vamos**, shipped with the `amigadev/crosstools` image, runs command-line tools that only use dos.library.
  Useful for argument parsing only; test the rest in WinUAE. Its limits:
  - it crashes on `printf` (libnix);
  - it has no windows;
  - it has no real `bsdsocket.library`: its stub makes amisqueeze retry forever (kill it);
  - 68020 programs need `vamos -C 68020`, otherwise alert `00068020`;
  - its `GetVar` ignores global variables (`ENV:`) and returns 0, so default options cannot be tested with it.
- **`-m68881` and libnix**: libnix links a start-up check (`__fpucheck`). Without an FPU, it shows an alert and
  quits before `main()`, without a message. `amiga.c` replaces it with an empty one and checks `AttnFlags` itself.
- **The FPU code on a 68040 or 68060**: the `-m68881` code uses `fintrz` and `fmovecr`, which their CPU library
  emulates, slowly.
- **The `$VER:` string** stays non-const, in the data hunk (`tools/check-version.py` checks it). Const, it sat in
  the code hunk, and AmigaOS 3.2's `Version` printed "amiseeze 0.3" for the 0.3 build, although the bytes were
  right (only when reading the file as an executable). Hence `Version <exe> FULL` for both executables before
  each release.

## The Workbench start and the window

- **Workbench start and libnix**: started from the Workbench, libnix opens the console named by `__stdiowin`
  before `main()`. `amiga.c` sets it to NULL, and opens its own once it has read the icon (`WINDOW`, `QUIET`).
  - While the window is closed, there is no output (`msg()` checks `Output()`), or `NIL:` from `Run >NIL:`.
  - When the window opens, and what it shows:
    [architecture](architecture.md#the-amiga-side-start-up-window-commodity).
- **The commodity**: amisqueeze is a commodity ("Remove" in Exchange = Ctrl-C), one at a time (`CBERR_DUP`). A
  second start does nothing to the running one. Show and Hide (`COF_SHOW_HIDE`) only with a window of its own.

**The message window** (`CON:`): how it works and why is in the
[architecture](architecture.md#the-amiga-side-start-up-window-commodity). What was tried in WinUAE
with AmigaOS 3.2.3, and `ACTION_FORCE` and `WAIT` also with Kickstart 3.1, without Workbench:

- **the close gadget** does not close the window. Without `AUTO`, its click is kept for the next read:
  - an EOF in cooked mode;
  - the event `CSI 11;...|` in RAW mode. `CSI 11{` asks for that event, which AmigaOS 3.2 sends unasked (not
    tried with Kickstart 3.1);
- **`ACTION_FORCE`** (2001) is not in the NDK. `ACTION_STACK` and `ACTION_QUEUE` do not end a pending read;
- **`WAIT`** does not hold `Close()`: the window outlives the handle;
- **`SMART`** prevents selecting and copying the text. Without it, the text was kept when uncovered on AmigaOS
  3.2.3, contrary to what was seen; not tried with Kickstart 3.1;
- **showing the window** costs the one task about 0.2 s to open it, and 0.2 to 0.3 ms a byte of text, on a slow
  68020 with Paula. With 3 audio blocks, the sound stops for a moment, also with `PRI=1`;
- the window has no scroll back.

## Debugging

There is no source-level debugging yet. Meanwhile, WinUAE's built-in debugger (`Shift+F12`) follows the program at
the assembly level.

**Planned** (step tools-8):

- `bgdbserver` (bebbo, Aminet `dev/debug/bgdbserver`) in the emulated Amiga, with WinUAE's `bsdsocket.library`
  emulation;
- `m68k-amigaos-gdb` (`target remote …:2345`), driven by a cpptools `launch.json`;
- a build with `-g`.

To check first: that WinUAE's port 2345 can be reached from wherever gdb runs.

**Ruled out**:

- uae-dap (prb28), which needs SAS/C `LINE DEBUG` hunks;
- Bartman's amiga-debug: its own toolchain, NDK 3.9, no libc.
