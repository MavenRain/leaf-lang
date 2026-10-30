/* The langc command line. SHARED by the tcc kits.
   Exit 0: ok. Exit 1: the program is refused. Exit 2: usage or IO. */
#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/fmt.h"
#include "front/front.h"
#include "front/lower.h"
#include "front/parser.h"
#include "target.h"
#include "zkml.h"

#define ARENA_LIMIT_BYTES ((size_t)1 << 30)

typedef enum {
  CMD_CHECK,
  CMD_EVAL,
  CMD_IR,
  CMD_ABI,
  CMD_BUILD,
  CMD_FMT,
  CMD_SIGNER,
  CMD_VERIFIER,
  CMD_ZKML
} Command;

typedef struct {
  Command command;
  const char *prog_path;
  const char *entry;
  char **args;
  int arg_count;
  const char *out_path;
  int runtime;
  int json;
  unsigned height;
  const char *pk_path;
  int shim;
  int entry2;
  HashBinding hash;
  unsigned count;
} Options;

static const struct {
  const char *word;
  Command command;
} COMMANDS[] = {
  {"check", CMD_CHECK},
  {"eval", CMD_EVAL},
  {"ir", CMD_IR},
  {"abi", CMD_ABI},
  {"build", CMD_BUILD},
  {"fmt", CMD_FMT},
  {"signer", CMD_SIGNER},
  {"verifier", CMD_VERIFIER},
  {"zkml", CMD_ZKML},
};

static int usage(FILE *err) {
  fputs("usage: langc check [--json] PROG\n"
        "       langc eval [--hash keccak|poseidon2] PROG NAME [ARGS...]\n"
        "       langc ir PROG\n"
        "       langc abi PROG\n"
        "       langc build PROG -o OUT [--runtime]\n"
        "       langc fmt PROG\n"
        "       langc signer --height H -o OUT\n"
        "       langc verifier --height H --pk FILE [--entry 8141|4337] [--shim] -o OUT\n"
        "       langc zkml --count N PROG NAME [ARGS...] -o OUT\n", err);
  return 2;
}

static int find_command(const char *word, Command *out) {
  for (size_t i = 0; i < sizeof COMMANDS / sizeof COMMANDS[0]; i++) {
    if (strcmp(COMMANDS[i].word, word) == 0) {
      *out = COMMANDS[i].command;
      return 1;
    }
  }
  return 0;
}

static int parse_build_options(int argc, char **argv, Options *opt, Diag *diag) {
  for (int i = 3; i < argc; i++) {
    int is_out = strcmp(argv[i], "-o") == 0 && i + 1 < argc;
    int is_runtime = strcmp(argv[i], "--runtime") == 0;
    if (!is_out && !is_runtime) return diag_fail(diag, "USAGE", NULL, "unknown build argument '%s'", argv[i]);
    opt->out_path = is_out ? argv[i + 1] : opt->out_path;
    opt->runtime |= is_runtime;
    i += is_out;
  }
  return opt->out_path != NULL ? 1 : diag_fail(diag, "USAGE", NULL, "build needs -o OUT");
}

/* H is decimal, 0 to TARGET_SIGNER_HEIGHT_MAX. */
static int parse_height(const char *text, unsigned *out) {
  size_t len = strlen(text);
  size_t i = 0;
  unsigned value = 0;
  for (; i < len && len <= 2 && text[i] >= '0' && text[i] <= '9'; i++) value = value * 10u + (unsigned)(text[i] - '0');
  *out = value;
  return len > 0 && i == len && value <= TARGET_SIGNER_HEIGHT_MAX;
}

