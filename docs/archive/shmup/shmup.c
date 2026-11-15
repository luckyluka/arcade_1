#include "shmup.h"
#include "tapcad.h"
#include <string.h>
#include "tables.inc"

#define W SH_W
#define H SH_H
typedef int32_t i32;
#define Q8(v) ((i32)((v) * 256))
static uint32_t rng;
static uint16_t rnd(void) { rng = rng * 1664525u + 1013904223u; return (uint16_t)(rng >> 16); }
static i32 cl(i32 v, i32 a, i32 b) { return v < a ? a : v > b ? b : v; }
static i32 ab(i32 v) { return v < 0 ? -v : v; }
#define SIN(i) SINT[(i) & 255]
#define COS(i) SINT[((i) + 64) & 255]
static uint32_t isq(uint32_t n) { uint32_t r = 0, b = 1u << 30; while (b > n) b >>= 2; while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }

/* ---------- colours ---------- */
static uint32_t *fb;
static uint32_t mixc(uint32_t a, uint32_t b, i32 t) {
    uint32_t o = 0; for (int s = 16; s >= 0; s -= 8) { i32 x = (a >> s) & 255, y = (b >> s) & 255; o |= (uint32_t)(x + (((y - x) * t) >> 8)) << s; } return o;
}
typedef struct { i32 alt; uint32_t top, bot, cl, cs; i32 ca; } kf_t;
static const kf_t KF[6] = {
    {12000, 0x04051a, 0x0a1030, 0x6a78a8, 0x3a4468, 64}, {9500, 0x0e2f6e, 0x2f6cc0, 0xffffff, 0xa8bce0, 230},
    {6500, 0x3c4a5e, 0x7a8698, 0xc8ced8, 0x7a8498, 256}, {3500, 0x5c86b8, 0xa8c4dc, 0xf4f8ff, 0xb4c8e0, 256},
    {1500, 0x7cb8e8, 0xd4e6f2, 0xffffff, 0xc0d4e8, 205}, {0, 0x8cc4ee, 0xe0eef4, 0xffffff, 0xc8dcec, 64}};
typedef struct { uint32_t top, bot, cl, cs; i32 ca; } pal_t;
static pal_t pal(i32 alt) {
    int i = 0; while (i < 4 && alt < KF[i + 1].alt) i++;
    const kf_t *a = &KF[i], *b = &KF[i + 1]; i32 t = cl((a->alt - alt) * 256 / (a->alt - b->alt), 0, 256);
    pal_t p = { mixc(a->top, b->top, t), mixc(a->bot, b->bot, t), mixc(a->cl, b->cl, t), mixc(a->cs, b->cs, t), a->ca + (((b->ca - a->ca) * t) >> 8) };
    return p;
}
static const char *PH[5] = { "STRATOSPHERE", "HIGH CLOUD", "STORM FRONT", "LOW HAZE", "GROUND" };
static int phase_of(i32 a) { return a > 9500 ? 0 : a > 6500 ? 1 : a > 3500 ? 2 : a > 1500 ? 3 : 4; }

/* ---------- drawing ---------- */
static void px(i32 x, i32 y, uint32_t c) { if ((uint32_t)x < W && (uint32_t)y < H) fb[y * W + x] = c; }
static void blendpx(i32 x, i32 y, uint32_t c, i32 a) { if ((uint32_t)x < W && (uint32_t)y < H && a > 0) fb[y * W + x] = a >= 255 ? c : mixc(fb[y * W + x], c, a); }
static void rect(i32 x, i32 y, i32 w, i32 h, uint32_t c) {
    for (i32 j = y < 0 ? 0 : y; j < y + h && j < H; j++) for (i32 i = x < 0 ? 0 : x; i < x + w && i < W; i++) fb[j * W + i] = c;
}
static void rectA(i32 x, i32 y, i32 w, i32 h, uint32_t c, i32 a) { for (i32 j = y; j < y + h; j++) for (i32 i = x; i < x + w; i++) blendpx(i, j, c, a); }
static void box(i32 x, i32 y, i32 w, i32 h, uint32_t c) { rect(x - 1, y - 1, w + 2, h + 2, 0x1a1424); rect(x, y, w, h, c); }
static void text(i32 x, i32 y, const char *s, i32 sc, uint32_t col, int shadow) {
    for (; *s; s++, x += 4 * sc) {
        const char *p = strchr(FONT_CHARS, *s); uint16_t g = p ? FONT[p - FONT_CHARS] : 0;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) if (g >> (14 - (r * 3 + c)) & 1) {
            if (shadow) rect(x + c * sc + 1, y + r * sc + 1, sc, sc, 0);
        }
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) if (g >> (14 - (r * 3 + c)) & 1) rect(x + c * sc, y + r * sc, sc, sc, col);
    }
}
static void textc(i32 y, const char *s, i32 sc, uint32_t col) { text((W - (i32)strlen(s) * 4 * sc + sc) / 2, y, s, sc, col, 1); }
static void num(char *o, int w, i32 v) { for (int i = w - 1; i >= 0; i--) { o[i] = (char)('0' + v % 10); v /= 10; } o[w] = 0; }

