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
 *     --capture FILE       write the final framebuffer to FILE (.png or .ppm
 *                          by extension); --frames N captures frame N-1
 *     --capture-dir DIR    write frame_NNNN.<ext> into DIR every --capture-every
 *     --capture-every K    frame stride for --capture-dir (default 1)
 *     --format F           --capture-dir extension: png or ppm (default ppm)
 *     --scale N            upscale captures N x, nearest neighbour (1..8)
 *     --syms FILE          load an `nm` symbol dump (<game>.syms); omitted
 *                          with --vars it auto-loads <image>.syms
 *     --vars LIST          print named game variables (comma list, or "all")
 *     --var-every K        also print the selected variables every K frames
 *     --record FILE         write the applied per-frame buttons to FILE, in the
 *                           input script format (run-length, "none" = idle)
 *     --live               play window: an SDL3 window shows every frame and
 *                          live keys drive the pad (arrows/WASD d-pad, Z/X ->
 *                          A/B, Enter -> START); R ends the session writing
 *                          --record's script, ESC quits. Plays until R/ESC
 *                          unless --frames bounds it. Optional: without SDL3
 *                          or a display, a clean message and exit 3.
 *     --quiet              suppress the per-frame hash lines (summary only)
 *
 * Exit codes: 0 success, 1 runtime error (bad image, unreadable file,
 * unknown/non-RAM variable, ...), 2 usage error — also --vars with no
 * sidecar .syms, or a .syms whose build CRC mismatches the image; 3 the
 * --live play window is unavailable (built without SDL3, or no display).
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
 * PLAY WINDOW (--live): the one path where a human, not a script, drives the
 * machine (ROADMAP_CREATION_FLOW §10; ROADMAP_V3 §4.5 pins SDL3, optional,
 * not in make check). It changes exactly two things — the input source (live
 * keys instead of --input) and the presentation (upload + 60 Hz pacing) — and
 * adds nothing else: the frame loop, the hashes, the captures and the record
 * writer are the shared ones, and the script loader's entries are never read
 * (scripting and --live share no mutable state). A recorded live session
 * replays bit-identically through --input like any scripted --record. An
 * open recording is finished on every exit path (R, ESC, window close, end
 * of --frames), so a live session always leaves a complete script.
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
#include <limits.h>       /* UINT_MAX: a live session with no --frames bound */
#include <unistd.h>       /* fork, execvp, unlink, rmdir, mkdtemp, access */
#include <fcntl.h>        /* open: capturing nm output in cmd_build */
#include <sys/stat.h>     /* mkdir: the capture subcommand's directory */
#include <errno.h>
#include <sys/wait.h>     /* waitpid */

/* The play window (cmd_run --live): SDL3 is OPTIONAL (ROADMAP_V3 §4.5). The
 * Makefile defines this only when pkg-config finds sdl3; without it every
 * other command is identical and --live exits 3 with a clean message. */
#ifdef ARCADE1_HAVE_SDL3
#include <SDL3/SDL.h>
#endif

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

/* --- Play window (--live; ROADMAP_CREATION_FLOW §10, ROADMAP_V3 §4.5) -----
 * `--live` is the one path where a human, not a script, drives the machine:
 * an SDL3 window shows every frame and live key state feeds the pad each
 * frame. It touches only the input source and the presentation; the frame
 * loop, the hashes, the captures and --record stay the shared, scripted
 * ones, and the script loader's entries are never read (scripting and
 * --live share no mutable state, determinism rule 2).
 *
 * SDL3 is optional: the Makefile defines ARCADE1_HAVE_SDL3 only when
 * pkg-config finds it, and without it (or without a display at runtime)
 * --live prints a clean message and exits 3 — every other command, and all
 * of `make check`, is unaffected. The window is therefore NOT in
 * `make check`; tests/test_playwindow.sh (by hand) drives it, including a
 * real XTEST key session when a display is available.
 *
 * Keys (the §10 mapping): arrows or WASD -> d-pad, Z -> A, X -> B,
 * Enter -> START; R -> end the session (writing --record's script), ESC or
 * window close -> quit — an open recording is finished on either exit, so a
 * live session always leaves a complete, replayable script. SELECT has no
 * binding in the card: reported as a premise gap, not invented here. */

/* record_open's sibling for live sessions: the frame count is only known
 * when the player ends the session, so the header states what the file is
 * and the runs carry the length (load_input ignores '#' lines either way;
 * the last run's end frame + 1 is the session's frame count). */
static int record_open_live(const char *path)
{
    g_record = fopen(path, "w");
    if (!g_record) {
        fprintf(stderr, "arcade1: cannot write record file '%s'\n", path);
        return -1;
    }
    fprintf(g_record, "# arcade1 run --live: recorded live session\n");
    g_run_open = 0;
    return 0;
}

#ifdef ARCADE1_HAVE_SDL3

static SDL_Window   *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture  *g_texture;
static int           g_sdl_up;      /* SDL_Init succeeded (SDL_Quit owed)    */
static int           g_live_end;    /* 1 = R / ESC / close asked to stop     */
static uint64_t      g_frame_ns;    /* 60 Hz pacing deadline (SDL_GetTicksNS)*/

/* Held key state: events set it, each frame reads it (level-triggered, so a
 * key held across frames stays held; R and ESC are edge-triggered above). */
static int gk_up, gk_down, gk_left, gk_right;
static int gk_w, gk_a, gk_s, gk_d;
static int gk_z, gk_x, gk_start;

/* Open window + renderer + texture. Returns 0, or -1 after a clean message
 * (the caller exits 3: no usable play window). NULL-safe partial setups are
 * unwound by live_shutdown(). */
