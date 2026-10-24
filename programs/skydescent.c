/* SKY DESCENT — a descending shoot-'em-up for ARCADE-1 (Game #6).
 *
 * Redone as an ARCADE-1 guest ROM from the owner's prototype in shmup/
 * ("Sky Descent", a libretro core): the same game — a 12,000 m descent
 * through five named layers, the tap-cadence gun, the A/B charge beam,
 * formations of fighters and drones, and a boss waiting on the ground —
 * re-laid-out for the machine's 480x270 screen and drawn with the
 * machine's commands.
 *
 * What the port had to change, and why:
 *   - the machine has NO alpha blending (SPEC v0.1), so every surface the
 *     prototype blended per pixel (sky gradient, cloud bodies, the ground
 *     map fading in below 2,200 m, the charge glow, the beam) is mixed at
 *     the COLOUR level instead: one RGB565 per rect, computed against the
 *     sky colour the prototype would have blended it over;
 *   - portrait 240x320 became landscape 480x270 (the screen is fixed);
 *   - the ship — the one sprite the prototype drew — is a 4-bit image
 *     built from its string map at start-up and drawn from palette bank 0;
 *   - text uses the SDK's opt-in 5x7 font (text.h), plus a local scaled
 *     drawer for the title and the phase banners.
 *
 * The tap-speed + charge cadence is the prototype's shmup/tapcad.h,
 * inlined verbatim (integer only, deterministic); time comes from the
 * frame timer, never the host clock.
 *
 * Observable globals (read with --vars):
 *     state, score, lives, altitude, phase, boss_hp
 *
 *     arcade1 run skydescent.arc1 --frames 600 \
 *         --vars state,score,altitude,lives --var-every 60
 *
 * States: 0 title, 1 flying, 2 crashed (game over), 3 touchdown (the
 * boss died and the descent is complete).
 *
 * Controls: the D-pad flies; A taps drive the cadence gun (tap faster =
 * faster and wider); alternating A and B charges; START releases the
 * beam. A, B or START start and restart.
 *
 * Determinism: integer maths only, a seeded LCG (rng = 7 on every start,
 * so a session replays bit-identically), time from the frame timer.
 */

#include "arcade1.h"
#include "text.h"
#include <stdint.h>          /* freestanding compiler header: int16_t for SINT */

typedef int i32;
typedef unsigned u32;
typedef unsigned short u16;
typedef unsigned char u8;

/* --- screen ---------------------------------------------------------------- */
#define W 480
#define H 270

/* --- world ----------------------------------------------------------------- */
#define STAR_COUNT 70
#define SCROLL_AT_TOP 205     /* Q8 px/frame at 12,000 m */
#define SCROLL_AT_GND 666     /* Q8 px/frame at the ground (205 + 461) */

/* --- entity pools ---------------------------------------------------------- */
#define NE  24                /* enemies, including the boss */
#define NB  64                /* the player's shots */
#define NEB 128               /* enemy shots */
#define NP 128                /* particles */
#define NC  3                 /* cloud layers: 14 + 12 + 7 clouds */

/* --- states (also the --vars values) --------------------------------------- */
enum { ST_TITLE, ST_PLAY, ST_DEAD, ST_WIN };

/* --- observable state (file scope: the tools read these by name) ----------- */
int state = ST_TITLE;
int score = 0;
int lives = 3;
int altitude = 12000;         /* metres above the ground */
int phase = 0;                /* 0 stratosphere .. 4 ground */
int boss_hp = 0;              /* 0 until the boss exists */

/* --- internal state -------------------------------------------------------- */
static i32 px, py;            /* the ship, Q8 pixels */
static i32 gy;                /* the world scroll, Q8 pixels */
static i32 inv, fcd, t, wt, beam, beamw, banner, flash, v;
static u32 rng;
static u32 now_ms;            /* frame timer -> ms, the prototype's clock */

/* --- maths ------------------------------------------------------------------ */
static i32 cl(i32 v, i32 a, i32 b) { return v < a ? a : v > b ? b : v; }
static i32 ab(i32 v) { return v < 0 ? -v : v; }
static u32 isq(u32 n) {                      /* floor(sqrt(n)), no libm */
    u32 r = 0, b = 1u << 30;
    while (b > n) b >>= 2;
    while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}

/* The seeded LCG (the prototype's stream: high 16 bits, one step per
 * draw, consumed in program order — deterministic by construction). */
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return (u16)(rng >> 16); }

static const int16_t SINT[256]={0,6,13,19,25,31,38,44,50,56,62,68,74,80,86,92,98,104,109,115,121,126,132,137,142,147,152,157,162,167,172,177,181,185,190,194,198,202,206,209,213,216,220,223,226,229,231,234,237,239,241,243,245,247,248,250,251,252,253,254,255,255,256,256,256,256,256,255,255,254,253,252,251,250,248,247,245,243,241,239,237,234,231,229,226,223,220,216,213,209,206,202,198,194,190,185,181,177,172,167,162,157,152,147,142,137,132,126,121,115,109,104,98,92,86,80,74,68,62,56,50,44,38,31,25,19,13,6,0,-6,-13,-19,-25,-31,-38,-44,-50,-56,-62,-68,-74,-80,-86,-92,-98,-104,-109,-115,-121,-126,-132,-137,-142,-147,-152,-157,-162,-167,-172,-177,-181,-185,-190,-194,-198,-202,-206,-209,-213,-216,-220,-223,-226,-229,-231,-234,-237,-239,-241,-243,-245,-247,-248,-250,-251,-252,-253,-254,-255,-255,-256,-256,-256,-256,-256,-255,-255,-254,-253,-252,-251,-250,-248,-247,-245,-243,-241,-239,-237,-234,-231,-229,-226,-223,-220,-216,-213,-209,-206,-202,-198,-194,-190,-185,-181,-177,-172,-167,-162,-157,-152,-147,-142,-137,-132,-126,-121,-115,-109,-104,-98,-92,-86,-80,-74,-68,-62,-56,-50,-44,-38,-31,-25,-19,-13,-6};