/* ship sprite 13x15, mirrored from left half + centre column */
static uint32_t SHIP[13 * 15];
static const char *SHIPROWS[15] = { "......o", ".....ow", ".....ow", "....obw", "....oby", "o...obb", "ow.oobb", "owwoobb", "owbbbdb", "owbbbdd", "oobbbdd", ".oobbbd", "...obbd", "...oodo", ".....oo" };
static void build_ship(void) {
    for (int y = 0; y < 15; y++) for (int x = 0; x < 13; x++) {
        int sx = x < 7 ? x : 12 - x; char ch = SHIPROWS[y][sx]; uint32_t c = 0xFF000000;
        switch (ch) { case 'o': c = 0x14182e; break; case 'w': c = 0xe8f0ff; break; case 'b': c = 0x4a8fe8; break; case 'd': c = 0x2a4fa0; break; case 'y': c = 0xffe08a; break; }
        SHIP[y * 13 + x] = c;
    }
}

/* ---------- world state ---------- */
typedef struct { i32 x, y, vx, vy; uint8_t on; } bul_t;
typedef struct { uint8_t on, k, dir, lin; i32 x, y, bx, vx, vy, ty, hp, ph, cd, cd2, born; } en_t;
typedef struct { i32 x, y, vx, vy, l; uint32_t c; uint8_t on; } part_t;
typedef struct { i32 x, y, w, h, s; } cloud_t;
#define NE 24
#define NB 64
#define NEB 128
#define NP 128
static const int LAYN[3] = { 14, 12, 7 }, LAYSPD[3] = { 128, 256, 410 }, LAYSC[3] = { 179, 256, 410 }, LAYA[3] = { 140, 256, 90 };
static struct {
    i32 alt, gy, px, py, lives, score, inv, fcd, t, wt, beam, beamw, phase, banner, flash, v; int state; en_t *boss; uint16_t held;
} G;
static en_t E[NE]; static bul_t B[NB], EB[NEB]; static part_t PT[NP]; static cloud_t CL[3][14]; static uint8_t STARS[70][3];
static tapcad_t TC; static uint32_t now_ms;
static const i32 DW[3] = { 11, 13, 56 }, DH[3] = { 9, 12, 30 };

