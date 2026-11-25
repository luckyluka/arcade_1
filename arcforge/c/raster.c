/* raster.c -- M4.2: drawables -> RGBA frames (arcforge AGENTS rule 4).
 *
 * Integer / fixed-point only: every decision here is integer arithmetic,
 * the trig comes from the generated gen/trig_table.h table and this file
 * never calls libm. The oracle is tools/arcforge_ref.py (part_offset /
 * rotf / rot_pt / shade_pick / render_frame / to_rgba / resolve_auto_cell
 * / render_all); tests/render_harness.c + tests/test_render_parity.py is
 * the differential driver.
 *
 * The input is the compiled IR (the af_json object compile_asset builds,
 * same keys as the oracle's): drawables, style, arch.layers, anchors,
 * animations, states, auto, min_cell, hitboxes, W, H.
 *
 * Grid iteration order is never load-bearing: every outline pass writes a
 * value determined solely by the pixel it examines, or one constant, so
 * the hash table's slot order cannot leak into a frame hash. Determinism
 * holds by construction (rule 11), not by sorting.
 *
 * Failure = the oracle's uncaught exception: af_render_all returns -1
 * with af_render_why() set, and the caller must exit 1 with no stdout.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "json.h"
#include "raster.h"
#include "gen/trig_table.h"

/* ---- failure reasons ---------------------------------------------------- */
static char g_why[512];

static int rerr(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_why, sizeof g_why, fmt, ap);
    va_end(ap);
    return -1;
}

const char *af_render_why(void) { return g_why; }

/* ---- Python bits -------------------------------------------------------- */

/* a // b with Python's floor semantics (every divisor here is > 0). */
static int64_t floordiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}

/* math.isqrt for the shading normal's z component (0 <= n <= 1024*1024). */
static int64_t isqrt64(int64_t n) {
    if (n <= 0) return 0;
    int64_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

/* the oracle's COS/SIN tables, index = deg + 180 */
static int rotf(int64_t x, int64_t y, int64_t deg, int64_t *ox, int64_t *oy) {
    if (deg < TRIG_DEG_MIN || deg > TRIG_DEG_MAX)
        return rerr("rotate degree %lld outside the trig table", (long long)deg);
    int64_t c = trig_cos[deg - TRIG_DEG_MIN], s = trig_sin[deg - TRIG_DEG_MIN];
    *ox = floordiv(x * c - y * s + 500000, 1000000);
    *oy = floordiv(x * s + y * c + 500000, 1000000);
    return 0;
}

/* Rotate a 1/64-px vector by whole degrees: the oracle's rot_fixed, which
 * the shape compiler uses. deg is range-checked by the caller. */
void af_rot_fixed(int64_t x, int64_t y, int64_t deg, int64_t *ox, int64_t *oy) {
    if (deg < TRIG_DEG_MIN || deg > TRIG_DEG_MAX) {  /* unreachable */
        *ox = 0;
        *oy = 0;
        return;
    }
    int64_t c = trig_cos[deg - TRIG_DEG_MIN], s = trig_sin[deg - TRIG_DEG_MIN];
    *ox = floordiv((x * c - y * s) * 64 + 500000, 1000000);
    *oy = floordiv((x * s + y * c) * 64 + 500000, 1000000);
}

/* rot_pt: rotate a fixed-point (1/64 px) point about whole-pixel pivot
 * r = (deg, px, py). */
static int rot_pt(int64_t X, int64_t Y, const int64_t r[3], int64_t *ox,
                  int64_t *oy) {
    int64_t dx = 0, dy = 0;
    if (rotf(X - r[1] * 64, Y - r[2] * 64, r[0], &dx, &dy)) return -1;
    *ox = r[1] * 64 + dx;
    *oy = r[2] * 64 + dy;
    return 0;
}

static const char *const TONES[5] = {"deep", "shadow", "base", "light", "high"};
static const int64_t DITH[4] = {-45, 15, 45, -15};   /* (y&1)*2+(x&1) */
static const int64_t LIGHT0[3] = {-606, -660, 496};

/* ---- node helpers ------------------------------------------------------- */

static const char *astr(const af_json *o, const char *k) {
    const af_json *v = o ? af_json_get(o, k) : NULL;
    return (v && v->type == AF_JSON_STRING) ? v->s : NULL;
}

/* an integer array element. Python's arithmetic folds bools (True == 1),
 * so AF_JSON_BOOL reads as 0/1 exactly as the oracle computes it. */
static int aint(const af_json *a, size_t i, int64_t *out) {
    if (!a || a->type != AF_JSON_ARRAY || i >= a->n || !a->items[i] ||
        (a->items[i]->type != AF_JSON_INT &&
         a->items[i]->type != AF_JSON_BOOL))
        return rerr("expected an integer array");
    *out = a->items[i]->type == AF_JSON_BOOL ? (a->items[i]->b ? 1 : 0)
                                             : a->items[i]->i;
    return 0;
}

/* a pair of integers (offsets / anchor `at` values) */
static int a2(const af_json *a, int64_t *x, int64_t *y) {
    if (!a || a->type != AF_JSON_ARRAY || a->n != 2)
        return rerr("expected a 2-element integer array");
    return aint(a, 0, x) || aint(a, 1, y) ? -1 : 0;
}

static int jint(const af_json *o, const char *k, int64_t *out) {
    const af_json *v = o ? af_json_get(o, k) : NULL;
    if (!v || v->type != AF_JSON_INT)
        return rerr("missing integer field '%s'", k);
    *out = v->i;
    return 0;
}

/* the non-const twin of af_json_get, for the auto-cell mutation */
static af_json *aget(af_json *o, const char *k) {
    if (!o || o->type != AF_JSON_OBJECT) return NULL;
    for (size_t i = 0; i + 1 < o->n; i += 2)
        if (o->items[i]->type == AF_JSON_STRING && !strcmp(o->items[i]->s, k))
            return o->items[i + 1];
    return NULL;
}

/* the oracle's fill tuple (ramp, tone) */
static int fill_of(const af_json *d, const char **ramp, const char **tone) {
    const af_json *f = af_json_get(d, "fill");
    if (!f || f->type != AF_JSON_ARRAY || f->n != 2 ||
        !f->items[0] || f->items[0]->type != AF_JSON_STRING ||
        !f->items[1] || f->items[1]->type != AF_JSON_STRING)
        return rerr("drawable has no fill");
    *ramp = f->items[0]->s;
    *tone = f->items[1]->s;
    return 0;
}

/* tl[ramp] = the ramp's tones in TONES order (render_frame's pre-pass) */
static int tones_of(const af_json *style, const char *ramp, const char **out,
                    int *n) {
    const af_json *ramps = style ? af_json_get(style, "ramps") : NULL;
    const af_json *rd = ramps ? af_json_get(ramps, ramp) : NULL;
    if (!rd || rd->type != AF_JSON_OBJECT)
        return rerr("ramp '%s' does not exist in the style pack", ramp);
    int k = 0;
    for (int i = 0; i < 5; i++)
        if (af_json_get(rd, TONES[i])) out[k++] = TONES[i];
    *n = k;
    return 0;
}

/* ---- the pixel grid -----------------------------------------------------
 * (x, y) -> (li, seq, part, ramp, tone): seq orders pixels within a layer
 * and part identity feeds the inner-outline pass; outer-outline pixels
 * carry part = NULL. Open addressing; slot order is never observable. */
typedef struct {
    int64_t li, seq;
    const char *part;          /* borrowed from the drawable (or NULL) */
    const char *ramp, *tone;   /* borrowed from the style / the fill  */
} pix;

typedef struct { int64_t x, y; pix v; int used; } gslot;
typedef struct { gslot *tab; size_t cap, n; } gmap;

static void gm_init(gmap *g) { g->tab = NULL; g->cap = 0; g->n = 0; }
static void gm_free(gmap *g) { free(g->tab); gm_init(g); }

static uint64_t gmix(int64_t x, int64_t y) {
    uint64_t h = (uint64_t)x * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)y + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 31;
    return h;
}

