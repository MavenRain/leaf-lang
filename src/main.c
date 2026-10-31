/* The langc command line. SHARED by the tcc kits.
   Exit 0: ok. Exit 1: the program is refused. Exit 2: usage or IO. */
#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/fmt.h"
#include "front/front.h"
#include "front/lower.h"
#include "front/parser.h"
#include "manifest.h"
#include "mcp.h"
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
  CMD_ZKML,
  CMD_REBUILD,
  CMD_EXPLAIN,
  CMD_MCP
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
  const char *text; /* `mcp`: the program text in memory, or NULL (read prog_path) */
  size_t text_len;
  FILE *out; /* `mcp`: the capture of the tool text, or NULL (stdout) */
  unsigned height;
  const char *pk_path;
  int shim;
  int entry2;
  HashBinding hash;
  unsigned count;
  char **words; /* the words after langc (D2c) */
  int word_count;
  OutBuffer *keep; /* rebuild (D3e): not NULL: finish_output keeps the bytes here */
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
  {"rebuild", CMD_REBUILD},
  {"explain", CMD_EXPLAIN},
  {"mcp", CMD_MCP},
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
        "       langc zkml --count N PROG NAME [ARGS...] -o OUT\n"
        "       langc rebuild MANIFEST\n"
        "       langc explain [--json] PROG [NAME]\n"
        "       langc mcp\n", err);
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
  opt->words = argv + 1;
  opt->word_count = argc - 1;
  if (argc < 2 || !find_command(argv[1], &opt->command)) {
    return diag_fail(diag, "USAGE", NULL, "expected a command and a program path");
  }
  if (opt->command == CMD_MCP) return argc == 2 ? 1 : diag_fail(diag, "USAGE", NULL, "mcp takes no argument");
  if (argc < 3) return diag_fail(diag, "USAGE", NULL, "expected a command and a program path");
  opt->json = (opt->command == CMD_CHECK || opt->command == CMD_EXPLAIN) && strcmp(argv[2], "--json") == 0;
  opt->prog_path = argv[2 + opt->json];
  switch (opt->command) {
    case CMD_CHECK:
    case CMD_IR:
    case CMD_ABI:
    case CMD_FMT:
    case CMD_REBUILD:
      if (opt->json && argc == 3) return diag_fail(diag, "USAGE", NULL, "check --json needs a program path");
      return argc == 3 + opt->json ? 1 : diag_fail(diag, "USAGE", NULL, "too many arguments");
    case CMD_EXPLAIN:
      if (opt->json && argc == 3) return diag_fail(diag, "USAGE", NULL, "explain --json needs a program path");
      opt->entry = argc == 4 + opt->json ? argv[3 + opt->json] : NULL;
      return argc <= 4 + opt->json ? 1 : diag_fail(diag, "USAGE", NULL, "too many arguments");
    case CMD_MCP: /* parse_options returns before this switch */
      return 0;
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

/* The counts of the last line of `check --json` (print_progress). */
static LawCounts progress_counts(const Front *front, const Machine *machine, int parsed, int typed) {
  LawCounts counts;
  size_t checked = machine->checked > front->domain_count ? machine->checked - front->domain_count : 0;
  counts.done = front->lexed ? front->decls.count - front->domain_count : 0;
  counts.count = counts.done + (size_t)(front->lexed && !parsed);
  counts.ok = typed ? counts.count : parsed ? checked : 0;
  return counts;
}

/* The manifest of OPT with SOURCES and LAWS (D2a). */
static Manifest manifest_of(const Options *opt, const ManifestSource *sources, int source_count, LawCounts laws) {
  Manifest m;
  m.words = opt->words;
  m.word_count = opt->word_count;
  m.out_path = opt->out_path;
  m.sources = sources;
  m.source_count = source_count;
  m.laws = laws;
  return m;
}

/* D2f for signer and verifier: the counts of the domain text. They load the
   domain with an empty program. */
static int domain_counts(Arena *arena, LawCounts *counts, Diag *diag) {
  Front front;
  Machine machine;
  memset(&front, 0, sizeof front);
  memset(&machine, 0, sizeof machine);
  if (!front_load(arena, "<domain>", "", 0, &front, diag)) return 0;
  if (!check_program(arena, &front.decls, &machine, diag)) return 0;
  counts->done = front.domain_count;
  counts->count = front.domain_count;
  counts->ok = front.domain_count;
  return 1;
}

/* D2j: the writer wrote the output into BUF and gave STATUS. With status 0,
   writes OUT and then OUT.manifest.json from the same bytes; else neither.
   D3e: with OPT->keep (rebuild), status 0 moves the bytes into OPT->keep,
   and no file is written. */
static int finish_output(const Options *opt, const Manifest *m, OutBuffer *buf, int status, Diag *diag) {
  int closed = out_buffer_close(buf);
  if (opt->keep != NULL && closed && status == 0) {
    *opt->keep = *buf;
    return 0;
  }
  ManifestStatus written = closed && status == 0 ? manifest_write(m, buf->data, buf->size) : MANIFEST_OK;
  out_buffer_free(buf);
  if (status != 0) return status;
  if (!closed) return diag_fail(diag, "IO", NULL, "cannot write '%s'", m->out_path) + 2;
  switch (written) {
    case MANIFEST_OK:
      return 0;
    case MANIFEST_USAGE:
      return diag_fail(diag, "USAGE", NULL, "a word or a path has a byte below 0x20") + 2;
    case MANIFEST_NO_MEMORY:
      return diag_fail(diag, "OOM", NULL, "no memory for the manifest of '%s'", m->out_path) + 2;
    case MANIFEST_OUT_IO:
      return diag_fail(diag, "IO", NULL, "cannot write '%s'", m->out_path) + 2;
    case MANIFEST_FILE_IO:
      return diag_fail(diag, "IO", NULL, "cannot write '%s.manifest.json'", m->out_path) + 2;
  }
  return 2;
}

/* langc zkml: the writer writes into memory, then OUT and the manifest. */
static int run_zkml(const Options *opt, Machine *machine, const Manifest *m, Diag *diag) {
  OutBuffer buf;
  if (!out_buffer_open(&buf)) return diag_fail(diag, "IO", NULL, "cannot write '%s'", opt->out_path) + 2;
  int status = zkml_command(machine, opt->entry, opt->args, opt->arg_count, opt->count, buf.file);
  return finish_output(opt, m, &buf, status, diag);
}

/* Lowers the entries, then runs the ir, abi or build command. */
static int run_target(const Options *opt, Machine *machine, const Manifest *m, Diag *diag) {
  IrProgram prog;
  OutBuffer buf;
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
    case CMD_EXPLAIN:
    case CMD_REBUILD:
    case CMD_SIGNER:
    case CMD_ZKML:
    case CMD_MCP:
      break;
  }
  if (!out_buffer_open(&buf)) return diag_fail(diag, "IO", NULL, "cannot write '%s'", opt->out_path) + 2;
  ok = target_write(&prog, opt->runtime ? TARGET_PART_RUNTIME : TARGET_PART_MAIN, buf.file, stderr);
  return finish_output(opt, m, &buf, ok ? 0 : 1, diag);
}

