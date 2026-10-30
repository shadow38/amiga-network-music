#!/usr/bin/env python3
"""Checks the size and complexity of the C code against the limits of CONTRIBUTING.md, Code rules, measured by
lizard: per function a complexity of 15 at most (lizard's -m count: a whole switch counts once), 60 lines of code
and 6 parameters; per file 800 lines of code, the tests' files excepted. Lines of code are lizard's NLOC (neither
comments nor blank lines). third_party/ and the generated bench_*.c are left out.

The functions over a limit today are listed in OVER, with the values they reach: a listed function may not grow
past them, its other values keep the common limits, and it leaves the list once it fits them all. Raising a listed
value needs the maintainer's agreement, with the reason in the commit.

The code's comments do not point to the project's history: the check fails on the name of a step of the plan in
the files of TEXT (CONTRIBUTING.md, Code rules).

Usage: check-quality.py   -> exit code 1 if a function or a file is over its limit, if OVER is out of date, or if
                             a step of the plan is named in the code.
Needs lizard at the version of tools/requirements.txt: pip install --require-hashes -r tools/requirements.txt
"""
import fnmatch
import glob
import os
import re
import sys

LIMITS = {"complexity": 15, "lines": 60, "parameters": 6}
FILE_LINES = 800
SOURCES = ("src/*/*.[ch]", "tests/*.[ch]", "tools/amiga/*.[ch]")
GENERATED = "bench_*.c"   # made by tools/bin2c.py from the test files
NO_FILE_LIMIT = "tests/*"

OVER = {
    ("src/amisqueeze/amiga.c", "plat_wait"): {"complexity": 26},
    ("src/amisqueeze/amiga.c", "main"): {"complexity": 35, "lines": 105},
    ("src/amisqueeze/decoder.c", "wav_header"): {"complexity": 19},
    ("src/amisqueeze/flac.c", "frame_streaminfo"): {"complexity": 29},
    ("src/amisqueeze/flac.c", "read_header"): {"complexity": 29, "lines": 95},
    ("src/amisqueeze/mp3.c", "mp3_lame_header"): {"complexity": 21},
    ("src/amisqueeze/mp3.c", "mp3_run"): {"complexity": 30, "lines": 78},
    ("src/amisqueeze/player.c", "tooltypes_line"): {"complexity": 16},
    ("src/amisqueeze/player.c", "run"): {"complexity": 31, "lines": 66},
    ("src/amisqueeze/player.c", "player_main"): {"complexity": 33, "lines": 96},
    ("src/amisqueeze/posix.c", "parse_args"): {"complexity": 33, "lines": 64},
    ("src/amisqueeze/slimproto.c", "slim_discover"): {"complexity": 18},
    ("src/amisqueeze/stream.c", "stream_start"): {"parameters": 9},
    ("src/amisqueeze/stream.c", "feed_audio"): {"complexity": 18},
    ("tests/test_core.c", "test_stream_sequence"): {"complexity": 16, "lines": 63},
    ("tests/test_core.c", "test_icy_cuts"): {"complexity": 18},
    ("tests/test_core.c", "wav"): {"parameters": 8},
    ("tests/test_core.c", "test_lame_header"): {"complexity": 20},
    ("tests/test_core.c", "lame_frame"): {"parameters": 8},
    ("tests/test_core.c", "main"): {"lines": 99},
}

# All the code, the C and what builds and tests it; the documents (docs/, *.md) and the review tooling (.claude/)
# are left out
TEXT = ("src/*/*.[ch]", "tests/*.[chs]", "tests/*.py", "tests/data/*.txt", "tools/*.py", "tools/*.sh",
        "tools/*.txt", "tools/*/*.py", "tools/*/*.c", "Makefile", ".github/workflows/*.yml", "winuae/*.uae")
# A step's name (the phase, a hyphen, its number), the words for a step of the plan, or an older name of a step
# (two numbers and a dot); written so that this file does not match it
STEP = re.compile(r"\b(tools|player|remote|publish)-\d+[a-z]?|\bplan\s+steps?\b|\bsteps?\s+\d+\.\d+\b", re.I)

