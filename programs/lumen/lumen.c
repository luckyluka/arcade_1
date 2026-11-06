/* LUMEN - a dark-room adventure for ARCADE-1. Your tap speed is your light.
 *
 * Ported from the browser prototype (lumen.html). Same structure as there:
 *   game state --build_dl()--> VECTOR DISPLAY LIST --vec_submit()--> screen
 * The game never draws anything itself. vec_submit() below is the SOFTWARE FALLBACK
 * (lines as runs of rects, intensity quantised to 8 levels, glow as a dim wide pass).
 * A machine with a vector block replaces only vec_submit(); the game does not change.
 * Display list format: arc1_vector_displaylist_spec.md (guest cap here: 900 segments,
 * the machine's 64 KiB of RAM is the limit, not the format).
 *
 * Mechanic (rules identical to tapcad.h / Neon Tap):
 *   nothing held ..... dim light, no sword
 *   hold A or B ...... slow sword swings, brighter light
 *   tap one button ... a swing per tap; fast taps = flurry, wide bright light, slow enemies
 *   alternate A / B .. charge light (no swinging)
 *   same button again, or START ... release: flash + spin attack that stuns and reveals
 * D-pad moves and aims. Creepers are fast in the dark and slow in your light.
 * Turrets only shoot at what is bright. The key is far from the door; taking it sets off an alarm.
 *
 * Freestanding RV32IM C, integer only, deterministic (frame timer, seeded LCG).
 * Observable globals: state room hp score kills haskey tap_speed charge lr n_enemies dl_n hero_x hero_y
 * States: 0 title, 1 playing, 2 faded (dead), 3 light restored (won).
 */
#include "arcade1.h"
#include "text.h"

#define SCRW 480
#define SCRH 270
#define TS 16
#define MW 30
#define MH 17
#define RGB(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

int state, room, hp, score, kills, haskey, tap_speed, charge, lr, n_enemies, dl_n, hero_x, hero_y;

