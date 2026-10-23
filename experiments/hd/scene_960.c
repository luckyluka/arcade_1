/* experiments/hd/scene_960.c — H3, the HD scene.
 *
 * The SAME composition as scene_480 — one shared body, scene_body.h, with
 * SCENE_SCALE 2 — but at 960x540 with the art regenerated at 2x native
 * density: the hero from the rig animator at twice the height (56x80), the
 * ridge drawn at 480x120 by the game's own generator with its geometry
 * doubled, and the HUD rasterised at 2x.
 *
 * HOW THE GUEST SEES COORDINATES (HD_EXPERIMENT.md card H3 asks this to be
 * stated): this guest draws 960x540 pixels NATIVELY, not through the block
 * expander. It runs on build/arcade1-geo960, which is the same sources with
 *   -DARCADE1_WIDTH=960 -DARCADE1_HEIGHT=540
 * and ARCADE1_HD_SCALE left at its #ifndef default of 1. Because machine.h
 * computes
 *   ARCADE1_FB_W      = ARCADE1_WIDTH  * ARCADE1_HD_SCALE
 *   ARCADE1_FB_PIXELS = ARCADE1_WIDTH  * ARCADE1_HEIGHT
 * both the guest's logical grid and the physical framebuffer become
 * 960x540 — one guest pixel is one scanout pixel, the expander's block size
 * is 1x1 (i.e. absent), and the guest's coordinates are simply 0..959 by
 * 0..539. programs/gfx.h contains no geometry at all: it writes registers
 * and the DEVICE clips to its ARCADE1_WIDTH/HEIGHT, so the SDK needed no
 * change and neither did programs/arcade1.h (this file defines its own
 * SCREEN_W/SCREEN_H, the pattern brickstorm/flappy/petalblade already use).
 *
 * A -DARCADE1_HD_SCALE=2 build with these dimensions would instead output
 * 1920x1080 THROUGH the block expander, which is exactly what this card
 * forbids for this scene — so the scene's build is deliberately k-free, and
 * the 1920x1080 capture comes from the host's --scale 2 nearest-neighbour
 * capture, the same operation the 480 scene gets at --scale 4.
 *
 * Zero changes to src/ were needed for any of this: the #ifndef guards H1
 * added around ARCADE1_WIDTH/ARCADE1_HEIGHT are the override.
 */

#define SCREEN_W     960
#define SCREEN_H     540
#define SCENE_SCALE  2
#define SCENE_ASSETS "scene960_art/assets.h"

#include "scene_body.h"