static int gm_grow(gmap *g) {
    size_t ncap = g->cap ? g->cap * 2 : 64;
    gslot *nt = calloc(ncap, sizeof *nt);
    if (!nt) return rerr("out of memory");
    gslot *old = g->tab;
    size_t ocap = g->cap;
    g->tab = nt;
    g->cap = ncap;
    for (size_t k = 0; k < ocap; k++) {
        if (!old[k].used) continue;
        size_t i = (size_t)gmix(old[k].x, old[k].y) & (ncap - 1);
        while (nt[i].used) i = (i + 1) & (ncap - 1);
        nt[i] = old[k];
    }
    free(old);
    return 0;
}

static int gm_set(gmap *g, int64_t x, int64_t y, pix v) {
    if (g->cap == 0 || (g->n + 1) * 10 >= g->cap * 7)
        if (gm_grow(g)) return -1;
    size_t mask = g->cap - 1;
    size_t i = (size_t)gmix(x, y) & mask;
    while (g->tab[i].used) {
        if (g->tab[i].x == x && g->tab[i].y == y) {
            g->tab[i].v = v;
            return 0;
        }
        i = (i + 1) & mask;
    }
    g->tab[i].used = 1;
    g->tab[i].x = x;
    g->tab[i].y = y;
    g->tab[i].v = v;
    g->n++;
    return 0;
}

static const pix *gm_get(const gmap *g, int64_t x, int64_t y) {
    if (!g->tab) return NULL;
    size_t mask = g->cap - 1;
    size_t i = (size_t)gmix(x, y) & mask;
    while (g->tab[i].used) {
        if (g->tab[i].x == x && g->tab[i].y == y) return &g->tab[i].v;
        i = (i + 1) & mask;
    }
    return NULL;
}

static int gm_clone(const gmap *src, gmap *dst) {
    gm_init(dst);
    if (!src->tab) return 0;
    dst->tab = malloc(src->cap * sizeof *dst->tab);
    if (!dst->tab) return rerr("out of memory");
    memcpy(dst->tab, src->tab, src->cap * sizeof *dst->tab);
    dst->cap = src->cap;
    dst->n = src->n;
    return 0;
}

/* ---- one frame ---------------------------------------------------------- */

typedef struct {
    int64_t W, H;
    int clip;
    int64_t clipped;
    const af_json *style;
    const af_json *layers;
    int64_t nlayers;
} rfctx;

static int put(rfctx *c, gmap *g, int64_t x, int64_t y, int64_t li,
               int64_t seq, const char *part, const char *ramp,
               const char *tone) {
    if (c->clip && !(x >= 0 && x < c->W && y >= 0 && y < c->H)) {
        c->clipped++;
        return 0;
    }
    pix v;
    v.li = li;
    v.seq = seq;
    v.part = part;
    v.ramp = ramp;
    v.tone = tone;
    return gm_set(g, x, y, v);
}

/* the oracle's shade_pick: normals *1024, light ~unit*1024, t in 1/1000 */
static int shade_pick(const af_json *style, const char *const *tones, int n,
                      int64_t nx, int64_t ny, int64_t x, int64_t y,
                      const char **out) {
    if (n <= 0) return rerr("ramp has no tones");
    int64_t v = 1024 * 1024 - nx * nx - ny * ny;
    int64_t nz = isqrt64(v);
    int64_t lx = LIGHT0[0], ly = LIGHT0[1], lz = LIGHT0[2];
    int bayer = 0;
    const af_json *sh = style ? af_json_get(style, "shading") : NULL;
    if (sh && sh->type == AF_JSON_OBJECT) {
        const af_json *lt = af_json_get(sh, "light");
        if (lt && (aint(lt, 0, &lx) || aint(lt, 1, &ly) || aint(lt, 2, &lz)))
            return -1;
        const af_json *dd = af_json_get(sh, "dither");
        bayer = dd && dd->type == AF_JSON_STRING && !strcmp(dd->s, "bayer2");
    }
    int64_t dot = floordiv(nx * lx + ny * ly + nz * lz, 1024);
    int64_t t = floordiv((dot + 460) * 1000, 1485);
    if (bayer) t += DITH[(int)((y & 1) * 2 + (x & 1))];
    int64_t idx = floordiv(t * n, 1000);
    if (idx < 0) idx = 0;
    if (idx > n - 1) idx = n - 1;
    *out = tones[idx];
    return 0;
}

