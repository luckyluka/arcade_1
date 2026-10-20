/* ARCADE-1 machine — implementation.
 *
 * Self-contained machine logic: RGB565 software framebuffer, test pattern,
 * RetroPad indicator overlay, D-pad moving square, and the timer (M12),
 * video (M13) and audio (M15) MMIO devices. From milestone 15 the machine
 * also owns the guest CPU: when firmware is attached, arcade1_run_frame runs
 * guest code once per frame (until the guest presents its frame) instead of
 * rendering the host demo. This file has NO dependency on libretro or any
 * host frontend; it is driven purely through the arcade1_* API in machine.h.
 */

#include "machine.h"

#include <string.h>

/* --- Square -------------------------------------------------------------- */

#define SQUARE_SIZE  32
#define SQUARE_SPEED 2

/* --- Beep ---------------------------------------------------------------- */

/* Amplitude of the square wave generated from the audio device's frequency
 * and enable state (milestone 15). The frequency itself is guest-writable
 * (AUDIO_FREQ); the power-on default 440 Hz reproduces the M05 demo beep. */
#define BEEP_AMP 8000

/* Safety cap: guest instructions executed per frame while firmware is
 * attached (docs.md M15). A well-behaved guest presents its frame long
 * before this; the cap only prevents a runaway guest from hanging the host.
 * The value lives in machine.h (ARCADE1_FRAME_STEP_CAP) so the CLI's
 * `arcade1 info` reports the very same constant the VM runs with. */

/* --- Pixel helpers ------------------------------------------------------- */

static inline uint16_t pack_rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0x1Fu) << 11) |
                      ((g & 0x3Fu) << 5)  |
                      ( b & 0x1Fu));
}

/* Fill a solid rectangle in the framebuffer, clipped to the screen. */
static void fill_rect(arcade1_machine_t *m, int x, int y, int w, int h,
                      uint16_t color)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= ARCADE1_HEIGHT)
            continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= ARCADE1_WIDTH)
                continue;
            m->framebuffer[yy * ARCADE1_WIDTH + xx] = color;
        }
    }
}

/* --- Test pattern -------------------------------------------------------- */

/* Render one deterministic test frame into the framebuffer.
 *
 *   Top third    : 8 solid color bars.
 *   Middle third : horizontal grayscale gradient (black -> white).
 *   Bottom third : vertical test bars scrolling by frame_counter.
 */
static void render_test_pattern(arcade1_machine_t *m)
{
    static const uint8_t bars[8][3] = {
        { 31, 63, 31 },  /* white   */
        { 31, 63,  0 },  /* yellow  */
        {  0, 63, 31 },  /* cyan    */
        {  0, 63,  0 },  /* green   */
        { 31,  0, 31 },  /* magenta */
        { 31,  0,  0 },  /* red     */
        {  0,  0, 31 },  /* blue    */
        {  0,  0,  0 },  /* black   */
    };

    const unsigned band_h = ARCADE1_HEIGHT / 3;
    const unsigned scroll = m->frame_counter % ARCADE1_WIDTH;

    for (unsigned y = 0; y < ARCADE1_HEIGHT; y++) {
        uint16_t *row = &m->framebuffer[y * ARCADE1_WIDTH];

        if (y < band_h) {
            for (unsigned x = 0; x < ARCADE1_WIDTH; x++) {
                unsigned band = (x * 8) / ARCADE1_WIDTH;
                row[x] = pack_rgb565(bars[band][0], bars[band][1],
                                     bars[band][2]);
            }
        } else if (y < 2 * band_h) {
            for (unsigned x = 0; x < ARCADE1_WIDTH; x++) {
                unsigned level = (x * 31) / (ARCADE1_WIDTH - 1);
                row[x] = pack_rgb565(level, (level * 63) / 31, level);
            }
        } else {
            for (unsigned x = 0; x < ARCADE1_WIDTH; x++) {
                unsigned sx = (x + scroll) % ARCADE1_WIDTH;
                unsigned on = ((sx / 20) & 1u) ? 31u : 0u;
                row[x] = pack_rgb565(on, on, on);
            }
        }
    }
}