#define SIN(i) SINT[(i) & 255]
#define COS(i) SINT[((i) + 64) & 255]

/* --- colours ----------------------------------------------------------------
 * Everything is 0xRRGGBB until it is drawn: C24() converts to the machine's
 * RGB565 (a 24-bit literal passed straight to the device would be silently
 * truncated — CHEATSHEET.md "Palette"), and mixc() is the prototype's
 * 256-step blend, run once per rect instead of once per pixel. */
#define C24(x) arcade1_rgb(((x) >> 16) & 255u, ((x) >> 8) & 255u, (x) & 255u)
static u32 mixc(u32 a, u32 b, i32 t)
{
    u32 o = 0;
    for (i32 s = 16; s >= 0; s -= 8) {
        i32 x = (a >> s) & 255, y = (b >> s) & 255;
        o |= (u32)(x + (((y - x) * t) >> 8)) << s;
    }
    return o;
}

/* --- drawing helpers (all take 0xRRGGBB) ------------------------------------ */
static void rect(int x, int y, int w, int h, u32 c)
{
    arcade1_rect(x, y, w, h, C24(c));
}
static void box(int x, int y, int w, int h, u32 c)
{
    rect(x - 1, y - 1, w + 2, h + 2, 0x1a1424);
    rect(x, y, w, h, c);
}
static void pxl(int x, int y, u32 c)
{
    arcade1_pixel(x, y, C24(c));
}
static int slen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Scaled 5x7 text (text.h's drawer is 1x). Horizontal runs merge into one
 * rect each; the shadow pass runs first so it always sits behind. */
static void text_s(int x, int y, int sc, const char *s, u32 col, int shadow)
{
    for (; *s; s++, x += 8 * sc) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20u || c > 0x5Fu)
            continue;
        const u8 *g = &arcade1_text_font[(c - 0x20u) * 8u];
        if (shadow)
            for (int r = 0; r < 7; r++)
                for (int k = 0; k < 5; k++)
                    if (g[r] & (0x80u >> k))
                        rect(x + k * sc + 1, y + r * sc + 1, sc, sc, 0);
        for (int r = 0; r < 7; r++) {
            int k = 0;
            while (k < 5) {
                if (g[r] & (0x80u >> k)) {
                    int e = k;
                    while (e < 5 && (g[r] & (0x80u >> e))) e++;
                    rect(x + k * sc, y + r * sc, (e - k) * sc, sc, col);
                    k = e;
                } else {
                    k++;
                }
            }
        }
    }
}
static void text_sc(int y, int sc, const char *s, u32 col)
{
    text_s((W - slen(s) * 8 * sc) / 2, y, sc, s, col, 1);
}

/* --- the tap-cadence + charge gun (the prototype's shmup/tapcad.h, inlined)
 * Q8 = 256 is 1.0. Tapping one button repeatedly raises the cadence;
 * alternating A/B quickly fills the charge. Deterministic: every call
 * takes the frame timer, never a host clock. ------------------------------ */
