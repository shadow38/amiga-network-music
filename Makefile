# Cross-compilation for AmigaOS: m68k-amigaos-gcc must be in the PATH (otherwise use tools/amiga-make.sh, which
# finds the available toolchain; see CONTRIBUTING.md).
#
# One program per directory: src/<prog>/*.c -> out/<prog>. In a program directory, amiga*.c (amiga.c,
# amiga_ahi.c) are only built for the Amiga and posix.c only for the host.
#
#   make [all]    Amiga build (out/): out/amisqueeze, and out/amisqueeze.fpu (68881 FPU code)
#   make host     host build of the programs listed in HOST_PROGS (out-host/), native compiler $(HOSTCC)
#   make test     host build, unit tests of the portable core (tests/test_core.c), then scenarios against the
#                 host build of amisqueeze driven by a fake server (tests/fake_server.py, python3), and against
#                 its build without MP3
#   make dist     release archives of amisqueeze: out/amisqueeze-<version>.zip and .lha (both executables and their
#                 icons, LICENSE, README.md, CHANGELOG.md, the user guide and its images), the version read from
#                 its $VER: string, their SHA-256 in out/amisqueeze-<version>.zip.sha256 and .lha.sha256, and the
#                 Aminet readme out/amisqueeze-<version>.readme (tools/make-dist.py, python3, lha); DEV=1 accepts a
#                 development version, to try them (never published)
#   make cpucount the free CPU meter of the tests (tools/amiga/cpucount.c -> out/cpucount), a developer tool:
#                 built on demand only, never by make all nor in the release archive
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
# The Shell's stack (4 KB by default) is too small for the decoders (dr_flac about 5.5 KB, minimp3 about 17.5 KB):
# libnix's start-up code enlarges it (__stkinit, linked in here; size in __stack, amiga.c). tools/check-stack.py
# reads the linker map of each program of STACK_PROGS: __stkinit linked, __stack defined in its amiga.o, with the
# size <prog>_STACK (the measure: CONTRIBUTING.md, Stack and memory).
amisqueeze_CFLAGS  := -m68020 -Wno-pointer-sign -Werror=format -Ithird_party/dr_flac -Ithird_party/minimp3
amisqueeze_LDFLAGS := -Wl,-u,___stkinit
amisqueeze_STACK   := 16384
STACK_PROGS        := amisqueeze

# amisqueeze.fpu: the same sources built for a 68881/68882 FPU (or a 68040/68060 with FPU): floating point in
# hardware, so it holds the MP3 decoder (AMISQUEEZE_MP3). Objects in obj/amisqueeze-fpu/.
amisqueeze_fpu_CFLAGS  := $(amisqueeze_CFLAGS) -m68881 -DAMISQUEEZE_FPU -DAMISQUEEZE_MP3
amisqueeze_fpu_LDFLAGS := $(amisqueeze_LDFLAGS)
amisqueeze_fpu_STACK   := 32768

# Third-party code (third_party/): built without our warnings, in a file of its own
THIRD_PARTY := dr_flac minimp3
$(foreach t,$(THIRD_PARTY),$(eval obj/amisqueeze/$(t).o: CFLAGS += -w -O2))
$(foreach t,$(THIRD_PARTY),$(eval obj/amisqueeze-fpu/$(t).o: CFLAGS += -w -O2))
$(foreach t,$(THIRD_PARTY),$(eval obj-host/amisqueeze/$(t).o: HOST_CFLAGS += -w))
# dr_flac's assert() off on the Amiga: one that fired would end the program without closing ahi.device. The host
# build keeps it, for the tests.
obj/amisqueeze/dr_flac.o obj/amisqueeze-fpu/dr_flac.o: CFLAGS += -DNDEBUG

# Host build: the portable core with posix.c, for tests on the developer's machine. AddressSanitizer and
# UndefinedBehaviorSanitizer stop the program at the first memory error or undefined behaviour (UBSan alone would
# only report it and go on: -fno-sanitize-recover makes it stop, so that the tests fail).
HOST_PROGS   := amisqueeze
HOSTCC       ?= cc
SANITIZE     := -fsanitize=address,undefined -fno-sanitize-recover=all
HOST_CFLAGS  := -O1 -g -Wall -Wextra -Werror=format -fno-omit-frame-pointer $(SANITIZE) \
                -Ithird_party/dr_flac -Ithird_party/minimp3 -DAMISQUEEZE_MP3
HOST_LDFLAGS := $(SANITIZE)