/* --- Pad indicator overlay ----------------------------------------------- */

/* Draw one indicator box per button, centred in the bottom band. Lights up
 * when the corresponding arcade1_input_t flag is held. */
static void draw_pad_overlay(arcade1_machine_t *m)
{
    /* Box order must match the arcade1_input_t field order below. */
    static const uint8_t colors[8][3] = {
        { 31, 63, 31 },  /* up:    green  */
        { 31, 63,  0 },  /* down:  yellow */
        {  0, 63, 31 },  /* left:  cyan   */
        { 31,  0, 31 },  /* right: magenta*/
        { 31,  0,  0 },  /* a:     red    */
        {  0,  0, 31 },  /* b:     blue   */
        { 31, 63, 31 },  /* start: white  */
        { 31, 63,  0 },  /* select:yellow */
    };
    const bool held[8] = {
        m->input.up, m->input.down, m->input.left, m->input.right,
        m->input.a,  m->input.b,    m->input.start, m->input.select,
    };

    const int box   = 24;
    const int gap   = 8;
    const int total = 8 * box + 7 * gap;
    const int x0    = (ARCADE1_WIDTH - total) / 2;
    const int y0    = 2 * (ARCADE1_HEIGHT / 3) + 16;

    for (int i = 0; i < 8; i++) {
        int x = x0 + i * (box + gap);
        fill_rect(m, x, y0, box, box, pack_rgb565(4, 8, 4));
        if (held[i])
            fill_rect(m, x + 4, y0 + 4, box - 8, box - 8,
                      pack_rgb565(colors[i][0], colors[i][1], colors[i][2]));
    }
}

/* --- Moving square ------------------------------------------------------- */

/* Orange unique to the square, so tests can locate it by scanning. */
static const uint16_t SQUARE_COLOR = 0xFD00; /* pack_rgb565(31,40,0) */

static void draw_square(arcade1_machine_t *m)
{
    fill_rect(m, m->square_x, m->square_y, SQUARE_SIZE, SQUARE_SIZE,
              SQUARE_COLOR);
}

/* --- Beep ---------------------------------------------------------------- */

/* Render this frame's audio from the audio device state (M15): a square wave
 * at AUDIO_FREQ Hz while AUDIO_CTRL enable is set, silence otherwise. The
 * full cycle maps to 2^32; the top bit selects the polarity. Frequency 0
 * produces a constant level (use the enable bit for silence). Called once
 * per frame AFTER the guest has run, so a register write at frame N shapes
 * frame N's samples. */
static void audio_render(arcade1_machine_t *m)
{
    const uint32_t freq = m->audio_dev.freq;
    const uint32_t step =
        (uint32_t)(((uint64_t)freq << 32) / (uint64_t)ARCADE1_SAMPLE_RATE_INT);
    const bool on = (m->audio_dev.ctrl & ARCADE1_AUDIO_CTRL_ENABLE) != 0;

    for (unsigned i = 0; i < ARCADE1_AUDIO_FRAMES; i++) {
        int16_t s = 0;
        if (on)
            s = (m->beep_phase & 0x80000000u) ? (int16_t)-BEEP_AMP
                                              : (int16_t) BEEP_AMP;
        m->audio[i * 2]     = s;    /* Left  */
        m->audio[i * 2 + 1] = s;    /* Right */
        m->beep_phase += step;
    }
}

/* --- MMIO dispatch (the window's single M11 handler) --------------------- *
 * The bus routes the whole MMIO window to one handler, so the machine muxes
 * it: 0x000..0x0FF is the timer, 0x100..0x1FF the video device, 0x200..0x2FF
 * the input device (read-only pad), 0x300..0x3FF the audio device. Offsets
 * that no device claims read 0 / ignore writes — still not stray accesses,
 * because the handler itself is attached (docs.md "MMIO decisions"). */