#define TC_SLOW_MS 300
#define TC_FAST_MS 70
#define TC_RESET_MS 700
#define TC_ALT_MS 400
#define TC_DEBOUNCE_MS 25
#define TC_CHARGE_FULL 25600
typedef struct {
    u32 last_t, charge_t, tick_t;
    u16 ema, speed, charge;
    u8 btn, has_last, has_charge;
} tapcad_t;
static i32 tc_cl(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
static void tc_init(tapcad_t *c)
{
    u8 *p = (u8 *)c;
    for (unsigned i = 0; i < sizeof *c; i++) p[i] = 0;
}
static void tc_press(tapcad_t *c, u32 ms, int btn)
{
    if (c->has_last) {
        u32 iv = ms - c->last_t;
        if (iv < TC_DEBOUNCE_MS)
            return;
        c->ema = iv < TC_RESET_MS ? (u16)(c->ema ? (c->ema + iv) / 2 : iv) : 0;
        if (btn != c->btn && iv < TC_ALT_MS) {
            i32 q = tc_cl(((i32)(TC_ALT_MS - iv) * 256) / 330, 0, 256);
            c->charge = (u16)tc_cl((i32)c->charge + 512 + 10 * q, 0, TC_CHARGE_FULL);
            c->charge_t = ms;
            c->has_charge = 1;
            c->btn = (u8)btn;
            c->last_t = ms;
            return;
        }
    }
    c->btn = (u8)btn;
    c->last_t = ms;
    c->has_last = 1;
    c->speed = c->ema ? (u16)tc_cl(((i32)(TC_SLOW_MS - c->ema) * 256) /
                                   (TC_SLOW_MS - TC_FAST_MS), 0, 256) : 0;
}
static u16 tc_speed(const tapcad_t *c, u32 now)
{
    if (!c->has_last)
        return 0;
    i32 idle = (i32)(now - c->last_t);
    i32 f = idle > 250 ? 256 - (idle - 250) * 256 / 350 : 256;
    return (u16)((c->speed * tc_cl(f, 0, 256)) >> 8);
}
static int tc_charging(const tapcad_t *c, u32 now)
{
    return c->has_charge && now - c->charge_t < 350;
}
static void tc_tick(tapcad_t *c, u32 now)
{
    u32 dt = now - c->tick_t;
    c->tick_t = now;
    if (c->charge && c->has_last && now - c->last_t > 500) {
        u32 d = dt * 512u / 100u;
        c->charge = d >= c->charge ? 0 : (u16)(c->charge - d);
    }
}
static u16 tc_spend(tapcad_t *c)
{
    u16 v = c->charge;
    c->charge = 0;
    return v;
}

/* --- entities --------------------------------------------------------------- */
typedef struct { i32 x, y, vx, vy; u8 on; } bul_t;
typedef struct { u8 on, k, dir, lin; i32 x, y, bx, vx, vy, ty, hp, ph, cd, cd2, born; } en_t;
typedef struct { i32 x, y, vx, vy, l; u32 c; u8 on; } part_t;
typedef struct { i32 x, y, w, h, s; } cloud_t;

static en_t E[NE];
static bul_t B[NB], EB[NEB];
static part_t PT[NP];
static cloud_t CL[NC][14];
static u16 STARS[STAR_COUNT][3];
static tapcad_t TC;
static en_t *boss;

/* the three enemy kinds, and their sizes (w x h) */
enum { E_FIGHTER, E_DRONE, E_BOSS };
static const i32 DW[3] = { 11, 13, 56 }, DH[3] = { 9, 12, 30 };

/* cloud layers: count, scroll speed, scale, alpha weight */
static const int LAYN[3] = { 14, 12, 7 };
static const int LAYSPD[3] = { 128, 256, 410 };
static const int LAYSC[3] = { 179, 256, 410 };
static const int LAYA[3] = { 140, 256, 90 };

/* --- sky: the five altitude keyframes --------------------------------------- */
typedef struct { i32 alt; u32 top, bot, cl, cs; i32 ca; } kf_t;
static const kf_t KF[6] = {
    {12000, 0x04051a, 0x0a1030, 0x6a78a8, 0x3a4468, 64},
    {9500,  0x0e2f6e, 0x2f6cc0, 0xffffff, 0xa8bce0, 230},
    {6500,  0x3c4a5e, 0x7a8698, 0xc8ced8, 0x7a8498, 256},
    {3500,  0x5c86b8, 0xa8c4dc, 0xf4f8ff, 0xb4c8e0, 256},
    {1500,  0x7cb8e8, 0xd4e6f2, 0xffffff, 0xc0d4e8, 205},
    {0,     0x8cc4ee, 0xe0eef4, 0xffffff, 0xc8dcec, 64}
};
typedef struct { u32 top, bot, cl, cs; i32 ca; } pal_t;
static pal_t pal(i32 alt)
{
    int i = 0;
    while (i < 4 && alt < KF[i + 1].alt) i++;
    const kf_t *a = &KF[i], *b = &KF[i + 1];
    i32 tt = cl((a->alt - alt) * 256 / (a->alt - b->alt), 0, 256);
    pal_t p;
    p.top = mixc(a->top, b->top, tt);
    p.bot = mixc(a->bot, b->bot, tt);
    p.cl  = mixc(a->cl,  b->cl,  tt);
    p.cs  = mixc(a->cs,  b->cs,  tt);
    p.ca  = a->ca + (((b->ca - a->ca) * tt) >> 8);
    return p;
}
static const char *PH[5] = {
    "STRATOSPHERE", "HIGH CLOUD", "STORM FRONT", "LOW HAZE", "GROUND"
};
static int phase_of(i32 a)
{
    return a > 9500 ? 0 : a > 6500 ? 1 : a > 3500 ? 2 : a > 1500 ? 3 : 4;
}

/* The sky gradient, one entry per 4-row band, rebuilt at the top of every
 * frame. The ground and the clouds mix against it (the per-pixel blends
 * the prototype did over the sky), so the fade costs one mix per cell. */
#define NBAND ((H + 3) / 4)
static u32 skyband[NBAND];
static u32 sky_at(int y)
{
    if (y < 0) y = 0;
    if (y >= H) y = H - 1;
    return skyband[y >> 2];
}

/* --- the ship: a 4-bit sprite built from the prototype's string map --------
 * 13 pixels of art mirrored about the centre column into a 14-wide image
 * (the 4-bit blit needs an EVEN width — an odd one draws nothing, silently;
 * column 13 stays transparent). Values 1..5 index palette bank 0. */
#define SHIP_W 14
#define SHIP_H 15
#define SHIP_ROWB (SHIP_W / 2)
static u8 ship4[SHIP_H * SHIP_ROWB];
static const char *SHIPROWS[SHIP_H] = {
    "......o", ".....ow", ".....ow", "....obw", "....oby",
    "o...obb", "ow.oobb", "owwoobb", "owbbbdb", "owbbbdd",
    "oobbbdd", ".oobbbd", "...obbd", "...oodo", ".....oo"
};
static const u32 SHIPC[6] = { 0x000000, 0x14182e, 0xe8f0ff, 0x4a8fe8,
                              0x2a4fa0, 0xffe08a };  /* 0 unused (transparent) */
static u16 ship_pal[6];
static u8 shipval(const char *row, int x)
{
    int sx;
    char ch;
    if (x > 12)
        return 0;                       /* the padding column */
    sx = x < 7 ? x : 12 - x;            /* mirror about the centre */
    ch = row[sx];
    switch (ch) {
    case 'o': return 1;
    case 'w': return 2;
    case 'b': return 3;
    case 'd': return 4;
    case 'y': return 5;
    default:  return 0;
    }
}
static void build_ship(void)
{
    for (int y = 0; y < SHIP_H; y++)
        for (int b = 0; b < SHIP_ROWB; b++)
            ship4[y * SHIP_ROWB + b] =
                (u8)((shipval(SHIPROWS[y], b * 2) << 4) |
                      shipval(SHIPROWS[y], b * 2 + 1));
    for (int i = 0; i < 6; i++)
        ship_pal[i] = (u16)C24(SHIPC[i]);
}

/* --- terrain (the prototype's value noise over 8x8 cells) ------------------- */
static u32 hs(i32 a, i32 b)
{
    u32 h = (u32)a * 374761393u + (u32)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) & 255;
}
static i32 vn(i32 x, i32 y)              /* smooth value noise, x/y Q8 -> 0..255 */
{
    i32 x0 = x >> 8, y0 = y >> 8, fx = x & 255, fy = y & 255;
    i32 sx = (fx * fx * (768 - 2 * fx)) >> 16;
    i32 sy = (fy * fy * (768 - 2 * fy)) >> 16;
    i32 a = (i32)hs(x0, y0), b = (i32)hs(x0 + 1, y0);
    i32 c = (i32)hs(x0, y0 + 1), d = (i32)hs(x0 + 1, y0 + 1);
    i32 tt = a + (((b - a) * sx) >> 8), u = c + (((d - c) * sx) >> 8);
    return tt + (((u - tt) * sy) >> 8);
}
static const u32 TCOL[7] = { 0x1a4a8a, 0x2a6ab0, 0xd8c890, 0x3f8a3a,
                             0x2a6a30, 0x8a8a80, 0xf0f0f0 };
