#!/usr/bin/env python3
"""Builds the release archive of a program: out/<prog>-<version>.zip, holding a <prog>-<version>/ directory with
the Amiga executable (out/<prog>, and out/<prog>.fpu if built) with its .info icon, LICENSE, README.md,
CHANGELOG.md and the user guide with its images (docs/images/ -> images/). The version is the one
tools/check-version.py --release accepts (the same in both $VER: strings, Firmware=, the user guide, the README and
the CHANGELOG, not a -dev one), so the archive always matches the executables.

Next to the zip, out/<prog>-<version>.zip.sha256 holds its SHA-256 in the format of sha256sum ("<hex>  <zip name>",
no path, LF), checked with "sha256sum -c" when that tool is there; the script ends with the line for the release
notes. Each run gives a new zip (new dates), so the zip, its .sha256 and the notes must come from the same run.

Paths inside the zip use forward slashes (the zip standard: Windows PowerShell 5.1's Compress-Archive writes
backslashes, which other tools misread). An .lha archive for Aminet is left for later (plan step 3.2).

Usage: make-dist.py <prog>   (from the repository root, after the Amiga build)
"""
import hashlib
import importlib.util
import os
import shutil
import subprocess
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


def write_sha256(zip_path):
    """Writes <zip>.sha256 in the format of sha256sum, checks it with sha256sum -c when it can; returns the hash."""
    with open(zip_path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    folder, name = os.path.split(zip_path)
    with open(f"{zip_path}.sha256", "w", encoding="ascii", newline="\n") as f:
        f.write(f"{digest}  {name}\n")
    if shutil.which("sha256sum"):
        if subprocess.run(["sha256sum", "-c", f"{name}.sha256"], cwd=folder or ".").returncode != 0:
            sys.exit(f"make-dist: sha256sum -c refuses {zip_path}.sha256")
    else:
        print(f"make-dist: sha256sum not found, {zip_path}.sha256 not checked")
    return digest


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
    if os.path.exists(f"{out}.sha256"):         # never left beside a zip it does not match, should this run fail
        os.remove(f"{out}.sha256")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for src, name in files:
            z.write(src, f"{root}/{name}")
    print(f"{out}:")
    with zipfile.ZipFile(out) as z:
        for info in z.infolist():
            print(f"  {info.filename} ({info.file_size} bytes)")
    digest = write_sha256(out)                  # after the with: the zip is complete only once closed
    print(f"{out}.sha256 written; for the release notes:")
    print(f"SHA-256 of `{root}.zip`: `{digest}` (also in `{root}.zip.sha256`).")


if __name__ == "__main__":
    main()
