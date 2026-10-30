/* Exercise rebuild with a clean version fixture, including literal marker paths. */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define main langc_main
#include "../src/main.c"
#undef main

#include <stdlib.h>
#include <unistd.h>

const char langc_commit[] = "0000000000000000000000000000000000000000";
const int langc_dirty = 0;

static int checks = 0;

static int rebuild_case(const char *path, int want, const char *code) {
  Options opt;
  Arena arena;
  Diag diag;
  memset(&opt, 0, sizeof opt);
  opt.prog_path = path;
  arena_init(&arena, ARENA_LIMIT_BYTES);
  diag_init(&diag);
  int status = run_rebuild(&opt, &arena, &diag);
  int ok = status == want && (code == NULL || (diag.code != NULL && strcmp(code, diag.code) == 0));
  checks++;
  if (!ok) fprintf(stderr, "FAIL rebuild %s: want %d %s, got %d %s\n", path, want,
                   code == NULL ? "success" : code, status, diag.code == NULL ? "success" : diag.code);
  arena_release(&arena);
  return ok;
}

static int put_source(const char *text) {
  FILE *out = fopen("<domain>", "wb");
  if (out == NULL) return 0;
  size_t size = strlen(text);
  int ok = fwrite(text, 1, size, out) == size;
  return fclose(out) == 0 && ok;
}

int main(void) {
  char dir[] = "/tmp/leaf-rebuild-review.XXXXXX";
  char *cwd = getcwd(NULL, 0);
  if (cwd == NULL || mkdtemp(dir) == NULL || chdir(dir) != 0) { free(cwd); return 1; }
  char *build[] = {"langc", "build", "<domain>", "-o", "review.wasm"};
  char *verifier[] = {"langc", "verifier", "--height", "0", "--pk", "<domain>", "-o", "review.hex"};
  char *signer[] = {"langc", "signer", "--height", "0", "-o", "signer.wasm"};
  char *zkml[] = {"langc", "zkml", "--count", "1", "<domain>", "entry", "-o", "review.zkml"};
  int ok = put_source("entry : ℕ\nentry = 7\n") && langc_main(5, build) == 0;
  if (ok) ok = rebuild_case("review.wasm.manifest.json", 0, NULL) && ok;
  if (put_source("-- changed source, same output\nentry : ℕ\nentry = 7\n"))
    ok = rebuild_case("review.wasm.manifest.json", 1, "REFUSE_REBUILD_SOURCE") && ok;
  else ok = 0;
  unsigned char pk[72] = {0};
  FILE *key = fopen("<domain>", "wb");
  int key_ok = key != NULL;
  if (key != NULL) { key_ok = fwrite(pk, 1, sizeof pk, key) == sizeof pk; key_ok = fclose(key) == 0 && key_ok; }
  if (key_ok && langc_main(8, verifier) == 0)
    ok = rebuild_case("review.hex.manifest.json", 0, NULL) && ok;
  else ok = 0;
  if (langc_main(6, signer) == 0)
    ok = rebuild_case("signer.wasm.manifest.json", 0, NULL) && ok;
  else ok = 0;
  const char *source =
    "value : Signer\n"
    "value = pack (pack (digest 0 0 0 0) (pack (digest 0 0 0 0) (v1Params 0))) "
    "(pack (digest 0 0 0 0) (pack 0 (pack (digest 0 0 0 0) "
    "(pack (λ i → digest 0 0 0 0) (λ i → digest 0 0 0 0)))))\n\n"
    "entry : ℕ → Signer\nentry k = value\n";
  if (put_source(source) && langc_main(8, zkml) == 0)
    ok = rebuild_case("review.zkml.manifest.json", 0, NULL) && ok;
  else ok = 0;
  const char *files[] = {"<domain>", "review.wasm", "review.wasm.manifest.json", "review.hex",
    "review.hex.manifest.json", "signer.wasm", "signer.wasm.manifest.json", "review.zkml", "review.zkml.manifest.json"};
  for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) unlink(files[i]);
  if (chdir(cwd) != 0 || rmdir(dir) != 0) ok = 0;
  free(cwd);
  if (ok && checks == 5) puts("rebuild regressions: 5 passed");
  return ok && checks == 5 ? 0 : 1;
}
