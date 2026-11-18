/* json_test.c -- M4.1a conformance vectors for the bundled JSON layer.
 *
 * Modes:
 *   (none)          run the embedded vectors; rc=0 iff all hold
 *   --parse-check F exit 0 iff F parses (for acceptance parity vs Python)
 *   --rewrite F     parse F, write indent=2 to stdout (no newline)
 *   --compact F     parse F, write the compact form to stdout
 *
 * The expected strings are the byte rules of docs/OUTPUT_FORMAT.md and
 * the parser contract of docs/ARCHITECTURE.md; tests/test_c_json.py
 * additionally diffs every corpus file against the Python oracle.
 */
#include "json.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

static void check(const char *name, int ok, const char *detail) {
    printf("%s  %s %s\n", ok ? "PASS" : "FAIL", name, detail ? detail : "");
    if (!ok) fails++;
}

/* ---- helpers ------------------------------------------------------ */

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { fclose(f); return NULL; }
    for (;;) {
        if (n + 1024 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
        }
        size_t got = fread(buf + n, 1, 1024, f);
        n += got;
        if (got < 1024) break;
    }
    fclose(f);
    *len = n;
    return buf;
}

/* parse src, write with indent, compare against want */
static void vec_roundtrip(const char *name, const char *src, const char *want) {
    af_json *v = NULL;
    char err[160];
    if (af_json_parse(src, strlen(src), &v, err, sizeof err)) {
        check(name, false, err);
        return;
    }
    char *out = af_json_write(v, 2);
    int ok = out && strcmp(out, want) == 0;
    char detail[400];
    snprintf(detail, sizeof detail, "%s",
             ok ? "" : (out ? out : "(null writer)"));
    if (!ok) {
        size_t wl = strlen(want);
        snprintf(detail, sizeof detail, "\n      got  %s\n      want %s",
                 out ? out : "(null)", wl > 200 ? "(long)" : want);
    }
    check(name, ok, detail);
    free(out);
    af_json_free(v);
}

static void vec_reject(const char *name, const char *src) {
    af_json *v = NULL;
    char err[160];
    int rc = af_json_parse(src, strlen(src), &v, err, sizeof err);
    if (rc == 0) {
        af_json_free(v);
        check(name, false, "accepted but must reject");
    } else {
        check(name, 1, err);
    }
}

static void vec_accept(const char *name, const char *src) {
    af_json *v = NULL;
    char err[160];
    if (af_json_parse(src, strlen(src), &v, err, sizeof err)) {
        check(name, false, err);
        return;
    }
    af_json_free(v);
    check(name, 1, "");
}

/* a constructed value written with the given indent */
static void vec_write(const char *name, const af_json *v, int indent,
                      const char *want) {
    char *out = af_json_write(v, indent);
    int ok = out && strcmp(out, want) == 0;
    char detail[320];
    if (ok) {
        detail[0] = '\0';
    } else {
        snprintf(detail, sizeof detail, "\n      got  %s\n      want %s",
                 out ? out : "(null)", want);
    }
    check(name, ok, detail);
    free(out);
}

/* ---- vectors ------------------------------------------------------ */