void shmup_init(void) {
    build_ship(); rng = 7; memset(&G, 0, sizeof G); memset(E, 0, sizeof E); memset(B, 0, sizeof B); memset(EB, 0, sizeof EB); memset(PT, 0, sizeof PT);
    tc_init(&TC); G.alt = 12000; G.px = (W / 2) * 256; G.py = 260 * 256; G.lives = 3; G.wt = 90; G.banner = 90; G.v = 205; G.state = SH_TITLE;
    for (int l = 0; l < 3; l++) for (int i = 0; i < LAYN[l]; i++) {
        cloud_t *c = &CL[l][i]; c->x = rnd() % W; c->y = (i32)(rnd() % (H + 40)) - 20;
        c->w = (30 + rnd() % 40) * LAYSC[l] >> 8; c->h = (12 + rnd() % 14) * LAYSC[l] >> 8; c->s = rnd() & 255;
    }
    for (int i = 0; i < 70; i++) { STARS[i][0] = rnd() % W; STARS[i][1] = rnd() % H; STARS[i][2] = rnd() & 255; }
}
static void start(void) { uint8_t keep = 0; (void)keep; shmup_init(); G.state = SH_PLAY; }
static void boom(i32 x, i32 y, int n) {
    for (int i = 0; i < n; i++) for (int k = 0; k < NP; k++) if (!PT[k].on) {
        i32 a = rnd() & 255, s = 128 + rnd() % 512; PT[k] = (part_t){ x << 8, y << 8, COS(a) * s >> 8, SIN(a) * s >> 8, 14 + rnd() % 14, rnd() & 1 ? 0xffb030 : 0xfff0a0, 1 }; break;
    }
}
static void ebul(i32 x, i32 y, i32 vx, i32 vy) { for (int k = 0; k < NEB; k++) if (!EB[k].on) { EB[k] = (bul_t){ x << 8, y << 8, vx, vy, 1 }; return; } }
static void aim(i32 x, i32 y, i32 sp, i32 spread) {            /* sp Q8 px/frame, spread in table units (256 = 360 deg) */
    i32 dx = G.px - x, dy = G.py - y; i32 len = (i32)isq((uint32_t)(dx * dx + dy * dy)); if (!len) len = 1;
    i32 ux = dx * 256 / len, uy = dy * 256 / len, c = COS(spread), s = SIN(spread);
    i32 rx = (ux * c - uy * s) >> 8, ry = (ux * s + uy * c) >> 8; ebul(x, y, rx * sp >> 8, ry * sp >> 8);
}
static en_t *spawn(void) { for (int i = 0; i < NE; i++) if (!E[i].on) { memset(&E[i], 0, sizeof E[i]); E[i].on = 1; return &E[i]; } return 0; }
static void wave(i32 pq) {
    uint16_t r = rnd();
    if (r < 26214) { i32 bx = 40 + rnd() % (W - 80); for (int i = 0; i < 5; i++) { en_t *e = spawn(); if (!e) break; e->k = 0; e->bx = (bx + (i - 2) * 16) << 8; e->x = e->bx; e->y = (-10 - ab(i - 2) * 10) * 256; e->ph = i * 4096; e->hp = 10; e->vy = 282 + (205 * pq >> 8); } }
    else if (r < 45875) { int n = 2 + (pq > 128); for (int i = 0; i < n; i++) { en_t *e = spawn(); if (!e) break; e->k = 1; e->x = (50 + rnd() % (W - 100)) << 8; e->y = (-14 - i * 30) * 256; e->ty = (50 + rnd() % 50) << 8; e->dir = (rnd() & 1) ? 1 : 2; e->hp = 40; e->cd = 40 + i * 20; e->born = G.t; } }
    else { i32 sd = (rnd() & 1) ? -1 : 1; for (int i = 0; i < 4; i++) { en_t *e = spawn(); if (!e) break; e->k = 0; e->lin = 1; e->x = (sd < 0 ? -10 - i * 14 : W + 10 + i * 14) * 256; e->y = (30 + i * 10) << 8; e->vx = -sd * 384; e->vy = 230; e->hp = 10; } }
}
static void hurt(en_t *e, i32 d) {
    e->hp -= d; if (e->hp <= 0 && e->on) { e->on = 0; boom(e->x >> 8, e->y >> 8, e->k == 2 ? 60 : 14); G.score += e->k == 2 ? 5000 : e->k == 1 ? 400 : 100; if (e->k == 2) G.state = SH_WIN; }
}
static void die(void) { G.lives--; boom(G.px >> 8, G.py >> 8, 40); G.inv = 120; memset(EB, 0, sizeof EB); if (G.lives < 0) G.state = SH_DEAD; }

