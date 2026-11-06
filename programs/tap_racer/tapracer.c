/* TAP RACER - a neon pseudo-3D racer for ARCADE-1. Your tap speed is your throttle.
 *
 * Ported from the browser prototype (tap_racer.html). Same structure as Lumen:
 *   game state --build_dl()--> VECTOR DISPLAY LIST --vec_submit()--> screen
 * The game draws nothing itself. vec_submit() is the SOFTWARE FALLBACK (lines as runs of rects, intensity in 8
 * levels, glow as a dim wide pass); a machine with a vector block replaces only that function.
 * Display list: arc1_vector_displaylist_spec.md; the guest cap here is 900 segments (the VM's 64 KiB of RAM).
 * The 3D projection is fixed point (no FPU): Q12 scale from one divide per road boundary.
 *
 * Mechanic (rules identical to tapcad.h / Neon Tap / Lumen):
 *   nothing held ... coast        hold A or B ... cruise        tap one button fast ... top speed
 *   alternate A/B .. charge (you slow down)     same button again, or START ... boost
 * Fast corners throw you wide (centrifugal pull grows with speed), so tap speed trades against control.
 * D-pad left/right steers. Orange road is rough and caps your speed unless you are boosting.
 * One lap against three rivals. Freestanding RV32IM C, integer only, deterministic.
 *
 * Observable globals: state mode(0 title 1 count 2 race 3 finish) pz px pv sp rank boost charge tap_speed dl_n tframe fin
 */
#include "arcade1.h"
#include "text.h"

#define SCRW 480
#define SCRH 270
#define RGB(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

int mode, pz, px, pv, sp, rank, boost, charge, tap_speed, dl_n, tframe, fin;
static const short SINT[256] = {
#include "tapracer_sin.inc"
};
#define SIN(i) ((int)SINT[(i) & 255])
#define COS(i) ((int)SINT[((i) + 64) & 255])
static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static unsigned rs = 11;
static int rnd(int n) { rs = rs * 1664525u + 1013904223u; return (int)(((rs >> 16) * (unsigned)n) >> 16); }

/* ---- tap cadence (tapcad.h rules; times in ms) ---- */
static unsigned now_ms, tc_last, tc_chg_t, tc_tick;
static int tc_ema, tc_spd, tc_btn, tc_has, tc_has_chg;
static void tc_reset(void) { tc_last = tc_chg_t = tc_tick = 0; tc_ema = tc_spd = tc_btn = tc_has = tc_has_chg = 0; charge = 0; }
static int tc_press(int btn) {                      /* -1 debounced, 1 alternation (charging), 0 plain tap */
    unsigned t = now_ms;
    if (tc_has) {
        int iv = (int)(t - tc_last);
        if (iv < 25) return -1;
        tc_ema = iv < 700 ? (tc_ema ? (tc_ema + iv) / 2 : iv) : 0;
        if (btn != tc_btn && iv < 400) {
            charge = clampi(charge + 512 + 10 * clampi((400 - iv) * 256 / 330, 0, 256), 0, 25600);
            tc_chg_t = t; tc_has_chg = 1; tc_btn = btn; tc_last = t; return 1;
        }
    }
    tc_btn = btn; tc_last = t; tc_has = 1;
    tc_spd = tc_ema ? clampi((300 - tc_ema) * 256 / 230, 0, 256) : 0;
    return 0;
}
static int tc_speed(void) { if (!tc_has) return 0; int idle = (int)(now_ms - tc_last), f = idle > 250 ? 256 - (idle - 250) * 256 / 350 : 256; return (tc_spd * clampi(f, 0, 256)) >> 8; }
static int tc_charging(void) { return tc_has_chg && (int)(now_ms - tc_chg_t) < 350; }
static void tc_frame(void) {
    unsigned dt = now_ms - tc_tick; tc_tick = now_ms;
    if (charge > 0 && tc_has && now_ms - tc_last > 500) { unsigned d = dt * 512u / 100u; charge = d >= (unsigned)charge ? 0 : charge - (int)d; }
}

