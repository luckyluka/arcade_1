/* tapcad.h - tap-speed cadence + A/B charge, integer only (RV32I-safe, deterministic).
 * Same rules as speedpunch.h, without the punch animation/event parts.
 *   tc_press(): call on each rising edge of button A or B with a monotonic ms timestamp.
 *   Tapping one button repeatedly raises speed; alternating A/B quickly fills charge.
 * Q8 = 256 is 1.0. Charge range is 0..TC_CHARGE_FULL (100.0 in Q8). */
#ifndef TAPCAD_H
#define TAPCAD_H
#include <stdint.h>
#define TC_SLOW_MS 300
#define TC_FAST_MS 70
#define TC_RESET_MS 700
#define TC_ALT_MS 400
#define TC_DEBOUNCE_MS 25
#define TC_CHARGE_FULL 25600
typedef struct { uint32_t last_t, charge_t, tick_t; uint16_t ema, speed, charge; uint8_t btn, has_last, has_charge; } tapcad_t;
static inline int32_t tc__cl(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline void tc_init(tapcad_t *c) { uint8_t *p = (uint8_t *)c; for (unsigned i = 0; i < sizeof *c; i++) p[i] = 0; }
static inline void tc_press(tapcad_t *c, uint32_t t, int btn) {
    if (c->has_last) {
        uint32_t iv = t - c->last_t;
        if (iv < TC_DEBOUNCE_MS) return;
        c->ema = iv < TC_RESET_MS ? (uint16_t)(c->ema ? (c->ema + iv) / 2 : iv) : 0;
        if (btn != c->btn && iv < TC_ALT_MS) {
            int32_t q = tc__cl(((int32_t)(TC_ALT_MS - iv) * 256) / 330, 0, 256);
            c->charge = (uint16_t)tc__cl((int32_t)c->charge + 512 + 10 * q, 0, TC_CHARGE_FULL);
            c->charge_t = t; c->has_charge = 1; c->btn = (uint8_t)btn; c->last_t = t; return;
        }
    }
    c->btn = (uint8_t)btn; c->last_t = t; c->has_last = 1;
    c->speed = c->ema ? (uint16_t)tc__cl(((int32_t)(TC_SLOW_MS - c->ema) * 256) / (TC_SLOW_MS - TC_FAST_MS), 0, 256) : 0;
}
/* speed (Q8) decaying after 250 ms of no taps */
static inline uint16_t tc_speed(const tapcad_t *c, uint32_t now) {
    if (!c->has_last) return 0;
    int32_t idle = (int32_t)(now - c->last_t), f = idle > 250 ? 256 - (idle - 250) * 256 / 350 : 256;
    return (uint16_t)((c->speed * tc__cl(f, 0, 256)) >> 8);
}
static inline int tc_charging(const tapcad_t *c, uint32_t now) { return c->has_charge && now - c->charge_t < 350; }
static inline void tc_tick(tapcad_t *c, uint32_t now) {      /* call once per frame */
    uint32_t dt = now - c->tick_t; c->tick_t = now;
    if (c->charge && c->has_last && now - c->last_t > 500) { uint32_t d = dt * 512u / 100u; c->charge = d >= c->charge ? 0 : (uint16_t)(c->charge - d); }
}
static inline uint16_t tc_spend(tapcad_t *c) { uint16_t v = c->charge; c->charge = 0; return v; }
#endif
