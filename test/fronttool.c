/* Domain declaration regressions use the same parser and checker as langc.
   `fronttool dump FILE` prints the syntax tree of FILE, one node a line and
   no positions, so a file and its fmt text can be compared. */
#include <stdio.h>
#include <string.h>

#include "front/check.h"
#include "front/lexer.h"
#include "front/parser.h"

/* The Sum family of domain/domain.lang: domain() replaces the domain. */
#define SUM_DATA "data Sum (A B : Type 0) : Type where\n  inl : (inlOf : A) → Sum A B\n  inr : (inrOf : B) → Sum A B\n\n"

/* A copy of T where a variable with a `'` names the private name with the
   each `'` as `.` (S2 U6c2b, Q-S2-U6c2-1 (a)). No source can write a `.`
   name, so this test path reaches D.walk, D.corec and D.here. */
static const Term *dotted(Arena *arena, const Term *t) {
  Term *n = t == NULL ? NULL : arena_alloc(arena, sizeof *n);
  const char *q = n == NULL || t->kind != TERM_VAR ? NULL : strchr(t->name, '\'');
  char *s = q == NULL ? NULL : arena_alloc(arena, strlen(t->name) + 1u);
  if (n == NULL)
    return t;
  *n = *t;
  if (s != NULL) {
    memcpy(s, t->name, strlen(t->name) + 1u);
    for (size_t i = 0; s[i] != '\0'; i++)
      if (s[i] == '\'') s[i] = '.';
    n->name = s;
  }
  n->left = dotted(arena, t->left);
  n->right = dotted(arena, t->right);
  return n;
}

static int dot_decls(Arena *arena, DeclList *decls) {
  size_t i;
  for (i = 0; i < decls->count; i++) {
    decls->items[i].type = dotted(arena, decls->items[i].type);
    decls->items[i].body = dotted(arena, decls->items[i].body);
  }
  return 1;
}

static int domain_run(const char *text, const char *want, int dot) {
  Arena arena;
  Diag diag;
  TokenList tokens = {0};
  DeclList decls = {0};
  Machine machine;
  arena_init(&arena, (size_t)1 << 24);
  diag_init(&diag);
  int ok = lex_source(&arena, "test-domain", text, strlen(text), &tokens, &diag)
    && parse_source(&arena, "test-domain", ORIGIN_DOMAIN, &tokens, &decls, &diag)
    && (dot == 0 || dot_decls(&arena, &decls))
    && check_program(&arena, &decls, &machine, &diag);
  int passed = want == NULL ? ok : !ok && diag.code != NULL && strcmp(diag.code, want) == 0;
  if (!passed) {
    fprintf(stderr, "FAIL domain: wanted %s, got %s: %s\n", want == NULL ? "ok" : want,
            ok ? "ok" : diag.code == NULL ? "no diagnostic" : diag.code, text);
  }
  arena_release(&arena);
  return passed;
}

static int domain(const char *text, const char *want) {
  return domain_run(text, want, 0);
}

/* domain() with each `'` variable read as a private `.` name (dotted). */
static int domain_dotted(const char *text, const char *want) {
  return domain_run(text, want, 1);
}

