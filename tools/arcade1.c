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
 *     --syms FILE          load an `nm` symbol dump (<game>.syms); omitted
 *                          with --vars it auto-loads <image>.syms
 *     --vars LIST          print named game variables (comma list, or "all")
 *     --var-every K        also print the selected variables every K frames
 *     --record FILE         write the applied per-frame buttons to FILE, in the
 *                           input script format (run-length, "none" = idle)
 *     --quiet              suppress the per-frame hash lines (summary only)
 *
 * Exit codes: 0 success, 1 runtime error (bad image, unreadable file,
 * unknown/non-RAM variable, ...), 2 usage error — also --vars with no
 * sidecar .syms, or a .syms whose build CRC mismatches the image.
 *
 * NAMED GAME VARIABLES (--vars): reads a game's globals out of guest RAM by
 * name, from an `nm` dump (the same <game>.syms the game tests use). Only
 * RAM-resident data/bss symbols count as variables; each is read as one 32-bit
 * word (the guest's `int`) and printed signed — exactly how the tests read
 * score/lives/state. Final values print as `var <name> = <value>`; with
 * --var-every K a `vars <frame> <name>=<value> ...` trace line prints every K
 * frames.
 *
 * RECORD (--record): the write side of the replay primitive. Every frame's
 * applied button state (what --input resolved to, or all-idle without it) is
 * run-length encoded to FILE in the input script format above — consecutive
 * frames in the same state merge into one entry, idle stretches record as
 * "none". A recording feeds straight back through --input, so a recorded
 * session replays bit-identically (this is what make test-run asserts).
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
#include "sdk_data.h"    /* the embedded guest SDK (via -Itools) */
#include "state_hash.h"   /* the golden state/fb fingerprints (via -Itests) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       /* fork, execvp, unlink, rmdir, mkdtemp, access */
#include <fcntl.h>        /* open: capturing nm output in cmd_build */
#include <sys/stat.h>     /* mkdir: the capture subcommand's directory */
#include <errno.h>
#include <sys/wait.h>     /* waitpid */

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

/* --- Record (write side of the replay primitive) ----------------------------
 * Run-length encode the applied per-frame button state to an open FILE in the
 * input script format: one "<first> <last> <buttons...>" line per run of
 * identical state, "none" for an idle run. */

static FILE             *g_record;
static arcade1_input_t   g_run_state;   /* button state of the open run    */
static unsigned          g_run_start;   /* first frame of the open run     */
static int               g_run_open;    /* a run is being accumulated      */

/* Write one run [first, last] with the given button state. */
static void record_write_run(unsigned first, unsigned last,
                             const arcade1_input_t *in)
{
    int any = 0;

    fprintf(g_record, "%u %u", first, last);
    if (in->up)     { fprintf(g_record, " up");     any = 1; }
    if (in->down)   { fprintf(g_record, " down");   any = 1; }
    if (in->left)   { fprintf(g_record, " left");   any = 1; }
    if (in->right)  { fprintf(g_record, " right");  any = 1; }
    if (in->a)      { fprintf(g_record, " a");      any = 1; }
    if (in->b)      { fprintf(g_record, " b");      any = 1; }
    if (in->start)  { fprintf(g_record, " start");  any = 1; }
    if (in->select) { fprintf(g_record, " select"); any = 1; }
    if (!any)         fprintf(g_record, " none");
    fprintf(g_record, "\n");
}

/* Open the record file and write its one-line header. Call only after the
 * frame count is known. Returns 0 ok, -1 error (message on stderr). */
static int record_open(const char *path, unsigned frames)
{
    g_record = fopen(path, "w");
    if (!g_record) {
        fprintf(stderr, "arcade1: cannot write record file '%s'\n", path);
        return -1;
    }
    fprintf(g_record, "# arcade1 run --record: %u frames\n", frames);
    g_run_open = 0;
    return 0;
}

/* Note the applied button state for one frame, closing the open run when the
 * state changes. */
static void record_frame(unsigned frame, const arcade1_input_t *in)
{
    if (!g_record)
        return;
    if (g_run_open && memcmp(in, &g_run_state, sizeof *in) == 0)
        return;                             /* still the same run          */
    if (g_run_open)
        record_write_run(g_run_start, frame - 1, &g_run_state);
    g_run_state = *in;
    g_run_start = frame;
    g_run_open  = 1;
}

/* Close the final run and the file. Safe to call more than once. */
static void record_close(unsigned last_frame)
{
    if (!g_record)
        return;
    if (g_run_open)
        record_write_run(g_run_start, last_frame, &g_run_state);
    fclose(g_record);
    g_record = NULL;
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

/* --- Named game variables --------------------------------------------------
 * "What does the game believe is happening?" (AI_NATIVE_DEVELOPMENT.md): read a
 * game's globals out of guest RAM by name, from an `nm` symbol dump (the same
 * <game>.syms the game tests use). Only RAM-resident data/bss symbols are
 * variables; each is read as one 32-bit word (the guest's `int`, cast to signed
 * — exactly how test-breakout etc. read score/lives/state). */

#define SYM_MAX 8192
#define VAR_MAX 1024

typedef struct symbol {
    uint32_t addr;
    char     type;                 /* nm type letter: B/b/D/d = RAM data/bss   */
    char     name[64];
} symbol_t;

static symbol_t        g_syms[SYM_MAX];
static unsigned        g_nsyms;
static const symbol_t *g_vars[VAR_MAX];    /* the resolved --vars selection    */
static unsigned        g_nvars;

/* The sidecar symbol file for an image: game.arc1 -> game.syms (anything
 * else gets ".syms" appended). Written by `arcade1 build`, auto-loaded by
 * `run`/`test`/`inspect` for --vars when --syms is omitted. */
static void syms_path_for_image(const char *img, char *buf, size_t buflen)
{
    size_t len = strlen(img);

    if (len > 5 && !strcmp(img + len - 5, ".arc1"))
        snprintf(buf, buflen, "%.*s.syms", (int)(len - 5), img);
    else
        snprintf(buf, buflen, "%s.syms", img);
}

/* Payload CRC from a build-written header line, when the file has one. */
static uint32_t g_syms_crc;
static int      g_syms_have_crc;

/* Parse an `nm` dump: "<hexaddr> <type> <name>" per line. Blank lines and nm
 * section headers (which don't match 3 fields) are skipped. A file written by
 * `arcade1 build` starts with "# arc1-syms crc32=0x<image payload CRC>",
 * remembered for the against-the-image check. Names outside the RAM window
 * are dropped with one summary warning (only hand-edited dumps hit this; a
 * build-written file is RAM-only by construction). 0 ok, -1 error. */
static int load_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    unsigned dropped = 0;

    if (!f) {
        fprintf(stderr, "arcade1: cannot open symbol dump '%s'\n", path);
        return -1;
    }
    g_nsyms = 0;
    g_syms_crc = 0;
    g_syms_have_crc = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned addr;
        char type, name[64];

        if (sscanf(line, "# arc1-syms crc32=%x", &addr) == 1) {
            g_syms_crc = (uint32_t)addr;
            g_syms_have_crc = 1;
            continue;
        }
        if (sscanf(line, "%x %c %63s", &addr, &type, name) != 3)
            continue;                        /* blank line or nm header */
        if (addr < ARCADE1_RAM_BASE ||
            addr >= ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE) {
            dropped++;                       /* code / linker marks */
            continue;
        }
        if (g_nsyms >= SYM_MAX) {
            fprintf(stderr, "arcade1: %s: too many symbols (max %d)\n",
                    path, SYM_MAX);
            fclose(f); return -1;
        }
        g_syms[g_nsyms].addr = (uint32_t)addr;
        g_syms[g_nsyms].type = type;
        snprintf(g_syms[g_nsyms].name, sizeof g_syms[g_nsyms].name, "%s", name);
        g_nsyms++;
    }
    fclose(f);
    if (dropped)
        fprintf(stderr, "arcade1: %s: dropped %u symbols outside the RAM window\n",
                path, dropped);
    if (g_nsyms == 0) {
        fprintf(stderr, "arcade1: %s: no symbols found\n", path);
        return -1;
    }
    return 0;
}