static int live_setup(const char *rom_path, unsigned scale)
{
    char title[160];
    const char *base = strrchr(rom_path, '/');

    base = base ? base + 1 : rom_path;
    snprintf(title, sizeof title, "ARCADE-1: %.120s", base);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr,
                "arcade1 run --live: no play window: %s\n"
                "  headless runs are unaffected: use --input/--frames, "
                "without --live\n", SDL_GetError());
        return -1;
    }
    g_sdl_up = 1;
    g_window = SDL_CreateWindow(title, (int)(ARCADE1_WIDTH * scale),
                                (int)(ARCADE1_HEIGHT * scale), 0);
    if (!g_window) {
        fprintf(stderr, "arcade1 run --live: cannot open a window: %s\n",
                SDL_GetError());
        return -1;
    }
    g_renderer = SDL_CreateRenderer(g_window, NULL);
    if (!g_renderer)                       /* headless X, no GL: software */
        g_renderer = SDL_CreateRenderer(g_window, "software");
    if (!g_renderer) {
        fprintf(stderr, "arcade1 run --live: no renderer: %s\n",
                SDL_GetError());
        return -1;
    }
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  ARCADE1_WIDTH, ARCADE1_HEIGHT);
    if (!g_texture) {
        fprintf(stderr, "arcade1 run --live: no RGB565 texture: %s\n",
                SDL_GetError());
        return -1;
    }
    /* Pixel art stays pixel art at any window scale (nearest, like captures). */
    if (!SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST)) {
        fprintf(stderr, "arcade1 run --live: no nearest-neighbour scaling: %s\n",
                SDL_GetError());
        return -1;
    }
    g_live_end = 0;
    g_frame_ns = SDL_GetTicksNS() + 1000000000ull / 60;
    return 0;
}

/* Drain SDL events: track the held keys, note R/ESC/window close. Returns 1
 * when the session should end (checked at the top of the frame loop, so the
 * recorded runs and the printed frame lines always cover the same frames). */
static int live_poll(void)
{
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            g_live_end = 1;
        } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            const SDL_Scancode sc = e.key.scancode;
            const int          down = e.key.down;

            if (down && !e.key.repeat &&
                (sc == SDL_SCANCODE_R || sc == SDL_SCANCODE_ESCAPE))
                g_live_end = 1;
            switch (sc) {
            case SDL_SCANCODE_UP:     gk_up    = down; break;
            case SDL_SCANCODE_DOWN:   gk_down  = down; break;
            case SDL_SCANCODE_LEFT:   gk_left  = down; break;
            case SDL_SCANCODE_RIGHT:  gk_right = down; break;
            case SDL_SCANCODE_W:      gk_w     = down; break;
            case SDL_SCANCODE_A:      gk_a     = down; break;
            case SDL_SCANCODE_S:      gk_s     = down; break;
            case SDL_SCANCODE_D:      gk_d     = down; break;
            case SDL_SCANCODE_Z:      gk_z     = down; break;
            case SDL_SCANCODE_X:      gk_x     = down; break;
            case SDL_SCANCODE_RETURN: gk_start = down; break;
            default: break;
            }
        }
    }
    return g_live_end;
}

/* Held keys -> this frame's pad state (the §10 mapping; SELECT is unbound —
 * see the premise note above). */
static void live_input(arcade1_input_t *out)
{
    memset(out, 0, sizeof *out);
    out->up     = gk_up    || gk_w;
    out->down   = gk_down  || gk_s;
    out->left   = gk_left  || gk_a;
    out->right  = gk_right || gk_d;
    out->a      = gk_z;
    out->b      = gk_x;
    out->start  = gk_start;
}

/* Upload this frame's framebuffer, show it, and pace to 60 Hz. */
static void live_present(const arcade1_machine_t *m)
{
    const uint64_t period = 1000000000ull / 60;
    uint64_t       now;

    if (!SDL_UpdateTexture(g_texture, NULL, arcade1_framebuffer(m),
                           (int)arcade1_framebuffer_pitch(m))) {
        fprintf(stderr, "arcade1 run --live: texture upload failed: %s\n",
                SDL_GetError());
        g_live_end = 1;                  /* end gracefully; the loop closes */
        return;
    }
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);

    now = SDL_GetTicksNS();
    if (g_frame_ns > now)
        SDL_DelayNS(g_frame_ns - now);
    g_frame_ns += period;
    if (g_frame_ns < now)                /* overran: owe no catch-up burst */
        g_frame_ns = now;
}

/* Tear the window down; NULL/id-safe, so partial setups unwind too. */
static void live_shutdown(void)
{
    if (g_texture)  { SDL_DestroyTexture(g_texture);   g_texture  = NULL; }
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = NULL; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window   = NULL; }
    if (g_sdl_up)   { SDL_Quit();                      g_sdl_up   = 0; }
}

#else /* !ARCADE1_HAVE_SDL3: no play window — clean message, exit 3 -------- */

static int live_setup(const char *rom_path, unsigned scale)
{
    (void)rom_path; (void)scale;
    fprintf(stderr,
            "arcade1 run --live: this build has no play window (SDL3 was not "
            "found at build time)\n"
            "  install SDL3 (pkg-config sdl3) and rebuild; headless runs "
            "(--input, --frames) work without it\n");
    return -1;
}
static int  live_poll(void)  { return 1; }
static void live_input(arcade1_input_t *out) { memset(out, 0, sizeof *out); }
static void live_present(const arcade1_machine_t *m) { (void)m; }
static void live_shutdown(void) { }

#endif /* ARCADE1_HAVE_SDL3 */

/* --- PPM (P6) frame dump -------------------------------------------------- */

/* RGB565 -> 8-bit per channel. One expansion, shared by the PPM and the
 * PNG writer, so the two formats always carry the same bytes. */
static void px_rgb888(uint16_t p, uint8_t out[3])
{
    out[0] = (uint8_t)(((p >> 11) & 31u) * 255u / 31u);   /* R */
    out[1] = (uint8_t)(((p >> 5) & 63u) * 255u / 63u);    /* G */
    out[2] = (uint8_t)((p & 31u) * 255u / 31u);           /* B */
}