/* A printed normal form must check at its original type and convert back. */
static int normal_roundtrip(const char *name) {
  static char source[1 << 16], normal[1 << 16], copy[1 << 17];
  const char *files[] = {"domain/domain.lang", "test/eval/print-cubical.lang"};
  size_t len = 0;
  for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
    FILE *in = fopen(files[i], "rb");
    if (in == NULL) return 0;
    size_t n = fread(source + len, 1, sizeof source - len - 2u, in);
    int ok = !ferror(in) && feof(in);
    fclose(in);
    if (!ok) return 0;
    len += n;
    source[len++] = '\n';
  }
  Arena arena;
  Diag diag;
  TokenList tokens = {0}, printed_tokens = {0};
  DeclList decls = {0}, printed = {0};
  Machine machine;
  arena_init(&arena, (size_t)1 << 27);
  diag_init(&diag);
  int ok = lex_source(&arena, "roundtrip", source, len, &tokens, &diag)
    && parse_source(&arena, "roundtrip", ORIGIN_DOMAIN, &tokens, &decls, &diag)
    && check_program(&arena, &decls, &machine, &diag);
  uint32_t index = 0;
  size_t decl = 0;
  if (ok) {
    while (index < machine.def_count && strcmp(machine.defs[index].name, name) != 0) index++;
    while (decl < decls.count && strcmp(decls.items[decl].name, name) != 0) decl++;
    ok = index < machine.def_count && decl < decls.count
      && value_print(&machine, NULL, 0, def_value(&machine, index), normal, sizeof normal);
  }
  if (ok) {
    int n = snprintf(copy, sizeof copy, "roundtripCopy : Unit\nroundtripCopy = %s\n", normal);
    ok = n > 0 && (size_t)n < sizeof copy
      && lex_source(&arena, "printed", copy, (size_t)n, &printed_tokens, &diag)
      && parse_source(&arena, "printed", ORIGIN_DOMAIN, &printed_tokens, &printed, &diag)
      && printed.count == 1u;
  }
  if (ok) {
    Decl *all = arena_alloc(&arena, (decls.count + 1u) * sizeof *all);
    ok = all != NULL;
    if (ok) {
      memcpy(all, decls.items, decls.count * sizeof *all);
      all[decls.count] = printed.items[0];
      all[decls.count].type = decls.items[decl].type;
      decls.items = all;
      decls.count++;
      ok = check_program(&arena, &decls, &machine, &diag)
        && conv_values(&machine, 0, def_value(&machine, index), def_value(&machine, machine.def_count - 1u));
    }
  }
  if (!ok) {
    fprintf(stderr, "FAIL normal roundtrip %s\n", name);
    if (diag.set) diag_print(&diag, stderr);
  }
  arena_release(&arena);
  return ok;
}

/* Even malformed source bytes must produce a readable JSON diagnostic. */
static int json_encoding(void) {
  static const struct { const char *source; const char *found; } cases[] = {
    {"\xff", "\\u00ff"},
    {"\x80", "\\u0080"},
    {"\xc0\xaf", "\\u00c0\\u00af"},
    {"\xe2\x86", "\\u00e2\\u0086"},
    {"\xe0\x80\x80", "\\u00e0\\u0080\\u0080"},
    {"\xed\xa0\x80", "\\u00ed\\u00a0\\u0080"},
    {"\xf4\x90\x80\x80", "\\u00f4\\u0090\\u0080\\u0080"},
    {"\xc3\xa9", "\xc3\xa9"},
    {"\xe2\x98\x83", "\xe2\x98\x83"},
    {"\xf0\x9f\x98\x80", "\xf0\x9f\x98\x80"},
  };
  int passed = 1;
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    Arena arena;
    Diag diag;
    TokenList tokens = {0};
    arena_init(&arena, (size_t)1 << 16);
    diag_init(&diag);
    int lexed = lex_source(&arena, "test-encoding", cases[i].source, strlen(cases[i].source), &tokens, &diag);
    FILE *out = tmpfile();
    char got[2048] = {0};
    char want[128];
    snprintf(want, sizeof want, "\"found\":\"%s\"", cases[i].found);
    if (out != NULL) {
      if (diag.set) diag_print_json(&diag, out);
      rewind(out);
      fread(got, 1, sizeof got - 1, out);
      fclose(out);
    }
    if (lexed || out == NULL || strstr(got, want) == NULL) {
      fprintf(stderr, "FAIL JSON encoding case %zu: expected %s\n", i, want);
      passed = 0;
    }
    arena_release(&arena);
  }
  return passed;
}

static const char *const TERM_KINDS[] = {"var", "nat", "app", "lam", "pi", "sigma", "univ"};

/* A switch, not a table: -Wswitch-enum refuses a new kind with no name. */
static const char *decl_kind(DeclKind kind) {
  switch (kind) {
    case DECL_DEF: return "def";
    case DECL_AXIOM: return "postulate";
    case DECL_FAMILY: return "data";
    case DECL_COFAMILY: return "codata";
  }
  return "?";
}

/* A cofamily line is an observation: one field, its type after self. */
static const char *line_kind(DeclKind kind) {
  switch (kind) {
    case DECL_DEF: return "ctor";
    case DECL_AXIOM: return "ctor";
    case DECL_FAMILY: return "ctor";
    case DECL_COFAMILY: return "obs";
  }
  return "?";
}

