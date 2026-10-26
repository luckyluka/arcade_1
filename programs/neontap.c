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
 *   state, score, lives, mult, kills, tap_speed, charge, n_enemies, secs,
 *   kinds_seen (bit per enemy kind, once it has appeared this session),
 *   kinds_killed (bit per enemy kind, once one has been killed this session),
 *   eh_fired (turret shots taken), mines_dropped (bomber mines laid)
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
int kinds_seen, kinds_killed, eh_fired, mines_dropped;   /* session-cumulative, not reset */

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
enum { K_SEEK, K_DRIFT, K_SPLIT, K_MINI, K_PIN, K_TURRET, K_SHIELD, K_BOMBER, K_WEAVER, K_WSEG, K_PHANTOM, K_KINDS };
typedef struct { int x, y, vx, vy; short hp, ph, a, t; unsigned char k, on; } enemy;
typedef struct { int x, y, vx, vy; short life; unsigned char on; } bullet;
typedef struct { int x, y; short life; unsigned char k, on; } marker;
typedef struct { int x, y, r, R; short life; unsigned char on; } ring;
typedef struct { int x, y, vx, vy; short life; unsigned short col; unsigned char on; } spark;
#define NE 64
#define NB 96
#define NM 32
#define NR 4
#define NS 96
#define NF 16                                       /* enemy shots (turret)        */
#define NN 8                                         /* bomber mines                */
#define NW 5                                         /* weaver chain segments       */
#define WTR 96                                       /* weaver trail length (frames) */
static enemy E[NE]; static bullet B[NB]; static marker M[NM]; static ring R[NR]; static spark S[NS];
static bullet F[NF]; static marker MN[NN];
static int wcx[WTR], wcy[WTR];                      /* weaver head trail, ring buffer */
static int wt, wcount;                              /* trail write index, segments spawned */

typedef struct { short r, hp, pts; unsigned short core, glow; } kind_t;
static const kind_t KIND[K_KINDS] = {
    { 7, 1, 100, RGB(255, 63, 164), RGB(90, 20, 60) },    /* seeker  : magenta diamond   */
    { 7, 1, 150, RGB(255, 160, 48), RGB(90, 55, 15) },    /* drifter : orange square     */
    { 9, 2, 300, RGB(64, 255, 144), RGB(15, 80, 45) },    /* splitter: green hexagon     */
    { 5, 1,  60, RGB(64, 255, 144), RGB(15, 80, 45) },    /* mini    : small hexagon     */
    { 8, 3, 400, RGB(176, 128, 255), RGB(55, 40, 90) },   /* pinwheel: purple spinner    */
    { 8, 3, 300, RGB(80, 240, 255), RGB(20, 70, 80) },    /* turret  : cyan triangle     */
    { 8, 3, 350, RGB(255, 63, 164), RGB(90, 20, 60) },    /* shield  : ringed seeker     */
    { 8, 2, 200, RGB(235, 245, 255), RGB(70, 80, 90) },   /* bomber  : white cross       */
    { 9, 6, 600, RGB(255, 110, 200), RGB(90, 30, 70) },   /* weaver  : chain head        */
    { 4, 1,  40, RGB(255, 110, 200), RGB(90, 30, 70) },   /* wseg    : chain segment     */
    { 7, 2, 400, RGB(210, 220, 255), RGB(60, 65, 90) },   /* phantom : pale diamond      */
};

/* ---- player ---- */
static int px, py, pvx, pvy, aim, inv, fcd, mult_t, spawn_cd, swarm_cd, frame_t, shake, flash, msg_t, over_t;
static int unlock_t;                              /* play frames this SESSION: survives death (gates the vocabulary) */
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
/* Vocabulary gates: cumulative PLAY frames this session (unlock_t, which
 * survives death — meeting an enemy once keeps it unlocked for the run).
 * Weights are the spawn mix once a gate is open; 0 = never queued directly
 * (minis come from splitters, segments from weavers). The ladder is a
 * compression of the design document's sector pacing — M1 has no sectors
 * yet (docs/experiments/neontap-vocabulary-gates.md). Gates sit late enough
 * that game 1 keeps its shipped difficulty; force-intro below still
 * guarantees every kind appears once its gate opens, across deaths.
 */
