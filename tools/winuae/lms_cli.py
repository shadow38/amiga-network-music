"""Client of LMS's command line interface (TCP 9090, docs/development/testing.md, "The server side").

One command per line; the answer repeats the command with the values filled in, every term URL-encoded and
separated by spaces. LmsCli(host, port).query("players", "0", "50") returns those terms decoded.
"""
import socket
import sys
from urllib.parse import quote, unquote


class LmsError(Exception):
    pass


class LmsCli:
    def __init__(self, host="127.0.0.1", port=9090, verbose=False):
        self.verbose = verbose
        try:
            self.sock = socket.create_connection((host, port), timeout=15)
        except OSError as e:
            raise LmsError(f"no LMS CLI on {host}:{port} ({e})") from e
        self.file = self.sock.makefile("rb")

    def query(self, *terms):
        """Sends one command; returns the terms of the answer, decoded"""
        line = " ".join(quote(str(t), safe="") for t in terms)
        if self.verbose:
            print(f"  lms> {' '.join(str(t) for t in terms)}", file=sys.stderr)
        try:
            self.sock.sendall(line.encode("utf-8") + b"\n")
            answer = self.file.readline()
        except OSError as e:
            raise LmsError(f"LMS CLI: {e}") from e
        if not answer:
            raise LmsError("LMS CLI: connection closed")
        terms = [unquote(t) for t in answer.decode("utf-8").split()]
        if self.verbose:
            print(f"  lms< {' '.join(terms)}", file=sys.stderr)
        return terms

    def items(self, terms, first_key):
        """The records of a list answer ("players", "titles"): one dict per record, a record starting at each
        `first_key`"""
        records = []
        for term in terms:
            key, sep, value = term.partition(":")
            if not sep:
                continue
            if key == first_key:
                records.append({})
            if records:
                records[-1][key] = value
        return records

    def player_id(self, name, connected=False):
        """The id (MAC address) of the player named `name`, None if LMS does not know it (or if it is not
        connected, with `connected`)"""
        for p in self.items(self.query("players", 0, 100), "playerindex"):
            if p.get("name") == name and (not connected or p.get("connected") == "1"):
                return p.get("playerid")
        return None

    def player(self, player_id, *terms):
        """A player command; returns the last term of the answer (the value of a "?" query)"""
        return self.query(player_id, *terms)[-1]

    def find_track(self, file_name):
        """(track id, length in seconds) of the track whose file is `file_name`, or None. Found through a search
        on the name without its extension, so that no path on the server is needed."""
        stem = file_name.rsplit(".", 1)[0]
        for t in self.items(self.query("titles", 0, 100, f"search:{stem}", "tags:du"), "id"):
            if unquote(t.get("url", "")).endswith("/" + file_name):
                return int(t["id"]), float(t.get("duration", 0))
        return None

    def close(self):
        self.sock.close()
