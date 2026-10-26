/* NEON TAP - a vector arena shooter for ARCADE-1.
 *
 * Everything on screen is lines, polygons and sparks drawn by the guest: no sprite
 * art, no assets. The machine has no line command and no blending, so a line is a
 * run of rects and "glow" is a dim wide pass under a bright thin pass.
 *
 * The signature mechanic is physical tap speed (rules identical to tapcad.h):
 *   - nothing held ........ slow auto-fire
 *   - hold A or B ......... regular fire
 *   - tap one button fast . fastest, widest fire
 *   - alternate A and B ... charge (the gun stops while you do)
 *   - START ............... spend the charge on a shockwave bomb
 *
 * Controls: D-pad moves AND aims (you fire the way you last moved), A / B fire,
 * START = bomb (during play); A / B start and restart from the title / game over.
 *
 * Freestanding RV32IM C, no libc, integer only, deterministic: time is the frame
 * timer, randomness is a seeded LCG, audio is a register.
 *
 * Observable globals (arcade1 run ... --vars):
 *   state, score, lives, mult, kills, tap_speed, charge, n_enemies, secs
 * States: 0 = title, 1 = playing, 2 = game over.
 */
#include "arcade1.h"
#include "text.h"

#define SW 480
#define SH 270
#define BOMB_BTN BTN_START             /* the third button (the live window maps START, not SELECT) */

#define RGB(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define C_BG      RGB(5, 5, 12)
#define C_FLASH   RGB(18, 26, 60)
#define C_WHITE   RGB(255, 255, 255)
#define C_HUD     RGB(200, 240, 255)
#define C_YELLOW  RGB(255, 224, 138)
#define C_ORANGE  RGB(255, 122, 61)
#define C_GRID0   RGB(18, 26, 90)
#define C_GRID1   RGB(40, 80, 200)
#define C_GRID2   RGB(150, 210, 255)

/* ---- observable state (file-scope globals, read by the HUD so -Os keeps them) ---- */
int state, score, lives, mult, kills, tap_speed, charge, n_enemies, secs;

/* ---- sine table, Q8, 256 steps per turn ---- */
static const short SINT[256] = {
#include "neontap_sin.inc"
};
#define SIN(i) ((int)SINT[(i) & 255])
#define COS(i) ((int)SINT[((i) + 64) & 255])