/* Writes the signer module (D23). It reads no program; the manifest has
   the domain text (D2d, D2f). */
static int run_signer(const Options *opt, Arena *arena, Diag *diag) {
  ManifestSource domain = {"<domain>", domain_source, domain_source_len};
  LawCounts laws;
  OutBuffer buf;
  if (!domain_counts(arena, &laws, diag)) return 1;
  if (!out_buffer_open(&buf)) return diag_fail(diag, "IO", NULL, "cannot write '%s'", opt->out_path) + 2;
  int ok = target_write_signer(opt->height, buf.file, stderr);
  Manifest m = manifest_of(opt, &domain, 1, laws);
  return finish_output(opt, &m, &buf, ok ? 0 : 1, diag);
}

/* D13: the pk FILE is the 72 bytes of a Wasm signer publicKey block. */
static int run_verifier(const Options *opt, Arena *arena, Diag *diag) {
  unsigned char pk[73];
  FILE *in = fopen(opt->pk_path, "rb");
  if (in == NULL) return diag_fail(diag, "IO", NULL, "cannot read '%s'", opt->pk_path) + 1;
  size_t got = fread(pk, 1, sizeof pk, in);
  if (fclose(in) != 0 || got != 72) {
    return diag_fail(diag, "IO", NULL, "the pk file '%s' is not 72 bytes", opt->pk_path) + 1;
  }
  LawCounts laws;
  OutBuffer buf;
  if (!domain_counts(arena, &laws, diag)) return 1;
  if (!out_buffer_open(&buf)) return diag_fail(diag, "IO", NULL, "cannot write '%s'", opt->out_path) + 2;
  VerifierMode entry1 = opt->shim ? VERIFIER_SHIM : VERIFIER_REAL;
  int ok = target_write_verifier(opt->height, pk, opt->entry2 ? VERIFIER_4337 : entry1, buf.file, stderr);
  /* The rebuild needs the key (D2d). */
  ManifestSource sources[2] = {{"<domain>", domain_source, domain_source_len}, {opt->pk_path, pk, 72}};
  Manifest m = manifest_of(opt, sources, 2, laws);
  return finish_output(opt, &m, &buf, ok ? 0 : 1, diag);
}