/* point_in_poly: the oracle's even-odd test on 1/64 px points */
static int point_in_poly(const int64_t *pts, size_t n, int64_t cx, int64_t cy) {
    int inside = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t x1 = pts[2 * i], y1 = pts[2 * i + 1];
        size_t j = (i + 1) % n;
        int64_t x2 = pts[2 * j], y2 = pts[2 * j + 1];
        if ((y1 > cy) != (y2 > cy)) {
            int64_t dy = y2 - y1;
            int64_t lhs = (cx - x1) * dy;
            int64_t rhs = (cy - y1) * (x2 - x1);
            if ((dy > 0 && lhs < rhs) || (dy < 0 && lhs > rhs)) inside = !inside;
        }
    }
    return inside;
}

/* the oracle materialises both frame tracks before it renders anything:
 *   {k: tuple(v) for k, v in fr["offsets"].items()}
 *   {k: tuple(v) for k, v in fr.get("rotate", {}).items()}
 * tuple(v) needs v iterable, so a scalar or null value is an uncaught
 * TypeError even when no drawable ever reads that part. Arrays, strings
 * and objects are all iterable in Python. */
static int track_values(const af_json *o) {
    for (size_t i = 0; i + 1 < o->n; i += 2) {
        const af_json *v = o->items[i + 1];
        if (!v || (v->type != AF_JSON_ARRAY && v->type != AF_JSON_STRING &&
                   v->type != AF_JSON_OBJECT))
            return rerr("frame track value is not iterable");
    }
    return 0;
}

/* part_offset: root offset + the part's own offset (both default 0) */
static int part_offset(const af_json *off, const char *part, int64_t *ox,
                       int64_t *oy) {
    int64_t rx = 0, ry = 0, px = 0, py = 0;
    if (off && off->type != AF_JSON_OBJECT)
        return rerr("frame offsets are not an object");
    if (off) {
        const af_json *r = af_json_get(off, "root");
        if (r && (a2(r, &rx, &ry))) return -1;
        if (part && strcmp(part, "root") != 0) {
            const af_json *p = af_json_get(off, part);
            if (p && (a2(p, &px, &py))) return -1;
        }
    }
    *ox = rx + px;
    *oy = ry + py;
    return 0;
}

/* part_rot: the oracle's `r = (rots or {}).get(part); return r if r and
 * r[0] else None`, then rot_pt's (deg, px, py). rots maps a part to
 * tuple(v), so an EMPTY value means "no rotation" and a truthy
 * non-integer degree is an uncaught TypeError (COS[deg]). Returns 1 with
 * r = (deg, px, py), 0 for no rotation, -1 on error. */
static int part_rot(const af_json *rots, const char *part, int64_t r[3]) {
    const af_json *v = (rots && part) ? af_json_get(rots, part) : NULL;
    if (!v) return 0;
    size_t n;
    switch (v->type) {
    case AF_JSON_ARRAY:  n = v->n; break;
    case AF_JSON_STRING: n = v->slen; break;       /* tuple(s) = its chars */
    case AF_JSON_OBJECT: n = v->n / 2; break;      /* tuple(d) = its keys */
    default: return rerr("bad frame rotation");    /* tuple(5) -> TypeError */
    }
    if (n == 0) return 0;                         /* () is falsy -> None */
    if (v->type != AF_JSON_ARRAY) return rerr("bad frame rotation");
    const af_json *d0 = v->items[0];
    if (!d0 || !af_truthy(d0)) return 0;          /* r[0] falsy -> None */
    if (d0->type != AF_JSON_INT && d0->type != AF_JSON_BOOL)
        return rerr("bad frame rotation");        /* COS[<non-int>] */
    int64_t deg = d0->type == AF_JSON_BOOL ? (d0->b ? 1 : 0) : d0->i;
    if (deg == 0) return 0;
    if (v->n < 3) return rerr("bad frame rotation");      /* IndexError */
    if (aint(v, 1, &r[1]) || aint(v, 2, &r[2])) return -1;
    r[0] = deg;
    return 1;
}