/* Nearest-neighbour upscale: output pixel (x, y) is framebuffer pixel
 * (x / scale, y / scale). scale 1 reproduces the framebuffer exactly. */
static int write_ppm(const char *path, const uint16_t *fb, unsigned scale)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "arcade1: cannot write '%s'\n", path);
        return -1;
    }
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)ARCADE1_WIDTH * scale,
            (unsigned)ARCADE1_HEIGHT * scale);
    for (unsigned y = 0; y < (unsigned)ARCADE1_HEIGHT * scale; y++) {
        const uint16_t *row = fb + (y / scale) * (unsigned)ARCADE1_WIDTH;
        for (unsigned x = 0; x < (unsigned)ARCADE1_WIDTH * scale; x++) {
            uint8_t rgb[3];
            px_rgb888(row[x / scale], rgb);
            if (fwrite(rgb, 1, 3, f) != 3) {
                fclose(f);
                return -1;
            }
        }
    }
    return fclose(f) == 0 ? 0 : -1;
}

/* --- PNG capture (ROADMAP_V3 card A3) -------------------------------------
 * A dependency-free PNG writer: 8-bit RGB (colour type 2), filter byte 0
 * per row, one zlib stream of stored (uncompressed) deflate blocks, and
 * Adler-32 / CRC-32 computed here. No zlib, no libpng. */

/* IEEE CRC-32 (poly 0xEDB88320, reflected) fed incrementally: the same
 * checksum as arcade1_crc32(), applied without copying chunk type+body
 * into one buffer. Caller inits to 0xFFFFFFFF and xors the result once. */
static uint32_t crc32_run(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int)(crc & 1u)));
    }
    return crc;
}

/* Adler-32 over the raw deflate data (RFC 1950). */
static uint32_t adler32_run(const uint8_t *data, size_t len)
{
    uint32_t a = 1, b = 0;

    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

/* One PNG chunk: length, type, body, CRC(type || body), all big-endian. */
static int png_chunk(FILE *f, const char *type, const uint8_t *data, size_t len)
{
    uint8_t  hdr[4];
    uint32_t crc = 0xFFFFFFFFu;

    hdr[0] = (uint8_t)(len >> 24); hdr[1] = (uint8_t)(len >> 16);
    hdr[2] = (uint8_t)(len >> 8);  hdr[3] = (uint8_t)len;
    crc = crc32_run(crc, (const uint8_t *)type, 4);
    if (len)
        crc = crc32_run(crc, data, len);
    crc ^= 0xFFFFFFFFu;

    if (fwrite(hdr, 1, 4, f) != 4 ||
        fwrite(type, 1, 4, f) != 4 ||
        (len && fwrite(data, 1, len, f) != len))
        return -1;
    hdr[0] = (uint8_t)(crc >> 24); hdr[1] = (uint8_t)(crc >> 16);
    hdr[2] = (uint8_t)(crc >> 8);  hdr[3] = (uint8_t)crc;
    return fwrite(hdr, 1, 4, f) == 4 ? 0 : -1;
}

static int write_png(const char *path, const uint16_t *fb, unsigned scale)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    const unsigned w = (unsigned)ARCADE1_WIDTH * scale;
    const unsigned h = (unsigned)ARCADE1_HEIGHT * scale;
    const size_t   stride = (size_t)w * 3 + 1;        /* filter byte + RGB  */
    const size_t   raw_len = stride * h;
    const size_t   nblk = (raw_len + 65534) / 65535;  /* ceil, >= 1         */
    const size_t   zlen = 2 + raw_len + 5 * nblk + 4;
    uint8_t  ihdr[13], *raw, *z;
    uint32_t ad;
    size_t   pos = 0, off = 0;
    FILE    *f;
    int      rc = 0;

    raw = malloc(raw_len);
    z = malloc(zlen);
    if (!raw || !z) {
        free(raw); free(z);
        fprintf(stderr, "arcade1: out of memory writing '%s'\n", path);
        return -1;
    }
    for (unsigned y = 0; y < h; y++) {
        const uint16_t *row = fb + (y / scale) * (unsigned)ARCADE1_WIDTH;
        uint8_t *dst = raw + (size_t)y * stride;
        dst[0] = 0;                                  /* filter: none */
        for (unsigned x = 0; x < w; x++)
            px_rgb888(row[x / scale], dst + 1 + (size_t)x * 3);
    }

    /* One zlib stream: 2-byte header, stored deflate blocks, Adler-32. */
    z[pos++] = 0x78;                                 /* CMF: deflate, 32K    */
    z[pos++] = 0x01;                                 /* FLG: check bits ok   */
    while (off < raw_len) {
        size_t n = raw_len - off;
        if (n > 65535)
            n = 65535;
        z[pos++] = (off + n == raw_len) ? 0x01 : 0x00;   /* BFINAL + BTYPE=00 */
        z[pos++] = (uint8_t)(n & 0xFFu);
        z[pos++] = (uint8_t)(n >> 8);
        z[pos++] = (uint8_t)(~n & 0xFFu);
        z[pos++] = (uint8_t)((~n >> 8) & 0xFFu);
        memcpy(z + pos, raw + off, n);
        pos += n;
        off += n;
    }
    ad = adler32_run(raw, raw_len);
    z[pos++] = (uint8_t)(ad >> 24); z[pos++] = (uint8_t)(ad >> 16);
    z[pos++] = (uint8_t)(ad >> 8);  z[pos++] = (uint8_t)ad;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "arcade1: cannot write '%s'\n", path);
        free(raw); free(z);
        return -1;
    }
    ihdr[0]  = (uint8_t)(w >> 24); ihdr[1]  = (uint8_t)(w >> 16);
    ihdr[2]  = (uint8_t)(w >> 8);  ihdr[3]  = (uint8_t)w;
    ihdr[4]  = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6]  = (uint8_t)(h >> 8);  ihdr[7] = (uint8_t)h;
    ihdr[8]  = 8;                  /* bit depth          */
    ihdr[9]  = 2;                  /* colour type: RGB   */
    ihdr[10] = 0;                  /* compression method */
    ihdr[11] = 0;                  /* filter method      */
    ihdr[12] = 0;                  /* interlace          */
    if (fwrite(sig, 1, sizeof sig, f) != sizeof sig ||
        png_chunk(f, "IHDR", ihdr, sizeof ihdr) != 0 ||
        png_chunk(f, "IDAT", z, zlen) != 0 ||
        png_chunk(f, "IEND", NULL, 0) != 0)
        rc = -1;
    if (fclose(f) != 0)
        rc = -1;
    free(raw);
    free(z);
    if (rc != 0)
        fprintf(stderr, "arcade1: write failed for '%s'\n", path);
    return rc;
}