static int run(const Options *opt, Arena *arena, Diag *diag) {
  char *text = NULL;
  size_t len = 0;
  if (!read_source(arena, opt->prog_path, &text, &len, diag)) return 2;
  Front front;
  if (!front_load(arena, opt->prog_path, text, len, &front, diag)) return 1;
  Machine machine;
  if (!check_program(arena, &front.decls, &machine, diag)) return 1;
  ManifestSource source = {opt->prog_path, (const unsigned char *)text, len};
  Manifest m = manifest_of(opt, &source, 1, progress_counts(&front, &machine, 1, 1));
  switch (opt->command) {
    case CMD_EVAL:
      return eval_command(&machine, opt->hash, opt->entry, opt->args, opt->arg_count, stdout, stderr);
    case CMD_ZKML:
      return run_zkml(opt, &machine, &m, diag);
    case CMD_CHECK: /* run_check does check */
    case CMD_IR:
    case CMD_ABI:
    case CMD_BUILD:
    case CMD_FMT: /* run_fmt does fmt */
    case CMD_REBUILD: /* run_rebuild does rebuild */
    case CMD_SIGNER: /* run_signer writes the signer */
    case CMD_VERIFIER: /* run_verifier writes the verifier */
    case CMD_EXPLAIN: /* run_explain does explain */
    case CMD_MCP: /* mcp_serve does mcp */
      break;
  }
  return run_target(opt, &machine, &m, diag);
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
  LawCounts counts = progress_counts(front, machine, parsed, typed);
  manifest_print_laws(&counts, out);
  fputc('\n', out);
}

/* The program text: the bytes of an `mcp` tool call (opt->text), else the
   file prog_path. */
static int source_of(const Options *opt, Arena *arena, char **text, size_t *len, Diag *diag) {
  if (opt->text == NULL) return read_source(arena, opt->prog_path, text, len, diag);
  *text = arena_strndup(arena, opt->text, opt->text_len);
  *len = opt->text_len;
  return *text != NULL ? 1 : diag_fail(diag, "OOM", NULL, "no memory for the program text");
}

/* The output of a command: stdout, or the capture of an `mcp` tool. */
static FILE *out_of(const Options *opt) {
  return opt->out != NULL ? opt->out : stdout;
}

/* The load and check path of `check` and `explain` (S9 D2). */
typedef struct {
  Front front;
  Machine machine;
  Diag warn;
  int loaded;
  int parsed;
  int typed;
} Checked;

static void load_checked(const Options *opt, Arena *arena, Diag *diag, Checked *out) {
  char *text = NULL;
  size_t len = 0;
  memset(out, 0, sizeof *out);
  diag_init(&out->warn);
  out->loaded = source_of(opt, arena, &text, &len, diag);
  out->parsed = out->loaded && front_load(arena, opt->prog_path, text, len, &out->front, diag);
  out->typed = out->parsed && check_program(arena, &out->front.decls, &out->machine, diag);
  if (out->parsed && !out->typed && out->machine.decl_failed) {
    diag_default_span(diag, decl_span(opt->prog_path, &out->front.decls.items[out->machine.checked]));
  }
  if (out->parsed) noncanon(arena, opt->prog_path, text, len, &out->front, &out->warn);
}

/* The `check --json` bytes: the error, the warning, the progress line. */
static void print_check_json(Checked *k, Diag *diag, FILE *out) {
  if (diag->set) diag_print_json(diag, out);
  if (k->warn.set) diag_print_json(&k->warn, out);
  print_progress(&k->front, &k->machine, k->parsed, k->typed, out);
}

/* `check` in both modes: the error (at most one), then the W_NONCANON
   warning (at most one), then `ok` (text) or the progress line (JSON). A
   warning does not change the exit code. */
static int run_check(const Options *opt, Arena *arena, Diag *diag) {
  Checked k;
  load_checked(opt, arena, diag, &k);
  if (opt->json) {
    print_check_json(&k, diag, out_of(opt));
  } else {
    if (diag->set) diag_print(diag, stderr);
    if (k.warn.set) diag_print(&k.warn, stderr);
    if (k.typed) puts("ok");
  }
  return k.typed ? 0 : k.loaded ? 1 : 2;
}

/* A JSON string of the LEN bytes at S. */
static void put_json_bytes(const char *s, size_t len, FILE *out) {
  size_t i;
  fputc('"', out);
  for (i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)s[i];
    switch (ch) {
      case '"':
      case '\\':
        fputc('\\', out);
        fputc(ch, out);
        break;
      default:
        if (ch < 0x20) fprintf(out, "\\u%04x", (unsigned)ch);
        else fputc(ch, out);
        break;
    }
  }
  fputc('"', out);
}

