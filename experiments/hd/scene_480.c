/* experiments/hd/scene_480.c — H3, the control scene.
 *
 * The Petal Blade valley at the machine's own geometry: 480x270, art at its
 * native density (the rig hero at 28x40, the committed 240x60 ridge, four
 * 16x16 terrain cells, the 12x12 coin, the SDK's 5x7 HUD).
 *
 * Runs on the DEFAULT machine — build/arcade1, untouched — so this guest
 * also proves the experiment added nothing to the default build's behaviour.
 * It draws exactly the same composition as scene_960; see scene_body.h for
 * what that means and docs/experiments/hd-art.md for the review.
 */

#define SCREEN_W     480
#define SCREEN_H     270
#define SCENE_SCALE  1
#define SCENE_ASSETS "scene480_art/assets.h"

#include "scene_body.h"
