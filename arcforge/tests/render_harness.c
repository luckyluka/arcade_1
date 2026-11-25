/* render_harness.c -- the M4.2 differential driver for c/raster.c.
 *
 * Usage: arcforge_render_harness ASSET.json
 *
 * Validates + compiles the asset with af_compile_asset(), renders every
 * frame of every expanded document with af_render_all() and prints
 *
 *   DOC <id>
 *   CELL <W> <H>
 *   FRAME <name>/<i> <sha64> clipped=<n>
 *
 * <id> is IR.asset.asset.id ("?" when absent, like asset_id), <i> the
 * frame's index inside its animation-name group -- the group order is
 * first-seen, which is the oracle's dict insertion order in render_all()
 * ("base" first, then each animation in document order), and the index
 * counts frames of that name in encounter order, exactly
 * `for name, fs in renders.items() for i, f in enumerate(fs)`.
 *
 * The whole output is buffered: stdout is written only when everything
 * succeeded. Any failure -- invalid input, an uncaught oracle exception,
 * an empty IR list -- exits 1 with an EMPTY stdout. The observable
 * contract is (exit code, stdout bytes); never a message.
 *
 * Host cc, C11, no external libraries (M4 contract): ARCFORGE_LIB
 * (json, schema, expr, sim, px, validate, raster, sha256).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raster.h"
#include "sha256.h"
#include "validate.h"

/* ---- tiny string builder ---------------------------------------------- */
typedef struct { char *b; size_t n, cap; } sb;

static void sb_init(sb *s) { s->b = NULL; s->n = s->cap = 0; }

static int sb_putn(sb *s, const char *p, size_t k) {
    if (s->n + k + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 64;
        while (nc < s->n + k + 1) nc *= 2;
        char *nb = realloc(s->b, nc);
        if (!nb) return -1;
        s->b = nb;
        s->cap = nc;
    }
    memcpy(s->b + s->n, p, k);
    s->n += k;
    s->b[s->n] = 0;
    return 0;
}

static int sb_puts(sb *s, const char *p) { return sb_putn(s, p, strlen(p)); }

/* ---- one FRAME line ---------------------------------------------------- */
static int emit_frame(sb *out, const af_renders *r, size_t k, size_t idx) {
    char buf[96];
    const af_frame *f = &r->frames[k];
    char hex[65];
    size_t sz = (size_t)r->W * (size_t)r->H * 4;

    af_sha256_hex(f->rgba, sz, hex);
    if (sb_puts(out, "FRAME ") || sb_puts(out, f->name)) return -1;
    snprintf(buf, sizeof buf, "/%zu %s clipped=%lld\n", idx, hex,
             (long long)f->clipped);
    return sb_puts(out, buf);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s ASSET.json\n", argv[0]);
        return 2;
    }

    af_json *report = NULL;
    af_json *irs = NULL;
    report = af_compile_asset(argv[1], argv[0], &irs);
    if (!report || !irs || irs->n == 0) {   /* the oracle's failure shape */
        af_json_free(report);
        af_json_free(irs);
        return 1;                           /* empty stdout */
    }
    af_json_free(report);

    sb out;
    sb_init(&out);
    int bad = 0;

    for (size_t di = 0; di < irs->n && !bad; di++) {
        af_json *ir = irs->items[di];
        if (!ir || ir->type != AF_JSON_OBJECT) { bad = 1; break; }

        /* DOC <id> = IR.asset.asset.id, "?" when absent (asset_id) */
        const af_json *a = af_json_get(ir, "asset");
        const af_json *ad = a ? af_json_get(a, "asset") : NULL;
        const af_json *idn = ad ? af_json_get(ad, "id") : NULL;
        const char *id = (idn && idn->type == AF_JSON_STRING) ? idn->s : "?";
        if (sb_puts(&out, "DOC ") || sb_puts(&out, id) ||
            sb_puts(&out, "\n")) { bad = 1; break; }

        af_renders *r = NULL;
        if (af_render_all(ir, &r)) {
            fprintf(stderr, "render_harness: %s\n", af_render_why());
            bad = 1;
            break;
        }
        char buf[96];
        snprintf(buf, sizeof buf, "CELL %lld %lld\n",
                 (long long)r->W, (long long)r->H);
        if (sb_puts(&out, buf)) { af_renders_free(r); bad = 1; break; }

        /* name groups in first-seen order == the oracle's dict insertion
         * order; the index counts that name's frames in encounter order */
        size_t *first = malloc((r->nframes ? r->nframes : 1) * sizeof *first);
        if (!first) { af_renders_free(r); bad = 1; break; }
        size_t ng = 0;
        for (size_t k = 0; k < r->nframes; k++) {
            int seen = 0;
            for (size_t g = 0; g < ng; g++)
                if (!strcmp(r->frames[first[g]].name, r->frames[k].name)) {
                    seen = 1;
                    break;
                }
            if (!seen) first[ng++] = k;
        }
        for (size_t g = 0; g < ng && !bad; g++) {
            const char *nm = r->frames[first[g]].name;
            size_t idx = 0;
            for (size_t k = 0; k < r->nframes; k++) {
                if (strcmp(r->frames[k].name, nm)) continue;
                if (emit_frame(&out, r, k, idx)) { bad = 1; break; }
                idx++;
            }
        }
        free(first);
        af_renders_free(r);
    }

    af_json_free(irs);
    if (bad) {                              /* nothing on stdout */
        free(out.b);
        return 1;
    }
    fwrite(out.b, 1, out.n, stdout);
    free(out.b);
    return ferror(stdout) ? 2 : 0;
}