static void dump_term(const char *role, const Term *t, int depth) {
  if (t == NULL) return;
  printf("%*s%s %s", depth * 2, "", role, TERM_KINDS[t->kind]);
  if (t->name != NULL) printf(" %s", t->name);
  if (t->kind == TERM_NAT || t->kind == TERM_UNIV) printf(" %llu", (unsigned long long)t->nat);
  putchar('\n');
  dump_term("left", t->left, depth + 1);
  dump_term("right", t->right, depth + 1);
}

static void dump_fields(const char *role, const Field *fields, size_t count, int depth) {
  size_t i;
  for (i = 0; i < count; i++) {
    printf("%*s%s %s\n", depth * 2, "", role, fields[i].name == NULL ? "_" : fields[i].name);
    dump_term("type", fields[i].type, depth + 1);
  }
}

/* Prints each declaration, then its parameters, type, body and constructors. */
static int dump(const char *file) {
  static char text[1 << 20];
  FILE *in = fopen(file, "rb");
  if (in == NULL) {
    fprintf(stderr, "fronttool: cannot read %s\n", file);
    return 2;
  }
  size_t len = fread(text, 1, sizeof text, in);
  fclose(in);
  if (len == sizeof text) {
    fprintf(stderr, "fronttool: %s is too large\n", file);
    return 2;
  }
  Arena arena;
  Diag diag;
  TokenList tokens = {0};
  DeclList decls = {0};
  arena_init(&arena, (size_t)1 << 24);
  diag_init(&diag);
  int ok = lex_source(&arena, file, text, len, &tokens, &diag)
    && parse_source(&arena, file, ORIGIN_DOMAIN, &tokens, &decls, &diag);
  if (!ok) diag_print(&diag, stderr);
  size_t i, j;
  for (i = 0; ok && i < decls.count; i++) {
    const Decl *d = &decls.items[i];
    printf("%s %s%s\n", decl_kind(d->kind), d->name, d->is_rec ? " rec" : "");
    dump_fields("param", d->params, d->param_count, 1);
    dump_term("type", d->type, 1);
    dump_term("body", d->body, 1);
    for (j = 0; j < d->ctor_count; j++) {
      printf("  %s %s\n", line_kind(d->kind), d->ctors[j].name);
      dump_fields("field", d->ctors[j].fields, d->ctors[j].field_count, 2);
    }
  }
  arena_release(&arena);
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "dump") == 0) return dump(argv[2]);
  if (argc != 1) {
    fputs("usage: fronttool [dump FILE]\n", stderr);
    return 2;
  }
  int ok = json_encoding();
  ok &= domain("data F : Type where\n  makeF : (field : ℕ) → (field : Bool) → F", "REFUSE_NAME");
  ok &= domain("data F : Type where\n  makeF : (makeF : ℕ) → F", "REFUSE_NAME");
  ok &= domain("data F : Type where\n  makeF : (firstField : ℕ) → (secondField : Bool) → F", NULL);
  ok &= domain("data F : Type where\n  a : (field : ℕ) → F\n  b : (field : ℕ) → F", "REFUSE_NAME");
  /* bind on a Sum: not_carrier (S2 R1 (b): Option is the one monad carrier). */
  ok &= domain(SUM_DATA "s : Sum ℕ ℕ\ns = inl ℕ ℕ 7\n\nbad : Sum Bool ℕ\nbad = bind s (λ x → inr Bool ℕ x)", "TYPE_MISMATCH");
  ok &= domain(SUM_DATA "data Lot (n : ℕ) : Type where\n  makeLot : (amount : ℕ) → Lot n\n\n"
               "s : Sum ℕ ℕ\ns = inl ℕ ℕ 0\n\n"
               "f : (n : ℕ) → ℕ → Lot n\nf n x = makeLot n x\n\n"
               "bad : (n : ℕ) → Lot n\nbad = fold f f s", "TYPE_MISMATCH");
  ok &= domain("bad : Type 1\nbad = ℕ", "TYPE_MISMATCH");
  ok &= domain("bad : Type 2\nbad = Type 1", "TYPE_UNIVERSE");
  ok &= domain("data U : Type where\n  makeU : (decode : Type 0) → U\n\nbad : Type 0\nbad = U", "TYPE_MISMATCH");
  ok &= domain("data U : Type where\n  makeU : (decode : Type 0) → U\n\n"
               "data W : Type where\n  makeW : (wrapped : U) → W\n\nbad : Type 0\nbad = W", "TYPE_MISMATCH");
  ok &= domain("data Box (A : Type 1) : Type where\n  box : (unbox : A) → Box A\n\nbad : Type 0\nbad = Box (Type 0)", "TYPE_MISMATCH");
  ok &= domain("data U : Type where\n  makeU : (decode : Type 0) → U\n\ngood : Type 1\ngood = U", NULL);
  ok &= domain("eta : (p : (n : ℕ) × Path ℕ n n) → Path ((n : ℕ) × Path ℕ n n) (pack (witness p) (payload p)) p\n"
               "eta p = refl", NULL);
  /* S2 U6a: a family with a type parameter is a cell signature (REFUSE_CELL
     before DOMAIN_FAMILY); the S1 grammar has no type index. */
  ok &= domain("data Pair (A : Type 0) : Type where\n  mk : (x : A) → (y : A) → Pair A", "REFUSE_CELL");
  ok &= domain("data Seq (A : Type 0) : Type where\n  snil : Seq A\n  scons : (hd : A) → (tl : Seq A) → Seq A", "REFUSE_CELL");
  ok &= domain("data Vec (A : Type 0) (n : ℕ) : Type where\n  vbox : (vOf : A) → Vec A n", "REFUSE_CELL");
  ok &= domain("data Ap (F : ℕ → Type 0) : Type where\n  ap : (apOf : F 0) → Ap F", "REFUSE_CELL");
  ok &= domain("data Ph (A : Type 0) : Type where\n  ph : Ph A", "REFUSE_CELL");
  ok &= domain("data Two (A : Type 0) : Type where\n  t1 : (a : A) → Two A\n  t2 : (b : A) → Two A", "REFUSE_CELL");
  /* S2 C1: a type-parameter field beside a closed field, with a generator; only cell_field (the cons shape) refuses it. */
  ok &= domain("data Gp (A : Type 0) : Type where\n  gp : (g : A) → Gp A\n  gtag : (hd : A) → (tagOf : ℕ) → Gp A", "REFUSE_CELL");
  ok &= domain("data Box (A : Type 0) : Type where\n  box : (boxOf : A) → Box A\n\n"
               "data Wr (A : Type 0) : Type where\n  wr : (w : A) → Wr A\n  wb : (wbox : Box A) → Wr A", "REFUSE_CELL");
  ok &= domain("data Fm (A : Type 0) : Type where\n  fmEta : (e : A) → Fm A\n  fmMul : (l : Fm A) → (r : Fm A) → Fm A", "DOMAIN_FAMILY");
  ok &= domain("data F : Type 0 → Type where\n  mkF : (n : ℕ) → F ℕ", "PARSE_EXPECT");
  ok &= domain("data Sum2 (A B : Type 0) : Type where\n  inl2 : (inlOf : A) → Sum2 A B\n  inr2 : (inrOf : B) → Sum2 A B\n"
               "  tag2 : (t : ℕ) → (u : ℕ) → Sum2 A B\n\n"
               "s : Sum2 ℕ Bool\ns = inl2 ℕ Bool 3\n\nm : ℕ\nm = fold (λ x → x) (λ y → 0) (λ t u → t) s", NULL);
  /* S2 U6c1: a codata is a cocell signature (REFUSE_CELL, PLAN 2.1 CC row);
     an admitted one binds D, D.pos and one destructor a line (U6c2a). */
  ok &= domain("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A", NULL);
  ok &= domain("codata Lab (A : Type 0) : Type where\n  hd : Lab A → A\n  step : Lab A → ℕ → Lab A", NULL);
  /* S2 U6c2a: the destructors type a program use (R8: D .A unfolds to a Pi). */
  ok &= domain("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A\n\n"
               "second : Stream ℕ → ℕ\nsecond = λ s → head ℕ (tail ℕ s)", NULL);
  ok &= domain("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A\n\n"
               "bad : Stream ℕ → Bool\nbad = λ s → head ℕ s", "TYPE_MISMATCH");
  ok &= domain("codata Lab (A : Type 0) : Type where\n  hd : Lab A → A\n  step : Lab A → Bool → Lab A\n\n"
               "nx : Lab ℕ → Lab ℕ\nnx = λ s → step ℕ s 3", "TYPE_MISMATCH");
  ok &= domain("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A\n\n"
               "head : ℕ\nhead = 0", "REFUSE_NAME");
  /* S2 U6c2b: the walk D.walk and the private corecursor D.corec
     (Q-S2-U6c2-1 (a), reached through domain_dotted). The head of a
     corecursor stream is the seed observation; the tail applies the
     step; the walk at D.here is the identity. */
  ok &= domain_dotted("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A\n\n"
                      "h0 : Path ℕ (head ℕ (Stream'corec ℕ ℕ (λ s → s) (λ s → 9) 7)) 7\nh0 = refl", NULL);
  ok &= domain_dotted("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A\n\n"
                      "h1 : Path ℕ (head ℕ (tail ℕ (Stream'corec ℕ ℕ (λ s → s) (λ s → 9) 7))) 9\nh1 = refl", NULL);
  ok &= domain_dotted("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A\n\n"
                      "h2 : Path ℕ (head ℕ (Stream'corec ℕ ℕ (λ s → s) (λ s → 9) 7)) 9\nh2 = λ i → head ℕ (Stream'corec ℕ ℕ (λ s → s) (λ s → 9) 7)", "TYPE_ENDPOINT");
  ok &= domain_dotted("codata Stream (A : Type 0) : Type where\n  head : Stream A → A\n  tail : Stream A → Stream A\n\n"
                      "w : Path ℕ (Stream'walk ℕ (λ s → 9) Stream'here 5) 5\nw = refl", NULL);
  ok &= domain_dotted("codata Lab (A : Type 0) : Type where\n  hd : Lab A → A\n  step : Lab A → ℕ → Lab A\n\n"
                      "w : Path ℕ (Lab'walk ℕ (λ s e → e) (Lab'step'step 1 Lab'here) 0) 1\nw = refl", NULL);
  ok &= domain_dotted("codata Lab (A : Type 0) : Type where\n  hd : Lab A → A\n  step : Lab A → ℕ → Lab A\n\n"
                      "c : Path ℕ (hd ℕ (step ℕ (Lab'corec ℕ ℕ (λ s → s) (λ s e → e) 0) 4)) 4\nc = refl", NULL);
  ok &= domain("codata Z (A : Type 0) : Type where\n  zt : Z A → Z A", "REFUSE_CELL");
  /* Public cofield names cannot collide with private elaboration names. */
  ok &= domain("codata C (A : Type 0) : Type where\n  hd : C A → A\n  here : C A → C A", NULL);
  ok &= domain("codata C (A : Type 0) : Type where\n  hd : C A → A\n  pos : C A → C A", NULL);
  ok &= domain("codata C (A : Type 0) : Type where\n  hd : C A → A\n  walk : C A → C A", NULL);
  ok &= domain("codata C (A : Type 0) : Type where\n  hd : C A → A\n  corec : C A → C A", NULL);
  ok &= domain("codata Two (A : Type 0) : Type where\n  o1 : Two A → A\n  o2 : Two A → A\n  tl : Two A → Two A", "REFUSE_CELL");
  ok &= domain("codata P (A B : Type 0) : Type where\n  ph : P A B → A", "REFUSE_CELL");
  ok &= domain("codata Nt (n : ℕ) : Type where\n  nh : Nt n → ℕ", "REFUSE_CELL");
  ok &= domain("codata Bad (A : Type 0) : Type where\n  bh : Bad A → A\n  bn : Bad A → ℕ", "REFUSE_CELL");
  ok &= domain("codata Big (A : Type 0) : Type where\n  gh : Big A → A\n  gs : Big A → Type 0 → Big A", "REFUSE_CELL");
  ok &= domain("codata Nm (A : Type 0) : Type where\n  nm : (s : Nm A) → A", "PARSE_CTOR_RESULT");
  ok &= normal_roundtrip("glueType");
  ok &= normal_roundtrip("glueValue");
  ok &= normal_roundtrip("unglueValue");
  ok &= normal_roundtrip("hcompValue");
  if (ok) puts("domain regressions: passed");
  return ok ? 0 : 1;
}