void shmup_update(uint32_t frame, uint16_t held, uint16_t pressed) {
    now_ms = (frame * 50u) / 3u; G.held = held; G.t++;
    if (G.state != SH_PLAY) { if (pressed & (SH_A | SH_B | SH_C)) start(); return; }
    if (pressed & SH_A) tc_press(&TC, now_ms, 0);
    if (pressed & SH_B) tc_press(&TC, now_ms, 1);
    tc_tick(&TC, now_ms);
    if ((pressed & SH_C) && TC.charge >= 2048) { G.beam = 22; G.beamw = 6 + 18 * (TC.charge / 100) / 256; tc_spend(&TC); }
    G.px = cl(G.px + (((held & SH_RIGHT) ? 1 : 0) - ((held & SH_LEFT) ? 1 : 0)) * 589, 8 * 256, (W - 8) * 256);
    G.py = cl(G.py + (((held & SH_DOWN) ? 1 : 0) - ((held & SH_UP) ? 1 : 0)) * 589, 20 * 256, (H - 16) * 256);
    i32 pq = (12000 - G.alt) * 256 / 12000, v = 205 + (461 * pq >> 8); G.v = v;
    if (G.alt > 0) G.alt = G.alt > 2 ? G.alt - 2 : 0;
    G.gy += v;
    for (int l = 0; l < 3; l++) for (int i = 0; i < LAYN[l]; i++) { cloud_t *c = &CL[l][i]; c->y += v * LAYSPD[l] >> 8; if (c->y >> 8 > H + c->h) { c->y = -c->h * 256; c->x = rnd() % W; } }
    int ph = phase_of(G.alt); if (ph != G.phase) { G.phase = ph; G.banner = 100; } if (G.banner > 0) G.banner--;
    if (ph == 2 && rnd() < 262) G.flash = 6;
    if (G.flash > 0) G.flash--;
    if (G.alt > 0) { if (--G.wt <= 0) { wave(pq); G.wt = 200 - 90 * pq / 256; } }
    else if (!G.boss) { en_t *e = spawn(); if (e) { e->k = 2; e->x = (W / 2) << 8; e->y = -40 * 256; e->hp = 1600; e->cd = 60; e->cd2 = 30; G.boss = e; } }
    i32 s = tc_speed(&TC, now_ms); int chg = tc_charging(&TC, now_ms);
    if (--G.fcd <= 0 && !chg) {
        int hold = (held & (SH_A | SH_B)) != 0, base = (hold || s > 13) ? 10 : 24;
        G.fcd = (base * 256 - (base - 4) * s + 128) >> 8;
        int n = 1 + (s > 90) + (s > 180); static const int AS[3][3] = { { 0, 0, 0 }, { -2, 2, 0 }, { -7, 0, 7 } };
        for (int j = 0; j < n; j++) { int a = AS[n - 1][j]; for (int k = 0; k < NB; k++) if (!B[k].on) { B[k] = (bul_t){ G.px, G.py - 8 * 256, SIN(a) * 7, -COS(a) * 7, 1 }; break; } }
    }
    for (int k = 0; k < NB; k++) if (B[k].on) { B[k].x += B[k].vx; B[k].y += B[k].vy; if (B[k].y < -(10 << 8) || B[k].x < 0 || B[k].x > (W << 8)) B[k].on = 0; }
    for (int i = 0; i < NE; i++) {
        en_t *e = &E[i]; if (!e->on) continue;
        if (e->k == 0) { e->ph += 625; e->y += e->vy; if (e->lin) e->x += e->vx; else e->x = e->bx + (SIN(e->ph >> 8) * 18); }
        else if (e->k == 1) {
            if (G.t - e->born > 700) e->y += 384; else if (e->y < e->ty) e->y += 307; else { e->x += (e->dir == 1 ? 1 : -1) * 128; if ((e->x >> 8) < 24 || (e->x >> 8) > W - 24) e->dir = e->dir == 1 ? 2 : 1; }
            if (--e->cd <= 0 && (e->y >> 8) > 10 && (e->y >> 8) < H - 60) { e->cd = 75; aim(e->x >> 8, (e->y >> 8) + 6, 563, 0); }
        } else {
            if ((e->y >> 8) < 50) e->y += 154; else {
                e->x = ((W / 2) << 8) + SIN((G.t * 13 / 25) & 255) * 70;
                if (--e->cd <= 0) { e->cd = 100; for (int j = 0; j < 14; j++) { i32 a = (j * 256 / 14 + G.t * 2 / 5) & 255; ebul(e->x >> 8, e->y >> 8, COS(a) * 410 >> 8, SIN(a) * 410 >> 8); } }
                if (--e->cd2 <= 0) { e->cd2 = 45; i32 d[3] = { -10, 0, 10 }; for (int j = 0; j < 3; j++) aim(e->x >> 8, (e->y >> 8) + 10, 614, d[j]); }
            }
        }
        i32 w = DW[e->k], h = DH[e->k], ex = e->x >> 8, ey = e->y >> 8;
        for (int k = 0; k < NB; k++) if (B[k].on && ab((B[k].x >> 8) - ex) < w / 2 + 1 && ab((B[k].y >> 8) - ey) < h / 2 + 2) { B[k].on = 0; hurt(e, 10); if (!e->on) break; }
        if (!e->on) continue;
        if (G.beam > 0 && ab(ex - (G.px >> 8)) < w / 2 + G.beamw / 2 && ey < (G.py >> 8)) hurt(e, e->k == 2 ? 12 : 30);
        if (!e->on) continue;
        if (G.inv <= 0 && ab(ex - (G.px >> 8)) < w / 2 + 2 && ab(ey - (G.py >> 8)) < h / 2 + 2) { die(); if (e->k != 2) hurt(e, 90); }
        if (e->on && (ey > H + 20 || ex < -30 || ex > W + 30)) e->on = 0;
    }
    if (G.beam > 0) G.beam--;
    for (int k = 0; k < NEB; k++) if (EB[k].on) {
        bul_t *b = &EB[k]; b->x += b->vx; b->y += b->vy; i32 bx = b->x >> 8, by = b->y >> 8;
        if (G.beam > 0 && ab(bx - (G.px >> 8)) < G.beamw / 2 && by < (G.py >> 8)) { b->on = 0; continue; }
        if (G.inv <= 0 && ab(bx - (G.px >> 8)) < 3 && ab(by - (G.py >> 8)) < 3) { b->on = 0; die(); continue; }
        if (bx < -5 || bx > W + 5 || by < -5 || by > H + 5) b->on = 0;
    }
    if (G.inv > 0) G.inv--;
    for (int k = 0; k < NP; k++) if (PT[k].on) { PT[k].x += PT[k].vx; PT[k].y += PT[k].vy; if (--PT[k].l <= 0) PT[k].on = 0; }
}

