#include <string.h>

#include "front/fmt.h"

#define SYM_ARROW "\xE2\x86\x92"  /* → */
#define SYM_LAMBDA "\xCE\xBB"     /* λ */
#define SYM_TIMES "\xC3\x97"      /* × */
#define SYM_LOLLI "\xE2\x8A\xB8"  /* ⊸ */
#define SYM_NAT "\xE2\x84\x95"    /* ℕ */
#define SYM_MEET "\xE2\x88\xA7"   /* ∧ */
#define SYM_JOIN "\xE2\x88\xA8"   /* ∨ */
#define SYM_MAPSTO "\xE2\x86\xA6" /* ↦ */

/* Precedence levels. A term below the level of its slot gets parentheses. */
enum {
  LEVEL_BINDER, /* λ, Pi */
  LEVEL_SIGMA,
  LEVEL_JOIN,   /* r ∨ s */
  LEVEL_MEET,   /* r ∧ s */
  LEVEL_APP,    /* application, Type n, ~ r */
  LEVEL_ATOM
};

void fmt_text_init(FmtText *out, Arena *arena) {
  out->arena = arena;
  out->data = NULL;
  out->len = 0;
  out->cap = 0;
  out->oom = 0;
}

static void put_bytes(FmtText *out, const char *bytes, size_t len) {
  if (out->oom || len == 0) return;
  if (out->len + len > out->cap) {
    size_t next = out->cap == 0 ? 256 : out->cap;
    while (next < out->len + len) next *= 2;
    char *fresh = arena_alloc(out->arena, next);
    out->oom = fresh == NULL;
    if (out->oom) return;
    if (out->len > 0) memcpy(fresh, out->data, out->len);
    out->data = fresh;
    out->cap = next;
  }
  memcpy(out->data + out->len, bytes, len);
  out->len += len;
}

static void put_str(FmtText *out, const char *text) {
  put_bytes(out, text, strlen(text));
}

static void put_nat(FmtText *out, uint64_t nat) {
  char digits[24];
  size_t at = sizeof digits;
  do {
    digits[--at] = (char)('0' + nat % 10);
    nat /= 10;
  } while (nat > 0);
  put_bytes(out, digits + at, sizeof digits - at);
}

/* The core name Nat prints as `ℕ`. */
static void put_name(FmtText *out, const char *name) {
  put_str(out, strcmp(name, "Nat") == 0 ? SYM_NAT : name);
}

/* `r ∧ s` parses to `∧ r s` and `r ∨ s` to `∨ r s`. The result is the
   connective, or NULL for any other term. */
static const char *infix_op(const Term *t) {
  const Term *head = t->kind == TERM_APP && t->left->kind == TERM_APP ? t->left->left : NULL;
  int infix = head != NULL && head->kind == TERM_VAR && (strcmp(head->name, SYM_MEET) == 0 || strcmp(head->name, SYM_JOIN) == 0);
  return infix ? head->name : NULL;
}

/* 1 when T is a system cell `φ ↦ u` applied to the other cells. */
static int is_system_cell(const Term *t) {
  return t->kind == TERM_APP && t->left->kind == TERM_APP && t->left->left->kind == TERM_APP
         && t->left->left->left->kind == TERM_VAR && strcmp(t->left->left->left->name, SYM_MAPSTO) == 0;
}

static int app_level(const Term *t) {
  const char *op = infix_op(t);
  if (is_system_cell(t)) return LEVEL_ATOM;
  if (op == NULL) return LEVEL_APP;
  return strcmp(op, SYM_MEET) == 0 ? LEVEL_MEET : LEVEL_JOIN;
}

static int term_level(const Term *t) {
  switch (t->kind) {
    case TERM_VAR:
    case TERM_NAT:
      return LEVEL_ATOM;
    case TERM_APP:
      return app_level(t);
    case TERM_UNIV:
      return LEVEL_APP;
    case TERM_SIGMA:
      return LEVEL_SIGMA;
    case TERM_LAM:
    case TERM_PI:
      return LEVEL_BINDER;
  }
  return LEVEL_BINDER;
}

