#!/usr/bin/env python3
"""Builds the release archive of a program: out/<prog>-<version>.zip, holding a <prog>-<version>/ directory with
the Amiga executable (out/<prog>, and out/<prog>.fpu if built) with its .info icon, LICENSE, README.md,
CHANGELOG.md and the user guide with its images (docs/images/ -> images/). The version is the one
tools/check-version.py --release accepts (the same in both $VER: strings, Firmware=, the user guide, the README and
the CHANGELOG, not a -dev one), so the archive always matches the executables.

Paths inside the zip use forward slashes (the zip standard: Windows PowerShell 5.1's Compress-Archive writes
backslashes, which other tools misread). An .lha archive for Aminet is left for later (plan step 3.2).

Usage: make-dist.py <prog>   (from the repository root, after the Amiga build)
"""
import importlib.util
import os
import sys
import zipfile


def version_of(prog):
    """The version, once tools/check-version.py --release has accepted it (it exits otherwise)."""
    spec = importlib.util.spec_from_file_location("check_version", os.path.join(os.path.dirname(__file__),
                                                                             "check-version.py"))
    check_version = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(check_version)
    if prog != check_version.PROG:
        sys.exit(f"make-dist: no version check for {prog} (tools/check-version.py)")
    return check_version.check(release=True)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    prog = sys.argv[1]
    version = version_of(prog)
    root = f"{prog}-{version}"
    files = [(f"out/{prog}", prog), ("LICENSE", "LICENSE"), ("README.md", "README.md"),
             ("CHANGELOG.md", "CHANGELOG.md"), ("docs/user-guide.md", "user-guide.md")]
    if os.path.isfile(f"out/{prog}.fpu"):       # the FPU build, when the program has one
        files.insert(1, (f"out/{prog}.fpu", f"{prog}.fpu"))
    for exe in (prog, f"{prog}.fpu"):           # their Workbench icons (tools/make-icon.py)
        if os.path.isfile(f"out/{exe}.info"):
            files.append((f"out/{exe}.info", f"{exe}.info"))
    if os.path.isdir("docs/images"):             # the user guide's images, at the same place relative to it
        files += [(f"docs/images/{n}", f"images/{n}") for n in sorted(os.listdir("docs/images"))]
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
