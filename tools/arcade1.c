/* ARCADE-1 developer CLI — `arcade1` (milestone 28, AI SDK v0.1 foundation).
 *
 * A headless, deterministic front door to the reference VM, for humans and AI
 * agents alike. It uses ONLY src/arcade1 (the machine) — no Libretro, no
 * RetroArch, no GPU, no host input devices — so it runs identically in CI, in a
 * terminal, and inside an AI agent's sandbox.
 *
 * This file implements the `run` subcommand: load a game image, drive it for a
 * fixed number of frames with optional scripted input, print one machine-
 * readable hash line per frame, and optionally dump framebuffers as PPM images.
 * The `build`/`test`/`capture` subcommands (roadmap M28) wrap this later; the
 * argument surface is shaped so they slot in without redesign.
 *
 * Determinism contract (SDK.md): the same image + the same input script always
 * yield the same per-frame hashes and the same pixels. The state and
 * framebuffer hashes are the SAME FNV-1a fingerprints the golden regression
 * uses (tests/state_hash.h), so `arcade1 run` output is directly diffable
 * against the tests/golden game files.
 *
 * Usage:
 *   arcade1 run <game.arc1|bin> [options]
 *     --frames N           run N frames (default 600)
 *     --input FILE         scripted input (see INPUT SCRIPT FORMAT below)
 *     --capture FILE       write the final framebuffer to FILE as a PPM (P6)
 *     --capture-dir DIR    write frame_NNNN.ppm into DIR every --capture-every
 *     --capture-every K    frame stride for --capture-dir (default 1)
 *     --quiet              suppress the per-frame hash lines (summary only)
 *
 * Exit codes: 0 success, 1 runtime error (bad image, unreadable file, ...),
 * 2 usage error.
 *
 * INPUT SCRIPT FORMAT (plain text, deterministic, AI-friendly):
 *   - one run-length entry per line:  <first_frame> <last_frame> <buttons...>
 *   - frames are inclusive; entries may overlap and later lines win
 *   - buttons are words: up down left right a b start select
 *   - a frame not covered by any entry is "no buttons held"
 *   - blank lines and lines starting with '#' are ignored
 * Example:
 *   0   59  none
 *   60  119 right a
 */

#define _POSIX_C_SOURCE 200809L   /* strtok_r, under -std=c11 */

#include "machine.h"
#include "memory.h"
#include "rom.h"
#include "state_hash.h"   /* the golden state/fb fingerprints (via -Itests) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Input script --------------------------------------------------------- */

#define INPUT_MAX_ENTRIES 8192

typedef struct input_entry {
    unsigned       first, last;
    arcade1_input_t in;
} input_entry_t;

static input_entry_t g_entries[INPUT_MAX_ENTRIES];
static unsigned     g_nentries;

/* Parse one button word into `in`; returns 0 on unknown word, 1 on success. */
static int apply_button(arcade1_input_t *in, const char *w)
{
    if      (!strcmp(w, "up"))     in->up     = true;
    else if (!strcmp(w, "down"))   in->down   = true;
    else if (!strcmp(w, "left"))   in->left   = true;
    else if (!strcmp(w, "right"))  in->right  = true;
    else if (!strcmp(w, "a"))      in->a      = true;
    else if (!strcmp(w, "b"))      in->b      = true;
    else if (!strcmp(w, "start"))  in->start  = true;
    else if (!strcmp(w, "select")) in->select = true;
    else if (!strcmp(w, "none"))   { /* hold nothing */ }
    else return 0;
    return 1;
}

/* Load an input script. Returns 0 on success, -1 on error (message on stderr). */
static int load_input(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    unsigned lineno = 0;

    if (!f) {
        fprintf(stderr, "arcade1: cannot open input script '%s'\n", path);
        return -1;
    }
    g_nentries = 0;
    while (fgets(line, sizeof line, f)) {
        char *tok, *save = NULL;
        unsigned first, last;
        arcade1_input_t in;

        lineno++;
        /* Strip a trailing comment. */
        { char *hash = strchr(line, '#'); if (hash) *hash = '\0'; }

        tok = strtok_r(line, " \t\r\n", &save);
        if (!tok)
            continue;                       /* blank line */

        { char *end; first = (unsigned)strtoul(tok, &end, 10);
          if (end == tok) {
              fprintf(stderr, "arcade1: %s:%u: expected a frame number\n",
                      path, lineno);
              fclose(f); return -1;
          } }
        tok = strtok_r(NULL, " \t\r\n", &save);
        if (!tok) {
            fprintf(stderr, "arcade1: %s:%u: missing last frame\n", path, lineno);
            fclose(f); return -1;
        }
        { char *end; last = (unsigned)strtoul(tok, &end, 10);
          if (end == tok || last < first) {
              fprintf(stderr, "arcade1: %s:%u: bad frame range\n", path, lineno);
              fclose(f); return -1;
          } }

        if (g_nentries >= INPUT_MAX_ENTRIES) {
            fprintf(stderr, "arcade1: %s: too many entries (max %d)\n",
                    path, INPUT_MAX_ENTRIES);
            fclose(f); return -1;
        }
        memset(&in, 0, sizeof in);
        while ((tok = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
            if (!apply_button(&in, tok)) {
                fprintf(stderr, "arcade1: %s:%u: unknown button '%s'\n",
                        path, lineno, tok);
                fclose(f); return -1;
            }
        }
        g_entries[g_nentries].first = first;
        g_entries[g_nentries].last  = last;
        g_entries[g_nentries].in    = in;
        g_nentries++;
    }
    fclose(f);
    return 0;
}

/* The controller state for `frame`: no buttons, overridden by any entry that
 * covers the frame (later entries win). */
static void input_for_frame(unsigned frame, arcade1_input_t *out)
{
    memset(out, 0, sizeof *out);
    for (unsigned i = 0; i < g_nentries; i++)
        if (frame >= g_entries[i].first && frame <= g_entries[i].last)
            *out = g_entries[i].in;
}

/* --- PPM (P6) frame dump -------------------------------------------------- */

static int write_ppm(const char *path, const uint16_t *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "arcade1: cannot write '%s'\n", path);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", ARCADE1_WIDTH, ARCADE1_HEIGHT);
    for (unsigned i = 0; i < (unsigned)ARCADE1_FB_PIXELS; i++) {
        const uint16_t p = fb[i];
        fputc((int)(((p >> 11) & 31u) * 255u / 31u), f);   /* R */
        fputc((int)(((p >> 5) & 63u) * 255u / 63u), f);    /* G */
        fputc((int)((p & 31u) * 255u / 31u), f);           /* B */
    }
    fclose(f);
    return 0;
}

