/* The zkml writer: langc zkml (docs/PLAN.md 5.3, 5.4). */
#ifndef ZKML_H
#define ZKML_H

#include "front/core.h"

/* zkml_command evaluates NAME ARGS k for k = 0 to COUNT - 1 under the
   Poseidon2 binding and writes a zkml-lang program to OUT_PATH. Exit 0, 1
   after a diagnostic, or 2 after a USAGE or IO diagnostic. */
int zkml_command(Machine *m, const char *name, char *const *args, int arg_count, unsigned count,
                 const char *out_path);

#endif
