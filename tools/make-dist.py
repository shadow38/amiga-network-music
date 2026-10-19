#!/usr/bin/env python3
"""Builds the release archive of a program: out/<prog>-<version>.zip, holding a <prog>-<version>/ directory with
the Amiga executable (out/<prog>), LICENSE, README.md and the user guide. The version is read from the $VER:
string of src/<prog>/amiga.c, so the archive always matches the executable.

Paths inside the zip use forward slashes (the zip standard: Windows PowerShell 5.1's Compress-Archive writes
backslashes, which other tools misread). An .lha archive for Aminet is left for later (plan step 3.2).

Usage: make-dist.py <prog>   (from the repository root, after the Amiga build)
"""
import os
import re
import sys
import zipfile


def version_of(prog):
    with open(f"src/{prog}/amiga.c", encoding="latin-1") as f:
        m = re.search(r"\$VER: " + re.escape(prog) + r" (\S+) \(", f.read())
    if not m:
        sys.exit(f"make-dist: no $VER: string for {prog} in src/{prog}/amiga.c")
    return m.group(1)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    prog = sys.argv[1]
    version = version_of(prog)
    root = f"{prog}-{version}"
    files = [(f"out/{prog}", prog), ("LICENSE", "LICENSE"), ("README.md", "README.md"),
             ("docs/user-guide.md", "user-guide.md")]
    for src, _ in files:
        if not os.path.isfile(src):
            sys.exit(f"make-dist: {src} missing (build the Amiga version first)")
    out = f"out/{root}.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, name in files:
            z.write(src, f"{root}/{name}")
    print(f"{out}:")
    with zipfile.ZipFile(out) as z:
        for info in z.infolist():
            print(f"  {info.filename} ({info.file_size} bytes)")


if __name__ == "__main__":
    main()