/* langc signer --height H -o OUT (D23). There is no program path. */
static int parse_signer_options(int argc, char **argv, Options *opt, Diag *diag) {
  int has_height = 0;
  opt->prog_path = NULL;
  for (int i = 2; i < argc; i += 2) {
    int is_out = strcmp(argv[i], "-o") == 0 && i + 1 < argc;
    int is_height = strcmp(argv[i], "--height") == 0 && i + 1 < argc;
    if (!is_out && !is_height) return diag_fail(diag, "USAGE", NULL, "unknown signer argument '%s'", argv[i]);
    if (is_height && !parse_height(argv[i + 1], &opt->height)) {
      return diag_fail(diag, "USAGE", NULL, "the signer height '%s' is not 0 to %u", argv[i + 1], TARGET_SIGNER_HEIGHT_MAX);
    }
    opt->out_path = is_out ? argv[i + 1] : opt->out_path;
    has_height |= is_height;
  }
  return opt->out_path != NULL && has_height ? 1 : diag_fail(diag, "USAGE", NULL, "signer needs --height H -o OUT");
}

/* D13, D16, D17: --entry 8141 (the default) with no --shim writes the real
   entry 1 (EIP-8141), with --shim the shim (D7); --entry 4337 writes entry 2
   (ERC-4337 v0.7). */
static int parse_verifier_options(int argc, char **argv, Options *opt, Diag *diag) {
  int has_height = 0;
  opt->prog_path = NULL;
  for (int i = 2; i < argc; i++) {
    int is_shim = strcmp(argv[i], "--shim") == 0;
    int is_out = strcmp(argv[i], "-o") == 0 && i + 1 < argc;
    int is_height = strcmp(argv[i], "--height") == 0 && i + 1 < argc;
    int is_pk = strcmp(argv[i], "--pk") == 0 && i + 1 < argc;
    int is_entry = strcmp(argv[i], "--entry") == 0 && i + 1 < argc;
    if (!is_shim && !is_out && !is_height && !is_pk && !is_entry) {
      return diag_fail(diag, "USAGE", NULL, "unknown verifier argument '%s'", argv[i]);
    }
    if (is_height && !parse_height(argv[i + 1], &opt->height)) {
      return diag_fail(diag, "USAGE", NULL, "the verifier height '%s' is not 0 to %u", argv[i + 1],
                       TARGET_SIGNER_HEIGHT_MAX);
    }
    if (is_entry && strcmp(argv[i + 1], "8141") != 0 && strcmp(argv[i + 1], "4337") != 0) {
      return diag_fail(diag, "USAGE", NULL, "the verifier entry '%s' is not 8141 or 4337", argv[i + 1]);
    }
    opt->entry2 = is_entry ? strcmp(argv[i + 1], "4337") == 0 : opt->entry2;
    opt->out_path = is_out ? argv[i + 1] : opt->out_path;
    opt->pk_path = is_pk ? argv[i + 1] : opt->pk_path;
    opt->shim |= is_shim;
    has_height |= is_height;
    i += !is_shim;
  }
  if (opt->shim && opt->entry2) return diag_fail(diag, "USAGE", NULL, "verifier --shim needs --entry 8141");
  return opt->out_path != NULL && opt->pk_path != NULL && has_height
             ? 1
             : diag_fail(diag, "USAGE", NULL, "verifier needs --height H --pk FILE [--entry 8141|4337] [--shim] -o OUT");
}

/* The --hash value of langc eval (Q-S7-7 (a)). */
static int parse_hash(const char *text, HashBinding *hash) {
  int poseidon2 = strcmp(text, "poseidon2") == 0;
  *hash = poseidon2 ? HASH_POSEIDON2 : HASH_KECCAK;
  return poseidon2 || strcmp(text, "keccak") == 0;
}

/* langc eval [--hash keccak|poseidon2] PROG NAME [ARGS...]. The binding is
   Keccak by default (D1, Q-S7-7 (a)). */
