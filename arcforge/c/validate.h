/* validate.h -- the `arcforge validate` command (M4.1c). */
#ifndef ARCFORGE_VALIDATE_H
#define ARCFORGE_VALIDATE_H

#include "json.h"

/* Validate an asset document. Returns the report node (owned by the
 * caller) to be printed with print_report(), or NULL when the oracle
 * would traceback: exit 1 with an empty stdout. */
af_json *af_validate(const char *asset_path, const char *argv0);

/* af_compile_asset(): validate plus the compiled IR (M4.2). *irs_out
 * receives one af_json IR per expanded document, in document order, or
 * NULL when any document could not produce one (invalid input, crash) --
 * the report itself is af_validate's, byte for byte. Pass irs_out = NULL
 * for plain validate. The caller owns both. */
af_json *af_compile_asset(const char *asset_path, const char *argv0,
                          af_json **irs_out);

/* Why af_validate() returned NULL (for a stderr note; never printed on
 * stdout). Valid until the next af_validate() call. */
const char *af_validate_why(void);

#endif
