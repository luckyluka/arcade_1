/* ARCADE-1 guest: TETRIS (milestone 18).
 *
 * The second real game, built to stress the M14 graphics API the way
 * docs.md M18 asks: grid-based graphics (a 10x20 playfield drawn cell by
 * cell), repeated objects (7 tetrominoes from one shape table, rotated by
 * math), text (3x5 font labels + digits), deterministic timing (gravity
 * and input repeat driven by the M12 timer), and game state (score /
 * lines / level, next-piece preview, game over + restart).
 *
 * No sprite hardware, no new devices — only gfx.h + the timer/input/audio
 * registers (docs.md M18: "observe what is missing"). Observations are
 * written down in docs.md at the end of the milestone.
 *
 * Determinism: there is no entropy source in the machine, so the piece
 * sequence comes from a fixed-seed xorshift32 PRNG (seed below). Two
 * identical runs produce identical games — the test asserts exactly that
 * (per-frame state hashes + framebuffer compare), and pins the first
 * pieces (L O L Z T ...) as a known sequence.
 *
 * Controls: left/right move (repeat), A rotates, down soft-drops,
 * START restarts after game over.
 */

#include "gfx.h"

/* --- MMIO registers (fixed map, see docs.md table) ----------------------- */

#define MMIO_BASE       ARCADE1_MMIO_BASE
#define REG_TIMER_COUNT (MMIO_BASE + 0x00u)  /* M12: ticks once per frame   */
#define REG_INPUT_PAD   (MMIO_BASE + 0x200u) /* M15: read-only pad          */
#define REG_AUDIO_FREQ  (MMIO_BASE + 0x300u) /* M15: tone in Hz             */
#define REG_AUDIO_CTRL  (MMIO_BASE + 0x304u) /* M15: bit0 = enable          */

#define PAD_DOWN   0x02u
#define PAD_LEFT   0x04u
#define PAD_RIGHT  0x08u
#define PAD_A      0x10u
#define PAD_START  0x40u

/* --- Geometry ------------------------------------------------------------- */

#define W 320
#define H 240

#define COLS     10
#define ROWS     20
#define CELL     10            /* playfield cell pitch (9px block + 1 gap)  */
#define PX       10            /* playfield top-left on screen              */
#define PY       10

#define PANEL_X  130           /* right-hand panel: labels, score, preview  */

#define PV_X     150           /* next-piece preview box (top-left)         */
#define PV_Y     132
#define PV_CELL  6             /* preview cell pitch                        */

/* --- Palette (RGB565, flat fills) ---------------------------------------- */

#define COL_BG     0x0000u     /* black                                      */
#define COL_FRAME  0x8410u     /* playfield border                           */
#define COL_FLOOR  0x1082u     /* playfield background                       */
#define COL_TEXT   0xFFFFu     /* panel labels                               */
#define COL_NUM    0x07E0u     /* panel numbers (green)                      */
#define COL_OVER   0xF800u     /* GAME OVER                                  */

/* One colour per piece id (I O T S Z J L). The playfield tilemap stores this
 * RGB565 value in each occupied cell's high half. */
static const unsigned short pcol[7] = {
    0x07FFu, 0xFFE0u, 0xF81Fu, 0x07E0u, 0xF800u, 0x001Fu, 0xFD20u
};

/* The playfield's single tile: a 9x9 solid square in the top-left of a
 * 10x10 cell (the last row/column stay transparent, giving the 1px grid gap
 * the old per-cell gfx_rect drew). 10px wide = 2 bytes/row, 10 rows. Every
 * occupied cell references this tile (index 1); the per-cell colour carries
 * the piece identity, so one tile renders all seven pieces. */
static const unsigned char block_tile[20] = {
    0xFF, 0x80, 0xFF, 0x80, 0xFF, 0x80, 0xFF, 0x80, 0xFF, 0x80,
    0xFF, 0x80, 0xFF, 0x80, 0xFF, 0x80, 0xFF, 0x80, 0x00, 0x00,
};

/* --- Pieces --------------------------------------------------------------- *
 * All pieces live in a 4x4 box; a cell is row*4+col. Rotation is the box
 * rotation (r,c) -> (c,3-r), applied rot times — except O, which is
 * rotation-stable and never rotates. One table, drawn and collided
 * everywhere: this is the "repeated objects" the milestone asks for. */

#define SH_I 0
#define SH_O 1