static void put_json_str(const char *s, FILE *out) {
  put_json_bytes(s, strlen(s), out);
}

/* The kind of a declaration (S9 D2). A checked program has only DECL_DEF
   (rules R1 to R3). */
static const char *explain_kind(DeclKind kind) {
  switch (kind) {
    case DECL_DEF: return "def";
    case DECL_AXIOM: return "postulate";
    case DECL_FAMILY: return "data CM";
    case DECL_COFAMILY: return "data CC";
  }
  return "def";
}

/* The multiplicity of a binder (S9 D2): 0, 1 or ω. */
static const char *explain_mult(Mult mult) {
  switch (mult) {
    case MULT_OMEGA: return "ω";
    case MULT_ZERO: return "0";
    case MULT_ONE: return "1";
  }
  return "ω";
}

/* The name of a Pi binder; `_` for the binder of `A ⊸ B` or `A → B`. */
static const char *binder_name(const Term *t) {
  return t->name != NULL ? t->name : "_";
}

/* The resource scopes of one program body (S9 D10): one item for each
   level and name, its count the maximum over the closes of that scope (the
   arms of a match, a term checked again), the lowest level first. */
typedef struct {
  const LeafUse **items;
  uint32_t *uses;
  size_t count;
} Leaves;

static void leaves_add(Leaves *out, const LeafUse *u) {
  size_t i = 0;
  while (i < out->count && (out->items[i]->level != u->level || strcmp(out->items[i]->name, u->name) != 0)) i++;
  if (i < out->count) {
    out->uses[i] = u->uses > out->uses[i] ? u->uses : out->uses[i];
  } else {
    out->items[i] = u;
    out->uses[i] = u->uses;
    out->count++;
  }
}

static int leaf_after(const LeafUse *a, const LeafUse *b) {
  return a->level != b->level ? a->level > b->level : strcmp(a->name, b->name) > 0;
}

static void explain_leaves(Arena *arena, const Machine *m, uint32_t def, Leaves *out) {
  const LeafUse *u;
  size_t n = 0;
  size_t i;
  for (u = m->leaves; u != NULL; u = u->next) n += u->def == def;
  out->items = arena_alloc(arena, (n + 1u) * sizeof *out->items);
  out->uses = arena_alloc(arena, (n + 1u) * sizeof *out->uses);
  out->count = 0;
  for (u = m->leaves; u != NULL && out->items != NULL && out->uses != NULL; u = u->next) {
    if (u->def == def) leaves_add(out, u);
  }
  for (i = 1; i < out->count; i++) {
    const LeafUse *item = out->items[i];
    uint32_t uses = out->uses[i];
    size_t j = i;
    for (; j > 0 && leaf_after(out->items[j - 1], item); j--) {
      out->items[j] = out->items[j - 1];
      out->uses[j] = out->uses[j - 1];
    }
    out->items[j] = item;
    out->uses[j] = uses;
  }
}

/* The index in Machine.defs of the program definition NAME. */
static uint32_t def_index(const Machine *m, const char *name) {
  uint32_t i = 0;
  while (i < m->def_count && (m->defs[i].origin != ORIGIN_PROGRAM || strcmp(m->defs[i].name, name) != 0)) i++;
  return i;
}

/* 1 when NAME is a declaration of the program, not of the domain. */
static int program_has(const Front *front, const char *name) {
  size_t i = front->domain_count;
  while (i < front->decls.count && strcmp(front->decls.items[i].name, name) != 0) i++;
  return i < front->decls.count;
}

/* One item of `defs` (S9 D2, D10). Q-S9-9 (a): `laws` is L5 when the affine
   check counts a resource in the body. FIRST: no comma before it. */
static void explain_json(const Decl *d, const Leaves *leaves, const FmtText *type, int first, FILE *out) {
  const Term *t;
  size_t i;
  fputs(first ? "{\"name\":" : ",{\"name\":", out);
  put_json_str(d->name, out);
  fputs(",\"kind\":", out);
  put_json_str(explain_kind(d->kind), out);
  fputs(",\"type\":", out);
  put_json_bytes(type->data, type->oom ? 0 : type->len, out);
  fputs(",\"binders\":[", out);
  for (t = d->type; t->kind == TERM_PI; t = t->right) {
    fputs(t == d->type ? "{\"name\":" : ",{\"name\":", out);
    put_json_str(binder_name(t), out);
    fputs(",\"mult\":", out);
    put_json_str(explain_mult(t->mult), out);
    fputc('}', out);
  }
  fputs("],\"leaves\":[", out);
  for (i = 0; i < leaves->count; i++) {
    fputs(i == 0 ? "{\"name\":" : ",{\"name\":", out);
    put_json_str(leaves->items[i]->name, out);
    fprintf(out, ",\"uses\":%u}", (unsigned)leaves->uses[i]);
  }
  fputs(leaves->count > 0 ? "],\"laws\":[\"L5\"]}" : "],\"laws\":[]}", out);
}