static const short SINT[256] = {
#include "lumen_sin.inc"
};
#define SIN(i) ((int)SINT[(i) & 255])
#define COS(i) ((int)SINT[((i) + 64) & 255])
static unsigned isqrt(unsigned n) { unsigned r = 0, b = 1u << 30; while (b > n) b >>= 2; while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static unsigned rs = 7;
static int rnd(int n) { rs = rs * 1664525u + 1013904223u; return (int)(((rs >> 16) * (unsigned)n) >> 16); }
static int dist(int ax, int ay, int bx, int by) { int dx = ax - bx, dy = ay - by; return (int)isqrt((unsigned)(dx * dx + dy * dy)); }

/* ---- tap cadence (tapcad.h rules; times in ms) ---- */
static unsigned now_ms, tc_last, tc_chg_t, tc_tick;
static int tc_ema, tc_spd, tc_btn, tc_has, tc_has_chg;
static void tc_reset(void) { tc_last = tc_chg_t = tc_tick = 0; tc_ema = tc_spd = tc_btn = tc_has = tc_has_chg = 0; charge = 0; }
/* returns -1 debounced, 1 alternation (charging), 0 plain tap */
static int tc_press(int btn) {
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
typedef struct { short x, y; unsigned char col, inten; char s[28]; } txt_t;
static seg_t DL[DLCAP];
static txt_t TX[14];
static int tx_n;
static void seg(int x0, int y0, int x1, int y1, int col, int inten, int w) {
    if (inten <= 2 || dl_n >= DLCAP) return;
    seg_t *s = &DL[dl_n++]; s->x0 = (short)(x0 * 16); s->y0 = (short)(y0 * 16); s->x1 = (short)(x1 * 16); s->y1 = (short)(y1 * 16);
    s->col = (unsigned char)col; s->inten = (unsigned char)(inten > 255 ? 255 : inten); s->w = (unsigned char)w; s->fl = 0;
}
static void poly(int cx, int cy, int r, int n, int a, int col, int inten, int w) {
    int px = cx + (COS(a) * r >> 8), py = cy + (SIN(a) * r >> 8), fx = px, fy = py;
    for (int i = 1; i <= n; i++) { int q = a + i * 256 / n, x = i == n ? fx : cx + (COS(q) * r >> 8), y = i == n ? fy : cy + (SIN(q) * r >> 8); seg(px, py, x, y, col, inten, w); px = x; py = y; }
}
static void text_at(int x, int y, const char *s, int col, int inten) {
    if (tx_n >= 14) return;
    txt_t *t = &TX[tx_n++];
    t->x = (short)x;
    t->y = (short)y;
    t->col = (unsigned char)col;
    t->inten = (unsigned char)(inten > 255 ? 255 : inten);
    int i = 0; for (; s[i] && i < 27; i++) t->s[i] = s[i]; t->s[i] = 0;
}
static int itoa_pad(char *o, int v, int w) { for (int i = w - 1; i >= 0; i--) { o[i] = (char)('0' + v % 10); v /= 10; } o[w] = 0; return w; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* palette: 0 hero, 1 wall, 2 creeper, 3 wisp, 4 turret/charge, 5 key, 6 door/ok, 7 white, 8 hud, 9 bolt, 10 spare */
static const unsigned char PAL[11][3] = { {159,255,224}, {70,110,255}, {255,63,164}, {255,224,138}, {255,122,61}, {255,210,60}, {80,255,150}, {255,255,255}, {200,240,255}, {255,80,80}, {190,170,255} };

/* ---- software fallback renderer for the display list ---- */
static int shade565(int col, int inten, int mul256) {
    int lv = (inten + 16) >> 5; if (lv > 8) lv = 8;                       /* 8 brightness levels */
    int r = PAL[col][0] * lv / 8 * mul256 >> 8, g = PAL[col][1] * lv / 8 * mul256 >> 8, b = PAL[col][2] * lv / 8 * mul256 >> 8;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
static void line(int x0, int y0, int x1, int y1, int t, unsigned col) {
    int dx = iabs(x1 - x0), dy = iabs(y1 - y0), h = t >> 1;
    if (dx >= dy) { int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx / 2, y = y0, rs0 = x0, x = x0;
        for (int i = 0; i <= dx; i++, x += sx) { int last = (i == dx), step = 0; err -= dy; if (err < 0) { err += dx; step = 1; }
            if (step || last) { int a = rs0 < x ? rs0 : x; arcade1_rect(a, y - h, iabs(x - rs0) + 1, t, col); rs0 = x + sx; y += sy; } } }
    else { int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dy / 2, x = x0, rs0 = y0, y = y0;
        for (int i = 0; i <= dy; i++, y += sy) { int last = (i == dy), step = 0; err -= dx; if (err < 0) { err += dy; step = 1; }
            if (step || last) { int a = rs0 < y ? rs0 : y; arcade1_rect(x - h, a, t, iabs(y - rs0) + 1, col); rs0 = y + sy; x += sx; } } }
}
static void vec_submit(void) {
    for (int i = 0; i < dl_n; i++) {                                       /* pass 1: glows, so cores stay on top */
        const seg_t *s = &DL[i]; if (s->w < 2) continue;
        line(s->x0 >> 4, s->y0 >> 4, s->x1 >> 4, s->y1 >> 4, s->w == 2 ? 3 : 5, (unsigned)shade565(s->col, s->inten, 70));
    }
    for (int i = 0; i < dl_n; i++) {                                       /* pass 2: cores */
        const seg_t *s = &DL[i];
        line(s->x0 >> 4, s->y0 >> 4, s->x1 >> 4, s->y1 >> 4, s->w >= 3 ? 2 : 1, (unsigned)shade565(s->col, s->inten, 256));
    }
    arcade1_cam(0, 0);
    for (int i = 0; i < tx_n; i++) text_draw(TX[i].x, TX[i].y, TX[i].s, (unsigned)shade565(TX[i].col, TX[i].inten, 256));
}

/* ======================= game ======================= */
typedef struct { int x, y, vx, vy, sx[7], sy[7]; short hp, ph, stun, hit, cd, ar; unsigned char k, on, sw, dir; } enemy;
typedef struct { int x, y, vx, vy; short l; unsigned char on; } bolt;
typedef struct { int x, y, vx, vy; short l; unsigned char c, on; } part;
typedef struct { int x, y, r, R; short l; unsigned char on; } ring;
enum { E_CREEP, E_WISP, E_TURRET };
#define NE 24
#define NBL 12
#define NPA 80
#define NRG 3
static enemy E[NE]; static bolt BO[NBL]; static part PA[NPA]; static ring RG[NRG];
static unsigned char MAP[MW * MH];
typedef struct { short x0, y0, x1, y1; } wall_t;
static wall_t WL[330]; static int nwall;

typedef struct { int x, y, vx, vy, aim, inv, flash, swing, sw_cd, hit; int lrq; } hero_t;
static hero_t H;
static int door_open, key_x, key_y, over_t, frame_t, msg_t, hp_carry; static const char *msg_s;
static unsigned prev_pad;

static const unsigned char P1[][4] = { {6,4,3,3}, {20,4,3,3}, {12,10,6,2} };
static const unsigned char P2[][4] = { {5,3,2,6}, {10,8,2,6}, {15,3,2,6}, {20,8,2,6}, {25,3,2,5} };
static const unsigned char CR1[][2] = { {10,3}, {15,13} }, CR2[][2] = { {9,2}, {22,14} }, WI2[][2] = { {8,13}, {18,2}, {24,9} }, TU2[][2] = { {14,15}, {22,1} };

static int solid(int xq, int yq) { int tx = xq >> 12, ty = yq >> 12; if (tx < 0 || ty < 0 || tx >= MW || ty >= MH) return 1; int m = MAP[ty * MW + tx]; return m == 1 || (m == 2 && !door_open); }
static int mv(int *px, int *py, int dx, int dy, int r) {
    r <<= 8; int ok = 0;
#define T4(x, y) (solid((x) - r, (y) - r) || solid((x) + r, (y) - r) || solid((x) - r, (y) + r) || solid((x) + r, (y) + r))
    if (!T4(*px + dx, *py)) { *px += dx; ok |= 1; }
    if (!T4(*px, *py + dy)) { *py += dy; ok |= 2; }
#undef T4
    return ok;
}
static void burst(int x, int y, int col, int n) {
    for (int i = 0; i < n; i++) for (int k = 0; k < NPA; k++) if (!PA[k].on) { int a = rnd(256), s = 64 + rnd(300); PA[k].x = x << 8; PA[k].y = y << 8; PA[k].vx = COS(a) * s >> 8; PA[k].vy = SIN(a) * s >> 8; PA[k].l = (short)(14 + rnd(18)); PA[k].c = (unsigned char)col; PA[k].on = 1; break; }
}

/* sound: one voice */
#define AUDIO_FREQ (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x300u))
#define AUDIO_CTRL (*(volatile unsigned int *)(ARCADE1_MMIO_BASE + 0x304u))
static int snd_f, snd_n, snd_sw, snd_pri;
static void snd(int f, int n, int sw, int pri) { if (snd_n > 0 && pri < snd_pri) return; snd_f = f; snd_n = n; snd_sw = sw; snd_pri = pri; }
static void snd_frame(void) {
    if (snd_n > 0) { AUDIO_FREQ = (unsigned)snd_f; __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1; snd_f += snd_sw; if (snd_f < 40) snd_f = 40; snd_n--; }
    else if (state == 1 && tc_charging()) { AUDIO_FREQ = (unsigned)(180 + (charge >> 5)); __asm__ volatile("" ::: "memory"); AUDIO_CTRL = 1; }
    else AUDIO_CTRL = 0;
}

static enemy *add_enemy(int k, int tx_, int ty_) {
    for (int i = 0; i < NE; i++) if (!E[i].on) { enemy *e = &E[i]; unsigned char *z = (unsigned char *)e; for (unsigned q = 0; q < sizeof *e; q++) z[q] = 0;
        e->on = 1; e->k = (unsigned char)k; e->x = tx_ << 8; e->y = ty_ << 8; e->ph = (short)rnd(256);
        e->hp = k == E_CREEP ? 3 : k == E_WISP ? 1 : 4; if (k == E_TURRET) e->cd = 60;
        for (int q = 0; q < 7; q++) { e->sx[q] = e->x; e->sy[q] = e->y; } return e; }
    return 0;
}
static void load_room(int i) {
    room = i; for (int q = 0; q < MW * MH; q++) MAP[q] = 0;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) if (x == 0 || y == 0 || x == MW - 1 || y == MH - 1) MAP[y * MW + x] = 1;
    int np = i == 0 ? 3 : 5;
    for (int p = 0; p < np; p++) { const unsigned char *r = i == 0 ? P1[p] : P2[p]; for (int y = r[1]; y < r[1] + r[3]; y++) for (int x = r[0]; x < r[0] + r[2]; x++) MAP[y * MW + x] = 1; }
    MAP[8 * MW + MW - 1] = 2; MAP[9 * MW + MW - 1] = 2;
    nwall = 0;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) if (MAP[y * MW + x] == 1) {
#define NB(a, b) ((a) < 0 || (b) < 0 || (a) >= MW || (b) >= MH || MAP[(b) * MW + (a)] == 1)
        int X = x * TS, Y = y * TS;
        if (!NB(x, y - 1) && nwall < 330) { WL[nwall++] = (wall_t){ X, Y, X + TS, Y }; }
        if (!NB(x, y + 1) && nwall < 330) { WL[nwall++] = (wall_t){ X, Y + TS, X + TS, Y + TS }; }
        if (!NB(x - 1, y) && nwall < 330) { WL[nwall++] = (wall_t){ X, Y, X, Y + TS }; }
        if (!NB(x + 1, y) && nwall < 330) { WL[nwall++] = (wall_t){ X + TS, Y, X + TS, Y + TS }; }
#undef NB
    }
    for (int q = 0; q < NE; q++) E[q].on = 0;
    for (int q = 0; q < NBL; q++) BO[q].on = 0;
    for (int q = 0; q < NPA; q++) PA[q].on = 0;
    for (int q = 0; q < NRG; q++) RG[q].on = 0;
    H.x = (26 * TS + 8) << 8; H.y = (8 * TS + 8) << 8; H.vx = H.vy = 0; H.aim = 0; H.inv = 60; H.flash = H.swing = H.sw_cd = H.hit = 0; H.lrq = 34 << 8; haskey = 0; door_open = 0;
    if (i == 0) { key_x = 2 * TS + 8; key_y = 3 * TS + 8; for (int q = 0; q < 2; q++) add_enemy(E_CREEP, CR1[q][0] * TS + 8, CR1[q][1] * TS + 8); }
    else { key_x = 2 * TS + 8; key_y = 14 * TS + 8; for (int q = 0; q < 2; q++) add_enemy(E_CREEP, CR2[q][0] * TS + 8, CR2[q][1] * TS + 8);
        for (int q = 0; q < 3; q++) add_enemy(E_WISP, WI2[q][0] * TS + 8, WI2[q][1] * TS + 8);
        for (int q = 0; q < 2; q++) add_enemy(E_TURRET, TU2[q][0] * TS + 8, TU2[q][1] * TS + 8);
        }
}
static void reset_game(void) { rs = 7; state = 1; score = 0; kills = 0; hp = hp_carry = 3; tc_reset(); frame_t = 0; msg_t = 0; snd_n = 0; load_room(0); }

static int swing_hit(int reach, int dot100, int dmg) {
    int n = 0, hx = H.x >> 8, hy = H.y >> 8, ux = COS(H.aim), uy = SIN(H.aim);
    for (int i = 0; i < NE; i++) { enemy *e = &E[i]; if (!e->on) continue; int ex = e->x >> 8, ey = e->y >> 8, dx = ex - hx, dy = ey - hy, d = dist(ex, ey, hx, hy); if (!d) d = 1; if (d > reach) continue;
        if (dot100 && ((dx * ux + dy * uy) >> 8) * 100 < d * dot100) continue;
        e->hp -= dmg; e->hit = 6; n++;
        if (e->hp <= 0) { e->on = 0; burst(ex, ey, e->k == E_CREEP ? 2 : e->k == E_WISP ? 3 : 4, 16); score += 100; kills++; snd(700, 3, -50, 2); }
        else { if (e->k == E_CREEP && e->stun < 18) e->stun = 18; snd(420, 2, -20, 1); } }
    return n;
}
static void hurt(void) {
    if (H.inv > 0) return;
    hp--;
    hp_carry = hp;
    H.inv = 90;
    H.hit = 8;
    burst(H.x >> 8, H.y >> 8, 7, 26);
    snd(260, 12, -12, 5);
    if (hp <= 0) { state = 2; over_t = 0; snd(200, 40, -4, 9); }
}
static void release(void) {
    if (charge < 2048) return;
    int c = charge / 100;
    charge = 0;
    H.flash = 40;
    H.swing = 0;
    snd(600, 20, -22, 6);
    for (int i = 0; i < NRG; i++) if (!RG[i].on) { RG[i].on = 1; RG[i].x = H.x >> 8; RG[i].y = H.y >> 8; RG[i].r = 2; RG[i].R = 40 + (110 * c >> 8); RG[i].l = 34; break; }
    burst(H.x >> 8, H.y >> 8, 0, 24);
}
static void do_tap(int b) {
    if (state != 1) { if (state == 0 || over_t > 45) reset_game(); return; }
    int t = tc_press(b); if (t < 0 || t == 1) return;
    if (charge >= 2048) { release(); return; }
    int s = tc_speed(); H.swing = 8; H.sw_cd = 16 - (10 * s >> 8); swing_hit(28, 64, 1);
}

/* lights: x, y, radius, power */
static int LX[26], LY[26], LRAD[26], LPW[26], nl;
static void light(int x, int y, int r, int p) { if (nl < 26) { LX[nl] = x; LY[nl] = y; LRAD[nl] = r; LPW[nl] = p; nl++; } }
static int light_at(int x, int y) {
    int v = 0; for (int i = 0; i < nl; i++) { int dx = x - LX[i], dy = y - LY[i], d2 = dx * dx + dy * dy, r2 = LRAD[i] * LRAD[i]; if (d2 < r2) { int q = LPW[i] * (r2 - d2) / r2; if (q > v) v = q; } }
    return v;
}

static void step_play(unsigned pad) {
    frame_t++; tc_frame();
    int mx = ((pad & BTN_RIGHT) ? 1 : 0) - ((pad & BTN_LEFT) ? 1 : 0), my = ((pad & BTN_DOWN) ? 1 : 0) - ((pad & BTN_UP) ? 1 : 0);
    int tx = mx * 420, ty = my * 420; if (mx && my) { tx = tx * 181 >> 8; ty = ty * 181 >> 8; }
    H.vx += (tx - H.vx) >> 2; H.vy += (ty - H.vy) >> 2; mv(&H.x, &H.y, H.vx, H.vy, 4);
    if (mx || my) { static const unsigned char A8[9] = { 224, 192, 160, 0, 0, 128, 32, 64, 96 }; H.aim = A8[(my + 1) * 3 + (mx + 1)]; }
    int s = tc_speed(); tap_speed = s; int chg = tc_charging(), hold = (pad & (BTN_A | BTN_B)) != 0;
    int target = (chg || charge > 1536) ? 34 + (charge * 282 >> 16) : (hold || s > 13) ? 46 + (92 * s >> 8) : 30;
    H.lrq += ((target << 8) - H.lrq) * 31 >> 8; lr = H.lrq >> 8;
    if (H.flash > 0) { H.flash--; if (lr < 150) lr = 150; }
    if (H.sw_cd > 0) H.sw_cd--;
    if (H.swing > 0) H.swing--;
    if (hold && !chg && H.sw_cd <= 0 && s < 13) { H.sw_cd = 16; H.swing = 8; swing_hit(28, 64, 1); }
    if (H.inv > 0) H.inv--;
    if (H.hit > 0) H.hit--;
    int hx = H.x >> 8, hy = H.y >> 8;
    hero_x = hx; hero_y = hy;
    nl = 0; light(hx, hy, lr, 255); if (!haskey) light(key_x, key_y, 24, 200); if (door_open) light(MW * TS - 8, 8 * TS + 8, 40, 220);
    for (int i = 0; i < NE; i++) if (E[i].on) { if (E[i].k == E_WISP) light(E[i].x >> 8, E[i].y >> 8, 30, 150); if (E[i].stun > 0) light(E[i].x >> 8, E[i].y >> 8, 22, 200); }
    for (int i = 0; i < NBL; i++) if (BO[i].on) light(BO[i].x >> 8, BO[i].y >> 8, 14, 230);
    int ne = 0;
    for (int i = 0; i < NE; i++) {
        enemy *e = &E[i]; if (!e->on) continue; ne++; if (e->hit > 0) e->hit--; if (e->stun > 0) e->stun--;
        int ex = e->x >> 8, ey = e->y >> 8, d = dist(ex, ey, hx, hy); if (!d) d = 1; int lit = d < lr, ux = (hx - ex) * 256 / d, uy = (hy - ey) * 256 / d;
        if (e->k == E_CREEP) {
            e->ph += 14;
            if (!e->stun) {
                int sp = lit ? (170 - (s * 140 >> 8) > 30 ? 170 - (s * 140 >> 8) : 30) : 330, wob = SIN(e->ph) >> 3;
                int vx = (ux * sp >> 8) + (-uy * wob >> 8), vy = (uy * sp >> 8) + (ux * wob >> 8);
                e->vx += (vx - e->vx) >> 3; e->vy += (vy - e->vy) >> 3;
                if (!mv(&e->x, &e->y, e->vx, e->vy, 4)) { e->vx = -e->vx; e->vy = -e->vy; }
            }
            int px = e->x, py = e->y;
            for (int q = 1; q < 7; q++) { int dx = e->sx[q] - px, dy = e->sy[q] - py, dd = (int)(isqrt((unsigned)((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4))) << 4); if (!dd) dd = 1;
                e->sx[q] = px + dx * (7 << 8) / dd; e->sy[q] = py + dy * (7 << 8) / dd; px = e->sx[q]; py = e->sy[q]; }
            if (d < 8 && !e->stun) hurt();
        } else if (e->k == E_WISP) {
            e->ph += 9; int vx, vy; if (d * 10 < lr * 11) { vx = -ux * 300 >> 8; vy = -uy * 300 >> 8; } else { vx = ux * 90 >> 8; vy = uy * 90 >> 8; }
            vx += SIN(e->ph) >> 3; vy += COS(e->ph) >> 3; if (!e->stun) mv(&e->x, &e->y, vx, vy, 4); if (d < 8) hurt();
        } else {
            if (e->cd > 0) e->cd--;
            int armed = d < 170 && lr > 62;
            if (armed) { if (e->ar < 40) e->ar++; if (e->ar >= 40 && e->cd <= 0 && !e->stun) { e->cd = 100; e->ar = 0;
                for (int k = 0; k < NBL; k++) if (!BO[k].on) { BO[k].on = 1; BO[k].x = e->x; BO[k].y = e->y; BO[k].vx = ux * 560 >> 8; BO[k].vy = uy * 560 >> 8; BO[k].l = 120; break; }
                snd(1100, 4, -80, 3);
                } }
            else e->ar = e->ar - 2 > 0 ? e->ar - 2 : 0;
        }
        if (state != 1) return;
    }
    n_enemies = ne;
    for (int i = 0; i < NBL; i++) if (BO[i].on) { bolt *b = &BO[i]; b->x += b->vx; b->y += b->vy; b->l--; if (solid(b->x, b->y)) b->l = 0; if (dist(b->x >> 8, b->y >> 8, hx, hy) < 6) { b->l = 0; hurt(); } if (b->l <= 0) b->on = 0; }
    for (int i = 0; i < NRG; i++) if (RG[i].on) { ring *r = &RG[i]; r->r += ((r->R - r->r) * 46 >> 8) + 2; r->l--;
        for (int q = 0; q < NE; q++) { enemy *e = &E[q]; if (!e->on || e->sw) continue; if (dist(e->x >> 8, e->y >> 8, r->x, r->y) < r->r + 6) { e->sw = 1; e->hp -= 2; e->stun = 100; e->hit = 8;
            if (e->hp <= 0) { e->on = 0; burst(e->x >> 8, e->y >> 8, 4, 16); score += 100; kills++; } } }
        if (r->l <= 0) { r->on = 0; for (int q = 0; q < NE; q++) E[q].sw = 0; } }
    for (int k = 0; k < NPA; k++) if (PA[k].on) { PA[k].x += PA[k].vx; PA[k].y += PA[k].vy; PA[k].vx -= PA[k].vx >> 5; PA[k].vy -= PA[k].vy >> 5; if (--PA[k].l <= 0) PA[k].on = 0; }
    if (!haskey && dist(hx, hy, key_x, key_y) < 9) {
        haskey = 1; door_open = 1; msg_s = "ALARM  RUN"; msg_t = 100; snd(1000, 10, 30, 4);
        for (int q = 0; q < 2 + room; q++) add_enemy(E_CREEP, 24 + rnd(30), 24 + rnd(220));
        burst(hx, hy, 5, 20);
    }
    if (door_open && hx > MW * TS - 10) { if (room + 1 < 2) { hp_carry = hp; load_room(room + 1); } else { state = 3; over_t = 0; snd(500, 30, 12, 8); } }
    if (msg_t > 0) msg_t--;
}

/* ======================= build the display list ======================= */
static void build_dl(unsigned pad) {
    (void)pad; dl_n = 0; tx_n = 0; int s = tap_speed, hx = H.x >> 8, hy = H.y >> 8;
    for (int i = 0; i < nwall; i++) { const wall_t *w = &WL[i]; int v = 34 + light_at((w->x0 + w->x1) >> 1, (w->y0 + w->y1) >> 1); if (v > 255) v = 255; seg(w->x0, w->y0, w->x1, w->y1, 1, v, v > 110 ? 2 : 1); }
    for (int y = 8; y <= 9; y++) { if (!door_open) seg(MW * TS - 1, y * TS, MW * TS - 1, y * TS + TS, 6, 70 + ((frame_t >> 1) & 31), 2); else seg(MW * TS - 1, y * TS, MW * TS - 1, y * TS + TS, 6, 200, 3); }
    if (!haskey) { int bob = SIN(frame_t * 5) >> 6; poly(key_x, key_y + bob, 4, 4, frame_t * 3, 5, 230, 2); seg(key_x, key_y + bob + 4, key_x, key_y + bob + 10, 5, 230, 2); }
    for (int i = 0; i < NE; i++) { const enemy *e = &E[i]; if (!e->on) continue; int ex = e->x >> 8, ey = e->y >> 8;
        int L = light_at(ex, ey) + (e->stun > 0 ? 150 : 0) + (e->hit > 0 ? 120 : 0); if (L > 255) L = 255;
        if (e->k == E_CREEP) {
            int ppx = ex, ppy = ey;
            for (int q = 1; q < 7; q++) { int nx = e->sx[q] >> 8, ny = e->sy[q] >> 8, v = (L + (q == 1 ? 55 : 14)); if (v > 255) v = 255; seg(ppx, ppy, nx, ny, 2, v * (10 - (q - 1)) / 10, q < 3 ? 3 : 2); ppx = nx; ppy = ny; }
            poly(ex, ey, 4, 4, e->ph >> 1, 2, L + 70, 2);
        } else if (e->k == E_WISP) { int r = 5 + (SIN(e->ph * 3) >> 7); poly(ex, ey, r, 10, e->ph, 3, 150 + L, 2); poly(ex, ey, r + 3, 10, -e->ph, 3, 70, 1); }
        else { int a40 = e->ar * 255 / 40, v = L + a40 * 200 / 255; poly(ex, ey, 7, 3, 64, 4, v, 2); poly(ex, ey, 2 + (e->ar * 3 / 40), 6, 0, 4, 40 + a40, 2); }
    }
    for (int i = 0; i < NBL; i++) if (BO[i].on) seg(BO[i].x >> 8, BO[i].y >> 8, (BO[i].x - BO[i].vx * 3) >> 8, (BO[i].y - BO[i].vy * 3) >> 8, 9, 255, 2);
    for (int k = 0; k < NPA; k++) if (PA[k].on) seg(PA[k].x >> 8, PA[k].y >> 8, (PA[k].x - PA[k].vx * 2) >> 8, (PA[k].y - PA[k].vy * 2) >> 8, PA[k].c, PA[k].l * 14, 1);
    for (int i = 0; i < NRG; i++) if (RG[i].on) { poly(RG[i].x, RG[i].y, RG[i].r, 16, 0, 0, RG[i].l * 10, 3); poly(RG[i].x, RG[i].y, RG[i].r * 7 / 8, 16, 0, 7, RG[i].l * 7, 1); }
    if (state != 2 && !(H.inv > 0 && ((frame_t >> 2) & 1))) {
        int a = H.aim, hot = s > 153 ? 7 : 0, I = H.hit > 0 ? 255 : 235;
        int fx = hx + (COS(a) * 8 >> 8), fy = hy + (SIN(a) * 8 >> 8), lx = hx + (COS(a + 106) * 6 >> 8), ly = hy + (SIN(a + 106) * 6 >> 8), rx = hx + (COS(a - 106) * 6 >> 8), ry = hy + (SIN(a - 106) * 6 >> 8);
        seg(fx, fy, lx, ly, hot, I, 2); seg(lx, ly, rx, ry, hot, I, 2); seg(rx, ry, fx, fy, hot, I, 2);
        if (H.swing > 0) for (int i = -3; i < 3; i++) { int a0 = a + i * 10, a1 = a + (i + 1) * 10; seg(hx + (COS(a0) * 22 >> 8), hy + (SIN(a0) * 22 >> 8), hx + (COS(a1) * 22 >> 8), hy + (SIN(a1) * 22 >> 8), 7, H.swing * 28, 3); }
        if (charge > 1536) poly(hx, hy, 12 + charge / 3072, 16, frame_t * 4, 0, 120 + charge / 256, 2);
    }
    for (int i = 0; i < hp; i++) poly(10 + i * 10, 10, 3, 4, 32, 6, 230, 2);
    if (haskey) poly(46, 10, 3, 4, 32, 5, 230, 2);
    seg(30, SCRH - 23, 90, SCRH - 23, 1, 60, 1); seg(30, SCRH - 23, 30 + 60 * s / 256, SCRH - 23, 5, 255, 2);
    seg(30, SCRH - 11, 90, SCRH - 11, 1, 60, 1); seg(30, SCRH - 11, 30 + 60 * charge / 25600, SCRH - 11, 4, 255, 2);
    text_at(4, SCRH - 28, "TAP", 8, 170); text_at(4, SCRH - 16, "PWR", 8, 170);
    char b[22]; b[0] = 'S'; b[1] = 'C'; b[2] = 'O'; b[3] = 'R'; b[4] = 'E'; b[5] = ' '; itoa_pad(b + 6, score, 5); text_at(SCRW - 96, 4, b, 8, 220);
    b[0] = 'R'; b[1] = 'O'; b[2] = 'O'; b[3] = 'M'; b[4] = ' '; b[5] = (char)('1' + room); b[6] = 0; text_at(SCRW - 96, 14, b, 8, 150);
    if (msg_t > 0 && state == 1) text_at((SCRW - slen(msg_s) * 8) / 2, 36, msg_s, 6, msg_t * 8);
    if (state == 0) { text_at((SCRW - 5 * 8) / 2, 96, "LUMEN", 7, 255); text_at((SCRW - 26 * 8) / 2, 118, "TAP FAST TO LIGHT THE DARK", 3, 230); text_at((SCRW - 16 * 8) / 2, 138, "TAP A TO START", 8, 200); }
    if (state == 2) { text_at((SCRW - 5 * 8) / 2, 108, "FADED", 9, 255); text_at((SCRW - 14 * 8) / 2, 128, "TAP A TO RETRY", 8, 220); }
    if (state == 3) { text_at((SCRW - 14 * 8) / 2, 108, "LIGHT RESTORED", 6, 255); itoa_pad(b + 6, score, 5); b[0] = 'S'; b[1] = 'C'; b[2] = 'O'; b[3] = 'R'; b[4] = 'E'; b[5] = ' '; text_at((SCRW - 11 * 8) / 2, 128, b, 8, 220); text_at((SCRW - 14 * 8) / 2, 148, "TAP A TO AGAIN", 8, 200); }
}

int main(void) {
    state = 0; tc_reset(); reset_game(); state = 0; nl = 0; light(H.x >> 8, H.y >> 8, 34, 255);
    for (;;) {
        unsigned pad = arcade1_pad(), edge = pad & ~prev_pad; prev_pad = pad;
        now_ms = (arcade1_timer_count() * 50u) / 3u;
        if (state == 1) {
            if (edge & BTN_A) do_tap(0);
            if (edge & BTN_B) do_tap(1);
            if ((edge & BTN_START) && state == 1) release();
            if (state == 1) step_play(pad);
        } else {
            frame_t++; tap_speed = 0; if (state >= 2) over_t++;
            if (edge & (BTN_A | BTN_B | BTN_START)) { if (state == 0 || over_t > 45) reset_game(); }
        }
        build_dl(pad);
        arcade1_cam(0, 0); arcade1_clear(RGB(5, 5, 12));
        vec_submit(); snd_frame(); arcade1_present();
    }
}