static unsigned isqrt(unsigned n) {
    unsigned r = 0, b = 1u << 30;
    while (b > n) b >>= 2;
    while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static unsigned rng_state = 5;
static int rnd(int n) { rng_state = rng_state * 1664525u + 1013904223u; return (int)(((rng_state >> 16) * (unsigned)n) >> 16); }

/* ---- tap cadence: same rules and constants as tapcad.h (times are ms) ---- */
static unsigned now_ms, tc_last, tc_chg_t, tc_tick;
static int tc_ema, tc_speed_raw, tc_btn, tc_has, tc_has_chg;
static void tc_reset(void) { tc_last = tc_chg_t = tc_tick = 0; tc_ema = tc_speed_raw = tc_btn = tc_has = tc_has_chg = 0; charge = 0; }
static void tc_press(int btn) {
    unsigned t = now_ms;
    if (tc_has) {
        int iv = (int)(t - tc_last);
        if (iv < 25) return;
        tc_ema = iv < 700 ? (tc_ema ? (tc_ema + iv) / 2 : iv) : 0;
        if (btn != tc_btn && iv < 400) {
            int q = clampi((400 - iv) * 256 / 330, 0, 256);
            charge = clampi(charge + 512 + 10 * q, 0, 25600);
            tc_chg_t = t; tc_has_chg = 1; tc_btn = btn; tc_last = t;
            return;
        }
    }
    tc_btn = btn; tc_last = t; tc_has = 1;
    tc_speed_raw = tc_ema ? clampi((300 - tc_ema) * 256 / 230, 0, 256) : 0;
}
static int tc_speed(void) {
    if (!tc_has) return 0;
    int idle = (int)(now_ms - tc_last), f = idle > 250 ? 256 - (idle - 250) * 256 / 350 : 256;
    return (tc_speed_raw * clampi(f, 0, 256)) >> 8;
}
static int tc_charging(void) { return tc_has_chg && (int)(now_ms - tc_chg_t) < 350; }
static void tc_frame(void) {
    unsigned dt = now_ms - tc_tick; tc_tick = now_ms;
    if (charge > 0 && tc_has && now_ms - tc_last > 500) { unsigned d = dt * 512u / 100u; charge = d >= (unsigned)charge ? 0 : charge - (int)d; }
}

/* ---- entity pools (positions in Q8 pixels) ---- */
enum { K_SEEK, K_DRIFT, K_SPLIT, K_MINI, K_PIN, K_KINDS };
typedef struct { int x, y, vx, vy; short hp, ph, a; unsigned char k, on; } enemy;
typedef struct { int x, y, vx, vy; short life; unsigned char on; } bullet;
typedef struct { int x, y; short life; unsigned char k, on; } marker;
typedef struct { int x, y, r, R; short life; unsigned char on; } ring;
typedef struct { int x, y, vx, vy; short life; unsigned short col; unsigned char on; } spark;
#define NE 64
#define NB 96
#define NM 32
#define NR 4
#define NS 96
static enemy E[NE]; static bullet B[NB]; static marker M[NM]; static ring R[NR]; static spark S[NS];

typedef struct { short r, hp, pts; unsigned short core, glow; } kind_t;
static const kind_t KIND[K_KINDS] = {
    { 7, 1, 100, RGB(255, 63, 164), RGB(90, 20, 60) },    /* seeker  : magenta diamond   */
    { 7, 1, 150, RGB(255, 160, 48), RGB(90, 55, 15) },    /* drifter : orange square     */
    { 9, 2, 300, RGB(64, 255, 144), RGB(15, 80, 45) },    /* splitter: green hexagon     */
    { 5, 1,  60, RGB(64, 255, 144), RGB(15, 80, 45) },    /* mini    : small hexagon     */
    { 8, 3, 400, RGB(176, 128, 255), RGB(55, 40, 90) },   /* pinwheel: purple spinner    */
};

/* ---- player ---- */
static int px, py, pvx, pvy, aim, inv, fcd, mult_t, spawn_cd, swarm_cd, frame_t, shake, flash, msg_t, over_t;
static unsigned prev_pad;

/* ---- warping grid: spring lattice, pushed by shots, kills, hits and bombs ---- */
#define GX 17
#define GY 10
#define GSP 30
static int gx[GX * GY], gy[GX * GY], gvx[GX * GY], gvy[GX * GY];
static void grid_init(void) { for (int j = 0; j < GY; j++) for (int i = 0; i < GX; i++) { int k = j * GX + i; gx[k] = i * GSP * 256; gy[k] = j * GSP * 256; gvx[k] = gvy[k] = 0; } }
static void grid_step(void) {
    for (int j = 1; j < GY - 1; j++) for (int i = 1; i < GX - 1; i++) {
        int k = j * GX + i, ox = i * GSP * 256, oy = j * GSP * 256;
        int ax = (gx[k - 1] + gx[k + 1] + gx[k - GX] + gx[k + GX]) / 4, ay = (gy[k - 1] + gy[k + 1] + gy[k - GX] + gy[k + GX]) / 4;
        gvx[k] += ((ox - gx[k]) >> 4) + ((ax - gx[k]) >> 3); gvy[k] += ((oy - gy[k]) >> 4) + ((ay - gy[k]) >> 3);
        gvx[k] -= gvx[k] >> 4; gvy[k] -= gvy[k] >> 4;
    }
    for (int j = 1; j < GY - 1; j++) for (int i = 1; i < GX - 1; i++) {
        int k = j * GX + i, ox = i * GSP * 256, oy = j * GSP * 256;
        gx[k] = clampi(gx[k] + gvx[k], ox - 30 * 256, ox + 30 * 256); gy[k] = clampi(gy[k] + gvy[k], oy - 30 * 256, oy + 30 * 256);
    }
}
/* push grid points outward from (x,y) (pixels); str in Q8 px/frame at the centre */
static void grid_warp(int x, int y, int str, int rad) {
    for (int j = 1; j < GY - 1; j++) for (int i = 1; i < GX - 1; i++) {
        int k = j * GX + i, dx = (gx[k] >> 8) - x, dy = (gy[k] >> 8) - y;
        if (iabs(dx) > rad || iabs(dy) > rad) continue;
        int d = (int)isqrt((unsigned)(dx * dx + dy * dy)); if (d >= rad) continue; if (!d) d = 1;
        int f = str * (rad - d) / rad; gvx[k] += dx * f / d; gvy[k] += dy * f / d;
    }
}
static void grid_ring(int x, int y, int r, int th, int str) {
    for (int j = 1; j < GY - 1; j++) for (int i = 1; i < GX - 1; i++) {
        int k = j * GX + i, dx = (gx[k] >> 8) - x, dy = (gy[k] >> 8) - y;
        int d = (int)isqrt((unsigned)(dx * dx + dy * dy)); if (!d) d = 1;
        int e = iabs(d - r); if (e >= th) continue;
        int f = str * (th - e) / th; gvx[k] += dx * f / d; gvy[k] += dy * f / d;
    }
}

/* ---- drawing: lines are runs of rects ---- */
static int shx, shy;                       /* screen shake, added to every world draw */
static void line(int x0, int y0, int x1, int y1, int t, unsigned col) {
    x0 += shx; x1 += shx; y0 += shy; y1 += shy;
    int dx = iabs(x1 - x0), dy = iabs(y1 - y0), h = t >> 1;
    if (dx >= dy) {                         /* x-major: runs of constant y */
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx / 2, y = y0, rs = x0, x = x0;
        for (int i = 0; i <= dx; i++, x += sx) {
            int last = (i == dx), step = 0;
            err -= dy; if (err < 0) { err += dx; step = 1; }
            if (step || last) { int a = rs < x ? rs : x, w = iabs(x - rs) + 1; arcade1_rect(a, y - h, w, t, col); rs = x + sx; y += sy; }
        }
    } else {                                /* y-major: runs of constant x */
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dy / 2, x = x0, rs = y0, y = y0;
        for (int i = 0; i <= dy; i++, y += sy) {
            int last = (i == dy), step = 0;
            err -= dx; if (err < 0) { err += dy; step = 1; }
            if (step || last) { int a = rs < y ? rs : y, hh = iabs(y - rs) + 1; arcade1_rect(x - h, a, t, hh, col); rs = y + sy; x += sx; }
        }
    }
}
static unsigned dim(unsigned c) { return (c >> 1) & 0x7BEFu; }       /* half brightness, RGB565 */
static void glow_line(int x0, int y0, int x1, int y1, unsigned core, unsigned glow) { line(x0, y0, x1, y1, 5, dim(glow)); line(x0, y0, x1, y1, 3, glow); line(x0, y0, x1, y1, 1, core); }
/* regular polygon, angle a in 1/256 turns */
static void poly(int cx, int cy, int r, int n, int a, unsigned core, unsigned glow) {
    int vx[8], vy[8];
    for (int i = 0; i < n; i++) { int q = a + i * 256 / n; vx[i] = cx + ((COS(q) * r) >> 8); vy[i] = cy + ((SIN(q) * r) >> 8); }
    for (int i = 0; i < n; i++) line(vx[i], vy[i], vx[(i + 1) % n], vy[(i + 1) % n], 5, dim(glow));
    for (int i = 0; i < n; i++) line(vx[i], vy[i], vx[(i + 1) % n], vy[(i + 1) % n], 3, glow);
    for (int i = 0; i < n; i++) line(vx[i], vy[i], vx[(i + 1) % n], vy[(i + 1) % n], 1, core);
}
static void circle(int cx, int cy, int r, int t, unsigned col) {
    int n = r > 60 ? 32 : 20, px0 = cx + r, py0 = cy;
    for (int i = 1; i <= n; i++) { int q = i * 256 / n, x = cx + ((COS(q) * r) >> 8), y = cy + ((SIN(q) * r) >> 8); line(px0, py0, x, y, t, col); px0 = x; py0 = y; }
}

/* ---- sound: one voice, a tiny queue ---- */
#define AUDIO_FREQ (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x300u))
#define AUDIO_CTRL (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x304u))
static int snd_f, snd_n, snd_sweep, snd_pri;
static void snd(int f, int n, int sweep, int pri) { if (snd_n > 0 && pri < snd_pri) return; snd_f = f; snd_n = n; snd_sweep = sweep; snd_pri = pri; }
static void snd_frame(void) {
    if (snd_n > 0) { AUDIO_FREQ = (unsigned)snd_f; __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1; snd_f += snd_sweep; if (snd_f < 40) snd_f = 40; snd_n--; }
    else if (tc_charging() && state == 1) { AUDIO_FREQ = (unsigned)(160 + (charge >> 5)); __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1; }
    else AUDIO_CTRL = 0;
}

/* ---- game logic ---- */
static void burst(int x, int y, unsigned col, int n, int sp) {
    for (int i = 0; i < n; i++) for (int k = 0; k < NS; k++) if (!S[k].on) {
        int a = rnd(256), s = sp / 2 + rnd(sp / 2 + 1);
        S[k].x = x << 8; S[k].y = y << 8; S[k].vx = COS(a) * s >> 8; S[k].vy = SIN(a) * s >> 8; S[k].life = 18 + rnd(20); S[k].col = (unsigned short)col; S[k].on = 1; break;
    }
}
static void spawn_enemy(int k, int x, int y) {
    for (int i = 0; i < NE; i++) if (!E[i].on) {
        E[i].on = 1; E[i].k = (unsigned char)k; E[i].x = x << 8; E[i].y = y << 8; E[i].vx = E[i].vy = 0; E[i].hp = KIND[k].hp; E[i].ph = (short)rnd(256); E[i].a = 0;
        if (k == K_DRIFT) { int a = rnd(256); E[i].vx = COS(a) * 276 >> 8; E[i].vy = SIN(a) * 276 >> 8; }
        return;
    }
}
static void queue(int k) {
    int x = 0, y = 0;
    for (int tries = 0; tries < 10; tries++) {
        int s = rnd(4);
        x = s < 2 ? 8 + rnd(SW - 16) : (s == 2 ? 8 : SW - 8); y = s >= 2 ? 8 + rnd(SH - 16) : (s == 0 ? 8 : SH - 8);
        int dx = x - (px >> 8), dy = y - (py >> 8); if (dx * dx + dy * dy > 96 * 96) break;
    }
    for (int i = 0; i < NM; i++) if (!M[i].on) { M[i].on = 1; M[i].k = (unsigned char)k; M[i].x = x; M[i].y = y; M[i].life = 45; return; }
}
static void kill_enemy(enemy *e, int bonus_x256) {
    const kind_t *d = &KIND[e->k]; int x = e->x >> 8, y = e->y >> 8;
    burst(x, y, d->core, e->k == K_SPLIT ? 22 : 12, 700); shake = shake > 3 ? shake : 3;
    grid_warp(x, y, 1100, 54);
    mult = mult + 64 > 2560 ? 2560 : mult + 64; mult_t = 110;
    score += d->pts * (mult >> 8) * bonus_x256 / 256; kills++;
    snd(900 + 40 * (mult >> 8), 3, -60, 1);
    if (e->k == K_SPLIT) for (int i = 0; i < 3; i++) { spawn_enemy(K_MINI, x, y); for (int q = NE - 1; q >= 0; q--) if (E[q].on && E[q].k == K_MINI && !E[q].vx && !E[q].vy) { int a = i * 85 + rnd(40); E[q].vx = COS(a) * 369 >> 8; E[q].vy = SIN(a) * 369 >> 8; break; } }
    e->on = 0;
}
static void add_ring(int x, int y, int R_, int kill) { for (int i = 0; i < NR; i++) if (!R[i].on) { R[i].on = 1; R[i].x = x; R[i].y = y; R[i].r = kill ? 2 : 0; R[i].R = R_; R[i].life = kill ? 34 : 24; return; } }
static void hit_player(void) {
    grid_warp(px >> 8, py >> 8, 2600, 120); lives--; inv = 130; shake = 7; flash = 3; burst(px >> 8, py >> 8, C_WHITE, 40, 900);
    add_ring(px >> 8, py >> 8, 72, 1); mult = 256; snd(300, 14, -14, 5);
    if (lives <= 0) { state = 2; over_t = 0; snd(220, 40, -4, 9); }
}
static void reset_game(void) {
    memset(E, 0, sizeof E); memset(B, 0, sizeof B); memset(M, 0, sizeof M); memset(R, 0, sizeof R); memset(S, 0, sizeof S);
    state = 1; score = 0; lives = 3; mult = 256; kills = 0; px = (SW / 2) << 8; py = (SH / 2) << 8; pvx = pvy = 0; aim = 192;
    inv = 0; fcd = 0; mult_t = 0; spawn_cd = 60; swarm_cd = 1500; frame_t = 0; shake = flash = msg_t = 0; rng_state = 5; tc_reset(); snd_n = 0;
}
static void bomb(void) {
    if (charge < 2048) return;
    int c = charge / 100;                                  /* 0..256 */
    charge = 0; add_ring(px >> 8, py >> 8, 54 + (180 * c >> 8), 1);
    shake = 4 + (6 * c >> 8); flash = 2; snd(700, 22, -26, 6);
}

static void step_play(unsigned pad, unsigned edge) {
    frame_t++;
    if (edge & BTN_A) tc_press(0);
    if (edge & BTN_B) tc_press(1);
    tc_frame();
    if (edge & BOMB_BTN) bomb();
    int mx = ((pad & BTN_RIGHT) ? 1 : 0) - ((pad & BTN_LEFT) ? 1 : 0), my = ((pad & BTN_DOWN) ? 1 : 0) - ((pad & BTN_UP) ? 1 : 0);
    int tx = mx * 512, ty = my * 512; if (mx && my) { tx = tx * 181 >> 8; ty = ty * 181 >> 8; }
    pvx += (tx - pvx) >> 2; pvy += (ty - pvy) >> 2;
    px = clampi(px + pvx, 10 << 8, (SW - 10) << 8); py = clampi(py + pvy, 10 << 8, (SH - 10) << 8);
    if (mx || my) { static const unsigned char A8[9] = { 224, 192, 160, 0, 0, 128, 32, 64, 96 }; aim = A8[(my + 1) * 3 + (mx + 1)]; }
    int s = tc_speed(); tap_speed = s;
    int chg = tc_charging(), hold = (pad & (BTN_A | BTN_B)) != 0;
    if (--fcd <= 0 && !chg) {
        int base = (hold || s > 13) ? 9 : 22; fcd = (base * 256 - (base - 3) * s + 128) >> 8;
        int n = 1 + (s > 90) + (s > 180); static const signed char OFF[3][3] = { { 0, 0, 0 }, { -4, 4, 0 }, { -9, 0, 9 } };
        for (int j = 0; j < n; j++) for (int k = 0; k < NB; k++) if (!B[k].on) {
            int a = aim + OFF[n - 1][j]; B[k].on = 1; B[k].life = 60;
            B[k].x = px + (COS(aim) * 8); B[k].y = py + (SIN(aim) * 8); B[k].vx = COS(a) * 6; B[k].vy = SIN(a) * 6; break;
        }
    }
    /* spawning: difficulty ramps with time */
    int d = clampi(frame_t * 256 / (60 * 150), 0, 256);
    if (--spawn_cd <= 0) {
        spawn_cd = 60 - (40 * d >> 8); int n = 1 + (rnd(256) < d / 2) + (rnd(256) < d * 30 / 100);
        for (int i = 0; i < n; i++) { int r = rnd(100), k = K_SEEK;
            if (frame_t > 1200) { if (r < 55) k = K_SEEK; else if (r < 80) k = K_DRIFT; else if (r < 93 && frame_t > 2700) k = K_SPLIT; else if (frame_t > 4500) k = K_PIN; }
            queue(k); }
    }
    if (--swarm_cd <= 0) {
        swarm_cd = 1500; msg_t = 100; snd(120, 18, 6, 3); int a0 = rnd(256);
        for (int i = 0; i < 14; i++) for (int k = 0; k < NM; k++) if (!M[k].on) { int a = a0 + i * 256 / 14; M[k].on = 1; M[k].k = K_SEEK; M[k].life = 60;
            M[k].x = clampi(SW / 2 + (COS(a) * (SW / 2 - 14) >> 8), 8, SW - 8); M[k].y = clampi(SH / 2 + (SIN(a) * (SH / 2 - 14) >> 8), 8, SH - 8); break; }
    }
    for (int i = 0; i < NM; i++) if (M[i].on && --M[i].life <= 0) { spawn_enemy(M[i].k, M[i].x, M[i].y); M[i].on = 0; }
    for (int k = 0; k < NB; k++) if (B[k].on) { B[k].x += B[k].vx; B[k].y += B[k].vy; grid_warp(B[k].x >> 8, B[k].y >> 8, 40, 22);
        if (--B[k].life <= 0 || B[k].x < 0 || B[k].x > SW << 8 || B[k].y < 0 || B[k].y > SH << 8) B[k].on = 0; }
    int ne = 0;
    for (int i = 0; i < NE; i++) {
        enemy *e = &E[i]; if (!e->on) continue; ne++;
        e->ph += 20; e->a += 10;
        int dx = px - e->x, dy = py - e->y, dist = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
        int ux = (dx >> 8) * 256 / dist, uy = (dy >> 8) * 256 / dist;
        if (e->k == K_SEEK) { int sp = 169 + (169 * d >> 8); e->vx += (ux * sp / 256 - e->vx) >> 5; e->vy += (uy * sp / 256 - e->vy) >> 5; }
        else if (e->k == K_SPLIT) { e->vx += (ux * 138 / 256 - e->vx) >> 5; e->vy += (uy * 138 / 256 - e->vy) >> 5; }
        else if (e->k == K_MINI) { e->vx += (ux * 184 / 256 - e->vx) >> 6; e->vy += (uy * 184 / 256 - e->vy) >> 6; }
        else if (e->k == K_PIN) { int off = (SIN(e->ph) * 45) >> 8, bx = COS(off) * ux / 256 - SIN(off) * uy / 256, by = SIN(off) * ux / 256 + COS(off) * uy / 256;
            e->vx += (bx * 246 / 256 - e->vx) >> 4; e->vy += (by * 246 / 256 - e->vy) >> 4; }
        e->x += e->vx; e->y += e->vy;
        if (e->k == K_DRIFT) { int r = KIND[e->k].r; if ((e->x >> 8) < r || (e->x >> 8) > SW - r) e->vx = -e->vx; if ((e->y >> 8) < r || (e->y >> 8) > SH - r) e->vy = -e->vy; }
        int er = KIND[e->k].r, ex = e->x >> 8, ey = e->y >> 8;
        for (int k = 0; k < NB; k++) if (B[k].on) { int bx = (B[k].x >> 8) - ex, by = (B[k].y >> 8) - ey;
            if (bx * bx + by * by < (er + 2) * (er + 2)) { B[k].on = 0; if (--e->hp <= 0) { kill_enemy(e, 256); break; } else burst(B[k].x >> 8, B[k].y >> 8, C_WHITE, 3, 300); } }
        if (!e->on) continue;
        if (inv <= 0) { int bx = (px >> 8) - ex, by = (py >> 8) - ey; if (bx * bx + by * by < (er + 5) * (er + 5)) { hit_player(); if (state != 1) return; kill_enemy(e, 0); } }
    }
    n_enemies = ne;
    for (int i = 0; i < NR; i++) if (R[i].on) {
        R[i].r += (((R[i].R - R[i].r) * 46) >> 8) + 2; grid_ring(R[i].x, R[i].y, R[i].r, 28, 700);
        for (int q = 0; q < NE; q++) if (E[q].on) { int dx = (E[q].x >> 8) - R[i].x, dy = (E[q].y >> 8) - R[i].y, rr = R[i].r + KIND[E[q].k].r;
            if (dx * dx + dy * dy < rr * rr) kill_enemy(&E[q], 256); }
        if (--R[i].life <= 0) R[i].on = 0;
    }
    for (int k = 0; k < NS; k++) if (S[k].on) { S[k].x += S[k].vx; S[k].y += S[k].vy; S[k].vx -= S[k].vx >> 5; S[k].vy -= S[k].vy >> 5; if (--S[k].life <= 0) S[k].on = 0; }
    if (inv > 0) inv--;
    if (mult_t > 0 && --mult_t == 0) mult = 256;
    if (frame_t % 60 == 0) secs = frame_t / 60;
}

/* ---- rendering ---- */
static void draw_world(void) {
    int sh = shake > 0 ? shake : 0; shx = sh ? rnd(2 * sh + 1) - sh : 0; shy = sh ? rnd(2 * sh + 1) - sh : 0;
    int s = tap_speed;
    /* grid: three brightness levels; points that are displaced light up */
    for (int j = 0; j < GY; j++) for (int i = 0; i < GX; i++) {
        int k = j * GX + i, dd = iabs(gx[k] - i * GSP * 256) + iabs(gy[k] - j * GSP * 256);
        unsigned col = dd > 5 * 256 ? C_GRID2 : (dd > 256 ? C_GRID1 : (s > 150 ? C_GRID1 : C_GRID0));
        if (i + 1 < GX) line(gx[k] >> 8, gy[k] >> 8, gx[k + 1] >> 8, gy[k + 1] >> 8, 1, col);
        if (j + 1 < GY) line(gx[k] >> 8, gy[k] >> 8, gx[k + GX] >> 8, gy[k + GX] >> 8, 1, col);
    }
    glow_line(2, 2, SW - 3, 2, C_GRID1, C_GRID0); glow_line(SW - 3, 2, SW - 3, SH - 3, C_GRID1, C_GRID0);
    glow_line(SW - 3, SH - 3, 2, SH - 3, C_GRID1, C_GRID0); glow_line(2, SH - 3, 2, 2, C_GRID1, C_GRID0);
    for (int i = 0; i < NM; i++) if (M[i].on) poly(M[i].x, M[i].y, 4 + M[i].life / 3, 4, M[i].life * 5, KIND[M[i].k].core, KIND[M[i].k].glow);
    for (int i = 0; i < NE; i++) if (E[i].on) { enemy *e = &E[i]; const kind_t *d = &KIND[e->k]; int x = e->x >> 8, y = e->y >> 8;
        if (e->k == K_SEEK) { int a = 0; if (e->vx || e->vy) { a = 0; int bx = e->vx, by = e->vy; a = (iabs(bx) > iabs(by)) ? (bx > 0 ? 0 : 128) : (by > 0 ? 64 : 192); } poly(x, y, d->r, 4, a, d->core, d->glow); }
        else if (e->k == K_DRIFT) poly(x, y, d->r, 4, e->a + 32, d->core, d->glow);
        else if (e->k == K_SPLIT || e->k == K_MINI) poly(x, y, d->r, 6, e->a, d->core, d->glow);
        else for (int q = 0; q < 4; q++) { int a = e->a * 2 + q * 64; glow_line(x, y, x + (COS(a) * d->r >> 8), y + (SIN(a) * d->r >> 8), d->core, d->glow); glow_line(x + (COS(a) * d->r >> 8), y + (SIN(a) * d->r >> 8), x + (COS(a + 40) * d->r * 6 / 10 >> 8), y + (SIN(a + 40) * d->r * 6 / 10 >> 8), d->core, d->glow); } }
    for (int k = 0; k < NB; k++) if (B[k].on) { int x = B[k].x >> 8, y = B[k].y >> 8; line(x, y, x - (B[k].vx >> 7), y - (B[k].vy >> 7), 3, RGB(90, 75, 25)); line(x, y, x - (B[k].vx >> 8), y - (B[k].vy >> 8), 1, RGB(255, 246, 160)); }
    for (int k = 0; k < NS; k++) if (S[k].on) line(S[k].x >> 8, S[k].y >> 8, (S[k].x - S[k].vx * 3) >> 8, (S[k].y - S[k].vy * 3) >> 8, 1, S[k].col);
    for (int i = 0; i < NR; i++) if (R[i].on) { circle(R[i].x, R[i].y, R[i].r, 5, RGB(30, 90, 120)); circle(R[i].x, R[i].y, R[i].r, 2, RGB(159, 232, 255)); }
    if (state == 1 || state == 0) {
        int x = px >> 8, y = py >> 8;
        if (!(inv > 0 && ((frame_t >> 2) & 1))) {
            if (charge > 400) { int rr = 14 + (charge >> 9); circle(x, y, rr, 2, charge > 24000 ? C_WHITE : C_ORANGE); }
            unsigned core = s > 150 ? C_WHITE : RGB(159, 255, 224), glow = RGB(25, 80, 70);
            int fx = x + (COS(aim) * 13 >> 8), fy = y + (SIN(aim) * 13 >> 8);
            int lx = x + (COS(aim + 106) * 11 >> 8), ly = y + (SIN(aim + 106) * 11 >> 8), rx = x + (COS(aim - 106) * 11 >> 8), ry = y + (SIN(aim - 106) * 11 >> 8);
            int bx = x - (COS(aim) * 4 >> 8), by = y - (SIN(aim) * 4 >> 8);
            glow_line(fx, fy, lx, ly, core, glow); glow_line(lx, ly, bx, by, core, glow); glow_line(bx, by, rx, ry, core, glow); glow_line(rx, ry, fx, fy, core, glow);
        }
    }
    shx = shy = 0;
}
static void draw_hud(void) {
    arcade1_cam(0, 0);
    text_u32(8, 6, (unsigned)score, 8, C_WHITE);
    text_draw(8, 16, "X", C_YELLOW); text_u32(16, 16, (unsigned)(mult >> 8), 2, C_YELLOW);
    text_draw(SW - 96, 6, "LIVES", C_HUD); for (int i = 0; i < lives; i++) arcade1_rect(SW - 48 + i * 10, 7, 6, 6, RGB(159, 255, 224));
    text_u32(SW - 40, 16, (unsigned)secs / 60, 2, RGB(120, 150, 170)); text_draw(SW - 24, 16, ":", RGB(120, 150, 170)); text_u32(SW - 16, 16, (unsigned)secs % 60, 2, RGB(120, 150, 170));
    text_draw(8, SH - 30, "TAP", RGB(150, 170, 190)); arcade1_rect(40, SH - 29, 100, 6, RGB(20, 20, 50)); arcade1_rect(40, SH - 29, 100 * tap_speed >> 8, 6, C_YELLOW);
    text_draw(8, SH - 18, "PWR", RGB(150, 170, 190)); arcade1_rect(40, SH - 17, 100, 6, RGB(20, 20, 50)); arcade1_rect(40, SH - 17, 100 * charge / 25600, 6, charge >= 25000 ? C_WHITE : C_ORANGE);
    if (msg_t > 0 && state == 1) { msg_t--; text_draw(SW / 2 - 16, 44, "SWARM", RGB(255, 63, 164)); }
}

int main(void) {
    grid_init(); state = 0; tc_reset(); reset_game(); state = 0;
    for (;;) {
        unsigned pad = arcade1_pad(), edge = pad & ~prev_pad; prev_pad = pad;
        now_ms = (arcade1_timer_count() * 50u) / 3u;
        grid_step();
        if (state == 1) step_play(pad, edge);
        else {
            if (state == 0) { frame_t++; tap_speed = 0; }
            if (state == 2) { over_t++; frame_t++; }
            for (int k = 0; k < NS; k++) if (S[k].on) { S[k].x += S[k].vx; S[k].y += S[k].vy; if (--S[k].life <= 0) S[k].on = 0; }
            if ((edge & (BTN_A | BTN_B | BTN_START)) && (state == 0 || over_t > 45)) reset_game();
        }
        if (shake > 0) shake = shake * 3 / 4;
        arcade1_cam(0, 0);
        arcade1_clear(flash > 0 ? C_FLASH : C_BG); if (flash > 0) flash--;
        draw_world(); draw_hud();
        if (state == 0) { text_draw(SW / 2 - 32, 96, "NEON TAP", C_WHITE); text_draw(SW / 2 - 56, 120, "TAP A TO START", C_YELLOW); text_draw(SW / 2 - 92, 140, "TAP FAST TO OUT-SHOOT THE SWARM", RGB(120, 150, 170)); }
        if (state == 2) { text_draw(SW / 2 - 36, 100, "GAME OVER", C_WHITE); text_u32(SW / 2 - 32, 118, (unsigned)score, 8, C_HUD); text_draw(SW / 2 - 56, 140, "TAP A TO RETRY", C_YELLOW); }
        snd_frame();
        arcade1_present();
    }
}
