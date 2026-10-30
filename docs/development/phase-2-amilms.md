# Phase 2 — amilms, remote control

Back to the [plan](README.md).

A remote control from the Amiga for LMS, then for Music Assistant. It drives any player of the server
(amisqueeze, squeezelite, Squeezebox…). **amilms** is a working name: **amiremote** is proposed, since the remote
is no longer LMS only; the name is decided by the study (remote-0), and the files are renamed then.

- **LMS**: the text CLI (TCP 9090): one command per line, a one-line reply, URL-encoded parameters (`%20`…),
  `key:value` pairs (steps remote-1 to remote-7).
- **Music Assistant**: its API, JSON messages over a WebSocket, with an access token (steps remote-8 to remote-11).
- **MPD** (idea): a third back end for the Music Player Daemon (step remote-12).

## Organisation (decided)

As in amisqueeze: a **portable core** in standard C, tested on the host, and thin layers around it.

- **Core**: the common interface (players, now playing, search results, queue; operations; events from the
  server), the state, and two **back ends** behind it: LMS CLI, then Music Assistant. Host tests (`make test`)
  against fake servers in Python, like `tests/fake_server.py`.
- **Shell front end** first (remote-1 to remote-5): commands such as `amilms SEARCH "beatles"`, `amilms PLAY`,
  `amilms STATUS`; used for development, automated tests and scripts.
