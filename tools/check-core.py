#!/usr/bin/env python3
"""Checks that the portable core of a program includes no system header: every file of src/<prog>/ but its
platform layers (amiga.c, posix.c) includes only the C library's headers, the program's own and the third-party
ones (third_party/*/). Everything system-related goes through platform.h: see CONTRIBUTING.md, Code rules.

Usage: check-core.py <prog>   -> exit code 1 if a file of the core includes another header.
"""
import glob
import os
import re
import sys

PLATFORM = ("amiga.c", "posix.c")
# The headers of the C library (ISO C99 and C11): nothing of POSIX, nor of AmigaOS
C_LIBRARY = {f"{h}.h" for h in ("assert", "complex", "ctype", "errno", "fenv", "float", "inttypes", "iso646",
                                "limits", "locale", "math", "setjmp", "signal", "stdalign", "stdarg", "stdatomic",
                                "stdbool", "stddef", "stdint", "stdio", "stdlib", "stdnoreturn", "string",
                                "tgmath", "threads", "time", "uchar", "wchar", "wctype")}
INCLUDE = re.compile(r'^\s*#\s*include\s*(<([^>]*)>|"([^"]*)")')


def problems(prog):
    own = {os.path.basename(p) for p in glob.glob(f"src/{prog}/*.h") + glob.glob("third_party/*/*.h")}
    found = []
    for path in sorted(glob.glob(f"src/{prog}/*.[ch]")):
        if os.path.basename(path) in PLATFORM:
            continue
        with open(path, encoding="latin-1") as f:
            for n, line in enumerate(f, 1):
                m = INCLUDE.match(line)
                if m and (m.group(2) not in C_LIBRARY if m.group(2) is not None else m.group(3) not in own):
                    found.append(f"{path}:{n}: {m.group(1)} is not a header of the C library, of the program or "
                                 "of third_party/")
    return found


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    found = problems(sys.argv[1])
    for p in found:
        print(f"error: {p}")
    if found:
        sys.exit(1)
    print(f"check-core: the portable core of {sys.argv[1]} includes only the C library's, its own and the "
          "third-party headers")
