#!/usr/bin/env python3
"""Checks the linker map of an Amiga program (ld -Map) for libnix's stack swap: __stkinit linked, which gives the
program the stack __stack asks for when the Shell's is smaller (4 KB by default), and __stack defined by the
program itself, not left to a default.

Without __stkinit, the program builds and starts, then overflows the Shell's stack in the decoders without a
warning (AmigaOS does not detect it): see CONTRIBUTING.md, Stack and memory.

Usage: check-stack.py <map> <object file that defines __stack>   -> exit code 1 if either is missing.
"""
import re
import sys


def problems(path, obj):
    with open(path, encoding="latin-1") as f:
        text = f.read()
    found = []
    if not re.search(r"^\s+0x[0-9a-f]+\s+__stkinit$", text, re.M):
        found.append("libnix's __stkinit is not linked (-Wl,-u,___stkinit in the Makefile)")
    if not re.search(r"^ \.(?:data|bss)\.__stack\s+0x[0-9a-f]+\s+0x[0-9a-f]+ " + re.escape(obj) + "$", text, re.M):
        found.append(f"__stack is not defined in {obj}")
    return found


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    found = problems(sys.argv[1], sys.argv[2])
    for p in found:
        print(f"{sys.argv[1]}: error: {p}")
    sys.exit(1 if found else 0)
