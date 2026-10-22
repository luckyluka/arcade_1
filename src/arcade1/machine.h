/* ARCADE-1 machine — public interface.
 *
 * This is the actual machine, independent of any host frontend. It can be
 * driven directly (see tests/test_machine.c) or through an adapter such as
 * Libretro (src/libretro/libretro.c). Nothing here depends on RetroArch.
 */

#ifndef ARCADE1_MACHINE_H
#define ARCADE1_MACHINE_H

#include "audio.h"     /* arcade1_audio_dev_t (milestone 15) */
#include "exec.h"      /* arcade1_cpu_t (milestone 15: guest firmware) */
#include "memory.h"
#include "timer.h"
#include "video.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- Machine geometry (M25: pinned game resolution) ---------------------
 * 480x270 is 16:9 and scales exactly 4x to 1080p and 8x to 4K. Every other
 * size in the machine, Libretro adapter and tests derives from these two
 * macros. The shipped games and demos still draw in the top-left 320x240
 * region (their guest code is unchanged); the rest of the screen shows
 * whatever their CLEAR colour leaves. */

#ifndef ARCADE1_WIDTH
#define ARCADE1_WIDTH        480
#endif
#ifndef ARCADE1_HEIGHT
#define ARCADE1_HEIGHT       270
#endif
#define ARCADE1_FPS          60.0
#define ARCADE1_SAMPLE_RATE  48000.0

/* Integer forms, for exact per-frame audio sizing. */
#define ARCADE1_FPS_INT         60
#define ARCADE1_SAMPLE_RATE_INT 48000

/* Stereo frames produced per frame: 48000 / 60 = 800. */
#define ARCADE1_AUDIO_FRAMES (ARCADE1_SAMPLE_RATE_INT / ARCADE1_FPS_INT)

/* Framebuffer size in pixels (RGB565, 16-bit, row-major). */
#define ARCADE1_FB_PIXELS (ARCADE1_WIDTH * ARCADE1_HEIGHT)

/* Safety cap: guest instructions executed per frame while firmware is
 * attached (docs.md M15). A well-behaved guest presents its frame long
 * before this; the cap only prevents a runaway guest from hanging the host.
 *
 * Shared (not private to machine.c) so `arcade1 info` can print the value
 * the VM is actually compiled with — ROADMAP_V4 card 4.1 ("one source of
 * truth for machine constants"). Pure move, no behaviour change. */
#define ARCADE1_FRAME_STEP_CAP 1000000u

/* --- Input device (milestone 15) ----------------------------------------- */

/* INPUT_PAD register bits (docs.md "Firmware and frame protocol, pinned at
 * M15"). The input register at 0x200 is read-only and reflects the
 * arcade1_input_t the host set for the current frame. */
#define ARCADE1_INPUT_REG_BASE   0x200u
#define ARCADE1_INPUT_REG_PAD    0x200u
#define ARCADE1_INPUT_BIT_UP     0x1u
#define ARCADE1_INPUT_BIT_DOWN   0x2u
#define ARCADE1_INPUT_BIT_LEFT   0x4u
#define ARCADE1_INPUT_BIT_RIGHT  0x8u
#define ARCADE1_INPUT_BIT_A      0x10u
#define ARCADE1_INPUT_BIT_B      0x20u
#define ARCADE1_INPUT_BIT_START  0x40u
#define ARCADE1_INPUT_BIT_SELECT 0x80u

/* --- Abstract input device ---------------------------------------------- */

/* Controller state for one frame. The host fills this from whatever input
 * source it has (Libretro RetroPad, a test harness, a future SDK call, ...)
 * and hands it to arcade1_set_input(). The machine never touches host input
 * devices directly. */
typedef struct arcade1_input {
    bool up, down, left, right;   /* D-pad   */
    bool a, b;                    /* action  */
    bool start, select;           /* system  */
} arcade1_input_t;

/* --- Machine ------------------------------------------------------------- */

typedef struct arcade1_machine {
    /* Output, valid after arcade1_run_frame(). */
    uint16_t framebuffer[ARCADE1_FB_PIXELS];    /* RGB565, row-major.       */
    int16_t  audio[ARCADE1_AUDIO_FRAMES * 2];   /* interleaved stereo L,R.  */

    /* Memory bus (ROM + RAM + MMIO). The CPU accesses it via
     * arcade1_read32()/arcade1_write32(); see memory.h. The MMIO window is
     * serviced by one muxed handler: timer below (0x000..), video (0x100..). */
    arcade1_bus_t bus;

    /* Timer device (M12): ticks once per frame while enabled and is exposed
     * to the guest as two MMIO registers (see docs.md register table). */
    arcade1_timer_t timer;

    /* Video device (M13): guest-driven CLEAR/SET_PIXEL/PRESENT drawing into
     * the framebuffer above (see docs.md register table). */
    arcade1_video_t video;

    /* Audio device (M15): guest-driven tone registers; the machine renders
     * the waveform from this state every frame (see docs.md register table). */
    arcade1_audio_dev_t audio_dev;

    /* Guest CPU (M15): idle until firmware is attached via
     * arcade1_set_firmware; then it runs once per arcade1_run_frame. */
    arcade1_cpu_t cpu;
    bool firmware;

    /* Input for the current frame (set via arcade1_set_input). Readable by
     * the guest through the read-only INPUT_PAD register (M15). */
    arcade1_input_t input;

    /* Internal machine state. */
    uint32_t frame_counter;
    uint32_t beep_phase;
    int      square_x, square_y;
} arcade1_machine_t;

/* Zero the machine (call once before arcade1_reset). */
void arcade1_init(arcade1_machine_t *m);

/* Reset to power-on state: square centred, counters cleared, CPU reset.
 * ROM contents and the firmware flag persist: with firmware attached the
 * guest reboots from the reset vector on the next frame. */
void arcade1_reset(arcade1_machine_t *m);

/* Attach or detach guest firmware (M15). Load the ROM first
 * (arcade1_load_rom(&m->bus, ...)); enabling firmware resets the CPU so the
 * guest boots from the reset vector on the next arcade1_run_frame. While
 * firmware is attached the host demo (test pattern / square / overlay) does
 * not render — the guest owns the frame. */
void arcade1_set_firmware(arcade1_machine_t *m, bool enabled);

/* Set the controller state for the next frame. */
void arcade1_set_input(arcade1_machine_t *m, const arcade1_input_t *in);

/* Advance one frame: run the guest (if firmware attached) until it presents
 * a frame, then generate audio from the audio device state. */
void arcade1_run_frame(arcade1_machine_t *m);

/* --- Output accessors ---------------------------------------------------- */

/* RGB565 framebuffer, ARCADE1_WIDTH x ARCADE1_HEIGHT, row-major. */
const uint16_t *arcade1_framebuffer(const arcade1_machine_t *m);

/* Framebuffer pitch in bytes (row stride). */
unsigned arcade1_framebuffer_pitch(const arcade1_machine_t *m);

/* Interleaved stereo audio for the last frame (L,R pairs). */
const int16_t *arcade1_audio(const arcade1_machine_t *m);

/* Number of stereo frames in the audio buffer. */
unsigned arcade1_audio_frames(const arcade1_machine_t *m);

#endif /* ARCADE1_MACHINE_H */