INSTALL = "pip install --require-hashes -r tools/requirements.txt"


def load_lizard():
    """lizard, at the version pinned in tools/requirements.txt: another version may count differently."""
    try:
        import lizard
        from lizard_ext.version import version
    except ImportError:
        sys.exit(f"error: lizard is missing: {INSTALL} (CONTRIBUTING.md, Requirements)")
    with open("tools/requirements.txt", encoding="utf-8") as f:
        pinned = re.search(r"^lizard==(\S+)", f.read(), re.M).group(1)
    if version != pinned:
        sys.exit(f"error: lizard {version} installed, tools/requirements.txt pins {pinned}: {INSTALL}")
    return lizard


def sources():
    paths = sorted({p.replace(os.sep, "/") for pattern in SOURCES for p in glob.glob(pattern)})
    return [p for p in paths if not fnmatch.fnmatch(os.path.basename(p), GENERATED)]


def measure(lizard, path):
    """(lines of code of the file, {function name: (line, {value name: value})})"""
    with open(path, encoding="latin-1") as f:
        info = lizard.FileAnalyzer(lizard.get_extensions(["modified"])).analyze_source_code(path, f.read())
    functions = {}
    for fn in info.function_list:
        if fn.name in functions:
            sys.exit(f"error: {path}: two functions named {fn.name}, which OVER cannot tell apart")
        functions[fn.name] = (fn.start_line, {"complexity": fn.cyclomatic_complexity, "lines": fn.nloc,
                                              "parameters": fn.parameter_count})
    return info.nloc, functions


def problems(lizard):
    found, notes, seen, count = [], [], set(), 0
    for path in sources():
        file_lines, functions = measure(lizard, path)
        if file_lines > FILE_LINES and not fnmatch.fnmatch(path, NO_FILE_LIMIT):
            found.append(f"{path}: {file_lines} lines of code, over {FILE_LINES}")
        for name, (line, values) in functions.items():
            count += 1
            listed = OVER.get((path, name), {})
            if listed:
                seen.add((path, name))
                if all(values[v] <= LIMITS[v] for v in LIMITS):
                    found.append(f"{path}:{line}: {name} fits the limits now: take it out of OVER in "
                                 "tools/check-quality.py")
                    continue
            for v, limit in LIMITS.items():
                allowed = listed.get(v, limit)
                if values[v] > allowed:
                    where = f"its value in OVER, {allowed}" if v in listed else f"the limit, {limit}"
                    found.append(f"{path}:{line}: {name}: {v} {values[v]}, over {where}")
                elif v in listed and values[v] < allowed:
                    notes.append(f"{path}:{line}: {name}: {v} {values[v]}, listed at {allowed} in OVER "
                                 "(can be lowered)")
    for path, name in sorted(set(OVER) - seen):
        found.append(f"{path}: {name}, listed in OVER, not found: take it out of tools/check-quality.py")
    return found, notes, count


def steps_named():
    found = []
    for path in sorted({p.replace(os.sep, "/") for pattern in TEXT for p in glob.glob(pattern)}):
        with open(path, encoding="latin-1") as f:
            for n, line in enumerate(f, 1):
                m = STEP.search(line)
                if m:
                    found.append(f"{path}:{n}: \"{m.group(0)}\" names a step of the plan: say the fact itself "
                                 "(CONTRIBUTING.md, Code rules)")
    return found


if __name__ == "__main__":
    if len(sys.argv) != 1:
        sys.exit(__doc__)
    found, notes, count = problems(load_lizard())
    found += steps_named()
    for n in notes:
        print(f"note: {n}")
    for p in found:
        print(f"error: {p}")
    if found:
        sys.exit(1)
    print(f"check-quality: {count} functions measured, all within the limits but the {len(OVER)} listed in OVER, "
          "none of them grown; no step of the plan named in the code")