/* one drawable, painter order already established */
static int draw_one(rfctx *c, gmap *g, const af_json *d, int64_t ox,
                    int64_t oy, const int64_t *rot) {
    const char *kind = astr(d, "kind");
    int64_t li = 0, seq = 0;
    if (jint(d, "li", &li) || jint(d, "seq", &seq)) return -1;
    const af_json *pn = af_json_get(d, "part");
    if (!pn) return rerr("drawable has no part");
    const char *part = pn->type == AF_JSON_STRING ? pn->s : NULL;

    if (kind && !strcmp(kind, "px")) {
        int64_t ax = 0, ay = 0;
        if (aint(af_json_get(d, "at"), 0, &ax) ||
            aint(af_json_get(d, "at"), 1, &ay)) return -1;
        if (rot) {
            int64_t X = 0, Y = 0;
            if (rot_pt(ax * 64, ay * 64, rot, &X, &Y)) return -1;
            ax = floordiv(X + 32, 64);
            ay = floordiv(Y + 32, 64);
        }
        const af_json *pxs = af_json_get(d, "px");
        if (!pxs || pxs->type != AF_JSON_ARRAY)
            return rerr("px drawable has no pixels");
        for (size_t i = 0; i < pxs->n; i++) {
            const af_json *e = pxs->items[i];
            int64_t dx = 0, dy = 0;
            if (!e || e->type != AF_JSON_ARRAY || e->n != 3)
                return rerr("bad px entry");
            if (aint(e, 0, &dx) || aint(e, 1, &dy)) return -1;
            const af_json *f = e->items[2];
            if (!f || f->type != AF_JSON_ARRAY || f->n != 2 ||
                !f->items[0] || f->items[0]->type != AF_JSON_STRING ||
                !f->items[1] || f->items[1]->type != AF_JSON_STRING)
                return rerr("bad px fill");
            if (put(c, g, ax + ox + dx, ay + oy + dy, li, seq, part,
                    f->items[0]->s, f->items[1]->s))
                return -1;
        }
        return 0;
    }

    const char *ramp = NULL, *tone = NULL;
    if (fill_of(d, &ramp, &tone)) return -1;
    int shaded = !strcmp(tone, "*");
    const char *tl[5];
    int ntl = 0;
    if (shaded && tones_of(c->style, ramp, tl, &ntl)) return -1;

    if (kind && !strcmp(kind, "ellipse")) {
        int64_t x = 0, y = 0, w = 0, h = 0;
        const af_json *box = af_json_get(d, "box");
        if (aint(box, 0, &x) || aint(box, 1, &y) || aint(box, 2, &w) ||
            aint(box, 3, &h)) return -1;
        if (rot) {                       /* the centre rotates, the box does not */
            int64_t X = 0, Y = 0;
            if (rot_pt((2 * x + w) * 32, (2 * y + h) * 32, rot, &X, &Y))
                return -1;
            x = floordiv(floordiv(X + 16, 32) - w, 2);
            y = floordiv(floordiv(Y + 16, 32) - h, 2);
        }
        x += ox;
        y += oy;
        int64_t w2 = w * w, h2 = h * h, wh2 = w2 * h2;
        for (int64_t py = y; py < y + h; py++)
            for (int64_t px = x; px < x + w; px++) {
                int64_t dx = (2 * px + 1) - (2 * x + w);
                int64_t dy = (2 * py + 1) - (2 * y + h);
                if (dx * dx * h2 + dy * dy * w2 > wh2) continue;
                const char *t = tone;
                if (shaded &&
                    shade_pick(c->style, tl, ntl, floordiv(dx * 1024, w),
                               floordiv(dy * 1024, h), px, py, &t))
                    return -1;
                if (put(c, g, px, py, li, seq, part, ramp, t)) return -1;
            }
        return 0;
    }

    if (kind && !strcmp(kind, "capsule")) {
        int64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0, hw = 0;
        const af_json *p = af_json_get(d, "p");
        if (aint(p, 0, &x0) || aint(p, 1, &y0) || aint(p, 2, &x1) ||
            aint(p, 3, &y1)) return -1;
        if (rot) {
            int64_t a = 0, b = 0;
            if (rot_pt(x0, y0, rot, &a, &b)) return -1;
            x0 = a;
            y0 = b;
            if (rot_pt(x1, y1, rot, &a, &b)) return -1;
            x1 = a;
            y1 = b;
        }
        if (jint(d, "hw", &hw)) return -1;
        const af_json *fl = af_json_get(d, "flat");
        int flat = fl && af_truthy(fl);
        x0 += ox * 64;
        x1 += ox * 64;
        y0 += oy * 64;
        y1 += oy * 64;
        int64_t sx = x1 - x0, sy = y1 - y0;
        int64_t l2 = sx * sx + sy * sy;
        int64_t py0 = floordiv((y0 < y1 ? y0 : y1) - hw, 64);
        int64_t py1 = floordiv((y0 > y1 ? y0 : y1) + hw, 64) + 2;
        int64_t px0 = floordiv((x0 < x1 ? x0 : x1) - hw, 64);
        int64_t px1 = floordiv((x0 > x1 ? x0 : x1) + hw, 64) + 2;
        int64_t hw2 = hw * hw;
        for (int64_t py = py0; py < py1; py++)
            for (int64_t px = px0; px < px1; px++) {
                int64_t cx = px * 64 + 32, cy = py * 64 + 32;
                int64_t t = l2 == 0 ? 0 : (cx - x0) * sx + (cy - y0) * sy;
                if (flat && !(t >= 0 && t <= l2)) continue;
                if (l2 != 0) {
                    if (t < 0) t = 0;
                    if (t > l2) t = l2;
                }
                int64_t qx, qy;
                if (l2 == 0) {
                    qx = x0;
                    qy = y0;
                } else {
                    qx = x0 + floordiv(sx * t, l2);
                    qy = y0 + floordiv(sy * t, l2);
                }
                int64_t ex = cx - qx, ey = cy - qy;
                if (ex * ex + ey * ey > hw2) continue;
                const char *t2 = tone;
                if (shaded &&
                    shade_pick(c->style, tl, ntl, floordiv(ex * 1024, hw),
                               floordiv(ey * 1024, hw), px, py, &t2))
                    return -1;
                if (put(c, g, px, py, li, seq, part, ramp, t2)) return -1;
            }
        return 0;
    }

    /* poly (and every other kind, like the oracle's else branch) */
    const af_json *pts_n = af_json_get(d, "pts");
    if (!pts_n || pts_n->type != AF_JSON_ARRAY || pts_n->n == 0)
        return rerr("poly drawable has no points");
    size_t np = pts_n->n;
    int64_t *pts = malloc(np * 2 * sizeof *pts);
    if (!pts) return rerr("out of memory");
    int rc = 0;
    for (size_t i = 0; i < np && !rc; i++) {
        int64_t fx = 0, fy = 0;
        if (aint(pts_n->items[i], 0, &fx) || aint(pts_n->items[i], 1, &fy)) {
            rc = -1;
            break;
        }
        fx += ox * 64;
        fy += oy * 64;
        if (rot && rot_pt(fx, fy, rot, &fx, &fy)) { rc = -1; break; }
        pts[2 * i] = fx;
        pts[2 * i + 1] = fy;
    }
    if (!rc) {
        int64_t mn, mx;
        mn = mx = pts[0];
        for (size_t i = 1; i < np; i++) {
            if (pts[2 * i] < mn) mn = pts[2 * i];
            if (pts[2 * i] > mx) mx = pts[2 * i];
        }
        int64_t x0 = floordiv(mn, 64), x1 = floordiv(mx, 64) + 1;
        mn = mx = pts[1];
        for (size_t i = 1; i < np; i++) {
            if (pts[2 * i + 1] < mn) mn = pts[2 * i + 1];
            if (pts[2 * i + 1] > mx) mx = pts[2 * i + 1];
        }
        int64_t y0 = floordiv(mn, 64), y1 = floordiv(mx, 64) + 1;
        for (int64_t py = y0; py <= y1 && !rc; py++)
            for (int64_t px = x0; px <= x1; px++)
                if (point_in_poly(pts, np, px * 64 + 32, py * 64 + 32)) {
                    rc = put(c, g, px, py, li, seq, part, ramp, tone);
                    if (rc) break;
                }
    }
    free(pts);
    return rc;
}

