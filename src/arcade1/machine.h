/* ARCADE-1 machine — public interface.
 *
 * This is the actual machine, independent of any host frontend. It can be
 * driven directly (see tests/test_machine.c) or through an adapter such as
 * Libretro (src/libretro/libretro.c). Nothing here depends on RetroArch.
 */

#ifndef ARCADE1_MACHINE_H
#define ARCADE1_MACHINE_H

#include "memory.h"
#include "timer.h"
#include "video.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- Machine geometry (temporary placeholder values) -------------------- */

#define ARCADE1_WIDTH        320
#define ARCADE1_HEIGHT       240
#define ARCADE1_FPS          60.0
#define ARCADE1_SAMPLE_RATE  48000.0

/* Integer forms, for exact per-frame audio sizing. */
#define ARCADE1_FPS_INT         60
#define ARCADE1_SAMPLE_RATE_INT 48000

/* Stereo frames produced per frame: 48000 / 60 = 800. */
#define ARCADE1_AUDIO_FRAMES (ARCADE1_SAMPLE_RATE_INT / ARCADE1_FPS_INT)

/* Framebuffer size in pixels (RGB565, 16-bit, row-major). */
#define ARCADE1_FB_PIXELS (ARCADE1_WIDTH * ARCADE1_HEIGHT)

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

    /* Input for the current frame (set via arcade1_set_input). */
    arcade1_input_t input;

    /* Internal machine state. */
    uint32_t frame_counter;
    uint32_t beep_phase;
    int      square_x, square_y;
} arcade1_machine_t;

/* Zero the machine (call once before arcade1_reset). */
void arcade1_init(arcade1_machine_t *m);

/* Reset to power-on state: square centred, counters cleared. */
void arcade1_reset(arcade1_machine_t *m);

/* Set the controller state for the next frame. */
void arcade1_set_input(arcade1_machine_t *m, const arcade1_input_t *in);

/* Advance one frame: update state, render video, generate audio. */
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