amiga_srcs = $(filter-out src/$(1)/posix.c,$(wildcard src/$(1)/*.c))
host_srcs  = $(filter-out src/$(1)/amiga%.c,$(wildcard src/$(1)/*.c))

# Headers copied into .sdk/ for the editor's IntelliSense (VS Code, on the host). Deferred (=): evaluated only
# by the sdk target, so the host build does not need the Amiga compiler.
# Toolchain install prefix, derived from the compiler (/opt/m68k-amigaos in the amigadev/crosstools image)
TOOLCHAIN = $(abspath $(dir $(shell command -v $(CC)))..)
GCC_INC   = $(shell $(CC) -print-file-name=include)

.PHONY: all clean sdk host test dist cpucount $(PROGS)

all: $(PROGS)

define PROG_template
$(1): out/$(1)
out/$(1): $(patsubst src/%.c,obj/%.o,$(call amiga_srcs,$(1))) | out
	$$(CC) $$(LDFLAGS) $$($(1)_LDFLAGS) $$($(1)_CFLAGS) -Wl,-Map,obj/$(1).map -o $$@ $$^
	$(if $(filter $(1),$(STACK_PROGS)),python3 tools/check-stack.py obj/$(1).map obj/$(1)/amiga.o $($(1)_STACK))
obj/$(1)/%.o: CFLAGS += $$($(1)_CFLAGS)
endef
$(foreach p,$(PROGS),$(eval $(call PROG_template,$(p))))

amisqueeze: out/amisqueeze.fpu out/amisqueeze.info out/amisqueeze.fpu.info
# Workbench icons (classic AmigaOS format), drawn by tools/make-icon.py
out/%.info: tools/make-icon.py | out
	python3 tools/make-icon.py $@
out/amisqueeze.fpu: $(patsubst src/amisqueeze/%.c,obj/amisqueeze-fpu/%.o,$(call amiga_srcs,amisqueeze)) | out
	$(CC) $(LDFLAGS) $(amisqueeze_fpu_LDFLAGS) $(amisqueeze_fpu_CFLAGS) -Wl,-Map,obj/amisqueeze-fpu.map -o $@ $^
	python3 tools/check-stack.py obj/amisqueeze-fpu.map obj/amisqueeze-fpu/amiga.o $(amisqueeze_fpu_STACK)
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
TEST_CORE := player slimproto stream icy ring audio decoder flac dr_flac mp3 minimp3 bench bench_flac bench_mp3
out-host/test_core: obj-host/tests/test_core.o obj-host/tests/stub_platform.o \
                    $(patsubst %,obj-host/amisqueeze/%.o,$(TEST_CORE))
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_LDFLAGS) -o $@ $^

# amisqueeze without the MP3 decoder, as out/amisqueeze is: built by make test so that its #else parts compile, and
# run by tests/fake_server.py (what it advertises and refuses)
HOST_NOMP3_CFLAGS := $(filter-out -DAMISQUEEZE_MP3,$(HOST_CFLAGS))
$(foreach t,$(THIRD_PARTY),$(eval obj-host/nomp3/amisqueeze/$(t).o: HOST_NOMP3_CFLAGS += -w))
out-host/amisqueeze-nomp3: $(patsubst src/%.c,obj-host/nomp3/%.o,$(call host_srcs,amisqueeze))
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_LDFLAGS) -o $@ $^

# tools/check-version.py: the version number agrees everywhere (make dist checks it again through make-dist.py,
# with --release: no -dev version, the CHANGELOG section and its date); make-dist.py --check-readme: the Aminet
# readme made from dist/amisqueeze.readme follows Aminet's rules (make dist checks it again); tests/check-saves.s:
# the assembly safeguard must name each _bad_ function there, and no _good_ one; tools/check-core.py: the portable
# core includes no header of the system; tools/check-quality.py: the C code's size and complexity (lizard, pinned in
# tools/requirements.txt), and no step name in the code
SAVES_BAD := _bad_dbra _bad_fpu _bad_plain _bad_postinc _bad_predec _bad_tst_postinc
test: host out-host/test_core out-host/amisqueeze-nomp3
	python3 tools/check-version.py
	python3 tools/check-core.py amisqueeze
	python3 tools/check-quality.py
	python3 tools/make-dist.py --check-readme amisqueeze
	python3 tools/make-dist.py --check-text amisqueeze
	python3 -c 'import ast, sys; [ast.parse(open(f, encoding="utf-8").read(), f) for f in sys.argv[1:]]' \
	    tools/*.py tools/winuae/*.py tests/*.py
	for f in tools/*.sh; do sh -n $$f || exit 1; done; echo "scripts: syntax checked"
	python3 tools/check-saves.py tests/check-saves.s | sed -n 's/.* error: \([a-z_]*\) modifies.*/\1/p' | sort | \
	    tr '\n' ' ' | grep -qx '$(SAVES_BAD) ' && echo "check-saves: self-test passed" || \
	    { echo "check-saves: self-test failed, expected: $(SAVES_BAD)"; python3 tools/check-saves.py tests/check-saves.s; exit 1; }
	out-host/test_core
	python3 tests/fake_server.py out-host/amisqueeze out-host/amisqueeze-nomp3

dist: out/amisqueeze out/amisqueeze.fpu out/amisqueeze.info out/amisqueeze.fpu.info
	python3 tools/make-dist.py $(if $(DEV),--dev) amisqueeze

# Developer tools that run on the Amiga (tools/amiga/), plain 68000 code
cpucount: out/cpucount
out/cpucount: obj/tools/cpucount.o | out
	$(CC) $(LDFLAGS) -o $@ $^
obj/tools/cpucount.o: tools/amiga/cpucount.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-pointer-sign -MMD -MP -save-temps=obj -c -o $@ $<
	python3 tools/check-saves.py obj/tools/cpucount.s

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

obj-host/nomp3/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_NOMP3_CFLAGS) -MMD -MP -c -o $@ $<

obj-host/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -Isrc/amisqueeze -MMD -MP -c -o $@ $<

-include $(wildcard obj/*/*.d) $(wildcard obj-host/*/*.d) $(wildcard obj-host/nomp3/*/*.d)

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
