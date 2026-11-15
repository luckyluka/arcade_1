/* shmup.h - "Sky Descent" game core for ARCADE-1. Portable C99, integer only, no libm,
 * no malloc, deterministic: same inputs per frame => same game. 240x320 XRGB8888. */
#ifndef SHMUP_H
#define SHMUP_H
#include <stdint.h>
#define SH_W 240
#define SH_H 320
enum { SH_UP = 1, SH_DOWN = 2, SH_LEFT = 4, SH_RIGHT = 8, SH_A = 16, SH_B = 32, SH_C = 64 };
enum { SH_TITLE, SH_PLAY, SH_DEAD, SH_WIN };
void shmup_init(void);
/* held = buttons down this frame; pressed = rising edges this frame. A/B are the two tap buttons, C releases the beam. */
void shmup_update(uint32_t frame, uint16_t held, uint16_t pressed);
void shmup_render(uint32_t *fb);                 /* SH_W*SH_H XRGB8888 */
int  shmup_state(void); int shmup_alt(void); int shmup_score(void); int shmup_lives(void);
void shmup_debug_set_alt(int alt);               /* tests only */
void shmup_debug_set_lives(int n);               /* tests only */
#endif