static int tile(i32 i, i32 k)
{
    i32 h = (153 * vn(i * 256 / 7, k * 256 / 7) +
             77 * vn(i * 256 * 10 / 32 + 9 * 256, k * 256 * 10 / 32 + 4 * 256) +
             26 * (i32)hs(i, k)) >> 8;
    return h < 92 ? 0 : h < 115 ? 1 : h < 128 ? 2 : h < 174 ? 3 :
           h < 205 ? 4 : h < 230 ? 5 : 6;
}
/* tile(i,k) is pure and the ground scrolls only a few pixels per frame, so
 * the visible window is cached by row: a row is recomputed only when it
 * scrolls in (every ~4 frames at descent speed), not 60 times per frame.
 * Measured: an uncached ground frame costs over the 1,000,000-instruction
 * per-frame cap (WORK_INST_MAX hits exactly 1,000,000 — the frame gets cut
 * and present() slips into the next machine frame, halving the update rate);
 * cached, the ground frame costs a fraction of that. Slot = k & 63 (the
 * window is ~36 rows, so live rows never collide in 64 slots). */
#define TC_SLOTS 64
static u8 tc_val[W / 8][TC_SLOTS];
static i32 tc_k[TC_SLOTS];
static u8 tc_ok[TC_SLOTS];
static i32 tc_next;             /* the next row the warmer will build */
static int tc_row(i32 k)
{
    int s = (int)(k & (TC_SLOTS - 1));
    i32 i;
    if (!tc_ok[s] || tc_k[s] != k) {
        for (i = 0; i < W / 8; i++)
            tc_val[i][s] = (u8)tile(i, k);
        tc_k[s] = k;
        tc_ok[s] = 1;
    }
    return s;
}
/* Warm a few rows per frame, always ~64 rows wide from the bottom of the
 * window. The visible window is ~36 rows, so the 28 rows of headroom mean
 * every row the ground will need is built frames ahead of time — no frame
 * ever builds the map in one go (that crosses the per-frame instruction
 * cap and gets the frame cut). A few rows per frame is ~70K extra
 * instructions at most; at descent speed a new row appears every ~3
 * frames, so the warmer keeps pace with room to spare. */
static void ground_warm(void)
{
    i32 kb = ((gy >> 8) - H) >> 3;
    i32 kmax = kb + TC_SLOTS - 1;
    if (tc_next < kb || tc_next > kmax)
        tc_next = kb;                       /* first run, or gy restarted */
    for (int n = 0; n < 4 && tc_next <= kmax; n++, tc_next++)
        (void)tc_row(tc_next);
}
/* The whole map fades in below 2,200 m. The prototype blended every pixel
 * over the sky; here each 8x8 cell is mixed once, against the band the sky
 * shows at that row, and drawn as one opaque rect. */
static void ground(i32 a)
{
    i32 g = gy >> 8;
    for (i32 k = (g - H) >> 3; k <= (g >> 3) + 1; k++) {
        i32 y = g - k * 8 - 8;
        int s = tc_row(k);
        for (i32 i = 0; i < W / 8; i++)
            rect((int)(i * 8), (int)y, 8, 8,
                 mixc(sky_at((int)y), TCOL[tc_val[i][s]], a));
    }
}