/* ---------- terrain ---------- */
static uint32_t hs(i32 a, i32 b) { uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return (h ^ (h >> 16)) & 255; }
static i32 vn(i32 x, i32 y) {                                   /* x,y Q8 -> 0..255 */
    i32 x0 = x >> 8, y0 = y >> 8, fx = x & 255, fy = y & 255, sx = (fx * fx * (768 - 2 * fx)) >> 16, sy = (fy * fy * (768 - 2 * fy)) >> 16;
    i32 a = (i32)hs(x0, y0), b = (i32)hs(x0 + 1, y0), c = (i32)hs(x0, y0 + 1), d = (i32)hs(x0 + 1, y0 + 1);
    i32 t = a + (((b - a) * sx) >> 8), u = c + (((d - c) * sx) >> 8); return t + (((u - t) * sy) >> 8);
}
static const uint32_t TCOL[7] = { 0x1a4a8a, 0x2a6ab0, 0xd8c890, 0x3f8a3a, 0x2a6a30, 0x8a8a80, 0xf0f0f0 };
static int tile(i32 i, i32 k) {
    i32 h = (153 * vn(i * 256 / 7, k * 256 / 7) + 77 * vn(i * 256 * 10 / 32 + 9 * 256, k * 256 * 10 / 32 + 4 * 256) + 26 * (i32)hs(i, k)) >> 8;
    return h < 92 ? 0 : h < 115 ? 1 : h < 128 ? 2 : h < 174 ? 3 : h < 205 ? 4 : h < 230 ? 5 : 6;
}
static void ground(i32 a) {
    i32 gy = G.gy >> 8;
    for (i32 k = (gy - H) >> 3; k <= (gy >> 3) + 1; k++) { i32 y = gy - k * 8 - 8; for (i32 i = 0; i < 30; i++) { uint32_t c = TCOL[tile(i, k)]; if (a >= 255) rect(i * 8, y, 8, 8, c); else for (int j = 0; j < 8; j++) for (int q = 0; q < 8; q++) blendpx(i * 8 + q, y + j, c, a); } }
}
static void ell(i32 cx, i32 cy, i32 w, i32 h, i32 s, const pal_t *p, i32 a) {
    for (i32 r = 0; r < h; r++) {
        i32 u = (2 * r + 1) * 256 / h - 256, sq = (i32)isq((uint32_t)(65536 - u * u)), hw = (w / 2) * sq >> 8;
        hw = hw * (256 + (46 * SIN(s * 40 + r * 35) >> 8)) >> 8; uint32_t c = r * 100 > h * 62 ? p->cs : p->cl;
        for (i32 x = cx - hw; x < cx + hw; x++) blendpx(x, cy + r, c, a);
    }
}
static void cloud(const cloud_t *c, const pal_t *p, i32 a) { i32 x = c->x, y = c->y >> 8; ell(x, y, c->w, c->h, c->s, p, a); ell(x + c->w * 56 / 256, y - c->h / 4, c->w / 2, c->h * 6 / 10, c->s + 2, p, a); }

