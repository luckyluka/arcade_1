/* schema.h -- M4.1b: hand-coded, table-driven JSON Schema validation.
 *
 * The oracle is tools/arcforge_ref.py::schema_diags, which runs jsonschema
 * 4.10.3 (Draft 2020-12) for structure and then rewords every failure
 * itself; this layer reproduces the resulting diagnostics BYTE-FOR-BYTE
 * with no external library (M4 contract). See schema.c for the pinned
 * semantics and the supported-keyword boundary.
 *
 * No machine behaviour changes: everything here is host-side tooling.
 */
#ifndef ARCFORGE_SCHEMA_H
#define ARCFORGE_SCHEMA_H

#include <stddef.h>

#include "json.h"

/* Validate `doc` against `schema_root` and return the diagnostics array
 * that Python's schema_diags(doc, <name>, prefix) would print: jsonschema
 * structural keywords + the P0.2 format vocabulary walk + the P0.3 int32
 * range walk, sorted by (path, kind, message) and deduplicated by
 * (code, path, message). `prefix` is the pointer the document sits at (a
 * registry file path, or "").
 *
 * Returns a heap-allocated JSON array (caller frees), or NULL with err[]
 * filled: a $ref the schemas cannot resolve (Python raises) or OOM. */
af_json *af_schema_diags(const af_json *doc, const af_json *schema_root,
                         const char *prefix, char *err, size_t errsz);

/* One registry file: ROOT-relative path + parsed document. */
typedef struct af_reg_entry {
    char *rel;    /* "registry/recipes/arms/sword.recipe.json" */
    af_json *doc; /* owned */
} af_reg_entry;

/* The registry, in load_registry() order: each group sorted by path
 * (Python's sorted(Path) == byte order of the relative path). */
typedef struct af_registry {
    af_reg_entry *archetypes;
    size_t n_archetypes;
    af_reg_entry *recipes;
    size_t n_recipes;
    af_reg_entry *styles;
    size_t n_styles;
} af_registry;

/* Read reg_dir ("registry") with the three globs of load_registry():
 * archetypes/ + "*.archetype.json", recipes/ + "**" + ".recipe.json",
 * styles/ + "*.style.json" (one level / any depth / one level).
 * Returns NULL with err[] filled if a directory cannot be read or a file
 * cannot be parsed / has no name field (Python raises in that case). */
af_registry *af_registry_load(const char *reg_dir, char *err, size_t errsz);
void af_registry_free(af_registry *r);

/* The load_registry() view: {"archetypes": {name: doc}, ...}, keys in file
 * order, a repeated name overwrites the value in place (Python dict). The
 * documents are clones; the caller frees the result. */
af_json *af_registry_json(const af_registry *r, char *err, size_t errsz);

#endif /* ARCFORGE_SCHEMA_H */