/* --- start / damage / death -------------------------------------------------- */
static void game_init(void);
static void start(void)
{
    game_init();
    state = ST_PLAY;
}
static void boom(int x, int y, int n)
{
    for (int i = 0; i < n; i++)
        for (int k = 0; k < NP; k++)
            if (!PT[k].on) {
                i32 ang = rnd() & 255, s = 128 + rnd() % 512;
                PT[k].x = x << 8;
                PT[k].y = y << 8;
                PT[k].vx = COS(ang) * s >> 8;
                PT[k].vy = SIN(ang) * s >> 8;
                PT[k].l = 14 + rnd() % 14;
                PT[k].c = rnd() & 1 ? 0xffb030 : 0xfff0a0;
                PT[k].on = 1;
                break;
            }
}
static void ebul(int x, int y, int vx, int vy)
{
    for (int k = 0; k < NEB; k++)
        if (!EB[k].on) {
            EB[k].x = x << 8;
            EB[k].y = y << 8;
            EB[k].vx = vx;
            EB[k].vy = vy;
            EB[k].on = 1;
            return;
        }
}
static void aim(int x, int y, int sp, int spread)   /* sp Q8 px/frame, spread 256 = 360 deg */
{
    i32 dx = px - x, dy = py - y;
    i32 len = (i32)isq((u32)(dx * dx + dy * dy));
    i32 ux, uy, c, s, rx, ry;
    if (!len) len = 1;
    ux = dx * 256 / len;
    uy = dy * 256 / len;
    c = COS(spread);
    s = SIN(spread);
    rx = (ux * c - uy * s) >> 8;
    ry = (ux * s + uy * c) >> 8;
    ebul(x, y, rx * sp >> 8, ry * sp >> 8);
}
static en_t *spawn(void)
{
    for (int i = 0; i < NE; i++)
        if (!E[i].on) {
            E[i].on = 0;
            E[i].k = E[i].dir = E[i].lin = 0;
            E[i].x = E[i].y = E[i].bx = E[i].vx = E[i].vy = 0;
            E[i].ty = E[i].hp = E[i].ph = E[i].cd = E[i].cd2 = E[i].born = 0;
            E[i].on = 1;
            return &E[i];
        }
    return 0;
}
static void wave(i32 pq)
{
    u16 r = rnd();
    if (r < 26214) {                       /* a five-ship formation, weaving */
        i32 bx = 40 + rnd() % (W - 80);
        for (int i = 0; i < 5; i++) {
            en_t *e = spawn();
            if (!e) break;
            e->k = E_FIGHTER;
            e->bx = (bx + (i - 2) * 16) << 8;
            e->x = e->bx;
            e->y = (-10 - ab(i - 2) * 10) * 256;
            e->ph = i * 4096;
            e->hp = 10;
            e->vy = 282 + (205 * pq >> 8);
        }
    } else if (r < 45875) {                /* hovering drones that aim */
        int n = 2 + (pq > 128);
        for (int i = 0; i < n; i++) {
            en_t *e = spawn();
            if (!e) break;
            e->k = E_DRONE;
            e->x = (50 + rnd() % (W - 100)) << 8;
            e->y = (-14 - i * 30) * 256;
            e->ty = (50 + rnd() % 50) << 8;
            e->dir = (rnd() & 1) ? 1 : 2;
            e->hp = 40;
            e->cd = 40 + i * 20;
            e->born = t;
        }
    } else {                               /* a line diving in from the side */
        i32 sd = (rnd() & 1) ? -1 : 1;
        for (int i = 0; i < 4; i++) {
            en_t *e = spawn();
            if (!e) break;
            e->k = E_FIGHTER;
            e->lin = 1;
            e->x = (sd < 0 ? -10 - i * 14 : W + 10 + i * 14) * 256;
            e->y = (30 + i * 10) << 8;
            e->vx = -sd * 384;
            e->vy = 230;
            e->hp = 10;
        }
    }
}
static void hurt(en_t *e, i32 d)
{
    e->hp -= d;
    if (e->hp <= 0 && e->on) {
        e->on = 0;
        boom((int)(e->x >> 8), (int)(e->y >> 8), e->k == E_BOSS ? 60 : 14);
        score += e->k == E_BOSS ? 5000 : e->k == E_DRONE ? 400 : 100;
        if (e->k == E_BOSS) {
            boss_hp = 0;
            state = ST_WIN;
        }
    }
}
static void die(void)
{
    lives--;
    boom((int)(px >> 8), (int)(py >> 8), 40);
    inv = 120;
    for (int k = 0; k < NEB; k++) EB[k].on = 0;
    if (lives < 0)
        state = ST_DEAD;
}