/* --- run subcommand ------------------------------------------------------- */

static int cmd_run(int argc, char **argv)
{
    const char *rom_path = NULL, *input_path = NULL;
    const char *capture = NULL, *capture_dir = NULL;
    unsigned frames = 600, capture_every = 1;
    int quiet = 0;
    static uint8_t img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
    static arcade1_machine_t m;
    size_t n;
    FILE *f;
    const void *rom;
    size_t rom_size;
    arcade1_input_t in;

    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--frames") && i + 1 < argc)        frames = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--input") && i + 1 < argc)    input_path = argv[++i];
        else if (!strcmp(a, "--capture") && i + 1 < argc)  capture = argv[++i];
        else if (!strcmp(a, "--capture-dir") && i + 1 < argc) capture_dir = argv[++i];
        else if (!strcmp(a, "--capture-every") && i + 1 < argc) capture_every = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--quiet"))                    quiet = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "arcade1 run: unknown option '%s'\n", a);
            return 2;
        } else if (!rom_path) {
            rom_path = a;
        } else {
            fprintf(stderr, "arcade1 run: unexpected argument '%s'\n", a);
            return 2;
        }
    }
    if (!rom_path || frames == 0 || capture_every == 0) {
        fprintf(stderr,
                "usage: arcade1 run <game.arc1|bin> [--frames N] [--input FILE]\n"
                "                   [--capture FILE] [--capture-dir DIR]\n"
                "                   [--capture-every K] [--quiet]\n");
        return 2;
    }

    /* Load the image (accept a raw flat binary too, like the tests). */
    f = fopen(rom_path, "rb");
    if (!f) {
        fprintf(stderr, "arcade1: cannot open '%s'\n", rom_path);
        return 1;
    }
    n = fread(img, 1, sizeof img, f);
    fclose(f);
    if (n == 0 || n > sizeof img) {
        fprintf(stderr, "arcade1: '%s' is empty or too large\n", rom_path);
        return 1;
    }
    rom = img;
    rom_size = n;
    if (arcade1_image_recognized(img, n)) {
        arcade1_image_t parsed;
        const arcade1_image_err_t err = arcade1_image_parse(img, n, &parsed);
        if (err != ARCADE1_IMAGE_OK) {
            fprintf(stderr, "arcade1: '%s': %s\n", rom_path,
                    arcade1_image_strerror(err));
            return 1;
        }
        rom = parsed.payload;
        rom_size = parsed.payload_size;
    }

    if (input_path && load_input(input_path) != 0)
        return 1;

    arcade1_init(&m);
    arcade1_reset(&m);
    if (arcade1_load_rom(&m.bus, rom, (uint32_t)rom_size) != rom_size) {
        fprintf(stderr, "arcade1: ROM did not load\n");
        return 1;
    }
    arcade1_set_firmware(&m, true);

    for (unsigned fr = 0; fr < frames; fr++) {
        input_for_frame(fr, &in);
        arcade1_set_input(&m, &in);
        arcade1_run_frame(&m);
        if (!quiet)
            printf("frame %4u hash=0x%016llX fb=0x%016llX\n", fr,
                   (unsigned long long)state_hash(&m.cpu, &m.bus),
                   (unsigned long long)fb_hash(&m));
        if (capture_dir && fr % capture_every == 0) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame_%04u.ppm", capture_dir, fr);
            if (write_ppm(path, arcade1_framebuffer(&m)) != 0)
                return 1;
        }
    }

    if (capture && write_ppm(capture, arcade1_framebuffer(&m)) != 0)
        return 1;

    printf("ran %u frames  final state hash=0x%016llX  fb hash=0x%016llX\n",
           frames,
           (unsigned long long)state_hash(&m.cpu, &m.bus),
           (unsigned long long)fb_hash(&m));
    return 0;
}

/* --- entry point ---------------------------------------------------------- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "arcade1 — ARCADE-1 developer CLI (M28 foundation)\n"
                "usage: arcade1 <command> [args]\n"
                "commands:\n"
                "  run    run a game headlessly with scripted input, hashes, dumps\n");
        return 2;
    }
    if (!strcmp(argv[1], "run"))
        return cmd_run(argc - 2, argv + 2);

    fprintf(stderr, "arcade1: unknown command '%s'\n", argv[1]);
    return 2;
}