static int parse_eval_options(int argc, char **argv, Options *opt, Diag *diag) {
  int shift = strcmp(argv[2], "--hash") == 0 ? 2 : 0;
  opt->hash = HASH_KECCAK;
  if (shift && argc < 4) return diag_fail(diag, "USAGE", NULL, "eval --hash needs keccak or poseidon2");
  if (shift && !parse_hash(argv[3], &opt->hash)) {
    return diag_fail(diag, "USAGE", NULL, "the eval hash '%s' is not keccak or poseidon2", argv[3]);
  }
  if (argc < 4 + shift) return diag_fail(diag, "USAGE", NULL, "eval needs a definition name");
  opt->prog_path = argv[2 + shift];
  opt->entry = argv[3 + shift];
  opt->args = argv + 4 + shift;
  opt->arg_count = argc - 4 - shift;
  return 1;
}

/* The N of langc zkml --count N: 1 to 64. */
static int parse_count(const char *text, unsigned *out) {
  unsigned n = 0;
  size_t i;
  for (i = 0; text[i] >= '0' && text[i] <= '9' && n <= 64; i++) n = n * 10 + (unsigned)(text[i] - '0');
  *out = n;
  return i > 0 && text[i] == '\0' && n >= 1 && n <= 64;
}

/* langc zkml --count N PROG NAME [ARGS...] -o OUT (D9). The writer
   evaluates NAME ARGS k for k = 0 to N - 1. */
static int parse_zkml_options(int argc, char **argv, Options *opt, Diag *diag) {
  int has_count = argc >= 4 && strcmp(argv[2], "--count") == 0 && parse_count(argv[3], &opt->count);
  if (!has_count) return diag_fail(diag, "USAGE", NULL, "zkml needs --count N with N from 1 to 64");
  if (argc < 8 || strcmp(argv[argc - 2], "-o") != 0)
    return diag_fail(diag, "USAGE", NULL, "zkml needs --count N PROG NAME [ARGS...] -o OUT");
  opt->prog_path = argv[4];
  opt->entry = argv[5];
  opt->args = argv + 6;
  opt->arg_count = argc - 8;
  opt->out_path = argv[argc - 1];
  return 1;
}

static int parse_options(int argc, char **argv, Options *opt, Diag *diag) {
  memset(opt, 0, sizeof *opt);
  if (argc < 3 || !find_command(argv[1], &opt->command)) {
    return diag_fail(diag, "USAGE", NULL, "expected a command and a program path");
  }
  opt->json = opt->command == CMD_CHECK && strcmp(argv[2], "--json") == 0;
  opt->prog_path = argv[2 + opt->json];
  switch (opt->command) {
    case CMD_CHECK:
    case CMD_IR:
    case CMD_ABI:
    case CMD_FMT:
      if (opt->json && argc == 3) return diag_fail(diag, "USAGE", NULL, "check --json needs a program path");
      return argc == 3 + opt->json ? 1 : diag_fail(diag, "USAGE", NULL, "too many arguments");
    case CMD_EVAL:
      return parse_eval_options(argc, argv, opt, diag);
    case CMD_BUILD:
      return parse_build_options(argc, argv, opt, diag);
    case CMD_SIGNER:
      return parse_signer_options(argc, argv, opt, diag);
    case CMD_VERIFIER:
      return parse_verifier_options(argc, argv, opt, diag);
    case CMD_ZKML:
      return parse_zkml_options(argc, argv, opt, diag);
  }
  return 0;
}

static int read_source(Arena *arena, const char *path, char **out, size_t *len, Diag *diag) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return diag_fail(diag, "IO", NULL, "cannot open %s", path);
  char *buf = arena_alloc(arena, SOURCE_MAX_BYTES + 1);
  size_t count = buf == NULL ? 0 : fread(buf, 1, SOURCE_MAX_BYTES + 1, file);
  int read_error = ferror(file);
  fclose(file);
  if (buf == NULL) return diag_fail(diag, "OOM", NULL, "no memory for %s", path);
  if (read_error) return diag_fail(diag, "IO", NULL, "cannot read %s", path);
  if (count > SOURCE_MAX_BYTES) return diag_fail(diag, "IO_SIZE", NULL, "%s is larger than %zu bytes", path, SOURCE_MAX_BYTES);
  *out = buf;
  *len = count;
  return 1;
}

