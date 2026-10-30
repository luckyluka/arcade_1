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
 *   eh_fired (enemy shots taken: turrets and the bosses),
 *   mines_dropped (bomber mines laid),
 *   sector, loop (run structure: sectors within a credit, credits completed
 *   this session - loop survives death, like the vocabulary gates),
 *   sweeps (clean-sweep bombs this session), extras (extra lives this credit),
 *   nd (sectors cleared without a hit), overdrive (1 while it is on),
 *   boss_seen, boss_kills (bosses killed this session),
 *   boss_k (1 sentinel, 2 hydra, 3 orbiter, 4 weaver, 5 core; 0 = none out),
 *   boss_hp (hits left on it and its parts, 0 when none is out),
 *   boss_plates (Sentinel shield plates still up, 0..4),
 *   boss_n (Hydra bodies / Orbiter satellites / Weaver segments still up),
 *   boss_ph (the boss's phase: the Core's 1..3, the Hydra's split stage
 *   0..2, the Sentinel's 1),
 *   arena (the sector's arena rule: 1 grid, 2 pylons, 3 ring, 4 crush, 5 dark),
 *   wall_in (crush: the walls' inset right now, px), pyl_hits, gates (session
 *   counters: things the pylons blocked, enemies that arrived through a gate),
 *   rec_n (frames of this credit's input stream recorded in the design doc 9
 *   replay format: one byte per frame, four direction bits + three button
 *   bits), rec_crc (its rolling checksum - a replay that recomputes the same
 *   rec_crc and the same score is the design's verification step),
 *   gst_score (the score the ghost's run ended with), gh_on (1 while the
 *   ghost is flying: the best run so far, replayed as an outline ship),
 *   mus_bar (M6: the effort-driven music's step count within the current
 *   8-step bar; its rate tracks the tap tier - auto 2 steps/s, regular 4,
 *   tap 7.5 - so a faster tap makes the bar turn quicker)
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
#define C_VIOLET  RGB(190, 120, 255)                 /* the pylons' palette (design doc 8) */
#define C_TEAL    RGB(60, 220, 200)                  /* the ring's                        */
#define C_CRUSH   RGB(255, 90, 90)                   /* the closing walls'                */

/* ---- observable state (file-scope globals, read by the HUD so -Os keeps them) ---- */
int state, score, lives, mult, kills, tap_speed, charge, n_enemies, secs;
int kinds_seen, kinds_killed, eh_fired, mines_dropped;   /* session-cumulative, not reset */
int sector, loop;                      /* run structure: sector 1..5 of a credit (M3 fills 2-5), then the run loops */
int sweeps, nd;                        /* session-cumulative: clean sweeps, no-damage sectors */
int extras;                            /* extra lives granted this credit */
int overdrive;                         /* 1 while the overdrive score boost is on */
int boss_seen, boss_kills, boss_k, boss_hp, boss_plates, boss_n, boss_ph;  /* the bosses: session flags + live state */
int arena, wall_in, pyl_hits, gates;           /* the sector's arena rule and what it has done this session */

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
typedef struct { int x, y, r, R; short life, n, cr; unsigned char b, d, on; unsigned short hm; } ring;
/* ring: b = bomb ring (n = kills it scored, cr = charge it refunds), d = already
 * hit the Sentinel this expansion, otherwise the death ring from a player hit */
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
static int sec_t;                                 /* play frames in the current SECTOR (the waves phase) */
static int sec_k;                                 /* kills this sector's waves: reaching SECQ's quota summons the boss early */
static int sec_hit;                               /* the player was hit this sector: kills the no-damage bonus */
static int od_t;                                  /* frames of tap speed > 0.8 building toward overdrive */
static int msg_k;                                 /* banner: 0 swarm, 1 sentinel, 2 overdrive, 3 clear, 4 sweep */
static int cur_ring;                              /* the ring whose kill-counter is open, or -1 */
static unsigned prev_pad;

/* ---- M5: replay recording and the ghost (design doc 9) ----
 * Every frame of a credit is packed into one byte in the design's replay
 * format - four direction bits and three button bits - and kept in REC[].
 * When a new credit starts the best run so far is copied to GST[] and plays
 * back as a faint outline ship you race: it flies the recorded path through
 * the same movement code, but it fires nothing, takes no hits and scores
 * nothing - a ghost is a race, not a second player. rec_n / rec_crc make the
 * stream readable to the tools; a replay that recomputes the same rec_crc and
 * the same score is the design's verification step. */
#define RECB 4096                                /* frames kept: 68 s of credit */
#define GSTMIN 240                               /* a run worth racing: 4 s */
static unsigned char REC[RECB], GST[RECB];
static int gst_n;                                /* frames in the ghost's run */
static int gh_on, gh_i;                          /* the ghost is flying: index into GST */
static int gpx, gpy, gh_vx, gh_vy, gh_aim;       /* the ghost's ship */
int rec_n, gst_score;                            /* observables: the stream's length, the best score */
unsigned rec_crc;                                /* the stream's rolling checksum (wraps: unsigned) */

/* ---- the bosses (design doc 5): one dispatcher, five shapes ----
 * When a sector's waves end the arena clears and that sector's boss warps in.
 * The Sentinel (sector 1) keeps its M2 grammar exactly: a rotating polygon
 * with four 3-hit shield plates around an 8-hit core, hits landing on the
 * nearest intact plate until all four are gone. The other four share one
 * part pool: the Hydra is a body that splits at 66% and 33% of its hits
 * left, the Orbiter a slow core ringed by satellites that fire inward, the
 * Weaver a segmented snake with a 6-hit head, the Core a three-phase
 * fortress that summons the vocabulary itself. */
#define BOR 15                                     /* core radius, px         */
#define BPHP 3                                     /* hits per Sentinel plate */
#define BCHP 8                                     /* hits on a core          */
#define BMAXS (BCHP + 4 * BPHP)                    /* the Sentinel: 20        */
static int bo_on, bo_k;                            /* 0 none, 1..5 = the sector's boss */
static int bo_x, bo_y, bo_vx, bo_vy, bo_a, bo_t, bo_cd, bo_ph, bo_max, bo_soak;   /* soak: the shielded beat after a phase change */
static short bo_php[4], bo_chp;
typedef struct { int x, y, vx, vy; short hp, r, a, t; unsigned char on; } bpart;
static bpart BP[9];                                /* hydra bodies / satellites / weaver segments */
static int btx[WTR], bty[WTR], bwt;                /* the Weaver boss's trail  */

/* ---- arenas (design doc 5): every sector past 1 changes the room ----
 * arena / wall_in / pyl_hits / gates are observable globals (see the header) */
static int gate_a;                                 /* ring: the spawn gates' rotation          */
static const short PYL[4][2] = { { 120, 76 }, { 360, 76 }, { 120, 194 }, { 360, 194 } };
#define PYLR 14                                    /* pylon radius, px                          */
#define RGX (SW / 2)
#define RGY (SH / 2)
#define RGR 112                                    /* ring arena radius, px                     */

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
static int mus_hold;                                /* M6: one-frame tail after an sfx, so it reads as a tick */
static void music_frame(void);                      /* M6: the effort-driven music, defined below snd_frame */
static void music_tick(void);                       /* M6: advance the music tempo, called every play frame */
static void snd(int f, int n, int sweep, int pri) { if (snd_n > 0 && pri < snd_pri) return; snd_f = f; snd_n = n; snd_sweep = sweep; snd_pri = pri; }
static void snd_frame(void) {
    if (snd_n > 0) { AUDIO_FREQ = (unsigned)snd_f; __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1; snd_f += snd_sweep; if (snd_f < 40) snd_f = 40; snd_n--; if (snd_n == 0) mus_hold = 1; }
    else if (tc_charging() && state == 1) { AUDIO_FREQ = (unsigned)(160 + (charge >> 5)); __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1; }
    else if (mus_hold) mus_hold = 0;                 /* one frame of decay after a hit, so sfx reads as a tick not a click */
    else music_frame();                              /* the music owns the voice whenever no sfx/whine is sounding */
}

/* ---- M6: the effort-driven music (design doc 8) ----
 * The machine has one square-wave voice, so the three "layers" the design
 * names cannot sound at once; the layers are carried by tempo and brightness
 * instead. One bar = 8 steps; the step rate and the note pattern are picked
 * from the effort tier, so the music literally follows your tap speed:
 *   auto   (tap_speed <=  63)  2 steps/s   low bass notes  - a sparse drone
 *   regular( 63 <  s     <=170) 4 steps/s   mid pulses      - a driving beat
 *   tap    (s > 170)           7.5 steps/s high staccato   - a frantic lead
 * The music clock ticks every play frame regardless of sfx, so the bar's
 * tempo tracks effort even mid-combat - otherwise the busiest, fastest tier
 * would starve the music exactly when it should sing loudest. SFX still own
 * the pitch: when one is sounding it writes the voice, and the music's step
 * lands on the next free frame (mus_hold). mus_bar observable: steps in the
 * current bar, so a test can measure the tempo and prove it tracks effort.
 */
static const unsigned char MUS_PAT[3][8] = {          /* one bar of notes per tier, a rising arpeggio */
    { 0, 0, 3, 3, 5, 5, 3, 3 },                       /* auto:   a slow open fifth                       */
    { 0, 3, 5, 7, 0, 3, 5, 7 },                       /* regular: a steady climb                          */
    { 0, 7, 5, 7, 10, 7, 5, 3 }                       /* tap:    a leaping lead                           */
};
static const unsigned char MUS_ROOT[3] = { 55, 110, 220 };   /* bass, mid, lead base frequency (Hz) */
static const unsigned char MUS_STEP[3] = { 30, 15, 8 };     /* frames per step: 2, 4 then 7.5 steps/s */
static int mus_tier, mus_step, mus_cnt, mus_due;      /* tier 0..2, step in bar, frames since step, a step is owed */
int mus_bar;                                          /* observable: steps taken in the current bar (resets each bar) */
static int music_tier(int s) { return s > 170 ? 2 : (s > 63 ? 1 : 0); }
static void music_tick(void) {                        /* advance the tempo every play frame; pick the note when due */
    if (state != 1) return;
    int t = music_tier(tap_speed);
    if (t != mus_tier) { mus_tier = t; mus_step = 0; mus_cnt = 0; }   /* a tier change restarts the bar cleanly */
    if (++mus_cnt >= MUS_STEP[t]) {                   /* 30, 15 or 8 frames per step: 2, 4 or 7.5 steps/s */
        mus_cnt = 0; mus_due = 1;
        mus_step++; if (mus_step >= 8) mus_step = 0;
        mus_bar++; if (mus_bar >= 8) mus_bar = 0;
    }
}
static void music_frame(void) {                       /* sound the current note, or the sfx that outranks it */
    if (state != 1) { AUDIO_CTRL = 0; return; }
    if (mus_due && snd_n <= 0) {                      /* the music's turn: no sfx is holding the voice */
        unsigned hz = (unsigned)MUS_ROOT[mus_tier] << MUS_PAT[mus_tier][mus_step];
        AUDIO_FREQ = hz; __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1;
        mus_due = 0; mus_hold = 1;
    } else if (snd_n <= 0 && !mus_hold) AUDIO_CTRL = 0;   /* the one-frame tail after a step or an sfx */
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
/* Sector 5 is the design's "phantoms and full vocabulary" arena: every kind
 * is open there whatever the session has met, and the late kinds weigh in */
static int kind_open(int k) { return sector == 5 || unlock_t >= GATE[k]; }
static int wgt(int k) { int w = WGT[k]; return (sector == 5 && (k == K_PHANTOM || k == K_WEAVER)) ? w + 4 : w; }

/* Run structure and scoring (design doc 5 and 6), compressed to the session
 * length the golden test can watch: the design's 90-110 s waves + 45-60 s
 * boss per sector become SECW[] waves frames (and SECQ[] kills, whichever
 * comes first) plus a boss that dies under fire in a few seconds
 * (docs/experiments/neontap-m2-sector-1.md and -m3-sectors.md). Sector 1
 * keeps its pure clock so its story is unchanged; sectors 2-5 also end their
 * waves when the sector's kill quota is met, so a player who is winning
 * reaches the boss sooner. */
static const short SECW[5] = { 1100, 400, 320, 260, 200 };   /* waves time cap, frames */
static const unsigned char SECQ[5] = { 0, 4, 3, 3, 2 };       /* kills that end the waves early; 0 = time only */
#define SWEEP_BONUS 2000                           /* flat: a clean-sweep bomb          */
#define ND_BONUS 5000                              /* flat: a sector cleared untouched  */
#define BOSS_PTS 5000                              /* a boss, times the chain           */
static const int EXTRA[2] = { 50000, 200000 };     /* extra-life score marks            */

/* ---- the arena rules (design doc 5) ---- */
static int pyl_at(int x, int y, int r) {           /* sector 2: is (x,y) inside a pylon? */
    if (arena != 2) return 0;
    for (int i = 0; i < 4; i++) { int dx = x - PYL[i][0], dy = y - PYL[i][1], rr = PYLR + r; if (dx * dx + dy * dy < rr * rr) return 1; }
    return 0;
}
/* keep a body inside the room: x,y in Q8 px, r in px. Sectors 1 and 2 leave
 * the plain screen clamp the game already did, so their behaviour is exact */
static void arena_clamp(int *x, int *y, int r) {
    int ix = *x >> 8, iy = *y >> 8;
    if (arena == 3) {                               /* the ring */
        int dx = ix - RGX, dy = iy - RGY, lim = RGR - r, d = (int)isqrt((unsigned)(dx * dx + dy * dy));
        if (d > lim) { if (!d) d = 1; ix = RGX + dx * lim / d; iy = RGY + dy * lim / d; }
    } else if (arena == 4) {                        /* between the closing walls */
        int in = wall_in + r; ix = clampi(ix, in, SW - in); iy = clampi(iy, r, SH - r);
    } else { ix = clampi(ix, r, SW - r); iy = clampi(iy, r, SH - r); }
    *x = ix << 8; *y = iy << 8;
}
static int arena_out(int x, int y) {                /* is this point outside the room? */
    if (arena == 3) { int dx = x - RGX, dy = y - RGY; return dx * dx + dy * dy > RGR * RGR; }
    if (arena == 4) return x < wall_in || x > SW - wall_in;
    return 0;
}
static int sec_done(void) {                         /* the waves are over: the boss is due */
    int q = SECQ[sector - 1];
    return sec_t >= SECW[sector - 1] || (q > 0 && sec_k >= q);
}

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
    if (arena == 3) {                               /* the ring: enemies arrive through the rotating gates */
        gate_a = (gate_a + 40) & 255; gates++;
        x = clampi(RGX + (COS(gate_a) * (RGR - 10) >> 8), 8, SW - 8);
        y = clampi(RGY + (SIN(gate_a) * (RGR - 10) >> 8), 8, SH - 8);
    } else for (int tries = 0; tries < 10; tries++) {
        int s = rnd(4);
        x = s < 2 ? 8 + rnd(SW - 16) : (s == 2 ? 8 : SW - 8); y = s >= 2 ? 8 + rnd(SH - 16) : (s == 0 ? 8 : SH - 8);
        int dx = x - (px >> 8), dy = y - (py >> 8); if (dx * dx + dy * dy > 96 * 96) break;
    }
    for (int i = 0; i < NM; i++) if (!M[i].on) { M[i].on = 1; M[i].k = (unsigned char)k; M[i].x = x; M[i].y = y; M[i].life = 45; return; }
}
/* every point goes through here, so the extra-life marks are checked once */
static void score_add(int v) {
    score += v;
    while (extras < 2 && score >= EXTRA[extras]) { extras++; lives++; snd(1200, 24, 4, 6); }
}
static void kill_enemy(enemy *e, int bonus_x256) {
    const kind_t *d = &KIND[e->k]; int x = e->x >> 8, y = e->y >> 8;
    burst(x, y, d->core, e->k == K_SPLIT ? 22 : 12, 700); shake = shake > 3 ? shake : 3;
    grid_warp(x, y, 1100, 54);
    mult += overdrive ? 128 : 64; if (mult > 2560) mult = 2560; mult_t = 110;   /* overdrive doubles the chain gain */
    score_add(d->pts * (mult >> 8) * bonus_x256 / 256); kills++;
    if (!bo_on) sec_k++;                            /* the waves' kill quota */
    kinds_killed |= 1 << e->k;
    if (cur_ring >= 0) R[cur_ring].n++;             /* the bomb ring's clean-sweep count */
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
/* returns the slot taken, or -1 if every ring is busy */
static int add_ring(int x, int y, int R_, int kill) { for (int i = 0; i < NR; i++) if (!R[i].on) { R[i].on = 1; R[i].x = x; R[i].y = y; R[i].r = kill ? 2 : 0; R[i].R = R_; R[i].life = kill ? 34 : 24; R[i].n = 0; R[i].cr = 0; R[i].b = 0; R[i].d = 0; R[i].hm = 0; return i; } return -1; }
static void hit_player(void) {
    grid_warp(px >> 8, py >> 8, 2600, 120); lives--; inv = 130; shake = 7; flash = 3; burst(px >> 8, py >> 8, C_WHITE, 40, 900);
    add_ring(px >> 8, py >> 8, 72, 1); mult = 256; snd(300, 14, -14, 5); sec_hit = 1;
    if (lives <= 0) { state = 2; over_t = 0; snd(220, 40, -4, 9); }
}
static void reset_game(void) {
    /* M5: a new credit starts, so the best run so far becomes the ghost.
     * score here is still the run that just ended (it is cleared below) */
    if (rec_n >= GSTMIN && score > gst_score) {
        for (int i = 0; i < RECB; i++) GST[i] = REC[i];
        gst_n = rec_n; gst_score = score;
    }
    rec_n = 0; rec_crc = 0;
    memset(E, 0, sizeof E); memset(B, 0, sizeof B); memset(M, 0, sizeof M); memset(R, 0, sizeof R); memset(S, 0, sizeof S);
    memset(F, 0, sizeof F); memset(MN, 0, sizeof MN); memset(BP, 0, sizeof BP); wt = 0; wcount = 0; bwt = 0;
    state = 1; score = 0; lives = 3; mult = 256; kills = 0; px = (SW / 2) << 8; py = (SH / 2) << 8; pvx = pvy = 0; aim = 192;
    gh_on = gst_n > 0; gh_i = 0; gpx = px; gpy = py; gh_vx = gh_vy = 0; gh_aim = 192;   /* the ghost flies the recorded start */
    inv = 0; fcd = 0; mult_t = 0; spawn_cd = 60; swarm_cd = 1500; frame_t = 0; shake = flash = msg_t = 0; rng_state = 5; tc_reset(); snd_n = 0;
    mus_tier = 0; mus_step = 0; mus_cnt = 0; mus_bar = 0; mus_hold = 0; mus_due = 0;   /* M6: the music restarts its bar with the credit */
    sector = 1; extras = 0; overdrive = 0; od_t = 0; sec_t = sec_k = 0; sec_hit = 0; arena = 1; wall_in = 0; gate_a = 0;
    bo_on = 0; bo_k = 0; boss_k = boss_n = boss_ph = boss_hp = boss_plates = 0; cur_ring = -1;
    /* kinds_seen / kinds_killed / eh_fired / mines_dropped / unlock_t /
     * sweeps / nd / boss_seen / boss_kills are SESSION-cumulative: a death
     * does not un-learn the vocabulary or un-count the run's achievements */
}
static void bomb(void) {
    if (charge < 2048) return;
    int c = charge / 100;                                  /* 0..256 */
    int refund = charge / 4;                               /* a clean sweep gives a quarter back */
    charge = 0;
    int i = add_ring(px >> 8, py >> 8, 54 + (180 * c >> 8), 1);
    if (i >= 0) { R[i].b = 1; R[i].cr = (short)refund; }
    shake = 4 + (6 * c >> 8); flash = 2; snd(700, 22, -26, 6);
}

/* ---- the bosses: waves stop, the arena clears, it fights ---- */
static void boss_sync(void) {                            /* mirror the parts to boss_hp / boss_plates / boss_n */
    int hp = bo_chp, pl = 0, n = 0;
    for (int i = 0; i < 4; i++) if (bo_php[i] > 0) { hp += bo_php[i]; pl++; }
    for (int i = 0; i < 9; i++) if (BP[i].on) { hp += BP[i].hp; n++; }
    boss_hp = hp; boss_plates = pl; boss_n = n; boss_ph = bo_ph;
}
/* the Hydra's split: every living body becomes three carrying the same hits
 * left, so the boss gets smaller and more numerous without gaining health */
static void hydra_split(int stage) {
    int src[9], ns = 0;
    for (int i = 0; i < 9; i++) if (BP[i].on) src[ns++] = i;
    for (int i = ns - 1; i >= 0; i--) {
        bpart *p = &BP[src[i]]; int h = p->hp, x = p->x, y = p->y, r = p->r, q[3];
        if (stage == 1) { q[0] = (h + 2) / 3; q[1] = (h + 1) / 3; q[2] = h - q[0] - q[1]; }
        else { q[0] = h >= 3; q[1] = h >= 2; q[2] = h >= 1; }
        p->on = 0;
        for (int s = 0; s < 3; s++) if (q[s] > 0) for (int j = 0; j < 9; j++) if (!BP[j].on) {
            int a = rnd(256);
            BP[j].on = 1; BP[j].x = x; BP[j].y = y; BP[j].hp = (short)q[s];
            BP[j].r = (short)(stage == 1 ? r * 2 / 3 : r); BP[j].a = 0; BP[j].t = 0;
            BP[j].vx = COS(a) * 200 >> 8; BP[j].vy = SIN(a) * 200 >> 8; break;
        }
    }
}
static void boss_fire(int n, int sp) {                   /* n aimed shots at the player */
    int dx = px - bo_x, dy = py - bo_y, dist = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
    for (int j = 0; j < n; j++) for (int q = 0; q < NF; q++) if (!F[q].on) {
        F[q].on = 1; F[q].life = 240; F[q].x = bo_x; F[q].y = bo_y;
        F[q].vx = (dx >> 8) * 256 / dist * (n + 1 - j) / (n + 1); F[q].vy = (dy >> 8) * 256 / dist * (n + 1 - j) / (n + 1);
        eh_fired++; snd(400, 3, 0, 2); break; }
}
static void boss_spawn(void) {
    int i;
    bo_on = 1; bo_k = sector; bo_x = (SW / 2) << 8; bo_y = (SH / 3) << 8;
    bo_vx = bo_vy = 0; bo_a = 0; bo_t = 0; bo_cd = 90; bo_ph = 1; bo_max = BMAXS; bo_soak = 0;
    memset(BP, 0, sizeof BP); bwt = 0; for (i = 0; i < 4; i++) bo_php[i] = 0; bo_chp = 0;
    switch (bo_k) {
    case 1:                                              /* the Sentinel: four plates, then the core (M2, unchanged) */
        bo_cd = 90; for (i = 0; i < 4; i++) bo_php[i] = BPHP; bo_chp = BCHP; break;
    case 2:                                              /* the Hydra: one 9-hit body that splits at 66% and 33% */
        BP[0].on = 1; BP[0].hp = 9; BP[0].r = 16; BP[0].x = bo_x; BP[0].y = bo_y; bo_ph = 0; break;
    case 3:                                              /* the Orbiter: a 6-hit core, four 2-hit satellites firing inward */
        bo_x = RGX << 8; bo_y = RGY << 8; bo_cd = 120; bo_chp = 6; bo_max = 14; bo_ph = 0;
        for (i = 0; i < 4; i++) { BP[i].on = 1; BP[i].hp = 2; BP[i].r = 7; BP[i].a = (short)(i * 64); BP[i].t = (short)(70 + i * 30); }
        break;
    case 4:                                              /* the Weaver: a 6-hit head pulling eight 1-hit segments */
        bo_cd = 120; bo_chp = 6; bo_max = 14; bo_t = (short)rnd(256);
        for (i = 0; i < WTR; i++) { btx[i] = bo_x; bty[i] = bo_y; }
        for (i = 0; i < 8; i++) { BP[i].on = 1; BP[i].hp = 1; BP[i].r = 9; BP[i].t = (short)i; BP[i].x = bo_x; BP[i].y = bo_y; }
        break;
    default:                                             /* the Core: a 12-hit fortress in three phases */
        bo_cd = 120; bo_chp = 12; bo_max = 12; break;
    }
    boss_sync(); boss_seen = 1; boss_k = bo_k; msg_t = 100; msg_k = bo_k == 1 ? 1 : 4 + bo_k; snd(140, 26, 3, 5);
    /* the arena clears for the boss: whatever is still alive bursts, unscored.
     * The shockwave rings go too - a boss must not warp in on top of a wave
     * phase ring still sweeping the room */
    for (i = 0; i < NE; i++) if (E[i].on) { E[i].on = 0; burst(E[i].x >> 8, E[i].y >> 8, C_WHITE, 6, 500); }
    memset(M, 0, sizeof M); memset(MN, 0, sizeof MN); memset(R, 0, sizeof R); cur_ring = -1;
    if (bo_k == 5) { queue(K_BOMBER);                   /* the Core uses every enemy type, and the
                                                        * bomber's runway is the fight itself */
        for (int q = 0, i = 1; i < K_KINDS && q < 3; i++) if (wgt(i)) { queue(i); q++; } }
}
static void boss_kill(void) {
    int bx = bo_x >> 8, by = bo_y >> 8;
    burst(bx, by, C_WHITE, 40, 900); grid_warp(bx, by, 2600, 120); shake = 7; flash = 3;
    for (int i = 0; i < 9; i++) if (BP[i].on) burst(BP[i].x >> 8, BP[i].y >> 8, C_YELLOW, 10, 700);
    bo_on = 0; bo_k = 0; boss_k = 0; boss_hp = boss_plates = boss_n = boss_ph = 0; boss_kills++; kills++;
    mult += overdrive ? 128 : 64; if (mult > 2560) mult = 2560; mult_t = 110;
    score_add(BOSS_PTS * (mult >> 8));
    if (!sec_hit) { nd++; score_add(ND_BONUS); }
    snd(500, 30, -10, 6);
    sector++; if (sector > 5) { sector = 1; loop++; }   /* five sectors per design credit, then the run loops */
    arena = sector; sec_t = 0; sec_k = 0; sec_hit = 0; wall_in = 0; spawn_cd = 60; swarm_cd = 1500; msg_t = 100; msg_k = 3;
    memset(BP, 0, sizeof BP);
    /* the design slows the world down on a boss death; this machine has one
     * frame clock and no way to stretch it, so the beat is a grace window:
     * you cannot be hit for two seconds as the next sector assembles */
    inv = 120;
}
/* a hit at (x,y) in pixels: the nearest part takes it, else the core */
static int boss_strike(int x, int y) {
    int best = -1, bd = 1 << 30, i;
    if (!bo_on) return 0;
    if (bo_soak > 0) {                                  /* the Core's shielded beat between phases */
        burst(x, y, C_WHITE, 3, 300); return 1;
    }
    if (bo_k == 1) {                                    /* the Sentinel: nearest intact plate, then the core (M2) */
        int hx = x - (bo_x >> 8), hy = y - (bo_y >> 8);
        if (hx * hx + hy * hy >= (BOR + 4) * (BOR + 4)) return 0;   /* M2's reach: the core circle */
        for (i = 0; i < 4; i++) if (bo_php[i] > 0) {
            int a = bo_a + i * 64, ox = COS(a) * BOR >> 8, oy = SIN(a) * BOR >> 8;
            int d = (hx - ox) * (hx - ox) + (hy - oy) * (hy - oy);
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) {
            int a = bo_a + best * 64, qx = (bo_x >> 8) + (COS(a) * BOR >> 8), qy = (bo_y >> 8) + (SIN(a) * BOR >> 8);
            if (--bo_php[best] <= 0) { burst(qx, qy, C_YELLOW, 14, 600); snd(300, 10, -20, 4); }
            else burst(qx, qy, C_WHITE, 4, 300);
        } else if (--bo_chp <= 0) { boss_kill(); return 1; }
        else burst(bo_x >> 8, bo_y >> 8, C_ORANGE, 6, 400);
        boss_sync(); return 1;
    }
    for (i = 0; i < 9; i++) if (BP[i].on) {             /* the nearest part takes the hit */
        int dx = x - (BP[i].x >> 8), dy = y - (BP[i].y >> 8), rr = BP[i].r + 4;
        if (dx * dx + dy * dy < rr * rr) { int d = dx * dx + dy * dy; if (d < bd) { bd = d; best = i; } }
    }
    if (best >= 0) {
        bpart *p = &BP[best];
        if (--p->hp <= 0) {
            p->on = 0; burst(p->x >> 8, p->y >> 8, C_YELLOW, 12, 600); snd(300, 10, -20, 4);
            if (bo_k == 4) {                                /* the chain grammar: kill the head OR the
                                                            * whole chain (design doc 5, "all segments") */
                int left = 0;
                for (i = 0; i < 9; i++) if (BP[i].on) left++;
                if (!left) { boss_kill(); return 1; }
            }
        }
        else burst(p->x >> 8, p->y >> 8, C_WHITE, 4, 300);
    } else {
        int dx = x - (bo_x >> 8), dy = y - (bo_y >> 8), rr = BOR + 4;
        if (bo_k == 2 || dx * dx + dy * dy >= rr * rr) return 0;   /* the Hydra has no core to shoot */
        if (--bo_chp <= 0) { boss_kill(); return 1; }
        burst(bo_x >> 8, bo_y >> 8, C_ORANGE, 6, 400);
    }
    if (bo_k == 2) {                                    /* the Hydra splits at 66% and 33% of its hits left */
        int total = 0, n = 0;
        for (i = 0; i < 9; i++) if (BP[i].on) { total += BP[i].hp; n++; }
        if (!total || !n) { boss_kill(); return 1; }
        if (bo_ph == 0 && total <= 6) { bo_ph = 1; hydra_split(1); snd(220, 14, 8, 5); }
        else if (bo_ph == 1 && total <= 3) { bo_ph = 2; hydra_split(2); snd(220, 14, 8, 5); }
    }
    boss_sync(); return 1;
}
/* a body part keeps to the room: reflect off the walls, then clamp */
static void part_bounce(bpart *p) {
    int lim = arena == 4 ? wall_in : 0;
    if ((p->x >> 8) < lim + p->r || (p->x >> 8) > SW - lim - p->r) p->vx = -p->vx;
    if ((p->y >> 8) < p->r || (p->y >> 8) > SH - p->r) p->vy = -p->vy;
    arena_clamp(&p->x, &p->y, p->r);
}
static void boss_update(void) {
    bo_t++;
    if (bo_k == 1) {                                    /* the Sentinel: slow drift, then two aimed shots (M2, unchanged) */
        bo_a += 2;
        if (bo_t % 128 == 0) {                          /* a slow drift, re-aimed at you every 2 s */
            int dx = px - bo_x, dy = py - bo_y, dist = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
            bo_vx = (dx >> 8) * 90 / dist; bo_vy = (dy >> 8) * 90 / dist;
        }
        bo_x = clampi(bo_x + bo_vx, 30 << 8, (SW - 30) << 8); bo_y = clampi(bo_y + bo_vy, 30 << 8, (SH - 30) << 8);
        if (--bo_cd <= 0) { bo_cd = 120;                /* two aimed shots every 2 s */
            int dx = px - bo_x, dy = py - bo_y, dist = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
            for (int j = 0; j < 2; j++) for (int q = 0; q < NF; q++) if (!F[q].on) {
                F[q].on = 1; F[q].life = 240; F[q].x = bo_x; F[q].y = bo_y;
                F[q].vx = (dx >> 8) * 256 / dist * (3 - j) / 2; F[q].vy = (dy >> 8) * 256 / dist * (3 - j) / 2;
                eh_fired++; snd(400, 3, 0, 2); break; } }
        return;
    }
    if (bo_k == 2) {                                    /* the Hydra: the pieces chase, the biggest shoots */
        int big = -1, bh = 0;
        for (int i = 0; i < 9; i++) if (BP[i].on) {
            bpart *p = &BP[i];
            int dx = px - p->x, dy = py - p->y, d = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
            int ux = (dx >> 8) * 256 / d, uy = (dy >> 8) * 256 / d;
            p->vx += (ux * 120 / 256 - p->vx) >> 5; p->vy += (uy * 120 / 256 - p->vy) >> 5;
            p->x += p->vx; p->y += p->vy; part_bounce(p);
            if (p->hp > bh) { bh = p->hp; big = i; }
        }
        if (big >= 0) { bo_x = BP[big].x; bo_y = BP[big].y; bo_a += 4; }
        if (--bo_cd <= 0) { bo_cd = 90; boss_fire(1, 3); }
        return;
    }
    if (bo_k == 3) {                                    /* the Orbiter: satellites orbit and fire inward */
        bo_a += 3;
        bo_x = (RGX << 8) + SIN(bo_t) * 22; bo_y = (RGY << 8) + (COS(bo_t * 2) * 12);
        arena_clamp(&bo_x, &bo_y, BOR);
        for (int i = 0; i < 4; i++) if (BP[i].on) {
            bpart *p = &BP[i]; p->a = (short)((p->a + 4) & 255);
            p->x = bo_x + COS(p->a) * 46; p->y = bo_y + SIN(p->a) * 46;
            if (--p->t <= 0) { p->t = 150;              /* an inward shot every 2.5 s */
                int dx = bo_x - p->x, dy = bo_y - p->y, d = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
                for (int q = 0; q < NF; q++) if (!F[q].on) { F[q].on = 1; F[q].life = 240; F[q].x = p->x; F[q].y = p->y;
                    F[q].vx = (dx >> 8) * 256 / d * 3 / 2; F[q].vy = (dy >> 8) * 256 / d * 3 / 2;
                    eh_fired++; snd(420, 3, 0, 2); break; } }
        }
        if (--bo_cd <= 0) { bo_cd = 120; boss_fire(2, 3); }
        return;
    }
    if (bo_k == 4) {                                    /* the Weaver: a fast snake that fills the room */
        int hx = COS(bo_t), hy = SIN(bo_t), dx = px - bo_x, dy = py - bo_y;
        int d = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
        int ux = (dx >> 8) * 256 / d, uy = (dy >> 8) * 256 / d, cross = hx * uy - hy * ux;
        bo_t = (short)((bo_t + (cross > 0 ? 2 : -2) + ((SIN(bo_t * 3) * 2) >> 8)) & 255);
        bo_vx = COS(bo_t) * 560 >> 8; bo_vy = SIN(bo_t) * 560 >> 8;   /* a touch slower than the waves' weaver: it must be killable */
        bo_x += bo_vx; bo_y += bo_vy; arena_clamp(&bo_x, &bo_y, BOR);
        btx[bwt] = bo_x; bty[bwt] = bo_y; bwt = (bwt + 1) % WTR;
        for (int i = 0; i < 8; i++) if (BP[i].on) {
            int idx = (bwt - (BP[i].t + 1) * (WTR / 9) + WTR * 2) % WTR;
            BP[i].x = btx[idx]; BP[i].y = bty[idx];
        }
        if (--bo_cd <= 0) { bo_cd = 120; boss_fire(1, 4); }
        return;
    }
    /* the Core: three phases, each faster and fuller than the last */
    bo_a += 2;
    if (bo_t % 128 == 0) {                               /* a slow drift, quicker with every phase */
        int dx = px - bo_x, dy = py - bo_y, dist = (int)isqrt((unsigned)(((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)))) + 1;
        int sp = 50 + 40 * bo_ph;
        bo_vx = (dx >> 8) * sp / dist; bo_vy = (dy >> 8) * sp / dist;
    }
    bo_x = clampi(bo_x + bo_vx, 30 << 8, (SW - 30) << 8); bo_y = clampi(bo_y + bo_vy, 30 << 8, (SH - 30) << 8);
    arena_clamp(&bo_x, &bo_y, BOR);
    int ph = bo_chp > 8 ? 1 : bo_chp > 5 ? 2 : 3;   /* phase 3 gets the last five hits: it must be seen */
    if (ph != bo_ph) {
        bo_ph = ph; bo_soak = 90;                       /* a shielded beat: the phase has time to arrive */
        msg_t = 60; msg_k = 9; snd(180, 16, 8, 5);
        queue(K_BOMBER);
        for (int q = 0, i = 1; i < K_KINDS && q < 3; i++) if (wgt(i) && (i + ph) % 2) { queue(i); q++; }
    }
    if (bo_soak > 0) bo_soak--;
    if (--bo_cd <= 0) { bo_cd = 120 - 20 * bo_ph; boss_fire(bo_ph + 1, 3); }
}

/* ---- M5: the replay stream and the ghost's flight (declarations above) ----
 * pack_pad / unpack_pad turn the pad into the design doc 9 replay byte and
 * back. ship_step is the ship's motion, shared by the player and the ghost:
 * a pure function of the pad history and the arena clock, so a recorded
 * stream reproduces its path exactly. */

/* direction pad -> the byte the design's replay format stores, and back */
static unsigned char pack_pad(unsigned pad) {
    return (unsigned char)(((pad & BTN_UP) ? 1 : 0) | ((pad & BTN_DOWN) ? 2 : 0) | ((pad & BTN_LEFT) ? 4 : 0)
                         | ((pad & BTN_RIGHT) ? 8 : 0) | ((pad & BTN_A) ? 16 : 0) | ((pad & BTN_B) ? 32 : 0)
                         | ((pad & BOMB_BTN) ? 64 : 0));
}
static unsigned unpack_pad(unsigned char b) {
    return ((b & 1) ? BTN_UP : 0) | ((b & 2) ? BTN_DOWN : 0) | ((b & 4) ? BTN_LEFT : 0) | ((b & 8) ? BTN_RIGHT : 0)
         | ((b & 16) ? BTN_A : 0) | ((b & 32) ? BTN_B : 0) | ((b & 64) ? BOMB_BTN : 0);
}
static const unsigned char A8[9] = { 224, 192, 160, 0, 0, 128, 32, 64, 96 };   /* 8-way aim from (mx,my) */
/* the ship's motion, shared by the player and the ghost: a pure function of
 * the pad history and the arena clock, so a recorded stream reproduces its
 * path exactly. live counts the pylons' blocks against the session counter */
static void ship_step(unsigned pad, int *x, int *y, int *vx, int *vy, int live) {
    int mx = ((pad & BTN_RIGHT) ? 1 : 0) - ((pad & BTN_LEFT) ? 1 : 0), my = ((pad & BTN_DOWN) ? 1 : 0) - ((pad & BTN_UP) ? 1 : 0);
    int tx = mx * 512, ty = my * 512; if (mx && my) { tx = tx * 181 >> 8; ty = ty * 181 >> 8; }
    *vx += (tx - *vx) >> 2; *vy += (ty - *vy) >> 2;
    *x = clampi(*x + *vx, 10 << 8, (SW - 10) << 8); *y = clampi(*y + *vy, 10 << 8, (SH - 10) << 8);
    if (arena >= 3) arena_clamp(x, y, 10);           /* the ring and the crush walls hold you in */
    if (arena == 2) for (int i = 0; i < 4; i++) {    /* Pylons: static, and they block movement */
        int dx = (*x >> 8) - PYL[i][0], dy = (*y >> 8) - PYL[i][1], rr = PYLR + 10, d2 = dx * dx + dy * dy;
        if (d2 < rr * rr) {
            int d = (int)isqrt((unsigned)d2);
            if (!d) { dx = 0; dy = 1; d = 1; }       /* dead centre: lift out to the near edge */
            *x = (PYL[i][0] + dx * rr / d) << 8; *y = (PYL[i][1] + dy * rr / d) << 8;
            if (live) pyl_hits++;
        }
    }
}

static void step_play(unsigned pad, unsigned edge) {
    frame_t++; unlock_t++; sec_t++;
    if (edge & BTN_A) tc_press(0);
    if (edge & BTN_B) tc_press(1);
    tc_frame();
    if (edge & BOMB_BTN) bomb();
    int mx = ((pad & BTN_RIGHT) ? 1 : 0) - ((pad & BTN_LEFT) ? 1 : 0), my = ((pad & BTN_DOWN) ? 1 : 0) - ((pad & BTN_UP) ? 1 : 0);
    if (arena == 4) {                                    /* Crush: the walls close and open on a 2 s beat */
        int bt = sec_t % 120;
        wall_in = bt < 50 ? 0 : bt < 70 ? (bt - 50) * 90 / 20 : bt < 100 ? 90 : (120 - bt) * 90 / 20;
    } else wall_in = 0;
    ship_step(pad, &px, &py, &pvx, &pvy, 1);            /* the ship, and the pylons' block count */
    if (gh_on) {                                        /* the ghost races the best run so far */
        if (gh_i < gst_n) {
            unsigned b = unpack_pad(GST[gh_i++]);
            ship_step(b, &gpx, &gpy, &gh_vx, &gh_vy, 0);
            int qx = ((b & BTN_RIGHT) ? 1 : 0) - ((b & BTN_LEFT) ? 1 : 0), qy = ((b & BTN_DOWN) ? 1 : 0) - ((b & BTN_UP) ? 1 : 0);
            if (qx || qy) gh_aim = A8[(qy + 1) * 3 + (qx + 1)];
        } else gh_on = 0;
    }
    if (rec_n < RECB) {                                 /* the design doc 9 stream, one byte per frame */
        unsigned char b = pack_pad(pad); REC[rec_n++] = b; rec_crc = rec_crc * 33 + b;
    }
    if (mx || my) aim = A8[(my + 1) * 3 + (mx + 1)];
    int s = tc_speed(); tap_speed = s; music_tick();   /* M6: the music's tempo tracks this frame's effort */
    /* overdrive (design doc 6): 3 s above 0.8 tap speed doubles the chain gain
     * and whitens the ship; it ends the moment speed drops below 0.5 */
    if (s > 204) { if (++od_t >= 180 && !overdrive) { overdrive = 1; msg_t = 80; msg_k = 2; snd(800, 18, 8, 4); } }
    else od_t = 0;
    if (overdrive && s < 128) overdrive = 0;
    int chg = tc_charging(), hold = (pad & (BTN_A | BTN_B)) != 0;
    if (--fcd <= 0 && !chg) {
        int base = (hold || s > 13) ? 9 : 22; fcd = (base * 256 - (base - 3) * s + 128) >> 8;
        int n = 1 + (s > 90) + (s > 180); static const signed char OFF[3][3] = { { 0, 0, 0 }, { -4, 4, 0 }, { -9, 0, 9 } };
        for (int j = 0; j < n; j++) for (int k = 0; k < NB; k++) if (!B[k].on) {
            int a = aim + OFF[n - 1][j]; B[k].on = 1; B[k].life = 60;
            B[k].x = px + (COS(aim) * 8); B[k].y = py + (SIN(aim) * 8); B[k].vx = COS(a) * 6; B[k].vy = SIN(a) * 6; break;
        }
    }
    /* spawning: difficulty ramps with time; the vocabulary widens with unlock_t.
     * The Sentinel owns the arena while it is out: no waves, no swarm. */
    int d = clampi(frame_t * 256 / (60 * 150), 0, 256);
    if (!bo_on && sec_done()) boss_spawn();
    if (!bo_on && --spawn_cd <= 0) {
        spawn_cd = 60 - (40 * d >> 8); int n = 1 + (rnd(256) < d / 2) + (rnd(256) < d * 30 / 100);
        int tot = 0;
        for (int k = 0; k < K_KINDS; k++) if (kind_open(k) && wgt(k)) tot += wgt(k);
        for (int i = 0; i < n; i++) {
            int r = rnd(tot), k = K_SEEK;
            for (int q = 0; q < K_KINDS; q++) if (kind_open(q) && wgt(q)) { if (r < wgt(q)) { k = q; break; } r -= wgt(q); }
            if (k == K_WEAVER && weaver_alive()) k = K_SEEK;           /* one chain at a time */
            queue(k); }
    }
    /* force-intro: the first enemy of a newly unlocked kind is guaranteed */
    if (!bo_on) for (int k = 1; k < K_KINDS; k++)
        if (kind_open(k) && wgt(k) && !(kinds_seen & (1 << k)) && !kind_pending(k)) { queue(k); break; }
    if (!bo_on && --swarm_cd <= 0) {
        swarm_cd = 1500; msg_t = 100; msg_k = 0; snd(120, 18, 6, 3); int a0 = rnd(256);
        for (int i = 0; i < 14; i++) for (int k = 0; k < NM; k++) if (!M[k].on) { int a = a0 + i * 256 / 14; M[k].on = 1; M[k].k = K_SEEK; M[k].life = 60;
            M[k].x = clampi(SW / 2 + (COS(a) * (SW / 2 - 14) >> 8), 8, SW - 8); M[k].y = clampi(SH / 2 + (SIN(a) * (SH / 2 - 14) >> 8), 8, SH - 8); break; }
    }
    for (int i = 0; i < NM; i++) if (M[i].on && --M[i].life <= 0) { spawn_enemy(M[i].k, M[i].x, M[i].y); M[i].on = 0; }
    for (int k = 0; k < NB; k++) if (B[k].on) { B[k].x += B[k].vx; B[k].y += B[k].vy; grid_warp(B[k].x >> 8, B[k].y >> 8, 40, 22);
        if (--B[k].life <= 0 || B[k].x < 0 || B[k].x > SW << 8 || B[k].y < 0 || B[k].y > SH << 8) B[k].on = 0;
        else if (pyl_at(B[k].x >> 8, B[k].y >> 8, 1)) { B[k].on = 0; pyl_hits++; burst(B[k].x >> 8, B[k].y >> 8, C_WHITE, 3, 300); }
        else if (arena_out(B[k].x >> 8, B[k].y >> 8)) B[k].on = 0; }
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
        if (arena >= 3) arena_clamp(&e->x, &e->y, KIND[e->k].r);   /* the ring and the walls hold the waves too */
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
    if (bo_on) {                                       /* the boss: fight it, and do not let it ram you */
        boss_update();
        for (int k = 0; k < NB; k++) if (B[k].on) {
            if (boss_strike(B[k].x >> 8, B[k].y >> 8)) { B[k].on = 0; if (!bo_on) break; }
        }
        if (bo_on && inv <= 0) {
            int bx = (px >> 8) - (bo_x >> 8), by = (py >> 8) - (bo_y >> 8), rr = BOR + 6;
            if (bo_k != 2 && bx * bx + by * by < rr * rr) { hit_player(); if (state != 1) return; }
            for (int i = 0; i < 9 && bo_on; i++) if (BP[i].on) {
                int qx = (px >> 8) - (BP[i].x >> 8), qy = (py >> 8) - (BP[i].y >> 8), pr = BP[i].r + 6;
                if (qx * qx + qy * qy < pr * pr) { hit_player(); if (state != 1) return; }
            }
        }
    }
    for (int i = 0; i < NR; i++) if (R[i].on) {
        R[i].r += (((R[i].R - R[i].r) * 46) >> 8) + 2; grid_ring(R[i].x, R[i].y, R[i].r, 28, 700);
        cur_ring = R[i].b ? i : -1;
        for (int q = 0; q < NE; q++) if (E[q].on && evis(&E[q])) { int dx = (E[q].x >> 8) - R[i].x, dy = (E[q].y >> 8) - R[i].y, rr = R[i].r + KIND[E[q].k].r;
            if (dx * dx + dy * dy < rr * rr) kill_enemy(&E[q], 256); }
        cur_ring = -1;
        if (R[i].b && bo_on) {                                    /* the shockwave sweeps the boss: the core
                                                                 * once, and every part the ring crosses once
                                                                 * (design doc 5: "Bomb it") */
            int hx = bo_x >> 8, hy = bo_y >> 8, dx = hx - R[i].x, dy = hy - R[i].y, rr = R[i].r + BOR;
            if (!R[i].d && bo_k != 2 && dx * dx + dy * dy < rr * rr) { R[i].d = 1; boss_strike(hx, hy); }
            for (int q = 0; q < 9 && bo_on; q++) if (BP[q].on && !(R[i].hm & (unsigned short)(1 << q))) {
                int qx = (BP[q].x >> 8) - R[i].x, qy = (BP[q].y >> 8) - R[i].y, qr = R[i].r + BP[q].r;
                if (qx * qx + qy * qy < qr * qr) { R[i].hm |= (unsigned short)(1 << q); boss_strike(BP[q].x >> 8, BP[q].y >> 8); }
            }
        }
        for (int q = 0; q < NF; q++) if (F[q].on) { int dx = (F[q].x >> 8) - R[i].x, dy = (F[q].y >> 8) - R[i].y, rr = R[i].r + 3;
            if (dx * dx + dy * dy < rr * rr) F[q].on = 0; }
        for (int q = 0; q < NN; q++) if (MN[q].on) { int dx = MN[q].x - R[i].x, dy = MN[q].y - R[i].y, rr = R[i].r + 6;
            if (dx * dx + dy * dy < rr * rr) { MN[q].on = 0; burst(MN[q].x, MN[q].y, RGB(255, 240, 200), 6, 400); } }
        if (--R[i].life <= 0) {
            R[i].on = 0;
            if (R[i].b && R[i].n >= 8) {                /* clean sweep (design doc 6): flat bonus, quarter charge back */
                sweeps++; score_add(SWEEP_BONUS); charge = clampi(charge + R[i].cr, 0, 25600); msg_t = 60; msg_k = 4; snd(1000, 14, -20, 4); }
        }
    }
    for (int k = 0; k < NS; k++) if (S[k].on) { S[k].x += S[k].vx; S[k].y += S[k].vy; S[k].vx -= S[k].vx >> 5; S[k].vy -= S[k].vy >> 5; if (--S[k].life <= 0) S[k].on = 0; }
    if (inv > 0) inv--;
    if (mult_t > 0 && --mult_t == 0) mult = 256;
    if (frame_t % 60 == 0) secs = frame_t / 60;
}

/* ---- rendering ---- */
static void draw_boss(void) {                           /* one shape per boss (design doc 5) */
    int x = bo_x >> 8, y = bo_y >> 8, i;
    if (bo_k == 1) {                                    /* the Sentinel: core + four plates on the spinning frame */
        for (i = 0; i < 4; i++) if (bo_php[i] > 0) {
            int a = bo_a + i * 64, qx = x + (COS(a) * BOR >> 8), qy = y + (SIN(a) * BOR >> 8);
            poly(qx, qy, 5, 4, -bo_a + i * 64, C_YELLOW, RGB(90, 75, 30));
            glow_line(x, y, qx, qy, RGB(60, 50, 20), RGB(40, 34, 14));
        }
        poly(x, y, BOR - 2, 6, bo_a, boss_plates ? RGB(159, 232, 255) : C_ORANGE, RGB(25, 60, 80));
        return;
    }
    for (i = 0; i < 9; i++) if (BP[i].on) {
        int qx = BP[i].x >> 8, qy = BP[i].y >> 8;
        if (bo_k == 2) {                                /* the Hydra: green hexagons, smaller with every split */
            poly(qx, qy, BP[i].r, 6, BP[i].a + bo_a, RGB(64, 255, 144), RGB(15, 80, 45));
            poly(qx, qy, BP[i].r > 8 ? BP[i].r - 8 : 2, 3, -bo_a, C_WHITE, RGB(20, 80, 40));
        } else if (bo_k == 3) {                         /* the Orbiter: satellites on spokes, firing inward */
            glow_line(x, y, qx, qy, RGB(20, 70, 70), RGB(10, 40, 40));
            poly(qx, qy, BP[i].r, 4, BP[i].a + bo_a, RGB(60, 230, 220), RGB(12, 60, 60));
        } else circle(qx, qy, BP[i].r, 3, RGB(255, 110, 200));   /* the Weaver's segments */
    }
    if (bo_k == 3) poly(x, y, BOR - 4, 6, -bo_a, RGB(159, 232, 255), RGB(25, 60, 80));
    else if (bo_k == 4) poly(x, y, BOR, 6, bo_a, RGB(255, 110, 200), RGB(90, 30, 70));
    else if (bo_k == 5) {                               /* the Core: the phase decides the colour */
        unsigned c = bo_ph == 1 ? C_VIOLET : bo_ph == 2 ? C_ORANGE : C_WHITE;
        poly(x, y, BOR + 4, 8, bo_a, c, RGB(45, 30, 70));
        circle(x, y, BOR + 12 + bo_ph * 5, 2, c);
    }
}
static void draw_world(void) {
    int sh = shake > 0 ? shake : 0; shx = sh ? rnd(2 * sh + 1) - sh : 0; shy = sh ? rnd(2 * sh + 1) - sh : 0;
    int s = tap_speed;
    /* grid: three brightness levels; points that are displaced light up.
     * Sector 5 is the design's dark grid: it never lights up at all */
    for (int j = 0; j < GY; j++) for (int i = 0; i < GX; i++) {
        int k = j * GX + i, dd = iabs(gx[k] - i * GSP * 256) + iabs(gy[k] - j * GSP * 256);
        unsigned col = arena == 5 ? C_GRID0 : (dd > 5 * 256 ? C_GRID2 : (dd > 256 ? C_GRID1 : (s > 150 ? C_GRID1 : C_GRID0)));
        if (i + 1 < GX) line(gx[k] >> 8, gy[k] >> 8, gx[k + 1] >> 8, gy[k + 1] >> 8, 1, col);
        if (j + 1 < GY) line(gx[k] >> 8, gy[k] >> 8, gx[k + GX] >> 8, gy[k + GX] >> 8, 1, col);
    }
    if (arena == 3) circle(RGX, RGY, RGR, 2, C_TEAL);            /* the ring's wall */
    else {
        glow_line(2, 2, SW - 3, 2, C_GRID1, C_GRID0); glow_line(SW - 3, 2, SW - 3, SH - 3, C_GRID1, C_GRID0);
        glow_line(SW - 3, SH - 3, 2, SH - 3, C_GRID1, C_GRID0); glow_line(2, SH - 3, 2, 2, C_GRID1, C_GRID0);
    }
    if (arena == 2) for (int i = 0; i < 4; i++) {                /* the pylons: static furniture */
        poly(PYL[i][0], PYL[i][1], PYLR, 6, 0, C_VIOLET, RGB(55, 30, 85));
        circle(PYL[i][0], PYL[i][1], PYLR + 5, 1, RGB(95, 55, 145));
    }
    if (arena == 4 && wall_in > 0) {                             /* the crush walls */
        glow_line(wall_in, 2, wall_in, SH - 3, C_CRUSH, RGB(110, 28, 28));
        glow_line(SW - wall_in, 2, SW - wall_in, SH - 3, C_CRUSH, RGB(110, 28, 28));
    }
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
    if (bo_on) draw_boss();
    if (gh_on && state == 1) {                            /* the ghost: your best run, flown again */
        unsigned gcol = RGB(70, 110, 150);                /* a faint outline, no glow: it is a memory */
        int x = gpx >> 8, y = gpy >> 8;
        int fx = x + (COS(gh_aim) * 13 >> 8), fy = y + (SIN(gh_aim) * 13 >> 8);
        int lx = x + (COS(gh_aim + 106) * 11 >> 8), ly = y + (SIN(gh_aim + 106) * 11 >> 8), rx = x + (COS(gh_aim - 106) * 11 >> 8), ry = y + (SIN(gh_aim - 106) * 11 >> 8);
        int bx = x - (COS(gh_aim) * 4 >> 8), by = y - (SIN(gh_aim) * 4 >> 8);
        line(fx, fy, lx, ly, 1, gcol); line(lx, ly, bx, by, 1, gcol); line(bx, by, rx, ry, 1, gcol); line(rx, ry, fx, fy, 1, gcol);
    }
    if (state == 1 || state == 0) {
        int x = px >> 8, y = py >> 8;
        if (!(inv > 0 && ((frame_t >> 2) & 1))) {
            if (charge > 400) { int rr = 14 + (charge >> 9); circle(x, y, rr, 2, charge > 24000 ? C_WHITE : C_ORANGE); }
            unsigned core = (s > 150 || overdrive) ? C_WHITE : RGB(159, 255, 224), glow = RGB(25, 80, 70);
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
    text_draw(SW / 2 - 24, 6, "S", RGB(120, 150, 170)); text_u32(SW / 2 - 16, 6, (unsigned)sector, 2, RGB(120, 150, 170));   /* sector N (loop L) */
    text_draw(SW / 2 - 16, 16, "L", RGB(90, 110, 130)); text_u32(SW / 2 - 8, 16, (unsigned)loop, 2, RGB(90, 110, 130));
    if (overdrive) text_draw(8, 26, "DRIVE", C_WHITE);
    text_draw(8, SH - 30, "TAP", RGB(150, 170, 190)); arcade1_rect(40, SH - 29, 100, 6, RGB(20, 20, 50)); arcade1_rect(40, SH - 29, 100 * tap_speed >> 8, 6, C_YELLOW);
    text_draw(8, SH - 18, "PWR", RGB(150, 170, 190)); arcade1_rect(40, SH - 17, 100, 6, RGB(20, 20, 50)); arcade1_rect(40, SH - 17, 100 * charge / 25600, 6, charge >= 25000 ? C_WHITE : C_ORANGE);
    if (bo_on) {                                        /* the boss's health, centre-top */
        unsigned bc = bo_k == 1 ? (boss_plates ? C_YELLOW : C_ORANGE)
                    : bo_k == 2 ? RGB(64, 255, 144) : bo_k == 3 ? C_TEAL
                    : bo_k == 4 ? RGB(255, 110, 200) : C_VIOLET;
        arcade1_rect(SW / 2 - 60, 26, 120, 5, RGB(20, 20, 50));
        arcade1_rect(SW / 2 - 60, 26, 120 * boss_hp / (bo_max > 0 ? bo_max : 1), 5, bc);
    }
    if (msg_t > 0 && state == 1) {
        msg_t--;
        if (msg_k == 1) text_draw(SW / 2 - 32, 44, "SENTINEL", C_ORANGE);
        else if (msg_k == 2) text_draw(SW / 2 - 40, 44, "OVERDRIVE", C_WHITE);
        else if (msg_k == 3) text_draw(SW / 2 - 16, 44, "CLEAR", C_HUD);
        else if (msg_k == 4) text_draw(SW / 2 - 24, 44, "SWEEP", C_YELLOW);
        else if (msg_k == 6) text_draw(SW / 2 - 15, 44, "HYDRA", RGB(64, 255, 144));
        else if (msg_k == 7) text_draw(SW / 2 - 21, 44, "ORBITER", C_TEAL);
        else if (msg_k == 8) text_draw(SW / 2 - 18, 44, "WEAVER", RGB(255, 110, 200));
        else if (msg_k == 9) text_draw(SW / 2 - 12, 44, "CORE", C_VIOLET);
        else text_draw(SW / 2 - 16, 44, "SWARM", RGB(255, 63, 164));
    }
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
