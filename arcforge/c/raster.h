/* raster.h -- M4.2: drawables -> RGBA frames (arcforge AGENTS rule 4).
 *
 * Integer / fixed-point only: every decision here is integer arithmetic,
 * the trig comes from the generated c/gen/trig_table.h table and this
 * file never calls libm. The oracle is tools/arcforge_ref.py
 * (render_frame / to_rgba / resolve_auto_cell / render_all / frame_hashes)
 * and tests/test_render_parity.py is the differential.
 *
 * The input is the compiled IR (the af_json object compile_asset builds,
 * same keys as the oracle's): drawables, style, arch.layers, anchors,
 * animations, states, auto, min_cell, hitboxes, W, H.
 */
#ifndef ARCFORGE_RASTER_H
#define ARCFORGE_RASTER_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"

/* Rotate a 1/64-px vector by whole degrees (the oracle's rot_fixed):
 *   ((x*cos - y*sin) * 64 + 500000) // 1000000   (floor division)
 * deg must be in -180..180. */
void af_rot_fixed(int64_t x, int64_t y, int64_t deg, int64_t *ox, int64_t *oy);

/* One rendered frame: the animation name it belongs to ("base" for the
 * bind pose), the clip counter of pass 1 and the W*H*4 RGBA bytes. */
typedef struct {
    char *name;              /* heap, owned */
    int64_t clipped;
    unsigned char *rgba;     /* heap, owned; W*H*4 bytes */
} af_frame;

typedef struct {
    int64_t W, H;            /* after the auto-cell resolve, like the oracle */
    af_frame *frames;        /* "base" first, then each animation in
                              * document order, frames in order */
    size_t nframes;
} af_renders;

/* Render every frame of a compiled IR. The IR is mutated exactly like the
 * oracle's render_all (an auto cell rewrites W/H, anchors and hitboxes).
 * Returns 0, or -1 with af_render_why() set -- the oracle's uncaught
 * exception (empty stdout, exit 1), which the caller must mirror. */
int af_render_all(af_json *ir, af_renders **out);
const char *af_render_why(void);
void af_renders_free(af_renders *r);

#endif /* ARCFORGE_RASTER_H */
