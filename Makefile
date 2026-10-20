# ARCADE-1 — build the Libretro core.
#
# Produces arcade1_libretro.so, loadable by RetroArch (or any libretro frontend).
# The machine (src/arcade1/) is frontend-independent; src/libretro/ is a thin
# adapter. `make android` cross-compiles the same core for RetroArch on Android
# (Google TV / Android TV boxes) — see the Android section below.

# Keep plain `make` building the .so: the test-* prerequisite rules are
# declared before `all` and would otherwise become the default goal.
.DEFAULT_GOAL := all

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
CFLAGS  += -fPIC -Ideps/libretro -Isrc/arcade1
LDFLAGS += -shared

TARGET  := arcade1_libretro.so
SRC     := src/arcade1/machine.c src/arcade1/memory.c src/arcade1/cpu.c \
           src/arcade1/exec.c src/arcade1/timer.c src/arcade1/video.c \
           src/arcade1/audio.c src/arcade1/rom.c src/libretro/libretro.c

# Test executables are built into this directory (gitignored) instead of /tmp.
TESTDIR := build

# Every test target builds into $(TESTDIR), so make sure the directory exists
# first. Order-only ("|") so its presence never affects rebuild decisions.
test test-input test-square test-beep test-machine test-memory test-cpu \
test-exec test-program test-mmio test-timer test-video test-gfx test-rom \
test-sprite test-tilemap test-scroll test-platformer test-breakout \
test-tetris test-bench test-terrain test-ray test-sandbox test-systems \
test-persist test-frame-rules test-video-worst test-video-palette \
test-video-sprite4 test-video-tilemap4 test-color test-run \
test-barrier test-m29game: | $(TESTDIR)

$(TESTDIR):
	@mkdir -p $(TESTDIR)

# --- RISC-V guest toolchain (milestones 10 + 16) ----------------------------
# Bare-metal RV32IM: the M extension (multiply/divide) is part of the CPU
# since M16, so guests compile for rv32im and may use * and / directly — still
# no libc, no libgcc, no SDK. Guest code is built to a flat binary and loaded
# into the VM's ROM.
RISCV_GCC     := riscv64-unknown-elf-gcc
RISCV_OBJCOPY := riscv64-unknown-elf-objcopy
RISCV_NM      := riscv64-unknown-elf-nm
GUEST_ARCH    := -march=rv32im -mabi=ilp32
GUEST_CFLAGS  := $(GUEST_ARCH) -Os -ffreestanding -nostdlib -nostartfiles \
                 -msmall-data-limit=0
GUEST_BUILD   := programs/build
GUEST_PROGS   := counter mmio_demo video_demo gfx_demo sprite_demo tilemap_demo scroll_demo color_demo platformer breakout tetris bench_gfx terrain_gen raycast_demo sandbox systems example_pong example_snake example_color m29game
GUEST_ELFS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .elf,$(GUEST_PROGS)))
GUEST_BINS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .bin,$(GUEST_PROGS)))
# Since M17 every guest is also packaged into a proper .arc1 game image.
GUEST_ARCS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .arc1,$(GUEST_PROGS)))

# --- Golden ROM directory (Task A, steps 3-6) -------------------------------
# The four game tests take their guest image from $(ARC1_ROM_DIR), which
# defaults to the freshly built guest directory (unchanged behaviour).
# Pointing it at tests/roms runs the SAME tests — every assertion intact —
# against the committed golden images plus the committed `nm` symbol dumps
# (<name>.syms), so no RISC-V toolchain is needed. `make test-golden` does
# exactly that and diffs the per-frame hash lines against tests/golden/.
# `make check` = every test target + test-golden. Both test-golden and
# golden-update are MANUAL: a golden change is a behaviour change (see
# docs.md "Golden regression policy").
ARC1_ROM_DIR  ?= $(GUEST_BUILD)
GOLDEN_ROMDIR := tests/roms
GOLDEN_DIR    := tests/golden
GOLDEN_GAMES  := breakout tetris platformer scroll color pong snake example-color \
                  m29game asteroid_run
# Image/ELF base name for a golden game (test-target name -> guest name).
golden_rom    = $(if $(filter scroll,$(1)),scroll_demo,\
                $(if $(filter color,$(1)),color_demo,\
                $(if $(filter pong snake,$(1)),example_$(1),\
                $(if $(filter example-color,$(1)),example_color,$(1)))))
# Symbol source for a game in $(ARC1_ROM_DIR): the committed `nm` dump if
# one is present, else `nm` the freshly built ELF (historical behaviour).
# Decided at make-expansion time so the result is a single command that can
# be piped into awk without any subshell parentheses.
nm_src        = $(if $(wildcard $(ARC1_ROM_DIR)/$(1).syms), \
                    cat $(ARC1_ROM_DIR)/$(1).syms, \
                    $(RISCV_NM) $(GUEST_BUILD)/$(1).elf)

# Keep the ELFs (make would otherwise delete them as intermediates); the test
# reads symbols (e.g. counter's address) from them.
.SECONDARY: $(GUEST_ELFS)

.PHONY: all clean program experimental android android-arm64 android-arm \
        test test-input test-square test-beep test-machine \
        test-memory test-cpu test-exec test-program test-mmio test-timer \
        test-video test-gfx test-rom test-sprite test-tilemap test-scroll \
        test-platformer test-breakout test-tetris test-bench test-terrain \
        test-ray test-sandbox test-systems test-persist roms-update \
        test-golden golden-update check test-frame-rules test-video-worst \
        test-video-palette test-video-sprite4 test-video-tilemap4 test-color \
        test-run

# --- Developer CLI: `arcade1 run` (milestone 28, AI SDK v0.1) ---------------
# The variables live up here, NOT beside their build rules further down:
# prereq lists expand when make READS the rule, so $(CLI_BIN)/$(CLI_NOSDL)
# used by the test targets below must be defined before the first such rule
# (a later definition expands to nothing and silently drops the prerequisite
# — the recipe still expands correctly, which hides the bug).
# A headless front end to the reference VM for humans and AI agents. Links ONLY
# src/arcade1 (no Libretro, no RetroArch, no GPU), so it runs identically in CI
# and in a sandbox. It includes tests/state_hash.h (via -Itests) so the hashes
# it prints are bit-identical to the golden regression's.
CLI_BIN    := $(TESTDIR)/arcade1
CLI_SRC    := tools/arcade1.c src/arcade1/machine.c src/arcade1/memory.c \
              src/arcade1/cpu.c src/arcade1/exec.c src/arcade1/timer.c \
              src/arcade1/video.c src/arcade1/audio.c src/arcade1/rom.c

# Play window (`arcade1 run --live`, ROADMAP_V3 §4.5): SDL3 is OPTIONAL.
# pkg-config decides — with SDL3 present the CLI grows the window; without it
# the build is byte-for-byte the same surface minus --live, which then exits 3
# with a clean message. tools/arcade1.c compiles both shapes from one source
# (ARCADE1_HAVE_SDL3 selects the real window or the stubs), so nothing else,
# and none of `make check`, depends on SDL3.
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL3_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
PLAY_CFLAGS := $(if $(strip $(SDL3_LIBS)),-DARCADE1_HAVE_SDL3 $(SDL3_CFLAGS))
PLAY_LIBS   := $(strip $(SDL3_LIBS))