static const symbol_t *find_sym(const char *name)
{
    for (unsigned i = 0; i < g_nsyms; i++)
        if (!strcmp(g_syms[i].name, name))
            return &g_syms[i];
    return NULL;
}

/* A RAM-resident data/bss symbol (a game variable), not code or a linker mark. */
static int sym_is_ram_var(const symbol_t *s)
{
    if (s->addr < ARCADE1_RAM_BASE ||
        s->addr + 4u > ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE)
        return 0;
    return s->type == 'B' || s->type == 'b' || s->type == 'D' || s->type == 'd';
}

/* Resolve the --vars selection ("all", or a comma list) into g_vars.
 * 0 ok, -1 error (message on stderr). */
static int resolve_vars(const char *list)
{
    g_nvars = 0;

    if (!strcmp(list, "all")) {
        for (unsigned i = 0; i < g_nsyms && g_nvars < VAR_MAX; i++) {
            const symbol_t *s = &g_syms[i];
            if (s->name[0] == '_')            /* linker internals (_sbss, ...) */
                continue;
            if (sym_is_ram_var(s))
                g_vars[g_nvars++] = s;
        }
        return g_nvars ? 0 : -1;
    }

    { char buf[1024], *tok, *save = NULL;
      if (strlen(list) >= sizeof buf) {
          fprintf(stderr, "arcade1: --vars list too long\n");
          return -1;
      }
      snprintf(buf, sizeof buf, "%s", list);
      for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
          const symbol_t *s = find_sym(tok);
          if (!s) {
              fprintf(stderr, "arcade1: no symbol named '%s'\n", tok);
              return -1;
          }
          if (!sym_is_ram_var(s)) {
              fprintf(stderr, "arcade1: '%s' is not a RAM variable "
                              "(type '%c' @ 0x%08x)\n", tok, s->type, s->addr);
              return -1;
          }
          if (g_nvars >= VAR_MAX) {
              fprintf(stderr, "arcade1: too many variables (max %d)\n", VAR_MAX);
              return -1;
          }
          g_vars[g_nvars++] = s;
      } }

    return g_nvars ? 0 : -1;
}

