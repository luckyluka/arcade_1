/* px.h -- M4.1c: the `.px` fragment parser (spec-next 02 §G).
 *
 * The oracle is tools/arcforge_ref.py::parse_px (v1 and v2). This layer
 * reproduces its accept set and its PxError texts byte-for-byte:
 *
 *   failures   PxError(line, msg, code): code is E_PX_PARSE for the file
 *              grammar and E_PX_FRAMES for the frame protocol; the caller
 *              reports `"{file} line {line}: {msg}"` at the shape's
 *              /file (or a custom fragment's /file).
 *   crash      An uncaught Python exception -- read_text() on bytes that
 *              are not valid UTF-8 (UnicodeDecodeError), or a signed
 *              anchor coordinate Python's guard lets through and int()
 *              then rejects (`anchor a --5 3`) -- means the oracle exits
 *              1 with EMPTY stdout; af_px_error.crash says so and the
 *              caller must do the same.
 *
 * No external libraries (M4 contract). Line splitting follows
 * str.splitlines() for the ASCII boundaries (\r\n, \r, \n, \v, \f,
 * \x1c-\x1f); the Unicode ones (\x85, \u2028, \u2029) are the documented
 * non-ASCII divergence.
 */
#ifndef ARCFORGE_PX_H
#define ARCFORGE_PX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int crash;              /* 1: the oracle exits 1 with no report */
    int line;               /* PxError.line (meaningless when crash)   */
    char code[24];          /* E_PX_PARSE (default) / E_PX_FRAMES      */
    char msg[512];          /* PxError.msg                             */
} af_px_error;

typedef struct {
    char *name;             /* `name X`, NULL when absent             */
    int64_t w, h;           /* `size W H` (required)                  */
    int version;            /* 1 or 2                                  */
    int64_t fps;            /* v2 `fps N`                              */
    int has_fps;
    int frame_count;        /* 1 for v1, declared N for v2            */
    char **leg_ch;          /* legend keys in file order              */
    char **leg_val;
    size_t nleg;
    char **anc_name;        /* anchors in file order                  */
    int64_t *anc_x, *anc_y;
    size_t nanc;
    char ***frame_rows;     /* frame_count groups of row strings       */
    size_t *frame_nrows;
} af_px;

/* Parse a whole decoded .px file. 0 on success; -1 with *err filled
 * (err->crash for an uncaught-exception mirror). */
int af_parse_px(const char *text, size_t len, af_px **out, af_px_error *err);

/* Read `path` (must already exist -- the caller reported E_PX_NOT_FOUND)
 * and parse it; undecodable bytes are err->crash, mirroring read_text(). */
int af_px_read(const char *path, af_px **out, af_px_error *err);

void af_px_free(af_px *px);

#endif /* ARCFORGE_PX_H */