/* painter order: (li, seq), seq unique within one drawable list */
static int64_t d_li(const af_json *d) {
    const af_json *v = af_json_get(d, "li");
    return v && v->type == AF_JSON_INT ? v->i : 0;
}

static int64_t d_seq(const af_json *d) {
    const af_json *v = af_json_get(d, "seq");
    return v && v->type == AF_JSON_INT ? v->i : 0;
}

static int cmp_d(const void *a, const void *b) {
    const af_json *x = *(const af_json *const *)a;
    const af_json *y = *(const af_json *const *)b;
    int64_t lx = d_li(x), ly = d_li(y);
    if (lx != ly) return lx < ly ? -1 : 1;
    int64_t sx = d_seq(x), sy = d_seq(y);
    if (sx != sy) return sx < sy ? -1 : 1;
    return 0;
}

/* the outline post-pass (style pack "outline"): outer first, then per-part
 * inner. Each pass reads only the grid it was handed, and writes each key
 * at most once with a value derived from that key alone, so the grid's
 * iteration order cannot change the result. */
static int outline_pass(rfctx *c, gmap *grid, int outer) {
    const af_json *ol = c->style ? af_json_get(c->style, "outline") : NULL;
    if (!ol || ol->type != AF_JSON_OBJECT) return rerr("style has no outline");
    const char *mode = astr(ol, "mode");
    if (!mode) return rerr("outline has no mode");

    static const int NB[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const char *ramp;
    const af_json *want = NULL;            /* inner: NULL = every part */
    char *sep = NULL;                      /* outer: layer flags */

    if (outer) {
        if (strcmp(mode, "outer") != 0) return 0;
        ramp = astr(ol, "ramp");
        if (!ramp) return rerr("outline has no ramp");
        sep = calloc((size_t)(c->nlayers > 0 ? c->nlayers : 1), 1);
        if (!sep) return rerr("out of memory");
        const af_json *sepn = af_json_get(ol, "separate_layers");
        if (sepn && sepn->type != AF_JSON_ARRAY) {
            free(sep);
            return rerr("bad separate_layers");
        }
        if (sepn)
            for (size_t i = 0; i < sepn->n; i++) {
                const af_json *it = sepn->items[i];
                if (!it || it->type != AF_JSON_STRING) continue;
                for (int64_t k = 0; k < c->nlayers; k++) {
                    const af_json *ln = c->layers->items[k];
                    if (ln && ln->type == AF_JSON_STRING &&
                        !strcmp(ln->s, it->s)) {
                        sep[k] = 1;
                        break;
                    }
                }
            }
    } else {
        const af_json *inner = af_json_get(ol, "inner");
        if (!inner) return 0;                     /* default {"mode":"none"} */
        const char *im = astr(inner, "mode");
        if (!im || strcmp(im, "part") != 0) return 0;
        ramp = astr(ol, "ramp");
        if (!ramp) return rerr("outline has no ramp");
        want = af_json_get(inner, "parts");
        if (want && want->type != AF_JSON_ARRAY)
            return rerr("bad inner parts");
    }

    gmap out;
    if (gm_clone(grid, &out)) {
        free(sep);
        return -1;
    }
    int rc = 0;

    for (size_t i = 0; i < grid->cap && !rc; i++) {
        const gslot *s = &grid->tab[i];
        if (!s->used) continue;
        int in_sep = sep && s->v.li >= 0 && s->v.li < c->nlayers &&
                     sep[s->v.li];
        if (outer && in_sep) {
            for (int k = 0; k < 4; k++) {
                const pix *n = gm_get(grid, s->x + NB[k][0], s->y + NB[k][1]);
                if (!n) continue;
                int nsep = n->li >= 0 && n->li < c->nlayers && sep[n->li];
                if (!nsep) {
                    pix nv = s->v;
                    nv.ramp = ramp;
                    nv.tone = "base";
                    rc = gm_set(&out, s->x, s->y, nv);
                    break;
                }
            }
        } else if (!outer) {
            const char *part = s->v.part;
            if (!part) continue;
            if (want) {
                int listed = 0;
                for (size_t k = 0; k < want->n; k++) {
                    const af_json *it = want->items[k];
                    if (it && it->type == AF_JSON_STRING &&
                        !strcmp(it->s, part)) {
                        listed = 1;
                        break;
                    }
                }
                if (!listed) continue;
            }
            for (int k = 0; k < 4 && !rc; k++) {
                const pix *n = gm_get(grid, s->x + NB[k][0], s->y + NB[k][1]);
                if (!n || !n->part || !strcmp(n->part, part)) continue;
                if (n->li < s->v.li || (n->li == s->v.li && n->seq < s->v.seq)) {
                    pix nv = s->v;
                    nv.ramp = ramp;
                    nv.tone = "base";
                    rc = gm_set(&out, s->x, s->y, nv);
                    break;
                }
            }
        }
    }
    if (!rc && outer) {
        /* one outline pixel outside every drawable (in-bounds only) */
        for (size_t i = 0; i < grid->cap && !rc; i++) {
            const gslot *s = &grid->tab[i];
            if (!s->used) continue;
            for (int k = 0; k < 4 && !rc; k++) {
                int64_t px = s->x + NB[k][0], py = s->y + NB[k][1];
                if (gm_get(grid, px, py)) continue;
                if (c->clip && !(px >= 0 && px < c->W && py >= 0 && py < c->H))
                    continue;
                pix nv;
                nv.li = c->nlayers;
                nv.seq = 0;
                nv.part = NULL;
                nv.ramp = ramp;
                nv.tone = "base";
                rc = gm_set(&out, px, py, nv);
            }
        }
    }
    free(sep);
    if (!rc) {
        gm_free(grid);
        *grid = out;
    } else {
        gm_free(&out);
    }
    return rc;
}

/* render_frame(ir, offsets, clip, drawables, rots) -> grid + clipped */
static int render_frame(const af_json *ir, const af_json *offsets, int clip,
                        const af_json *ds, const af_json *rots, gmap *grid_out,
                        int64_t *clipped_out) {
    rfctx c;
    memset(&c, 0, sizeof c);
    if (jint(ir, "W", &c.W) || jint(ir, "H", &c.H)) return -1;
    c.clip = clip;
    c.clipped = 0;
    c.style = af_json_get(ir, "style");
    if (!c.style || c.style->type != AF_JSON_OBJECT)
        return rerr("ir has no style");
    const af_json *arch = af_json_get(ir, "arch");
    c.layers = arch ? af_json_get(arch, "layers") : NULL;
    if (!c.layers || c.layers->type != AF_JSON_ARRAY)
        return rerr("ir has no layers");
    c.nlayers = (int64_t)c.layers->n;

    const af_json *dlist = ds ? ds : af_json_get(ir, "drawables");
    if (!dlist || dlist->type != AF_JSON_ARRAY)
        return rerr("ir has no drawables");

    /* painter order (li, seq); seq is unique, so any sort is exact */
    const af_json **sorted = NULL;
    if (dlist->n) {
        sorted = malloc(dlist->n * sizeof *sorted);
        if (!sorted) return rerr("out of memory");
        for (size_t i = 0; i < dlist->n; i++) {
            const af_json *d = dlist->items[i];
            if (!d || d->type != AF_JSON_OBJECT) {
                free(sorted);
                return rerr("bad drawable");
            }
            int64_t v;
            if (jint(d, "li", &v) || jint(d, "seq", &v) || !af_json_get(d, "part")) {
                free(sorted);
                return rerr("drawable is missing part/li/seq");
            }
            sorted[i] = d;
        }
        qsort(sorted, dlist->n, sizeof *sorted, cmp_d);
    }

    gmap g;
    gm_init(&g);
    int rc = 0;
    for (size_t i = 0; i < dlist->n && !rc; i++) {
        const af_json *d = sorted[i];
        const af_json *pn = af_json_get(d, "part");
        const char *part = pn->type == AF_JSON_STRING ? pn->s : NULL;
        int64_t ox = 0, oy = 0;
        if (part_offset(offsets, part, &ox, &oy)) { rc = -1; break; }
        int64_t r[3] = {0, 0, 0};
        int has_rot = part_rot(rots, part, r);
        if (has_rot < 0) { rc = -1; break; }
        rc = draw_one(&c, &g, d, ox, oy, has_rot ? r : NULL);
    }
    free(sorted);
    if (!rc) rc = outline_pass(&c, &g, 1);
    if (!rc) rc = outline_pass(&c, &g, 0);
    if (rc) {
        gm_free(&g);
        return -1;
    }
    *grid_out = g;
    *clipped_out = c.clipped;
    return 0;
}

/* ---- rgba --------------------------------------------------------------- */

static int to_rgba(const af_json *ir, const gmap *grid, unsigned char **out) {
    int64_t W = 0, H = 0;
    if (jint(ir, "W", &W) || jint(ir, "H", &H)) return -1;
    if (W <= 0 || H <= 0 || (uint64_t)W > UINT64_MAX / 4 / (uint64_t)H)
        return rerr("bad cell size");
    const af_json *style = af_json_get(ir, "style");
    const af_json *ramps = style ? af_json_get(style, "ramps") : NULL;
    if (!ramps || ramps->type != AF_JSON_OBJECT)
        return rerr("style has no ramps");
    size_t sz = (size_t)W * (size_t)H * 4;
    unsigned char *buf = calloc(sz, 1);
    if (!buf) return rerr("out of memory");
    for (size_t i = 0; i < grid->cap; i++) {
        const gslot *s = &grid->tab[i];
        if (!s->used) continue;
        if (s->x < 0 || s->x >= W || s->y < 0 || s->y >= H) continue;
        const af_json *rd = af_json_get(ramps, s->v.ramp);
        const af_json *td = rd ? af_json_get(rd, s->v.tone) : NULL;
        int64_t r = 0, g = 0, b = 0;
        if (!td || aint(td, 0, &r) || aint(td, 1, &g) || aint(td, 2, &b) ||
            r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
            free(buf);
            return rerr("ramp/tone '%s.%s' does not exist in the style pack",
                        s->v.ramp, s->v.tone);
        }
        size_t k = ((size_t)s->y * (size_t)W + (size_t)s->x) * 4;
        buf[k] = (unsigned char)r;
        buf[k + 1] = (unsigned char)g;
        buf[k + 2] = (unsigned char)b;
        buf[k + 3] = 255;
    }
    *out = buf;
    return 0;
}

/* ---- the frame list ----------------------------------------------------- */

typedef struct {
    const char *name;         /* borrowed from the IR */
    const af_json *off;       /* frame offsets (NULL = {}) */
    const af_json *rots;      /* frame rotate (NULL = none) */
    const af_json *ds;        /* state drawables (NULL = the base set) */
    gmap grid;
    int64_t clipped;
    unsigned char *rgba;
} rent;

static void ents_free(rent *e, size_t n) {
    if (!e) return;
    for (size_t i = 0; i < n; i++) {
        gm_free(&e[i].grid);
        free(e[i].rgba);
    }
    free(e);
}

static int ent_push(rent **ents, size_t *n, size_t *cap, const char *name,
                    const af_json *off, const af_json *rots,
                    const af_json *ds) {
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 8;
        rent *ne = realloc(*ents, nc * sizeof *ne);
        if (!ne) return rerr("out of memory");
        *ents = ne;
        *cap = nc;
    }
    rent *e = &(*ents)[*n];
    memset(e, 0, sizeof *e);
    e->name = name;
    e->off = off;
    e->rots = rots;
    e->ds = ds;
    gm_init(&e->grid);
    (*n)++;
    return 0;
}