/* The text block of one definition: the signature line, then the kind,
   binders, leaves and laws lines. */
static void explain_text(const Decl *d, const Leaves *leaves, const FmtText *type, FILE *out) {
  const Term *t;
  size_t i;
  fputs(d->name, out);
  fputs(" : ", out);
  if (type->data != NULL && !type->oom) fwrite(type->data, 1, type->len, out);
  fprintf(out, "\n  kind: %s\n  binders:", explain_kind(d->kind));
  for (t = d->type; t->kind == TERM_PI; t = t->right)
    fprintf(out, "%s%s %s", t == d->type ? " " : ", ", binder_name(t), explain_mult(t->mult));
  fputs(d->type->kind == TERM_PI ? "\n  leaves:" : " none\n  leaves:", out);
  for (i = 0; i < leaves->count; i++)
    fprintf(out, "%s%s %u", i == 0 ? " " : ", ", leaves->items[i]->name, (unsigned)leaves->uses[i]);
  fputs(leaves->count > 0 ? "\n  laws: L5\n" : " none\n  laws: none\n", out);
}

/* One program definition of `explain`. Returns 0: FIRST of the next one. */
static int explain_decl(const Options *opt, Arena *arena, const Machine *m, const Decl *d, int first) {
  Leaves leaves;
  FmtText type;
  fmt_text_init(&type, arena);
  fmt_term(&type, d->type);
  explain_leaves(arena, m, def_index(m, d->name), &leaves);
  if (opt->json) explain_json(d, &leaves, &type, first, out_of(opt));
  else explain_text(d, &leaves, &type, out_of(opt));
  return 0;
}

/* `explain` (S9 D2, D10, D11). A program that fails `check`: the `check
   --json` bytes, exit 1. NAME keeps one definition (Q-S9-3 (a)); a NAME that
   is not a program definition is USAGE, exit 2 (S9 D11). */
static int run_explain(const Options *opt, Arena *arena, Diag *diag) {
  Checked k;
  size_t i;
  int first = 1;
  load_checked(opt, arena, diag, &k);
  if (!k.typed) {
    print_check_json(&k, diag, out_of(opt));
    return k.loaded ? 1 : 2;
  }
  if (opt->entry != NULL && !program_has(&k.front, opt->entry)) {
    (void)diag_fail(diag, "USAGE", NULL, "%s is not a definition of the program", opt->entry);
    if (opt->out != NULL) diag_print_json(diag, opt->out);
    else diag_print(diag, stderr);
    return 2;
  }
  LawCounts counts = progress_counts(&k.front, &k.machine, k.parsed, k.typed);
  if (opt->json) {
    fputs("{\"counts\":", out_of(opt));
    manifest_print_laws(&counts, out_of(opt));
    fputs(",\"defs\":[", out_of(opt));
  }
  for (i = k.front.domain_count; i < k.front.decls.count; i++) {
    const Decl *d = &k.front.decls.items[i];
    if (opt->entry == NULL || strcmp(opt->entry, d->name) == 0) first = explain_decl(opt, arena, &k.machine, d, first);
  }
  if (opt->json) fputs("]}\n", out_of(opt));
  return 0;
}

/* Lexes and parses the program only (no domain, no check), then prints the
   canonical text. */
static int run_fmt(const Options *opt, Arena *arena, Diag *diag) {
  char *text = NULL;
  size_t len = 0;
  if (!source_of(opt, arena, &text, &len, diag)) return 2;
  TokenList toks;
  DeclList decls = {NULL, 0, 0};
  if (!lex_source(arena, opt->prog_path, text, len, &toks, diag)) return 1;
  if (!parse_source(arena, opt->prog_path, ORIGIN_PROGRAM, &toks, &decls, diag)) return 1;
  FmtText out;
  fmt_text_init(&out, arena);
  if (!fmt_program(&out, decls.items, decls.count, &toks)) {
    return diag_fail(diag, "OOM", NULL, "no memory for the canonical text of %s", opt->prog_path) + 2;
  }
  size_t written = out.len == 0 ? 0 : fwrite(out.data, 1, out.len, out_of(opt));
  if (written != out.len || fflush(out_of(opt)) != 0) return diag_fail(diag, "IO", NULL, "cannot write the canonical text") + 2;
  return 0;
}

/* D3e: the first word and the -o of a writer run again. */
static char REBUILD_LANGC[] = "langc";
static char REBUILD_OUT_FLAG[] = "-o";