/* --- run subcommand ------------------------------------------------------- */

static int cmd_run(int argc, char **argv)
{
    const char *rom_path = NULL, *input_path = NULL;
    const char *capture = NULL, *capture_dir = NULL;
    const char *record_path = NULL;
    const char *syms_path = NULL, *var_list = NULL;
    unsigned frames = 600, capture_every = 1, var_every = 0;
    int inspect = 0;    int quiet = 0;    int strict = 0;
    unsigned long long skipped = 0;   /* M29: out-of-window source reads */
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
        else if (!strcmp(a, "--record") && i + 1 < argc)    record_path = argv[++i];
        else if (!strcmp(a, "--syms") && i + 1 < argc)     syms_path = argv[++i];
        else if (!strcmp(a, "--vars") && i + 1 < argc)     var_list = argv[++i];
        else if (!strcmp(a, "--var-every") && i + 1 < argc) var_every = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--inspect")) inspect = 1;
        else if (!strcmp(a, "--quiet"))                    quiet = 1;
        else if (!strcmp(a, "--strict"))                   strict = 1;
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
    if (!rom_path || frames == 0 || capture_every == 0 ||
        (var_every && !var_list)) {
        fprintf(stderr,
                "usage: arcade1 run <game.arc1|bin> [--frames N] [--input FILE]\n"
                "                   [--capture FILE] [--capture-dir DIR]\n"
                "                   [--capture-every K] [--record FILE]\n"
                "                   [--syms FILE] [--vars LIST] [--var-every K]\n"
                "                   [--quiet] [--strict]\n"
                "  --strict exits non-zero if any draw read a source address\n"
                "  outside ROM/RAM (they silently draw nothing); see below.\n"
                "  --vars LIST is a comma list of RAM globals, or \"all\";\n"
                "  without --syms it auto-loads <image>.syms ('arcade1 build'\n"
                "  writes it); --var-every requires --vars.\n"
                "  --record writes the applied buttons in the script format.\n");
        return 2;
    }

    /* --vars without --syms: auto-load the sidecar `arcade1 build` writes
     * beside the image (game.arc1 -> game.syms). No sidecar is a clear,
     * teachable error, not a usage error (ROADMAP_V3 card A1). */
    if (var_list && !syms_path) {
        static char side[1024];

        syms_path_for_image(rom_path, side, sizeof side);
        if (access(side, R_OK) != 0) {
            fprintf(stderr,
                    "arcade1: no symbols for %s: build with 'arcade1 build' "
                    "(writes game.syms) or pass --syms FILE\n", rom_path);
            return 2;
        }
        syms_path = side;
    }

    /* Fail fast on an unwritable record file (before running anything). */
    if (record_path && record_open(record_path, frames) != 0)
        return 1;

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
    int      img_have_crc = 0;
    uint32_t img_crc = 0;

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
        img_have_crc = 1;                   /* header bytes 12..15, LE */
        img_crc = (uint32_t)img[12] | (uint32_t)img[13] << 8 |
                  (uint32_t)img[14] << 16 | (uint32_t)img[15] << 24;
    }

    if (input_path && load_input(input_path) != 0)
        return 1;
    if (syms_path && load_syms(syms_path) != 0)
        return 1;
    if (syms_path && g_syms_have_crc && img_have_crc && g_syms_crc != img_crc) {
        fprintf(stderr,
                "arcade1: syms file belongs to a different build "
                "(syms crc 0x%08x vs image 0x%08x); "
                "rebuild or pass the matching file\n", g_syms_crc, img_crc);
        return 2;
    }
    if (var_list && resolve_vars(var_list) != 0)
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
        record_frame(fr, &in);
        arcade1_run_frame(&m);
        skipped += arcade1_video_frame_totals(&m.video)->skipped_ops;
        if (!quiet)
            printf("frame %4u hash=0x%016llX fb=0x%016llX\n", fr,
                   (unsigned long long)state_hash(&m.cpu, &m.bus),
                   (unsigned long long)fb_hash(&m));
        if (var_every && var_list && fr % var_every == 0) {
            printf("vars %4u", fr);
            for (unsigned v = 0; v < g_nvars; v++)
                printf(" %s=%d", g_vars[v]->name,
                       (int)(int32_t)arcade1_read32(&m.bus, g_vars[v]->addr));
            printf("\n");
        }
        if (capture_dir && fr % capture_every == 0) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame_%04u.ppm", capture_dir, fr);
            if (write_ppm(path, arcade1_framebuffer(&m)) != 0) {
                record_close(fr);
                return 1;
            }
        }
    }

    record_close(frames - 1);

    if (capture && write_ppm(capture, arcade1_framebuffer(&m)) != 0)
        return 1;

    for (unsigned v = 0; v < g_nvars; v++)
        printf("var %s = %d\n", g_vars[v]->name,
               (int)(int32_t)arcade1_read32(&m.bus, g_vars[v]->addr));

    if (inspect) {
        printf("inspect: frame=%u pc=0x%08X instret=%llu\n",
               m.frame_counter, m.cpu.pc, (unsigned long long)m.cpu.instret);
        for (int r = 0; r < 32; r++)
            printf("  x%-2d = 0x%08X\n", r, m.cpu.regs[r]);
    }    printf("ran %u frames  final state hash=0x%016llX  fb hash=0x%016llX\n",           frames,
           (unsigned long long)state_hash(&m.cpu, &m.bus),
           (unsigned long long)fb_hash(&m));

    /* M29 diagnostics: the device reads guest memory through the integer
     * addresses the guest hands it; anything outside ROM/RAM reads back as
     * zero, so those draws painted nothing and said nothing. One line, to
     * stderr, so it never disturbs the machine-readable stdout. */
    if (skipped != 0) {
        fprintf(stderr,
                "arcade1 run: warning: %llu draws read source addresses "
                "outside ROM/RAM and drew nothing%s\n",
                skipped, strict ? "" : " (--strict to fail on this)");
        if (strict)
            return 1;
    }
    return 0;
}