/* ======================= display list ======================= */
#define DLCAP 900
typedef struct { short x0, y0, x1, y1; unsigned char col, inten, w, fl; } seg_t;
typedef struct { short x, y; unsigned char col, inten; char s[26]; } txt_t;
static seg_t DL[DLCAP];
static txt_t TX[12];
static int tx_n, dl_peak;
static void seg(int x0, int y0, int x1, int y1, int col, int inten, int w) {
    if (inten <= 3 || dl_n >= DLCAP) return;
    if ((x0 < -40 && x1 < -40) || (x0 > SCRW + 40 && x1 > SCRW + 40) || (y0 < -40 && y1 < -40) || (y0 > SCRH + 40 && y1 > SCRH + 40)) return;
    seg_t *s = &DL[dl_n++];
    s->x0 = (short)(clampi(x0, -900, 900) * 16); s->y0 = (short)(clampi(y0, -900, 900) * 16); s->x1 = (short)(clampi(x1, -900, 900) * 16); s->y1 = (short)(clampi(y1, -900, 900) * 16);
    s->col = (unsigned char)col; s->inten = (unsigned char)(inten > 255 ? 255 : inten); s->w = (unsigned char)w; s->fl = 0;
}
static void poly(int cx, int cy, int r, int n, int a, int col, int inten, int w) {
    int px_ = cx + (COS(a) * r >> 8), py_ = cy + (SIN(a) * r >> 8), fx = px_, fy = py_;
    for (int i = 1; i <= n; i++) { int q = a + i * 256 / n, x = i == n ? fx : cx + (COS(q) * r >> 8), y = i == n ? fy : cy + (SIN(q) * r >> 8); seg(px_, py_, x, y, col, inten, w); px_ = x; py_ = y; }
}
static void text_at(int x, int y, const char *s, int col, int inten) {
    if (tx_n >= 12) return;
    txt_t *t = &TX[tx_n++]; t->x = (short)x; t->y = (short)y; t->col = (unsigned char)col; t->inten = (unsigned char)(inten > 255 ? 255 : inten);
    int i = 0;
    for (; s[i] && i < 25; i++) t->s[i] = s[i];
    t->s[i] = 0;
}
static int put_num(char *o, int v, int minw) {      /* writes digits, returns length */
    char t[12]; int n = 0; if (v == 0) t[n++] = '0';
    while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n < minw) t[n++] = '0';
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    return n;
}
/* palette: 0 cyan, 1 blue, 2 magenta, 3 yellow, 4 orange, 5 white, 6 green, 7 red, 8 lavender */
static const unsigned char PAL[9][3] = { {80,255,240}, {70,110,255}, {255,63,164}, {255,224,138}, {255,122,61}, {255,255,255}, {80,255,150}, {255,80,80}, {190,170,255} };

