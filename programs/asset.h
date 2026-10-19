#ifndef ARCADE1_ASSET_H
#define ARCADE1_ASSET_H
#include "arcade1.h"

/* asset.h — typed accessors over arc1-assetc output (ROADMAP_CREATION_FLOW
 * Wave 2). tools/arc1_assetc.py emits, per sprite, a per-frame ref array
 *
 *     static const sprite4_ref hero_frames[HERO_FRAMES] = {…};
 *
 * so game code addresses art by frame, never by stride arithmetic. A ref
 * carries the frame's pixels, geometry and palette bank, plus the gameplay
 * metadata declared in assets.json ("origin" and "collision"). The emitted
 * header names sprite4_ref, so include THIS header before it:
 *
 *     #include "asset.h"            // sprite4_ref + asset_sprite4
 *     #include "assets.h"           // the generated header
 *
 * Names resolve at build time only (AssetEcosystem.md §13): the manifest's
 * "name" becomes a C identifier; nothing on the target looks art up by
 * string. */

/* One drawn frame. Field order is the emitter's initializer order:
 *   data         this frame's 4-bit pixels (sheet + frame * STRIDE)
 *   w, h         frame size in pixels (w even)
 *   bank         palette bank the indices address (0..15)
 *   ox, oy       origin: the anchor INSIDE the frame ([0,0] = top-left;
 *                [w/2, h] is the classic bottom-centre feet anchor — the
 *                edge itself is allowed)
 *   cx, cy, cw, ch
 *                collision box inside the frame; cw = ch = 0 = no box */
typedef struct sprite4_ref {
    const unsigned char *data;
    int w, h;
    int bank;
    int ox, oy;
    int cx, cy, cw, ch;
} sprite4_ref;

/* Draw one ref at top-left (x, y): the ref carries pixels, size and bank,
 * so the call site repeats nothing. flags bit0 = flip X, bit1 = flip Y —
 * the same bits arcade1_sprite4() takes; the device mirrors within the
 * frame, so one sheet serves both directions. Placement by a ref's own
 * metadata reads:
 *     asset_sprite4(&hero_frames[f], x - hero_frames[f].ox,
 *                                       y - hero_frames[f].oy, flip); */
static inline void asset_sprite4(const sprite4_ref *ref, int x, int y,
                                 unsigned flags)
{
    arcade1_sprite4(x, y, ref->data, ref->w, ref->h,
                    (unsigned)ref->bank, flags);
}

#endif
