/* B2 regression probe (ROADMAP_V3 §5, card B2): pixel provenance.
 *
 * Draws a sprite, then a FILL covering it — once per host frame, paced by
 * the timer tick (TIMER_COUNT advances exactly once per machine frame, so
 * the recorded last frame holds one clean CLEAR/SPRITE/FILL/PRESENT
 * sequence and never overflows the 4096-record cap).
 *
 * tests/test_inspect_commands.sh asserts that `arcade1 inspect --commands`
 * lists the SPRITE before the FILL with every PC inside main's symbol
 * range (from the .elf's nm output), and that `--at 12,12` — a pixel both
 * commands touch — names the FILL as the last writer.
 *
 * Everything draws inside main on purpose: one guest function, one symbol
 * range, no inlining ambiguity for the PC assertion.
 */
#include "arcade1.h"

static const unsigned char blob[8] = {  /* 8x8 all-set bits: a solid sprite */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

int main(void)
{
    /* TIMER_COUNT (timer device, MMIO offset 0x00): +1 per machine frame. */
    volatile unsigned *tick =
        (volatile unsigned *)(ARCADE1_MMIO_BASE + 0x00u);
    unsigned last = 0u;

    for (;;) {
        unsigned t = *tick;

        if (t == last)
            continue;                       /* no new frame yet: idle      */
        last = t;
        arcade1_clear(0x0000u);
        arcade1_sprite(10, 10, blob, 8, 8, 0xF800u); /* red sprite at 10,10 */
        arcade1_rect(5, 5, 40, 40, 0x07E0u);   /* green FILL covers it      */
        arcade1_present();
    }
}