static uint32_t machine_mmio_read(void *ctx, uint32_t offset)
{
    arcade1_machine_t *m = ctx;

    if (offset < ARCADE1_VIDEO_REG_BASE)
        return arcade1_timer_mmio_read(&m->timer, offset);
    if (offset < ARCADE1_INPUT_REG_BASE)
        return arcade1_video_mmio_read(&m->video, offset);
    if (offset < ARCADE1_AUDIO_REG_BASE) {
        /* INPUT_PAD: read-only view of the host's pad for this frame. */
        const arcade1_input_t *in = &m->input;
        return ((uint32_t)in->up     << 0) |
               ((uint32_t)in->down   << 1) |
               ((uint32_t)in->left   << 2) |
               ((uint32_t)in->right  << 3) |
               ((uint32_t)in->a      << 4) |
               ((uint32_t)in->b      << 5) |
               ((uint32_t)in->start  << 6) |
               ((uint32_t)in->select << 7);
    }
    return arcade1_audio_mmio_read(&m->audio_dev, offset);
}

static void machine_mmio_write(void *ctx, uint32_t offset, uint32_t value)
{
    arcade1_machine_t *m = ctx;

    if (offset < ARCADE1_VIDEO_REG_BASE)
        arcade1_timer_mmio_write(&m->timer, offset, value);
    else if (offset < ARCADE1_INPUT_REG_BASE)
        arcade1_video_mmio_write(&m->video, offset, value);
    else if (offset < ARCADE1_AUDIO_REG_BASE)
        ;   /* input registers are read-only */
    else
        arcade1_audio_mmio_write(&m->audio_dev, offset, value);
}

/* --- Public API ---------------------------------------------------------- */

void arcade1_init(arcade1_machine_t *m)
{
    memset(m, 0, sizeof(*m));
    arcade1_bus_init(&m->bus);
    arcade1_timer_reset(&m->timer);
    arcade1_video_reset(&m->video);
    arcade1_audio_reset(&m->audio_dev);
    /* The video device borrows the machine's scanout buffer and the guest
     * ROM/RAM (set once here, kept across resets). ROM/RAM are the only
     * legal sources for SPRITE image fetches. */
    m->video.framebuffer = m->framebuffer;
    m->video.rom         = m->bus.rom;
    m->video.ram         = m->bus.ram;
    /* B2 (ROADMAP_V3 §5): expose the PC of the executing store to the video
     * hook. It is the STORE's own PC because exec.c commits cpu->pc only
     * after an instruction retires, so during a command write cpu->pc still
     * points at that store. Host-side wiring only; kept across resets. */
    m->video.store_pc    = &m->cpu.pc;
    /* Attach the machine's muxed handler to the MMIO window (the M11 handler
     * mechanism). arcade1_bus_reset() keeps this attachment, so a reset
     * restarts the devices without re-registering anything. */
    arcade1_set_mmio(&m->bus, machine_mmio_read, machine_mmio_write, m);
}

void arcade1_reset(arcade1_machine_t *m)
{
    m->frame_counter = 0;
    m->beep_phase    = 0;
    m->square_x      = (ARCADE1_WIDTH  - SQUARE_SIZE) / 2;
    m->square_y      = (ARCADE1_HEIGHT - SQUARE_SIZE) / 2;

    /* Power-on video state: a black screen with no guest commands pending.
     * (Everything rendered by arcade1_run_frame() is regenerated from scratch
     * each frame, so this only matters for guest-drawn pixels.) */
    memset(m->framebuffer, 0, sizeof(m->framebuffer));
    arcade1_video_reset(&m->video);

    /* The timer restarts with the machine (count 0, enabled). */
    arcade1_timer_reset(&m->timer);

    /* Audio returns to the power-on tone (440 Hz, enabled): a machine with
     * no guest firmware plays the demo beep again after a reset (M05). */
    arcade1_audio_reset(&m->audio_dev);

    /* The guest CPU reboots from the reset vector. With firmware attached the
     * next frame re-runs crt0/main from the top (a deterministic cold boot). */
    arcade1_cpu_reset(&m->cpu);

    /* RAM is volatile; a reset clears it but preserves any loaded ROM and the
     * devices' MMIO attachment. The firmware flag persists: a machine with
     * attached firmware stays a firmware machine across reset. */
    arcade1_bus_reset(&m->bus);
}

