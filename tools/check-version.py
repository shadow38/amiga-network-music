#!/usr/bin/env python3
"""Checks that amisqueeze's version number agrees everywhere it is written (rules.md V4 to V7, V9):

- the two $VER: strings (amiga.c: amisqueeze and amisqueeze.fpu) have the same number and date, in the form
  `static char <name>[] = "$VER: <prog> <x.y or x.y-dev> (dd.mm.yyyy)";`, not const (V6, V7), and fit as the
  commodity's title (CBD_TITLELEN, V9);
- Firmware= of the HELO capabilities (slimproto.c) has the same number (V6);
- the user guide's state line reads "Version x.y in development. The latest release, <latest>," for x.y-dev, or
  "Version x.y," for a release (V6);
- CHANGELOG.md starts with "## [Unreleased]"; its newest numbered section is the latest release, the one the guide
  names; an x.y-dev version comes after it; a release version is that section, with the $VER: date (V4, V5).

With --release (make dist, through make-dist.py), also refuses an x.y-dev version and a non-empty "Unreleased"
part (V4, V5).

Usage: check-version.py [--release]   (from the repository root) -> exit code 1, every problem listed.
"""
import datetime
import glob
import re
import sys

PROG = "amisqueeze"
NAMES = ["amisqueeze", "amisqueeze.fpu"]
SOURCES = "src/amisqueeze/*.c"
SLIMPROTO = "src/amisqueeze/slimproto.c"
GUIDE = "docs/user-guide.md"
CHANGELOG = "CHANGELOG.md"
CBD_TITLELEN = 40       # libraries/commodities.h: the title's buffer, NUL included

VER_LINE = re.compile(r'static char \w+\[\] = "\$VER: (\S+) (\d+\.\d+(?:-dev)?) \((\d\d)\.(\d\d)\.(\d{4})\)";')
NUMBER = r"\d+\.\d+"

problems = []


def fail(text, rule):
    problems.append(f"check-version: {text} - rules.md {rule}")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().split("\n")


def find(path, pattern):
    """[(line number, match)] of the lines of path matching pattern (re.search)."""
    return [(n, m) for n, line in enumerate(read(path), 1) if (m := re.search(pattern, line))]


def number(text):
    return tuple(int(p) for p in text.split("."))


def ver_strings():
    """[(where, name, version, date)] of the $VER: strings of the sources, checked for their form (V7)."""
    found = []
    for path in sorted(glob.glob(SOURCES)):
        lines = read(path)
        for n, line in enumerate(lines, 1):
            if not re.search(r'"\$VER: [^"]', line):    # a string that starts with "$VER: ", not a comment's "$VER: "
                continue
            where = f"{path}:{n}"
            m = VER_LINE.fullmatch(line)
            before = lines[n - 2].strip() if n > 1 else ""
            if not m or not (before == "" or before.startswith("#") or before.endswith((";", "}", "*/"))):
                fail(f'{where}: not of the form static char <name>[] = "$VER: <prog> <x.y or x.y-dev> (dd.mm.yyyy)"; '
                     f"on a line of its own (never const: in the code hunk, Version misreads it)", "V7")
                continue
            try:
                date = datetime.date(int(m.group(5)), int(m.group(4)), int(m.group(3)))
            except ValueError:
                fail(f"{where}: {m.group(3)}.{m.group(4)}.{m.group(5)} is not a date", "V6")
                continue
            found.append((where, m.group(1), m.group(2), date))
    return found


def changelog():
    """(where of "Unreleased" or None, its text is empty, [(where, version, date or None)] of the numbered ones)."""
    heads = find(CHANGELOG, r"^## (.*)$")
    if not heads or heads[0][1].group(1) != "[Unreleased]":
        fail(f'{CHANGELOG}: the first section is not "## [Unreleased]"', "V4")
        unreleased, empty = None, True
    else:
        unreleased = f"{CHANGELOG}:{heads[0][0]}"
        end = heads[1][0] if len(heads) > 1 else None
        body = read(CHANGELOG)[heads[0][0]:end - 1 if end else None]
        empty = all(not line.strip() for line in body)
    releases = []
    for n, m in heads[1 if unreleased else 0:]:
        r = re.fullmatch(rf"\[({NUMBER})\] - (\d{{4}}-\d\d-\d\d)", m.group(1))
        try:
            date = datetime.date.fromisoformat(r.group(2)) if r else None
        except ValueError:
            date = None
        if not date:
            fail(f'{CHANGELOG}:{n}: "{m.group(0)}" is not of the form "## [x.y] - yyyy-mm-dd"', "V4")
            continue
        releases.append((f"{CHANGELOG}:{n}", r.group(1), date))
    return unreleased, empty, releases