/* --- per-frame update -------------------------------------------------------- */
static void game_update(u32 frame, u16 held, u16 pressed)
{
    i32 pq, s;
    int chg, ph;

    now_ms = (frame * 50u) / 3u;           /* the prototype's clock: frame -> ms */
    t++;
    if (state != ST_PLAY) {
        if (pressed & (BTN_A | BTN_B | BTN_START))
            start();
        return;
    }
    if (pressed & BTN_A) tc_press(&TC, now_ms, 0);
    if (pressed & BTN_B) tc_press(&TC, now_ms, 1);
    tc_tick(&TC, now_ms);
    if ((pressed & BTN_START) && TC.charge >= 2048) {
        beam = 22;
        beamw = 6 + 18 * (TC.charge / 100) / 256;
        tc_spend(&TC);
    }
    /* the ship: Q8 pixels, clamped to the playfield */
    px = cl(px + (((held & BTN_RIGHT) ? 1 : 0) - ((held & BTN_LEFT) ? 1 : 0)) * 589,
            8 * 256, (W - 8) * 256);
    py = cl(py + (((held & BTN_DOWN) ? 1 : 0) - ((held & BTN_UP) ? 1 : 0)) * 589,
            20 * 256, (H - 16) * 256);
    /* the descent: 2 m per frame, faster the lower you are */
    pq = (12000 - altitude) * 256 / 12000;
    v = SCROLL_AT_TOP + (SCROLL_AT_GND - SCROLL_AT_TOP) * pq / 256;
    if (altitude > 0)
        altitude = altitude > 2 ? altitude - 2 : 0;
    gy += v;
    for (int l = 0; l < NC; l++)
        for (int i = 0; i < LAYN[l]; i++) {
            cloud_t *c = &CL[l][i];
            c->y += v * LAYSPD[l] >> 8;
            if (c->y >> 8 > H + c->h) {
                c->y = -c->h * 256;
                c->x = rnd() % W;
            }
        }
    ph = phase_of(altitude);
    if (ph != phase) {
        phase = ph;
        banner = 100;
    }
    if (banner > 0) banner--;
    if (ph == 2 && rnd() < 262) flash = 6; /* storm-front lightning */
    if (flash > 0) flash--;
    if (altitude > 0) {
        if (--wt <= 0) {
            wave(pq);
            wt = 200 - 90 * pq / 256;
        }
    } else if (!boss) {
        en_t *e = spawn();
        if (e) {
            e->k = E_BOSS;
            e->x = (W / 2) << 8;
            e->y = -40 * 256;
            e->hp = 1600;
            e->cd = 60;
            e->cd2 = 30;
            boss = e;
            boss_hp = 1600;
        }
    }
    /* the gun: cadence rises with the tap speed; charging holds fire */
    s = tc_speed(&TC, now_ms);
    chg = tc_charging(&TC, now_ms);
    if (--fcd <= 0 && !chg) {
        int hold = (held & (BTN_A | BTN_B)) != 0;
        int base = (hold || s > 13) ? 10 : 24;
        int n, j;
        static const int AS[3][3] = { { 0, 0, 0 }, { -2, 2, 0 }, { -7, 0, 7 } };
        fcd = (base * 256 - (base - 4) * s + 128) >> 8;
        n = 1 + (s > 90) + (s > 180);
        for (j = 0; j < n; j++) {
            int a = AS[n - 1][j];
            for (int k = 0; k < NB; k++)
                if (!B[k].on) {
                    B[k].x = px;
                    B[k].y = py - 8 * 256;
                    B[k].vx = SIN(a) * 7;
                    B[k].vy = -COS(a) * 7;
                    B[k].on = 1;
                    break;
                }
        }
    }
    for (int k = 0; k < NB; k++)
        if (B[k].on) {
            B[k].x += B[k].vx;
            B[k].y += B[k].vy;
            if (B[k].y < -(10 << 8) || B[k].x < 0 || B[k].x > (W << 8))
                B[k].on = 0;
        }
    /* enemies: motion, fire, then collisions against shots and the ship */
    for (int i = 0; i < NE; i++) {
        en_t *e = &E[i];
        i32 w, h, ex, ey;
        int k;
        if (!e->on) continue;
        if (e->k == E_FIGHTER) {
            e->ph += 625;
            e->y += e->vy;
            if (e->lin)
                e->x += e->vx;
            else
                e->x = e->bx + (SIN(e->ph >> 8) * 18);
        } else if (e->k == E_DRONE) {
            if (t - e->born > 700)
                e->y += 384;
            else if (e->y < e->ty)
                e->y += 307;
            else {
                e->x += (e->dir == 1 ? 1 : -1) * 128;
                if ((e->x >> 8) < 24 || (e->x >> 8) > W - 24)
                    e->dir = e->dir == 1 ? 2 : 1;
            }
            if (--e->cd <= 0 && (e->y >> 8) > 10 && (e->y >> 8) < H - 60) {
                e->cd = 75;
                aim((int)(e->x >> 8), (int)((e->y >> 8) + 6), 563, 0);
            }
        } else {
            if ((e->y >> 8) < 50) {
                e->y += 154;
            } else {
                e->x = ((W / 2) << 8) + SIN((t * 13 / 25) & 255) * 70;
                if (--e->cd <= 0) {
                    e->cd = 100;
                    for (int j = 0; j < 14; j++) {
                        i32 a = (j * 256 / 14 + t * 2 / 5) & 255;
                        ebul((int)(e->x >> 8), (int)(e->y >> 8),
                             COS(a) * 410 >> 8, SIN(a) * 410 >> 8);
                    }
                }
                if (--e->cd2 <= 0) {
                    e->cd2 = 45;
                    i32 d[3] = { -10, 0, 10 };
                    for (int j = 0; j < 3; j++)
                        aim((int)(e->x >> 8), (int)((e->y >> 8) + 10), 614, d[j]);
                }
            }
        }
        w = DW[e->k];
        h = DH[e->k];
        ex = e->x >> 8;
        ey = e->y >> 8;
        for (k = 0; k < NB; k++)
            if (B[k].on && ab((B[k].x >> 8) - ex) < w / 2 + 1 &&
                           ab((B[k].y >> 8) - ey) < h / 2 + 2) {
                B[k].on = 0;
                hurt(e, 10);
                if (!e->on) break;
            }
        if (!e->on) continue;
        if (beam > 0 && ab(ex - (px >> 8)) < w / 2 + beamw / 2 && ey < (py >> 8))
            hurt(e, e->k == E_BOSS ? 12 : 30);
        if (!e->on) continue;
        if (inv <= 0 && ab(ex - (px >> 8)) < w / 2 + 2 &&
                       ab(ey - (py >> 8)) < h / 2 + 2) {
            die();
            if (e->k != E_BOSS)
                hurt(e, 90);
        }
        if (e->on && (ey > H + 20 || ex < -30 || ex > W + 30))
            e->on = 0;
    }
    if (beam > 0) beam--;
    for (int k = 0; k < NEB; k++)
        if (EB[k].on) {
            bul_t *b = &EB[k];
            i32 bx, by;
            b->x += b->vx;
            b->y += b->vy;
            bx = b->x >> 8;
            by = b->y >> 8;
            if (beam > 0 && ab(bx - (px >> 8)) < beamw / 2 && by < (py >> 8)) {
                b->on = 0;
                continue;
            }
            if (inv <= 0 && ab(bx - (px >> 8)) < 3 && ab(by - (py >> 8)) < 3) {
                b->on = 0;
                die();
                continue;
            }
            if (bx < -5 || bx > W + 5 || by < -5 || by > H + 5)
                b->on = 0;
        }
    if (inv > 0) inv--;
    for (int k = 0; k < NP; k++)
        if (PT[k].on) {
            PT[k].x += PT[k].vx;
            PT[k].y += PT[k].vy;
            if (--PT[k].l <= 0)
                PT[k].on = 0;
        }
    if (boss && boss->on)
        boss_hp = boss->hp;
}

