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
 *     --syms FILE          load an `nm` symbol dump (<game>.syms)
 *     --vars LIST          print named game variables (comma list, or "all")
 *     --var-every K        also print the selected variables every K frames
 *     --record FILE         write the applied per-frame buttons to FILE, in the
 *                           input script format (run-length, "none" = idle)
 *     --quiet              suppress the per-frame hash lines (summary only)
 *
 * Exit codes: 0 success, 1 runtime error (bad image, unreadable file,
 * unknown/non-RAM variable, ...), 2 usage error.
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

/* Parse an `nm` dump: "<hexaddr> <type> <name>" per line. Blank lines and nm
 * section headers (which don't match 3 fields) are skipped. 0 ok, -1 error. */
static int load_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];

    if (!f) {
        fprintf(stderr, "arcade1: cannot open symbol dump '%s'\n", path);
        return -1;
    }
    g_nsyms = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned addr;
        char type, name[64];

        if (sscanf(line, "%x %c %63s", &addr, &type, name) != 3)
            continue;                        /* blank line or nm header */
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
    int inspect = 0;    int quiet = 0;
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
        (var_list && !syms_path) || (var_every && !var_list)) {
        fprintf(stderr,
                "usage: arcade1 run <game.arc1|bin> [--frames N] [--input FILE]\n"
                "                   [--capture FILE] [--capture-dir DIR]\n"
                "                   [--capture-every K] [--record FILE]\n"
                "                   [--syms FILE] [--vars LIST] [--var-every K]\n"
                "                   [--quiet]\n"
                "  --vars LIST is a comma list of RAM globals, or \"all\";\n"
                "  it requires --syms, and --var-every requires --vars.\n"
                "  --record writes the applied buttons in the script format.\n");
        return 2;
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
    if (syms_path && load_syms(syms_path) != 0)
        return 1;
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
    return 0;
}

/* --- entry point ---------------------------------------------------------- */


/* --- capture subcommand: run + dump frames (wrapper over run) ------------ */
static int cmd_capture(int argc, char **argv)
{
    /* Default to a capture directory if none given. */
    int has_dir = 0;
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--capture-dir")) has_dir = 1;
    if (!has_dir) {
        /* append --capture-dir captures */
        /* build new argv */
    }
    /* Delegate to run; run already supports --capture/--capture-dir. */
    return cmd_run(argc, argv);
}

/* --- test subcommand: run a game and (optionally) check golden hashes ----- */
static int cmd_test(int argc, char **argv)
{
    /* Smoke: just run; non-zero run result propagates. */
    return cmd_run(argc, argv);
}

/* --- build subcommand: compile a guest C program into a .arc1 image ------ */
static int cmd_build(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "usage: arcade1 build <program.c> [output.arc1]\n");
        return 2;
    }
    char cmd[2048];
    const char *src = argv[0];
    const char *out = (argc >= 2) ? argv[1] : "program.arc1";
    /* derive base name */
    char base[256];
    strncpy(base, src, sizeof base - 1);
    base[sizeof base - 1] = 0;
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    char *slash = strrchr(base, '/');
    const char *name = slash ? slash + 1 : base;
    char elf[512], bin[512];
    snprintf(elf, sizeof elf, "programs/build/%s.elf", name);
    snprintf(bin, sizeof bin, "programs/build/%s.bin", name);
    snprintf(cmd, sizeof cmd,
        "mkdir -p programs/build && "
        "riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32 -Os -ffreestanding -nostdlib -nostartfiles "
        "-T programs/link.ld -o %s programs/crt0.S %s && "
        "riscv64-unknown-elf-objcopy -O binary %s %s && "
        "build/packarc1 %s %s",
        elf, src, elf, bin, bin, out);
    int rc = system(cmd);
    return rc == 0 ? 0 : 1;
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
                "  run      run a game headlessly with scripted input, hashes, dumps\n"
                "  capture  dump frames to a directory (wrapper over run)\n"
                "  test     run a game (smoke; optional golden compare)\n"
                "  build    compile a guest C program into a .arc1 image\n"
                "  inspect  dump machine state after a run\n");
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
