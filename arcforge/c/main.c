/* main.c -- the `arcforge` C CLI (M4.1b + M4.1c).
 *
 * Three entry points, all byte-compatible with the Python oracle
 * (arcforge AGENTS rule 8: Python stays the oracle until M4.3 flips the
 * gate; tests/test_schema_parity.py + test_validate_parity.py prove the
 * parity):
 *
 *   arcforge validate ASSET.json
 *       The oracle's cmd_validate: the full non-raster validate pipeline
 *       (extends/variants, compile_asset, recipe/style docs) printed as
 *       dump_json(report, indent=2) plus a newline; exit 0 on PASS, 1 on
 *       FAIL. An oracle traceback is an exit 1 with EMPTY stdout: only a
 *       stderr note is added.
 *   arcforge --schema-check JOBS.json
 *       JOBS is an array of {schema, base, file} or {schema, base, doc}
 *       jobs. `schema` names a file under schema/, `base` is the pointer
 *       prefix the document sits at (the oracle's `prefix` argument), and
 *       `file` reads the document from disk while `doc` embeds it. The
 *       output is an array with one diagnostics array per job, written
 *       with indent=2 plus one trailing newline (OUTPUT_FORMAT.md).
 *   arcforge --registry-list
 *       The load_registry() view -- {"archetypes": {...}, ...} -- written
 *       the same way.
 *
 * Diagnostics are data, not failures: a job whose document violates its
 * schema still exits 0. Exit 2 with a message on stderr means the tool
 * itself could not run (unreadable/unparsable file, bad job shape, OOM);
 * for `validate`, usage errors are exit 2 as well.
 *
 * --schema-check/--registry-list paths are relative to the current
 * directory; `validate` locates the arcforge root from its own binary
 * (find_root) like the oracle does from its script path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "schema.h"
#include "validate.h"

/* Read a whole file. Returns 0 with a heap buffer + length, or -1 with
 * err[] filled. */
static int slurp(const char *path, char **out, size_t *len, char *err,
                 size_t errsz) {
    *out = NULL;
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errsz, "cannot open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, errsz, "cannot read %s", path);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        snprintf(err, errsz, "cannot read %s", path);
        return -1;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        snprintf(err, errsz, "out of memory");
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    *len = got;
    return 0;
}

/* Parse a file into a tree (heap). Returns 0 or -1 with err[] filled. */
static int load_file(const char *path, af_json **out, char *err,
                     size_t errsz) {
    *out = NULL;
    char *text;
    size_t len;
    if (slurp(path, &text, &len, err, errsz)) return -1;
    int rc = af_json_parse(text, len, out, err, errsz);
    free(text);
    return rc;
}

/* Write the report: indent=2, one trailing newline, nothing else. */
static int print_report(const af_json *report) {
    char *text = af_json_write(report, 2);
    if (!text) {
        fprintf(stderr, "arcforge: out of memory\n");
        return 2;
    }
    fputs(text, stdout);
    fputc('\n', stdout);
    free(text);
    return 0;
}