/* Lowers the entries, then runs the ir, abi or build command. */
static int run_target(const Options *opt, Machine *machine, Diag *diag) {
  IrProgram prog;
  FILE *out;
  size_t i;
  int ok = 1;
  if (!lower_program(machine, &prog)) return 1;
  switch (opt->command) {
    case CMD_IR:
      ir_print(&prog, stdout);
      return 0;
    case CMD_ABI:
      for (i = 0; i < prog.func_count && ok; i++) ok = target_abi_line(&prog.funcs[i], stdout, stderr);
      return ok ? 0 : 1;
    case CMD_CHECK:
    case CMD_EVAL:
    case CMD_BUILD:
    case CMD_FMT:
    case CMD_VERIFIER:
    case CMD_SIGNER:
    case CMD_ZKML:
      break;
  }
  out = fopen(opt->out_path, "wb");
  if (out == NULL) return diag_fail(diag, "IO", NULL, "cannot write '%s'", opt->out_path) + 1;
  ok = target_write(&prog, opt->runtime ? TARGET_PART_RUNTIME : TARGET_PART_MAIN, out, stderr);
  return fclose(out) == 0 && ok ? 0 : 1;
}

/* Writes the signer module (D23). It reads no program. */
static int run_signer(const Options *opt, Diag *diag) {
  FILE *out = fopen(opt->out_path, "wb");
  if (out == NULL) return diag_fail(diag, "IO", NULL, "cannot write '%s'", opt->out_path) + 1;
  int ok = target_write_signer(opt->height, out, stderr);
  return fclose(out) == 0 && ok ? 0 : 1;
}

/* D13: the pk FILE is the 72 bytes of a Wasm signer publicKey block. */
static int run_verifier(const Options *opt, Diag *diag) {
  unsigned char pk[73];
  FILE *in = fopen(opt->pk_path, "rb");
  if (in == NULL) return diag_fail(diag, "IO", NULL, "cannot read '%s'", opt->pk_path) + 1;
  size_t got = fread(pk, 1, sizeof pk, in);
  if (fclose(in) != 0 || got != 72) {
    return diag_fail(diag, "IO", NULL, "the pk file '%s' is not 72 bytes", opt->pk_path) + 1;
  }
  FILE *out = fopen(opt->out_path, "wb");
  if (out == NULL) return diag_fail(diag, "IO", NULL, "cannot write '%s'", opt->out_path) + 1;
  VerifierMode entry1 = opt->shim ? VERIFIER_SHIM : VERIFIER_REAL;
  int ok = target_write_verifier(opt->height, pk, opt->entry2 ? VERIFIER_4337 : entry1, out, stderr);
  return fclose(out) == 0 && ok ? 0 : 1;
}

static int run(const Options *opt, Arena *arena, Diag *diag) {
  char *text = NULL;
  size_t len = 0;
  if (!read_source(arena, opt->prog_path, &text, &len, diag)) return 2;
  Front front;
  if (!front_load(arena, opt->prog_path, text, len, &front, diag)) return 1;
  Machine machine;
  if (!check_program(arena, &front.decls, &machine, diag)) return 1;
  switch (opt->command) {
    case CMD_EVAL:
      return eval_command(&machine, opt->hash, opt->entry, opt->args, opt->arg_count, stdout, stderr);
    case CMD_ZKML:
      return zkml_command(&machine, opt->entry, opt->args, opt->arg_count, opt->count, opt->out_path);
    case CMD_CHECK: /* run_check does check */
    case CMD_IR:
    case CMD_ABI:
    case CMD_BUILD:
    case CMD_FMT: /* run_fmt does fmt */
    case CMD_SIGNER: /* run_signer writes the signer */
    case CMD_VERIFIER: /* run_verifier writes the verifier */
      break;
  }
  return run_target(opt, &machine, diag);
}