/* --- rendering ---------------------------------------------------------------- */
static void ell(int cx, int cy, int w, int h, int s, const pal_t *p, i32 a)
{
    for (i32 r = 0; r < h; r++) {
        i32 u = (2 * r + 1) * 256 / h - 256;
        i32 sq = (i32)isq((u32)(65536 - u * u));
        i32 hw = (w / 2) * sq >> 8;
        u32 c;
        hw = hw * (256 + (46 * SIN(s * 40 + r * 35) >> 8)) >> 8;
        if (hw <= 0)
            continue;
        /* the prototype blended each pixel of the row over the sky; one
         * pre-mixed rect per row is the same picture, one command */
        c = r * 100 > h * 62 ? p->cs : p->cl;
        rect(cx - (int)hw, cy + (int)r, (int)(hw * 2), 1,
             mixc(sky_at(cy + (int)r), c, a));
    }
}
static void cloud(const cloud_t *c, const pal_t *p, i32 a)
{
    int y = (int)(c->y >> 8);
    ell(c->x, y, (int)c->w, (int)c->h, (int)c->s, p, a);
    ell(c->x + (int)(c->w * 56 / 256), y - (int)(c->h / 4),
        (int)(c->w / 2), (int)(c->h * 6 / 10), (int)(c->s + 2), p, a);
}

static void draw_ship(int x, int y, u16 speed)
{
    /* the flame */
    i32 f = 2 + (6 * speed >> 8) + (t & 1);
    rect(x - 1, y + 7, 3, (int)f, 0xffb030);
    rect(x, y + 7, 1, (int)(f * 6 / 10), 0xffffff);
    arcade1_sprite4(x - 6, y - 8, ship4, SHIP_W, SHIP_H, 0, 0);
}

static void hud(u16 speed)
{
    /* top strip: altitude, score, lives */
    text_draw(4, 3, "ALT", 0xffff);
    text_u32(32, 3, (unsigned)altitude, 5, 0xffff);
    text_draw(76, 3, "M", 0xffff);
    text_u32(W - 60, 3, (unsigned)score, 7, 0xffff);
    for (int i = 0; i < lives && i < 8; i++)
        rect(4 + i * 8, 14, 5, 5, 0x4a8fe8);
    /* bottom strip: the cadence and the charge, the boss bar */
    text_draw(4, H - 24, "TAP", 0xffff);
    rect(32, H - 23, 60, 6, 0x222222);
    rect(32, H - 23, 60 * speed >> 8, 6, 0xffe08a);
    text_draw(4, H - 13, "PWR", 0xffff);
    rect(32, H - 12, 60, 6, 0x222222);
    rect(32, H - 12, 60 * TC.charge / TC_CHARGE_FULL, 6,
         TC.charge >= 25000 ? 0xffffff : 0xff7a3d);
    if (state == ST_PLAY && boss && boss->on && boss_hp > 0) {
        rect(W / 2 - 40, H - 12, 80, 5, 0x222222);
        rect(W / 2 - 40, H - 12, 80 * boss_hp / 1600, 5, 0xe8604c);
    }
}