static const short GATE[K_KINDS] = { 0, 600, 900, 0, 1200, 1600, 1700, 1800, 1950, 0, 2100 };
static const unsigned char WGT[K_KINDS] = { 40, 18, 8, 0, 7, 7, 7, 6, 3, 0, 3 };

static int kind_pending(int k) { for (int i = 0; i < NM; i++) if (M[i].on && M[i].k == k) return 1; return 0; }
static int weaver_alive(void) { for (int i = 0; i < NE; i++) if (E[i].on && E[i].k == K_WEAVER) return 1; return 0; }
/* the phantom is only there half the time: visible, hittable and dangerous
 * only when its flicker phase is on */
static int evis(const enemy *e) { return e->k != K_PHANTOM || ((e->ph / 300) & 1); }

static void spawn_enemy(int k, int x, int y) {
    for (int i = 0; i < NE; i++) if (!E[i].on) {
        E[i].on = 1; E[i].k = (unsigned char)k; E[i].x = x << 8; E[i].y = y << 8; E[i].vx = E[i].vy = 0; E[i].hp = KIND[k].hp; E[i].ph = (short)rnd(256); E[i].a = 0; E[i].t = 0;
        kinds_seen |= 1 << k;
        if (k == K_DRIFT || k == K_BOMBER) { int a = rnd(256); E[i].vx = COS(a) * 276 >> 8; E[i].vy = SIN(a) * 276 >> 8; }
        if (k == K_BOMBER) E[i].t = 60;                                 /* first mine after 1 s */
        if (k == K_TURRET) E[i].t = (short)(50 + rnd(60));            /* first aimed shot */
        if (k == K_WEAVER) {                                           /* head lays its own trail, then the chain */
            for (int q = 0; q < WTR; q++) { wcx[q] = x << 8; wcy[q] = y << 8; }
            wt = 0; wcount = 0; E[i].t = (short)rnd(256);              /* heading */
            for (int q = 0; q < NW; q++) spawn_enemy(K_WSEG, x, y);
        }
        if (k == K_WSEG) E[i].t = (short)(wcount++);                    /* chain position: 0 = just behind head */
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
    kinds_killed |= 1 << e->k;
    snd(900 + 40 * (mult >> 8), 3, -60, 1);
    if (e->k == K_SPLIT) for (int i = 0; i < 3; i++) { spawn_enemy(K_MINI, x, y); for (int q = NE - 1; q >= 0; q--) if (E[q].on && E[q].k == K_MINI && !E[q].vx && !E[q].vy) { int a = i * 85 + rnd(40); E[q].vx = COS(a) * 369 >> 8; E[q].vy = SIN(a) * 369 >> 8; break; } }
    e->on = 0;
    /* weaver: kill the head and the chain dies; kill the last segment and the
     * head dies with it (the segments themselves are worth only their points) */
    if (e->k == K_WEAVER) for (int i = 0; i < NE; i++) if (E[i].on && E[i].k == K_WSEG) kill_enemy(&E[i], 0);
    if (e->k == K_WSEG) { int left = 0, head = -1;
        for (int i = 0; i < NE; i++) if (E[i].on) { if (E[i].k == K_WSEG) left++; if (E[i].k == K_WEAVER) head = i; }
        if (!left && head >= 0) kill_enemy(&E[head], 256); }
}
static void add_ring(int x, int y, int R_, int kill) { for (int i = 0; i < NR; i++) if (!R[i].on) { R[i].on = 1; R[i].x = x; R[i].y = y; R[i].r = kill ? 2 : 0; R[i].R = R_; R[i].life = kill ? 34 : 24; return; } }
static void hit_player(void) {
    grid_warp(px >> 8, py >> 8, 2600, 120); lives--; inv = 130; shake = 7; flash = 3; burst(px >> 8, py >> 8, C_WHITE, 40, 900);
    add_ring(px >> 8, py >> 8, 72, 1); mult = 256; snd(300, 14, -14, 5);
    if (lives <= 0) { state = 2; over_t = 0; snd(220, 40, -4, 9); }
}
static void reset_game(void) {
    memset(E, 0, sizeof E); memset(B, 0, sizeof B); memset(M, 0, sizeof M); memset(R, 0, sizeof R); memset(S, 0, sizeof S);
    memset(F, 0, sizeof F); memset(MN, 0, sizeof MN); wt = 0; wcount = 0;
    state = 1; score = 0; lives = 3; mult = 256; kills = 0; px = (SW / 2) << 8; py = (SH / 2) << 8; pvx = pvy = 0; aim = 192;
    inv = 0; fcd = 0; mult_t = 0; spawn_cd = 60; swarm_cd = 1500; frame_t = 0; shake = flash = msg_t = 0; rng_state = 5; tc_reset(); snd_n = 0;
    /* kinds_seen / kinds_killed / eh_fired / mines_dropped / unlock_t are
     * SESSION-cumulative: a death does not un-learn the vocabulary */
}
static void bomb(void) {
    if (charge < 2048) return;
    int c = charge / 100;                                  /* 0..256 */
    charge = 0; add_ring(px >> 8, py >> 8, 54 + (180 * c >> 8), 1);
    shake = 4 + (6 * c >> 8); flash = 2; snd(700, 22, -26, 6);
}

static void step_play(unsigned pad, unsigned edge) {
    frame_t++; unlock_t++;
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
    /* spawning: difficulty ramps with time; the vocabulary widens with unlock_t */
    int d = clampi(frame_t * 256 / (60 * 150), 0, 256);
    if (--spawn_cd <= 0) {
        spawn_cd = 60 - (40 * d >> 8); int n = 1 + (rnd(256) < d / 2) + (rnd(256) < d * 30 / 100);
        int tot = 0;
        for (int k = 0; k < K_KINDS; k++) if (unlock_t >= GATE[k] && WGT[k]) tot += WGT[k];
        for (int i = 0; i < n; i++) {
            int r = rnd(tot), k = K_SEEK;
            for (int q = 0; q < K_KINDS; q++) if (unlock_t >= GATE[q] && WGT[q]) { if (r < WGT[q]) { k = q; break; } r -= WGT[q]; }
            if (k == K_WEAVER && weaver_alive()) k = K_SEEK;           /* one chain at a time */
            queue(k); }
    }
    /* force-intro: the first enemy of a newly unlocked kind is guaranteed */
    for (int k = 1; k < K_KINDS; k++)
        if (unlock_t >= GATE[k] && WGT[k] && !(kinds_seen & (1 << k)) && !kind_pending(k)) { queue(k); break; }
    if (--swarm_cd <= 0) {
        swarm_cd = 1500; msg_t = 100; snd(120, 18, 6, 3); int a0 = rnd(256);
        for (int i = 0; i < 14; i++) for (int k = 0; k < NM; k++) if (!M[k].on) { int a = a0 + i * 256 / 14; M[k].on = 1; M[k].k = K_SEEK; M[k].life = 60;
            M[k].x = clampi(SW / 2 + (COS(a) * (SW / 2 - 14) >> 8), 8, SW - 8); M[k].y = clampi(SH / 2 + (SIN(a) * (SH / 2 - 14) >> 8), 8, SH - 8); break; }
    }
    for (int i = 0; i < NM; i++) if (M[i].on && --M[i].life <= 0) { spawn_enemy(M[i].k, M[i].x, M[i].y); M[i].on = 0; }
    for (int k = 0; k < NB; k++) if (B[k].on) { B[k].x += B[k].vx; B[k].y += B[k].vy; grid_warp(B[k].x >> 8, B[k].y >> 8, 40, 22);
        if (--B[k].life <= 0 || B[k].x < 0 || B[k].x > SW << 8 || B[k].y < 0 || B[k].y > SH << 8) B[k].on = 0; }
    for (int k = 0; k < NF; k++) if (F[k].on) { F[k].x += F[k].vx; F[k].y += F[k].vy;
        if (--F[k].life <= 0 || F[k].x < 0 || F[k].x > SW << 8 || F[k].y < 0 || F[k].y > SH << 8) F[k].on = 0;
        else if (inv <= 0) { int bx = (px - F[k].x) >> 8, by = (py - F[k].y) >> 8;
            if (bx * bx + by * by < 36) { F[k].on = 0; hit_player(); if (state != 1) return; } } }
    for (int k = 0; k < NN; k++) if (MN[k].on && --MN[k].life <= 0) {          /* the mine goes off */
        MN[k].on = 0; int mx = MN[k].x, my = MN[k].y;
        burst(mx, my, RGB(255, 240, 200), 16, 700); grid_warp(mx, my, 1800, 48); shake = shake > 3 ? shake : 3; snd(160, 10, -10, 4);
        if (inv <= 0) { int bx = (px >> 8) - mx, by = (py >> 8) - my; if (bx * bx + by * by < 34 * 34) { hit_player(); if (state != 1) return; } } }
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
        else if (e->k == K_SHIELD) { int sp = 154; e->vx += (ux * sp / 256 - e->vx) >> 5; e->vy += (uy * sp / 256 - e->vy) >> 5; }
        else if (e->k == K_PHANTOM) { int sp = 138; e->vx += (ux * sp / 256 - e->vx) >> 5; e->vy += (uy * sp / 256 - e->vy) >> 5; }
        else if (e->k == K_TURRET) {                                        /* stationary: only the gun moves */
            if (--e->t <= 0) { e->t = 120;
                for (int q = 0; q < NF; q++) if (!F[q].on) { F[q].on = 1; F[q].life = 240; F[q].x = e->x; F[q].y = e->y;
                    F[q].vx = ux * 3; F[q].vy = uy * 3; eh_fired++; snd(400, 3, 0, 2); break; } } }
        else if (e->k == K_BOMBER) { if (--e->t <= 0) { e->t = 90;      /* a mine every 1.5 s */
                for (int q = 0; q < NN; q++) if (!MN[q].on) { MN[q].on = 1; MN[q].x = e->x >> 8; MN[q].y = e->y >> 8;
                    MN[q].life = 120; mines_dropped++; break; } } }
        else if (e->k == K_WEAVER) {                                        /* snake: turn toward you, wobble on top */
            int hx = COS(e->t), hy = SIN(e->t), cross = hx * uy - hy * ux;
            e->t = (short)((e->t + (cross > 0 ? 2 : -2) + ((SIN(e->ph) * 2) >> 8)) & 255);
            e->vx = COS(e->t) * 640 >> 8; e->vy = SIN(e->t) * 640 >> 8; }
        else if (e->k == K_WSEG) { int idx = (wt - (e->t + 1) * (WTR / (NW + 1)) + WTR * 2) % WTR;
            e->x = wcx[idx]; e->y = wcy[idx]; e->vx = e->vy = 0; }
        e->x += e->vx; e->y += e->vy;
        if (e->k == K_WEAVER) { wcx[wt] = e->x; wcy[wt] = e->y; wt = (wt + 1) % WTR; }
        if (e->k == K_DRIFT || e->k == K_BOMBER) { int r = KIND[e->k].r; if ((e->x >> 8) < r || (e->x >> 8) > SW - r) e->vx = -e->vx; if ((e->y >> 8) < r || (e->y >> 8) > SH - r) e->vy = -e->vy; }
        int er = KIND[e->k].r, ex = e->x >> 8, ey = e->y >> 8;
        int hittable = evis(e);                     /* the phantom is only there half the time */
        for (int k = 0; k < NB; k++) if (B[k].on && hittable) { int bx = (B[k].x >> 8) - ex, by = (B[k].y >> 8) - ey;
            if (bx * bx + by * by < (er + 2) * (er + 2)) { B[k].on = 0;
                /* the shield's ring absorbs two hits before the seeker inside is exposed */
                if (--e->hp <= 0) { kill_enemy(e, 256); break; }
                else burst(B[k].x >> 8, B[k].y >> 8, e->k == K_SHIELD ? C_YELLOW : C_WHITE, 3, 300); } }
        if (!e->on) continue;
        if (inv <= 0 && hittable) { int bx = (px >> 8) - ex, by = (py >> 8) - ey; if (bx * bx + by * by < (er + 5) * (er + 5)) { hit_player(); if (state != 1) return; kill_enemy(e, 0); } }
    }
    n_enemies = ne;
    for (int i = 0; i < NR; i++) if (R[i].on) {
        R[i].r += (((R[i].R - R[i].r) * 46) >> 8) + 2; grid_ring(R[i].x, R[i].y, R[i].r, 28, 700);
        for (int q = 0; q < NE; q++) if (E[q].on && evis(&E[q])) { int dx = (E[q].x >> 8) - R[i].x, dy = (E[q].y >> 8) - R[i].y, rr = R[i].r + KIND[E[q].k].r;
            if (dx * dx + dy * dy < rr * rr) kill_enemy(&E[q], 256); }
        for (int q = 0; q < NF; q++) if (F[q].on) { int dx = (F[q].x >> 8) - R[i].x, dy = (F[q].y >> 8) - R[i].y, rr = R[i].r + 3;
            if (dx * dx + dy * dy < rr * rr) F[q].on = 0; }
        for (int q = 0; q < NN; q++) if (MN[q].on) { int dx = MN[q].x - R[i].x, dy = MN[q].y - R[i].y, rr = R[i].r + 6;
            if (dx * dx + dy * dy < rr * rr) { MN[q].on = 0; burst(MN[q].x, MN[q].y, RGB(255, 240, 200), 6, 400); } }
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
    for (int k = 0; k < NN; k++) if (MN[k].on) { int x = MN[k].x, y = MN[k].y;      /* mines blink faster as they near off */
        unsigned c = (MN[k].life < 30 && (MN[k].life & 4)) ? RGB(255, 120, 90) : RGB(255, 240, 200);
        poly(x, y, 4, 4, MN[k].life * 8, c, RGB(100, 90, 60)); }
    for (int i = 0; i < NE; i++) if (E[i].on) { enemy *e = &E[i]; const kind_t *d = &KIND[e->k]; int x = e->x >> 8, y = e->y >> 8;
        if (!evis(e)) continue;                                     /* the phantom is between frames */
        if (e->k == K_SEEK || e->k == K_SHIELD || e->k == K_PHANTOM) {
            int a = 0; if (e->vx || e->vy) { int bx = e->vx, by = e->vy; a = (iabs(bx) > iabs(by)) ? (bx > 0 ? 0 : 128) : (by > 0 ? 64 : 192); }
            poly(x, y, d->r, 4, e->k == K_PHANTOM ? 48 : a, d->core, d->glow);
            if (e->k == K_SHIELD && e->hp > 1) circle(x, y, d->r + 5, 2, C_YELLOW); }   /* the ring: up while it has hits */
        else if (e->k == K_DRIFT) poly(x, y, d->r, 4, e->a + 32, d->core, d->glow);
        else if (e->k == K_SPLIT || e->k == K_MINI) poly(x, y, d->r, 6, e->a, d->core, d->glow);
        else if (e->k == K_TURRET) poly(x, y, d->r, 3, e->a, d->core, d->glow);
        else if (e->k == K_BOMBER) { glow_line(x - d->r, y, x + d->r, y, d->core, d->glow); glow_line(x, y - d->r, x, y + d->r, d->core, d->glow); }
        else if (e->k == K_WSEG) circle(x, y, d->r - 2, 3, d->core);
        else if (e->k == K_WEAVER) poly(x, y, d->r, 6, e->a, d->core, d->glow);
        else for (int q = 0; q < 4; q++) { int a = e->a * 2 + q * 64; glow_line(x, y, x + (COS(a) * d->r >> 8), y + (SIN(a) * d->r >> 8), d->core, d->glow); glow_line(x + (COS(a) * d->r >> 8), y + (SIN(a) * d->r >> 8), x + (COS(a + 40) * d->r * 6 / 10 >> 8), y + (SIN(a + 40) * d->r * 6 / 10 >> 8), d->core, d->glow); } }
    for (int k = 0; k < NB; k++) if (B[k].on) { int x = B[k].x >> 8, y = B[k].y >> 8; line(x, y, x - (B[k].vx >> 7), y - (B[k].vy >> 7), 3, RGB(90, 75, 25)); line(x, y, x - (B[k].vx >> 8), y - (B[k].vy >> 8), 1, RGB(255, 246, 160)); }
    for (int k = 0; k < NF; k++) if (F[k].on) { int x = F[k].x >> 8, y = F[k].y >> 8; line(x, y, x - (F[k].vx >> 7), y - (F[k].vy >> 7), 3, RGB(40, 90, 100)); line(x, y, x - (F[k].vx >> 8), y - (F[k].vy >> 8), 1, RGB(159, 232, 255)); }
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
