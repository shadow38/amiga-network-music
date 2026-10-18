#!/bin/sh
# Runs make for this repository with the m68k-amigaos-gcc toolchain, looked up in this order:
#   1. $AMIGA_MAKE, if set: local script run instead (receives the make arguments);
#   2. m68k-amigaos-gcc in the PATH: plain make;
#   3. ../amiga-make.local.sh (next to the repository, outside Git), if present: same role as $AMIGA_MAKE;
#   4. Docker: image $AMIGA_IMAGE (default amigadev/crosstools:m68k-amigaos-gcc10).
# Usage: sh tools/amiga-make.sh [all|clean|sdk|<prog>]
set -e

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
        "${AMIGA_IMAGE:-amigadev/crosstools:m68k-amigaos-gcc10}" make "$@"
fi

echo "amiga-make.sh: m68k-amigaos-gcc not found (no AMIGA_MAKE, PATH, ../amiga-make.local.sh or Docker)" >&2
exit 1