static void put_term(FmtText *out, const Term *t, int level);

/* The binder mark and its space; a binder with no mark is omega. */
static const char *mult_mark(Mult mult) {
  switch (mult) {
    case MULT_OMEGA: return "";
    case MULT_ZERO: return "@0 ";
    case MULT_ONE: return "@1 ";
  }
  return "";
}

/* `(x : A)`, or `(@0 x : A)` and `(@1 x : A)` for a marked binder. */
static void put_group(FmtText *out, Mult mult, const char *name, const Term *type) {
  put_str(out, "(");
  put_str(out, mult_mark(mult));
  put_str(out, name);
  put_str(out, " : ");
  put_term(out, type, LEVEL_BINDER);
  put_str(out, ")");
}

/* `λ x (y : A) → t`: all leading binders in one λ. */
static void put_lambda(FmtText *out, const Term *t) {
  put_str(out, SYM_LAMBDA);
  for (; t->kind == TERM_LAM; t = t->right) {
    put_str(out, " ");
    if (t->left == NULL) put_str(out, t->name);
    else put_group(out, t->mult, t->name, t->left);
  }
  put_str(out, " " SYM_ARROW " ");
  put_term(out, t, LEVEL_BINDER);
}

/* Pi and Sigma. A named binder is a group and is never merged. */
static void put_binary(FmtText *out, const Term *t, const char *op, int left_level, int right_level) {
  if (t->name != NULL) put_group(out, t->mult, t->name, t->left);
  else put_term(out, t->left, left_level);
  put_str(out, op);
  put_term(out, t->right, right_level);
}

/* `r ∧ s ∧ t` is `r ∧ (s ∧ t)`: the left operand is one level tighter. */
static void put_infix(FmtText *out, const Term *t, const char *op) {
  int level = term_level(t);
  put_term(out, t->left->right, level + 1);
  put_str(out, " ");
  put_str(out, op);
  put_str(out, " ");
  put_term(out, t->right, level);
}

/* `[ φ ↦ u , ψ ↦ v ]`: the cells of a `↦` chain, which ends at `[]`. */
static void put_system(FmtText *out, const Term *t) {
  put_str(out, "[ ");
  for (; is_system_cell(t); t = t->right) {
    put_term(out, t->left->left->right, LEVEL_BINDER);
    put_str(out, " " SYM_MAPSTO " ");
    put_term(out, t->left->right, LEVEL_BINDER);
    put_str(out, is_system_cell(t->right) ? " , " : " ]");
  }
}

static void put_term(FmtText *out, const Term *t, int level) {
  int paren = term_level(t) < level;
  if (paren) put_str(out, "(");
  switch (t->kind) {
    case TERM_VAR:
      put_name(out, t->name);
      break;
    case TERM_NAT:
      put_nat(out, t->nat);
      break;
    case TERM_UNIV:
      put_str(out, "Type ");
      put_nat(out, t->nat);
      break;
    case TERM_APP:
      if (is_system_cell(t)) {
        put_system(out, t);
        break;
      }
      if (infix_op(t) != NULL) {
        put_infix(out, t, infix_op(t));
        break;
      }
      put_term(out, t->left, LEVEL_APP);
      put_str(out, " ");
      put_term(out, t->right, LEVEL_ATOM);
      break;
    case TERM_LAM:
      put_lambda(out, t);
      break;
    case TERM_PI:
      /* `A ⊸ B` is the Pi at one with no binder name. */
      put_binary(out, t, t->name == NULL && t->mult == MULT_ONE ? " " SYM_LOLLI " " : " " SYM_ARROW " ", LEVEL_SIGMA,
                 LEVEL_BINDER);
      break;
    case TERM_SIGMA:
      put_binary(out, t, " " SYM_TIMES " ", LEVEL_JOIN, LEVEL_SIGMA);
      break;
  }
  if (paren) put_str(out, ")");
}

/* `name : T` on one line, then the clause `name x y = t`: the leading bare
   binders of the body become the clause arguments. */