/* Pick the capture writer by extension: *.png -> PNG, anything else -> PPM
 * (the historical default, so existing scripts keep their bytes). */
static int write_capture(const char *path, const uint16_t *fb, unsigned scale)
{
    size_t n = strlen(path);

    if (n > 4 && !strcmp(path + n - 4, ".png"))
        return write_png(path, fb, scale);
    return write_ppm(path, fb, scale);
}

/* --- Named game variables --------------------------------------------------
 * "What does the game believe is happening?" (AI_NATIVE_DEVELOPMENT.md): read a
 * game's globals out of guest RAM by name, from an `nm` symbol dump (the same
 * <game>.syms the game tests use). Only RAM-resident data/bss symbols are
 * variables; each is read as one 32-bit word (the guest's `int`, cast to signed
 * — exactly how test-breakout etc. read score/lives/state). */

#define SYM_MAX 8192
/* The --vars selection is sized to hold every loaded name: "all" prints
 * every name in the symbol file and can never silently truncate
 * (ROADMAP_V3 card A4; a comma list is bounded by its own 1 KiB text). */
#define VAR_MAX SYM_MAX

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
        /* Every name in the symbol file, no cap (ROADMAP_V3 card A4).
         * load_syms already keeps only RAM-window names, and g_vars is
         * sized to SYM_MAX, so this selection cannot silently truncate. */
        for (unsigned i = 0; i < g_nsyms && g_nvars < VAR_MAX; i++) {
            const symbol_t *s = &g_syms[i];
            if (s->name[0] == '_')            /* linker internals (_sbss, ...) */
                continue;
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
    unsigned scale = 1;              /* capture upscale, 1..8 (A3) */
    int format_png = 0;              /* capture-dir extension: .png? */
    int inspect = 0;    int quiet = 0;    int strict = 0;
    int live = 0;                      /* --live: the play window (SDL3)    */
    int frames_set = 0;                /* did --frames appear on the line?  */
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
        if (!strcmp(a, "--frames") && i + 1 < argc) {      frames = (unsigned)strtoul(argv[++i], NULL, 10); frames_set = 1; }
        else if (!strcmp(a, "--input") && i + 1 < argc)    input_path = argv[++i];
        else if (!strcmp(a, "--capture") && i + 1 < argc)  capture = argv[++i];
        else if (!strcmp(a, "--capture-dir") && i + 1 < argc) capture_dir = argv[++i];
        else if (!strcmp(a, "--capture-every") && i + 1 < argc) capture_every = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--format") && i + 1 < argc) {
            const char *fmt = argv[++i];
            if (!strcmp(fmt, "png"))       format_png = 1;
            else if (!strcmp(fmt, "ppm"))  format_png = 0;
            else {
                fprintf(stderr, "arcade1 run: --format must be png or ppm (got '%s')\n", fmt);
                return 2;
            }
        }
        else if (!strcmp(a, "--scale") && i + 1 < argc) scale = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--record") && i + 1 < argc)    record_path = argv[++i];
        else if (!strcmp(a, "--live"))                      live = 1;
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
                "                   [--capture-every K] [--format png|ppm]\n"
                "                   [--scale N] [--record FILE] [--live]\n"
                "                   [--syms FILE] [--vars LIST] [--var-every K]\n"
                "                   [--quiet] [--strict]\n"
                "  --strict exits non-zero if any draw read a source address\n"
                "  outside ROM/RAM (they silently draw nothing); see below.\n"
                "  --vars LIST is a comma list of RAM globals, or \"all\";\n"
                "  without --syms it auto-loads <image>.syms ('arcade1 build'\n"
                "  writes it); --var-every requires --vars.\n"
                "  --capture picks the format by extension (.png or .ppm);\n"
                "  --format sets --capture-dir's extension (default ppm);\n"
                "  --scale N upscales captures N x, nearest neighbour (1..8).\n"
                "  --record writes the applied buttons in the script format.\n"
                "  --live opens the play window (SDL3): arrows/WASD d-pad,\n"
                "  Z -> A, X -> B, Enter -> START, --scale N sets the window\n"
                "  size; R ends the session writing --record's script, ESC\n"
                "  quits. Plays until R/ESC unless --frames bounds it, and\n"
                "  exits 3 (clean message) without SDL3 or a display.\n");
        return 2;
    }
    if (scale < 1 || scale > 8) {
        fprintf(stderr, "arcade1 run: --scale must be 1..8 (got %u)\n", scale);
        return 2;
    }

    /* --live rules (ROADMAP_CREATION_FLOW §10): live keys and a replayed
     * script never drive the same run (they must not share mutable state),
     * and an unbounded live session plays until R/ESC ends it — --frames
     * bounds it, for tests and for fixed-length recordings. */
    if (live) {
        if (input_path) {
            fprintf(stderr,
                    "arcade1 run: --live reads live keys and --input replays "
                    "a script; they must not drive the same run\n");
            return 2;
        }
        if (!frames_set)
            frames = UINT_MAX;
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

    /* Fail fast on an unwritable record file (before running anything). A
     * live session's length is unknown at open, so its header differs. */
    if (record_path) {
        const int rec = live ? record_open_live(record_path)
                             : record_open(record_path, frames);
        if (rec != 0)
            return 1;
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

    /* Open the play window last, before the loop: if it cannot open, exit 3
     * cleanly and take back a record file this run just created (nothing was
     * played, so the file would be a header-only stub). */
    if (live && live_setup(rom_path, scale) != 0) {
        if (record_path) {
            record_close(0);
            unlink(record_path);
        }
        live_shutdown();
        return 3;
    }

    /* `fr` outlives the loop: it equals `frames` for a scripted run, and the
     * number actually played when R/ESC ends a live session early. */
    {
    unsigned fr = 0;

    for (; fr < frames; fr++) {
        if (live) {
            if (live_poll())            /* R / ESC / window close: stop here */
                break;
            live_input(&in);
        } else {
            input_for_frame(fr, &in);
        }
        arcade1_set_input(&m, &in);
        record_frame(fr, &in);
        arcade1_run_frame(&m);
        if (live)
            live_present(&m);           /* upload + show + pace to 60 Hz */
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
            snprintf(path, sizeof path, "%s/frame_%04u.%s", capture_dir, fr,
                     format_png ? "png" : "ppm");
            if (write_capture(path, arcade1_framebuffer(&m), scale) != 0) {
                record_close(fr);
                if (live)
                    live_shutdown();
                return 1;
            }
        }
    }

    /* The recorded runs always end on the last frame actually played. */
    record_close(fr ? fr - 1 : 0);

    if (capture && write_capture(capture, arcade1_framebuffer(&m), scale) != 0) {
        if (live)
            live_shutdown();
        return 1;
    }

    for (unsigned v = 0; v < g_nvars; v++)
        printf("var %s = %d\n", g_vars[v]->name,
               (int)(int32_t)arcade1_read32(&m.bus, g_vars[v]->addr));

    if (inspect) {
        printf("inspect: frame=%u pc=0x%08X instret=%llu\n",
               m.frame_counter, m.cpu.pc, (unsigned long long)m.cpu.instret);
        for (int r = 0; r < 32; r++)
            printf("  x%-2d = 0x%08X\n", r, m.cpu.regs[r]);
    }
    printf("ran %u frames  final state hash=0x%016llX  fb hash=0x%016llX\n",
           fr, (unsigned long long)state_hash(&m.cpu, &m.bus),
           (unsigned long long)fb_hash(&m));

    /* Every live exit path reaches here, so the recording is always finished
     * (R, ESC, window close and end-of-frames all leave a complete script). */
    if (live) {
        live_shutdown();
        if (record_path)
            printf("recorded live session -> %s\n", record_path);
    }
    }                                       /* scope of fr */

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
                "  --format png for .png files, --scale N to upscale; or a\n"
                "  single final frame with --capture FILE — format by its\n"
                "  extension).\n");
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
            /* A hash diff alone doesn't say WHY — point at the recorded
             * conditions (ROADMAP_V3 4.1: card 0.2's hint, merged here). */
            fprintf(stderr,
                    "arcade1 test: goldens record hashes only — reproduce "
                    "the golden's run\n"
                    "arcade1 test: conditions (frames, input script, ROM). "
                    "See\n"
                    "arcade1 test: tests/golden/conditions.txt\n");
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
    if (n < total) {
        /* Partial coverage used to exit 0 — a short run passed silently
         * (F9). Keep the note, but it is a FAILURE (ROADMAP_V3 4.1 0.3). */
        printf("arcade1 test: %u of %u golden frame lines match %s\n"
               "  (the run was shorter than the golden's first pass; "
               "--frames %u covers it)\n", n, total, golden_path, total);
        return 1;
    }
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
 * gfx.h, arcade1_map.h, asset.h, text.h) is compiled into this binary as
 * string literals (tools/sdk_data.c, generated from programs/ by the
 * Makefile), so `arcade1 build` works in any directory with no checkout,
 * no build/ tree and no packarc1 helper beside it. The toolchain runs in
 * a private temp directory that is removed again on the way out; the only
 * file left behind is the image you asked for.
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
        (char *)"gfx.h", (char *)"arcade1_map.h", (char *)"asset.h",
        (char *)"text.h"
    };
    static char *const sdk_bodies[] = {
        (char *)sdk_crt0_S, (char *)sdk_link_ld, (char *)sdk_arcade1_h,
        (char *)sdk_gfx_h, (char *)sdk_arcade1_map_h, (char *)sdk_asset_h,
        (char *)sdk_text_h
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

    /* elf + bin + nm output on top of the SDK files (sized from the SDK
     * list, so a new SDK file cannot outrun it again). */
    char *tmpfiles[sizeof sdk_names / sizeof sdk_names[0] + 3];
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


/* --- inspect subcommand: structured last-frame report (ROADMAP_V3 5, B1) --
 * `run` answers "did the state change?"; `inspect` answers "what is the
 * machine doing at the END of this run": one greppable KEY value line per
 * fact — the last frame's video work (frame_totals), the run's hazards
 * (STRAY, VIDEO_OOB_DRAWS — same meaning as --strict), the payload budget
 * and the selected globals. Reads existing host-side counters only: no
 * MMIO, no guest-visible change, nothing in any hash. The register dump
 * moved behind --regs; --json prints one object with the same keys.
 * B2 adds provenance: --commands prints the last frame's command records,
 * --at X,Y names the last command that wrote a pixel (recorded only when
 * asked — zero cost otherwise), and STATE_HASH/FB_HASH expose the same
 * fingerprints `run` prints so a report can be cross-checked. */

static const char *video_type_name(unsigned type)
{
    switch (type) {
    case ARCADE1_VIDEO_CMD_CLEAR:     return "CLEAR";
    case ARCADE1_VIDEO_CMD_SET_PIXEL: return "SET_PIXEL";
    case ARCADE1_VIDEO_CMD_PRESENT:   return "PRESENT";
    case ARCADE1_VIDEO_CMD_FILL:      return "FILL";
    case ARCADE1_VIDEO_CMD_SPRITE:    return "SPRITE";
    case ARCADE1_VIDEO_CMD_TILEMAP:   return "TILEMAP";
    case ARCADE1_VIDEO_CMD_SPRITE4:   return "SPRITE4";
    case ARCADE1_VIDEO_CMD_TILEMAP4:  return "TILEMAP4";
    default:                          return "?";
    }
}

/* One record as a greppable CMD line (execution order). */
static void inspect_print_cmd(const arcade1_video_cmd_t *r)
{
    printf("CMD %u %s x=%d y=%d w=%u h=%u addr=0x%08X flags=%u "
           "camera=0x%08X pc=0x%08X px=%u clipped=%u oob=%u\n",
           r->id, video_type_name(r->type), r->x, r->y, r->w, r->h,
           r->addr, r->flags, r->camera, r->pc, r->px_written,
           (unsigned)r->clipped, r->oob);
}

/* The same record as one JSON object (no newline; for COMMANDS/AT). */
static void inspect_print_cmd_json(const arcade1_video_cmd_t *r)
{
    printf("{\"id\":%u,\"type\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%u,\"h\":%u,"
           "\"addr\":\"0x%08X\",\"flags\":%u,\"camera\":\"0x%08X\","
           "\"pc\":\"0x%08X\",\"px_written\":%u,\"clipped\":%u,\"oob\":%u}",
           r->id, video_type_name(r->type), r->x, r->y, r->w, r->h,
           r->addr, r->flags, r->camera, r->pc, r->px_written,
           (unsigned)r->clipped, r->oob);
}

static int cmd_inspect(int argc, char **argv)
{
    const char *rom_path = NULL, *input_path = NULL;
    const char *syms_path = NULL, *var_list = NULL;
    unsigned    frames = 600;
    int         json = 0, regs = 0;
    int         commands = 0, at_set = 0;
    unsigned    at_x = 0, at_y = 0;
    int         recording = 0;
    unsigned    at_id = 0;
    static uint8_t img[ARCADE1_IMAGE_HEADER_SIZE + ARCADE1_ROM_SIZE];
    static arcade1_machine_t m;
    size_t   n;
    const void *rom;
    size_t   rom_size;
    FILE    *f;
    arcade1_input_t in;
    unsigned long long oob = 0;              /* run: OOB source draws      */
    unsigned long long instret_before_last = 0;

    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];

        if (!strcmp(a, "--frames") && i + 1 < argc)
            frames = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--input") && i + 1 < argc)
            input_path = argv[++i];
        else if (!strcmp(a, "--syms") && i + 1 < argc)
            syms_path = argv[++i];
        else if (!strcmp(a, "--vars") && i + 1 < argc)
            var_list = argv[++i];
        else if (!strcmp(a, "--json"))
            json = 1;
        else if (!strcmp(a, "--regs"))
            regs = 1;
        else if (!strcmp(a, "--commands"))
            commands = 1;
        else if (!strcmp(a, "--at")) {
            const char *s;
            char       *e = NULL;
            unsigned long xv, yv;

            if (i + 1 >= argc) {
                fprintf(stderr, "arcade1 inspect: --at needs X,Y\n");
                return 2;
            }
            s  = argv[++i];
            xv = strtoul(s, &e, 10);
            if (e == s || *e != ',') {
                fprintf(stderr, "arcade1 inspect: --at wants pixel "
                        "coordinates X,Y (got '%s')\n", s);
                return 2;
            }
            s = e + 1;
            yv = strtoul(s, &e, 10);
            if (e == s || *e != '\0') {
                fprintf(stderr, "arcade1 inspect: --at wants pixel "
                        "coordinates X,Y (got '%s')\n", argv[i]);
                return 2;
            }
            at_x = (unsigned)xv;
            at_y = (unsigned)yv;
            at_set = 1;
        }
        else if (a[0] == '-') {
            fprintf(stderr, "arcade1 inspect: unknown option '%s'\n", a);
            return 2;
        } else if (!rom_path) {
            rom_path = a;
        } else {
            fprintf(stderr, "arcade1 inspect: unexpected argument '%s'\n",
                    a);
            return 2;
        }
    }
    if (!rom_path || frames == 0) {
        fprintf(stderr,
                "usage: arcade1 inspect <game.arc1|bin> [--frames N]\n"
                "                        [--input FILE] [--syms FILE]\n"
                "                        [--vars LIST] [--json] [--regs]\n"
                "                        [--commands] [--at X,Y]\n"
                "  runs N frames (default 600) and reports the LAST frame:\n"
                "  greppable KEY value lines — FRAME, PC, INSTRET_FRAME,\n"
                "  STRAY, VIDEO_CMDS, VIDEO_PIXELS_WRITTEN, VIDEO_OOB_DRAWS,\n"
                "  ROM_USED, STATE_HASH, FB_HASH, then VAR <name> <value>\n"
                "  per --vars selection. The hashes are the ones `run`\n"
                "  prints, so a report cross-checks against any golden.\n"
                "  --json prints one object with the same keys; the\n"
                "  32-register dump appears only with --regs.\n"
                "  --vars reads RAM globals by name; without --syms it\n"
                "  auto-loads <image>.syms ('arcade1 build' writes it).\n"
                "  --commands records the last frame's commands and prints\n"
                "  them in order (CMD lines); --at X,Y names the last\n"
                "  command that wrote that pixel. Recording happens only\n"
                "  when one of the two is asked for.\n"
                "  exit: 0 ok, 2 usage\n");
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
        img_have_crc = 1;                    /* header bytes 12..15, LE */
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

    /* No recording, no captures, no printing here — the report must not
     * perturb the run (B1 rules). B2: when (and only when) --commands or
     * --at asks, the video device records the LAST frame's commands —
     * so the buffers exist for exactly one frame of a run that asked. */
    for (unsigned fr = 0; fr < frames; fr++) {
        if ((commands || at_set) && fr + 1u == frames) {
            if (arcade1_video_record_start(&m.video) != 0) {
                fprintf(stderr,
                        "arcade1 inspect: cannot allocate the command "
                        "record\n");
                return 1;
            }
            recording = 1;
        }
        instret_before_last = m.cpu.instret;   /* value before the last frame */
        input_for_frame(fr, &in);
        arcade1_set_input(&m, &in);
        arcade1_run_frame(&m);
        oob += arcade1_video_frame_totals(&m.video)->skipped_ops;
    }

    /* --at: the owner buffer holds the id of the last command that wrote
     * each pixel during the recorded frame (0 = never written / not
     * recorded — an off-screen pixel trivially counts as never written). */
    if (at_set && at_x < (unsigned)ARCADE1_WIDTH &&
        at_y < (unsigned)ARCADE1_HEIGHT)
        at_id = m.video.owner[at_y * (unsigned)ARCADE1_WIDTH + at_x];

    {
        const arcade1_video_stats_t *ft =
            arcade1_video_frame_totals(&m.video);

        if (json) {
            printf("{\"FRAME\":%u,\"PC\":\"0x%08X\",\"INSTRET_FRAME\":%llu,"
                   "\"STRAY\":%u,"
                   "\"VIDEO_CMDS\":{\"CLEAR\":%u,\"FILL\":%u,\"SPRITE\":%u,"
                   "\"TILEMAP\":%u,\"SPRITE4\":%u,\"TILEMAP4\":%u,"
                   "\"PRESENT\":%u},"
                   "\"VIDEO_PIXELS_WRITTEN\":%u,\"VIDEO_OOB_DRAWS\":%llu,"
                   "\"ROM_USED\":{\"used\":%u,\"size\":%u},"
                   "\"STATE_HASH\":\"0x%016llX\",\"FB_HASH\":\"0x%016llX\","
                   "\"VAR\":{",
                   m.frame_counter, m.cpu.pc,
                   (unsigned long long)(m.cpu.instret - instret_before_last),
                   m.bus.stray_accesses,
                   ft->cmds_clear, ft->cmds_fill, ft->cmds_sprite,
                   ft->cmds_tilemap, ft->cmds_sprite4, ft->cmds_tilemap4,
                   ft->cmds_present, ft->pixels_written, oob,
                   (unsigned)rom_size, (unsigned)ARCADE1_ROM_SIZE,
                   (unsigned long long)state_hash(&m.cpu, &m.bus),
                   (unsigned long long)fb_hash(&m));
            for (unsigned v = 0; v < g_nvars; v++)
                printf("%s\"%s\":%d", v ? "," : "", g_vars[v]->name,
                       (int)(int32_t)arcade1_read32(&m.bus,
                                                    g_vars[v]->addr));
            printf("}");                       /* close VAR */
            if (regs) {
                printf(",\"REGS\":[");
                for (int r = 0; r < 32; r++)
                    printf("%s\"0x%08X\"", r ? "," : "", m.cpu.regs[r]);
                printf("]");
            }
            if (commands) {
                printf(",\"COMMANDS\":[");
                for (uint32_t k = 0; k < m.video.rec_count; k++) {
                    if (k) printf(",");
                    inspect_print_cmd_json(&m.video.rec[k]);
                }
                printf("]");
            }
            if (at_set) {
                printf(",\"AT\":{\"x\":%u,\"y\":%u,\"id\":%u,\"cmd\":",
                       at_x, at_y, at_id);
                if (at_id >= 1 && at_id <= m.video.rec_count)
                    inspect_print_cmd_json(&m.video.rec[at_id - 1]);
                else
                    printf("null");
                printf("}");
            }
            printf("}\n");                     /* close the object */
        } else {
            printf("FRAME %u\n", m.frame_counter);
            printf("PC 0x%08X\n", m.cpu.pc);
            printf("INSTRET_FRAME %llu\n",
                   (unsigned long long)(m.cpu.instret - instret_before_last));
            printf("STRAY %u\n", m.bus.stray_accesses);
            printf("VIDEO_CMDS CLEAR=%u FILL=%u SPRITE=%u TILEMAP=%u "
                   "SPRITE4=%u TILEMAP4=%u PRESENT=%u\n",
                   ft->cmds_clear, ft->cmds_fill, ft->cmds_sprite,
                   ft->cmds_tilemap, ft->cmds_sprite4, ft->cmds_tilemap4,
                   ft->cmds_present);
            printf("VIDEO_PIXELS_WRITTEN %u\n", ft->pixels_written);
            printf("VIDEO_OOB_DRAWS %llu\n", oob);
            printf("ROM_USED %u / %u\n", (unsigned)rom_size,
                   (unsigned)ARCADE1_ROM_SIZE);
            printf("STATE_HASH 0x%016llX\n",
                   (unsigned long long)state_hash(&m.cpu, &m.bus));
            printf("FB_HASH 0x%016llX\n",
                   (unsigned long long)fb_hash(&m));
            for (unsigned v = 0; v < g_nvars; v++)
                printf("VAR %s %d\n", g_vars[v]->name,
                       (int)(int32_t)arcade1_read32(&m.bus, g_vars[v]->addr));
            if (regs)
                for (int r = 0; r < 32; r++)
                    printf("  x%-2d = 0x%08X\n", r, m.cpu.regs[r]);
            if (commands)
                for (uint32_t k = 0; k < m.video.rec_count; k++)
                    inspect_print_cmd(&m.video.rec[k]);
            if (at_set) {
                printf("AT %u,%u id=%u\n", at_x, at_y, at_id);
                if (at_id >= 1 && at_id <= m.video.rec_count)
                    inspect_print_cmd(&m.video.rec[at_id - 1]);
                else
                    printf("AT %u,%u pixel never written in the recorded "
                           "frame\n", at_x, at_y);
            }
        }
    }
    if (recording)
        arcade1_video_record_stop(&m.video);
    return 0;
}