void shmup_render(uint32_t *out) {
    fb = out; pal_t p = pal(G.alt);
    for (int y = 0; y < H; y += 4) { uint32_t c = mixc(p.top, p.bot, y * 256 / H); rect(0, y, W, 4, c); }
    i32 sa = cl((G.alt - 8500) * 256 / 2500, 0, 256);
    if (sa > 0) for (int i = 0; i < 70; i++) blendpx(STARS[i][0], (STARS[i][1] + (G.gy >> 8) * STARS[i][2] / 1280) % H, 0xffffff, sa * (100 + (STARS[i][2] * 155 >> 8)) >> 8);
    i32 ga = cl((2200 - G.alt) * 256 / 1200, 0, 256); if (ga > 0) ground(ga);
    for (int l = 0; l < 2; l++) for (int i = 0; i < LAYN[l]; i++) cloud(&CL[l][i], &p, p.ca * LAYA[l] >> 8);
    for (int i = 0; i < NE; i++) if (E[i].on) {
        en_t *e = &E[i]; i32 x = e->x >> 8, y = e->y >> 8;
        if (e->k == 0) { box(x - 5, y - 1, 11, 3, 0xc0392b); box(x - 2, y - 4, 5, 9, 0xe8604c); rect(x - 1, y - 2, 3, 2, 0xffe08a); }
        else if (e->k == 1) { box(x - 6, y - 2, 13, 4, 0x2e8b57); box(x - 3, y - 6, 7, 12, 0x46b878); rect(x - 5, y + 2, 2, 4, 0x222222); rect(x + 4, y + 2, 2, 4, 0x222222); rect(x - 1, y - 3, 3, 3, 0xffe08a); }
        else { box(x - 28, y - 6, 56, 10, 0x6a6a78); box(x - 12, y - 15, 24, 30, 0x8a8a9a); box(x - 22, y + 2, 6, 8, 0x555566); box(x + 16, y + 2, 6, 8, 0x555566); rect(x - 4, y - 8, 8, 6, 0xffe08a); }
    }
    for (int k = 0; k < NB; k++) if (B[k].on) { i32 x = B[k].x >> 8, y = B[k].y >> 8; rect(x - 1, y - 4, 2, 7, 0xffe08a); rect(x, y - 3, 1, 4, 0xffffff); }
    for (int k = 0; k < NEB; k++) if (EB[k].on) { i32 x = EB[k].x >> 8, y = EB[k].y >> 8; rect(x - 1, y - 2, 3, 5, 0xff5a8a); rect(x - 2, y - 1, 5, 3, 0xff5a8a); rect(x - 1, y - 1, 3, 3, 0xffffff); }
    for (int k = 0; k < NP; k++) if (PT[k].on) rect(PT[k].x >> 8, PT[k].y >> 8, 2, 2, PT[k].c);
    if (G.state == SH_PLAY || G.state == SH_TITLE) {
        i32 x = G.px >> 8, y = G.py >> 8, s = tc_speed(&TC, now_ms); int blink = G.inv > 0 && ((G.t >> 2) & 1);
        if (!blink) {
            i32 f = 2 + (6 * s >> 8) + (G.t & 1); rect(x - 1, y + 7, 3, f, 0xffb030); rect(x, y + 7, 1, f * 6 / 10, 0xffffff);
            for (int j = 0; j < 15; j++) for (int i = 0; i < 13; i++) { uint32_t c = SHIP[j * 13 + i]; if (!(c >> 24)) px(x - 6 + i, y - 8 + j, c & 0xffffff); }
            if (tc_charging(&TC, now_ms) || TC.charge > 2560) { i32 r = 8 + TC.charge / 2560, a = 64 + TC.charge / 100 / 2; rectA(x - r, y - r, r * 2, r * 2, 0xff7a3d, a > 160 ? 160 : a); }
        }
    }
    if (G.beam > 0) { i32 x = G.px >> 8, y = G.py >> 8, w = G.beamw; rectA(x - w / 2, 0, w, y - 8, 0x9fe8ff, 170 + (rnd() & 63)); rect(x - w / 4, 0, w / 2 ? w / 2 : 1, y - 8, 0xffffff); }
    for (int i = 0; i < LAYN[2]; i++) cloud(&CL[2][i], &p, p.ca * LAYA[2] >> 8);
    if (G.flash > 0) rectA(0, 0, W, H, 0xffffff, 90);
    /* HUD */
    char b[16]; num(b, 5, G.alt); text(4, 3, "ALT", 1, 0xffffff, 1); text(20, 3, b, 1, 0xffffff, 1); text(40, 3, "M", 1, 0xffffff, 1);
    num(b, 7, G.score); text(W - 32, 3, b, 1, 0xffffff, 1);
    for (int i = 0; i < G.lives; i++) rect(4 + i * 8, 14, 5, 5, 0x4a8fe8);
    { i32 s = tc_speed(&TC, now_ms); text(4, H - 24, "TAP", 1, 0xffffff, 1); rect(28, H - 23, 60, 6, 0x222222); rect(28, H - 23, 60 * s >> 8, 6, 0xffe08a);
      text(4, H - 13, "PWR", 1, 0xffffff, 1); rect(28, H - 12, 60, 6, 0x222222); rect(28, H - 12, 60 * TC.charge / TC_CHARGE_FULL, 6, TC.charge >= 25000 ? 0xffffff : 0xff7a3d); }
    if (G.boss && G.boss->on) { rect(W / 2 - 40, H - 12, 80, 5, 0x222222); rect(W / 2 - 40, H - 12, 80 * G.boss->hp / 1600, 5, 0xe8604c); }
    if (G.banner > 0 && G.state == SH_PLAY) textc(60, PH[G.phase], 2, 0xffffff);
    if (G.state == SH_TITLE) { textc(100, "SKY DESCENT", 2, 0xffffff); textc(120, "TAP A TO START", 1, 0xffe08a); textc(140, "DESCEND FROM 12000 M TO THE GROUND", 1, 0xccffee); }
    if (G.state == SH_DEAD) { textc(110, "CRASHED", 2, 0xffffff); num(b, 7, G.score); textc(130, b, 1, 0xffffff); textc(145, "TAP A TO RETRY", 1, 0xffe08a); }
    if (G.state == SH_WIN) { textc(110, "TOUCHDOWN", 2, 0xffffff); num(b, 7, G.score); textc(130, b, 1, 0xffffff); textc(145, "TAP A TO FLY AGAIN", 1, 0xffe08a); }
}
int shmup_state(void) { return G.state; } int shmup_alt(void) { return G.alt; } int shmup_score(void) { return G.score; } int shmup_lives(void) { return G.lives; }
void shmup_debug_set_alt(int a) { G.alt = a; }
void shmup_debug_set_lives(int n) { G.lives = n; }
