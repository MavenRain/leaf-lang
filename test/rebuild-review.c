/* Exercise rebuild with a clean version fixture, including literal marker paths.
   S8 D7: the clean-tree rows (exit 0, SOURCE, WRITER, OUTPUT) run here on all trees. */
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

/* Copy FROM to TO with one character changed: the first character after
   MARK2, which comes after MARK1. A '0' becomes '1'; another character
   becomes '0'. */
static int copy_flip(const char *from, const char *to, const char *mark1, const char *mark2) {
  char text[8192];
  FILE *in = fopen(from, "rb");
  if (in == NULL) return 0;
  size_t size = fread(text, 1, sizeof text - 1, in);
  int ok = ferror(in) == 0 && feof(in) != 0;
  fclose(in);
  text[size] = '\0';
  char *at = ok ? strstr(text, mark1) : NULL;
  at = at == NULL ? NULL : strstr(at, mark2);
  if (at == NULL) return 0;
  at += strlen(mark2);
  if (*at == '\0') return 0;
  *at = *at == '0' ? '1' : '0';
  FILE *out = fopen(to, "wb");
  if (out == NULL) return 0;
  ok = fwrite(text, 1, size, out) == size;
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
  /* S8 D7: the writer of a changed entry word fails (WRITER). A changed
     output hash and a changed output file are refused (OUTPUT). */
  if (copy_flip("review.zkml.manifest.json", "writer.json", "\"command\":[", "\"<domain>\",\""))
    ok = rebuild_case("writer.json", 1, "REFUSE_REBUILD_WRITER") && ok;
  else ok = 0;
  if (copy_flip("review.zkml.manifest.json", "output.json", "\"output\":{", "\"keccak256\":\"0x"))
    ok = rebuild_case("output.json", 1, "REFUSE_REBUILD_OUTPUT") && ok;
  else ok = 0;
  FILE *grow = fopen("review.zkml", "ab");
  int grow_ok = grow != NULL && fputc('\n', grow) != EOF;
  if (grow != NULL) grow_ok = fclose(grow) == 0 && grow_ok;
  if (grow_ok) ok = rebuild_case("review.zkml.manifest.json", 1, "REFUSE_REBUILD_OUTPUT") && ok;
  else ok = 0;
  const char *files[] = {"<domain>", "review.wasm", "review.wasm.manifest.json", "review.hex",
    "review.hex.manifest.json", "signer.wasm", "signer.wasm.manifest.json", "review.zkml", "review.zkml.manifest.json",
    "writer.json", "output.json"};
  for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) unlink(files[i]);
  if (chdir(cwd) != 0 || rmdir(dir) != 0) ok = 0;
  free(cwd);
  if (ok && checks == 8) puts("rebuild regressions: 8 passed");
  return ok && checks == 8 ? 0 : 1;
}