static const unsigned char shape[7][4] = {
    { 4, 5, 6, 7 },   /* I: row 1 full            */
    { 0, 1, 4, 5 },   /* O: 2x2 (never rotated)  */
    { 1, 4, 5, 6 },   /* T                        */
    { 1, 2, 4, 5 },   /* S                        */
    { 0, 1, 5, 6 },   /* Z                        */
    { 0, 4, 5, 6 },   /* J                        */
    { 2, 4, 5, 6 },   /* L                        */
};

/* --- Game state ----------------------------------------------------------- *
 * score/lines/level/state/locked/field/cur are globals the test reads out
 * of guest RAM (addresses from the ELF symbol table). */

int score;
int lines;
int level;
int state;
int locked;              /* pieces locked so far (test progress probe)     */
/* The playfield IS the tilemap (M20): each cell is a 32-bit tilemap word —
 * 0 = empty, else 1u | (pcol[piece id] << 16): tile index 1 (block_tile) plus
 * a per-cell colour. This array is the single source of truth for collision
 * AND for rendering, so draw() is one gfx_tilemap command and lock/clear only
 * touch the cells that change (no per-frame 200-cell repaint loop). */
unsigned int field[COLS * ROWS];

int cur;                 /* current piece id                               */
int next;                /* preview piece id                               */
int rot;                 /* rotation 0..3                                  */
int cur_x, cur_y;        /* piece position (box top-left, cell units)      */
unsigned prng;           /* xorshift32 state (fixed seed: no entropy here) */

static unsigned char curc[4];      /* current piece cells (row*4+col)      */
static unsigned prev_pad;
static unsigned next_fall;         /* timer count of the next gravity step */
static unsigned beep_until;
static int beep_on;

#define SEED       0x1234ABCDu
#define BE_TICKS   6

/* Tone events (the test reads AUDIO_FREQ after each). */
#define BE_LOCK    440u
#define BE_CLEAR   880u
#define BE_OVER    110u
#define BE_START   220u

#define ST_PLAY  1
#define ST_OVER  2

/* Points per simultaneously cleared lines, x (level + 1). */
static const unsigned short pts[5] = { 0, 100, 300, 500, 800 };

/* --- MMIO helpers --------------------------------------------------------- */

static unsigned mmio_read(unsigned addr)  { return *(volatile unsigned *)addr; }
static void mmio_write(unsigned addr, unsigned v)
{
    *(volatile unsigned *)addr = v;
}

static unsigned timer_now(void) { return mmio_read(REG_TIMER_COUNT); }
static unsigned input_pad(void) { return mmio_read(REG_INPUT_PAD); }

static void event_beep(unsigned hz)
{
    mmio_write(REG_AUDIO_FREQ, hz);
    mmio_write(REG_AUDIO_CTRL, 1u);
    beep_until = timer_now() + BE_TICKS;
    beep_on = 1;
}

/* Gravity delay in ticks: level 0 falls every 30 ticks (0.5 s), each
 * level tightens it, floor at 4. */
static int fall_delay(void)
{
    return level >= 13 ? 4 : 30 - 2 * level;
}

/* --- Piece plumbing ------------------------------------------------------- */

static unsigned prng_step(void)
{
    prng ^= prng << 13;
    prng ^= prng >> 17;
    prng ^= prng << 5;
    return prng;
}

/* Recompute curc[] from cur/rot (the only place rotation math lives). */
static void setcells(void)
{
    int i, k;
    for (i = 0; i < 4; i++) {
        int r = shape[cur][i] / 4, c = shape[cur][i] % 4;
        if (cur != SH_O)
            for (k = 0; k < rot; k++) {
                int t = r;
                r = c;
                c = 3 - t;
            }
        curc[i] = (unsigned char)(r * 4 + c);
    }
}

static int collide(int x, int y)
{
    int i;
    for (i = 0; i < 4; i++) {
        int fr = y + curc[i] / 4, fc = x + curc[i] % 4;
        if (fc < 0 || fc >= COLS || fr >= ROWS)
            return 1;
        if (fr >= 0 && field[fr * COLS + fc])
            return 1;
    }
    return 0;
}

static void spawn(void);

