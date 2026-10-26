/* make test: determinism, balance and a boss fight, headless. Optional: ./shmup_test dump -> /tmp/shmup_*.ppm */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "shmup.h"
static uint32_t fb[SH_W * SH_H];
static uint32_t hash(void) { shmup_render(fb); uint32_t h = 2166136261u; for (int i = 0; i < SH_W * SH_H; i++) h = (h ^ fb[i]) * 16777619u; return h; }
static void ppm(const char *n) { FILE *f = fopen(n, "wb"); fprintf(f, "P6\n%d %d\n255\n", SH_W, SH_H); for (int i = 0; i < SH_W * SH_H; i++) { fputc(fb[i] >> 16, f); fputc(fb[i] >> 8, f); fputc(fb[i], f); } fclose(f); }
/* scripted player: taps A every `tap` frames (0 = never), sways left/right */
static uint32_t run(int tap, int alt, int frames, int *won, int dump) {
    shmup_init(); uint16_t prev = 0; shmup_update(0, SH_A, SH_A); shmup_update(1, 0, 0); shmup_debug_set_alt(alt); shmup_debug_set_lives(99);
    uint32_t h = 0; for (int f = 2; f < frames; f++) {
        uint16_t cur = ((f / 90) & 1) ? SH_LEFT : SH_RIGHT; if (tap && f % tap < 2) cur |= SH_A;
        shmup_update(f, cur, cur & ~prev); prev = cur; if (f % 50 == 0) h ^= hash() + f;
        if (dump && f == frames - 1) { shmup_render(fb); }
        if (shmup_state() != SH_PLAY) break;
    }
    *won = shmup_state() == SH_WIN; return h ^ (uint32_t)shmup_score();
}
int main(int argc, char **argv) {
    (void)argv;
    int w1, w2; uint32_t a = run(4, 0, 3600, &w1, 0), b = run(4, 0, 3600, &w2, 0);
    printf("determinism: %08x %08x -> %s\n", a, b, a == b ? "same" : "DIFFERENT"); assert(a == b);
    int won; run(4, 0, 3600, &won, 0); printf("fast tapper vs boss: %s, score %d\n", won ? "won" : "not won", shmup_score()); assert(won);
    run(0, 0, 3600, &won, 0); printf("auto-fire only vs boss: %s, score %d\n", won ? "won" : "not won", shmup_score());
    run(4, 12000, 5400, &won, 0); printf("full descent with fast taps: state %d, alt %d, score %d, lives %d\n", shmup_state(), shmup_alt(), shmup_score(), shmup_lives());
    if (argc > 1) {                                           /* dump 5 altitude frames */
        int alts[5] = { 11500, 8500, 5000, 2500, 500 };
        for (int i = 0; i < 5; i++) { shmup_init(); shmup_update(0, SH_A, SH_A); shmup_update(1, 0, 0); shmup_debug_set_alt(alts[i]); uint16_t prev = 0;
            for (int f = 2; f < 260; f++) { uint16_t cur = (f % 7 < 2) ? SH_A : 0; cur |= (f / 60 & 1) ? SH_LEFT : SH_RIGHT; shmup_update(f, cur, cur & ~prev); prev = cur; shmup_debug_set_alt(alts[i] - (f - 2) * 2 + (f - 2) * 2); }
            shmup_render(fb); char n[40]; sprintf(n, "/tmp/shmup_%d.ppm", i); ppm(n); }
    }
    puts("ok"); return 0;
}