static void game_render(void)
{
    pal_t p = pal(altitude);
    i32 sa = cl((altitude - 8500) * 256 / 2500, 0, 256);
    i32 ga = cl((2200 - altitude) * 256 / 1200, 0, 256);
    u16 speed = tc_speed(&TC, now_ms);
    int blink;

    /* 1. the backdrop. Below 2,200 m the ground map covers the whole
     *    screen — pre-mixed toward the sky — so the gradient behind it is
     *    never visible and is not drawn (paint less: CHEATSHEET.md).
     *
     *    Before that, warm the map rows a few per frame (ground_warm):
     *    the window is ~36 rows and gy advances every frame, so the rows
     *    the ground will need are computed ahead of time. Without this
     *    the first ground frame builds the whole visible map at once and
     *    crosses the per-frame instruction cap (the frame gets cut and
     *    present() slips into the next machine frame). */
    ground_warm();
    for (int b = 0; b < NBAND; b++)
        skyband[b] = mixc(p.top, p.bot, (b * 4) * 256 / H);
    if (ga > 0) {
        ground(ga);
    } else {
        for (int b = 0; b < NBAND; b++)
            rect(0, b * 4, W, 4, skyband[b]);
        if (sa > 0)
            for (int i = 0; i < STAR_COUNT; i++) {
                int y = (int)((STARS[i][1] + (gy >> 8) * STARS[i][2] / 1280) % H);
                pxl(STARS[i][0], y,
                    mixc(sky_at(y), 0xffffff,
                         sa * (100 + (STARS[i][2] * 155 >> 8)) >> 8));
            }
    }

    /* 2. the clouds behind the action */
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < LAYN[l]; i++)
            cloud(&CL[l][i], &p, p.ca * LAYA[l] >> 8);

    /* 3. the action */
    for (int i = 0; i < NE; i++) {
        en_t *e = &E[i];
        int x, y;
        if (!e->on) continue;
        x = (int)(e->x >> 8);
        y = (int)(e->y >> 8);
        if (e->k == E_FIGHTER) {
            box(x - 5, y - 1, 11, 3, 0xc0392b);
            box(x - 2, y - 4, 5, 9, 0xe8604c);
            rect(x - 1, y - 2, 3, 2, 0xffe08a);
        } else if (e->k == E_DRONE) {
            box(x - 6, y - 2, 13, 4, 0x2e8b57);
            box(x - 3, y - 6, 7, 12, 0x46b878);
            rect(x - 5, y + 2, 2, 4, 0x222222);
            rect(x + 4, y + 2, 2, 4, 0x222222);
            rect(x - 1, y - 3, 3, 3, 0xffe08a);
        } else {
            box(x - 28, y - 6, 56, 10, 0x6a6a78);
            box(x - 12, y - 15, 24, 30, 0x8a8a9a);
            box(x - 22, y + 2, 6, 8, 0x555566);
            box(x + 16, y + 2, 6, 8, 0x555566);
            rect(x - 4, y - 8, 8, 6, 0xffe08a);
        }
    }
    for (int k = 0; k < NB; k++)
        if (B[k].on) {
            int x = (int)(B[k].x >> 8), y = (int)(B[k].y >> 8);
            rect(x - 1, y - 4, 2, 7, 0xffe08a);
            rect(x, y - 3, 1, 4, 0xffffff);
        }
    for (int k = 0; k < NEB; k++)
        if (EB[k].on) {
            int x = (int)(EB[k].x >> 8), y = (int)(EB[k].y >> 8);
            rect(x - 1, y - 2, 3, 5, 0xff5a8a);
            rect(x - 2, y - 1, 5, 3, 0xff5a8a);
            rect(x - 1, y - 1, 3, 3, 0xffffff);
        }
    for (int k = 0; k < NP; k++)
        if (PT[k].on)
            pxl((int)(PT[k].x >> 8), (int)(PT[k].y >> 8), PT[k].c);

    /* the ship, its charge glow and the beam */
    if (state == ST_PLAY || state == ST_TITLE) {
        int x = (int)(px >> 8), y = (int)(py >> 8);
        blink = inv > 0 && ((t >> 2) & 1);
        if (!blink) {
            if (tc_charging(&TC, now_ms) || TC.charge > 2560) {
                int r = 8 + TC.charge / 2560;
                i32 a = 64 + TC.charge / 100 / 2;
                rect(x - r, y - r, r * 2, r * 2,
                     mixc(sky_at(y), 0xff7a3d, a > 160 ? 160 : a));
            }
            draw_ship(x, y, speed);
        }
    }
    if (beam > 0) {
        int x = (int)(px >> 8), y = (int)(py >> 8), w = (int)beamw;
        rect(x - w / 2, 0, w, y - 8, 0x9fe8ff);
        rect(x - w / 4, 0, w / 2 ? w / 2 : 1, y - 8, 0xffffff);
    }

    /* 4. the near clouds, over everything */
    for (int i = 0; i < LAYN[2]; i++)
        cloud(&CL[2][i], &p, p.ca * LAYA[2] >> 8);

    /* 5. the storm flash */
    if (flash > 0)
        rect(0, 0, W, H, mixc(sky_at(H / 4), 0xffffff, 90));

    /* 6. the HUD and the overlays */
    hud(speed);
    if (banner > 0 && state == ST_PLAY)
        text_sc(60, 2, PH[phase], 0xffffff);
    if (state == ST_TITLE) {
        text_sc(100, 2, "SKY DESCENT", 0xffffff);
        text_sc(124, 1, "TAP A TO START", 0xffe08a);
        text_sc(144, 1, "DESCEND FROM 12000 M TO THE GROUND", 0xccffee);
    } else if (state == ST_DEAD) {
        text_sc(110, 2, "CRASHED", 0xffffff);
        text_u32((W - 7 * 8) / 2, 130, (unsigned)score, 7, 0xffffff);
        text_sc(145, 1, "TAP A TO RETRY", 0xffe08a);
    } else if (state == ST_WIN) {
        text_sc(110, 2, "TOUCHDOWN", 0xffffff);
        text_u32((W - 7 * 8) / 2, 130, (unsigned)score, 7, 0xffffff);
        text_sc(145, 1, "TAP A TO FLY AGAIN", 0xffe08a);
    }
}

/* --- start-up ------------------------------------------------------------------ */
static void game_init(void)
{
    rng = 7;                               /* fixed seed: a session replays exactly */
    for (int i = 0; i < NE; i++) E[i].on = 0;
    for (int i = 0; i < NB; i++) B[i].on = 0;
    for (int i = 0; i < NEB; i++) EB[i].on = 0;
    for (int i = 0; i < NP; i++) PT[i].on = 0;
    tc_init(&TC);
    px = (W / 2) * 256;
    py = (H - 20) * 256;
    gy = 0;
    inv = fcd = t = wt = beam = beamw = banner = flash = 0;
    v = SCROLL_AT_TOP;
    altitude = 12000;
    state = ST_TITLE;
    score = 0;
    lives = 3;
    phase = 0;
    boss_hp = 0;
    boss = 0;
    wt = 90;
    banner = 90;
    for (int l = 0; l < NC; l++)
        for (int i = 0; i < LAYN[l]; i++) {
            cloud_t *c = &CL[l][i];
            c->x = rnd() % W;
            c->y = (i32)(rnd() % (H + 40)) - 20;
            c->w = (30 + rnd() % 40) * LAYSC[l] >> 8;
            c->h = (12 + rnd() % 14) * LAYSC[l] >> 8;
            c->s = rnd() & 255;
        }
    for (int i = 0; i < STAR_COUNT; i++) {
        STARS[i][0] = (u16)(rnd() % W);
        STARS[i][1] = (u16)(rnd() % H);
        STARS[i][2] = rnd() & 255;
    }
}

int main(void)
{
    unsigned prev_pad = 0;

    build_ship();
    gfx_pal_upload(0, ship_pal, 6);
    game_init();

    for (;;) {
        unsigned pad = arcade1_pad();
        unsigned pressed = pad & ~prev_pad;

        prev_pad = pad;
        game_update(arcade1_timer_count(), (u16)pad, (u16)pressed);
        game_render();
        arcade1_present();
    }
    return 0;
}