/* The span of declaration D. A domain declaration is in domain/domain.lang. */
static Span decl_span(const char *path, const Decl *d) {
  Span span = {d->origin == ORIGIN_DOMAIN ? "domain/domain.lang" : path, d->line, d->col, d->end_line, d->end_col};
  return span;
}

/* The offset after COUNT more newlines from AT, or LEN. */
static size_t skip_lines(const char *text, size_t len, size_t at, int count) {
  size_t pos = at;
  for (int n = 0; n < count && pos < len; pos++) n += text[pos] == '\n';
  return pos;
}

static int count_newlines(const char *text, size_t len) {
  int n = 0;
  for (size_t i = 0; i < len; i++) n += text[i] == '\n';
  return n;
}

/* Records W_NONCANON in WARN for the region of lines FIRST to LAST of
   declaration D (NULL for the tail). FOUND is the input text of the region,
   FIX its canonical text. */
static void warn_region(Arena *arena, const char *path, const Decl *d, int first, int last, const char *found,
                        size_t found_len, const char *fix, size_t fix_len, Diag *warn) {
  Span span = d != NULL ? decl_span(path, d) : (Span){path, first, 1, last, 1};
  diag_fail_at(warn, "W_NONCANON", d != NULL ? d->name : NULL, span, NULL, NULL, NULL,
               "%s:%d:%d: lines %d to %d are not canonical; replace them with the fix text (the langc fmt output)",
               path, span.line, span.col, first, last);
  const char *found_copy = arena_strndup(arena, found, found_len);
  const char *fix_copy = fix_len == 0 ? "" : arena_strndup(arena, fix, fix_len);
  diag_attach(warn, fix_copy, found_copy, fix_copy);
}

/* Records one W_NONCANON in WARN for the first program region that differs
   from its canonical text (HANDOFF decision 3): region I runs from the line
   after declaration I-1 to the end of the last line of declaration I. The
   text after the last declaration is one more region (the tail). */
static void noncanon(Arena *arena, const char *path, const char *text, size_t len, const Front *front, Diag *warn) {
  const Decl *decls = front->decls.items + front->domain_count;
  size_t count = front->decls.count - front->domain_count;
  FmtText canon;
  size_t next_comment = 0;
  size_t pos = 0;
  int prev_line = 0;
  fmt_text_init(&canon, arena);
  for (size_t i = 0; i <= count && !warn->set && !canon.oom; i++) {
    int tail = i == count;
    size_t before = canon.len;
    size_t end = tail ? len : skip_lines(text, len, pos, decls[i].end_line - prev_line);
    if (tail) {
      fmt_tail(&canon, count, &front->toks, next_comment);
    } else {
      fmt_region(&canon, decls, i, &front->toks, &next_comment);
    }
    size_t fix_len = canon.len - before;
    int same = fix_len == end - pos && (fix_len == 0 || memcmp(canon.data + before, text + pos, fix_len) == 0);
    int lines = count_newlines(text + pos, end - pos);
    int last = tail ? prev_line + (lines > 0 ? lines : 1) : decls[i].end_line;
    if (!same && !canon.oom) {
      warn_region(arena, path, tail ? NULL : &decls[i], prev_line + 1, last, text + pos, end - pos, canon.data + before,
                  fix_len, warn);
    }
    prev_line = last;
    pos = end;
  }
}

/* The last line of `check --json`. The count is the number of program
   declarations: after a parse failure, the complete ones and the failed
   one; after a lex failure, 0. The use count runs in the same pass as
   the types, so `affine` is the `typed` count (Q-S3-9 (a)). */
