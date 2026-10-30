#!/usr/bin/env python3
"""Writes the Workbench icon of a program: a classic AmigaOS tool icon (.info), readable by any AmigaOS 3.x.

The format (struct DiskObject of workbench/workbench.h, then its parts in that order): magic 0xE310, version 1, a
Gadget (its GadgetRender points to the image), the type (WBTOOL), no default tool, the ToolTypes, an unset
position, no drawer data, the stack size; then the Image (struct Image) and its bitplanes; then the ToolTypes
(their count + 1 times 4, then each string as a length followed by its bytes and a NUL). Pointers are only "set"
or "not set" in the file: any non-zero value means the part follows.

The image: a loudspeaker with three sound waves, in the four colours of the Workbench (0 grey, 1 black, 2 white,
3 blue), drawn here so that the icon can be made again from the repository.

Usage: make-icon.py <output.info> [stack size]
"""
import math
import struct
import sys

WIDTH, HEIGHT = 44, 22
TOOLTYPES = [
    "(SERVER=192.168.1.10)",
    "(NAME=Amiga)",
    "(UNIT=0)",
    "(LOG=RAM:amisqueeze.log)",
    "(QUIET)",
    "(WINDOW=CON:0/20/640/120/amisqueeze/CLOSE)",
    "DONOTWAIT",
]


def draw():
    """The image as rows of colour numbers (0 to 3)"""
    img = [[0] * WIDTH for _ in range(HEIGHT)]

    def put(x, y, c):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            img[y][x] = c

    cy = HEIGHT // 2
    # The speaker's box (blue, black outline), then its cone opening to the right
    for y in range(cy - 3, cy + 3):
        for x in range(4, 10):
            put(x, y, 3)
    for x in range(4, 10):
        put(x, cy - 4, 1)
        put(x, cy + 3, 1)
    for y in range(cy - 4, cy + 4):
        put(3, y, 1)
    for i, x in enumerate(range(10, 19)):
        top, bottom = cy - 4 - i, cy + 3 + i
        for y in range(top + 1, bottom):
            put(x, y, 3)
        put(x, top, 1)
        put(x, bottom, 1)
    for y in range(cy - 13, cy + 12):
        put(19, y, 1)
    for x in range(4, 10):                  # a white highlight on the box
        put(x, cy - 3, 2)
    # Three waves: arcs centred on the cone's mouth, black with a white edge
    for r in (5, 10, 15):
        for a in range(-55, 56):
            t = math.radians(a)
            x = 19 + r * math.cos(t)
            y = cy - 0.5 + r * math.sin(t)
            put(int(round(x)), int(round(y)), 1)
            put(int(round(x)) + 1, int(round(y)), 2)
    return img


def planes(img):
    """Bitplane 0 then bitplane 1, each row padded to a multiple of 16 pixels"""
    row_bytes = (WIDTH + 15) // 16 * 2
    out = b""
    for bit in (0, 1):
        for row in img:
            data = bytearray(row_bytes)
            for x, c in enumerate(row):
                if c >> bit & 1:
                    data[x // 8] |= 0x80 >> (x % 8)
            out += bytes(data)
    return out


def icon(stack):
    gadget = struct.pack(">IhhhhHHHIIIIIHI",
                         0,                 # NextGadget
                         0, 0,              # LeftEdge, TopEdge
                         WIDTH, HEIGHT + 1,  # Width, Height
                         0x0004,            # Flags: GFLG_GADGIMAGE, highlight by complement
                         0x0003,            # Activation: RELVERIFY, GADGIMMEDIATE
                         0x0001,            # GadgetType: BOOLGADGET
                         1,                 # GadgetRender: the image follows
                         0, 0, 0, 0,        # SelectRender, GadgetText, MutualExclude, SpecialInfo
                         0,                 # GadgetID
                         1)                 # UserData: WB_DISKREVISION 1
    disk_object = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIII",
        3, 0,                               # do_Type: WBTOOL, padding
        0,                                  # do_DefaultTool: none
        1,                                  # do_ToolTypes: they follow
        -0x80000000, -0x80000000,           # do_CurrentX/Y: NO_ICON_POSITION, placed by the Workbench
        0, 0,                               # do_DrawerData, do_ToolWindow
        stack)                              # do_StackSize
    image = struct.pack(">hhhhhIBBI", 0, 0, WIDTH, HEIGHT, 2, 1, 3, 0, 0) + planes(draw())
    tooltypes = struct.pack(">I", (len(TOOLTYPES) + 1) * 4)
    for t in TOOLTYPES:
        data = t.encode("ascii") + b"\0"
        tooltypes += struct.pack(">I", len(data)) + data
    return disk_object + image + tooltypes


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    stack = int(sys.argv[2]) if len(sys.argv) == 3 else 16384
    with open(sys.argv[1], "wb") as f:
        f.write(icon(stack))


if __name__ == "__main__":
    main()
