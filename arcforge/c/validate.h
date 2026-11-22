/* validate.h -- the `arcforge validate` command (M4.1c). */
#ifndef ARCFORGE_VALIDATE_H
#define ARCFORGE_VALIDATE_H

#include "json.h"

/* Validate an asset document. Returns the report node (owned by the
 * caller) to be printed with print_report(), or NULL when the oracle
 * would traceback: exit 1 with an empty stdout. */
af_json *af_validate(const char *asset_path, const char *argv0);

/* Why af_validate() returned NULL (for a stderr note; never printed on
 * stdout). Valid until the next af_validate() call. */
const char *af_validate_why(void);

#endif