/* D3c step 4: 0 when the keccak256 of source I of R now is the manifest
   value. `<domain>` is the embedded domain text. */
static int rebuild_source(const ManifestRead *r, int i, Arena *arena, Diag *diag) {
  char hex[MANIFEST_HASH_CHARS];
  char *text = NULL;
  size_t len = 0;
  /* Only the first source of signer/verifier is embedded. A user program
     or a public-key file can also be named "<domain>". */
  int domain = i == 0 && strcmp(r->source_paths[i], "<domain>") == 0 &&
               (strcmp(r->words[0], "signer") == 0 || strcmp(r->words[0], "verifier") == 0);
  if (!domain && !read_source(arena, r->source_paths[i], &text, &len, diag)) return 2;
  manifest_keccak_hex(domain ? domain_source : (const unsigned char *)text, domain ? domain_source_len : len, hex);
  if (strcmp(hex, r->source_hashes[i]) == 0) return 0;
  return diag_fail(diag, "REFUSE_REBUILD_SOURCE", NULL, "the keccak256 of '%s' is %s, not the manifest value %s",
                   r->source_paths[i], hex, r->source_hashes[i]) + 1;
}

/* D3c step 5: the writer diagnostic comes first on stderr, then the refusal. */
static int rebuild_writer_failed(const char *manifest, int status, Diag *diag) {
  if (diag->set) diag_print(diag, stderr);
  diag_init(diag);
  return diag_fail(diag, "REFUSE_REBUILD_WRITER", NULL, "the command of '%s' gives exit %d", manifest, status) + 1;
}

/* D3c step 6: 1 when the file PATH holds the SIZE bytes of DATA, 0 when it
   holds other bytes, -1 when it cannot be read. */
static int file_holds(const char *path, const char *data, size_t size) {
  unsigned char chunk[4096];
  FILE *file = fopen(path, "rb");
  if (file == NULL) return -1;
  size_t at = 0;
  size_t got = 0;
  int same = 1;
  do {
    got = fread(chunk, 1, sizeof chunk, file);
    same = same && got <= size - at && memcmp(chunk, data + at, got) == 0;
    at += same ? got : 0;
  } while (same && got == sizeof chunk);
  int error = ferror(file);
  fclose(file);
  return error ? -1 : same && at == size;
}

/* D3c steps 5 to 7: runs the words of R again with -o OUT into memory
   (D3e), then compares the bytes with `output` and with the file OUT. */
static int rebuild_output(const char *manifest, const ManifestRead *r, Arena *arena, Diag *diag) {
  int argc = r->word_count + 3;
  void *mem = arena_alloc(arena, (size_t)(argc + 1) * sizeof(char *));
  char **argv = mem;
  if (argv == NULL) return diag_fail(diag, "OOM", NULL, "no memory for the command of '%s'", manifest) + 2;
  argv[0] = REBUILD_LANGC;
  memcpy(argv + 1, r->words, (size_t)r->word_count * sizeof(char *));
  argv[argc - 2] = REBUILD_OUT_FLAG;
  argv[argc - 1] = r->out_path;
  argv[argc] = NULL;
  Options w;
  OutBuffer kept = {NULL, NULL, 0};
  if (!parse_options(argc, argv, &w, diag)) return rebuild_writer_failed(manifest, 2, diag);
  w.keep = &kept;
  int status = w.command == CMD_SIGNER     ? run_signer(&w, arena, diag)
               : w.command == CMD_VERIFIER ? run_verifier(&w, arena, diag)
                                           : run(&w, arena, diag);
  if (status != 0) return rebuild_writer_failed(manifest, status, diag);
  char hex[MANIFEST_HASH_CHARS];
  size_t size = kept.size;
  manifest_keccak_hex((const unsigned char *)kept.data, size, hex);
  int same = size == r->out_size && strcmp(hex, r->out_hash) == 0;
  int held = same ? file_holds(r->out_path, kept.data, size) : 0;
  out_buffer_free(&kept);
  if (held < 0) return diag_fail(diag, "IO", NULL, "cannot read '%s'", r->out_path) + 2;
  if (!same) {
    return diag_fail(diag, "REFUSE_REBUILD_OUTPUT", NULL, "the rebuild of '%s' is %zu bytes, %s; the manifest has %zu bytes, %s",
                     r->out_path, size, hex, r->out_size, r->out_hash) + 1;
  }
  if (!held) return diag_fail(diag, "REFUSE_REBUILD_OUTPUT", NULL, "'%s' does not hold the bytes of the rebuild", r->out_path) + 1;
  printf("rebuild: %s matches (%zu bytes, %s)\n", r->out_path, size, hex);
  return 0;
}

