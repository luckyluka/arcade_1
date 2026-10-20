/* tests/test_info.c — ROADMAP_V4 card 4.1: `arcade1 info` is one source of
 * truth for the machine's constants.
 *
 * The test reads the JSON `arcade1 info --json` produced and asserts, field
 * by field, that every number equals the macro the VM is compiled with
 * (src/arcade1/machine.h, memory.h) — and that the SDK header list is
 * exactly the header subset of the embedded SDK set (tools/sdk_data.h), i.e.
 * enumerated, not hand-typed.
 *
 * It is deliberately tiny and dependency-free: the JSON it reads is produced
 * by the CLI itself, so the parser only has to handle one flat object of
 * numbers, strings and one array of strings.
 *
 * Usage: test_info <info.json>
 */

#include "machine.h"    /* ARCADE1_WIDTH, ARCADE1_FRAME_STEP_CAP, ... */
#include "memory.h"     /* ARCADE1_ROM_BASE, ... */
#include "sdk_data.h"   /* sdk_files[] — the embedded SDK set */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define MAX_BUF (1u << 20)

static char buf[MAX_BUF];

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "test-info: FAILED — ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

/* Position just after the key's closing quote, or NULL. */
static const char *after_key(const char *key)
{
    char pat[64];
    const char *p;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(buf, pat);
    return p ? p + strlen(pat) : NULL;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

/* Same, but also crosses the ':' between a key and its value. */
static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == ':')
        p = skip_ws(p + 1);
    return p;
}

static long json_int(const char *key)
{
    const char *p = after_key(key);
    char *end;
    long v;
    if (!p)
        fail("JSON has no key \"%s\"", key);
    p = skip_value(p);
    v = strtol(p, &end, 10);
    if (end == p)
        fail("key \"%s\" is not a number", key);
    return v;
}

/* Copy the string value of `key` into `out`; returns 0 on success. */
static int json_str(const char *key, char *out, size_t cap)
{
    const char *p = after_key(key);
    size_t n = 0;
    if (!p)
        return -1;
    p = skip_value(p);
    if (*p != '"')
        return -1;
    p++;
    while (*p && *p != '"') {
        if (n + 1 >= cap)
            return -1;
        out[n++] = *p++;
    }
    if (*p != '"')
        return -1;
    out[n] = '\0';
    return 0;
}

/* The quoted strings of the array under `key`, in order. Returns the count. */
static int json_str_array(const char *key, char out[][64], int max)
{
    const char *p = after_key(key);
    int n = 0;
    if (!p)
        fail("JSON has no key \"%s\"", key);
    p = skip_value(p);
    if (*p != '[')
        fail("key \"%s\" is not an array", key);
    p++;
    for (;;) {
        size_t len = 0;
        p = skip_ws(p);
        if (*p == ']')
            return n;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"')
            fail("key \"%s\" holds a non-string entry", key);
        p++;
        while (*p && *p != '"') {
            if (len + 1 >= 64)
                fail("key \"%s\" holds a too-long entry", key);
            if (n >= max)
                fail("key \"%s\" holds more than %d entries", key, max);
            out[n][len++] = *p++;
        }
        out[n][len] = '\0';
        n++;
        p++;   /* step over the closing quote */
    }
}

static void expect_int(const char *key, long want)
{
    long got = json_int(key);
    if (got != want)
        fail("key \"%s\" is %ld, the macro says %ld", key, got, want);
}

static int ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    size_t got;
    char fmt[64];
    char headers[64][64];
    char files[64][64];
    int n_headers, n_files, i, j;

    if (argc != 2) {
        fprintf(stderr, "usage: test_info <info.json>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f)
        fail("cannot open %s", argv[1]);
    got = fread(buf, 1, MAX_BUF - 1, f);
    buf[got] = '\0';
    fclose(f);

    /* --- every numeric field equals the macro it claims to come from ---- */
    expect_int("screen_width",  ARCADE1_WIDTH);
    expect_int("screen_height", ARCADE1_HEIGHT);
    expect_int("framebuffer_pixels", ARCADE1_FB_PIXELS);
    expect_int("fps",            ARCADE1_FPS_INT);
    expect_int("sample_rate",    ARCADE1_SAMPLE_RATE_INT);
    expect_int("audio_samples_per_frame", ARCADE1_AUDIO_FRAMES);
    expect_int("instruction_cap", (long)ARCADE1_FRAME_STEP_CAP);
    expect_int("rom_base",  (long)ARCADE1_ROM_BASE);
    expect_int("rom_size",  (long)ARCADE1_ROM_SIZE);
    expect_int("ram_base",  (long)ARCADE1_RAM_BASE);
    expect_int("ram_size",  (long)ARCADE1_RAM_SIZE);
    expect_int("mmio_base", (long)ARCADE1_MMIO_BASE);
    expect_int("mmio_size", (long)ARCADE1_MMIO_SIZE);

    /* --- the framebuffer format is the machine's format ------------------ */
    if (json_str("framebuffer_format", fmt, sizeof fmt) != 0)
        fail("key \"framebuffer_format\" is not a string");
    if (strcmp(fmt, "RGB565") != 0)
        fail("framebuffer_format is \"%s\", want \"RGB565\"", fmt);

    /* --- the SDK header list equals the header subset of the embedded set */
    n_headers = json_str_array("sdk_headers", headers, 64);
    n_files   = json_str_array("sdk_files", files, 64);

    if (n_files != (int)sdk_file_count)
        fail("JSON lists %d sdk files, the embedded set has %u",
             n_files, sdk_file_count);
    for (i = 0; i < n_files; i++)
        if (strcmp(files[i], sdk_files[i].name) != 0)
            fail("sdk file %d is \"%s\", the embedded set has \"%s\"",
                 i, files[i], sdk_files[i].name);

    {
        int n_expected = 0;
        for (i = 0; i < (int)sdk_file_count; i++)
            if (ends_with(sdk_files[i].name, ".h"))
                n_expected++;
        if (n_headers != n_expected)
            fail("JSON lists %d sdk headers, the embedded set has %d",
                 n_headers, n_expected);
        j = 0;
        for (i = 0; i < (int)sdk_file_count; i++) {
            if (!ends_with(sdk_files[i].name, ".h"))
                continue;
            if (strcmp(headers[j], sdk_files[i].name) != 0)
                fail("sdk header %d is \"%s\", the embedded set has \"%s\"",
                     j, headers[j], sdk_files[i].name);
            j++;
        }
    }

    printf("test-info: ok (%d numeric fields match the macros, "
           "%d sdk headers match the embedded set)\n",
           13, n_headers);
    return 0;
}