/* --- entry point ---------------------------------------------------------- */


/* --- capture subcommand: run + dump frames --------------------------------
 * `arcade1 run` only writes images when it is told where (so a capture that
 * captures nothing was easy to get). This wrapper always captures: frames
 * land in captures/ unless --capture or --capture-dir says otherwise. */
static int cmd_capture(int argc, char **argv)
{
    int has_capture_file = 0;
    const char *dir = "captures";

    if (argc < 1) {
        fprintf(stderr,
                "usage: arcade1 capture <game.arc1|bin> [arcade1 run options]\n"
                "  runs the game and dumps frames; default destination is\n"
                "  captures/frame_NNNN.ppm (override with --capture-dir DIR,\n"
                "  or a single final frame with --capture FILE).\n");
        return 2;
    }
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--capture"))
            has_capture_file = 1;
        if (!strcmp(argv[i], "--capture-dir") && i + 1 < argc)
            dir = argv[i + 1];
    }
    /* A capture command creates its own destination — asking the caller to
     * mkdir first is how "capture" ended up capturing nothing. */
    if (!has_capture_file && mkdir(dir, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "arcade1 capture: cannot create '%s'\n", dir);
        return 1;
    }
    /* A single --capture FILE needs nothing from us: run writes it. */
    if (has_capture_file)
        return cmd_run(argc, argv);

    /* An explicit --capture-dir likewise — run honours it, we only mkdir'd. */
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--capture-dir"))
            return cmd_run(argc, argv);

    char **nargv = calloc((size_t)argc + 2, sizeof *nargv);
    if (!nargv)
        return 1;
    for (int i = 0; i < argc; i++)
        nargv[i] = argv[i];
    nargv[argc]     = (char *)"--capture-dir";
    nargv[argc + 1] = (char *)dir;
    int rc = cmd_run(argc + 2, nargv);
    free(nargv);
    return rc;
}