- **GUI front end** (remote-6): calls the **same core directly, in the same program** (not a GUI starting a CLI program
  and parsing its output: no `fork` on AmigaOS, clumsy pipes, a program start per action). One task and one loop
  waiting on both the sockets and the window (`WaitSelect()` with the window's signal), as in amisqueeze.
- **Later**: an ARexx port on the GUI version, the Amiga way for scripts to drive a running program.
- Whether both front ends live in one executable (Shell with arguments, window from the Workbench) or two is
  decided by the study (remote-0).

Network code shared with amisqueeze (connection, line reading, logging, Ctrl-C) goes into `src/common/`.

## remote-0 Study: common interface and Music Assistant feasibility ⬜ (before any amilms code)

- **Goal**: fix the interface the front ends rely on, so that the Music Assistant back end fits in later without
  reworking the core or the GUI.
- **Content** (documents only, no code):
  - compare what each server offers, from its documentation: players, now playing, transport commands, volume,
    search, library browsing, queue, notifications (LMS `subscribe` / `listen`; Music Assistant events);
  - define the common interface: data (player, track, search result, queue item), operations, events, and what
    exists on one side only;
  - Music Assistant feasibility on a 68020: WebSocket client (HTTP upgrade, framing, client masking), JSON parser
    (small, MIT-compatible: e.g. jsmn or cJSON, both MIT), memory use of large replies (paging), token handling
    (cleartext on the LAN, no TLS), stability of the API across Music Assistant versions;
  - check that the interface also suits MPD (remote-12), even if it comes last: a server that plays by itself, with one
    queue instead of players;
  - name of the program and organisation of the executables;
  - Music Assistant test server (tools-9).
- **Criterion**: the interface and the conclusions are written in this plan and agreed with the user.

**Other remote for Music Assistant: [ami2ha](https://github.com/evil4dmin/ami2ha)** (native AmigaOS Home
Assistant client, MIT, MUI 3.8+, 68020+, WebSocket API with a long-lived token, AmiSSL optional, otherwise the token
travels in cleartext). Music Assistant players linked to Home Assistant show up there as `media_player` entities,
which its `media` widget drives: now playing, previous / play-pause / next, volume. According to its README (not
tested here): no library browsing, search or queue, and no documented generic service call (such as Music
Assistant's `play_media` or `search` actions); it needs Home Assistant (a standalone Music Assistant is out of its
reach). So it covers basic control of amisqueeze under Music Assistant, while search stays amilms' job on LMS
(remote-4). To test it, a Home Assistant + Music Assistant test server is needed (not set up).

## remote-1 LMS: CLI connection ⬜

- **Content**: connect to `<server>:9090`, send `version ?`, read and decode the reply.
- **Criterion**: `amilms SERVER=<ip>` prints the server version.
- **Test**: test server; on the host, reply parsing (`%xx` decoding, splitting into pairs).
- **Documentation** (agreed with the user): with the first amilms page, one guide per program;
  amisqueeze's stays one file in two parts, using it then a reference. To adapt with it: `tools/check-version.py`
  (the state line), `tools/make-dist.py` (the zip), the links and anchors (README, CONTRIBUTING, `CLAUDE.md`, the
  plan, `rules.md`, `amiga.c`), and the review agent `review-tests-docs` (the user's decision, as for any change of
  the review tooling).

## remote-2 Players and basic commands ⬜

- **Content**: player list (`players 0 <n>`), choosing a player; `play`, `pause`, `stop`, `playlist index +1/-1`,
  `mixer volume`.
- **Criterion**: from the Amiga, the squeezelite player on the PC starts, stops, changes track and volume.
- **Volume**: amilms changes it through LMS (`mixer volume`), which sends it to the player (`audg`), as LMS's own
  interface does; nothing special for amisqueeze. When LMS's "Output level is fixed at 100%" is chosen for the
  player (step player-10), LMS reports `use_volume_control 0`: the remote should then grey its volume control, the
  volume being set on the amplifier; a `MIXER` mode driven from the Amiga would need amilms to talk to amisqueeze
  itself: see the volume modes of plan step player-10.

## remote-3 Real-time state ⬜

- **Content**: current track, artist, duration, progress (`status`); server notifications (`subscribe` /
  `listen`) instead of polling.
- **Criterion**: a change made from the LMS web UI shows up on the Amiga within a second.

## remote-4 Library and queue ⬜

- **Content**: browsing artists → albums → tracks; play or add an album; show and edit the queue; **search**
  (a main goal of amilms): a search term returns artists, albums and tracks from the library (LMS CLI
  `search 0 <n> term:<text>`, or `artists` / `albums` / `titles … search:<text>`), and a result can be played
  or added to the queue (`playlistcontrol cmd:load|add track_id:… / album_id:…`). Commands written from memory:
  check them against the server's CLI documentation (LMS web UI, Help → Technical Information → CLI) at remote-1.
- **Criterion**: pick an album from the Amiga and play it on a player; search a track by part of its title and
  play it.

## remote-5 Service menus ⬜

- **Content**: the server's generic menus (radio, plugins), which the CLI exposes without the remote having to know
  them; Deezer through its LMS plugin (depending on the plugin's state).
- **Criterion**: start a radio station, then a Deezer track, from the Amiga.

## remote-6 Graphical interface ⬜

- **Content**: a window with lists, transport buttons, volume, progress. To decide: ReAction (shipped with
  AmigaOS 3.2) or MUI.
- **Criterion**: every feature of remote-2 to remote-5 usable with the mouse.

## remote-7 Real machine ⬜

- **Criterion**: same features on the real machine, with good responsiveness.

## Music Assistant back end (remote-8 to remote-11)

Behind the same interface as LMS, so the Shell and GUI front ends need no change; content to be refined by the
study (remote-0). Prerequisite: the Music Assistant test server (tools-9). It may come after a first release of the LMS
remote.

## remote-8 Music Assistant: connection ⬜

- **Content**: WebSocket client (HTTP upgrade, frames, client masking, ping), access token, JSON parsing; read
  the server information.
- **Criterion**: `amilms SERVER=<ip> …` (Music Assistant mode, option to be defined) prints the server version.
- **Test**: host first (fake WebSocket server in Python: fragmented frames, large replies, disconnection).

## remote-9 Music Assistant: players, commands, state ⬜

- **Content**: player list, transport commands, volume, now playing; events instead of polling.
- **Criterion**: same as remote-2 and remote-3, on Music Assistant (amisqueeze and squeezelite as players).

## remote-10 Music Assistant: search, library, queue ⬜

- **Criterion**: same as remote-4 on Music Assistant: search a track by part of its title and play it; pick an album
  and play it; show the queue.

## remote-11 Music Assistant: GUI and real machine ⬜

- **Criterion**: the GUI of remote-6 works the same with both servers; same features on the real machine.

## remote-12 MPD back end (idea) ⬜

- **Why**: no MPD client was found for AmigaOS (search: Aminet, OS4, MorphOS). MPD plays the music
  itself, on the machine it runs on: on the Amiga this is a remote control, so it belongs here, not in amisqueeze.
  Probably the easiest back end: a text protocol over TCP 6600 (one command per line, `key: value` replies ending
  with `OK` or `ACK`), no TLS, an optional password; `idle` for the notifications (like LMS `listen`).
- **Content**: the same features as the LMS back end (remote-2 to remote-4: transport, volume, now playing, library, search,
  queue) behind the common interface; MPD has one player per server (its outputs are chosen, not its players).
  Host tests against a fake server in Python; a test MPD server to set up.
- **Also possible**: MPD's `httpd` output streams what it plays as MP3, FLAC or WAV over HTTP; playing it on the
  Amiga would be a player's job (amisqueeze's decoders could serve), not this remote's. Not planned.
- **Criterion** (to agree when the step starts): same as remote-2 to remote-4 and remote-6 with an MPD server.