static void lock_piece(void)
{
    int i, r, rr, c, full, n = 0;

    for (i = 0; i < 4; i++)
        field[(cur_y + curc[i] / 4) * COLS + (cur_x + curc[i] % 4)] =
            1u | ((unsigned int)pcol[cur] << 16);   /* tile 1 + this colour */
    locked++;

    /* Clear full rows, shifting everything above down. Recheck the same
     * row after a shift so consecutive full rows all clear. */
    for (r = ROWS - 1; r >= 0; ) {
        for (c = 0, full = 1; c < COLS; c++)
            if (!field[r * COLS + c])
                full = 0;
        if (full) {
            for (rr = r; rr > 0; rr--)
                for (c = 0; c < COLS; c++)
                    field[rr * COLS + c] = field[(rr - 1) * COLS + c];
            for (c = 0; c < COLS; c++)
                field[c] = 0;
            n++;
        } else {
            r--;
        }
    }
    if (n) {
        score += pts[n] * (level + 1);
        lines += n;
        level = lines / 10 > 15 ? 15 : lines / 10;
        event_beep(BE_CLEAR);
    } else {
        event_beep(BE_LOCK);
    }

    spawn();
}

static void spawn(void)
{
    cur = next;
    next = (int)(prng_step() % 7u);
    rot = 0;
    cur_x = 3;
    cur_y = 0;
    setcells();
    if (collide(cur_x, cur_y)) {
        state = ST_OVER;
        event_beep(BE_OVER);
    }
}

static void new_game(int beep)
{
    int i;
    score = lines = level = 0;
    locked = 0;
    for (i = 0; i < COLS * ROWS; i++)
        field[i] = 0;
    prng = SEED;
    next = (int)(prng_step() % 7u);
    state = ST_PLAY;
    next_fall = timer_now() + (unsigned)fall_delay();
    spawn();
    if (beep)
        event_beep(BE_START);
}

/* --- Frame update --------------------------------------------------------- */

static void update(void)
{
    unsigned pad = input_pad();
    unsigned now = timer_now();

    if (beep_on && now >= beep_until) {
        mmio_write(REG_AUDIO_CTRL, 0u);
        beep_on = 0;
    }

    if (state == ST_OVER) {
        if ((pad & PAD_START) && !(prev_pad & PAD_START))
            new_game(1);
        prev_pad = pad;
        return;
    }

    /* Horizontal: one step on the press edge, then every 3rd tick. */
    if (pad & PAD_LEFT) {
        if (!(prev_pad & PAD_LEFT) || now % 3 == 0) {
            if (!collide(cur_x - 1, cur_y))
                cur_x--;
        }
    }
    if (pad & PAD_RIGHT) {
        if (!(prev_pad & PAD_RIGHT) || now % 3 == 0) {
            if (!collide(cur_x + 1, cur_y))
                cur_x++;
        }
    }

    /* Rotate on the A press edge; keep the old state if it collides. */
    if ((pad & PAD_A) && !(prev_pad & PAD_A)) {
        int save = rot;
        rot = (rot + 1) & 3;
        setcells();
        if (collide(cur_x, cur_y)) {
            rot = save;
            setcells();
        }
    }

    /* Soft drop: while DOWN is held, step every 2nd tick. */
    if ((pad & PAD_DOWN) && now % 2 == 0) {
        if (!collide(cur_x, cur_y + 1))
            cur_y++;
        else
            lock_piece();
        next_fall = now + (unsigned)fall_delay();
    }

    /* Gravity. */
    if (now >= next_fall) {
        if (!collide(cur_x, cur_y + 1))
            cur_y++;
        else
            lock_piece();
        next_fall = now + (unsigned)fall_delay();
    }

    prev_pad = pad;
}

/* --- Drawing -------------------------------------------------------------- */

/* 3x5 font at scale 2 (6x10 pixels), one byte per row, bit2 = left pixel.
 * Digits 0-9, then letters A C E G I L M N O P R S T V (the set the panel
 * and the GAME OVER screen need). */