/* --- test subcommand: smoke run, optional golden compare ------------------
 * `arcade1 test <game>` runs it headlessly and propagates the exit status
 * (a smoke test: the image loads, the guest runs, nothing blows up).
 *
 * `arcade1 test <game> --golden FILE` additionally compares the per-frame
 * `frame N hash=0x.. fb=0x..` lines against FILE — the same layout the
 * golden files under tests/golden use — and fails if they differ. A state hash
 * folds in the guest's instruction counter, so a golden pins an exact image:
 * check a freshly rebuilt guest against the ROM the golden was made from. */
static int golden_compare(const char *run_path, const char *golden_path)
{
    FILE *a = fopen(run_path, "r");
    FILE *b = fopen(golden_path, "r");
    char  la[512], lb[512];
    unsigned total = 0, n = 0, mismatch = 0;
    unsigned fn, prev = 0;
    int have_prev = 0, run_out = 0;

    if (!a || !b) {
        fprintf(stderr, "arcade1 test: cannot read %s\n",
                a ? golden_path : run_path);
        if (a) fclose(a);
        if (b) fclose(b);
        return 1;
    }

    /* Pass 1 of the golden: `frame 0`, `frame 1`, ... — stop at the first
     * break in that sequence. A golden file also carries the reset-and-
     * replay pass (which starts at frame 0 again) and one `arcade1 test`
     * run cannot reproduce it; tests/test_run.sh compares the same first
     * pass for the same reason. */
    while (fgets(lb, sizeof lb, b)) {
        if (strncmp(lb, "frame ", 6) != 0)
            continue;
        if (sscanf(lb, "frame %u", &fn) != 1 || (have_prev && fn != prev + 1))
            break;
        have_prev = 1;
        prev = fn;
        total++;
    }
    rewind(b);
    if (total == 0) {
        fprintf(stderr, "arcade1 test: %s has no frame lines to compare\n",
                golden_path);
        fclose(a);
        fclose(b);
        return 1;
    }

    /* Walk the golden's first pass, pulling one frame line per step from
     * the captured run output. */
    while (n < total && fgets(lb, sizeof lb, b)) {
        if (strncmp(lb, "frame ", 6) != 0)
            continue;
        do {
            if (!fgets(la, sizeof la, a)) { run_out = 1; break; }
        } while (strncmp(la, "frame ", 6) != 0);
        if (run_out)
            break;
        n++;
        if (strcmp(la, lb) != 0 && !mismatch) {
            mismatch = n;
            fprintf(stderr,
                    "arcade1 test: frame line %u differs\n"
                    "  ran     %s"
                    "  golden  %s", n, la, lb);
        }
    }
    fclose(a);
    fclose(b);

    if (mismatch)
        return 1;
    if (n == 0) {
        fprintf(stderr, "arcade1 test: no frame lines compared against %s\n",
                golden_path);
        return 1;
    }
    if (n < total)
        printf("arcade1 test: %u of %u golden frame lines match %s\n"
               "  (the run was shorter than the golden's first pass; "
               "--frames %u covers it)\n", n, total, golden_path, total);
    else
        printf("arcade1 test: %u frame lines match %s\n", n, golden_path);
    return 0;
}

static int cmd_test(int argc, char **argv)
{
    const char *golden = NULL;
    char      **rest;
    int         nrest = 0, rc;

    if (argc < 1) {
        fprintf(stderr,
                "usage: arcade1 test <game.arc1|bin> [arcade1 run options]\n"
                "       arcade1 test <game.arc1|bin> --golden FILE [options]\n"
                "  smoke: runs the game and exits non-zero on any failure.\n"
                "  --golden FILE compares the per-frame hash lines against\n"
                "  FILE (the golden files' frame lines, first pass) and\n"
                "  fails on a difference. --quiet is ignored in that mode.\n");
        return 2;
    }

    rest = calloc((size_t)argc, sizeof *rest);
    if (!rest)
        return 1;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--golden") && i + 1 < argc) {
            golden = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--quiet"))
            continue;                       /* the hash lines are the point */
        rest[nrest++] = argv[i];
    }

    if (!golden) {
        rc = cmd_run(nrest, rest);
        free(rest);
        return rc;
    }

    /* Run with stdout captured, then diff it against the golden. */
    char tmpl[] = "/tmp/arcade1-test-XXXXXX";
    int  saved = -1, fd = mkstemp(tmpl);

    if (fd < 0) {
        fprintf(stderr, "arcade1 test: cannot create a temp file\n");
        free(rest);
        return 1;
    }
    fflush(stdout);
    saved = dup(1);
    if (saved < 0 || dup2(fd, 1) < 0) {
        close(fd);
        free(rest);
        return 1;
    }
    close(fd);

    rc = cmd_run(nrest, rest);
    free(rest);

    fflush(stdout);
    dup2(saved, 1);
    close(saved);

    if (rc != 0) {
        unlink(tmpl);
        return rc;
    }
    rc = golden_compare(tmpl, golden);
    unlink(tmpl);
    return rc;
}

