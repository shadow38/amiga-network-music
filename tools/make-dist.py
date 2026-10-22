#!/usr/bin/env python3
"""Builds the release archives of a program, out/<prog>-<version>.zip and out/<prog>-<version>.lha, and its Aminet
readme, out/<prog>-<version>.readme.

Both archives hold the same <prog>-<version>/ directory: the Amiga executable (out/<prog>, and out/<prog>.fpu if
built) with its .info icon, LICENSE, README.md, CHANGELOG.md and the user guide with its images (docs/images/ ->
images/). The files are copied first into a temporary directory, the executables with mode 755 and the rest 644,
so that both archives record the same modes whatever the file system the repository sits on. The version is the
one tools/check-version.py --release accepts (the same in both $VER: strings, Firmware=, the user guide, the
README and the CHANGELOG, not a -dev one), so the archives always match the executables.

The .lha is made by lha (LHa for UNIX, jca02266/lha; the reference image amigadev/crosstools has it), method
-lh5- and header level 1, what LhA 2.15 writes on the Amiga; it is then tested (lha t) and its list of files and
sizes compared with the zip's. The readme is dist/<prog>.readme with its @VERSION@ replaced, checked against
Aminet's rules (fields, Short: of 40 characters at most, lines of 78 at most, ASCII, LF).

Next to each archive, <archive>.sha256 holds its SHA-256 in the format of sha256sum ("<hex>  <name>", no path,
LF), checked with "sha256sum -c" when that tool is there; the script ends with the lines for the release notes.
Each run gives new archives (new dates), so the archives, their .sha256 and the notes must come from the same
run.

Paths inside the zip use forward slashes (the zip standard: Windows PowerShell 5.1's Compress-Archive writes
backslashes, which other tools misread).

Usage: make-dist.py [--dev] <prog>        (from the repository root, after the Amiga build)
         --dev: a development version is accepted (out/<prog>-<x.y>-dev.*), to try the archives; never published
       make-dist.py --check-readme <prog> (make test) checks the readme made from dist/<prog>.readme only
"""
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

READMES = "dist"
SHORT_MAX = 40          # Aminet: "This description must not be longer than 40 characters."
LINE_MAX = 78           # Aminet: "Lines should not be longer than 78 characters"
FIELDS = ("Short", "Uploader", "Author", "Type", "Version", "Replaces", "Architecture", "Requires", "Distribution",
          "Kurz")
REQUIRED = ("Short", "Uploader", "Type", "Architecture")
ARCHS = "m68k-amigaos|ppc-amigaos|ppc-morphos|ppc-powerup|ppc-warpup|i386-aros|i386-amithlon|generic"
ARCH = rf"(?:{ARCHS})(?: (?:>=|>|=|<=|<) \d+\.\d+\.\d+)?"


def version_of(prog, release):
    """The version, once tools/check-version.py has accepted it (with --release unless release is False)."""
    spec = importlib.util.spec_from_file_location("check_version", os.path.join(os.path.dirname(__file__),
                                                                             "check-version.py"))
    check_version = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(check_version)
    if prog != check_version.PROG:
        sys.exit(f"make-dist: no version check for {prog} (tools/check-version.py)")
    return check_version.check(release=release)


def aminet_readme(prog, version):
    """The text of the Aminet readme: dist/<prog>.readme with its @VERSION@ replaced."""
    with open(os.path.join(READMES, f"{prog}.readme"), "rb") as f:
        return f.read().decode("ascii", errors="replace").replace("@VERSION@", version)


def readme_problems(text, version):
    """What breaks Aminet's rules in the readme text (wiki.aminet.net, "The Readme file"), as a list."""
    problems = []
    if "�" in text or not text.isascii():
        problems.append("not ASCII")
    if "\r" in text:
        problems.append("CR in it: LF line endings only")
    if not text.endswith("\n"):
        problems.append("no LF at the end")
    lines = text.split("\n")
    for n, line in enumerate(lines, 1):
        if len(line) > LINE_MAX:
            problems.append(f"line {n}: {len(line)} characters, at most {LINE_MAX}")
    if "@VERSION@" in text:
        problems.append("@VERSION@ left")
    if "" not in lines[:-1]:
        problems.append("no empty line between the fields and the description")
        return problems
    head, body = lines[:lines.index("")], lines[lines.index("") + 1:]
    fields = {}
    for n, line in enumerate(head, 1):
        m = re.fullmatch(r"([A-Za-z]+): +(\S.*)", line)
        if not m or m.group(1) not in FIELDS:
            problems.append(f'line {n}: "{line}" is not one of the fields {", ".join(FIELDS)}')
        elif m.group(1) in fields:
            problems.append(f"line {n}: {m.group(1)}: given twice")
        else:
            fields[m.group(1)] = m.group(2).rstrip()
    if not head or not head[0].startswith("Short:"):
        problems.append("the first line is not Short:")
    for name in REQUIRED:
        if name not in fields:
            problems.append(f"no {name}: (required by Aminet)")
    if len(fields.get("Short", "")) > SHORT_MAX:
        problems.append(f"Short: {len(fields['Short'])} characters, at most {SHORT_MAX}")
    if fields.get("Version") != version:
        problems.append(f"Version: {fields.get('Version')}, the build is {version}")
    if "Type" in fields and not re.fullmatch(r"[a-z]+/[a-z]+", fields["Type"]):
        problems.append(f"Type: {fields['Type']} is not an Aminet directory (such as mus/play)")
    if "Architecture" in fields and not re.fullmatch(rf"{ARCH}(?:; {ARCH})*", fields["Architecture"]):
        problems.append(f"Architecture: {fields['Architecture']} is not of the form m68k-amigaos >= 3.0.0")
    if fields.get("Distribution", "Aminet") not in ("Aminet", "NoCD"):
        problems.append(f"Distribution: {fields['Distribution']}, Aminet or NoCD only")
    if not any(line.strip() for line in body):
        problems.append("no description after the fields")
    return problems