/* --- `arcade1 info` (ROADMAP_V4 card 4.1) ----------------------------------
 *
 * One source of truth for the machine's constants. Every number comes from
 * the SAME macros the VM is compiled with (the headers under src/arcade1),
 * and the SDK header list is enumerated from the embedded set (sdk_files),
 * never hand-typed. Trust rule (GAME_DEV_GUIDE.md, CHEATSHEET.md): if a
 * brief, a tutorial or a model disagrees with `arcade1 info`, `arcade1 info`
 * is right.
 */

#define ARCADE1_CLI_VERSION "0.1"

static void info_print_json(void)
{
    unsigned i;
    printf("{\n");
    printf("  \"cli_version\": \"%s\",\n", ARCADE1_CLI_VERSION);
    printf("  \"screen_width\": %d,\n", ARCADE1_WIDTH);
    printf("  \"screen_height\": %d,\n", ARCADE1_HEIGHT);
    printf("  \"framebuffer_format\": \"RGB565\",\n");
    printf("  \"framebuffer_pixels\": %d,\n", ARCADE1_FB_PIXELS);
    printf("  \"fps\": %d,\n", ARCADE1_FPS_INT);
    printf("  \"sample_rate\": %d,\n", ARCADE1_SAMPLE_RATE_INT);
    printf("  \"audio_samples_per_frame\": %d,\n", ARCADE1_AUDIO_FRAMES);
    printf("  \"instruction_cap\": %u,\n", (unsigned)ARCADE1_FRAME_STEP_CAP);
    printf("  \"rom_base\": %u,\n", (unsigned)ARCADE1_ROM_BASE);
    printf("  \"rom_size\": %u,\n", (unsigned)ARCADE1_ROM_SIZE);
    printf("  \"ram_base\": %u,\n", (unsigned)ARCADE1_RAM_BASE);
    printf("  \"ram_size\": %u,\n", (unsigned)ARCADE1_RAM_SIZE);
    printf("  \"mmio_base\": %u,\n", (unsigned)ARCADE1_MMIO_BASE);
    printf("  \"mmio_size\": %u,\n", (unsigned)ARCADE1_MMIO_SIZE);
    printf("  \"sdk_headers\": [");
    {
        int first = 1;
        for (i = 0; i < sdk_file_count; i++) {
            const char *name = sdk_files[i].name;
            size_t n = strlen(name);
            if (n < 2 || strcmp(name + n - 2, ".h") != 0)
                continue;                       /* headers only */
            printf("%s\"%s\"", first ? "" : ", ", name);
            first = 0;
        }
    }
    printf("],\n");
    printf("  \"sdk_files\": [");
    for (i = 0; i < sdk_file_count; i++)
        printf("%s\"%s\"", i ? ", " : "", sdk_files[i].name);
    printf("]\n");
    printf("}\n");
}

