# ARCADE-1 — build the Libretro core.
#
# Produces arcade1_libretro.so, loadable by RetroArch (or any libretro frontend).
# The machine (src/arcade1/) is frontend-independent; src/libretro/ is a thin
# adapter.

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
           src/libretro/libretro.c

# Test executables are built into this directory (gitignored) instead of /tmp.
TESTDIR := build

# Every test target builds into $(TESTDIR), so make sure the directory exists
# first. Order-only ("|") so its presence never affects rebuild decisions.
test test-input test-square test-beep test-machine test-memory test-cpu \
test-exec test-program test-mmio test-timer test-video: | $(TESTDIR)

$(TESTDIR):
	@mkdir -p $(TESTDIR)

# --- RISC-V guest toolchain (milestone 10) ---------------------------------
# Bare-metal RV32I: no M extension (added in M16), no libc, no SDK. Guest
# code is built to a flat binary and loaded into the VM's ROM.
RISCV_GCC     := riscv64-unknown-elf-gcc
RISCV_OBJCOPY := riscv64-unknown-elf-objcopy
RISCV_NM      := riscv64-unknown-elf-nm
GUEST_ARCH    := -march=rv32i -mabi=ilp32
GUEST_CFLAGS  := $(GUEST_ARCH) -Os -ffreestanding -nostdlib -nostartfiles \
                 -msmall-data-limit=0
GUEST_BUILD   := programs/build
GUEST_PROGS   := counter mmio_demo video_demo
GUEST_ELFS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .elf,$(GUEST_PROGS)))
GUEST_BINS    := $(addprefix $(GUEST_BUILD)/,$(addsuffix .bin,$(GUEST_PROGS)))

# Keep the ELFs (make would otherwise delete them as intermediates); the test
# reads symbols (e.g. counter's address) from them.
.SECONDARY: $(GUEST_ELFS)

.PHONY: all clean program test test-input test-square test-beep test-machine \
        test-memory test-cpu test-exec test-program test-mmio test-timer \
        test-video

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)

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
# sources (no libretro, no .so) — proving the machine is usable without
# RetroArch.
test-machine:
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_machine \
	    tests/test_machine.c src/arcade1/machine.c src/arcade1/memory.c \
	    src/arcade1/timer.c src/arcade1/video.c
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

# --- Guest programs: C -> RV32 ELF -> flat binary --------------------------
program: $(GUEST_BINS)

$(GUEST_BUILD)/%.elf: programs/%.c programs/crt0.S programs/link.ld
	@mkdir -p $(GUEST_BUILD)
	$(RISCV_GCC) $(GUEST_CFLAGS) -T programs/link.ld -o $@ programs/crt0.S $<

$(GUEST_BUILD)/%.bin: $(GUEST_BUILD)/%.elf
	$(RISCV_OBJCOPY) -O binary $< $@

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
	    src/arcade1/video.c src/arcade1/memory.c src/arcade1/exec.c \
	    src/arcade1/cpu.c
	$(TESTDIR)/test_timer

# Build and run the milestone-13 video harness: run video_demo.c on the CPU so
# it issues CLEAR/SET_PIXEL/PRESENT over MMIO into the machine's scanout
# framebuffer, then check pixels, register readbacks, timer+video coexisting on
# the one muxed window, and reset-and-compare determinism. Links
# machine+video+timer+memory+exec+cpu (no libretro, no .so).
test-video: $(GUEST_BUILD)/video_demo.bin
	$(CC) $(CFLAGS) -o $(TESTDIR)/test_video \
	    tests/test_video.c src/arcade1/machine.c src/arcade1/video.c \
	    src/arcade1/timer.c src/arcade1/memory.c src/arcade1/exec.c \
	    src/arcade1/cpu.c
	$(TESTDIR)/test_video $(GUEST_BUILD)/video_demo.bin

clean:
	rm -f $(TARGET)
	rm -rf $(GUEST_BUILD)
	rm -rf $(TESTDIR)
