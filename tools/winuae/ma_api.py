"""Client of Music Assistant's JSON API (docs/development/winuae-scenarios.md, "Music Assistant").

One command per HTTP request: POST /api with {"command": ..., "args": {...}} and a long-lived token
(Authorization: Bearer). MaApi("127.0.0.1", 8095, token).call("players/all") returns the decoded answer. The
commands and their arguments are listed by the server itself, at http://<server>:8095/api-docs.
"""
import json
import sys
import time
import urllib.error
import urllib.request


class MaError(Exception):
    pass


class MaApi:
    def __init__(self, host="127.0.0.1", port=8095, token="", verbose=False):
        self.url = f"http://{host}:{port}/api"
        self.token = token
        self.verbose = verbose
        # A test server on this host or the local network: never through a proxy
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            self.call("info")
        except MaError as e:
            raise MaError(f"no Music Assistant API on {host}:{port} ({e})") from e

    def call(self, command, **args):
        """Sends one command; returns its answer, decoded (None for a command that returns nothing)"""
        if self.verbose:
            print(f"  ma> {command} {json.dumps(args)}", file=sys.stderr)
        body = json.dumps({"message_id": "1", "command": command, "args": args}).encode()
        request = urllib.request.Request(self.url, data=body, headers={
            "Content-Type": "application/json", "Authorization": "Bearer " + self.token})
        try:
            with self.opener.open(request, timeout=30) as r:
                text = r.read()
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace").strip()[:200]
            raise MaError(f"{command}: HTTP {e.code} {detail}") from e
        except OSError as e:
            raise MaError(f"{command}: {e}") from e
        answer = json.loads(text) if text.strip() else None
        if self.verbose:
            print(f"  ma< {json.dumps(answer)[:300]}", file=sys.stderr)
        return answer

    def player_id(self, name, connected=False):
        """The id (MAC address) of the player named `name`, None if the server does not know it (or if it is not
        available, with `connected`)"""
        for p in self.call("players/all") or []:
            if (p.get("name") or p.get("display_name")) == name and (not connected or p.get("available")):
                return p.get("player_id")
        return None

    def find_track(self, file_name):
        """(track URI, length in seconds) of the library track whose file is `file_name`, or None. Found through a
        search on the name without its extension, so that no path on the server is needed."""
        stem = file_name.rsplit(".", 1)[0]
        for t in self.call("music/tracks/library_items", search=stem, limit=100) or []:
            for m in t.get("provider_mappings", []):
                path = str(m.get("item_id", "")).replace("\\", "/")
                if path == file_name or path.endswith("/" + file_name):
                    return t["uri"], float(t.get("duration") or 0)
        return None

    def queue(self, player):
        """The player's queue (its id is the player's): state, current_index, elapsed_time..."""
        q = self.call("player_queues/get", queue_id=player)
        if not q:
            raise MaError(f"no queue for player {player}")
        return q

    def elapsed(self, queue):
        """The elapsed time of the current track: the server's value moved on by the time since it was taken,
        while playing"""
        e = float(queue.get("elapsed_time") or 0)
        if queue.get("state") == "playing" and queue.get("elapsed_time_last_updated"):
            e += time.time() - float(queue["elapsed_time_last_updated"])
        return e

    def reset(self, name):
        """Stops the player and clears its queue"""
        player = self.player_id(name)
        if player:
            self.call("player_queues/stop", queue_id=player)
            self.call("player_queues/clear", queue_id=player)

    def close(self):
        pass