def check(release=False):
    """Checks everything; returns the version, or exits listing the problems."""
    vers = ver_strings()
    names = [name for _, name, _, _ in vers]
    if sorted(names) != sorted(NAMES):
        fail(f"$VER: strings found for {names or 'nothing'} in {SOURCES}, expected one each for {NAMES}", "V6")
    if not vers:
        sys.exit("\n".join(problems))
    where, _, version, date = vers[-1]
    for w, name, v, d in vers:
        if (v, d) != (version, date):
            fail(f"$VER: says {v} ({d:%d.%m.%Y}) ({w}), and {version} ({date:%d.%m.%Y}) ({where})", "V6")
        title = f"{name} {v} ({d:%d.%m.%Y})"
        if len(title) >= CBD_TITLELEN:
            fail(f'{w}: "{title}" is the commodity\'s title, {len(title)} characters, at most {CBD_TITLELEN - 1}',
                 "V9")
    dev = version.endswith("-dev")
    base = version[:-len("-dev")] if dev else version

    firmware = find(SLIMPROTO, r"Firmware=([^,\"]*)")
    if len(firmware) != 1:
        fail(f"{len(firmware)} Firmware= in {SLIMPROTO}, expected one", "V6")
    for n, m in firmware:
        if m.group(1) != version:
            fail(f"Firmware= says {m.group(1)} ({SLIMPROTO}:{n}), $VER: says {version} ({where})", "V6")

    unreleased, empty, releases = changelog()
    latest = releases[0] if releases else None
    if not latest:
        fail(f"{CHANGELOG}: no release section", "V4")
    elif dev and number(base) <= number(latest[1]):
        fail(f"$VER: says {version} ({where}), but {latest[1]} is already released ({latest[0]})", "V5")
    elif not dev:
        if latest[1] != version:
            fail(f"$VER: says {version} ({where}), the newest section of {CHANGELOG} is {latest[1]} ({latest[0]})",
                 "V4")
        elif latest[2] != date:
            fail(f"$VER: is dated {date:%d.%m.%Y} ({where}), the release {latest[2]} ({latest[0]})", "V5")
    if release:
        if dev:
            fail(f"--release: {version} is a development version ({where})", "V5")
        if unreleased and not empty:
            fail(f'--release: the "Unreleased" part is not empty ({unreleased})', "V4")

    state = find(GUIDE, r"^Version \d")
    if len(state) != 1:
        fail(f'{len(state)} lines starting with "Version <number>" in {GUIDE}, expected one', "V6")
    for n, m in state:
        line = m.string
        if dev:
            g = re.match(rf"Version {re.escape(base)} in development\. The latest release, ({NUMBER}),", line)
            if not g:
                fail(f'{GUIDE}:{n} does not start with "Version {base} in development. The latest release, <x.y>,"'
                     f", $VER: says {version} ({where})", "V6")
            elif latest and g.group(1) != latest[1]:
                fail(f"{GUIDE}:{n} names {g.group(1)} as the latest release, {CHANGELOG} {latest[1]} ({latest[0]})",
                     "V6")
        elif not line.startswith(f"Version {version},") or "in development" in line:
            fail(f'{GUIDE}:{n} does not start with "Version {version}," (a release), $VER: says {version} ({where})',
                 "V6")

    if problems:
        sys.exit("\n".join(problems))
    return version


def main():
    args = sys.argv[1:]
    if args not in ([], ["--release"]):
        sys.exit(__doc__)
    version = check(release=bool(args))
    print(f"check-version: {PROG} {version}, the same everywhere")


if __name__ == "__main__":
    main()
