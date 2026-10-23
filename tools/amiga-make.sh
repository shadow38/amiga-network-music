#!/bin/sh
# Runs make for this repository with the m68k-amigaos-gcc toolchain, looked up in this order:
#   1. $AMIGA_MAKE, if set: local script run instead (receives the make arguments);
#   2. m68k-amigaos-gcc in the PATH: plain make;
#   3. ../amiga-make.local.sh (next to the repository, outside Git), if present: same role as $AMIGA_MAKE;
#   4. Docker: image $AMIGA_IMAGE (default: the reference image, pinned by its digest so that every build uses
#      the same toolchain; the tag alone moves when the image is rebuilt).
# Usage: sh tools/amiga-make.sh [all|clean|sdk|host|test|dist|<prog>]
set -e

# m68k-amigaos-gcc (AmigaDev 04c51c0) 16.2.1b; to change it: CONTRIBUTING.md, Requirements
REFERENCE_IMAGE=amigadev/crosstools:m68k-amigaos-gcc10@sha256:96ca6c4c63af047ff4e717f2b1782dad628cdbe5bed3160415282ace83efc27d

cd "$(dirname "$0")/.."

if [ -n "$AMIGA_MAKE" ]; then
    exec sh "$AMIGA_MAKE" "$@"
fi

if command -v m68k-amigaos-gcc >/dev/null 2>&1; then
    exec make "$@"
fi

if [ -f ../amiga-make.local.sh ]; then
    exec sh ../amiga-make.local.sh "$@"
fi

if command -v docker >/dev/null 2>&1; then
    exec docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/work -w /work \
        "${AMIGA_IMAGE:-$REFERENCE_IMAGE}" make "$@"
fi

echo "amiga-make.sh: m68k-amigaos-gcc not found (no AMIGA_MAKE, PATH, ../amiga-make.local.sh or Docker)" >&2
exit 1