static const unsigned char font[24][5] = {
    { 7, 5, 5, 5, 7 },   /* 0 */
    { 2, 1, 1, 1, 7 },   /* 1 */
    { 7, 1, 7, 4, 7 },   /* 2 */
    { 7, 1, 7, 1, 7 },   /* 3 */
    { 5, 5, 7, 1, 1 },   /* 4 */
    { 7, 4, 7, 1, 7 },   /* 5 */
    { 7, 4, 7, 5, 7 },   /* 6 */
    { 7, 1, 1, 1, 1 },   /* 7 */
    { 7, 5, 7, 5, 7 },   /* 8 */
    { 7, 5, 7, 1, 7 },   /* 9 */
    { 2, 5, 7, 5, 5 },   /* A */
    { 7, 4, 4, 4, 7 },   /* C */
    { 7, 4, 7, 4, 7 },   /* E */
    { 7, 4, 5, 5, 7 },   /* G */
    { 7, 2, 2, 2, 7 },   /* I */
    { 4, 4, 4, 4, 7 },   /* L */
    { 5, 7, 7, 5, 5 },   /* M */
    { 5, 7, 7, 7, 5 },   /* N */
    { 7, 5, 5, 5, 7 },   /* O */
    { 7, 5, 7, 4, 4 },   /* P */
    { 7, 5, 7, 5, 5 },   /* R */
    { 7, 4, 7, 1, 7 },   /* S */
    { 7, 2, 2, 2, 2 },   /* T */
    { 5, 5, 5, 5, 2 },   /* V */
};

static int glyph_index(char ch)
{
    int i;
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    for (i = 0; i < 14; i++)
        if ("ACEGILMNOPRSTV"[i] == ch)
            return 10 + i;
    return -1;
}

static void draw_glyph(int x, int y, int g, unsigned col)
{
    int r, c;
    for (r = 0; r < 5; r++)
        for (c = 0; c < 3; c++)
            if (font[g][r] & (unsigned)(4 >> c))
                gfx_rect(x + c * 2, y + r * 2, 2, 2, col);
}

static void draw_text(int x, int y, const char *s, unsigned col)
{
    for (; *s; s++) {
        int g = glyph_index(*s);
        if (g >= 0)
            draw_glyph(x, y, g, col);
        x += 10;
    }
}

/* Right-aligned decimal with `digits` digits (10px pitch, like Breakout). */
static void draw_num(int x, int y, int v, int digits, unsigned col)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        draw_glyph(x + i * 10, y, v % 10, col);
        v /= 10;
    }
}

static void draw_cell_block(int x, int y, int pitch, unsigned col)
{
    gfx_rect(x, y, pitch - 1, pitch - 1, col);
}

static void draw(void)
{
    int i;

    gfx_clear(COL_BG);

    /* Playfield: border, floor, then the whole grid in ONE tilemap command
     * (the field IS the map, so no per-frame repaint loop). Empty cells
     * (word 0) leave the floor showing through. */
    gfx_rect(PX - 2, PY - 2, COLS * CELL + 4, ROWS * CELL + 4, COL_FRAME);
    gfx_rect(PX, PY, COLS * CELL, ROWS * CELL, COL_FLOOR);
    gfx_tilemap(PX, PY, field, block_tile, COLS, ROWS, CELL, CELL);

    /* Panel: labels, numbers, next-piece preview. */
    draw_text(PANEL_X, 14, "SCORE", COL_TEXT);
    draw_num(PANEL_X, 26, score, 5, COL_NUM);
    draw_text(PANEL_X, 46, "LINES", COL_TEXT);
    draw_num(PANEL_X, 58, lines, 3, COL_NUM);
    draw_text(PANEL_X, 78, "LEVEL", COL_TEXT);
    draw_num(PANEL_X, 90, level, 2, COL_NUM);
    draw_text(PANEL_X, 110, "NEXT", COL_TEXT);
    gfx_rect(PV_X - 2, PV_Y - 2, 4 * PV_CELL + 2, 4 * PV_CELL + 2,
             COL_FRAME);

    if (state == ST_PLAY) {
        /* Current piece, from the same shape table as everything else. */
        for (i = 0; i < 4; i++)
            draw_cell_block(PX + (cur_x + curc[i] % 4) * CELL,
                            PY + (cur_y + curc[i] / 4) * CELL,
                            CELL, pcol[cur]);
        /* Next-piece preview: the same 4 cells of the next id. */
        for (i = 0; i < 4; i++)
            gfx_rect(PV_X + (shape[next][i] % 4) * PV_CELL,
                     PV_Y + (shape[next][i] / 4) * PV_CELL,
                     PV_CELL - 1, PV_CELL - 1, pcol[next]);
    } else {
        draw_text(106, 100, "GAME OVER", COL_OVER);
        draw_text(96, 120, "PRESS START", COL_TEXT);
    }

    gfx_present();
}

/* --- Entry point ---------------------------------------------------------- */

int main(void)
{
    mmio_write(REG_AUDIO_CTRL, 0u);   /* silent boot: the device powers on
                                       * at 440 Hz (M15) */
    new_game(0);
    for (;;) {
        update();
        draw();
    }
    return 0;
}