/* ---- software fallback renderer for the display list ---- */
static int shade565(int col, int inten, int mul256) {
    int lv = (inten + 16) >> 5; if (lv > 8) lv = 8;
    int r = PAL[col][0] * lv / 8 * mul256 >> 8, g = PAL[col][1] * lv / 8 * mul256 >> 8, b = PAL[col][2] * lv / 8 * mul256 >> 8;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
static void line(int x0, int y0, int x1, int y1, int t, unsigned col) {
    int dx = iabs(x1 - x0), dy = iabs(y1 - y0), h = t >> 1;
    if (dx >= dy) {
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx / 2, y = y0, rs0 = x0, x = x0;
        for (int i = 0; i <= dx; i++, x += sx) {
            int last = (i == dx), step = 0; err -= dy; if (err < 0) { err += dx; step = 1; }
            if (step || last) { int a = rs0 < x ? rs0 : x; arcade1_rect(a, y - h, iabs(x - rs0) + 1, t, col); rs0 = x + sx; y += sy; }
        }
    } else {
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dy / 2, x = x0, rs0 = y0, y = y0;
        for (int i = 0; i <= dy; i++, y += sy) {
            int last = (i == dy), step = 0; err -= dx; if (err < 0) { err += dy; step = 1; }
            if (step || last) { int a = rs0 < y ? rs0 : y; arcade1_rect(x - h, a, t, iabs(y - rs0) + 1, col); rs0 = y + sy; x += sx; }
        }
    }
}
static void vec_submit(void) {
    for (int i = 0; i < dl_n; i++) { const seg_t *s = &DL[i]; if (s->w < 2) continue; line(s->x0 >> 4, s->y0 >> 4, s->x1 >> 4, s->y1 >> 4, s->w == 2 ? 3 : 5, (unsigned)shade565(s->col, s->inten, 70)); }
    for (int i = 0; i < dl_n; i++) { const seg_t *s = &DL[i]; line(s->x0 >> 4, s->y0 >> 4, s->x1 >> 4, s->y1 >> 4, s->w >= 3 ? 2 : 1, (unsigned)shade565(s->col, s->inten, 256)); }
    arcade1_cam(0, 0);
    for (int i = 0; i < tx_n; i++) text_draw(TX[i].x, TX[i].y, TX[i].s, (unsigned)shade565(TX[i].col, TX[i].inten, 256));
}

/* ======================= track ======================= */
#define SL 200                      /* segment length, world units */
#define RW 2000                     /* road half width */
#define DRAW 72                     /* segments drawn */
#define CAMH 1000
#define MAXSP 190
#define MAXSEG 1300
static short SY[MAXSEG], SCV[MAXSEG]; static unsigned char SZN[MAXSEG];   /* height, curve (Q8), zone (1 = rough) */
static int nseg_all, nseg, track_len;     /* track_len in world units */
static int ease(int a, int b, int num, int den) { int i = num * 128 / den; return a + (b - a) * (256 - COS(i)) / 512; }
static void add_seg(int curve_q8, int y, int zone) { if (nseg_all < MAXSEG) { SY[nseg_all] = (short)y; SCV[nseg_all] = (short)curve_q8; SZN[nseg_all] = (unsigned char)zone; nseg_all++; } }
static void road(int en, int hold, int lv, int curve10, int dy, int zone) {   /* curve in tenths */
    int sy = nseg_all ? SY[nseg_all - 1] : 0, ey = sy + dy * SL, tot = en + hold + lv, n = 0, c = curve10 * 256 / 10;
    for (int i = 0; i < en; i++, n++) add_seg(ease(0, c, i, en), ease(sy, ey, n, tot), zone);
    for (int i = 0; i < hold; i++, n++) add_seg(c, ease(sy, ey, n, tot), zone);
    for (int i = 0; i < lv; i++, n++) add_seg(ease(c, 0, i, lv), ease(sy, ey, n, tot), zone);
}
static void build_track(void) {
    nseg_all = 0;
    road(0, 40, 0, 0, 0, 0); road(30, 40, 30, 20, 0, 0); road(20, 30, 20, 0, 20, 0); road(25, 25, 25, -30, -10, 0); road(20, 40, 20, 0, 0, 1);
    road(25, 30, 25, 35, 30, 0); road(20, 20, 20, -20, -40, 0); road(30, 40, 30, -40, 0, 0); road(20, 50, 20, 0, 15, 0); road(30, 30, 30, 30, -15, 0);
    road(20, 30, 20, 0, 0, 1); road(30, 30, 30, -35, 10, 0); road(20, 40, 20, 20, -10, 0); road(30, 40, 10, 0, 0, 0);
    nseg = nseg_all; track_len = nseg * SL;
    for (int i = 0; i < DRAW + 20; i++) add_seg(0, SY[nseg_all - 1], 0);
}
static int seg_at(int zunits) { int i = (zunits / SL) % nseg_all; return i < 0 ? 0 : i; }

/* ======================= game ======================= */
typedef struct { int z, x, v, tp, fin; short col; } car_t;     /* z in Q8 units, x and v Q8 */
static car_t CAR[3];
static int lean, shake, flash, count_t, over_t, frame_t;
static unsigned char SKYR[48];
static unsigned prev_pad;
static const int CAR_COL[3] = { 2, 3, 8 };

#define AUDIO_FREQ (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x300u))
#define AUDIO_CTRL (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x304u))
static int snd_f, snd_n, snd_sw, snd_pri;
static void snd(int f, int n, int sw, int pri) { if (snd_n > 0 && pri < snd_pri) return; snd_f = f; snd_n = n; snd_sw = sw; snd_pri = pri; }
static void snd_frame(void) {
    if (snd_n > 0) { AUDIO_FREQ = (unsigned)snd_f; __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1; snd_f += snd_sw; if (snd_f < 40) snd_f = 40; snd_n--; }
    else if (mode == 2) { AUDIO_FREQ = (unsigned)(70 + sp * 2 + (boost > 0 ? 80 : 0)); __asm__ volatile("" ::: "memory"); AUDIO_CTRL = (frame_t & 3) == 0 ? 1 : 0; }
    else AUDIO_CTRL = 0;
}

static void start_race(void) {
    rs = 11; mode = 1; tc_reset(); frame_t = 0; count_t = 210; tframe = 0; fin = 0; pz = 0; px = 0; pv = 0; lean = 0; boost = 0; shake = 0; flash = 0; over_t = 0; snd_n = 0;
    CAR[0] = (car_t){ 7 * SL * 256, -140, 0, 210, 0, 0 }; CAR[1] = (car_t){ 14 * SL * 256, 128, 0, 195, 0, 1 }; CAR[2] = (car_t){ 21 * SL * 256, 0, 0, 179, 0, 2 };
}
static int rank_now(void) { int r = 1; for (int i = 0; i < 3; i++) if (CAR[i].z > pz || (CAR[i].fin && (!fin || CAR[i].fin < fin))) r++; return r; }
static void release(void) { if (mode != 2 || charge < 2048) return; boost = 40 + 110 * (charge / 100) / 256; flash = 10; charge = 0; snd(500, 18, 14, 6); }
static void do_tap(int b) {
    if (mode == 0 || (mode == 3 && over_t > 60)) { start_race(); return; }
    if (mode != 2) return;
    int t = tc_press(b); if (t < 0 || t == 1) return;
    if (charge >= 2048) release();
}

static void step_race(unsigned pad) {
    int s = tc_speed(); tap_speed = s; tc_frame();
    int chg = tc_charging(), held = (pad & (BTN_A | BTN_B)) != 0, si = seg_at(pz >> 8);
    int tp = chg ? 77 : (held || s > 13) ? 141 + (115 * s >> 8) : 77;
    if (boost > 0) { tp += 90; boost--; }
    int slope = (SY[(si + 1) % nseg_all] - SY[si]) * 256 / SL;
    if (slope > 0) tp = tp * (256 - clampi(slope * 4, 0, 64)) >> 8; else tp = tp * (256 + clampi(-slope * 2, 0, 26)) >> 8;
    int off = iabs(px) > 269;
    if (SZN[si] == 1 && boost <= 0 && tp > 107) tp = 107;
    if (off && tp > 90) tp = 90;
    pv += ((tp * MAXSP) - pv) * 11 >> 9; sp = pv / MAXSP;
    int steer = ((pad & BTN_RIGHT) ? 1 : 0) - ((pad & BTN_LEFT) ? 1 : 0), dxq = sp / 30;
    lean += ((steer << 8) - lean) * 51 >> 8;
    px += steer * dxq; px -= (((dxq * sp >> 8) * SCV[si] >> 8) * 77) >> 8; px = clampi(px, -563, 563);
    pz += pv; shake = off ? 2 : boost > 0 ? 1 : 0; if (flash > 0) flash--;
    for (int i = 0; i < 3; i++) {
        car_t *c = &CAR[i]; int cs = seg_at(c->z >> 8), t2 = c->tp;
        if (pz - c->z > SL * 40 * 256) t2 += 13;
        if (c->z - pz > SL * 60 * 256) t2 -= 13;
        t2 = t2 * (256 + (SIN((tframe + CAR_COL[i] * 40) * 209 >> 8) * 13 >> 8)) >> 8; if (SZN[cs] == 1 && t2 > 128) t2 = 128;
        c->v += ((t2 * MAXSP) - c->v) * 5 >> 8; c->z += c->v;
        c->x += ((SIN((tframe + CAR_COL[i] * 70) * 116 >> 8) >> 1) - c->x) >> 7;
        if (!c->fin && c->z >= track_len * 256) c->fin = tframe;
        if (iabs(c->z - pz) < SL * 256 * 12 / 10 && iabs(c->x - px) < 72) { px += (px > c->x ? 10 : -10); pv = pv * 252 >> 8; snd(220, 3, -10, 3); }
    }
    if (pz >= track_len * 256) { fin = tframe; mode = 3; over_t = 0; rank = rank_now(); snd(600, 30, 10, 8); }
}

/* ======================= build the display list ======================= */
static int BX[DRAW + 2], BY[DRAW + 2], BW[DRAW + 2], BS[DRAW + 2], CLIP[DRAW + 2];
static void car_shape(int x, int y, int w, int col, int inten, int lean_q8, int boosting, int sp256) {
    int h = w / 2; x += lean_q8 * w * 15 / 100 >> 8;
    int ax[6] = { -w, w, w * 9 / 10, w * 55 / 100, -w * 55 / 100, -w * 9 / 10 }, ay[6] = { 0, 0, -h * 55 / 100, -h, -h, -h * 55 / 100 };
    for (int i = 0; i < 6; i++) { int j = (i + 1) % 6; seg(x + ax[i], y + ay[i], x + ax[j], y + ay[j], col, inten, 2); }
    seg(x - w * 55 / 100, y - h, x - w * 4 / 10, y - h * 62 / 100, col, inten * 8 / 10, 1); seg(x + w * 55 / 100, y - h, x + w * 4 / 10, y - h * 62 / 100, col, inten * 8 / 10, 1);
    seg(x - w * 4 / 10, y - h * 62 / 100, x + w * 4 / 10, y - h * 62 / 100, col, inten * 8 / 10, 1);
    seg(x - w * 8 / 10, y, x - w * 8 / 10, y + h * 18 / 100, 5, inten * 8 / 10, 2); seg(x + w * 8 / 10, y, x + w * 8 / 10, y + h * 18 / 100, 5, inten * 8 / 10, 2);
    seg(x - w * 85 / 100, y - h * 3 / 10, x - w * 55 / 100, y - h * 3 / 10, 7, inten, 2); seg(x + w * 55 / 100, y - h * 3 / 10, x + w * 85 / 100, y - h * 3 / 10, 7, inten, 2);
    if (sp256 > 0) { int f = w * (15 + sp256 * 50 / 256 + (boosting ? 50 : 0)) / 100 * (8 + (frame_t & 3)) / 10;
        seg(x - w * 3 / 10, y - h / 10, x - w * 3 / 10, y + f, boosting ? 0 : 4, inten, 2); seg(x + w * 3 / 10, y - h / 10, x + w * 3 / 10, y + f, boosting ? 0 : 4, inten, 2); }
}
static void build_dl(void) {
    dl_n = 0; tx_n = 0;
    int base = seg_at(pz >> 8), zr = (pz >> 8) % SL, pct = zr * 256 / SL, camx = px * RW >> 8;
    int camy = CAMH + SY[base] + (SY[base + 1] - SY[base]) * pct / 256, camz = pz >> 8;
    int x = 0, dxc = -(SCV[base] * pct >> 8);                       /* Q8 world x and its slope */
    int skyoff = (base * 2) / 5, hy = SCRH / 2 + 14 - SY[base] * 2 / 100;
    for (int i = 0; i < 14; i++) { int sx = ((i * 37 + 400 - skyoff * 4) % SCRW + SCRW) % SCRW, sy = 6 + (SKYR[i] * 50 >> 8); seg(sx, sy, sx + 1, sy + 1, 5, 90, 1); }
    poly(336, 70, 26, 16, 0, 4, 150, 2); for (int k = 0; k < 3; k++) seg(310, 72 + k * 8, 362, 72 + k * 8, 2, 70, 1);
    for (int layer = 0; layer < 2; layer++) { int off = (-skyoff * (layer ? 5 : 5 / 2)) % 60, ppx = -60, ppy = hy;
        for (int i = 0; i <= 10; i++) { int nx = i * 54 + off, ny = hy - (6 + (SKYR[(i * 3 + layer * 7) % 48] * (layer ? 26 : 40) >> 8)); if (i > 0) seg(ppx, ppy, nx, ny, 1, layer ? 55 : 90, 1); ppx = nx; ppy = ny; } }
    seg(0, hy, SCRW, hy, 0, 120, 1);
    int maxy = SCRH + 20;
    for (int n = 0; n <= DRAW; n++) {                                /* boundary n: world z = (base+n)*SL */
        int idx = (base + n) % nseg_all, wz = (base + n) * SL, tz = wz - camz;
        if (tz <= 1) { BS[n] = 0; BX[n] = BY[n] = BW[n] = 0; }
        else { int sc = 201 * 4096 / tz; BS[n] = sc; BX[n] = SCRW / 2 + (((x >> 8) - camx) * sc >> 12); BY[n] = SCRH / 2 + 14 - (((SY[idx] - camy) * sc >> 12) * 9 >> 4); BW[n] = RW * sc >> 12; }
        x += dxc; dxc += SCV[idx];
    }
    for (int n = 0; n < DRAW; n++) {
        CLIP[n] = maxy; if (!BS[n] || !BS[n + 1] || BY[n + 1] >= BY[n] || BY[n + 1] >= maxy) continue;
        int idx = (base + n) % nseg_all, gi = base + n, u = n * 256 / DRAW, f = 256 - u, I = (f * f >> 8) * 150 / 256 + (f * 105 >> 8);
        int x1 = BX[n], y1 = BY[n], w1 = BW[n], x2 = BX[n + 1], y2 = BY[n + 1], w2 = BW[n + 1], rough = SZN[idx] == 1, ec = rough ? 4 : 0;
        seg(x1 - w1, y1, x2 - w2, y2, ec, I, 2); seg(x1 + w1, y1, x2 + w2, y2, ec, I, 2);
        if (gi % 2 == 0) seg(x1 - w1, y1, x1 + w1, y1, rough ? 4 : 1, I / 2, 1);
        if (((gi >> 2) & 1) == 0) seg(x1, y1, x2, y2, 3, I * 8 / 10, 1);
        seg(x1 - w1 * 16 / 10, y1, x2 - w2 * 16 / 10, y2, 1, I * 28 / 100, 1); seg(x1 + w1 * 16 / 10, y1, x2 + w2 * 16 / 10, y2, 1, I * 28 / 100, 1);
        seg(x1 - w1 * 32 / 10, y1, x2 - w2 * 32 / 10, y2, 1, I * 14 / 100, 1); seg(x1 + w1 * 32 / 10, y1, x2 + w2 * 32 / 10, y2, 1, I * 14 / 100, 1);
        if (gi % 8 == 0) { int hh = w1 * 35 / 100; seg(x1 - w1 * 125 / 100, y1, x1 - w1 * 125 / 100, y1 - hh, 2, I * 9 / 10, 1); seg(x1 + w1 * 125 / 100, y1, x1 + w1 * 125 / 100, y1 - hh, 2, I * 9 / 10, 1); }
        if (gi == nseg - 1 || gi == 3) { int hh = w1 * 6 / 10; seg(x1 - w1, y1, x1 + w1, y1, 5, I, 3); seg(x1 - w1, y1 - hh, x1 + w1, y1 - hh, 5, I, 3); seg(x1 - w1, y1, x1 - w1, y1 - hh, 5, I, 3); seg(x1 + w1, y1, x1 + w1, y1 - hh, 5, I, 3); }
        maxy = y1;
    }
    for (int n = DRAW - 1; n >= 1; n--) for (int i = 0; i < 3; i++) {
        if ((CAR[i].z >> 8) / SL != base + n || !BS[n]) continue;
        int cx = BX[n] + BW[n] * CAR[i].x / 256, cy = BY[n], cw = BW[n] * 12 / 100, f = 256 - n * 256 / DRAW;
        if (cy >= CLIP[n] + 60) continue;
        car_shape(cx, cy, cw, CAR_COL[i], f * f >> 8, 0, 0, 77);
    }
    if (mode >= 1) car_shape(SCRW / 2, SCRH - 30, 38, 0, 255, lean, boost > 0, sp);
    /* HUD */
    char b[26]; int k;
    text_at(6, 4, "SPEED", 8, 200); text_at(6, 14, "TAP", 8, 170); text_at(6, 24, "PWR", 8, 170);
    seg(46, 8, 116, 8, 1, 60, 1); seg(46, 8, 46 + 70 * sp / 256, 8, sp > 230 ? 5 : 6, 255, 2);
    seg(46, 18, 116, 18, 1, 60, 1); seg(46, 18, 46 + 70 * tap_speed / 256, 18, 3, 255, 2);
    seg(46, 28, 116, 28, 1, 60, 1); seg(46, 28, 46 + 70 * charge / 25600, 28, 4, 255, 2);
    k = 0; b[k++] = 'P'; b[k++] = 'O'; b[k++] = 'S'; b[k++] = ' '; b[k++] = (char)('0' + (mode == 3 ? rank : rank_now())); b[k++] = '/'; b[k++] = '4'; b[k] = 0; text_at(SCRW - 86, 4, b, 3, 230);
    k = 0; b[k++] = 'T'; b[k++] = 'I'; b[k++] = 'M'; b[k++] = 'E'; b[k++] = ' '; { int t = (mode == 3 ? fin : tframe); k += put_num(b + k, t / 60, 1); b[k++] = '.'; b[k++] = (char)('0' + (t % 60) * 10 / 60); } b[k] = 0; text_at(SCRW - 86, 14, b, 8, 200);
    { int pr = clampi(pz / 256 * 70 / track_len, 0, 70); seg(SCRW - 86, 30, SCRW - 16, 30, 1, 60, 1); seg(SCRW - 86 + pr, 26, SCRW - 86 + pr, 34, 0, 255, 2);
      for (int i = 0; i < 3; i++) { int q = clampi(CAR[i].z / 256 * 70 / track_len, 0, 70); seg(SCRW - 86 + q, 27, SCRW - 86 + q, 33, CAR_COL[i], 200, 1); } }
    if (boost > 0) text_at(SCRW / 2 - 20, 40, "BOOST", 0, 255);
    if (mode == 1) { b[0] = (char)('0' + (count_t + 69) / 70); b[1] = 0; text_at(SCRW / 2 - 4, 100, b, 5, 255); }
    if (mode == 2 && tframe < 40) text_at(SCRW / 2 - 12, 100, "GO!", 6, 255);
    if (mode == 3) { text_at((SCRW - (rank == 1 ? 7 : 10) * 8) / 2, 96, rank == 1 ? "YOU WIN" : "FINISHED", rank == 1 ? 6 : 3, 255); text_at((SCRW - 22 * 8) / 2, 126, "TAP A OR B TO RACE AGAIN", 0, 200); }
    if (mode == 0) { text_at((SCRW - 9 * 8) / 2, 92, "TAP RACER", 5, 255); text_at((SCRW - 25 * 8) / 2, 112, "TAP FAST. CORNERS PUNISH IT", 3, 230); text_at((SCRW - 14 * 8) / 2, 132, "TAP A TO START", 0, 200); }
    if (dl_n > dl_peak) dl_peak = dl_n;
}

int main(void) {
    build_track(); for (int i = 0; i < 48; i++) SKYR[i] = (unsigned char)rnd(256);
    start_race(); mode = 0; pz = 0;
    for (;;) {
        unsigned pad = arcade1_pad(), edge = pad & ~prev_pad; prev_pad = pad;
        now_ms = (arcade1_timer_count() * 50u) / 3u; frame_t++;
        if (mode == 1) { if (--count_t <= 0) { mode = 2; tframe = 0; snd(800, 10, 0, 7); } }
        else if (mode == 2) { tframe++; if (edge & BTN_A) do_tap(0); if (edge & BTN_B) do_tap(1); if ((edge & BTN_START) && mode == 2) release(); if (mode == 2) step_race(pad); }
        else if (mode == 3) { over_t++; pv = pv * 248 >> 8; pz += pv; if (edge & (BTN_A | BTN_B | BTN_START)) do_tap(0); }
        else { if (edge & (BTN_A | BTN_B | BTN_START)) do_tap(0); }
        if (mode == 1 && (edge & (BTN_A | BTN_B))) { /* taps before GO are ignored */ }
        build_dl(); arcade1_cam(0, 0); arcade1_clear(RGB(5, 5, 12)); vec_submit(); snd_frame(); arcade1_present();
    }
}