# The same CLI compiled WITHOUT the play window: the absence path that
# tests/test-playwindow.sh checks (exit 3, clean message, everything else
# intact). Built on demand by that test, never part of `make check`.
CLI_NOSDL   := $(TESTDIR)/arcade1_nosdl

# `make check` = the whole regression: every test target plus the golden
# per-frame comparison (Task A step 6). This is the command CI/humans should
# run before pushing.
CHECK_TARGETS := test test-input test-square test-beep test-machine \
        test-memory test-cpu test-exec test-program test-mmio test-timer \
        test-video test-gfx test-rom test-sprite test-tilemap test-scroll \
        test-platformer test-breakout test-tetris test-bench test-terrain \
        test-ray test-sandbox test-systems test-persist test-frame-rules \
        test-video-worst test-video-palette test-video-sprite4 \
        test-video-tilemap4 test-color test-run test-golden \
        test-pong test-snake test-example-color test-cheatsheet \
        test-barrier test-build test-oob \
        test-m29game test-syms test-capture-png test-text test-arc1assetc \
        test-mapdemo test-asteroid_run \
        test-golden-conditions test-inspect test-inspect-commands

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

# --- Android cross-builds (RetroArch on Google TV / Android TV) -------------
# The desktop rule above targets the host (x86-64 Linux). RetroArch on Android
# needs a core for the *device* ABI, and the ABI must match the installed
# RetroArch build: arm64-v8a (most Google TV devices and 64-bit RetroArch) or
# armeabi-v7a (32-bit devices / 32-bit RetroArch). The core is plain C11 with
# no host dependencies, so both cross-compile with only the NDK's clang:
#
#   make android          # both ABIs
#   make android-arm64    # -> arcade1_libretro_android_arm64.so
#   make android-arm      # -> arcade1_libretro_android_arm.so
#
# Install on the TV: copy the matching .so into the device's Downloads folder,
# then RetroArch -> Load Core -> Install or Restore a Core -> pick the file.
# API 21 is the minimum that supports arm64 (and is what RetroArch itself
# targets). ANDROID_NDK can be overridden to point at a different NDK.
ANDROID_NDK ?= $(shell ls -d $(HOME)/Android/Sdk/ndk/* 2>/dev/null | sort -V | tail -1)
NDK_LLVM    := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
NDK_API     := 21
A64_CC      := $(NDK_LLVM)/aarch64-linux-android$(NDK_API)-clang
A32_CC      := $(NDK_LLVM)/armv7a-linux-androideabi$(NDK_API)-clang
A64_TARGET  := arcade1_libretro_android_arm64.so
A32_TARGET  := arcade1_libretro_android_arm.so

android: android-arm64 android-arm

android-arm64: $(A64_TARGET)
android-arm: $(A32_TARGET)

$(A64_TARGET): $(SRC)
	$(A64_CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

$(A32_TARGET): $(SRC)
	$(A32_CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

# Build and run the milestone-02 test-pattern harness against the real .so.
test: $(TARGET)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_pattern tests/test_pattern.c \
	    -L. -l:$(TARGET) -Wl,-rpath,"$(CURDIR)"
	LD_LIBRARY_PATH=. $(TESTDIR)/test_pattern

# Build and run the milestone-03 RetroPad input harness against the real .so.
test-input: $(TARGET)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_input tests/test_input.c \
	    -L. -l:$(TARGET) -Wl,-rpath,"$(CURDIR)"
	LD_LIBRARY_PATH=. $(TESTDIR)/test_input

# Build and run the milestone-04 moving-square harness against the real .so.
test-square: $(TARGET)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_square tests/test_square.c \
	    -L. -l:$(TARGET) -Wl,-rpath,"$(CURDIR)"
	LD_LIBRARY_PATH=. $(TESTDIR)/test_square

# Build and run the milestone-05 audio-beep harness against the real .so.
test-beep: $(TARGET)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_beep tests/test_beep.c \
	    -L. -l:$(TARGET) -Wl,-rpath,"$(CURDIR)"
	LD_LIBRARY_PATH=. $(TESTDIR)/test_beep

# Build and run the milestone-06 machine harness. Links ONLY the machine
# sources — no libretro, no .so — proving the machine is usable without
# RetroArch. (M15 made the machine own the guest CPU, so exec+cpu are part
# of "the machine sources" now.)
test-machine:
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_machine \
	    tests/test_machine.c src/arcade1/machine.c src/arcade1/memory.c \
	    src/arcade1/timer.c src/arcade1/video.c src/arcade1/audio.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_machine

# Build and run the milestone-07 memory/bus harness. Links ONLY memory.c —
# no libretro, no machine, no .so — proving the bus is independently testable.
test-memory:
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_memory \
	    tests/test_memory.c src/arcade1/memory.c
	$(TESTDIR)/test_memory

# Build and run the milestone-08 RV32I decoder harness. Links ONLY cpu.c — no
# libretro, no machine, no .so — proving the decoder is independently testable.
test-cpu:
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_cpu \
	    tests/test_cpu.c src/arcade1/cpu.c
	$(TESTDIR)/test_cpu

# Build and run the milestone-09 RV32I execution harness. Links exec.c +
# cpu.c + memory.c (no libretro, no machine, no .so) — proving the interpreter
# runs without a frontend.
test-exec:
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_exec \
	    tests/test_exec.c src/arcade1/exec.c src/arcade1/cpu.c \
	    src/arcade1/memory.c
	$(TESTDIR)/test_exec

# --- Guest programs: C -> RV32 ELF -> flat binary -> .arc1 image -----------
program: $(GUEST_BINS) $(GUEST_ARCS)

# gfx.h is shared guest API: rebuild guests when it changes.
$(GUEST_BUILD)/%.elf: programs/%.c programs/gfx.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -T programs/link.ld -o $@ programs/crt0.S $<

$(GUEST_BUILD)/%.bin: $(GUEST_BUILD)/%.elf
	$(RISCV_OBJCOPY) -O binary $< $@

# Milestone 17: wrap each flat binary into a proper ARCADE-1 game image.
# The packer shares the header writer and CRC with the VM itself
# (src/arcade1/rom.c), so writer and reader cannot disagree.
$(GUEST_BUILD)/%.arc1: $(GUEST_BUILD)/%.bin $(TESTDIR)/packarc1
	$(TESTDIR)/packarc1 $< $@

# --- M29 regression probes (tests/guests/*.c) --------------------------------
# Same pipeline and same flags as a real guest (GUEST_CFLAGS, -Os), because
# the miscompile they pin down only shows up in optimized code: a table the
# device reads through a register address gets deleted as dead. probe_tm4 is
# built once per geometry so the test covers the loop shapes GCC deletes as
# well as the ones it keeps. Run by `make test-barrier`.
PROBE_DIR      := tests/guests
PROBE_TM4_GEOS := 4x3 5x1 1x8 6x3 10x3
PROBE_BINS     := $(addprefix $(GUEST_BUILD)/,$(addsuffix .bin,\
                    $(addprefix probe_tm4_,$(PROBE_TM4_GEOS)) \
                    probe_sprite4 probe_tilemap1))

# probe_tm4_4x3.bin <- tests/guests/probe_tm4.c built with -DCOLS=4 -DROWS=3
# (the stem is <cols>x<rows>; the shorter-stem rule wins over probe_%.bin).
$(GUEST_BUILD)/probe_tm4_%.bin: $(PROBE_DIR)/probe_tm4.c programs/gfx.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -Iprograms -T programs/link.ld \
	    -o $(GUEST_BUILD)/probe_tm4_$*.elf programs/crt0.S $< \
	    -DCOLS=$(word 1,$(subst x, ,$*)) -DROWS=$(word 2,$(subst x, ,$*))
	$(RISCV_OBJCOPY) -O binary $(GUEST_BUILD)/probe_tm4_$*.elf $@

$(GUEST_BUILD)/probe_%.bin: $(PROBE_DIR)/probe_%.c programs/gfx.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -Iprograms -T programs/link.ld \
	    -o $(GUEST_BUILD)/probe_$*.elf programs/crt0.S $<
	$(RISCV_OBJCOPY) -O binary $(GUEST_BUILD)/probe_$*.elf $@

# One test binary drives every probe image: boot each, run a few frames, and
# count the painted pixels (tests/test_barrier.c).
test-barrier: $(TESTDIR)/test_barrier $(PROBE_BINS) | $(TESTDIR)
	$(TESTDIR)/test_barrier $(GUEST_BUILD)

# ROADMAP_V3 A6: the 5x7 text-font probe — the test checks glyph pixels
# against a hand-written oracle (never derived from text.h) and the
# arcade1_rand(3) histogram. Built exactly like the other probes.
$(GUEST_BUILD)/text_probe.bin: $(PROBE_DIR)/text_probe.c programs/text.h \
        programs/arcade1.h programs/gfx.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -Iprograms -T programs/link.ld \
	    -o $(GUEST_BUILD)/text_probe.elf programs/crt0.S $<
	$(RISCV_OBJCOPY) -O binary $(GUEST_BUILD)/text_probe.elf $@

test-text: $(CLI_BIN) $(GUEST_BUILD)/text_probe.bin
	sh tests/test_text.sh $(CLI_BIN) $(GUEST_BUILD)/text_probe.bin $(GOLDEN_DIR)

# Wave 1 card 1.1 (docs/ROADMAP_CREATION_FLOW.md): arc1-assetc is python3
# stdlib host tooling — no build step, the test invokes it directly.
test-arc1assetc: tools/arc1_assetc.py tests/test_arc1assetc.sh
	sh tests/test_arc1assetc.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

# map_demo's test IS in make check (unlike walkdemo/asset_demo): its 4x3
# and 5x1 fixtures are the draw-proof that replaced the retracted tilemap
# min-geometry validator (ROADMAP_V3 4.3), and a proof that only runs by
# hand pins nothing.
test-mapdemo: $(CLI_BIN) $(GUEST_BUILD)/map_demo.arc1
	sh tests/test_mapdemo.sh $(CLI_BIN) $(GUEST_BUILD)

# Wave B card B1 (ROADMAP_V3 §5): structured last-frame inspect report —
# every key on m29game, --strict agreement, --json parses, --regs gate.
test-inspect: $(CLI_BIN) tests/test_inspect.sh
	sh tests/test_inspect.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

# Wave B card B2 (ROADMAP_V3 §5): inspect command records + pixel
# provenance. tests/guests/probe_cmds.c draws a sprite, then a FILL
# covering it, paced by the timer tick; the .elf survives so the test can
# bound main's symbol range with $(RISCV_NM) and prove each record's pc
# is the issuing store's PC.
test-inspect-commands: $(CLI_BIN) $(GUEST_BUILD)/probe_cmds.arc1 \
        $(GUEST_BUILD)/probe_cmds.elf
	sh tests/test_inspect_commands.sh $(CLI_BIN) \
	    $(GUEST_BUILD)/probe_cmds.arc1 $(GUEST_BUILD)/probe_cmds.elf \
	    $(RISCV_NM)

# Wave 0 card 0.4 (ROADMAP_V3 4.1): the F9 conditions manifest is
# enforced — it parses, the CLI reproduces the recorded runs from it, a
# bare run points at it, a tampered golden fails, partial coverage fails.
test-golden-conditions: $(CLI_BIN) tests/golden/conditions.txt
	sh tests/test_golden_conditions.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

$(TESTDIR)/test_barrier: tests/test_barrier.c src/arcade1/machine.c \
        src/arcade1/video.c src/arcade1/timer.c src/arcade1/audio.c \
        src/arcade1/memory.c src/arcade1/exec.c src/arcade1/cpu.c \
        src/arcade1/rom.c | $(TESTDIR)
	$(CC) $(CFLAGS) -o $@ tests/test_barrier.c src/arcade1/machine.c \
	    src/arcade1/video.c src/arcade1/timer.c src/arcade1/audio.c \
	    src/arcade1/memory.c src/arcade1/exec.c src/arcade1/cpu.c \
	    src/arcade1/rom.c

# --- Experimental guests ------------------------------------------------------
# programs/experimental/ holds works in progress: NOT built by default (not in
# GUEST_PROGS). Build on demand with `make experimental`.
# walkdemo is an experiment (docs/experiments/walkdemo.md), so its test is
# run by hand with `make test-walkdemo` — not part of `make check`.
test-walkdemo: $(CLI_BIN) $(GUEST_BUILD)/walkdemo.arc1
	sh tests/test_walkdemo.sh $(CLI_BIN) $(GUEST_BUILD)

# The play window (`arcade1 run --live`, ROADMAP_CREATION_FLOW §10) is by
# hand too, for the same reason plus a stronger one: SDL3 and an X display
# are optional dependencies (ROADMAP_V3 §4.5 — exit 3 when absent, NOT in
# `make check`). Run: make test-playwindow
# It needs the CLI, the same CLI built without SDL3 (the absence path), and
# walkdemo (the card's acceptance guest); it builds its own XTEST key
# injector when a display is available and notes a skip when not.
test-playwindow: $(CLI_BIN) $(CLI_NOSDL) $(GUEST_BUILD)/walkdemo.arc1
	sh tests/test_playwindow.sh $(CLI_BIN) $(CLI_NOSDL) $(GUEST_BUILD)

# asset_demo is the Wave-2 typed-accessor experiment (docs/experiments/
# arc1-assetc.md), likewise run by hand with `make test-assetdemo`.
test-assetdemo: $(CLI_BIN) $(GUEST_BUILD)/asset_demo.arc1
	sh tests/test_assetdemo.sh $(CLI_BIN) $(GUEST_BUILD)

# asteroid_run is Game #1 (ROADMAP_V3 Wave 5): it graduated — its test and
# its golden are part of `make check` (test-asteroid_run, golden game 10).
# Its placeholder art is generated (tools/gen_game1_art.py); `make
# game1-sheet` regenerates art + manifest + level through arc1-assetc.
game1-sheet: programs/experimental/asteroid_run_sheet.h

programs/experimental/asteroid_run_player.png programs/experimental/asteroid_run_enemies.png programs/experimental/asteroid_run_tiles.png programs/experimental/asteroid_run_level.txt: tools/gen_game1_art.py
	python3 tools/gen_game1_art.py

programs/experimental/asteroid_run_sheet.h: programs/experimental/asteroid_run_sheet.json programs/experimental/asteroid_run_player.png programs/experimental/asteroid_run_enemies.png programs/experimental/asteroid_run_tiles.png programs/experimental/asteroid_run_level.txt tools/arc1_assetc.py
	python3 tools/arc1_assetc.py --assets programs/experimental/asteroid_run_sheet.json --out-dir programs/experimental

experimental: $(GUEST_BUILD)/asteroid_run.arc1 $(GUEST_BUILD)/walkdemo.arc1 \
              $(GUEST_BUILD)/asset_demo.arc1 $(GUEST_BUILD)/map_demo.arc1
	@echo "experimental: built $^ (asteroid_run also has a make check test)"

$(GUEST_BUILD)/asteroid_run.elf: programs/experimental/asteroid_run.c programs/experimental/asteroid_run_sheet.h programs/asset.h programs/text.h programs/arcade1.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -Iprograms -T programs/link.ld -o $@ programs/crt0.S $<

$(GUEST_BUILD)/asteroid_run.bin: $(GUEST_BUILD)/asteroid_run.elf
	$(RISCV_OBJCOPY) -O binary $< $@

$(GUEST_BUILD)/asteroid_run.arc1: $(GUEST_BUILD)/asteroid_run.bin $(TESTDIR)/packarc1
	$(TESTDIR)/packarc1 $< $@

$(TESTDIR)/packarc1: tools/packarc1.c src/arcade1/rom.c src/arcade1/rom.h | $(TESTDIR)
	$(CC) $(CFLAGS) -o $@ tools/packarc1.c src/arcade1/rom.c

# walkdemo: the walking-spritesheet experiment (docs/experiments/walkdemo.md).
# Its art is generated by tools/gen_walk_sheet.py (PNG strip + assets.json)
# and the checked-in sheet header is arc1-assetc's output — refs and
# animation tables like any other game's (Wave 3). Building the guest
# itself needs no python; `make walkdemo-sheet` regenerates both halves.
walkdemo-sheet: programs/experimental/walkdemo_sheet.h

programs/experimental/walkdemo_sheet.h: tools/gen_walk_sheet.py tools/arc1_assetc.py
	python3 tools/gen_walk_sheet.py
	python3 tools/arc1_assetc.py --assets programs/experimental/walkdemo_sheet.json --out-dir programs/experimental

$(GUEST_BUILD)/walkdemo.elf: programs/experimental/walkdemo.c programs/experimental/walkdemo_sheet.h programs/asset.h programs/arcade1.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -Iprograms -T programs/link.ld -o $@ programs/crt0.S $<

$(GUEST_BUILD)/walkdemo.bin: $(GUEST_BUILD)/walkdemo.elf
	$(RISCV_OBJCOPY) -O binary $< $@

$(GUEST_BUILD)/walkdemo.arc1: $(GUEST_BUILD)/walkdemo.bin $(TESTDIR)/packarc1
	$(TESTDIR)/packarc1 $< $@

# asset_demo: the typed-accessor experiment (ROADMAP_CREATION_FLOW Wave 2 —
# programs/asset.h refs + arc1-assetc origin/collision metadata). Like
# walkdemo, its generated sheet header is checked in (regenerate with
# `make asset-demo-sheet`, which needs python3), so building the guest
# itself needs no python.
asset-demo-sheet: programs/experimental/asset_demo_sheet.h

programs/experimental/asset_demo_sheet.h: programs/experimental/asset_demo_sheet.json programs/experimental/asset_demo_hero.png tools/arc1_assetc.py
	python3 tools/arc1_assetc.py --assets programs/experimental/asset_demo_sheet.json --out-dir programs/experimental

$(GUEST_BUILD)/asset_demo.elf: programs/experimental/asset_demo.c programs/experimental/asset_demo_sheet.h programs/asset.h programs/arcade1.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -Iprograms -T programs/link.ld -o $@ programs/crt0.S $<

$(GUEST_BUILD)/asset_demo.bin: $(GUEST_BUILD)/asset_demo.elf
	$(RISCV_OBJCOPY) -O binary $< $@

$(GUEST_BUILD)/asset_demo.arc1: $(GUEST_BUILD)/asset_demo.bin $(TESTDIR)/packarc1
	$(TESTDIR)/packarc1 $< $@

# map_demo: the map-import experiment (ROADMAP_CREATION_FLOW Wave 4 +
# the tilemap-minsize retraction). Its tileset PNG is generated by
# tools/gen_map_tiles.py, the maps are hand-authored ASCII grids, and the
# checked-in header is arc1-assetc's output (deduped tiles + cells).
# `make mapdemo-sheet` regenerates both halves.
mapdemo-sheet: programs/experimental/mapdemo_tiles.h

programs/experimental/mapdemo_tiles.png: tools/gen_map_tiles.py
	python3 tools/gen_map_tiles.py

programs/experimental/mapdemo_tiles.h: programs/experimental/mapdemo_tiles.json programs/experimental/mapdemo_tiles.png tools/arc1_assetc.py
	python3 tools/arc1_assetc.py --assets programs/experimental/mapdemo_tiles.json --out-dir programs/experimental

$(GUEST_BUILD)/map_demo.elf: programs/experimental/map_demo.c programs/experimental/mapdemo_tiles.h programs/asset.h programs/arcade1.h programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -Iprograms -T programs/link.ld -o $@ programs/crt0.S $<

$(GUEST_BUILD)/map_demo.bin: $(GUEST_BUILD)/map_demo.elf
	$(RISCV_OBJCOPY) -O binary $< $@

$(GUEST_BUILD)/map_demo.arc1: $(GUEST_BUILD)/map_demo.bin $(TESTDIR)/packarc1
	$(TESTDIR)/packarc1 $< $@

# `arcade1 build` is self-contained: the guest SDK (crt0.S, link.ld and the
# headers) is compiled into the CLI as string literals in the generated
# $(SDK_DATA). programs/ stays the single source of truth — this rule
# regenerates the copy whenever it changes — but the built binary needs no
# checkout, no build/ tree and no packarc1 helper beside it.
# The embedded SDK `arcade1 build` unpacks in an empty directory. asset.h
# and text.h joined for Game #1 (Wave 5): the tenth-game metric requires
# `arcade1 build` to build asteroid_run.c, which includes both.
SDK_FILES  := crt0.S link.ld arcade1.h gfx.h arcade1_map.h asset.h text.h
SDK_DATA   := $(TESTDIR)/sdk_data.c

$(SDK_DATA): $(addprefix programs/,$(SDK_FILES)) tools/gen_sdk_data.awk | $(TESTDIR)
	@{ echo "/* GENERATED from programs/ by tools/gen_sdk_data.awk — do not edit. */"; \
	   echo '#include "sdk_data.h"'; \
	   for f in $(SDK_FILES); do \
	     echo "const char sdk_$$(printf '%s' $$f | tr . _ )[] ="; \
	     awk -f tools/gen_sdk_data.awk programs/$$f; \
	     echo '    ;'; \
	   done; } > $@.tmp && mv $@.tmp $@

$(CLI_BIN): $(CLI_SRC) tools/sdk_data.h $(SDK_DATA) tests/state_hash.h | $(TESTDIR)
	$(CC) $(CFLAGS) $(PLAY_CFLAGS) -Itests -Itools -o $@ $(CLI_SRC) $(SDK_DATA) $(PLAY_LIBS)

# Identical sources, no -DARCADE1_HAVE_SDL3 and no SDL libs: the CLI as it
# builds on a machine without SDL3 (used by tests/test-playwindow.sh to
# prove the --live absence path exits 3 and nothing else changes).
$(CLI_NOSDL): $(CLI_SRC) tools/sdk_data.h $(SDK_DATA) tests/state_hash.h | $(TESTDIR)
	$(CC) $(CFLAGS) -Itests -Itools -o $@ $(CLI_SRC) $(SDK_DATA)

# Build + run the milestone-10 guest-program harness: compile counter.c with
# RISC-V GCC, load its flat binary into the VM, execute it on the CPU, and
# assert the program set counter = 123. Links exec+cpu+memory (no .so).
test-program: $(GUEST_BUILD)/counter.bin
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_program tests/test_program.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/memory.c
	@ADDR=$$($(RISCV_NM) $(GUEST_BUILD)/counter.elf | awk '$$3=="counter"{print $$1}'); \
	  echo "counter @ 0x$$ADDR"; \
	  $(TESTDIR)/test_program $(GUEST_BUILD)/counter.bin 0x$$ADDR

# Build + run the milestone-11 MMIO harness: register a device on the bus's
# MMIO region, compile and load mmio_demo.c, execute it, and assert the guest
# read a host-provided register and wrote value+1 back. Links exec+cpu+memory
# (no .so).
test-mmio: $(GUEST_BUILD)/mmio_demo.bin
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_mmio tests/test_mmio.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/memory.c
	$(TESTDIR)/test_mmio $(GUEST_BUILD)/mmio_demo.bin

# Build and run the milestone-12 timer harness: drive real machine frames and
# check the timer's MMIO registers (host-level rw + a hand-assembled CPU lw),
# CTRL pause/resume, read-only COUNT, and reset-and-compare determinism. Links
# machine+timer+video+memory+exec+cpu (no libretro, no .so).
test-timer:
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_timer \
	    tests/test_timer.c src/arcade1/machine.c src/arcade1/timer.c \
	    src/arcade1/video.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_timer

# Build and run the milestone-13 video harness: run video_demo.c on the CPU so
# it issues CLEAR/SET_PIXEL/PRESENT over MMIO into the machine's scanout
# framebuffer, then check pixels, register readbacks, timer+video coexisting on
# the one muxed window, and reset-and-compare determinism. Links
# machine+video+timer+memory+exec+cpu (no libretro, no .so).
test-video: $(GUEST_BUILD)/video_demo.bin
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_video \
	    tests/test_video.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_video $(GUEST_BUILD)/video_demo.bin

# Build and run the M27-step-1 palette harness: drive the video device's
# PAL_INDEX/PAL_DATA registers directly over MMIO, verify the 256-entry
# palette, auto-increment + wrap, the power-on default set, read-no-increment,
# and that the existing 1-bit draw path is unaffected. No guest program.
test-video-palette: | $(TESTDIR)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_video_palette \
	    tests/test_video_palette.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_video_palette

# Build and run the M27-step-2 SPRITE4 harness: drive the SPRITE4 command over
# MMIO and verify the whole framebuffer against the documented 4-bit semantics
# (transparency, palette bank, flip X/Y, clipping + camera, even-width rule).
test-video-sprite4: | $(TESTDIR)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_video_sprite4 \
	    tests/test_video_sprite4.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_video_sprite4

# Build and run the M27-step-3 TILEMAP4 harness: drive the TILEMAP4 command over
# MMIO and verify the whole framebuffer against the documented semantics
# (per-cell palette bank + flips, empty cells, camera windowing, edge cells,
# even-width rule, PAL_BANK/FLAGS ignored).
test-video-tilemap4: | $(TESTDIR)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_video_tilemap4 \
	    tests/test_video_tilemap4.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_video_tilemap4

# Build and run the milestone-14 graphics-API harness: gfx_demo.c draws ONLY
# via gfx_clear/gfx_rect/gfx_present (programs/gfx.h), and the harness counts
# every framebuffer pixel to prove drawing, overlap order and clipping (incl.
# negative coordinates), plus reset-and-compare determinism. Links
# machine+video+timer+memory+exec+cpu (no libretro, no .so).
test-gfx: $(GUEST_BUILD)/gfx_demo.bin
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_gfx \
	    tests/test_gfx.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_gfx $(GUEST_BUILD)/gfx_demo.bin

# Build and run the milestone-19 sprite-engine harness: boot sprite_demo.c on
# the machine's own CPU, drive it once, and verify every sprite pixel exactly —
# MSB-first bit order, transparent clear bits, ROM vs RAM sources, right/bottom
# clipping, negative origins, and fully-off-screen/zero-size/illegal-source
# draws leaving nothing behind. Plus reset-and-compare determinism.
test-sprite: $(GUEST_BUILD)/sprite_demo.bin
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_sprite \
	    tests/test_sprite.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_sprite $(GUEST_BUILD)/sprite_demo.bin

# Build and run the milestone-20 tilemap-engine harness: boot tilemap_demo.c
# on the machine's own CPU, drive it once, and verify the whole framebuffer
# against an independent re-render of the documented semantics — multiple
# tiles, per-cell colour, empty cells, negative origin and right/bottom
# clipping, ROM-vs-RAM cells, an illegal cell source and zero-size guards
# drawing nothing — plus reset-and-compare determinism.
test-tilemap: $(GUEST_BUILD)/tilemap_demo.bin
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_tilemap \
	    tests/test_tilemap.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c
	$(TESTDIR)/test_tilemap $(GUEST_BUILD)/tilemap_demo.bin

# Build and run the milestone-21 camera-scrolling harness: boot scroll_demo.c
# on the machine's own CPU as firmware, sweep the camera across a map larger
# than the screen for 120 frames, and verify EVERY pixel of every frame against
# an independent full-visit model (reading the map cells straight out of guest
# RAM) — proving the camera pans world-space sprites/tilemaps, leaves the HUD
# fixed, and that the device's tilemap windowing is pixel-identical. Plus
# register readbacks and reset-and-compare determinism.
test-scroll: $(ARC1_ROM_DIR)/scroll_demo.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_scroll \
	    tests/test_scroll.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@CELLS=$$($(call nm_src,scroll_demo) | awk '$$3=="cells"{print $$1}'); \
	 echo "cells @ 0x$$CELLS"; \
	 $(TESTDIR)/test_scroll $(ARC1_ROM_DIR)/scroll_demo.arc1 0x$$CELLS

# Build and run the M27 colour harness: boot color_demo.c on the machine's own
# CPU, drive it for 120 frames, verify the palette/SPRITE4/TILEMAP4 result by
# property checks (HUD + both palette banks + a sprite colour), per-frame
# determinism, pinned final hashes, and dump a PPM to $TMPDIR.
test-color: $(ARC1_ROM_DIR)/color_demo.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_color \
	    tests/test_color.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	$(TESTDIR)/test_color $(ARC1_ROM_DIR)/color_demo.arc1

# Build and run the milestone-22 platformer harness: boot platformer.c on the
# machine's own CPU as firmware and drive a scripted RetroPad through the whole
# game story — spawn/fall/land under gravity, walk + coin + camera follow,
# jump/land, walk to the goal (WIN), restart — reading the game globals out of
# guest RAM, plus audio beeps, camera clamping and reset-and-compare
# determinism. Exercises tilemaps + sprites + scrolling + collision + animation
# + layers + input + audio together.
test-platformer: $(ARC1_ROM_DIR)/platformer.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_platformer \
	    tests/test_platformer.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SYMS=$$($(call nm_src,platformer) | \
	    awk '$$3=="px"{px=$$1} $$3=="py"{py=$$1} $$3=="onground"{og=$$1} \
	         $$3=="score"{sc=$$1} $$3=="state"{st=$$1} \
	         $$3=="cam_x"{cx=$$1} $$3=="cam_y"{cy=$$1} \
	         END{print "0x"px, "0x"py, "0x"og, "0x"sc, "0x"st, "0x"cx, "0x"cy}'); \
	 echo "syms: $$SYMS"; \
	 $(TESTDIR)/test_platformer $(ARC1_ROM_DIR)/platformer.arc1 $$SYMS

# Voxel-experiment P0 (research, see VOXEL_EXPERIMENT.md): graphics primitive
# micro-benchmark. Runs bench_gfx.c (one fixed workload per phase, 8 phases x
# 30 frames, phase picked by the guest from the frame timer) twice, records
# guest instructions retired and host time per phase, subtracts the baseline
# to report the all-in per-operation cost of SET_PIXEL / FILL / SPRITE /
# TILEMAP / CLEAR — the measured numbers that replace VOXEL_EXPERIMENT.md's
# estimates. Determinism asserted (per-frame hashes + final framebuffer).
test-bench: $(GUEST_BUILD)/bench_gfx.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_bench \
	    tests/test_bench.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@PH=$$($(RISCV_NM) $(GUEST_BUILD)/bench_gfx.elf | \
	    awk '$$3=="phase"{print $$1}'); \
	 echo "phase @ 0x$$PH"; \
	 $(TESTDIR)/test_bench $(GUEST_BUILD)/bench_gfx.arc1 0x$$PH

# Voxel-experiment P1 (research, see VOXEL_EXPERIMENT.md): deterministic
# terrain generator. Boots terrain_gen.c as firmware (one row of the 64x64
# grid per frame) and checks the result three ways: against an independent
# host-C model of the same spec (per-cell row 0 + full FNV-1a-64 digest),
# against pinned golden constants (so edits to BOTH copies still fail), and
# via reset-and-compare determinism (per-frame hashes + final framebuffer).
test-terrain: $(GUEST_BUILD)/terrain_gen.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_terrain \
	    tests/test_terrain.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SYMS=$$($(RISCV_NM) $(GUEST_BUILD)/terrain_gen.elf | \
	    awk '$$3=="state"{st=$$1} $$3=="digest_lo"{dl=$$1} \
	         $$3=="digest_hi"{dh=$$1} $$3=="sample"{sa=$$1} \
	         END{print "0x"st, "0x"dl, "0x"dh, "0x"sa}'); \
	 echo "syms: $$SYMS"; \
	 $(TESTDIR)/test_terrain $(GUEST_BUILD)/terrain_gen.arc1 $$SYMS

# Voxel-experiment P2 (research, see VOXEL_EXPERIMENT.md): first-person
# heightmap raycast. Boots raycast_demo.c as firmware (4 timer-driven phases:
# baseline, height-eval micro x2, full 80x60 raycast with camera pan) and
# measures per-phase guest instruction cost — pinning the height-evaluation
# number P1 handed off and the full raycast frame cost against the 1M frame
# cap. Checks the render is real (palette/variation property checks), plus
# reset-and-compare determinism and golden pins.
test-ray: $(GUEST_BUILD)/raycast_demo.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_ray \
	    tests/test_ray.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SYMS=$$($(RISCV_NM) $(GUEST_BUILD)/raycast_demo.elf | \
	    awk '$$3=="phase"{ph=$$1} $$3=="sink"{sk=$$1} \
	         END{print "0x"ph, "0x"sk}'); \
	 echo "syms: $$SYMS"; \
	 $(TESTDIR)/test_ray $(GUEST_BUILD)/raycast_demo.arc1 $$SYMS

# Voxel-experiment P3 (research, see VOXEL_EXPERIMENT.md): interaction —
# walk / dig / place over the delta overlay. Boots sandbox.c as firmware and
# drives a scripted pad (walk 30 cells, dig 3, place 2), checking gameplay
# truth (camx/digs/places/delta_count), collision truth vs an independent
# host model of ray_height, that edits are VISIBLE in the render (fb digest
# changes at fixed camera), a golden-pinned delta-table digest (pins the
# exact 5 edits), the linear-scan cost on top of P2's raycast, and
# reset-and-compare determinism.
test-sandbox: $(GUEST_BUILD)/sandbox.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_sandbox \
	    tests/test_sandbox.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SYMS=$$($(RISCV_NM) $(GUEST_BUILD)/sandbox.elf | \
	    awk '$$3=="camx"{cx=$$1} $$3=="digs"{dg=$$1} $$3=="places"{pl=$$1} \
	         $$3=="delta_count"{dc=$$1} $$3=="mode"{md=$$1} $$3=="py"{py=$$1} \
	         $$3=="tdigest"{td=$$1} $$3=="tgt1_x"{x1=$$1} $$3=="tgt1_z"{z1=$$1} \
	         $$3=="tgt2_x"{x2=$$1} $$3=="tgt2_z"{z2=$$1} \
	         END{print "0x"cx, "0x"dg, "0x"pl, "0x"dc, "0x"md, "0x"py, "0x"td, \
	               "0x"x1, "0x"z1, "0x"x2, "0x"z2}'); \
	 echo "syms: $$SYMS"; \
	 $(TESTDIR)/test_sandbox $(GUEST_BUILD)/sandbox.arc1 $$SYMS

# Voxel-experiment P4 (research, see VOXEL_EXPERIMENT.md): game systems —
# inventory (dig yields, place consumes), crafting (3 dirt -> 1 stone),
# patrolling sprite entities, day/night dithered TILEMAP overlay, HUD.
# Boots systems.c and runs a scripted scenario (dig, place, dig x3, craft,
# REJECTED place on empty inventory), checking counters, inventory truth,
# the replayed edit-model digest, tod timing, entity motion/visibility,
# collision, and reset-and-compare determinism.
test-systems: $(GUEST_BUILD)/systems.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_systems \
	    tests/test_systems.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SYMS=$$($(RISCV_NM) $(GUEST_BUILD)/systems.elf | \
	    awk '$$3=="camx"{cx=$$1} $$3=="digs"{dg=$$1} $$3=="places"{pl=$$1} \
	         $$3=="crafts"{cr=$$1} $$3=="inv_dirt"{di=$$1} \
	         $$3=="inv_stone"{st=$$1} $$3=="delta_count"{dc=$$1} \
	         $$3=="mode"{md=$$1} $$3=="py"{py=$$1} $$3=="tdigest"{td=$$1} \
	         $$3=="tod"{td2=$$1} $$3=="e1x"{a1=$$1} $$3=="e2x"{a2=$$1} \
	         $$3=="act_tx"{q1=$$1} $$3=="act_tz"{q2=$$1} \
	         $$3=="act_ok"{q3=$$1} $$3=="act_n"{q4=$$1} \
	         END{print "0x"cx, "0x"dg, "0x"pl, "0x"cr, "0x"di, "0x"st, \
	               "0x"dc, "0x"md, "0x"py, "0x"td, "0x"td2, "0x"a1, "0x"a2, \
	               "0x"q1, "0x"q2, "0x"q3, "0x"q4}'); \
	 echo "syms: $$SYMS"; \
	 $(TESTDIR)/test_systems $(GUEST_BUILD)/systems.arc1 $$SYMS

# Voxel-experiment P5 (research, see VOXEL_EXPERIMENT.md): persistence —
# input-log replay (bit-exact whole-run reproduction) and snapshot
# load-equals-replay (reboot + restore RAM/timer at frame 122, continue;
# every frame's RAM digest, the final framebuffer and the end-state
# symbols must match the recorded run). No guest changes: the save is the
# guest's globals + RAM, measured as save sizes in section 0.6.
test-persist: $(GUEST_BUILD)/systems.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_persist \
	    tests/test_persist.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SYMS=$$($(RISCV_NM) $(GUEST_BUILD)/systems.elf | \
	    awk '$$3=="camx"{cx=$$1} $$3=="digs"{dg=$$1} $$3=="places"{pl=$$1} \
	         $$3=="crafts"{cr=$$1} $$3=="inv_dirt"{di=$$1} \
	         $$3=="inv_stone"{st=$$1} $$3=="delta_count"{dc=$$1} \
	         $$3=="tdigest"{td=$$1} \
	         END{print "0x"cx, "0x"dg, "0x"pl, "0x"cr, "0x"di, "0x"st, \
	               "0x"dc, "0x"td}'); \
	 echo "syms: $$SYMS"; \
	 $(TESTDIR)/test_persist $(GUEST_BUILD)/systems.arc1 $$SYMS

# Build and run the SPEC section 8 "Frame boundaries" harness (BEFORE_M25
# task B): three hand-assembled guests pinning exactly what the reference does
# at the end of a frame — PRESENT ends it immediately (a marker after it runs
# only next frame), a parked guest re-ends on its first step, and a 2-instr
# loop runs exactly 1,000,000 steps with the next frame continuing the count.
# Hand-assembled: needs NO RISC-V toolchain and no guest image.
test-frame-rules: | $(TESTDIR)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_frame_rules \
	    tests/test_frame_rules.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	$(TESTDIR)/test_frame_rules

# Build and run the synthetic video worst-case harness (BEFORE_M25 task C
# step 3): five hand-assembled guests that drive the video device through the
# heaviest frames a guest can produce (1 and 100 full-screen CLEARs, a
# full-screen 20x15 TILEMAP, 128 overlapping 32x32 sprites, and the most
# commands that fit in the 1,000,000-step cap), reading the device's own
# end-of-frame counters and checking each against its arithmetic. Sizes a
# per-frame work budget from data. Hand-assembled: needs NO RISC-V toolchain.
test-video-worst: | $(TESTDIR)
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_video_worst \
	    tests/test_video_worst.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	$(TESTDIR)/test_video_worst

# Build and run the milestone-15 Breakout harness: boot breakout.c on the
# machine's own CPU as firmware, drive it frame-by-frame with a scripted pad,
# and check gameplay (serve -> launch -> brick hit -> lives -> game over ->
# restart), the input/audio/timer devices it uses, per-frame state hashes and
# reset-and-compare determinism. Links machine+video+timer+audio+memory+
# exec+cpu (no libretro, no .so). The four game globals are read straight out
# of guest RAM via addresses from the ELF's symbol table.
# Build and run the milestone-17 .arc1 game-image harness. Links rom.c +
# memory.c only (no libretro, no machine, no .so), proving the format code is
# frontend-independent. Covers CRC vectors, header round-trip, payload load,
# and every rejection (short/magic/version/size/crc).
test-rom:
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_rom \
	    tests/test_rom.c src/arcade1/rom.c src/arcade1/memory.c
	$(TESTDIR)/test_rom

test-breakout: $(ARC1_ROM_DIR)/breakout.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_breakout \
	    tests/test_breakout.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SCORE=$$($(call nm_src,breakout) | awk '$$3=="score"{print $$1}'); \
	 LIVES=$$($(call nm_src,breakout) | awk '$$3=="lives"{print $$1}'); \
	 STATE=$$($(call nm_src,breakout) | awk '$$3=="state"{print $$1}'); \
	 PADDLE=$$($(call nm_src,breakout) | awk '$$3=="paddle_x"{print $$1}'); \
	 echo "score @ 0x$$SCORE  lives @ 0x$$LIVES  state @ 0x$$STATE  paddle_x @ 0x$$PADDLE"; \
	 $(TESTDIR)/test_breakout $(ARC1_ROM_DIR)/breakout.arc1 \
	   0x$$SCORE 0x$$LIVES 0x$$STATE 0x$$PADDLE

# Build and run the milestone-18 Tetris harness against the guest firmware.
test-tetris: $(ARC1_ROM_DIR)/tetris.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_tetris \
	    tests/test_tetris.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SCORE=$$($(call nm_src,tetris) | awk '$$3=="score"{print $$1}'); \
	 LINES=$$($(call nm_src,tetris) | awk '$$3=="lines"{print $$1}'); \
	 LEVEL=$$($(call nm_src,tetris) | awk '$$3=="level"{print $$1}'); \
	 STATE=$$($(call nm_src,tetris) | awk '$$3=="state"{print $$1}'); \
	 LOCKED=$$($(call nm_src,tetris) | awk '$$3=="locked"{print $$1}'); \
	 FIELD=$$($(call nm_src,tetris) | awk '$$3=="field"{print $$1}'); \
	 CUR=$$($(call nm_src,tetris) | awk '$$3=="cur"{print $$1}'); \
	 NEXT=$$($(call nm_src,tetris) | awk '$$3=="next"{print $$1}'); \
	 ROT=$$($(call nm_src,tetris) | awk '$$3=="rot"{print $$1}'); \
	 CURX=$$($(call nm_src,tetris) | awk '$$3=="cur_x"{print $$1}'); \
	 CURY=$$($(call nm_src,tetris) | awk '$$3=="cur_y"{print $$1}'); \
	 PRNG=$$($(call nm_src,tetris) | awk '$$3=="prng"{print $$1}'); \
	 echo "score @ 0x$$SCORE  lines @ 0x$$LINES  state @ 0x$$STATE  locked @ 0x$$LOCKED"; \
	 $(TESTDIR)/test_tetris $(ARC1_ROM_DIR)/tetris.arc1 \
	   0x$$SCORE 0x$$LINES 0x$$LEVEL 0x$$STATE 0x$$LOCKED 0x$$FIELD \
	   0x$$CUR 0x$$NEXT 0x$$ROT 0x$$CURX 0x$$CURY 0x$$PRNG

# M29: `arcade1 build` must be self-contained — it carries its own SDK
# (tools/sdk_data.c) and needs no checkout, no programs/ dir and no
# packarc1 helper beside it. This test builds a game in an empty directory
# and runs the result.
test-build: $(CLI_BIN)
	sh tests/test_build.sh $(CLI_BIN)

# ROADMAP_V3 A1: `arcade1 build` writes <image>.syms beside the image;
# run/test/inspect auto-load it for --vars (build-id CRC check, header-less
# dumps still accepted). Standard three test arguments; only the CLI is
# used — the test builds in its own temp directory.
test-syms: $(CLI_BIN)
	sh tests/test_syms.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

# ROADMAP_V3 A3: PNG captures (--capture by extension, --format for
# capture-dir) and --scale. The python decoder validates every chunk CRC,
# so the hand-rolled CRC-32/Adler-32 are pinned by this test.
test-capture-png: $(CLI_BIN)
	sh tests/test_capture_png.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

# M29: draws whose source address falls outside ROM/RAM silently paint
# nothing on the device. The host counts them (arcade1_video_stats_t's
# skipped_ops), `arcade1 run` reports them on stderr, and --strict turns
# them into a failing exit. This test builds one guest that draws from
# 0x40000 and one that does not, and checks all three behaviours.
test-oob: $(CLI_BIN)
	sh tests/test_oob.sh $(CLI_BIN)

# --- AI SDK headless-runner test (milestone 28) ----------------------------
# Builds the `arcade1 run` CLI and drives it end-to-end: determinism (two
# identical runs byte-identical), golden faithfulness (its per-frame hashes
# match tests/golden/color.txt), PPM frame capture, scripted-input injection
# (a button script changes breakout's state hash, reproducibly) and exit codes.
# Needs the RISC-V toolchain only to (re)build the guest images it runs.
test-run: $(CLI_BIN) $(ARC1_ROM_DIR)/color_demo.arc1 $(ARC1_ROM_DIR)/breakout.arc1 | $(TESTDIR)
	sh tests/test_run.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

# The three example games: shell-script tests in the style of test_run.sh,
# asserting through named variables (--syms/--vars) and captured frames.
# M29's game as golden game 6: the record copy lives in docs/m29/game.c,
# this is the copy that builds and locks (tests/golden/m29game.txt).
test-m29game: $(CLI_BIN) $(ARC1_ROM_DIR)/m29game.arc1 | $(TESTDIR)
	sh tests/test_m29game.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

# Game #1 (ROADMAP_V3 Wave 5): asteroid_run's narrative — score rises,
# level clear, restart, game over, restart — through a committed input
# script; its frame lines are golden game 10's (test-golden greps them
# from this test's stdout).
test-asteroid_run: $(CLI_BIN) $(ARC1_ROM_DIR)/asteroid_run.arc1 | $(TESTDIR)
	sh tests/test_asteroid_run.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

test-pong: $(CLI_BIN) $(ARC1_ROM_DIR)/example_pong.arc1 | $(TESTDIR)
	sh tests/test_pong.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

test-snake: $(CLI_BIN) $(ARC1_ROM_DIR)/example_snake.arc1 | $(TESTDIR)
	sh tests/test_snake.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

test-example-color: $(CLI_BIN) $(ARC1_ROM_DIR)/example_color.arc1 | $(TESTDIR)
	sh tests/test_example_color.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

test-cheatsheet:
	sh tests/test_cheatsheet.sh $(CLI_BIN) $(ARC1_ROM_DIR) $(GOLDEN_DIR)

# --- Golden per-frame regression (Task A step 5) ----------------------------
# tests/golden/<game>.txt holds every "frame N hash=... fb=..." line from a
# golden run of the game against the committed images in tests/roms/.
# `make test-golden` re-runs that and diffs; on mismatch it prints the first
# differing frame. It needs ONLY the committed artifacts + the host compiler
# (verified with the RISC-V tools off PATH) — no cross toolchain.
# `make golden-update` regenerates the files; like roms-update it is MANUAL,
# and a golden change is a behaviour change that the commit message must
# explain (see docs.md "Golden regression policy").
GOLDEN_OUT := $(TESTDIR)/golden

# `make check` (Task A step 6): everything — every test target plus the
# golden per-frame comparison. Run this before pushing.
check: $(CHECK_TARGETS)
	@echo "check: all tests + test-golden passed"

test-golden: | $(TESTDIR)
	@mkdir -p $(GOLDEN_OUT)
	@fail=0; \
	for g in $(GOLDEN_GAMES); do \
	  $(MAKE) --no-print-directory ARC1_ROM_DIR=$(GOLDEN_ROMDIR) test-$$g \
	    > $(GOLDEN_OUT)/$$g.log 2>&1 || { \
	    echo "test-golden: $$g FAILED to run (see $(GOLDEN_OUT)/$$g.log):"; \
	    tail -5 $(GOLDEN_OUT)/$$g.log; fail=1; continue; }; \
	  grep '^frame ' $(GOLDEN_OUT)/$$g.log > $(GOLDEN_OUT)/$$g.txt; \
	  if diff -u $(GOLDEN_DIR)/$$g.txt $(GOLDEN_OUT)/$$g.txt \
	       > $(GOLDEN_OUT)/$$g.diff 2>&1; then \
	    echo "test-golden: $$g ok ($$(wc -l < $(GOLDEN_OUT)/$$g.txt) frames)"; \
	  else \
	    echo "test-golden: $$g DIFFERS — first differing frame:"; \
	    grep -m2 '^[-+]frame ' $(GOLDEN_OUT)/$$g.diff | sed 's/^/    /'; \
	    echo "    (full diff: $(GOLDEN_OUT)/$$g.diff)"; \
	    fail=1; \
	  fi; \
	done; \
	if [ $$fail -ne 0 ]; then \
	  echo "test-golden: FAILED — a golden change means a behaviour change."; \
	  echo "  If intended: make golden-update (+ roms-update) and say why in the commit."; \
	  exit 1; \
	fi; \
	echo "test-golden: all $(words $(GOLDEN_GAMES)) games match tests/golden/"

golden-update: | $(TESTDIR)
	@mkdir -p $(GOLDEN_DIR) $(GOLDEN_OUT)
	@for g in $(GOLDEN_GAMES); do \
	  $(MAKE) --no-print-directory ARC1_ROM_DIR=$(GOLDEN_ROMDIR) test-$$g \
	    > $(GOLDEN_OUT)/$$g.log 2>&1 || { \
	    echo "golden-update: $$g FAILED to run (see $(GOLDEN_OUT)/$$g.log)"; exit 1; }; \
	  grep '^frame ' $(GOLDEN_OUT)/$$g.log > $(GOLDEN_DIR)/$$g.txt; \
	  echo "golden-update: $(GOLDEN_DIR)/$$g.txt ($$(wc -l < $(GOLDEN_DIR)/$$g.txt) frames)"; \
	done
	@echo "goldens refreshed (commit deliberately, with the reason)"

# --- Golden ROM snapshot (Task A steps 3-4) ---------------------------------
# tests/roms/ holds the exact .arc1 images AND the `nm` symbol dumps
# (<name>.syms) the golden regression runs against (tests/golden/,
# `make test-golden`). They are a DELIBERATE, MANUAL snapshot: refresh them
# ONLY when a game's behaviour is meant to change, in the same commit as the
# golden update, and say in the commit message why (a golden change IS a
# behaviour change — see docs.md "Golden regression policy").
# This target needs the RISC-V cross-compiler; test-golden does not.
GOLDEN_IMGS   := breakout tetris platformer scroll_demo color_demo example_pong example_snake example_color \
                  m29game

roms-update: $(addprefix $(GUEST_BUILD)/,$(addsuffix .arc1,$(GOLDEN_IMGS)))
	mkdir -p $(GOLDEN_ROMDIR)
	cp $(addprefix $(GUEST_BUILD)/,$(addsuffix .arc1,$(GOLDEN_IMGS))) \
	   $(GOLDEN_ROMDIR)/
	@for g in $(GOLDEN_IMGS); do \
	   $(RISCV_NM) $(GUEST_BUILD)/$$g.elf > $(GOLDEN_ROMDIR)/$$g.syms || exit 1; \
	 done
	@echo "golden ROMs + symbol dumps refreshed in $(GOLDEN_ROMDIR)/ (commit deliberately)"

clean:
	rm -f $(TARGET) $(A64_TARGET) $(A32_TARGET)
	rm -rf $(GUEST_BUILD)
	rm -rf $(TESTDIR)