static void print_progress(const Front *front, const Machine *machine, int parsed, int typed, FILE *out) {
  size_t done = front->lexed ? front->decls.count - front->domain_count : 0;
  size_t count = done + (size_t)(front->lexed && !parsed);
  size_t checked = machine->checked > front->domain_count ? machine->checked - front->domain_count : 0;
  size_t ok = typed ? count : parsed ? checked : 0;
  fprintf(out, "{\"parsed\":\"%zu of %zu\",\"typed\":\"%zu of %zu\",\"affine\":\"%zu of %zu\",\"laws\":\"0 of 0\"}\n",
          done, count, ok, count, ok, count);
}

/* `check` in both modes: the error (at most one), then the W_NONCANON
   warning (at most one), then `ok` (text) or the progress line (JSON). A
   warning does not change the exit code. */
static int run_check(const Options *opt, Arena *arena, Diag *diag) {
  char *text = NULL;
  size_t len = 0;
  Front front;
  Machine machine;
  Diag warn;
  memset(&front, 0, sizeof front);
  memset(&machine, 0, sizeof machine);
  diag_init(&warn);
  int loaded = read_source(arena, opt->prog_path, &text, &len, diag);
  int parsed = loaded && front_load(arena, opt->prog_path, text, len, &front, diag);
  int typed = parsed && check_program(arena, &front.decls, &machine, diag);
  if (parsed && !typed && machine.decl_failed) {
    diag_default_span(diag, decl_span(opt->prog_path, &front.decls.items[machine.checked]));
  }
  if (parsed) noncanon(arena, opt->prog_path, text, len, &front, &warn);
  if (opt->json) {
    if (diag->set) diag_print_json(diag, stdout);
    if (warn.set) diag_print_json(&warn, stdout);
    print_progress(&front, &machine, parsed, typed, stdout);
  } else {
    if (diag->set) diag_print(diag, stderr);
    if (warn.set) diag_print(&warn, stderr);
    if (typed) puts("ok");
  }
  return typed ? 0 : loaded ? 1 : 2;
}

/* Lexes and parses the program only (no domain, no check), then prints the
   canonical text. */
static int run_fmt(const Options *opt, Arena *arena, Diag *diag) {
  char *text = NULL;
  size_t len = 0;
  if (!read_source(arena, opt->prog_path, &text, &len, diag)) return 2;
  TokenList toks;
  DeclList decls = {NULL, 0, 0};
  if (!lex_source(arena, opt->prog_path, text, len, &toks, diag)) return 1;
  if (!parse_source(arena, opt->prog_path, ORIGIN_PROGRAM, &toks, &decls, diag)) return 1;
  FmtText out;
  fmt_text_init(&out, arena);
  if (!fmt_program(&out, decls.items, decls.count, &toks)) {
    return diag_fail(diag, "OOM", NULL, "no memory for the canonical text of %s", opt->prog_path) + 2;
  }
  size_t written = out.len == 0 ? 0 : fwrite(out.data, 1, out.len, stdout);
  if (written != out.len || fflush(stdout) != 0) return diag_fail(diag, "IO", NULL, "cannot write the canonical text") + 2;
  return 0;
}

int main(int argc, char **argv) {
  Diag diag;
  diag_init(&diag);
  Options opt;
  if (!parse_options(argc, argv, &opt, &diag)) {
    diag_print(&diag, stderr);
    return usage(stderr);
  }
  Arena arena;
  arena_init(&arena, ARENA_LIMIT_BYTES);
  int status = opt.command == CMD_FMT        ? run_fmt(&opt, &arena, &diag)
               : opt.command == CMD_CHECK    ? run_check(&opt, &arena, &diag)
               : opt.command == CMD_SIGNER   ? run_signer(&opt, &diag)
               : opt.command == CMD_VERIFIER ? run_verifier(&opt, &diag)
                                             : run(&opt, &arena, &diag);
  arena_release(&arena);
  /* run_check prints its own diagnostics. */
  if (status != 0 && diag.set && opt.command != CMD_CHECK) diag_print(&diag, stderr);
  return status;
}