/* --- build subcommand: compile a guest C program into a .arc1 image ------ */
/* --- build: guest C program -> .arc1 image --------------------------------
 *
 * Self-contained by design. The guest SDK (crt0.S, link.ld, arcade1.h,
 * gfx.h, arcade1_map.h) is compiled into this binary as string literals
 * (tools/sdk_data.c, generated from programs/ by the Makefile), so
 * `arcade1 build` works in any directory with no checkout, no build/ tree
 * and no packarc1 helper beside it. The toolchain runs in a private temp
 * directory that is removed again on the way out; the only file left
 * behind is the image you asked for.
 *
 * The one thing it cannot carry is riscv64-unknown-elf-gcc (and objcopy),
 * which must be on PATH.
 */

/* Write a whole file from memory. 0 on success. */
static int write_file(const char *path, const char *contents)
{
    FILE  *f = fopen(path, "wb");
    size_t n = strlen(contents);

    if (!f)
        return -1;
    if (fwrite(contents, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

/* Run one command without a shell: no quoting pitfalls, and a missing
 * tool shows up as exit 127 rather than as a shell error string. */
static int run_cmd(char *const argv[])
{
    pid_t pid = fork();
    int   status = 0;

    if (pid < 0)
        return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* Run one command with stdout redirected to `out_path`. Returns the exit
 * status — 127 when the tool could not be exec'd at all (it isn't on PATH),
 * -1 on fork/wait trouble. */
static int run_cmd_capture(char *const argv[], const char *out_path)
{
    pid_t pid = fork();
    int   status = 0;

    if (pid < 0)
        return -1;
    if (pid == 0) {
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || dup2(fd, 1) < 0)
            _exit(126);
        close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

/* Write <image>.syms from an nm dump: the build header line first, then the
 * RAM-window names in nm's own "<hexaddr> <type> <name>" format. Warnings on
 * stderr, never a failure — the image is already built (ROADMAP_V3 card A1).
 * Returns 1 when the file was written, 0 when it was not. */
static int write_syms_file(const char *nmout_path, const char *syms_path,
                           uint32_t img_crc)
{
    FILE    *in = fopen(nmout_path, "r");
    FILE    *outf;
    char     line[512];
    unsigned kept = 0;

    if (!in) {
        fprintf(stderr, "arcade1: warning: cannot read nm output, "
                        "no .syms written\n");
        return 0;
    }
    outf = fopen(syms_path, "w");
    if (!outf) {
        fprintf(stderr, "arcade1: warning: cannot write %s, no .syms written\n",
                syms_path);
        fclose(in);
        return 0;
    }
    fprintf(outf, "# arc1-syms crc32=0x%08x\n", img_crc);
    while (fgets(line, sizeof line, in)) {
        unsigned addr;
        char     type, name[64];

        if (sscanf(line, "%x %c %63s", &addr, &type, name) != 3)
            continue;
        if (addr < ARCADE1_RAM_BASE ||
            addr >= ARCADE1_RAM_BASE + ARCADE1_RAM_SIZE)
            continue;                        /* code / linker marks */
        fprintf(outf, "%08x %c %s\n", addr, type, name);
        kept++;
    }
    fclose(in);
    fclose(outf);
    if (kept == 0) {
        unlink(syms_path);
        fprintf(stderr, "arcade1: warning: nm reported no RAM symbols, "
                        "no .syms written\n");
        return 0;
    }
    return 1;
}

/* Pack a flat guest binary into an .arc1 image, in process. Uses the same
 * header writer and CRC as the VM itself (src/arcade1/rom.c), so the CLI
 * cannot produce an image the machine would reject. 0 on success; on success
 * stores the payload CRC in *crc_out (when non-NULL) — the value the
 * <output>.syms header records. */
static int pack_image(const char *bin_path, const char *out_path,
                      uint32_t *crc_out)
{
    static uint8_t image[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
    FILE          *f;
    size_t         n;
    int            extra;

    f = fopen(bin_path, "rb");
    if (!f) {
        fprintf(stderr, "arcade1: cannot read %s\n", bin_path);
        return -1;
    }
    n = fread(image + ARCADE1_IMAGE_HEADER_SIZE, 1, ARCADE1_ROM_SIZE, f);
    extra = fgetc(f) != EOF;          /* one byte past the ROM: too big */
    fclose(f);
    if (n == 0 || extra) {
        fprintf(stderr, "arcade1: %s is not a flat ROM of 1..%u bytes\n",
                bin_path, ARCADE1_ROM_SIZE);
        return -1;
    }
    {
        const uint32_t crc =
            arcade1_crc32(image + ARCADE1_IMAGE_HEADER_SIZE, n);
        arcade1_image_header(image, (uint32_t)n, crc);
        if (crc_out)
            *crc_out = crc;
    }

    f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "arcade1: cannot write %s\n", out_path);
        return -1;
    }
    if (fwrite(image, 1, ARCADE1_IMAGE_HEADER_SIZE + n, f) != ARCADE1_IMAGE_HEADER_SIZE + n) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

/* Remove the temp directory and everything in it. Best effort: a leftover
 * /tmp dir is untidy, never wrong. */
static void cleanup_sdk_dir(const char *dir, char *const files[], size_t nfiles)
{
    char path[1024];

    for (size_t i = 0; i < nfiles; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        unlink(path);
    }
    rmdir(dir);
}

static int cmd_build(int argc, char **argv)
{
    /* The SDK files, in the order sdk_data.h declares them. */
    static char *const sdk_names[] = {
        (char *)"crt0.S", (char *)"link.ld", (char *)"arcade1.h",
        (char *)"gfx.h", (char *)"arcade1_map.h"
    };
    static char *const sdk_bodies[] = {
        (char *)sdk_crt0_S, (char *)sdk_link_ld, (char *)sdk_arcade1_h,
        (char *)sdk_gfx_h, (char *)sdk_arcade1_map_h
    };
    const size_t nsdk = sizeof sdk_names / sizeof sdk_names[0];

    if (argc < 1) {
        fprintf(stderr,
                "usage: arcade1 build <program.c> [output.arc1]\n"
                "  output must end in .arc1; the default is <source stem>.arc1\n");
        return 2;
    }
    const char *src = argv[0];
    char        defout[1024];
    const char *out;

    if (argc >= 2) {
        const size_t olen = strlen(argv[1]);

        out = argv[1];
        if (olen < 5 || strcmp(out + olen - 5, ".arc1") != 0) {
            fprintf(stderr, "arcade1: output must end in .arc1 (got '%s')\n",
                    out);
            return 2;
        }
    } else {
        /* <source stem>.arc1 in the current directory (ROADMAP_V3 A2):
         * game.c -> game.arc1, dir/game.c -> game.arc1 here. The stem
         * buffer leaves room for the ".arc1" suffix (-Wformat-truncation). */
        const char *base = strrchr(src, '/');
        char        stem[1024 - 5];
        char       *dot;

        base = base ? base + 1 : src;
        snprintf(stem, sizeof stem, "%s", base);
        dot = strrchr(stem, '.');
        if (dot && dot != stem)
            *dot = '\0';
        snprintf(defout, sizeof defout, "%s.arc1", stem);
        out = defout;
    }

    /* Scratch directory: $TMPDIR if set, else /tmp. */
    char dir[1024];
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    snprintf(dir, sizeof dir, "%s/arcade1-sdk-XXXXXX", tmp);
    if (!mkdtemp(dir)) {
        fprintf(stderr, "arcade1: cannot create a temp directory under %s\n", tmp);
        return 1;
    }

    char elf[1200], bin[1200], crt0[1200], ld[1200], incdir[1200];
    snprintf(elf,    sizeof elf,    "%s/program.elf", dir);
    snprintf(bin,    sizeof bin,    "%s/program.bin", dir);
    snprintf(crt0,   sizeof crt0,   "%s/crt0.S", dir);
    snprintf(ld,     sizeof ld,     "%s/link.ld", dir);
    snprintf(incdir, sizeof incdir, "-I%s", dir);

    for (size_t i = 0; i < nsdk; i++) {
        char path[1200];
        snprintf(path, sizeof path, "%s/%s", dir, sdk_names[i]);
        if (write_file(path, sdk_bodies[i]) != 0) {
            fprintf(stderr, "arcade1: cannot write %s\n", path);
            cleanup_sdk_dir(dir, sdk_names, nsdk);
            return 1;
        }
    }

    /* Same flags `make program` uses for guests, plus -I so the guest's
     * own #include "arcade1.h" resolves to the temp copy. */
    char *const gcc_argv[] = {
        (char *)"riscv64-unknown-elf-gcc",
        (char *)"-march=rv32im", (char *)"-mabi=ilp32", (char *)"-Os",
        (char *)"-ffreestanding", (char *)"-nostdlib", (char *)"-nostartfiles",
        (char *)"-msmall-data-limit=0",
        incdir, (char *)"-T", ld, (char *)"-o", elf, crt0, (char *)src, NULL
    };
    if (run_cmd(gcc_argv) != 0) {
        fprintf(stderr, "arcade1: riscv64-unknown-elf-gcc failed "
                        "(is a RISC-V toolchain on PATH?)\n");
        cleanup_sdk_dir(dir, sdk_names, nsdk);
        return 1;
    }

    char *const objcopy_argv[] = {
        (char *)"riscv64-unknown-elf-objcopy",
        (char *)"-O", (char *)"binary", elf, bin, NULL
    };
    if (run_cmd(objcopy_argv) != 0) {
        fprintf(stderr, "arcade1: riscv64-unknown-elf-objcopy failed\n");
        cleanup_sdk_dir(dir, sdk_names, nsdk);
        return 1;
    }

    uint32_t img_crc = 0;
    int      rc = pack_image(bin, out, &img_crc) == 0 ? 0 : 1;

    /* Symbols beside the image (ROADMAP_V3 card A1): the same toolchain's
     * nm, filtered to the RAM window, headered with the image's payload CRC
     * so a later run can reject a .syms from a different build. A missing or
     * failing nm never fails the build — it warns. */
    char syms[1200], nmout[1200];
    int  wrote_syms = 0;

    syms_path_for_image(out, syms, sizeof syms);
    snprintf(nmout, sizeof nmout, "%s/nm.out", dir);
    {
        char *const nm_argv[] = { (char *)"riscv64-unknown-elf-nm", elf, NULL };
        const int   nm_rc = run_cmd_capture(nm_argv, nmout);

        if (nm_rc == 127)
            fprintf(stderr, "arcade1: warning: nm not found, no .syms written\n");
        else if (nm_rc != 0)
            fprintf(stderr, "arcade1: warning: nm failed, no .syms written\n");
        else if (rc == 0)
            wrote_syms = write_syms_file(nmout, syms, img_crc);
    }

    /* elf + bin + nm output on top of the SDK files. */
    char *tmpfiles[8];
    for (size_t i = 0; i < nsdk; i++)
        tmpfiles[i] = sdk_names[i];
    tmpfiles[nsdk]     = (char *)"program.elf";
    tmpfiles[nsdk + 1] = (char *)"program.bin";
    tmpfiles[nsdk + 2] = (char *)"nm.out";
    cleanup_sdk_dir(dir, tmpfiles, nsdk + 3);

    /* The final line reports every file written (ROADMAP_V3 A2). */
    if (rc == 0) {
        if (wrote_syms)
            printf("arcade1: built %s (+ %s)\n", out, syms);
        else
            printf("arcade1: built %s\n", out);
    }
    return rc;
}


static int cmd_inspect(int argc, char **argv)
{
    /* Build argv with --inspect appended. */
    char **nargv = calloc(argc + 1, sizeof(char*));
    for (int i = 0; i < argc; i++) nargv[i] = argv[i];
    nargv[argc] = (char*)"--inspect";
    int rc = cmd_run(argc + 1, nargv);
    free(nargv);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "arcade1 — ARCADE-1 developer CLI (M28 foundation)\n"
                "usage: arcade1 <command> [args]\n"
                "commands:\n"
                "  run      run a game headlessly: scripted input, hashes, captures\n"
                "  capture  run + dump frames (default dir: captures/)\n"
                "  test     smoke run; --golden FILE compares per-frame hashes\n"
                "  build    compile a guest C program into a .arc1 image\n"
                "  inspect  run, then dump final PC, registers, instruction count\n");
        return 2;
    }
    if (!strcmp(argv[1], "run"))
        return cmd_run(argc - 2, argv + 2);
    if (!strcmp(argv[1], "capture"))
        return cmd_capture(argc - 2, argv + 2);
    if (!strcmp(argv[1], "test"))
        return cmd_test(argc - 2, argv + 2);
    if (!strcmp(argv[1], "build"))
        return cmd_build(argc - 2, argv + 2);
    if (!strcmp(argv[1], "inspect"))
        return cmd_inspect(argc - 2, argv + 2);

    fprintf(stderr, "arcade1: unknown command '%s'\n", argv[1]);
    return 2;
}
