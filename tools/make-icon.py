#!/usr/bin/env python3
"""Writes the Workbench icon of a program: a classic AmigaOS tool icon (.info), readable by any AmigaOS 3.x; or,
with --drawer, the icon of the release archives' directory, without which the Workbench does not show it.

The format (struct DiskObject of workbench/workbench.h, then its parts in that order): magic 0xE310, version 1, a
Gadget (its GadgetRender points to the image), the type (WBTOOL or WBDRAWER), no default tool, the ToolTypes (a
tool's), an unset position, the drawer data (a drawer's), the stack size; then a drawer's DrawerData (struct
NewWindow, its window, then the window's scroll position); then the Image (struct Image) and its bitplanes; then
a tool's ToolTypes (their count + 1 times 4, then each string as a length followed by its bytes and a NUL), or a
drawer's DrawerData of AmigaOS 2.0 and later (dd_Flags, dd_ViewModes), as the system's own drawer icons have it.
Pointers are only "set" or "not set" in the file: any non-zero value means the part follows.

The images, in the four colours of the Workbench (0 grey, 1 black, 2 white, 3 blue), drawn here so that the icons
can be made again from the repository: a loudspeaker with three sound waves; a drawer.

Usage: make-icon.py <output.info> [stack size]
       make-icon.py --drawer <output.info>
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


def draw_drawer():
    """The drawer's image as rows of colour numbers (0 to 3): a cabinet seen from above and the front, its drawer
    with a handle"""
    img = [[0] * WIDTH for _ in range(HEIGHT)]

    def put(x, y, c):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            img[y][x] = c

    def box(x0, y0, x1, y1, fill, light, dark):
        """A filled rectangle with a light top-left edge and a dark bottom-right one"""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, fill)
        for x in range(x0, x1 + 1):
            put(x, y0, light)
            put(x, y1, dark)
        for y in range(y0, y1 + 1):
            put(x0, y, light)
            put(x1, y, dark)

    # The top, in perspective: a parallelogram from (8..39, 1) to (4..35, 5)
    for i, y in enumerate(range(1, 6)):
        for x in range(8 - i, 40 - i):
            put(x, y, 3)
        put(8 - i, y, 1)
        put(39 - i, y, 1)
    for x in range(8, 40):
        put(x, 1, 1)
    # The front, and the right side going back
    box(4, 5, 35, 21, 0, 2, 1)
    for i in range(1, 5):
        for y in range(5 - i, 21 - i + 1):
            put(35 + i, y, 1 if i == 4 or y == 21 - i else 3)
    # The drawer in the front, and its handle in the middle
    box(7, 8, 32, 19, 0, 1, 2)
    box(15, 12, 24, 15, 3, 2, 1)
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


def icon(stack, drawer=False):
    """The bytes of the .info: a tool icon with its ToolTypes, or a drawer icon (its window's place and size, all
    its files shown)"""
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
        2 if drawer else 3, 0,              # do_Type: WBDRAWER or WBTOOL, padding
        0,                                  # do_DefaultTool: none
        0 if drawer else 1,                 # do_ToolTypes: they follow (a tool's only)
        -0x80000000, -0x80000000,           # do_CurrentX/Y: NO_ICON_POSITION, placed by the Workbench
        1 if drawer else 0,                 # do_DrawerData: it follows (a drawer's only)
        0,                                  # do_ToolWindow
        stack)                              # do_StackSize
    if drawer:
        disk_object += struct.pack(
            ">hhhhBBIIIIIIIhhHHH",          # struct NewWindow, the drawer's window
            40, 20, 560, 180,               # LeftEdge, TopEdge, Width, Height: the archives' nine entries in
                                            # view on a 640 x 256 Workbench
            255, 255,                       # DetailPen, BlockPen: the screen's
            0, 0,                           # IDCMPFlags, Flags: set by the Workbench
            0, 0, 0, 0, 0,                  # FirstGadget, CheckMark, Title, Screen, BitMap
            90, 40, 0xFFFF, 0xFFFF,         # MinWidth, MinHeight, MaxWidth, MaxHeight
            1) + struct.pack(">ii", 0, 0)   # Type: WBENCHSCREEN; dd_CurrentX/Y: the window not scrolled
    image = struct.pack(">hhhhhIBBI", 0, 0, WIDTH, HEIGHT, 2, 1, 3, 0, 0) + \
        planes(draw_drawer() if drawer else draw())
    if drawer:
        # The DrawerData of AmigaOS 2.0 and later, after the rest: dd_Flags DDFLAGS_SHOWALL (the files without an
        # icon shown too: the documents), dd_ViewModes DDVM_BYICON
        return disk_object + image + struct.pack(">IH", 2, 1)
    tooltypes = struct.pack(">I", (len(TOOLTYPES) + 1) * 4)
    for t in TOOLTYPES:
        data = t.encode("ascii") + b"\0"
        tooltypes += struct.pack(">I", len(data)) + data
    return disk_object + image + tooltypes


def main():
    args = sys.argv[1:]
    drawer = bool(args) and args[0] == "--drawer"
    if drawer:
        args.pop(0)
    if len(args) not in (1, 2) or (drawer and len(args) != 1):
        sys.exit(__doc__)
    stack = 0 if drawer else int(args[1]) if len(args) == 2 else 16384
    with open(args[0], "wb") as f:
        f.write(icon(stack, drawer))


if __name__ == "__main__":
    main()