static void put_def(FmtText *out, const Decl *d) {
  const Term *body = d->body;
  if (d->is_rec) put_str(out, "rec ");
  put_str(out, d->name);
  put_str(out, " : ");
  put_term(out, d->type, LEVEL_BINDER);
  put_str(out, "\n");
  put_str(out, d->name);
  for (; body->kind == TERM_LAM && body->left == NULL; body = body->right) {
    put_str(out, " ");
    put_str(out, body->name);
  }
  put_str(out, " = ");
  put_term(out, body, LEVEL_BINDER);
}

/* `  c : (f : A) → F p1 .. pn` */
static void put_ctor(FmtText *out, const Decl *d, const Ctor *c) {
  put_str(out, "\n  ");
  put_str(out, c->name);
  put_str(out, " : ");
  for (size_t i = 0; i < c->field_count; i++) {
    put_group(out, MULT_OMEGA, c->fields[i].name, c->fields[i].type);
    put_str(out, " " SYM_ARROW " ");
  }
  put_str(out, d->name);
  for (size_t i = 0; i < d->param_count; i++) {
    put_str(out, " ");
    put_str(out, d->params[i].name);
  }
}

/* `  o : F p1 .. pn → T` (S2 U6c1) */
static void put_obs(FmtText *out, const Decl *d, const Ctor *c) {
  put_str(out, "\n  ");
  put_str(out, c->name);
  put_str(out, " : ");
  put_str(out, d->name);
  for (size_t i = 0; i < d->param_count; i++) {
    put_str(out, " ");
    put_str(out, d->params[i].name);
  }
  put_str(out, " " SYM_ARROW " ");
  put_term(out, c->fields[0].type, LEVEL_BINDER);
}

/* `data F (p : A) : Type where`, then one constructor a line; `codata`
   has one observation a line. */
static void put_data(FmtText *out, const Decl *d) {
  put_str(out, d->kind == DECL_COFAMILY ? "codata " : "data ");
  put_str(out, d->name);
  for (size_t i = 0; i < d->param_count; i++) {
    put_str(out, " ");
    put_group(out, MULT_OMEGA, d->params[i].name, d->params[i].type);
  }
  put_str(out, " : Type where");
  for (size_t i = 0; i < d->ctor_count; i++) {
    if (d->kind == DECL_COFAMILY)
      put_obs(out, d, &d->ctors[i]);
    else
      put_ctor(out, d, &d->ctors[i]);
  }
}

void fmt_decl(FmtText *out, const Decl *decl) {
  switch (decl->kind) {
    case DECL_DEF:
      put_def(out, decl);
      break;
    case DECL_AXIOM:
      put_str(out, "postulate ");
      put_str(out, decl->name);
      put_str(out, " : ");
      put_term(out, decl->type, LEVEL_BINDER);
      break;
    case DECL_FAMILY:
    case DECL_COFAMILY:
      put_data(out, decl);
      break;
  }
}

static void put_comment(FmtText *out, const Comment *c) {
  put_bytes(out, c->text, c->len);
  put_str(out, "\n");
}

void fmt_region(FmtText *out, const Decl *decls, size_t i, const TokenList *toks, size_t *next_comment) {
  const Decl *d = &decls[i];
  if (i > 0) put_str(out, "\n");
  for (; *next_comment < toks->comment_count && toks->comments[*next_comment].line <= d->end_line; (*next_comment)++) {
    put_comment(out, &toks->comments[*next_comment]);
  }
  fmt_decl(out, d);
  put_str(out, "\n");
}

void fmt_tail(FmtText *out, size_t decl_count, const TokenList *toks, size_t next_comment) {
  if (decl_count > 0 && next_comment < toks->comment_count) put_str(out, "\n");
  for (; next_comment < toks->comment_count; next_comment++) put_comment(out, &toks->comments[next_comment]);
}

int fmt_program(FmtText *out, const Decl *decls, size_t count, const TokenList *toks) {
  size_t next_comment = 0;
  for (size_t i = 0; i < count; i++) fmt_region(out, decls, i, toks, &next_comment);
  fmt_tail(out, count, toks, next_comment);
  return !out->oom;
}