static int cmd_schema_check(const char *jobs_path) {
    char err[512];
    af_json *jobs = NULL;
    if (load_file(jobs_path, &jobs, err, sizeof err)) {
        fprintf(stderr, "arcforge: %s\n", err);
        return 2;
    }
    if (!jobs || jobs->type != AF_JSON_ARRAY) {
        fprintf(stderr, "arcforge: %s: jobs must be a JSON array\n", jobs_path);
        af_json_free(jobs);
        return 2;
    }

    af_json *report = af_json_arr();
    if (!report) {
        fprintf(stderr, "arcforge: out of memory\n");
        af_json_free(jobs);
        return 2;
    }

    for (size_t i = 0; i < jobs->n; i++) {
        const af_json *job = jobs->items[i];
        if (!job || job->type != AF_JSON_OBJECT) {
            fprintf(stderr, "arcforge: job %zu is not an object\n", i);
            goto fail;
        }
        const af_json *schema = af_json_get(job, "schema");
        const af_json *base = af_json_get(job, "base");
        if (!schema || schema->type != AF_JSON_STRING) {
            fprintf(stderr, "arcforge: job %zu has no string `schema`\n", i);
            goto fail;
        }
        const char *prefix =
            (base && base->type == AF_JSON_STRING) ? base->s : "";

        /* the schema document: schema/<name>, loaded per job (the oracle
         * does the same; the corpus is small enough that caching would
         * only complicate ownership) */
        char spath[1024];
        snprintf(spath, sizeof spath, "schema/%s", schema->s);
        af_json *root = NULL;
        if (load_file(spath, &root, err, sizeof err)) {
            fprintf(stderr, "arcforge: job %zu: %s\n", i, err);
            goto fail;
        }

        /* the instance document: embedded `doc` or a `file` to read */
        const af_json *file = af_json_get(job, "file");
        const af_json *doc = af_json_get(job, "doc");
        af_json *loaded = NULL;
        const af_json *inst;
        if (file && file->type == AF_JSON_STRING) {
            if (load_file(file->s, &loaded, err, sizeof err)) {
                fprintf(stderr, "arcforge: job %zu: %s\n", i, err);
                af_json_free(root);
                goto fail;
            }
            inst = loaded;
        } else if (doc) {
            inst = doc;
        } else {
            fprintf(stderr, "arcforge: job %zu needs `file` or `doc`\n", i);
            af_json_free(root);
            goto fail;
        }

        af_json *diags = af_schema_diags(inst, root, prefix, err, sizeof err);
        af_json_free(loaded);
        af_json_free(root);
        if (!diags) {
            fprintf(stderr, "arcforge: job %zu: %s\n", i, err);
            goto fail;
        }
        if (af_json_arr_push(report, diags)) {
            af_json_free(diags);
            fprintf(stderr, "arcforge: out of memory\n");
            goto fail;
        }
    }

    af_json_free(jobs);
    int rc = print_report(report);
    af_json_free(report);
    return rc;

fail:
    af_json_free(report);
    af_json_free(jobs);
    return 2;
}

static int cmd_registry_list(void) {
    char err[512];
    af_registry *reg = af_registry_load("registry", err, sizeof err);
    if (!reg) {
        fprintf(stderr, "arcforge: %s\n", err);
        return 2;
    }
    af_json *view = af_registry_json(reg, err, sizeof err);
    af_registry_free(reg);
    if (!view) {
        fprintf(stderr, "arcforge: %s\n", err);
        return 2;
    }
    int rc = print_report(view);
    af_json_free(view);
    return rc;
}

/* arcforge validate ASSET.json -- the oracle's cmd_validate (M4.1c).
 * The report goes to stdout exactly like print(dump_json(report, 2));
 * exit 0 on PASS, 1 on FAIL. A traceback in the oracle is an exit 1
 * with an EMPTY stdout: af_validate() returns NULL and we print only a
 * stderr note. OOM while writing the report is exit 1 too. */
static int cmd_validate(const char *asset, const char *argv0) {
    af_json *report = af_validate(asset, argv0);
    if (!report) {
        const char *why = af_validate_why();
        fprintf(stderr, "arcforge: validate: %s\n", why && why[0] ? why : "error");
        return 1;
    }
    const af_json *st = af_json_get(report, "status");
    int pass = st && st->type == AF_JSON_STRING && strcmp(st->s, "PASS") == 0;
    int pr = print_report(report);
    af_json_free(report);
    if (pr != 0) return 1;
    return pass ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--schema-check") == 0)
        return cmd_schema_check(argv[2]);
    if (argc == 2 && strcmp(argv[1], "--registry-list") == 0)
        return cmd_registry_list();
    if (argc == 3 && strcmp(argv[1], "validate") == 0)
        return cmd_validate(argv[2], argv[0]);
    fprintf(stderr,
            "usage: arcforge validate ASSET.json\n"
            "       arcforge --schema-check JOBS.json\n"
            "       arcforge --registry-list\n");
    return 2;
}
