# Cross-compilation for AmigaOS: m68k-amigaos-gcc must be in the PATH (otherwise use tools/amiga-make.sh, which
# finds the available toolchain; see CONTRIBUTING.md).
#
# One program per directory: src/<prog>/*.c -> out/<prog>. In a program directory, amiga.c is only built for
# the Amiga and posix.c only for the host.
#
#   make [all]    Amiga build (out/): out/amisqueeze, and out/amisqueeze.fpu (68881 FPU code)
#   make host     host build of the programs listed in HOST_PROGS (out-host/), native compiler $(HOSTCC)
#   make test     host build, unit tests of the portable core (tests/test_core.c), then scenarios against the
#                 host build of amisqueeze driven by a fake server (tests/fake_server.py, python3)
#   make dist     release archive of amisqueeze: out/amisqueeze-<version>.zip (both executables, LICENSE, README.md,
#                 user guide), the version read from its $VER: string (tools/make-dist.py, python3)
#   make clean    removes every build directory
#   make sdk      copies the toolchain headers into .sdk/ (IntelliSense)

CC      := m68k-amigaos-gcc
# No -m68020 by default: executables run on every CPU supported by AmigaOS 3.2 (68000+).
# One section per function, unused ones dropped at link time: third-party code brings functions we do not call
# (dr_flac's float output would need soft-float helpers libnix does not link).
CFLAGS  := -Os -Wall -Wextra -noixemul -fomit-frame-pointer -ffunction-sections -fdata-sections
LDFLAGS := -noixemul -s -Wl,--gc-sections