/* ---- auto cell (spec-next 01 §2) ---------------------------------------- */

/* the union bbox of every frame, + margin, to multiple_of, to min_cell,
 * placed per align and clamped so the bbox keeps >= margin on every side.
 * Metrics were evaluated at the provisional cell, so this is a pure
 * grid/anchor/hitbox shift: nothing is re-derived, nothing can clip. */
static int resolve_auto_cell(af_json *ir, rent *ents, size_t nent) {
    int any = 0;
    int64_t minx = 0, maxx = 0, miny = 0, maxy = 0;
    for (size_t e = 0; e < nent; e++)
        for (size_t i = 0; i < ents[e].grid.cap; i++) {
            const gslot *s = &ents[e].grid.tab[i];
            if (!s->used) continue;
            if (!any) {
                minx = maxx = s->x;
                miny = maxy = s->y;
                any = 1;
            } else {
                if (s->x < minx) minx = s->x;
                if (s->x > maxx) maxx = s->x;
                if (s->y < miny) miny = s->y;
                if (s->y > maxy) maxy = s->y;
            }
        }
    int64_t bw = any ? maxx - minx + 1 : 0;
    int64_t bh = any ? maxy - miny + 1 : 0;
    const af_json *a = af_json_get(ir, "auto");
    if (!a || a->type != AF_JSON_OBJECT) return rerr("bad auto canvas");
    int64_t m = 0, k = 0;
    if (jint(a, "margin", &m) || jint(a, "multiple_of", &k)) return -1;
    if (k <= 0) return rerr("multiple_of must be positive");
    int64_t w = bw + 2 * m, h = bh + 2 * m;
    const af_json *mc = af_json_get(ir, "min_cell");
    if (mc && af_truthy(mc)) {
        int64_t mw, mh;
        if (a2(mc, &mw, &mh)) return -1;
        if (mw > w) w = mw;
        if (mh > h) h = mh;
    }
    w = floordiv(w + k - 1, k) * k;
    h = floordiv(h + k - 1, k) * k;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    int64_t px = 0, py = 0;
    const af_json *anchors = af_json_get(ir, "anchors");
    if (anchors && anchors->type == AF_JSON_OBJECT) {
        const af_json *org = af_json_get(anchors, "origin");
        const af_json *at = org ? af_json_get(org, "at") : NULL;
        if (at && (a2(at, &px, &py))) return -1;
    }
    const char *align = astr(a, "align");
    int64_t dx, dy;
    if (align && !strcmp(align, "top_left")) {
        dx = m - minx;
        dy = m - miny;
    } else if (align && !strcmp(align, "pivot_center")) {
        dx = w / 2 - px;
        dy = h / 2 - py;
    } else {                                /* pivot_bottom_center */
        dx = w / 2 - px;
        dy = h - 1 - m - py;
    }
    if (any) {
        int64_t lo = m - minx, hi = (w - 1 - m) - maxx;
        if (dx < lo) dx = lo;
        if (dx > hi) dx = hi;
        lo = m - miny;
        hi = (h - 1 - m) - maxy;
        if (dy < lo) dy = lo;
        if (dy > hi) dy = hi;
    }

    if (!aget(ir, "W") || !aget(ir, "H"))
        return rerr("ir has no W/H");
    af_json *wv = af_json_int(w), *hv = af_json_int(h);
    if (!wv || !hv) {
        af_json_free(wv);
        af_json_free(hv);
        return rerr("out of memory");
    }
    if (af_json_obj_set(ir, "W", wv)) {      /* takes wv (frees on failure) */
        af_json_free(hv);
        return rerr("out of memory");
    }
    if (af_json_obj_set(ir, "H", hv))        /* takes hv */
        return rerr("out of memory");

    for (size_t e = 0; e < nent; e++) {
        gmap ng;
        gm_init(&ng);
        for (size_t i = 0; i < ents[e].grid.cap; i++) {
            const gslot *s = &ents[e].grid.tab[i];
            if (!s->used) continue;
            if (gm_set(&ng, s->x + dx, s->y + dy, s->v)) {
                gm_free(&ng);
                return -1;
            }
        }
        gm_free(&ents[e].grid);
        ents[e].grid = ng;
        ents[e].clipped = 0;
    }

    af_json *ans = aget(ir, "anchors");
    if (!ans || ans->type != AF_JSON_OBJECT)
        return rerr("ir has no anchors");
    for (size_t i = 0; i < ans->n; i += 2) {
        af_json *ad = ans->items[i + 1];
        af_json *at = ad ? aget(ad, "at") : NULL;
        if (!at || at->type != AF_JSON_ARRAY || at->n != 2 ||
            at->items[0]->type != AF_JSON_INT ||
            at->items[1]->type != AF_JSON_INT)
            return rerr("bad anchor at");
        at->items[0]->i += dx;
        at->items[1]->i += dy;
    }
    af_json *hbs = aget(ir, "hitboxes");
    if (!hbs || hbs->type != AF_JSON_ARRAY)
        return rerr("ir has no hitboxes");
    for (size_t i = 0; i < hbs->n; i++) {
        af_json *hb = hbs->items[i];
        af_json *rc = hb ? aget(hb, "rect") : NULL;
        if (!rc || rc->type != AF_JSON_ARRAY || rc->n != 4 ||
            rc->items[0]->type != AF_JSON_INT ||
            rc->items[1]->type != AF_JSON_INT)
            return rerr("bad hitbox rect");
        rc->items[0]->i += dx;
        rc->items[1]->i += dy;
    }
    return 0;
}