/* D3c steps 2 to 7 for the manifest R of the file MANIFEST. */
static int rebuild_checks(const char *manifest, const ManifestRead *r, Arena *arena, Diag *diag) {
  int i;
  int status = 0;
  if (strcmp(r->commit, "unknown") == 0 || strcmp(r->commit, langc_commit) != 0) {
    return diag_fail(diag, "REFUSE_REBUILD_COMMIT", NULL, "'%s' has the langc commit %s, and this langc has %s",
                     manifest, r->commit, langc_commit) + 1;
  }
  if (r->dirty) return diag_fail(diag, "REFUSE_REBUILD_DIRTY", NULL, "'%s' is from a langc of a tree with a change", manifest) + 1;
  if (langc_dirty) return diag_fail(diag, "REFUSE_REBUILD_DIRTY", NULL, "this langc is from a tree with a change") + 1;
  for (i = 0; i < r->source_count && status == 0; i++) status = rebuild_source(r, i, arena, diag);
  return status != 0 ? status : rebuild_output(manifest, r, arena, diag);
}

/* D3: `langc rebuild MANIFEST`, in the D3c order. OPT->prog_path is MANIFEST. */
static int run_rebuild(const Options *opt, Arena *arena, Diag *diag) {
  char *text = NULL;
  size_t len = 0;
  ManifestRead r;
  if (!read_source(arena, opt->prog_path, &text, &len, diag)) return 2;
  switch (manifest_read(text, len, &r)) {
    case MANIFEST_READ_OK:
      break;
    case MANIFEST_READ_SHAPE:
      return diag_fail(diag, "REFUSE_REBUILD_MANIFEST", NULL, "'%s' is not one manifest line (D2a)", opt->prog_path) + 1;
    case MANIFEST_READ_NO_MEMORY:
      return diag_fail(diag, "OOM", NULL, "no memory for '%s'", opt->prog_path) + 2;
  }
  int status = rebuild_checks(opt->prog_path, &r, arena, diag);
  manifest_read_free(&r);
  return status;
}

/* One base name of PLAN 3.2 for `list-base`: the name, its kind (the
   Kind column of the row) and its slice. */
typedef struct {
  const char *name;
  const char *kind;
  const char *slice;
} BaseName;

/* Each name of the PLAN 3.2 admission table, in the order of the table
   (S9 D3). A row with more names gives one entry for each name. */
static const BaseName BASE_NAMES[] = {
  {"Bool", "base type", "S1"},
  {"true", "base op", "S1"},
  {"false", "base op", "S1"},
  {"Nat", "base type", "S1"},
  {"zero", "base op", "S1"},
  {"suc", "base op", "S1"},
  {"natAdd", "base op", "S4"},
  {"natSub", "base op", "S4"},
  {"natMul", "base op", "S4"},
  {"natDiv", "base op", "S4"},
  {"natMod", "base op", "S4"},
  {"natEq", "base op", "S4"},
  {"natLe", "base op", "S4"},
  {"lt", "defined", "S4"},
  {"Digest", "base type", "S4"},
  {"LogW", "base type", "S4"},
  {"logW1", "base op", "S4"},
  {"logW2", "base op", "S4"},
  {"logW4", "base op", "S4"},
  {"logW8", "base op", "S4"},
  {"logWBits", "base op", "S4"},
  {"digest", "base op", "S4"},
  {"digestEq", "base op", "S4"},
  {"digit", "base op", "S4"},
  {"Tweak", "defined", "S4"},
  {"thChain", "hash op", "S4"},
  {"thNode", "hash op", "S4"},
  {"thMsg", "hash op", "S4"},
  {"prf", "hash op", "S4"},
  {"Params", "defined", "S4"},
  {"chain", "defined", "S4"},
  {"wotsPk", "defined", "S4"},
  {"wotsSign", "defined", "S4"},
  {"wotsPkFromSig", "defined", "S4"},
  {"ltree", "defined", "S4"},
  {"treeHash", "defined", "S4"},
  {"authPath", "defined", "S4"},
  {"rootFromPath", "defined", "S4"},
  {"pkFromSeeds", "defined", "S4"},
  {"signAt", "defined", "S4"},
  {"PubKey", "defined", "S4"},
  {"Sig", "defined", "S4"},
  {"boolElim", "base op", "S4"},
  {"verify", "defined", "S4"},
  {"Leaf", "resource", "S3"},
  {"Keystore", "resource", "S3"},
  {"Durable", "CM (domain)", "S3"},
  {"keygen", "base op", "S5"},
  {"publicKey", "base op", "S5"},
  {"next", "base op", "S5"},
  {"sign", "base op", "S5"},
  {"leafHeight", "base op", "S5"},
  {"verifyAt", "defined", "S5"},
  {"andThen", "base op", "S5"},
  {"accept", "defined", "S6"},
  {"sigHash", "target boundary (EVM)", "S6"},
  {"txNonce", "target boundary (EVM)", "S6"},
  {"approve", "target boundary (EVM)", "S6"},
  {"commit", "target boundary (Wasm)", "S5"},
  {"aggregate", "defined", "S7"}
};