def write_readme(prog, version, path):
    text = aminet_readme(prog, version)
    problems = readme_problems(text, version)
    if problems:
        sys.exit("\n".join(f"make-dist: {READMES}/{prog}.readme: {p}" for p in problems))
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(text)


def write_sha256(path):
    """Writes <path>.sha256 in the format of sha256sum, checks it with sha256sum -c when it can; returns the hash."""
    with open(path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    folder, name = os.path.split(path)
    with open(f"{path}.sha256", "w", encoding="ascii", newline="\n") as f:
        f.write(f"{digest}  {name}\n")
    if shutil.which("sha256sum"):
        if subprocess.run(["sha256sum", "-c", f"{name}.sha256"], cwd=folder or ".").returncode != 0:
            sys.exit(f"make-dist: sha256sum -c refuses {path}.sha256")
    else:
        print(f"make-dist: sha256sum not found, {path}.sha256 not checked")
    return digest


def lha(*args, cwd=None):
    """Runs lha, exits with its output if it fails; returns its output."""
    run = subprocess.run(["lha", *args], cwd=cwd, capture_output=True, text=True, errors="replace")
    if run.returncode != 0:
        sys.exit(f"make-dist: lha {' '.join(args)} failed:\n{run.stdout}{run.stderr}")
    return run.stdout


def make_lha(out, stage, root, expected):
    """Builds out from stage/root, tests it, and checks that it lists the expected {name: size}."""
    lha("-a1o5q", os.path.abspath(out), root, cwd=stage)   # the directory: lha adds its entries (-lhd-) too
    lha("t", out)
    listed = {}
    for line in lha("-lq", out).splitlines():
        m = re.fullmatch(r"\S+\s+\S+\s+(\d+)\s+\S+\s+\w{3}\s+\d+\s+[\d:]+\s(.+)", line.rstrip())
        if not m:
            sys.exit(f'make-dist: lha -lq {out}: cannot read "{line}"')
        if not m.group(2).endswith("/"):
            listed[m.group(2)] = int(m.group(1))
    if listed != expected:
        sys.exit(f"make-dist: {out} lists {sorted(listed.items())}, the zip {sorted(expected.items())}")


def main():
    args = sys.argv[1:]
    mode = args.pop(0) if args and args[0] in ("--dev", "--check-readme") else None
    if len(args) != 1:
        sys.exit(__doc__)
    prog = args[0]
    version = version_of(prog, release=mode is None)
    if mode == "--check-readme":
        problems = readme_problems(aminet_readme(prog, version), version)
        if problems:
            sys.exit("\n".join(f"make-dist: {READMES}/{prog}.readme: {p}" for p in problems))
        print(f"make-dist: {READMES}/{prog}.readme follows Aminet's rules")
        return
    if not shutil.which("lha"):
        sys.exit("make-dist: lha not found: LHa for UNIX (https://github.com/jca02266/lha), which the image "
                 "amigadev/crosstools has, builds the .lha")
    root = f"{prog}-{version}"
    files = [(f"out/{prog}", prog), ("LICENSE", "LICENSE"), ("README.md", "README.md"),
             ("CHANGELOG.md", "CHANGELOG.md"), ("docs/user-guide.md", "user-guide.md")]
    executables = {prog}
    if os.path.isfile(f"out/{prog}.fpu"):       # the FPU build, when the program has one
        files.insert(1, (f"out/{prog}.fpu", f"{prog}.fpu"))
        executables.add(f"{prog}.fpu")
    for exe in (prog, f"{prog}.fpu"):           # their Workbench icons (tools/make-icon.py)
        if os.path.isfile(f"out/{exe}.info"):
            files.append((f"out/{exe}.info", f"{exe}.info"))
    if os.path.isdir("docs/images"):             # the user guide's images, at the same place relative to it
        files += [(f"docs/images/{n}", f"images/{n}") for n in sorted(os.listdir("docs/images"))]
    for src, _ in files:
        if not os.path.isfile(src):
            sys.exit(f"make-dist: {src} missing (build the Amiga version first)")
    out = f"out/{root}"
    for path in (f"{out}.zip.sha256", f"{out}.lha.sha256", f"{out}.lha"):   # never left beside what they do not
        if os.path.exists(path):                                            # match, should this run fail
            os.remove(path)
    with tempfile.TemporaryDirectory() as stage:
        for src, name in files:
            dest = os.path.join(stage, root, name)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copy2(src, dest)
            os.chmod(dest, 0o755 if name in executables else 0o644)
        with zipfile.ZipFile(f"{out}.zip", "w", zipfile.ZIP_DEFLATED) as z:
            for _, name in files:
                z.write(os.path.join(stage, root, name), f"{root}/{name}")
        with zipfile.ZipFile(f"{out}.zip") as z:
            sizes = {info.filename: info.file_size for info in z.infolist()}
        make_lha(f"{out}.lha", stage, root, sizes)
    write_readme(prog, version, f"{out}.readme")
    print(f"{out}.zip and {out}.lha:")
    for name, size in sizes.items():
        print(f"  {name} ({size} bytes)")
    digests = [(f"{root}.{ext}", write_sha256(f"{out}.{ext}")) for ext in ("zip", "lha")]
    print(f"{out}.readme, {out}.zip.sha256 and {out}.lha.sha256 written; for the release notes:")
    for name, digest in digests:
        print(f"SHA-256 of `{name}`: `{digest}` (also in `{name}.sha256`).")


if __name__ == "__main__":
    main()
