# Cross-compilation for AmigaOS: m68k-amigaos-gcc must be in the PATH (otherwise use tools/amiga-make.sh, which
# finds the available toolchain; see README.md).
#
# One program per directory: src/<prog>/*.c -> out/<prog>.

CC      := m68k-amigaos-gcc
# No -m68020 by default: executables run on every CPU supported by AmigaOS 3.2 (68000+).
CFLAGS  := -Os -Wall -Wextra -noixemul -fomit-frame-pointer
LDFLAGS := -noixemul -s

PROGS   := $(notdir $(wildcard src/*))

# Per-program options
# amisqueeze (slimproto player): targets 68020+ (WinUAE, PiStorm); no FPU until there is a decoder.
# -Wno-pointer-sign: dos.library prototypes (Printf...) expect UBYTE * strings.
amisqueeze_CFLAGS := -m68020 -Wno-pointer-sign

# Headers copied into .sdk/ for the editor's IntelliSense (VS Code, on the host)
# Toolchain install prefix, derived from the compiler (/opt/m68k-amigaos in the amigadev/crosstools image)
TOOLCHAIN := $(abspath $(dir $(shell command -v $(CC)))..)
GCC_INC   := $(shell $(CC) -print-file-name=include)

.PHONY: all clean sdk $(PROGS)

all: $(PROGS)

define PROG_template
$(1): out/$(1)
out/$(1): $(patsubst src/%.c,obj/%.o,$(wildcard src/$(1)/*.c)) | out
	$$(CC) $$(LDFLAGS) $$($(1)_CFLAGS) -o $$@ $$^
obj/$(1)/%.o: CFLAGS += $$($(1)_CFLAGS)
endef
$(foreach p,$(PROGS),$(eval $(call PROG_template,$(p))))

# Check of the generated assembly (kept in obj/<prog>/*.s): tools/check-saves.py fails the build if a function
# modifies a d2-d7/a2-a6 register without saving it (gcc 16.1 bug, fixed in 16.2; kept as a safeguard).
# -MMD -MP: header dependencies in obj/<prog>/*.d
.DELETE_ON_ERROR:
obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -save-temps=obj -c -o $@ $<
	python3 tools/check-saves.py obj/$*.s

-include $(wildcard obj/*/*.d)

out:
	mkdir -p $@

clean:
	rm -rf obj out

sdk:
	rm -rf .sdk
	mkdir -p .sdk/gcc-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/ndk-include .sdk/ndk-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/libnix/include .sdk/libnix-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/sys-include .sdk/sys-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/include .sdk/include
	cp $(GCC_INC)/*.h .sdk/gcc-include/
