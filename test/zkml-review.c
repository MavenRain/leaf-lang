/* Exercise the writer through the same checked declarations as the CLI. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "front/check.h"
#include "front/front.h"
#include "zkml.h"

static const char source[] =
  "value : Signer\n"
  "value = pack (pack (digest 0 0 0 0) (pack (digest 0 0 0 0) (v1Params 0))) "
  "(pack (digest 0 0 0 0) (pack 0 (pack (digest 0 0 0 0) "
  "(pack (λ i → digest 0 0 0 0) (λ i → digest 0 0 0 0)))))\n\n"
  "valid : ℕ → ℕ → Signer\nvalid n k = value\n\n"
  "boolArg : Bool → ℕ → Signer\nboolArg b k = value\n\n"
  "boolIndex : Bool → Signer\nboolIndex b = value\n\n"
  "finIndex : Fin 0 → Signer\nfinIndex i = value\n\n"
  "wrongResult : ℕ → ℕ\nwrongResult k = k\n";

/* The writer writes into memory (S8 D2j). A refusal writes no byte: main
   writes OUT only after exit 0. */
static int writer_case(const char *name, int argc, int want) {
  Arena arena;
  Diag diag;
  Front front;
  Machine machine;
  char *args[] = {"5", "6"};
  char *data = NULL;
  size_t size = 0;
  int ok;
  arena_init(&arena, (size_t)1 << 28);
  diag_init(&diag);
  ok = front_load(&arena, "zkml-review", source, strlen(source), &front, &diag)
    && check_program(&arena, &front.decls, &machine, &diag);
  if (ok) {
    FILE *zk_out = open_memstream(&data, &size);
    int rc = zk_out == NULL ? 2 : zkml_command(&machine, name, args, argc, 2, zk_out);
    if (zk_out != NULL && fclose(zk_out) != 0) rc = 2;
    ok = zk_out != NULL && rc == want && (want == 0 || (diag.code != NULL && strcmp(diag.code, "USAGE") == 0));
    ok = ok && (want == 0 ? size > 8 && strncmp(data, "-- Generated", 12) == 0 : size == 0);
  }
  if (!ok) fprintf(stderr, "FAIL zkml writer %s argc %d: %s\n", name, argc, diag.code == NULL ? "unexpected result" : diag.code);
  arena_release(&arena);
  free(data);
  return ok;
}

int main(void) {
  int ok = writer_case("valid", 1, 0);
  ok &= writer_case("boolArg", 1, 2);
  ok &= writer_case("boolIndex", 0, 2);
  ok &= writer_case("finIndex", 0, 2);
  ok &= writer_case("wrongResult", 0, 2);
  ok &= writer_case("valid", 0, 2);
  ok &= writer_case("valid", 2, 2);
  if (ok) puts("zkml writer regressions: 7 passed");
  return ok ? 0 : 1;
}