static void run_vectors(void) {
    /* 1. parse + pretty write (layout, insertion order, empty containers) */
    vec_roundtrip("int scalar", "42", "42");
    vec_roundtrip("report layout",
        "{\"status\":\"FAIL\",\"asset\":\"a\",\"stage\":\"validate\",\"diagnostics\":[]}",
        "{\n"
        "  \"status\": \"FAIL\",\n"
        "  \"asset\": \"a\",\n"
        "  \"stage\": \"validate\",\n"
        "  \"diagnostics\": []\n"
        "}");
    vec_roundtrip("empty containers",
        "{\"a\":{},\"b\":[],\"c\":[[]]}",
        "{\n"
        "  \"a\": {},\n"
        "  \"b\": [],\n"
        "  \"c\": [\n"
        "    []\n"
        "  ]\n"
        "}");
    vec_roundtrip("nested layout",
        "{\"b\":1,\"a\":[1,{\"k\":\"v\"},[]]}",
        "{\n"
        "  \"b\": 1,\n"
        "  \"a\": [\n"
        "    1,\n"
        "    {\n"
        "      \"k\": \"v\"\n"
        "    },\n"
        "    []\n"
        "  ]\n"
        "}");
    vec_roundtrip("array of ints", "[1,-2,0]",
        "[\n  1,\n  -2,\n  0\n]");

    /* 2. string escaping round-trips (OUTPUT_FORMAT rule 4) */
    vec_roundtrip("escape round-trip",
        "\"q\\\" \\\\ \\n \\t \\u0001 \\u00e9 \\ud83d\\ude00\"",
        "\"q\\\" \\\\ \\n \\t \\u0001 \\u00e9 \\ud83d\\ude00\"");
    vec_roundtrip("key escaping",
        "{\"\\u00e9\":1}",
        "{\n  \"\\u00e9\": 1\n}");
    vec_roundtrip("slash not escaped", "{\"p\":\"a/b\"}",
        "{\n  \"p\": \"a/b\"\n}");
    vec_roundtrip("raw utf-8 in, escaped out",
        "\"caf\xc3\xa9 \xf0\x9f\x98\x80\"",
        "\"caf\\u00e9 \\ud83d\\ude00\"");

    /* 3. numbers (OUTPUT_FORMAT rules 5-6) */
    vec_roundtrip("big int exact", "4294967296", "4294967296");
    vec_roundtrip("int64 boundary", "-9223372036854775808",
                  "-9223372036854775808");
    vec_roundtrip("negative zero float", "-0.0", "-0.0000");
    vec_roundtrip("float four decimals", "0.6904", "0.6904");
    vec_roundtrip("float rounding", "-1.23456", "-1.2346");
    vec_roundtrip("exponent input fixed output", "1e3", "1000.0000");
    vec_roundtrip("inf formats like Python", "1e400", "inf");

    /* constructed floats: the four-decimal rule verbatim */
    af_json *f0 = af_json_float(0.0);
    af_json *f1 = af_json_float(1.0);
    af_json *f5 = af_json_float(0.5);
    af_json *f69 = af_json_float(0.6904);
    af_json *fn = af_json_float(0.0 * -1.0);
    af_json *fm = af_json_float(-1.23456);
    af_json *fb = af_json_float(1234.56789);
    vec_write("float 0.0", f0, 2, "0.0000");
    vec_write("float 1.0", f1, 2, "1.0000");
    vec_write("float 0.5", f5, 2, "0.5000");
    vec_write("float 0.6904", f69, 2, "0.6904");
    vec_write("float -0.0", fn, 2, "-0.0000");
    vec_write("float -1.23456", fm, 2, "-1.2346");
    vec_write("float 1234.56789", fb, 2, "1234.5679");
    af_json_free(f0); af_json_free(f1); af_json_free(f5);
    af_json_free(f69); af_json_free(fn); af_json_free(fm);
    af_json_free(fb);

    /* 4. compact form (message fragments, _lit) */
    vec_roundtrip("compact round-trip marker",
        "{\"a\":1,\"b\":[true,null]}",
        "{\n  \"a\": 1,\n  \"b\": [\n    true,\n    null\n  ]\n}");
    {
        af_json *v = NULL;
        char err[160];
        const char *src = "{\"a\":1,\"b\":[true,null],\"e\":{},\"l\":[]}";
        if (af_json_parse(src, strlen(src), &v, err, sizeof err) == 0) {
            vec_write("compact object", v, -1,
                      "{\"a\": 1, \"b\": [true, null], \"e\": {}, \"l\": []}");
            af_json_free(v);
        } else {
            check("compact object", 0, err);
        }
        const char *s2 = "[]";
        if (af_json_parse(s2, strlen(s2), &v, err, sizeof err) == 0) {
            vec_write("compact empty array", v, -1, "[]");
            af_json_free(v);
        }
        const char *s3 = "{}";
        if (af_json_parse(s3, strlen(s3), &v, err, sizeof err) == 0) {
            vec_write("compact empty object", v, -1, "{}");
            af_json_free(v);
        }
    }

    /* 5. accept vectors */
    vec_accept("scalars", "null");
    vec_accept("true", "true");
    vec_accept("false", "false");
    vec_accept("empty string", "\"\"");
    vec_accept("lone surrogate escape", "\"\\ud800\"");
    vec_accept("valid pair", "\"\\ud83d\\ude00\"");
    vec_accept("leading plus in exponent", "1E+2");
    {
        /* deep nesting just under the parser's depth cap: 300 levels */
        char deep[1100];
        size_t k = 0;
        memset(deep, '[', 300); k = 300;
        deep[k++] = '1';
        memset(deep + k, ']', 300); k += 300;
        deep[k] = '\0';
        vec_accept("deep nesting under cap", deep);
        deep[0] = '['; /* reuse: one level over the cap must fail */
        memset(deep, '[', 513);
        memset(deep + 513, ']', 513);
        deep[1026] = '\0';
        vec_reject("nesting over cap", deep);
    }

    /* 6. reject vectors (parser contract) */
    vec_reject("duplicate key", "{\"a\":1,\"a\":2}");
    vec_reject("NaN token", "NaN");
    vec_reject("Infinity token", "Infinity");
    vec_reject("-Infinity token", "-Infinity");
    vec_reject("trailing comma array", "[1,]");
    vec_reject("trailing comma object", "{\"a\":1,}");
    vec_reject("missing comma", "[1 2]");
    vec_reject("missing colon", "{\"a\" 1}");
    vec_reject("single quotes", "{'a':1}");
    vec_reject("leading zero", "01");
    vec_reject("plus sign", "+1");
    vec_reject("bare fraction", ".5");
    vec_reject("trailing dot", "5.");
    vec_reject("bare exponent", "1e");
    vec_reject("empty input", "");
    vec_reject("whitespace only", "   \n");
    vec_reject("trailing garbage", "1 2");
    vec_reject("line comment", "// c\n1");
    vec_reject("block comment", "/* c */1");
    vec_reject("non-string key", "{1:2}");
    vec_reject("unknown escape", "\"a\\x\"");
    vec_reject("unterminated string", "\"abc");
    vec_reject("raw tab in string", "\"a\tb\"");
    vec_reject("int64 overflow", "9223372036854775808");
    vec_reject("int64 underflow", "-9223372036854775809");
    vec_reject("bad utf-8 byte", "\"\xff\"");
    vec_reject("BOM", "\xef\xbb\xbf{}");
    vec_reject("tru", "tru");
    vec_reject("nul", "nul");

    printf("%s\n", fails ? "FAILURES" : "ALL GREEN");
}

