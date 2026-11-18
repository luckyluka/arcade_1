/* json.h -- bundled JSON value model, parser and writer (M4.1a).
 *
 * No external libraries (M4 contract): this is the JSON layer the C
 * compiler is built on. Contracts:
 *
 *   parser  arcforge/docs/ARCHITECTURE.md -- key order preserved
 *           (objects are ordered arrays of pairs), duplicate keys
 *           REJECTED, integers outside int64 REJECTED, the bare tokens
 *           NaN/Infinity/-Infinity REJECTED (not JSON, P0.5), strict
 *           UTF-8 input, depth cap.
 *   writer  arcforge/docs/OUTPUT_FORMAT.md -- the normative byte rules:
 *           2-space indent (or the compact form for message fragments),
 *           insertion order, ensure_ascii escaping (lowercase hex,
 *           surrogate pairs), integers plain, floats "%.4f".
 *
 * The Python reference's dump_json()/load_json() are the oracle;
 * tests/test_c_json.py compares this code against them byte-for-byte
 * over the whole corpus.
 */
#ifndef ARCFORGE_JSON_H
#define ARCFORGE_JSON_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AF_JSON_NULL = 0,
    AF_JSON_BOOL,
    AF_JSON_INT,
    AF_JSON_FLOAT,
    AF_JSON_STRING,
    AF_JSON_ARRAY,
    AF_JSON_OBJECT
} af_json_type;

/* A node. Objects hold 2n entries in af_json **pairs: key (a STRING
 * node) then value, in document order. Arrays hold n items. */
typedef struct af_json {
    af_json_type type;
    int b;                 /* AF_JSON_BOOL   */
    int64_t i;             /* AF_JSON_INT    */
    double d;              /* AF_JSON_FLOAT  */
    char *s;               /* AF_JSON_STRING: UTF-8, NUL-terminated */
    size_t slen;           /* length of s in bytes (excluding NUL)  */
    struct af_json **items;/* AF_JSON_ARRAY items, AF_JSON_OBJECT: key/value pairs */
    size_t n;              /* items / pairs (pairs = 2n entries)     */
    size_t cap;
} af_json;

/* Parse text[0..len). Returns 0 and stores a heap-allocated tree in
 * *out, or -1 with a message in err ("offset N: reason"). Any non-JSON
 * construct fails: trailing garbage, comments, trailing commas, leading
 * zeros, lone +/-, bad escapes, invalid UTF-8, int64 overflow. */
int af_json_parse(const char *text, size_t len, af_json **out,
                  char *err, size_t errsz);

/* Serialise per OUTPUT_FORMAT.md. indent=2 is the report form;
 * indent=-1 is the compact form for message fragments (", " between
 * items, ": " between key and value). Returns a heap-allocated
 * NUL-terminated string (no trailing newline -- sinks add that), or
 * NULL on out-of-memory or invalid UTF-8 in a string. */
char *af_json_write(const af_json *v, int indent);

void af_json_free(af_json *v);

/* constructors (tests, and later the compiler's own emitters) */
af_json *af_json_null(void);
af_json *af_json_bool(int b);
af_json *af_json_int(int64_t i);
af_json *af_json_float(double d);
af_json *af_json_str(const char *utf8);            /* strlen(utf8) */
af_json *af_json_strn(const char *utf8, size_t n);
af_json *af_json_arr(void);
af_json *af_json_obj(void);
int af_json_arr_push(af_json *a, af_json *v);      /* 0 ok, -1 OOM */
int af_json_obj_put(af_json *o, const char *key, af_json *v);

/* first value for key, or NULL (objects never hold duplicates) */
const af_json *af_json_get(const af_json *obj, const char *key);

#endif /* ARCFORGE_JSON_H */