void arcade1_set_firmware(arcade1_machine_t *m, bool enabled)
{
    m->firmware = enabled;
    /* Boot from the reset vector: the next arcade1_run_frame starts executing
     * at 0x00000000 (crt0), so enabling is equivalent to a cold boot. */
    arcade1_cpu_reset(&m->cpu);
}

/* Run the guest for one frame (M15): execute instructions until the guest
 * presents its frame (a PRESENT write through gfx_present), until the CPU
 * parks (PC stops changing, as crt0's `j .` does after main returns), or
 * until the safety cap. This makes one guest loop iteration equal exactly one
 * host frame, deterministically: game speed is locked to 60 fps. */
static void run_firmware(arcade1_machine_t *m)
{
    const uint32_t presents_before = m->video.presents;
    uint32_t prev_pc = 0xFFFFFFFFu;   /* != any PC: first step always runs */
    uint32_t steps   = 0;

    while (m->cpu.pc != prev_pc && steps < ARCADE1_FRAME_STEP_CAP) {
        prev_pc = m->cpu.pc;
        arcade1_cpu_step(&m->cpu, &m->bus);
        steps++;
        if (m->video.presents != presents_before)
            return;                   /* frame boundary: guest presented */
    }
}

void arcade1_set_input(arcade1_machine_t *m, const arcade1_input_t *in)
{
    m->input = *in;
}

void arcade1_run_frame(arcade1_machine_t *m)
{
    /* Task C (DEVICE_BUDGET.md): per-frame video work counters. Reset at the
     * frame start, snapshotted at the frame end, so frame_totals always holds
     * the frame that just finished. Measurement only — no behaviour change. */
    arcade1_video_stats_begin_frame(&m->video);
    if (m->firmware) {
        /* Guest owns the frame (M15): it reads INPUT_PAD, updates its state,
         * draws through the video device and presents with gfx_present. The
         * host demo does not render while firmware is attached. */
        run_firmware(m);
    } else {
        /* Host demo (M02-M05): update state from input, then render. */
        if (m->input.left)  m->square_x -= SQUARE_SPEED;
        if (m->input.right) m->square_x += SQUARE_SPEED;
        if (m->input.up)    m->square_y -= SQUARE_SPEED;
        if (m->input.down)  m->square_y += SQUARE_SPEED;

        if (m->square_x < 0)                              m->square_x = 0;
        if (m->square_y < 0)                              m->square_y = 0;
        if (m->square_x > ARCADE1_WIDTH  - SQUARE_SIZE)
            m->square_x = ARCADE1_WIDTH  - SQUARE_SIZE;
        if (m->square_y > ARCADE1_HEIGHT - SQUARE_SIZE)
            m->square_y = ARCADE1_HEIGHT - SQUARE_SIZE;

        render_test_pattern(m);
        draw_pad_overlay(m);
        draw_square(m);
    }

    /* Generate audio (M15: from the audio device state, both modes). */
    audio_render(m);

    m->frame_counter++;

    /* Frame time -> timer: exactly one tick per completed frame while the
     * timer is enabled (M12). Never wall-clock: the count is a pure function
     * of frames executed, so it is identical across hosts. */
    arcade1_timer_tick(&m->timer);

    /* Task C: freeze this frame's video work for readers (DEVICE_BUDGET.md). */
    arcade1_video_stats_end_frame(&m->video);
}

const uint16_t *arcade1_framebuffer(const arcade1_machine_t *m)
{
    return m->framebuffer;
}

unsigned arcade1_framebuffer_pitch(const arcade1_machine_t *m)
{
    (void)m;
    return ARCADE1_WIDTH * (unsigned)sizeof(uint16_t);
}

const int16_t *arcade1_audio(const arcade1_machine_t *m)
{
    return m->audio;
}

unsigned arcade1_audio_frames(const arcade1_machine_t *m)
{
    (void)m;
    return ARCADE1_AUDIO_FRAMES;
}
