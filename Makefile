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
test-tetris test-bench test-terrain: | $(TESTDIR)

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
GUEST_PROGS   := counter mmio_demo video_demo gfx_demo sprite_demo tilemap_demo scroll_demo platformer breakout tetris bench_gfx terrain_gen
GUEST_ELFS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .elf,$(GUEST_PROGS)))
GUEST_BINS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .bin,$(GUEST_PROGS)))
# Since M17 every guest is also packaged into a proper .arc1 game image.
GUEST_ARCS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .arc1,$(GUEST_PROGS)))

# Keep the ELFs (make would otherwise delete them as intermediates); the test
# reads symbols (e.g. counter's address) from them.
.SECONDARY: $(GUEST_ELFS)

.PHONY: all clean program android android-arm64 android-arm \
        test test-input test-square test-beep test-machine \
        test-memory test-cpu test-exec test-program test-mmio test-timer \
        test-video test-gfx test-rom test-sprite test-tilemap test-scroll \
        test-platformer test-breakout test-tetris test-bench test-terrain

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

$(TESTDIR)/packarc1: tools/packarc1.c src/arcade1/rom.c src/arcade1/rom.h | $(TESTDIR)
	$(CC) $(CFLAGS) -o $@ tools/packarc1.c src/arcade1/rom.c

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
test-scroll: $(GUEST_BUILD)/scroll_demo.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_scroll \
	    tests/test_scroll.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@CELLS=$$($(RISCV_NM) $(GUEST_BUILD)/scroll_demo.elf | awk '$$3=="cells"{print $$1}'); \
	 echo "cells @ 0x$$CELLS"; \
	 $(TESTDIR)/test_scroll $(GUEST_BUILD)/scroll_demo.arc1 0x$$CELLS

# Build and run the milestone-22 platformer harness: boot platformer.c on the
# machine's own CPU as firmware and drive a scripted RetroPad through the whole
# game story — spawn/fall/land under gravity, walk + coin + camera follow,
# jump/land, walk to the goal (WIN), restart — reading the game globals out of
# guest RAM, plus audio beeps, camera clamping and reset-and-compare
# determinism. Exercises tilemaps + sprites + scrolling + collision + animation
# + layers + input + audio together.
test-platformer: $(GUEST_BUILD)/platformer.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_platformer \
	    tests/test_platformer.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SYMS=$$($(RISCV_NM) $(GUEST_BUILD)/platformer.elf | \
	    awk '$$3=="px"{px=$$1} $$3=="py"{py=$$1} $$3=="onground"{og=$$1} \
	         $$3=="score"{sc=$$1} $$3=="state"{st=$$1} \
	         $$3=="cam_x"{cx=$$1} $$3=="cam_y"{cy=$$1} \
	         END{print "0x"px, "0x"py, "0x"og, "0x"sc, "0x"st, "0x"cx, "0x"cy}'); \
	 echo "syms: $$SYMS"; \
	 $(TESTDIR)/test_platformer $(GUEST_BUILD)/platformer.arc1 $$SYMS

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

test-breakout: $(GUEST_BUILD)/breakout.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_breakout \
	    tests/test_breakout.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SCORE=$$($(RISCV_NM) $(GUEST_BUILD)/breakout.elf | awk '$$3=="score"{print $$1}'); \
	 LIVES=$$($(RISCV_NM) $(GUEST_BUILD)/breakout.elf | awk '$$3=="lives"{print $$1}'); \
	 STATE=$$($(RISCV_NM) $(GUEST_BUILD)/breakout.elf | awk '$$3=="state"{print $$1}'); \
	 PADDLE=$$($(RISCV_NM) $(GUEST_BUILD)/breakout.elf | awk '$$3=="paddle_x"{print $$1}'); \
	 echo "score @ 0x$$SCORE  lives @ 0x$$LIVES  state @ 0x$$STATE  paddle_x @ 0x$$PADDLE"; \
	 $(TESTDIR)/test_breakout $(GUEST_BUILD)/breakout.arc1 \
	   0x$$SCORE 0x$$LIVES 0x$$STATE 0x$$PADDLE

# Build and run the milestone-18 Tetris harness against the guest firmware.
test-tetris: $(GUEST_BUILD)/tetris.arc1
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_tetris \
	    tests/test_tetris.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/audio.c src/arcade1/memory.c \
	    src/arcade1/exec.c src/arcade1/cpu.c src/arcade1/rom.c
	@SCORE=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="score"{print $$1}'); \
	 LINES=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="lines"{print $$1}'); \
	 LEVEL=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="level"{print $$1}'); \
	 STATE=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="state"{print $$1}'); \
	 LOCKED=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="locked"{print $$1}'); \
	 FIELD=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="field"{print $$1}'); \
	 CUR=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="cur"{print $$1}'); \
	 NEXT=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="next"{print $$1}'); \
	 ROT=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="rot"{print $$1}'); \
	 CURX=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="cur_x"{print $$1}'); \
	 CURY=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="cur_y"{print $$1}'); \
	 PRNG=$$($(RISCV_NM) $(GUEST_BUILD)/tetris.elf | awk '$$3=="prng"{print $$1}'); \
	 echo "score @ 0x$$SCORE  lines @ 0x$$LINES  state @ 0x$$STATE  locked @ 0x$$LOCKED"; \
	 $(TESTDIR)/test_tetris $(GUEST_BUILD)/tetris.arc1 \
	   0x$$SCORE 0x$$LINES 0x$$LEVEL 0x$$STATE 0x$$LOCKED 0x$$FIELD \
	   0x$$CUR 0x$$NEXT 0x$$ROT 0x$$CURX 0x$$CURY 0x$$PRNG

clean:
	rm -f $(TARGET) $(A64_TARGET) $(A32_TARGET)
	rm -rf $(GUEST_BUILD)
	rm -rf $(TESTDIR)