PROGS   := $(notdir $(wildcard src/*))

# Per-program options
# amisqueeze (slimproto player): targets 68020+ (WinUAE, PiStorm), without FPU.
# -Wno-pointer-sign: dos.library prototypes expect UBYTE * strings. -Werror=format: msg() format checks.
amisqueeze_CFLAGS := -m68020 -Wno-pointer-sign -Werror=format -Ithird_party/dr_flac -Ithird_party/minimp3

# amisqueeze.fpu: the same sources built for a 68881/68882 FPU (or a 68040/68060 with FPU): floating point in
# hardware, so it holds the MP3 decoder (AMISQUEEZE_MP3). Objects in obj/amisqueeze-fpu/. minimp3 needs about
# 17 KB of stack: libnix's start-up code enlarges it (__stkinit, size in __stack, amiga.c).
amisqueeze_fpu_CFLAGS  := $(amisqueeze_CFLAGS) -m68881 -DAMISQUEEZE_FPU -DAMISQUEEZE_MP3
amisqueeze_fpu_LDFLAGS := -Wl,-u,___stkinit

# Third-party code (third_party/): built without our warnings, in a file of its own
THIRD_PARTY := dr_flac minimp3
$(foreach t,$(THIRD_PARTY),$(eval obj/amisqueeze/$(t).o: CFLAGS += -w -O2))
$(foreach t,$(THIRD_PARTY),$(eval obj/amisqueeze-fpu/$(t).o: CFLAGS += -w -O2))
$(foreach t,$(THIRD_PARTY),$(eval obj-host/amisqueeze/$(t).o: HOST_CFLAGS += -w))

# Host build: the portable core with posix.c, for tests on the developer's machine. AddressSanitizer and
# UndefinedBehaviorSanitizer stop the program at the first memory error or undefined behaviour.
HOST_PROGS   := amisqueeze
HOSTCC       ?= cc
HOST_CFLAGS  := -O1 -g -Wall -Wextra -Werror=format -fno-omit-frame-pointer -fsanitize=address,undefined \
                -Ithird_party/dr_flac -Ithird_party/minimp3 -DAMISQUEEZE_MP3
HOST_LDFLAGS := -fsanitize=address,undefined

amiga_srcs = $(filter-out src/$(1)/posix.c,$(wildcard src/$(1)/*.c))
host_srcs  = $(filter-out src/$(1)/amiga.c,$(wildcard src/$(1)/*.c))

# Headers copied into .sdk/ for the editor's IntelliSense (VS Code, on the host). Deferred (=): evaluated only
# by the sdk target, so the host build does not need the Amiga compiler.
# Toolchain install prefix, derived from the compiler (/opt/m68k-amigaos in the amigadev/crosstools image)
TOOLCHAIN = $(abspath $(dir $(shell command -v $(CC)))..)
GCC_INC   = $(shell $(CC) -print-file-name=include)

.PHONY: all clean sdk host test dist $(PROGS)

all: $(PROGS)

define PROG_template
$(1): out/$(1)
out/$(1): $(patsubst src/%.c,obj/%.o,$(call amiga_srcs,$(1))) | out
	$$(CC) $$(LDFLAGS) $$($(1)_CFLAGS) -o $$@ $$^
obj/$(1)/%.o: CFLAGS += $$($(1)_CFLAGS)
endef
$(foreach p,$(PROGS),$(eval $(call PROG_template,$(p))))

amisqueeze: out/amisqueeze.fpu out/amisqueeze.info out/amisqueeze.fpu.info
# Workbench icons (classic AmigaOS format), drawn by tools/make-icon.py
out/%.info: tools/make-icon.py | out
	python3 tools/make-icon.py $@
out/amisqueeze.fpu: $(patsubst src/amisqueeze/%.c,obj/amisqueeze-fpu/%.o,$(call amiga_srcs,amisqueeze)) | out
	$(CC) $(LDFLAGS) $(amisqueeze_fpu_LDFLAGS) $(amisqueeze_fpu_CFLAGS) -o $@ $^
obj/amisqueeze-fpu/%.o: src/amisqueeze/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(amisqueeze_fpu_CFLAGS) -MMD -MP -save-temps=obj -c -o $@ $<
	python3 tools/check-saves.py obj/amisqueeze-fpu/$*.s

define HOST_template
host: out-host/$(1)
out-host/$(1): $(patsubst src/%.c,obj-host/%.o,$(call host_srcs,$(1)))
	@mkdir -p $$(dir $$@)
	$$(HOSTCC) $$(HOST_LDFLAGS) -o $$@ $$^
endef
$(foreach p,$(HOST_PROGS),$(eval $(call HOST_template,$(p))))

# Unit tests: the portable core of amisqueeze on a fake platform (tests/stub_platform.c)
TEST_CORE := player slimproto stream ring audio decoder flac dr_flac mp3 minimp3 bench bench_flac bench_mp3
out-host/test_core: obj-host/tests/test_core.o obj-host/tests/stub_platform.o \
                    $(patsubst %,obj-host/amisqueeze/%.o,$(TEST_CORE))
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_LDFLAGS) -o $@ $^

test: host out-host/test_core
	out-host/test_core
	python3 tests/fake_server.py out-host/amisqueeze

dist: out/amisqueeze out/amisqueeze.fpu out/amisqueeze.info out/amisqueeze.fpu.info
	python3 tools/make-dist.py amisqueeze

# Check of the generated assembly (kept in obj/<prog>/*.s): tools/check-saves.py fails the build if a function
# modifies a d2-d7/a2-a6 register without saving it (gcc 16.1 bug, fixed in 16.2; kept as a safeguard).
# -MMD -MP: header dependencies in obj/<prog>/*.d
.DELETE_ON_ERROR:
obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -save-temps=obj -c -o $@ $<
	python3 tools/check-saves.py obj/$*.s

obj-host/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -MMD -MP -c -o $@ $<

obj-host/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -Isrc/amisqueeze -MMD -MP -c -o $@ $<

-include $(wildcard obj/*/*.d) $(wildcard obj-host/*/*.d)

out:
	mkdir -p $@

clean:
	rm -rf obj out obj-host out-host

sdk:
	rm -rf .sdk
	mkdir -p .sdk/gcc-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/ndk-include .sdk/ndk-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/libnix/include .sdk/libnix-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/sys-include .sdk/sys-include
	cp -r $(TOOLCHAIN)/m68k-amigaos/include .sdk/include
	cp $(GCC_INC)/*.h .sdk/gcc-include/
