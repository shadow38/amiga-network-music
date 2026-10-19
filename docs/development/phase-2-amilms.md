# Phase 2 — amilms, remote control

Back to the [plan](README.md).

A remote control for LMS from the Amiga, through the LMS text CLI (TCP 9090): one command per line, a one-line
reply, URL-encoded parameters (`%20`…), `key:value` pairs. It drives any player (amisqueeze, squeezelite,
Squeezebox). Music Assistant (JSON API, token, WebSocket) is out of scope for now ⏸.

Network code shared with amisqueeze (connection, line reading, logging, Ctrl-C) goes into `src/common/`.

## 2.1 CLI connection ⬜

- **Content**: connect to `<server>:9090`, send `version ?`, read and decode the reply.
- **Criterion**: `amilms SERVER=<ip>` prints the server version.
- **Test**: test server; on the host, reply parsing (`%xx` decoding, splitting into pairs).

## 2.2 Players and basic commands ⬜

- **Content**: player list (`players 0 <n>`), choosing a player; `play`, `pause`, `stop`, `playlist index +1/-1`,
  `mixer volume`.
- **Criterion**: from the Amiga, the squeezelite player on the PC starts, stops, changes track and volume.
- **Volume**: amilms changes it through LMS (`mixer volume`), which sends it to the player (`audg`), as LMS's own
  interface does; nothing special for amisqueeze in its default mode. With amisqueeze's possible `FIXED` mode the
  volume is set on the amplifier; a `MIXER` mode driven from the Amiga would need amilms to talk to amisqueeze
  itself: see the volume modes of plan step 1.10.

## 2.3 Real-time state ⬜

- **Content**: current track, artist, duration, progress (`status`); server notifications (`subscribe` /
  `listen`) instead of polling.
- **Criterion**: a change made from the LMS web UI shows up on the Amiga within a second.

## 2.4 Library and queue ⬜

- **Content**: browsing artists → albums → tracks; play or add an album; show and edit the queue; search.
- **Criterion**: pick an album from the Amiga and play it on a player.

## 2.5 Service menus ⬜

- **Content**: the server's generic menus (radio, plugins), which the CLI exposes without the remote having to know
  them; Deezer through its LMS plugin (depending on the plugin's state).
- **Criterion**: start a radio station, then a Deezer track, from the Amiga.

## 2.6 Graphical interface ⬜

- **Content**: a window with lists, transport buttons, volume, progress. To decide: ReAction (shipped with
  AmigaOS 3.2) or MUI.
- **Criterion**: every feature of 2.2 to 2.5 usable with the mouse.

## 2.7 Real machine ⬜

- **Criterion**: same features on the real machine, with good responsiveness.
