/* colour truncation probe (ROADMAP_V4 card 4.4).
 *
 * Writes a 24-bit colour literal (0xE8C0A0) where RGB565 was meant, in a
 * CLEAR and in a FILL, and one correct 16-bit colour (from arcade1_rgb()).
 *
 * The device masks the colour register to 16 bits exactly as it always
 * did — the pixels are unchanged — but the host now counts the write, so
 * `arcade1 inspect` reports VIDEO_COLOR_TRUNC and `arcade1 run --strict`
 * fails. The clean colour must not be counted.
 *
 * Built twice: the default writes the 24-bit literals, and
 * -DWRITE_MASKED writes the already-masked 16-bit values instead. The two
 * must produce the SAME framebuffer (the diagnostic changes no pixel) —
 * the test compares their fb hashes.
 */
#include "arcade1.h"

#ifdef WRITE_MASKED
/* What the device makes of the literals below, written directly. */
#define BAD_CLEAR 0xC0A0u
#define BAD_RECT  0xFF00u
#else
#define BAD_CLEAR 0xE8C0A0u    /* 24-bit: truncated to 0xC0A0 by the device */
#define BAD_RECT  0x00FF00u   /* 24-bit: truncated to 0xFF00 by the device */
#endif

int bad_clears = 0;      /* observable: how many bad colours this guest wrote */
int good_clears = 0;

int main(void)
{
    for (;;) {
        /* A 24-bit literal in a 16-bit colour register: truncated silently. */
        arcade1_clear(BAD_CLEAR);
        bad_clears++;
        arcade1_rect(10, 10, 20, 20, BAD_RECT);
        /* The correct way: arcade1_rgb() returns 16 bits. */
        arcade1_rect(40, 40, 20, 20, arcade1_rgb(0xE8, 0xC0, 0xA0));
        good_clears++;
        arcade1_present();
    }
}