#define BASE_NAME_COUNT (sizeof BASE_NAMES / sizeof BASE_NAMES[0])

/* The `list-base` tool of `langc mcp` (S9 D3): one JSON line, each base
   name of PLAN 3.2 with its kind and slice, then each declaration of
   domain/domain.lang with its kind and type. */
static int list_base(Arena *arena, FILE *out, Diag *diag) {
  Front front;
  FmtText type;
  size_t i;
  if (!front_load(arena, "<domain>", "", 0, &front, diag)) return 1;
  for (i = 0; i < BASE_NAME_COUNT; i++) {
    fputs(i == 0 ? "{\"base\":[{\"name\":" : ",{\"name\":", out);
    put_json_str(BASE_NAMES[i].name, out);
    fputs(",\"kind\":", out);
    put_json_str(BASE_NAMES[i].kind, out);
    fputs(",\"slice\":", out);
    put_json_str(BASE_NAMES[i].slice, out);
    fputc('}', out);
  }
  fputs("],\"domain\":[", out);
  for (i = 0; i < front.domain_count; i++) {
    const Decl *d = &front.decls.items[i];
    fmt_text_init(&type, arena);
    if (d->type != NULL)
      fmt_term(&type, d->type);
    else
      fmt_family_type(&type, d);
    fputs(i == 0 ? "{\"name\":" : ",{\"name\":", out);
    put_json_str(d->name, out);
    fputs(",\"kind\":", out);
    put_json_str(explain_kind(d->kind), out);
    fputs(",\"type\":", out);
    put_json_bytes(type.data, type.oom ? 0 : type.len, out);
    fputc('}', out);
  }
  fputs("]}\n", out);
  return 0;
}

/* The tools of `langc mcp` that need the front (S9 D3): TOOL on the LEN
   bytes of the program TEXT, the tool text on OUT. Returns 0, or 1 for
   isError true. */
static int mcp_tool(McpTool tool, const char *text, size_t len, const char *name, FILE *out) {
  Options opt;
  Diag diag;
  Arena arena;
  int status = 0;
  memset(&opt, 0, sizeof opt);
  opt.json = 1;
  opt.prog_path = "source";
  opt.text = text;
  opt.text_len = len;
  opt.entry = name;
  opt.out = out;
  diag_init(&diag);
  arena_init(&arena, ARENA_LIMIT_BYTES);
  switch (tool) {
    case MCP_TOOL_CHECK:
      opt.command = CMD_CHECK;
      status = run_check(&opt, &arena, &diag);
      break;
    case MCP_TOOL_FMT:
      opt.command = CMD_FMT;
      status = run_fmt(&opt, &arena, &diag);
      if (status != 0 && diag.set) diag_print_json(&diag, out);
      break;
    case MCP_TOOL_EXPLAIN:
      opt.command = CMD_EXPLAIN;
      status = run_explain(&opt, &arena, &diag);
      break;
    case MCP_TOOL_LIST_BASE:
      status = list_base(&arena, out, &diag);
      if (status != 0 && diag.set) diag_print_json(&diag, out);
      break;
  }
  arena_release(&arena);
  return status != 0;
}

int main(int argc, char **argv) {
  Diag diag;
  diag_init(&diag);
  Options opt;
  if (!parse_options(argc, argv, &opt, &diag)) {
    diag_print(&diag, stderr);
    return usage(stderr);
  }
  if (opt.command == CMD_MCP) return mcp_serve(mcp_tool, stdin, stdout);
  Arena arena;
  arena_init(&arena, ARENA_LIMIT_BYTES);
  int status = opt.command == CMD_FMT        ? run_fmt(&opt, &arena, &diag)
               : opt.command == CMD_CHECK    ? run_check(&opt, &arena, &diag)
               : opt.command == CMD_SIGNER   ? run_signer(&opt, &arena, &diag)
               : opt.command == CMD_VERIFIER ? run_verifier(&opt, &arena, &diag)
               : opt.command == CMD_REBUILD  ? run_rebuild(&opt, &arena, &diag)
               : opt.command == CMD_EXPLAIN  ? run_explain(&opt, &arena, &diag)
                                             : run(&opt, &arena, &diag);
  arena_release(&arena);
  /* run_check and run_explain print their own diagnostics. */
  if (status != 0 && diag.set && opt.command != CMD_CHECK && opt.command != CMD_EXPLAIN) diag_print(&diag, stderr);
  return status;
}
