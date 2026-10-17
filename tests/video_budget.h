/* ARCADE-1 — running per-frame video-work accumulator (BEFORE_M25 task C).
 *
 * Test-only, header-only. Each of the four game tests keeps one of these over
 * its scripted run so DEVICE_BUDGET.md can be sized from measured data: an
 * average and a maximum per frame for instructions, commands, pixels written
 * and bytes fetched.
 *
 * Reads the device's end-of-frame snapshot (arcade1_video_frame_totals),
 * which arcade1_run_frame freezes just before it returns, so calling this
 * once per arcade1_run_frame gives that frame's real work. Pure observation:
 * it changes no machine state and no assertion.
 */

#ifndef ARCADE1_TEST_VIDEO_BUDGET_H
#define ARCADE1_TEST_VIDEO_BUDGET_H

#include "machine.h"    /* arcade1_machine_t; pulls in video.h */

#include <stdint.h>
#include <stdio.h>

typedef struct video_budget {
    /* Averages over the run. */
    uint64_t avg_inst, avg_cmd, avg_px, avg_bytes;
    /* Worst single frame seen. */
    uint64_t max_inst, max_cmd, max_px, max_bytes;
    uint32_t frames;
} video_budget_t;

/* Fold one just-finished frame into the accumulator. */
static inline void video_budget_sample(video_budget_t *b,
                                       const arcade1_machine_t *m,
                                       uint64_t inst_this_frame)
{
    const arcade1_video_stats_t *s = arcade1_video_frame_totals(&m->video);

    b->avg_inst  += inst_this_frame;
    b->avg_cmd   += s->commands;
    b->avg_px    += s->pixels_written;
    b->avg_bytes += s->bytes_fetched;

    if (inst_this_frame > b->max_inst)  b->max_inst  = inst_this_frame;
    if (s->commands     > b->max_cmd)   b->max_cmd   = s->commands;
    if (s->pixels_written > b->max_px)  b->max_px    = s->pixels_written;
    if (s->bytes_fetched > b->max_bytes) b->max_bytes = s->bytes_fetched;

    b->frames++;
}

/* One summary line per game (task C step 2). Every number is MEASURED. */
static inline void video_budget_report(const char *game, const video_budget_t *b)
{
    if (b->frames == 0u)
        return;
    printf("video budget: %s  %u frames | cmd/frame avg %llu max %llu | "
           "px/frame avg %llu max %llu | bytes/frame avg %llu max %llu | "
           "inst/frame avg %llu max %llu\n",
           game, b->frames,
           (unsigned long long)(b->avg_cmd   / b->frames),
           (unsigned long long)b->max_cmd,
           (unsigned long long)(b->avg_px    / b->frames),
           (unsigned long long)b->max_px,
           (unsigned long long)(b->avg_bytes / b->frames),
           (unsigned long long)b->max_bytes,
           (unsigned long long)(b->avg_inst  / b->frames),
           (unsigned long long)b->max_inst);
}

#endif /* ARCADE1_TEST_VIDEO_BUDGET_H */