/* ---- CLI ---------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc == 1) {
        run_vectors();
        return fails ? 1 : 0;
    }
    if (argc == 3 && strcmp(argv[1], "--parse-check") == 0) {
        size_t len;
        char *buf = read_file(argv[2], &len);
        if (!buf) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
        af_json *v = NULL;
        char err[160];
        int rc = af_json_parse(buf, len, &v, err, sizeof err);
        if (rc == 0) af_json_free(v);
        else fprintf(stderr, "%s: %s\n", argv[2], err);
        free(buf);
        return rc ? 1 : 0;
    }
    if ((strcmp(argv[1], "--rewrite") == 0 ||
         strcmp(argv[1], "--compact") == 0) && argc == 3) {
        size_t len;
        char *buf = read_file(argv[2], &len);
        if (!buf) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
        af_json *v = NULL;
        char err[160];
        if (af_json_parse(buf, len, &v, err, sizeof err)) {
            fprintf(stderr, "%s: %s\n", argv[2], err);
            free(buf);
            return 1;
        }
        free(buf);
        char *out = af_json_write(v, argv[1][2] == 'r' ? 2 : -1);
        af_json_free(v);
        if (!out) { fprintf(stderr, "write failed\n"); return 1; }
        fputs(out, stdout);
        free(out);
        return 0;
    }
    fprintf(stderr, "usage: %s [--parse-check|--rewrite|--compact FILE]\n",
            argv[0]);
    return 2;
}