/* ---- the frame list ----------------------------------------------------- */

/* the oracle's frame_state_key + states[key] */
static int frame_drawables(const af_json *ir, const af_json *fr,
                           const af_json **out) {
    const af_json *tr = af_json_get(fr, "params");
    if (tr && !af_truthy(tr)) tr = NULL;
    const af_json *sm = af_json_get(fr, "swap");
    if (sm && !af_truthy(sm)) sm = NULL;
    if (!tr && !sm) {
        *out = NULL;
        return 0;
    }
    const af_json *states = af_json_get(ir, "states");
    if (!states || states->type != AF_JSON_ARRAY)
        return rerr("ir has no states");
    for (size_t i = 0; i < states->n; i++) {
        const af_json *st = states->items[i];
        const af_json *sp = st ? af_json_get(st, "params") : NULL;
        const af_json *ss = st ? af_json_get(st, "swap") : NULL;
        int ep = tr ? af_json_equal(sp, tr)
                    : (sp && sp->type == AF_JSON_OBJECT && sp->n == 0);
        int es = sm ? af_json_equal(ss, sm)
                    : (ss && ss->type == AF_JSON_OBJECT && ss->n == 0);
        if (ep && es) {
            const af_json *ds = af_json_get(st, "drawables");
            if (!ds || ds->type != AF_JSON_ARRAY)
                return rerr("bad frame state");
            *out = ds;
            return 0;
        }
    }
    return rerr("no state for the frame's (params, swap)");  /* KeyError */
}

