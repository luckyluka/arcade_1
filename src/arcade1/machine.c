/* ARCADE-1 machine — implementation.
 *
 * Self-contained machine logic: RGB565 software framebuffer, test pattern,
 * RetroPad indicator overlay, D-pad moving square, a 440 Hz beep, and the
 * timer (M12) + video (M13) MMIO devices. This file has NO dependency on
 * libretro or any host frontend; it is driven purely through the arcade1_*
 * API in machine.h.
 */

#include "machine.h"

#include <string.h>

/* --- Square -------------------------------------------------------------- */

#define SQUARE_SIZE  32
#define SQUARE_SPEED 2

/* --- Beep ---------------------------------------------------------------- */

#define BEEP_FREQ_HZ 440
#define BEEP_AMP     8000

/* Per-sample phase step for BEEP_FREQ_HZ at ARCADE1_SAMPLE_RATE. The full
 * cycle maps to 2^32; the top bit selects the square-wave polarity. */
#define BEEP_STEP \
    ((uint32_t)(((uint64_t)BEEP_FREQ_HZ << 32) / \
                (uint64_t)ARCADE1_SAMPLE_RATE_INT))

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

static void audio_render(arcade1_machine_t *m)
{
    for (unsigned i = 0; i < ARCADE1_AUDIO_FRAMES; i++) {
        int16_t s = (m->beep_phase & 0x80000000u) ? (int16_t)-BEEP_AMP
                                                  : (int16_t) BEEP_AMP;
        m->audio[i * 2]     = s;    /* Left  */
        m->audio[i * 2 + 1] = s;    /* Right */
        m->beep_phase += BEEP_STEP;
    }
}

/* --- MMIO dispatch (the window's single M11 handler) --------------------- *
 * The bus routes the whole 0x4000 window to one handler, so the machine muxes
 * it: 0x000..0x0FF is the timer, 0x100.. is the video device. Offsets that no
 * device claims read 0 / ignore writes — still not stray accesses, because the
 * handler itself is attached (docs.md "MMIO decisions"). */

static uint32_t machine_mmio_read(void *ctx, uint32_t offset)
{
    arcade1_machine_t *m = ctx;

    if (offset < ARCADE1_VIDEO_REG_BASE)
        return arcade1_timer_mmio_read(&m->timer, offset);
    return arcade1_video_mmio_read(&m->video, offset);
}

static void machine_mmio_write(void *ctx, uint32_t offset, uint32_t value)
{
    arcade1_machine_t *m = ctx;

    if (offset < ARCADE1_VIDEO_REG_BASE)
        arcade1_timer_mmio_write(&m->timer, offset, value);
    else
        arcade1_video_mmio_write(&m->video, offset, value);
}

/* --- Public API ---------------------------------------------------------- */

void arcade1_init(arcade1_machine_t *m)
{
    memset(m, 0, sizeof(*m));
    arcade1_bus_init(&m->bus);
    arcade1_timer_reset(&m->timer);
    arcade1_video_reset(&m->video);
    /* The video device borrows the machine's scanout buffer (set once here,
     * kept across resets). */
    m->video.framebuffer = m->framebuffer;
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

    /* RAM is volatile; a reset clears it but preserves any loaded ROM and the
     * devices' MMIO attachment. */
    arcade1_bus_reset(&m->bus);
}

void arcade1_set_input(arcade1_machine_t *m, const arcade1_input_t *in)
{
    m->input = *in;
}

void arcade1_run_frame(arcade1_machine_t *m)
{
    /* Update state from input (input -> state). */
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

    /* Render video (state -> rendering). */
    render_test_pattern(m);
    draw_pad_overlay(m);
    draw_square(m);

    /* Generate audio. */
    audio_render(m);

    m->frame_counter++;

    /* Frame time -> timer: exactly one tick per completed frame while the
     * timer is enabled (M12). Never wall-clock: the count is a pure function
     * of frames executed, so it is identical across hosts. */
    arcade1_timer_tick(&m->timer);
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