static void info_print_text(void)
{
    unsigned i;
    printf("arcade1 %s — ARCADE-1 developer CLI\n", ARCADE1_CLI_VERSION);
    printf("machine:\n");
    printf("  screen            %dx%d (RGB565, %d pixels)\n",
           ARCADE1_WIDTH, ARCADE1_HEIGHT, ARCADE1_FB_PIXELS);
    printf("  tick rate        %d fps\n", ARCADE1_FPS_INT);
    printf("  audio            %d Hz, %d samples per frame (stereo)\n",
           ARCADE1_SAMPLE_RATE_INT, ARCADE1_AUDIO_FRAMES);
    printf("  instruction cap  %u per frame\n",
           (unsigned)ARCADE1_FRAME_STEP_CAP);
    printf("memory map:\n");
    printf("  rom              0x%08X  %u bytes\n",
           (unsigned)ARCADE1_ROM_BASE, (unsigned)ARCADE1_ROM_SIZE);
    printf("  ram              0x%08X  %u bytes\n",
           (unsigned)ARCADE1_RAM_BASE, (unsigned)ARCADE1_RAM_SIZE);
    printf("  mmio             0x%08X  %u bytes\n",
           (unsigned)ARCADE1_MMIO_BASE, (unsigned)ARCADE1_MMIO_SIZE);
    printf("embedded sdk headers:\n");
    for (i = 0; i < sdk_file_count; i++) {
        const char *name = sdk_files[i].name;
        size_t n = strlen(name);
        if (n < 2 || strcmp(name + n - 2, ".h") != 0)
            continue;
        printf("  %s\n", name);
    }
    printf("embedded sdk files:\n");
    for (i = 0; i < sdk_file_count; i++)
        printf("  %s\n", sdk_files[i].name);
}

static int cmd_info(int argc, char **argv)
{
    int json = 0;
    int i;

    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) {
            json = 1;
        } else {
            fprintf(stderr, "arcade1 info: unknown option '%s'\n"
                    "usage: arcade1 info [--json]\n", argv[i]);
            return 2;
        }
    }

    if (json)
        info_print_json();
    else
        info_print_text();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "arcade1 — ARCADE-1 developer CLI (M28 foundation)\n"
                "usage: arcade1 <command> [args]\n"
                "commands:\n"
                "  run      run a game: scripted input, hashes, captures; --live\n"
                "           opens the play window (SDL3) driven by your keyboard\n"
                "  capture  run + dump frames (default dir: captures/)\n"
                "  test     smoke run; --golden FILE compares per-frame hashes\n"
                "  build    compile a guest C program into a .arc1 image\n"
                "  inspect  structured last-frame report: KEY value lines\n"
                "           (--json, --regs, --commands, --at X,Y)\n"
                "  info     the machine's constants, from the compiled-in macros\n"
                "           (--json)\n");
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
    if (!strcmp(argv[1], "info"))
        return cmd_info(argc - 2, argv + 2);

    fprintf(stderr, "arcade1: unknown command '%s'\n", argv[1]);
    return 2;
}