int af_render_all(af_json *ir, af_renders **out) {
    g_why[0] = 0;
    if (!out) return rerr("no output slot");
    *out = NULL;
    if (!ir || ir->type != AF_JSON_OBJECT) return rerr("bad ir");

    const af_json *auto_n = af_json_get(ir, "auto");
    int auto_mode = auto_n && af_truthy(auto_n);
    const af_json *anims = af_json_get(ir, "animations");
    if (anims && anims->type != AF_JSON_OBJECT)
        return rerr("bad animations");

    rent *ents = NULL;
    size_t nent = 0, cap = 0;
    if (ent_push(&ents, &nent, &cap, "base", NULL, NULL, NULL)) goto fail;

    if (anims)
        for (size_t i = 0; i + 1 < anims->n; i += 2) {
            const char *name = anims->items[i]->s;
            const af_json *tpl = anims->items[i + 1];
            const af_json *frames =
                tpl && tpl->type == AF_JSON_OBJECT ? af_json_get(tpl, "frames")
                                                   : NULL;
            if (!frames || frames->type != AF_JSON_ARRAY) {
                rerr("animation '%s' has no frames", name);
                goto fail;
            }
            for (size_t fi = 0; fi < frames->n; fi++) {
                const af_json *fr = frames->items[fi];
                if (!fr || fr->type != AF_JSON_OBJECT) {
                    rerr("bad frame");
                    goto fail;
                }
                const af_json *off = af_json_get(fr, "offsets");
                if (!off || off->type != AF_JSON_OBJECT) {
                    rerr("frame has no offsets");     /* the oracle's KeyError */
                    goto fail;
                }
                if (track_values(off)) goto fail;
                const af_json *rot = af_json_get(fr, "rotate");
                if (rot && rot->type != AF_JSON_OBJECT) {
                    rerr("bad frame rotate");         /* .items() on a scalar */
                    goto fail;
                }
                if (rot && track_values(rot)) goto fail;
                const af_json *ds = NULL;
                if (frame_drawables(ir, fr, &ds)) goto fail;
                if (ent_push(&ents, &nent, &cap, name, off, rot, ds))
                    goto fail;
            }
        }

    for (size_t e = 0; e < nent; e++)
        if (render_frame(ir, ents[e].off, !auto_mode, ents[e].ds,
                         ents[e].rots, &ents[e].grid, &ents[e].clipped))
            goto fail;

    if (auto_mode && resolve_auto_cell(ir, ents, nent))
        goto fail;

    for (size_t e = 0; e < nent; e++)
        if (to_rgba(ir, &ents[e].grid, &ents[e].rgba)) goto fail;

    {
        af_renders *r = calloc(1, sizeof *r);
        if (!r) {
            rerr("out of memory");
            goto fail;
        }
        r->frames = calloc(nent ? nent : 1, sizeof *r->frames);
        if (!r->frames) {
            free(r);
            rerr("out of memory");
            goto fail;
        }
        r->nframes = nent;
        if (jint(ir, "W", &r->W) || jint(ir, "H", &r->H)) {
            free(r->frames);
            free(r);
            goto fail;
        }
        for (size_t e = 0; e < nent; e++) {
            size_t ln = strlen(ents[e].name);
            char *nm = malloc(ln + 1);
            if (!nm) {
                free(r->frames);
                free(r);
                rerr("out of memory");
                goto fail;
            }
            memcpy(nm, ents[e].name, ln + 1);
            r->frames[e].name = nm;
            r->frames[e].clipped = ents[e].clipped;
            r->frames[e].rgba = ents[e].rgba;
            ents[e].rgba = NULL;
        }
        ents_free(ents, nent);
        *out = r;
        return 0;
    }

fail:
    ents_free(ents, nent);
    return -1;
}

void af_renders_free(af_renders *r) {
    if (!r) return;
    for (size_t i = 0; i < r->nframes; i++) {
        free(r->frames[i].name);
        free(r->frames[i].rgba);
    }
    free(r->frames);
    free(r);
}
