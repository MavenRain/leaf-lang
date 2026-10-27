/* The type checker (see front/check.h). SHARED by the tcc kits.

   Checking is bidirectional. A term is checked against an expected type (the
   hint) or its type is inferred. Two types are equal when their normal forms
   are equal (front/eval.c). The checker writes core terms (front/core.h). */
#include "front/check.h"

#include <stdio.h>
#include <string.h>

#define CHECK_DEPTH_LIMIT 2000u
#define SPINE_MAX 64u
#define SHOW_MAX 512u
#define NAMES_MAX 64u
#define UNIVERSE_MAX 2u
#define PRINT_MAX 65536u
#define NO_FAMILY UINT32_MAX
#define NO_DEF UINT32_MAX

#define FAIL(c, code, ...) diag_fail((c)->m->diag, (code), (c)->m->def, __VA_ARGS__)

typedef struct Scope Scope;
struct Scope {
  const char *name; /* NULL for the binder of `A -> B` */
  const Value *type;
  uint32_t level;
  Mult mult;     /* the binder multiplicity (PLAN 4.1) */
  uint32_t uses; /* the uses so far, each one scaled by the level */
  Scope *next;
};

typedef struct {
  Machine *m;
  const DeclList *decls;
  size_t decl_index;
  Scope *scope;       /* the innermost binder first */
  const Env *env;     /* a variable for each binder */
  uint32_t level;     /* the number of binders */
  unsigned depth;
  uint32_t eta_count;
  uint32_t self;      /* the family whose fields are checked, or NO_FAMILY */
  uint32_t self_uses; /* the uses of that family in the current field */
  const Value *face;  /* the faces of the enclosing system cells, or NULL */
  Mult at;            /* the level of the current term: 0 in a type position, else 1 (PLAN 4.1) */
} Checker;

static int resource_type(Checker *c, const Value *v);
static int copyable_type(Checker *c, const Value *v);
static int pos_family(const Checker *c, const Value *v);

typedef enum {
  RULE_TYPE0,
  RULE_TYPE1,
  RULE_VALUE0,
  RULE_EMPTY,
  RULE_SOME,
  RULE_PACK,
  RULE_REFL,
  RULE_SIGMA_PART,
  RULE_UNPACK,
  RULE_OPTION,
  RULE_PURE,
  RULE_MAP,
  RULE_BIND,
  RULE_FOLD,
  RULE_SYMM,
  RULE_TRANS,
  RULE_CONG,
  RULE_TRANSPORT,
  RULE_NAT2,
  RULE_FLAG_IF,
  RULE_PATHP,
  RULE_INTERVAL,
  RULE_PARTIAL,
  RULE_SYSTEM,
  RULE_TRANSP,
  RULE_HCOMP,
  RULE_GLUE,
  RULE_GLUE_ELEM,
  RULE_UNGLUE
} Rule;

typedef struct {
  const char *name;
  uint32_t arity; /* fold: the least number of arguments */
  Rule rule;
  Op op;
  Op result;     /* VALUE0, NAT2: the result type. EMPTY, INJ: the type former. */
  uint32_t part; /* INJ, SIGMA_PART: 0 or 1. PATHP: 0 PathP, 1 Path. */
} Builtin;

static const Builtin BUILTINS[] = {
  {"Nat", 0, RULE_TYPE0, OP_NAT, OP_NAT, 0},
  {"Bool", 0, RULE_TYPE0, OP_BOOL, OP_NAT, 0},
  {"Unit", 0, RULE_TYPE0, OP_UNIT, OP_NAT, 0},
  {"Leaf", 0, RULE_TYPE0, OP_LEAF, OP_NAT, 0},
  {"Option", 1, RULE_TYPE1, OP_OPTION, OP_NAT, 0},
  {"I", 0, RULE_VALUE0, OP_I, OP_SSET, 0},
  {"i0", 0, RULE_VALUE0, OP_I0, OP_I, 0},
  {"i1", 0, RULE_VALUE0, OP_I1, OP_I, 0},
  {"~", 1, RULE_INTERVAL, OP_INEG, OP_I, 0},
  {"∧", 2, RULE_INTERVAL, OP_IMIN, OP_I, 0},
  {"∨", 2, RULE_INTERVAL, OP_IMAX, OP_I, 0},
  /* Faces and systems. The parser makes the heads of `[ φ ↦ u , ... ]`;
     no identifier can spell them. */
  {"Partial", 2, RULE_PARTIAL, OP_PARTIAL, OP_SSET, 0},
  {"↦", 3, RULE_SYSTEM, OP_SYSTEM, OP_NAT, 0},
  {"[]", 0, RULE_SYSTEM, OP_SYSNIL, OP_NAT, 0},
  /* Kan operations. */
  {"transp", 3, RULE_TRANSP, OP_TRANSP, OP_NAT, 0},
  {"hcomp", 2, RULE_HCOMP, OP_HCOMP, OP_NAT, 0},
  /* Glue types. */
  {"Glue", 2, RULE_GLUE, OP_GLUE, OP_NAT, 0},
  {"glue", 2, RULE_GLUE_ELEM, OP_GLUE_ELEM, OP_NAT, 0},
  {"unglue", 1, RULE_UNGLUE, OP_UNGLUE, OP_NAT, 0},
  {"PathP", 3, RULE_PATHP, OP_PATHP, OP_NAT, 0},
  {"Path", 3, RULE_PATHP, OP_PATHP, OP_NAT, 1},
  {"unit", 0, RULE_VALUE0, OP_UNIT_VAL, OP_UNIT, 0},
  {"true", 0, RULE_VALUE0, OP_TRUE, OP_BOOL, 0},
  {"false", 0, RULE_VALUE0, OP_FALSE, OP_BOOL, 0},
  {"none", 0, RULE_EMPTY, OP_NONE, OP_OPTION, 0},
  {"some", 1, RULE_SOME, OP_SOME, OP_OPTION, 0},
  {"pack", 2, RULE_PACK, OP_PACK, OP_NAT, 0},
  {"refl", 0, RULE_REFL, OP_PLAM, OP_PATHP, 0},
  {"witness", 1, RULE_SIGMA_PART, OP_WITNESS, OP_NAT, 0},
  {"payload", 1, RULE_SIGMA_PART, OP_PAYLOAD, OP_NAT, 1},
  {"unpack", 2, RULE_UNPACK, OP_NAT, OP_NAT, 0},
  {"option", 3, RULE_OPTION, OP_OPTION_ELIM, OP_OPTION, 0},
  {"pure", 1, RULE_PURE, OP_PURE, OP_NAT, 0},
  {"map", 2, RULE_MAP, OP_MAP, OP_NAT, 0},
  {"bind", 2, RULE_BIND, OP_BIND, OP_NAT, 0},
  {"fold", 2, RULE_FOLD, OP_FOLD_NAT, OP_NAT, 0},
  {"symm", 1, RULE_SYMM, OP_PLAM, OP_PATHP, 0},
  {"trans", 2, RULE_TRANS, OP_PLAM, OP_PATHP, 0},
  {"cong", 2, RULE_CONG, OP_PLAM, OP_PATHP, 0},
  {"transport", 3, RULE_TRANSPORT, OP_TRANSP, OP_PATHP, 0},
  {"natAdd", 2, RULE_NAT2, OP_NAT_ADD, OP_NAT, 0},
  {"natSub", 2, RULE_NAT2, OP_NAT_SUB, OP_NAT, 0},
  {"natMul", 2, RULE_NAT2, OP_NAT_MUL, OP_NAT, 0},
  {"natEq", 2, RULE_NAT2, OP_NAT_EQ, OP_BOOL, 0},
  {"natLe", 2, RULE_NAT2, OP_NAT_LE, OP_BOOL, 0},
  {"flagIf", 3, RULE_FLAG_IF, OP_FLAG_IF, OP_BOOL, 0}
};

#define BUILTIN_COUNT ((uint32_t)(sizeof BUILTINS / sizeof BUILTINS[0]))

/* Names that no declaration can take (rule R4). */
static const char *const RESERVED[] = {"Text"};

typedef enum {
  HEAD_TERM, /* a local, a definition or another term: plain application */
  HEAD_FAMILY,
  HEAD_CTOR,
  HEAD_PROJ,
  HEAD_BUILTIN
} HeadKind;

typedef struct {
  HeadKind kind;
  uint32_t index; /* FAMILY: the family. CTOR, PROJ: the constructor. */
  uint32_t field; /* PROJ: the field number */
  const Builtin *builtin;
} Head;

/* One application of a core name to the arguments of a spine. */
typedef struct {
  Checker *c;
  const Builtin *b;
  const Term *const *args;
  uint32_t argc;      /* the arguments in the spine */
  const Value *hint;  /* the expected type of the result, or NULL */
  const Value *outer; /* the expected type of the whole spine, or NULL */
  uint32_t used;      /* the arguments that the core name takes */
  const Value *type;  /* the type of the result */
} Call;

static const Core *nul(int failed) {
  (void)failed;
  return NULL;
}

static Core *new_core(Checker *c, CoreKind kind) {
  Core *k = arena_alloc(c->m->arena, sizeof *k);
  if (k == NULL) {
    FAIL(c, "OOM", "out of memory");
    return NULL;
  }
  k->kind = kind;
  return k;
}

static const Core *core_index(Checker *c, CoreKind kind, uint32_t index) {
  Core *k = new_core(c, kind);
  if (k != NULL)
    k->index = index;
  return k;
}

static const Core *core_leaf(Checker *c, CoreKind kind, uint64_t nat) {
  Core *k = new_core(c, kind);
  if (k != NULL)
    k->nat = nat;
  return k;
}

static const Core *core_binder(Checker *c, CoreKind kind, Mult mult, const char *name, const Core *a, const Core *b) {
  Core *k;
  if (b == NULL || (a == NULL && kind != CORE_LAM))
    return NULL;
  k = new_core(c, kind);
  if (k == NULL)
    return NULL;
  k->name = name;
  k->mult = mult;
  k->a = a;
  k->b = b;
  return k;
}

static const Core *core_bind(Checker *c, CoreKind kind, const char *name, const Core *a, const Core *b) {
  return core_binder(c, kind, MULT_OMEGA, name, a, b);
}

static const Core *core_app(Checker *c, const Core *f, const Core *x) {
  Core *k;
  if (f == NULL || x == NULL)
    return NULL;
  k = new_core(c, CORE_APP);
  if (k == NULL)
    return NULL;
  k->a = f;
  k->b = x;
  return k;
}

static const Core *core_op(Checker *c, Op op, uint32_t inst, uint32_t field, const Core *const *args, uint32_t n) {
  const Core **copy = arena_alloc(c->m->arena, ((size_t)n + 1u) * sizeof *copy);
  Core *k;
  uint32_t i;
  if (copy == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  for (i = 0; i < n; i++) {
    if (args[i] == NULL)
      return NULL;
    copy[i] = args[i];
  }
  k = new_core(c, CORE_OP);
  if (k == NULL)
    return NULL;
  k->op = op;
  k->inst = inst;
  k->field = field;
  k->args = copy;
  k->argc = n;
  return k;
}

static const Core *op3(Checker *c, Op op, uint32_t inst, uint32_t n, const Core *a, const Core *b, const Core *x) {
  const Core *args[3];
  args[0] = a;
  args[1] = b;
  args[2] = x;
  return core_op(c, op, inst, 0, args, n);
}

static const Value *tyop(Checker *c, Op op, uint32_t n, const Value *a, const Value *b, const Value *x) {
  return val_make(c->m, op, 0, n, a, b, x);
}

static const Value *nat_type(Checker *c) {
  return tyop(c, OP_NAT, 0, NULL, NULL, NULL);
}

static const Value *bool_type(Checker *c) {
  return tyop(c, OP_BOOL, 0, NULL, NULL, NULL);
}

/* The value of the core term K in the current context. */
static const Value *here(Checker *c, const Core *k) {
  return k == NULL ? NULL : eval_core(c->m, c->env, k);
}

/* A value with no arguments: SSet, I, i0 or i1. */
static const Value *op0(Checker *c, Op op) {
  return tyop(c, op, 0, NULL, NULL, NULL);
}

/* Conversion in the active face context, also under fresh binders. */
static int converts(Checker *c, uint32_t level, const Value *a, const Value *b) {
  return conv_values(c->m, level, a, b)
      || (c->face != NULL && conv_under_face(c->m, level, c->face, a, b));
}

/* Path B x y: PathP over the constant line `λ _ → B` (there is no OP_PATH). */
static const Value *path_type(Checker *c, const Value *b, const Value *x, const Value *y) {
  const Value *line = b == NULL ? NULL : here(c, core_bind(c, CORE_LAM, "_", NULL, quote_value(c->m, c->level + 1u, b)));
  return line == NULL || x == NULL || y == NULL ? NULL : tyop(c, OP_PATHP, 3, line, x, y);
}

/* 1 when the line of a PathP type does not use its interval variable. */
static int line_const(Checker *c, const Value *line) {
  return converts(c, c->level + 1u, apply_value(c->m, line, val_var(c->m, c->level)), apply_value(c->m, line, op0(c, OP_I0)));
}

/* V in the surface syntax, for a diagnostic. */
static const char *show(Checker *c, const Value *v) {
  const char *names[NAMES_MAX];
  const Scope *s;
  char *buf = arena_alloc(c->m->arena, SHOW_MAX);
  uint32_t count = c->level < NAMES_MAX ? c->level : NAMES_MAX;
  for (s = c->scope; s != NULL; s = s->next) {
    if (s->level < NAMES_MAX)
      names[s->level] = s->name != NULL ? s->name : "_";
  }
  if (buf == NULL || v == NULL)
    return "?";
  value_print(c->m, names, count, v, buf, SHOW_MAX);
  return buf;
}

static int push_at(Checker *c, Mult mult, const char *name, const Value *type) {
  Scope *s = arena_alloc(c->m->arena, sizeof *s);
  const Env *e = env_push(c->m, c->env, val_var(c->m, c->level));
  if (type == NULL || s == NULL || e == NULL)
    return c->m->diag->set ? 0 : FAIL(c, "OOM", "out of memory");
  s->name = name;
  s->type = type;
  s->level = c->level;
  s->mult = mult;
  s->uses = 0;
  s->next = c->scope;
  c->scope = s;
  c->env = e;
  c->level++;
  return 1;
}

/* A binder at omega. */
static int push(Checker *c, const char *name, const Value *type) {
  return push_at(c, MULT_OMEGA, name, type);
}

static void pop(Checker *c) {
  c->scope = c->scope->next;
  c->env = c->env->next;
  c->level--;
}

/* 1 when a term of type FOUND can stand where WANT is expected.
   Universes are not cumulative (former F14). A pretype (sort SSet) is in
   no universe. */
static int expect(Checker *c, const Value *found, const Value *want) {
  if (found == NULL || want == NULL)
    return 0;
  if (converts(c, c->level, found, want))
    return 1;
  if (val_is(found, OP_SSET))
    return FAIL(c, "TYPE_PRETYPE", "expected a term of type %s, found a pretype (its sort is SSet)", show(c, want));
  return FAIL(c, "TYPE_MISMATCH", "expected a term of type %s, found a term of type %s", show(c, want), show(c, found));
}

static Scope *find_local(const Checker *c, const char *name) {
  Scope *s;
  for (s = c->scope; s != NULL; s = s->next) {
    if (s->name != NULL && strcmp(s->name, name) == 0)
      return s;
  }
  return NULL;
}

/* The uses that the multiplicity M allows; omega has no limit. */
static uint32_t mult_bound(Mult m) {
  switch (m) {
  case MULT_ZERO:
    return 0u;
  case MULT_ONE:
    return 1u;
  case MULT_OMEGA:
    return UINT32_MAX;
  }
  return UINT32_MAX;
}

/* The level of an argument at a binder of multiplicity M, in an
   application at the level AT (QTT scaling): 0 gives 0, 1 gives AT, and
   omega gives omega (0 when AT is 0). */
static Mult mult_scale(Mult at, Mult m) {
  switch (m) {
  case MULT_ZERO:
    return MULT_ZERO;
  case MULT_ONE:
    return at;
  case MULT_OMEGA:
    return at == MULT_ZERO ? MULT_ZERO : MULT_OMEGA;
  }
  return at;
}

/* A use of S at the level c->at (PLAN 4.1). A 1-variable used at omega is
   REFUSE_AFFINE_OMEGA. Another use above the multiplicity of S is
   REFUSE_AFFINE_REUSE: a 1-variable used two times, or a 0-variable at run
   time (Q-S3-4 (a)). */
static int use_local(Checker *c, Scope *s) {
  uint32_t add = mult_bound(c->at);
  uint32_t bound = mult_bound(s->mult);
  s->uses = add > UINT32_MAX - s->uses ? UINT32_MAX : s->uses + add;
  if (s->uses <= bound)
    return 1;
  if (c->at == MULT_OMEGA && s->mult == MULT_ONE)
    return FAIL(c, "REFUSE_AFFINE_OMEGA", "the variable %s is used at omega, allowed 1: an argument at an omega binder, or a closure that captures it and goes to one, can run more than one time (PLAN 4.1)", s->name);
  if (c->at == MULT_OMEGA)
    return FAIL(c, "REFUSE_AFFINE_REUSE", "the variable %s is used more times than its multiplicity allows on one evaluation path: used at omega, allowed %u (PLAN 4.1)", s->name, bound);
  return FAIL(c, "REFUSE_AFFINE_REUSE", "the variable %s is used more times than its multiplicity allows on one evaluation path: used %u, allowed %u (PLAN 4.1)", s->name, s->uses, bound);
}

/* The use counts of the scope, the innermost binder first. A probe that
   fails, or whose term is checked again, restores them. */
static uint32_t *uses_save(Checker *c) {
  uint32_t *saved = arena_alloc(c->m->arena, ((size_t)c->level + 1u) * sizeof *saved);
  const Scope *s;
  uint32_t i = 0;
  for (s = c->scope; s != NULL && saved != NULL; s = s->next)
    saved[i++] = s->uses;
  return saved;
}

static void uses_restore(Checker *c, const uint32_t *saved) {
  Scope *s;
  uint32_t i = 0;
  for (s = c->scope; s != NULL && saved != NULL; s = s->next)
    s->uses = saved[i++];
}

/* The arms of an eliminator start from the same counts, and after them each
   count is the maximum over the arms (PLAN 4.1). MOST holds the counts of
   the arms before the current one. */
static void uses_max(Checker *c, const uint32_t *most) {
  Scope *s;
  uint32_t i = 0;
  for (s = c->scope; s != NULL && most != NULL; s = s->next, i++)
    s->uses = most[i] > s->uses ? most[i] : s->uses;
}

/* A closure at omega can run more than one time (PLAN 4.1). Its body counts
   at 1 for one call; then each variable that it captures (its count is not
   the count in SAVED) is used at omega. c->at is MULT_OMEGA. */
static int uses_omega(Checker *c, const uint32_t *saved) {
  Scope *s;
  uint32_t i = 0;
  int ok = 1;
  for (s = c->scope; s != NULL && saved != NULL && ok; s = s->next, i++) {
    if (s->uses != saved[i]) {
      s->uses = saved[i];
      ok = use_local(c, s);
    }
  }
  return ok;
}

#define FIX_WHY "is used by a fold step or a recursive case, which runs one time for each layer: used at omega"
#define PATH_WHY "is free under an interval binder at 1 (λ i →, refl, symm, trans, cong, transport, transp, hcomp, Glue, glue or unglue): both end points of the path hold it, used 2"

/* A term that runs one time for each layer (a fold step, the case of a
   constructor with a recursive field: REFUSE_AFFINE_FIX), or a path whose
   end points both hold its body (REFUSE_AFFINE_PATH), refuses each
   1-variable that it uses: its count is not the count in SAVED (PLAN
   4.2). */
static int uses_refuse(Checker *c, const uint32_t *saved, const char *code, const char *why) {
  const Scope *s;
  uint32_t i = 0;
  for (s = c->scope; s != NULL && saved != NULL; s = s->next, i++) {
    if (s->uses != saved[i] && s->mult == MULT_ONE)
      return FAIL(c, code, "the variable %s %s, allowed 1 (PLAN 4.2)", s->name, why);
  }
  return 1;
}

static uint32_t find_def(const Machine *m, const char *name) {
  uint32_t i;
  for (i = 0; i < m->def_count; i++) {
    if (strcmp(m->defs[i].name, name) == 0)
      return i;
  }
  return NO_DEF;
}

/* An environment that binds VALS in order: the last one is index 0. */
static const Env *env_of(Checker *c, const Value *const *vals, uint32_t n) {
  const Env *e = NULL;
  uint32_t i;
  for (i = 0; i < n; i++)
    e = env_push(c->m, e, vals[i]);
  return e;
}

static const Core *elab(Checker *c, const Term *t, const Value *hint, const Value **type);
static const Core *check_lam(Checker *c, const Term *t, const Value *hint, const Value **type);

static const Core *check(Checker *c, const Term *t, const Value *want) {
  const Value *type = NULL;
  Mult at = c->at;
  const Core *k;
  if (want != NULL && want->kind == VAL_UNIV)
    c->at = MULT_ZERO;
  k = want == NULL ? NULL : elab(c, t, want, &type);
  c->at = at;
  return k;
}

static const Core *infer(Checker *c, const Term *t, const Value **type) {
  *type = NULL;
  return elab(c, t, NULL, type);
}

/* infer, but a failure leaves no diagnostic. */
static const Core *try_infer(Checker *c, const Term *t, const Value **type) {
  Diag saved = *c->m->diag;
  uint32_t *uses = uses_save(c);
  const Core *k = infer(c, t, type);
  if (k == NULL) {
    *c->m->diag = saved;
    uses_restore(c, uses);
  }
  return k;
}

/* check at the level AT: 0 in a type position, else 1 (PLAN 4.1). */
static const Core *check_at(Checker *c, Mult at, const Term *t, const Value *want) {
  Mult saved = c->at;
  const Core *k;
  c->at = at;
  k = check(c, t, want);
  c->at = saved;
  return k;
}

/* T as a type; *LEVEL is its universe. With PRETYPE, a pretype (sort SSet:
   I, or a function type over I) is also a type and *LEVEL is UINT64_MAX. */
static const Core *check_type_at(Checker *c, const Term *t, uint64_t *level, int pretype) {
  const Value *type = NULL;
  Mult at = c->at;
  const Core *k;
  c->at = MULT_ZERO; /* a type uses nothing (PLAN 4.1) */
  k = infer(c, t, &type);
  c->at = at;
  if (k == NULL)
    return NULL;
  if (pretype && val_is(type, OP_SSET)) {
    *level = UINT64_MAX;
    return k;
  }
  if (val_is(type, OP_SSET))
    return nul(FAIL(c, "TYPE_PRETYPE", "expected a type in a universe, found the pretype %s", show(c, here(c, k))));
  if (type == NULL || type->kind != VAL_UNIV)
    return nul(FAIL(c, "TYPE_MISMATCH", "expected a type, found a term of type %s", show(c, type)));
  *level = type->nat;
  return k;
}

static const Core *check_type(Checker *c, const Term *t, uint64_t *level) {
  return check_type_at(c, t, level, 0);
}

/* Compares the inferred *TYPE with HINT. */
static const Core *finish(Checker *c, const Core *k, const Value *hint, const Value **type) {
  if (k == NULL || *type == NULL)
    return NULL;
  if (hint != NULL && !expect(c, *type, hint))
    return NULL;
  *type = hint != NULL ? hint : *type;
  return k;
}

/* 1 when T is Option A. *ELEM is A. */
static int carrier_of(const Value *t, Carrier *carrier, const Value **elem) {
  if (!val_is(t, OP_OPTION))
    return 0;
  *carrier = CARRIER_OPTION;
  *elem = t->args[0];
  return 1;
}

/* SHAPE (Option or List) with the element ELEM. */
static const Value *carried(Checker *c, const Value *shape, const Value *elem) {
  if (shape == NULL)
    return NULL;
  return tyop(c, shape->op, 1, elem, NULL, NULL);
}

/* Checks T as a function from DOM. *RESULT is WANT, or the result type that
   T infers when WANT is NULL. */
static const Value *case_arrow(Checker *c, const Value *dom, const Value *cod) {
  return val_arrow_mult(c->m, copyable_type(c, dom) ? MULT_OMEGA : MULT_ONE, dom, cod);
}

static const Core *fn_into(Checker *c, const Term *t, const Value *dom, const Value *want, const Value **result) {
  const Value *ft = NULL;
  const Core *f;
  if (want != NULL) {
    *result = want;
    return check(c, t, case_arrow(c, dom, want));
  }
  f = infer(c, t, &ft);
  if (f == NULL)
    return NULL;
  if (ft == NULL || ft->kind != VAL_PI)
    return nul(FAIL(c, "TYPE_NOT_FUNCTION", "expected a function, found a term of type %s", show(c, ft)));
  *result = closure_apply(c->m, ft, val_var(c->m, c->level));
  /* Compare at two fresh arguments before the result leaves the binder.
     Reusing its level in expect would capture a dependent result. */
  if (!converts(c, c->level + 2u, *result,
                   closure_apply(c->m, ft, val_var(c->m, c->level + 1u))))
    return nul(FAIL(c, "TYPE_MISMATCH", "this operation needs a function with a non-dependent result type"));
  return expect(c, ft, case_arrow(c, dom, *result)) ? f : NULL;
}

/* The argument type of the function T, from its binder or its type. */
static const Value *lam_domain(Checker *c, const Term *t) {
  uint64_t level = 0;
  const Value *ft = NULL;
  uint32_t *uses;
  int ok;
  if (t->kind == TERM_LAM && t->left != NULL)
    return here(c, check_type(c, t->left, &level));
  uses = uses_save(c);
  ok = try_infer(c, t, &ft) != NULL;
  uses_restore(c, uses); /* the caller checks T again */
  return ok && ft != NULL && ft->kind == VAL_PI ? ft->dom : NULL;
}

/* The carried argument ARG of map, bind or filter. When ARG does not infer
   (`none`), its type comes from the expected type and from the binder of
   the function FN. */
static const Core *carried_arg(Call *k, const Term *arg, const Term *fn, const Value **type) {
  Checker *c = k->c;
  Carrier carrier = CARRIER_OPTION;
  const Value *elem = NULL;
  const Value *dom;
  const Core *x = try_infer(c, arg, type);
  if (x != NULL)
    return x;
  if (!carrier_of(k->hint, &carrier, &elem))
    return infer(c, arg, type);
  dom = lam_domain(c, fn);
  if (dom == NULL)
    return infer(c, arg, type);
  *type = carried(c, k->hint, dom);
  return check(c, arg, *type);
}

static const Core *need_type(Call *k, const char *what) {
  return nul(FAIL(k->c, "TYPE_INFER", "%s needs a declared type %s here", k->b->name, what));
}

static const Core *not_carrier(Call *k, const Value *type) {
  return nul(FAIL(k->c, "TYPE_MISMATCH", "%s needs a value of type Option A, found a term of type %s", k->b->name, show(k->c, type)));
}

/* 1 when T is Path A a b (a PathP over a constant line). */
static int path_like(Checker *c, const Value *t) {
  return val_is(t, OP_PATHP) && line_const(c, t->args[0]);
}

/* The type A of Path A a b. */
static const Value *path_base(Checker *c, const Value *t) {
  return apply_value(c->m, t->args[0], op0(c, OP_I0));
}

static const Core *infer_path(Call *k, uint32_t i, const Value **type) {
  const Core *e = infer(k->c, k->args[i], type);
  if (e == NULL)
    return NULL;
  return path_like(k->c, *type) ? e : nul(FAIL(k->c, "TYPE_MISMATCH", "%s needs a proof of type Path A a b, found a term of type %s", k->b->name, show(k->c, *type)));
}

static const Core *rule_type1(Call *k) {
  uint64_t level = 0;
  const Core *a = check_type(k->c, k->args[0], &level);
  k->type = val_univ(k->c->m, level);
  return op3(k->c, k->b->op, 0, 1, a, NULL, NULL);
}

/* Path A: the constant line `λ _ → A` (there is no OP_PATH). */
static const Core *const_line(Checker *c, const Term *t, uint64_t *level) {
  const Core *a = check_type(c, t, level);
  return a == NULL ? NULL : core_bind(c, CORE_LAM, "_", NULL, quote_value(c->m, c->level + 1u, here(c, a)));
}

/* PathP A: A is a line of type I → Type n. */
static const Core *pathp_line(Checker *c, const Term *t, uint64_t *level) {
  const Value *lt = NULL;
  Mult at = c->at;
  const Core *a;
  const Value *cod = NULL;
  c->at = MULT_ZERO;
  a = infer(c, t, &lt);
  c->at = at;
  if (a == NULL)
    return NULL;
  if (lt != NULL && lt->kind == VAL_PI && converts(c, c->level, lt->dom, op0(c, OP_I)))
    cod = closure_apply(c->m, lt, val_var(c->m, c->level));
  if (cod == NULL || cod->kind != VAL_UNIV)
    return nul(FAIL(c, "TYPE_MISMATCH", "PathP needs a line of type I → Type n, found a term of type %s", show(c, lt)));
  *level = cod->nat;
  return a;
}

/* PathP A a b (part 0) and Path A a b (part 1). */
static const Core *rule_pathp(Call *k) {
  Checker *c = k->c;
  uint64_t level = 0;
  const Core *line = k->b->part == 1u ? const_line(c, k->args[0], &level) : pathp_line(c, k->args[0], &level);
  const Value *lv = here(c, line);
  const Core *a = lv == NULL ? NULL : check(c, k->args[1], apply_value(c->m, lv, op0(c, OP_I0)));
  const Core *b = a == NULL ? NULL : check(c, k->args[2], apply_value(c->m, lv, op0(c, OP_I1)));
  k->type = val_univ(c->m, level);
  return op3(c, OP_PATHP, 0, 3, line, a, b);
}

static const Core *rule_empty(Call *k) {
  if (!val_is(k->hint, k->b->result))
    return need_type(k, "Option A");
  k->type = k->hint;
  return op3(k->c, k->b->op, 0, 0, NULL, NULL, NULL);
}

static const Core *rule_some(Call *k) {
  Checker *c = k->c;
  const Value *a = val_is(k->hint, OP_OPTION) ? k->hint->args[0] : NULL;
  const Core *x = a != NULL ? check(c, k->args[0], a) : infer(c, k->args[0], &a);
  k->type = tyop(c, OP_OPTION, 1, a, NULL, NULL);
  return op3(c, OP_SOME, 0, 1, x, NULL, NULL);
}

static const Core *rule_pack(Call *k) {
  Checker *c = k->c;
  const Value *s = k->hint;
  const Core *w;
  const Core *p;
  if (s == NULL || s->kind != VAL_SIGMA)
    return need_type(k, "Sigma (x : A) B");
  w = check(c, k->args[0], s->dom);
  p = w == NULL ? NULL : check(c, k->args[1], closure_apply(c->m, s, here(c, w)));
  k->type = s;
  return op3(c, OP_PACK, 0, 2, w, p, NULL);
}

/* The path lambda λ NAME → BODY (BODY is under one more interval binder). */
static const Core *path_lam(Checker *c, const char *name, const Core *body) {
  return op3(c, OP_PLAM, 0, 1, core_bind(c, CORE_LAM, name, NULL, body), NULL, NULL);
}

/* The interval variable with the de Bruijn index I. */
static const Core *ivar(Checker *c, uint32_t i) {
  return core_index(c, CORE_VAR, i);
}

/* P R under DEPTH new interval binders, where P has the type T = PathP A a0 a1.
   OP_PAPP takes the ends of the type of P (zk 1d263fb check.c 1640..1805). */
static const Core *path_at(Checker *c, uint32_t depth, const Core *p, const Value *t, const Core *r) {
  const Core *args[4];
  args[0] = quote_value(c->m, c->level + depth, here(c, p));
  args[1] = r;
  args[2] = quote_value(c->m, c->level + depth, t->args[1]);
  args[3] = quote_value(c->m, c->level + depth, t->args[2]);
  return core_op(c, OP_PAPP, 0, 0, args, 4u);
}

/* The cell [ FACE ↦ BRANCH , REST ] of a system. */
static const Core *sys_cell(Checker *c, const Core *face, const Core *branch, const Core *rest) {
  return op3(c, OP_SYSTEM, 0, 3, face, branch, rest);
}

/* The body of trans p q under i, where p : Path A a b and q : Path A b c:
   hcomp A (~ i ∨ i) (λ j → [ ~ i ↦ a , i ↦ q j ]) (p i). */
static const Core *trans_body(Checker *c, const Core *p, const Value *tp, const Core *q, const Value *tq) {
  const Core *args[4];
  const Core *nil = op3(c, OP_SYSNIL, 0, 0, NULL, NULL, NULL);
  const Core *rest = sys_cell(c, ivar(c, 1), path_at(c, 2u, q, tq, ivar(c, 0)), nil);
  const Core *side = sys_cell(c, op3(c, OP_INEG, 0, 1, ivar(c, 1), NULL, NULL), quote_value(c->m, c->level + 2u, tp->args[1]), rest);
  args[0] = quote_value(c->m, c->level + 1u, path_base(c, tp));
  args[1] = op3(c, OP_IMAX, 0, 2, op3(c, OP_INEG, 0, 1, ivar(c, 0), NULL, NULL), ivar(c, 0), NULL);
  args[2] = core_bind(c, CORE_LAM, "j", NULL, side);
  args[3] = path_at(c, 1u, p, tp, ivar(c, 0));
  return core_op(c, OP_HCOMP, 0, 0, args, 4u);
}

/* 1 when the variable with the de Bruijn index I is free in K. */
static int core_free(const Core *k, uint32_t i) {
  uint32_t j;
  if (k == NULL)
    return 0;
  switch (k->kind) {
  case CORE_VAR:
    return k->index == i;
  case CORE_GLOBAL:
  case CORE_NAT:
  case CORE_TRAP:
  case CORE_UNIV:
    return 0;
  case CORE_LAM:
  case CORE_PI:
  case CORE_SIGMA:
    return core_free(k->a, i) || core_free(k->b, i + 1u);
  case CORE_APP:
    return core_free(k->a, i) || core_free(k->b, i);
  case CORE_OP:
    for (j = 0; j < k->argc; j++) {
      if (core_free(k->args[j], i))
        return 1;
    }
    return core_free(k->a, i) || core_free(k->b, i);
  }
  return 0;
}

/* refl is λ i → a with a from the type, so it counts no use. At 1 or at
   omega, each 1-variable free in A is REFUSE_AFFINE_PATH (PLAN 4.2). */
static int refl_free(Checker *c, const Value *a) {
  const Core *q = c->at == MULT_ZERO ? NULL : quote_value(c->m, c->level, a);
  const Scope *s;
  uint32_t i = 0;
  for (s = c->scope; s != NULL && q != NULL; s = s->next, i++) {
    if (s->mult == MULT_ZERO && core_free(q, i))
      return FAIL(c, "REFUSE_AFFINE_REUSE", "the erased variable %s occurs in an executable path endpoint: used 1, allowed 0 (PLAN 4.1)", s->name);
    if (s->mult == MULT_ONE && core_free(q, i))
      return FAIL(c, "REFUSE_AFFINE_PATH", "the variable %s %s, allowed 1 (PLAN 4.2)", s->name, PATH_WHY);
  }
  return 1;
}

static const Core *rule_refl(Call *k) {
  Checker *c = k->c;
  const Value *e = k->hint;
  if (!path_like(c, e))
    return need_type(k, "Path A a b");
  if (!converts(c, c->level, e->args[1], e->args[2]))
    return nul(FAIL(c, "TYPE_REFL", "refl cannot prove the equality: the left side normalizes to %s, the right side normalizes to %s", show(c, e->args[1]), show(c, e->args[2])));
  if (!refl_free(c, e->args[1]))
    return NULL;
  k->type = e;
  return path_lam(c, "i", quote_value(c->m, c->level + 1u, e->args[1]));
}

static const Core *rule_sigma_part(Call *k) {
  Checker *c = k->c;
  const Value *st = NULL;
  const Core *s = infer(c, k->args[0], &st);
  const Core *w;
  if (s == NULL)
    return NULL;
  if (st == NULL || st->kind != VAL_SIGMA)
    return nul(FAIL(c, "TYPE_MISMATCH", "%s needs a Sigma value, found a term of type %s", k->b->name, show(c, st)));
  w = op3(c, OP_WITNESS, 0, 1, s, NULL, NULL);
  k->type = k->b->part == 0u ? st->dom : closure_apply(c->m, st, here(c, w));
  return k->b->part == 0u ? w : op3(c, OP_PAYLOAD, 0, 1, s, NULL, NULL);
}

/* unpack p f (Q-S3-6 (a)): p : Sigma (x : A) B is used one time, and f
   takes the two parts at 1: (@1 a : A) → (@1 b : B a) → C, where C is the
   declared type. The Core applies f to witness p and payload p, so the
   lowering does not change. */
static const Core *rule_unpack(Call *k) {
  Checker *c = k->c;
  const Value *st = NULL;
  const Core *s = infer(c, k->args[0], &st);
  const Core *ft;
  const Core *f;
  if (s == NULL)
    return NULL;
  if (st == NULL || st->kind != VAL_SIGMA)
    return nul(FAIL(c, "TYPE_MISMATCH", "unpack needs a Sigma value, found a term of type %s", show(c, st)));
  if (k->hint == NULL)
    return need_type(k, "C for the result of its function");
  ft = core_binder(c, CORE_PI, MULT_ONE, "a", quote_value(c->m, c->level, st->dom),
                   core_binder(c, CORE_PI, MULT_ONE, "b", quote_value(c->m, c->level + 1u, closure_apply(c->m, st, val_var(c->m, c->level))),
                               quote_value(c->m, c->level + 2u, k->hint)));
  f = check(c, k->args[1], here(c, ft));
  k->type = k->hint;
  return f == NULL ? NULL : core_app(c, core_app(c, f, op3(c, OP_WITNESS, 0, 1, s, NULL, NULL)), op3(c, OP_PAYLOAD, 0, 1, s, NULL, NULL));
}

/* option z f o */
static const Core *rule_option(Call *k) {
  Checker *c = k->c;
  const Value *ot = NULL;
  const Value *res = k->hint;
  const Core *o = infer(c, k->args[2], &ot);
  uint32_t *start = uses_save(c); /* the scrutinee one time; each arm from here */
  uint32_t *most;
  const Core *z;
  const Core *f;
  if (o == NULL)
    return NULL;
  if (!val_is(ot, OP_OPTION))
    return nul(FAIL(c, "TYPE_MISMATCH", "option needs a value of type Option A, found a term of type %s", show(c, ot)));
  z = res != NULL ? check(c, k->args[0], res) : infer(c, k->args[0], &res);
  most = uses_save(c);
  uses_restore(c, start);
  f = z == NULL ? NULL : check(c, k->args[1], case_arrow(c, ot->args[0], res));
  uses_max(c, most);
  k->type = res;
  return op3(c, OP_OPTION_ELIM, 0, 3, z, f, o);
}

static const Core *rule_pure(Call *k) {
  Carrier carrier = CARRIER_OPTION;
  const Value *a = NULL;
  if (!carrier_of(k->hint, &carrier, &a))
    return need_type(k, "Option A");
  k->type = k->hint;
  return op3(k->c, OP_PURE, carrier, 1, check(k->c, k->args[0], a), NULL, NULL);
}

/* map f m */
static const Core *rule_map(Call *k) {
  Checker *c = k->c;
  Carrier carrier = CARRIER_OPTION;
  Carrier hint_carrier = CARRIER_OPTION;
  const Value *mt = NULL;
  const Value *a = NULL;
  const Value *b = NULL;
  const Value *hint_elem = NULL;
  const Core *x = carried_arg(k, k->args[1], k->args[0], &mt);
  const Core *f;
  if (x == NULL)
    return NULL;
  if (!carrier_of(mt, &carrier, &a))
    return not_carrier(k, mt);
  f = fn_into(c, k->args[0], a, carrier_of(k->hint, &hint_carrier, &hint_elem) ? hint_elem : NULL, &b);
  k->type = carried(c, mt, b);
  return op3(c, OP_MAP, carrier, 2, f, x, NULL);
}

/* bind m f */
static const Core *rule_bind(Call *k) {
  Checker *c = k->c;
  Carrier carrier = CARRIER_OPTION;
  Carrier result_carrier = CARRIER_OPTION;
  const Value *mt = NULL;
  const Value *a = NULL;
  const Value *b = NULL;
  const Value *res = NULL;
  const Core *x = carried_arg(k, k->args[0], k->args[1], &mt);
  const Core *f;
  if (x == NULL)
    return NULL;
  if (!carrier_of(mt, &carrier, &a))
    return not_carrier(k, mt);
  f = fn_into(c, k->args[1], a, k->hint, &res);
  if (f == NULL)
    return NULL;
  if (!carrier_of(res, &result_carrier, &b) || result_carrier != carrier)
    return nul(FAIL(c, "TYPE_MISMATCH", "bind needs a function into the carrier of %s, found one into %s", show(c, mt), show(c, res)));
  k->type = res;
  return op3(c, OP_BIND, carrier, 2, x, f, NULL);
}

/* fold step z n (Nat). */
static const Core *fold_simple(Call *k, const Core *s) {
  Checker *c = k->c;
  const Value *res = k->hint;
  uint32_t *start = uses_save(c); /* each arm from here; the step runs one time for each layer (R2) */
  const Core *z = res != NULL ? check(c, k->args[1], res) : infer(c, k->args[1], &res);
  uint32_t *most = uses_save(c);
  const Core *f;
  uses_restore(c, start);
  f = z == NULL ? NULL : check(c, k->args[0], case_arrow(c, res, res));
  if (f != NULL && !uses_refuse(c, start, "REFUSE_AFFINE_FIX", FIX_WHY))
    return NULL;
  uses_max(c, most);
  k->used = 3u;
  k->type = res;
  return op3(c, OP_FOLD_NAT, 0, 3, f, z, s);
}

/* The type of the fold case of CTOR: one argument for each field (a
   recursive field arrives folded, so its type is RES), then RES. */
static const Value *case_type(Checker *c, const CtorInfo *ctor, const Env *env, const Value *res) {
  const Value *t = res;
  uint32_t i = ctor->field_count;
  while (i > 0u && t != NULL) {
    const FieldInfo *field = &ctor->fields[--i];
    t = case_arrow(c, field->recursive ? res : eval_core(c->m, env, field->type), t);
  }
  return t;
}

/* 1 when CTOR has a recursive field: its fold case runs one time for each
   layer (PLAN 4.2). */
static int ctor_recursive(const CtorInfo *ctor) {
  uint32_t i;
  for (i = 0; i < ctor->field_count; i++) {
    if (ctor->fields[i].recursive)
      return 1;
  }
  return 0;
}

/* fold case... x over a domain family: one case for each constructor. */
static const Core *fold_family(Call *k, const Core *s, const Value *st) {
  Checker *c = k->c;
  Machine *m = c->m;
  const FamilyInfo *f = &m->families[st->inst];
  const Env *env = env_of(c, st->args, st->argc);
  const Value *res = k->hint;
  const Core **cores = arena_alloc(m->arena, ((size_t)f->ctor_count + 2u) * sizeof *cores);
  uint32_t *start = uses_save(c); /* each case from here; a recursive case runs one time for each layer (R2) */
  uint32_t *most = start;
  uint32_t j;
  if (cores == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  if (res == NULL && m->ctors[f->first_ctor].field_count == 0u)
    cores[0] = infer(c, k->args[0], &res);
  if (res == NULL)
    return nul(FAIL(c, "TYPE_INFER", "fold over %s needs a declared result type here", f->name));
  for (j = 0; j < f->ctor_count; j++) {
    if (cores[j] == NULL)
      uses_restore(c, start);
    cores[j] = cores[j] != NULL ? cores[j] : check(c, k->args[j], case_type(c, &m->ctors[f->first_ctor + j], env, res));
    if (cores[j] == NULL)
      return NULL;
    if (ctor_recursive(&m->ctors[f->first_ctor + j]) && !uses_refuse(c, start, "REFUSE_AFFINE_FIX", FIX_WHY))
      return NULL;
    uses_max(c, most);
    most = uses_save(c);
  }
  cores[f->ctor_count] = s;
  k->used = f->ctor_count + 1u;
  k->type = res;
  return core_op(c, OP_FOLD_FAMILY, st->inst, 0, cores, f->ctor_count + 1u);
}

static int simple_scrutinee(const Value *t) {
  return val_is(t, OP_NAT);
}

/* fold takes its scrutinee last, so the scrutinee type sets the arity. */
static const Core *rule_fold(Call *k) {
  Checker *c = k->c;
  const Value *st = NULL;
  uint32_t *uses = uses_save(c);
  const Core *s = try_infer(c, k->args[k->argc - 1u], &st);
  uint32_t fam = val_is(st, OP_FAMILY) ? st->inst : NO_FAMILY;
  if (s != NULL && fam != NO_FAMILY && c->m->families[fam].ctor_count + 1u == k->argc) {
    k->hint = k->outer;
    return fold_family(k, s, st);
  }
  if (s != NULL && k->argc == 3u && simple_scrutinee(st)) {
    k->hint = k->outer;
    return fold_simple(k, s);
  }
  uses_restore(c, uses); /* the last argument is checked again */
  s = k->argc > 3u ? try_infer(c, k->args[2], &st) : NULL;
  if (s != NULL && simple_scrutinee(st)) {
    k->hint = NULL;
    return fold_simple(k, s);
  }
  s = infer(c, k->args[k->argc - 1u], &st);
  return s == NULL ? NULL : nul(FAIL(c, "TYPE_MISMATCH", "fold needs a last argument of type Nat or a domain data type, and one case for each constructor; found %u arguments and a last argument of type %s", k->argc, show(c, st)));
}

static const Core *rule_symm(Call *k) {
  const Value *et = NULL;
  const Core *e = infer_path(k, 0, &et);
  if (e == NULL)
    return NULL;
  k->type = path_type(k->c, path_base(k->c, et), et->args[2], et->args[1]);
  return path_lam(k->c, "i", path_at(k->c, 1u, e, et, op3(k->c, OP_INEG, 0, 1, ivar(k->c, 0), NULL, NULL)));
}

static const Core *rule_trans(Call *k) {
  Checker *c = k->c;
  const Value *t1 = NULL;
  const Value *t2 = NULL;
  const Core *e1 = infer_path(k, 0, &t1);
  const Core *e2 = e1 == NULL ? NULL : infer_path(k, 1, &t2);
  if (e2 == NULL)
    return NULL;
  if (!converts(c, c->level, path_base(c, t1), path_base(c, t2)) || !converts(c, c->level, t1->args[2], t2->args[1]))
    return nul(FAIL(c, "TYPE_MISMATCH", "trans needs the right side of the first proof, %s, to equal the left side of the second, %s", show(c, t1->args[2]), show(c, t2->args[1])));
  k->type = path_type(c, path_base(c, t1), t1->args[1], t2->args[2]);
  return path_lam(c, "i", trans_body(c, e1, t1, e2, t2));
}

/* cong f e */
static const Core *rule_cong(Call *k) {
  Checker *c = k->c;
  const Value *et = NULL;
  const Value *bt = NULL;
  const Value *fv;
  const Value *hint_type = path_like(c, k->hint) ? path_base(c, k->hint) : NULL;
  const Core *e = infer_path(k, 1, &et);
  const Core *f = e == NULL ? NULL : fn_into(c, k->args[0], path_base(c, et), hint_type, &bt);
  if (f == NULL)
    return NULL;
  fv = here(c, f);
  k->type = path_type(c, bt, apply_value(c->m, fv, et->args[1]), apply_value(c->m, fv, et->args[2]));
  return path_lam(c, "i", core_app(c, quote_value(c->m, c->level + 1u, fv), path_at(c, 1u, e, et, ivar(c, 0))));
}

/* transport P e u */
static const Core *rule_transport(Call *k) {
  Checker *c = k->c;
  const Value *et = NULL;
  const Value *pt = NULL;
  const Value *cod;
  const Value *pv;
  const Core *e = infer_path(k, 1, &et);
  const Core *p = e == NULL ? NULL : infer(c, k->args[0], &pt);
  const Core *u;
  if (p == NULL)
    return NULL;
  cod = pt != NULL && pt->kind == VAL_PI ? closure_apply(c->m, pt, val_var(c->m, c->level)) : NULL;
  if (cod == NULL || cod->kind != VAL_UNIV || !converts(c, c->level, pt->dom, path_base(c, et)))
    return nul(FAIL(c, "TYPE_MISMATCH", "transport needs a family of types over %s, found a term of type %s", show(c, path_base(c, et)), show(c, pt)));
  pv = here(c, p);
  u = check(c, k->args[2], apply_value(c->m, pv, et->args[1]));
  k->type = apply_value(c->m, pv, et->args[2]);
  return op3(c, OP_TRANSP, 0, 3, core_bind(c, CORE_LAM, "i", NULL, core_app(c, quote_value(c->m, c->level + 1u, pv), path_at(c, 1u, e, et, ivar(c, 0)))), op3(c, OP_I0, 0, 0, NULL, NULL, NULL), u);
}

static const Core *rule_nat2(Call *k) {
  Checker *c = k->c;
  const Core *x = check(c, k->args[0], nat_type(c));
  const Core *y = x == NULL ? NULL : check(c, k->args[1], nat_type(c));
  k->type = tyop(c, k->b->result, 0, NULL, NULL, NULL);
  return op3(c, k->b->op, 0, 2, x, y, NULL);
}

/* flagIf b yes no */
static const Core *rule_flag_if(Call *k) {
  Checker *c = k->c;
  const Value *res = k->hint;
  const Core *b = check(c, k->args[0], bool_type(c));
  uint32_t *start = uses_save(c); /* the scrutinee one time; each arm from here */
  const Core *yes = b == NULL ? NULL : (res != NULL ? check(c, k->args[1], res) : infer(c, k->args[1], &res));
  uint32_t *most = uses_save(c);
  const Core *no;
  uses_restore(c, start);
  no = yes == NULL ? NULL : check(c, k->args[2], res);
  uses_max(c, most);
  k->type = res;
  return op3(c, OP_FLAG_IF, 0, 3, b, yes, no);
}

/* ~ r, r ∧ s and r ∨ s: each argument is in I, and so is the result. */
static const Core *rule_interval(Call *k) {
  Checker *c = k->c;
  const Value *iv = op0(c, OP_I);
  const Core *a = check(c, k->args[0], iv);
  const Core *b = k->b->arity > 1u && a != NULL ? check(c, k->args[1], iv) : NULL;
  k->type = iv;
  return op3(c, k->b->op, 0, k->b->arity, a, b, NULL);
}

/* Partial φ A: φ is in I and A is a type. A pretype, as I. */
static const Core *rule_partial(Call *k) {
  Checker *c = k->c;
  uint64_t level = 0;
  const Core *phi = check(c, k->args[0], op0(c, OP_I));
  const Core *a = phi == NULL ? NULL : check_type(c, k->args[1], &level);
  k->type = op0(c, OP_SSET);
  return op3(c, OP_PARTIAL, 0, 2, phi, a, NULL);
}

/* Face equality in the face context. */
static int same_face(Checker *c, const Value *a, const Value *b) {
  const Value *x = a == NULL || c->face == NULL ? a : tyop(c, OP_IMIN, 2, c->face, a, NULL);
  const Value *y = b == NULL || c->face == NULL ? b : tyop(c, OP_IMIN, 2, c->face, b, NULL);
  return x != NULL && y != NULL && face_equal(c->m, c->level, x, y);
}

#define CELLS_MAX FACE_MEETS_MAX

/* 1 when T is a system cell `φ ↦ u` applied to the other cells. */
static int is_system_cell(const Term *t) {
  return t->kind == TERM_APP && t->left->kind == TERM_APP && t->left->left->kind == TERM_APP
         && t->left->left->left->kind == TERM_VAR && strcmp(t->left->left->left->name, "↦") == 0;
}

/* T against WANT with FACE added to the face context. A face with no meet
   (it never holds) adds nothing, so that branch gets the usual checks. */
static const Core *check_on(Checker *c, const Term *t, const Value *want, const Core *face) {
  FaceMeets fm;
  const Value *saved = c->face;
  const Value *fv = here(c, face);
  const Value *now = fv == NULL || saved == NULL ? fv : tyop(c, OP_IMIN, 2, saved, fv, NULL);
  if (now == NULL)
    return NULL;
  c->face = face_meets(c->m, c->level, now, &fm) && fm.count == 0 ? saved : now;
  const Value *restricted = c->face == NULL ? want : value_under_face(c->m, c->level, c->face, want);
  const Core *out = restricted == NULL ? NULL : check(c, t, restricted);
  c->face = saved;
  return out;
}

/* The branches UA and UB agree on each meet of FA ∧ FB (and the face
   context). An atom in a meet stays free, so the test is conservative
   there. */
static int cells_agree(Checker *c, const Core *fa, const Core *ua, const Core *fb, const Core *ub) {
  FaceMeets fm;
  const Value *both = here(c, op3(c, OP_IMIN, 0, 2, fa, fb, NULL));
  const Value *meet = both == NULL || c->face == NULL ? both : tyop(c, OP_IMIN, 2, both, c->face, NULL);
  const Value *x = meet == NULL ? NULL : here(c, ua);
  const Value *y = x == NULL ? NULL : here(c, ub);
  if (y == NULL)
    return 0;
  if (!face_meets(c->m, c->level, meet, &fm))
    return FAIL(c, "TYPE_SYSTEM", "the overlap %s of two faces is not in the face bounds", show(c, meet));
  if (!conv_under_face(c->m, c->level, meet, x, y))
    return FAIL(c, "TYPE_SYSTEM", "two branches disagree where the faces meet at %s: %s against %s", show(c, meet),
                show(c, value_under_face(c->m, c->level, meet, x)), show(c, value_under_face(c->m, c->level, meet, y)));
  return 1;
}

/* [ φ1 ↦ u1 , ... ] against Partial ψ A: each face is in I, each branch is
   in A under its face, the faces join to ψ, and two branches agree where
   their faces meet. Two systems convert by structure, in order (no
   order-free conv_system). */
static const Core *rule_system(Call *k) {
  Checker *c = k->c;
  const Value *e = k->hint;
  const Term *face[CELLS_MAX];
  const Term *branch[CELLS_MAX];
  const Core *fk[CELLS_MAX];
  const Core *uk[CELLS_MAX];
  const Term *rest = k->b->arity == 3u ? k->args[2] : NULL;
  uint32_t n = 0;
  if (!val_is(e, OP_PARTIAL))
    return need_type(k, "Partial φ A");
  if (rest != NULL) {
    face[0] = k->args[0];
    branch[0] = k->args[1];
    n = 1;
  }
  for (; rest != NULL && is_system_cell(rest) && n < CELLS_MAX; rest = rest->right) {
    face[n] = rest->left->left->right;
    branch[n] = rest->left->right;
    n++;
  }
  if (rest != NULL && (rest->kind != TERM_VAR || strcmp(rest->name, "[]") != 0))
    return nul(FAIL(c, "TYPE_SYSTEM", "a system has at most %u cells", (unsigned)CELLS_MAX));
  const Value *join = op0(c, OP_I0);
  for (uint32_t i = 0; i < n; i++) {
    fk[i] = check(c, face[i], op0(c, OP_I));
    uk[i] = fk[i] == NULL ? NULL : check_on(c, branch[i], e->args[1], fk[i]);
    const Value *fv = uk[i] == NULL ? NULL : here(c, fk[i]);
    if (fv == NULL)
      return NULL;
    join = i == 0 ? fv : tyop(c, OP_IMAX, 2, join, fv, NULL);
  }
  if (!same_face(c, join, e->args[0]))
    return nul(FAIL(c, "TYPE_SYSTEM", "the system covers %s, the type needs %s", show(c, join), show(c, e->args[0])));
  for (uint32_t i = 0; i < n; i++)
    for (uint32_t j = i + 1u; j < n; j++)
      if (!cells_agree(c, fk[i], uk[i], fk[j], uk[j]))
        return NULL;
  const Core *acc = op3(c, OP_SYSNIL, 0, 0, NULL, NULL, NULL);
  for (uint32_t i = n; i > 0; i--)
    acc = op3(c, OP_SYSTEM, 0, 3, fk[i - 1u], uk[i - 1u], acc);
  k->type = e;
  return acc;
}

/* The line A of transp A r u: an unannotated `λ i → T` binds i : I, so the
   printed form checks again. Any other term goes to pathp_line. */
static const Core *line_of(Checker *c, const Term *t, uint64_t *level) {
  const Core *body;
  if (t->kind != TERM_LAM || t->left != NULL)
    return pathp_line(c, t, level);
  if (!push(c, t->name, op0(c, OP_I)))
    return NULL;
  body = check_type_at(c, t->right, level, 0);
  pop(c);
  return body == NULL ? NULL : core_bind(c, CORE_LAM, t->name, NULL, body);
}

/* The line LV is constant on each meet of R and the face context, so
   transp A r u = u where r holds. A face with no meet adds no condition.
   An atom in a meet stays free, so the test is conservative there. A face
   that is not in the bounds needs a constant line. */
static int line_const_on(Checker *c, const Value *lv, const Value *r) {
  FaceMeets fm;
  const Value *phi = r == NULL || c->face == NULL ? r : tyop(c, OP_IMIN, 2, r, c->face, NULL);
  const Value *x = phi == NULL ? NULL : apply_value(c->m, lv, val_var(c->m, c->level));
  const Value *y = x == NULL ? NULL : apply_value(c->m, lv, op0(c, OP_I0));
  if (y == NULL)
    return 0;
  if (conv_values(c->m, c->level + 1u, x, y))
    return 1;
  if (face_meets(c->m, c->level, phi, &fm) && (fm.count == 0u || conv_under_face(c->m, c->level + 1u, phi, x, y)))
    return 1;
  return FAIL(c, "TYPE_LINE", "transp needs a line that is constant where r holds, found the line %s", show(c, lv));
}

/* transp A r u: A : I → Type n, r : I and u : A i0, of type A i1. A is
   constant where r holds. */
static const Core *rule_transp(Call *k) {
  Checker *c = k->c;
  uint64_t level = 0;
  const Core *line = line_of(c, k->args[0], &level);
  const Value *lv = here(c, line);
  const Core *r = lv == NULL ? NULL : check(c, k->args[1], op0(c, OP_I));
  const Core *u;
  if (r == NULL || !line_const_on(c, lv, here(c, r)))
    return NULL;
  u = check(c, k->args[2], apply_value(c->m, lv, op0(c, OP_I0)));
  k->type = apply_value(c->m, lv, op0(c, OP_I1));
  return op3(c, OP_TRANSP, 0, 3, line, r, u);
}

/* The side `λ j → [ φ1 ↦ u1 , ... ]` of hcomp against TY, with j : I. The
   faces are checked first in the outer context, so a face cannot name j:
   FK gets the faces and *PHI their join. Then each branch is checked under
   j and its face, and two branches agree where their faces meet. A side
   that is not a system literal is refused. */
static const Core *side_of(Checker *c, const Term *t, const Value *ty, const Core **phi, const Core **fk, uint32_t *n) {
  const Term *s = t->kind == TERM_LAM ? t->right : NULL;
  const Value *fv[CELLS_MAX];
  const Term *branch[CELLS_MAX];
  const Core *inner[CELLS_MAX];
  const Core *uk[CELLS_MAX];
  const Core *body;
  uint32_t i;
  uint32_t q;
  *n = 0;
  *phi = op3(c, OP_I0, 0, 0, NULL, NULL, NULL);
  if (s == NULL || (t->left != NULL && (t->left->kind != TERM_VAR || strcmp(t->left->name, "I") != 0))
      || (!is_system_cell(s) && (s->kind != TERM_VAR || strcmp(s->name, "[]") != 0)))
    return nul(FAIL(c, "TYPE_SYSTEM", "hcomp needs a side λ j → [ φ ↦ u , ... ]"));
  for (; is_system_cell(s) && *n < CELLS_MAX; s = s->right) {
    fk[*n] = check(c, s->left->left->right, op0(c, OP_I));
    fv[*n] = here(c, fk[*n]);
    if (fv[*n] == NULL)
      return NULL;
    branch[*n] = s->left->right;
    *phi = *n == 0 ? fk[0] : op3(c, OP_IMAX, 0, 2, *phi, fk[*n], NULL);
    (*n)++;
  }
  if (s->kind != TERM_VAR || strcmp(s->name, "[]") != 0)
    return nul(FAIL(c, "TYPE_SYSTEM", "a system has at most %u cells", (unsigned)CELLS_MAX));
  if (!push(c, t->name, op0(c, OP_I)))
    return NULL;
  for (i = 0; i < *n; i++) {
    inner[i] = quote_value(c->m, c->level, fv[i]);
    uk[i] = inner[i] == NULL ? NULL : check_on(c, branch[i], ty, inner[i]);
    if (uk[i] == NULL) {
      pop(c);
      return NULL;
    }
  }
  for (i = 0; i < *n; i++)
    for (q = i + 1u; q < *n; q++)
      if (!cells_agree(c, inner[i], uk[i], inner[q], uk[q])) {
        pop(c);
        return NULL;
      }
  body = op3(c, OP_SYSNIL, 0, 0, NULL, NULL, NULL);
  for (i = *n; i > 0; i--)
    body = op3(c, OP_SYSTEM, 0, 3, inner[i - 1u], uk[i - 1u], body);
  pop(c);
  return core_bind(c, CORE_LAM, t->name, NULL, body);
}

/* The base condition of hcomp: where its face holds, each branch of SIDE
   at j = i0 is U0. */
static int side_base(Checker *c, const Core *side, const Core **fk, uint32_t n, const Core *u0) {
  const Core *i0 = op3(c, OP_I0, 0, 0, NULL, NULL, NULL);
  const Core *i1 = op3(c, OP_I1, 0, 0, NULL, NULL, NULL);
  const Core *s = side->b;
  uint32_t q;
  for (q = 0; q < n && s != NULL && s->kind == CORE_OP && s->op == OP_SYSTEM; q++, s = s->args[2])
    if (!cells_agree(c, fk[q], core_app(c, core_bind(c, CORE_LAM, side->name, NULL, s->args[1]), i0), i1, u0))
      return 0;
  return 1;
}

/* Only a type in a universe has hcomp. A function type is a pretype when
   its domain or its codomain is a pretype. */
static int hcomp_type(Checker *c, const Value *a) {
  const Value *point;
  if (a == NULL)
    return 0;
  if (val_is(a, OP_I) || val_is(a, OP_PARTIAL))
    return FAIL(c, "TYPE_PRETYPE", "hcomp needs a type in a universe, found the pretype %s", show(c, a));
  if (a->kind != VAL_PI)
    return 1;
  if (!hcomp_type(c, a->dom))
    return 0;
  point = val_var(c->m, c->m->fresh_level);
  return point != NULL && hcomp_type(c, closure_apply(c->m, a, point));
}

/* hcomp side u0: A is the hint, else the type of u0. φ is the join of the
   faces of the side; where a face holds, its branch at j = i0 is u0. The
   type is A. */
static const Core *rule_hcomp(Call *k) {
  Checker *c = k->c;
  const Value *a = k->hint;
  const Core *fk[CELLS_MAX];
  const Core *args[4];
  const Core *phi = NULL;
  const Core *side;
  const Core *u0 = NULL;
  uint32_t n = 0;
  if (a == NULL) {
    u0 = try_infer(c, k->args[1], &a);
    if (u0 == NULL)
      return need_type(k, "A");
  }
  if (!hcomp_type(c, a))
    return NULL;
  side = side_of(c, k->args[0], a, &phi, fk, &n);
  if (side == NULL)
    return NULL;
  if (u0 == NULL)
    u0 = check(c, k->args[1], a);
  if (u0 == NULL || !side_base(c, side, fk, n, u0))
    return NULL;
  k->type = a;
  args[0] = quote_value(c->m, c->level, a);
  args[1] = phi;
  args[2] = side;
  args[3] = u0;
  return core_op(c, OP_HCOMP, 0, 0, args, 4u);
}

/* The type (T : Type 0) × Equiv T A of a Glue branch, from the domain
   definition Equiv. */
static const Value *equiv_sigma(Checker *c, const Value *av) {
  uint32_t d = find_def(c->m, "Equiv");
  const Core *body;
  if (d == NO_DEF) {
    FAIL(c, "NAME_UNKNOWN", "Glue needs the domain definition Equiv, which is not declared");
    return NULL;
  }
  body = core_app(c, core_app(c, core_index(c, CORE_GLOBAL, d), core_index(c, CORE_VAR, 0)), quote_value(c->m, c->level + 1u, av));
  return here(c, core_bind(c, CORE_SIGMA, "T", core_leaf(c, CORE_UNIV, 0), body));
}

/* 1 when a failure inside a Glue branch keeps its own code. */
static int glue_keeps(const char *code) {
  return code == NULL || strcmp(code, "OOM") == 0 || strcmp(code, "INTERNAL") == 0 || strcmp(code, "EVAL_FUEL") == 0
         || strcmp(code, "EVAL_DEPTH") == 0;
}

/* A failed Glue branch fails again as CODE over the diagnostic state SAVED.
   The text keeps the inner code and message. */
static const Core *glue_refail(Checker *c, const Diag *saved, const char *code) {
  Diag inner = *c->m->diag;
  if (glue_keeps(inner.code))
    return NULL;
  *c->m->diag = *saved;
  return nul(FAIL(c, code, "each branch of Glue needs pack T e with e : Equiv T A (%s: %s)", inner.code, inner.msg));
}

/* The system [ f ↦ t , ... ] of Glue and glue, with no binder: each face
   in I, their join to *PHI, each branch against WANT under its face, then
   the overlaps. The faces go to FK and FV, the branches to UK. A branch
   that fails, fails again as WRAP when WRAP is not NULL. */
static const Core *glue_system(Checker *c, const Term *t, const char *who, const Value *want, const char *wrap, const Core **phi, const Core **fk, const Value **fv, const Core **uk, uint32_t *n) {
  const Term *s = t;
  const Term *branch[CELLS_MAX];
  const Core *body;
  Diag saved = *c->m->diag;
  uint32_t i;
  uint32_t q;
  *n = 0;
  *phi = op3(c, OP_I0, 0, 0, NULL, NULL, NULL);
  if (!is_system_cell(s) && (s->kind != TERM_VAR || strcmp(s->name, "[]") != 0))
    return nul(FAIL(c, "TYPE_SYSTEM", "%s needs [ ... ]", who));
  for (; is_system_cell(s) && *n < CELLS_MAX; s = s->right) {
    fk[*n] = check(c, s->left->left->right, op0(c, OP_I));
    if (fk[*n] == NULL)
      return NULL;
    fv[*n] = here(c, fk[*n]);
    if (fv[*n] == NULL)
      return NULL;
    branch[*n] = s->left->right;
    *phi = *n == 0 ? fk[0] : op3(c, OP_IMAX, 0, 2, *phi, fk[*n], NULL);
    (*n)++;
  }
  if (s->kind != TERM_VAR || strcmp(s->name, "[]") != 0)
    return nul(FAIL(c, "TYPE_SYSTEM", "a system has at most %u cells", (unsigned)CELLS_MAX));
  for (i = 0; i < *n; i++) {
    uk[i] = check_on(c, branch[i], want, fk[i]);
    if (uk[i] == NULL)
      return wrap == NULL ? NULL : glue_refail(c, &saved, wrap);
  }
  for (i = 0; i < *n; i++)
    for (q = i + 1u; q < *n; q++)
      if (!cells_agree(c, fk[i], uk[i], fk[q], uk[q]))
        return NULL;
  body = op3(c, OP_SYSNIL, 0, 0, NULL, NULL, NULL);
  for (i = *n; i > 0; i--)
    body = op3(c, OP_SYSTEM, 0, 3, fk[i - 1u], uk[i - 1u], body);
  return body;
}

/* Glue A [ f ↦ pack T e , ... ]: A : Type 0 and each branch is pack T e
   with e : Equiv T A. The type is Type 0. A branch that fails is
   GLUE_EQUIV. */
static const Core *rule_glue(Call *k) {
  Checker *c = k->c;
  const Core *fk[CELLS_MAX];
  const Value *fv[CELLS_MAX];
  const Core *uk[CELLS_MAX];
  const Core *phi = NULL;
  const Core *sys;
  const Core *a = check(c, k->args[0], val_univ(c->m, 0));
  const Value *want = a == NULL ? NULL : equiv_sigma(c, here(c, a));
  uint32_t n = 0;
  if (want == NULL)
    return NULL;
  sys = glue_system(c, k->args[1], "Glue", want, "GLUE_EQUIV", &phi, fk, fv, uk, &n);
  if (sys == NULL)
    return NULL;
  k->type = val_univ(c->m, 0);
  return op3(c, OP_GLUE, 0, 3, a, phi, sys);
}

/* glue [ f ↦ t , ... ] a against Glue A sys, in check mode: the faces join
   to the face of the type (GLUE_COVER), each branch has the type under its
   face (there Glue A sys is T), a : A, and on each face unglue t is a
   (GLUE_BOUNDARY). Leaf has no face environment: the boundary test is
   conv_under_face on the global unglue t, as in cells_agree. */
static const Core *rule_glue_elem(Call *k) {
  Checker *c = k->c;
  const Value *g = k->hint;
  const Core *fk[CELLS_MAX];
  const Value *fv[CELLS_MAX];
  const Core *uk[CELLS_MAX];
  const Core *args[4];
  const Core *phi = NULL;
  const Core *sys;
  const Core *a;
  const Core *gt;
  const Core *ung;
  const Value *face;
  const Value *uv;
  const Value *av;
  FaceMeets fm;
  uint32_t n = 0;
  uint32_t i;
  if (!val_is(g, OP_GLUE))
    return need_type(k, "Glue A sys");
  sys = glue_system(c, k->args[0], "glue", g, NULL, &phi, fk, fv, uk, &n);
  if (sys == NULL)
    return NULL;
  if (!same_face(c, here(c, phi), g->args[1]))
    return nul(FAIL(c, "GLUE_COVER", "the faces of glue join to %s, but the type needs %s", show(c, here(c, phi)), show(c, g->args[1])));
  a = check(c, k->args[1], g->args[0]);
  av = a == NULL ? NULL : here(c, a);
  args[0] = quote_value(c->m, c->level, g->args[0]);
  args[1] = quote_value(c->m, c->level, g->args[1]);
  args[2] = quote_value(c->m, c->level, g->args[2]);
  gt = quote_value(c->m, c->level, g);
  if (av == NULL || args[0] == NULL || args[1] == NULL || args[2] == NULL || gt == NULL)
    return NULL;
  for (i = 0; i < n; i++) {
    face = c->face == NULL ? fv[i] : tyop(c, OP_IMIN, 2, c->face, fv[i], NULL);
    args[3] = uk[i];
    ung = face == NULL ? NULL : core_op(c, OP_UNGLUE, 0, 0, args, 4u);
    uv = ung == NULL ? NULL : here(c, ung);
    if (uv == NULL)
      return NULL;
    if (!face_meets(c->m, c->level, face, &fm))
      return nul(FAIL(c, "TYPE_SYSTEM", "the Glue boundary face %s is not in the face bounds", show(c, face)));
    if (!conv_under_face(c->m, c->level, face, uv, av))
      return c->m->diag->set ? NULL
                             : nul(FAIL(c, "GLUE_BOUNDARY", "on the face %s, unglue of the branch is %s, but the base is %s", show(c, face),
                                        show(c, value_under_face(c->m, c->level, face, uv)), show(c, value_under_face(c->m, c->level, face, av))));
  }
  k->type = g;
  return op3(c, OP_GLUE_ELEM, 0, 3, gt, sys, a);
}

/* unglue g: g : Glue A sys, of type A. */
static const Core *rule_unglue(Call *k) {
  Checker *c = k->c;
  const Value *gt = NULL;
  const Core *args[4];
  const Core *g = infer(c, k->args[0], &gt);
  if (g == NULL)
    return NULL;
  if (!val_is(gt, OP_GLUE))
    return nul(FAIL(c, "UNGLUE_TYPE", "unglue needs a term of a type Glue A sys, found a term of type %s", show(c, gt)));
  args[0] = quote_value(c->m, c->level, gt->args[0]);
  args[1] = quote_value(c->m, c->level, gt->args[1]);
  args[2] = quote_value(c->m, c->level, gt->args[2]);
  args[3] = g;
  if (args[0] == NULL || args[1] == NULL || args[2] == NULL)
    return NULL;
  k->type = gt->args[0];
  return core_op(c, OP_UNGLUE, 0, 0, args, 4u);
}

/* A path rule (PLAN 4.2): each 1-variable that its arguments use is free
   under an interval binder or in transp, hcomp, Glue, so both end points
   hold it. */
static const Core *path_rule(Call *k, const Core *(*f)(Call *)) {
  uint32_t *saved = uses_save(k->c);
  const Core *r = f(k);
  return r != NULL && !uses_refuse(k->c, saved, "REFUSE_AFFINE_PATH", PATH_WHY) ? NULL : r;
}

static const Core *rule(Call *k) {
  switch (k->b->rule) {
  case RULE_TYPE0:
    k->type = val_univ(k->c->m, 0);
    return op3(k->c, k->b->op, 0, 0, NULL, NULL, NULL);
  case RULE_VALUE0:
    k->type = tyop(k->c, k->b->result, 0, NULL, NULL, NULL);
    return op3(k->c, k->b->op, 0, 0, NULL, NULL, NULL);
  case RULE_INTERVAL:
    return rule_interval(k);
  case RULE_PARTIAL:
    return rule_partial(k);
  case RULE_SYSTEM:
    return rule_system(k);
  case RULE_TRANSP:
    return path_rule(k, rule_transp);
  case RULE_HCOMP:
    return path_rule(k, rule_hcomp);
  case RULE_GLUE:
    return path_rule(k, rule_glue);
  case RULE_GLUE_ELEM:
    return path_rule(k, rule_glue_elem);
  case RULE_UNGLUE:
    return path_rule(k, rule_unglue);
  case RULE_TYPE1:
    return rule_type1(k);
  case RULE_EMPTY:
    return rule_empty(k);
  case RULE_SOME:
    return rule_some(k);
  case RULE_PACK:
    return rule_pack(k);
  case RULE_REFL:
    return rule_refl(k);
  case RULE_SIGMA_PART:
    return rule_sigma_part(k);
  case RULE_UNPACK:
    return rule_unpack(k);
  case RULE_OPTION:
    return rule_option(k);
  case RULE_PURE:
    return rule_pure(k);
  case RULE_MAP:
    return rule_map(k);
  case RULE_BIND:
    return rule_bind(k);
  case RULE_FOLD:
    return rule_fold(k);
  case RULE_SYMM:
    return path_rule(k, rule_symm);
  case RULE_TRANS:
    return path_rule(k, rule_trans);
  case RULE_CONG:
    return path_rule(k, rule_cong);
  case RULE_TRANSPORT:
    return path_rule(k, rule_transport);
  case RULE_NAT2:
    return rule_nat2(k);
  case RULE_FLAG_IF:
    return rule_flag_if(k);
  case RULE_PATHP:
    return rule_pathp(k);
  }
  return NULL;
}

/* Checks the family parameters ARGS. VALS gets their values. */
static int check_params(Checker *c, const FamilyInfo *f, const Term *const *args, const Core **cores, const Value **vals) {
  uint32_t i;
  for (i = 0; i < f->param_count; i++) {
    cores[i] = check_at(c, MULT_ZERO, args[i], eval_core(c->m, env_of(c, vals, i), f->param_types[i]));
    vals[i] = here(c, cores[i]);
    if (vals[i] == NULL)
      return 0;
  }
  return 1;
}

static const Core *family_app(Call *k, uint32_t fam) {
  Checker *c = k->c;
  const FamilyInfo *f = &c->m->families[fam];
  const Core **cores = arena_alloc(c->m->arena, ((size_t)f->param_count + 1u) * sizeof *cores);
  const Value **vals = arena_alloc(c->m->arena, ((size_t)f->param_count + 1u) * sizeof *vals);
  if (cores == NULL || vals == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  if (!check_params(c, f, k->args, cores, vals))
    return NULL;
  k->type = val_univ(c->m, f->universe);
  return core_op(c, OP_FAMILY, fam, 0, cores, f->param_count);
}

/* A constructor takes the family parameters (the indices), then its fields. */
static const Core *ctor_app(Call *k, uint32_t ctor) {
  Checker *c = k->c;
  const CtorInfo *ci = &c->m->ctors[ctor];
  const FamilyInfo *f = &c->m->families[ci->family];
  uint32_t p = f->param_count;
  uint32_t n = p + ci->field_count;
  const Core **cores = arena_alloc(c->m->arena, ((size_t)n + 1u) * sizeof *cores);
  const Value **vals = arena_alloc(c->m->arena, ((size_t)p + 1u) * sizeof *vals);
  const Env *env;
  uint32_t j;
  if (cores == NULL || vals == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  if (!check_params(c, f, k->args, cores, vals))
    return NULL;
  env = env_of(c, vals, p);
  for (j = 0; j < ci->field_count; j++) {
    cores[p + j] = check(c, k->args[p + j], eval_core(c->m, env, ci->fields[j].type));
    if (cores[p + j] == NULL)
      return NULL;
  }
  k->type = val_op(c->m, VAL_OP, OP_FAMILY, ci->family, 0, vals, p);
  return core_op(c, OP_CTOR, ctor, 0, cores, n);
}

/* A field of a one-constructor family is a projection. */
static const Core *proj_app(Call *k, uint32_t ctor, uint32_t field) {
  Checker *c = k->c;
  const CtorInfo *ci = &c->m->ctors[ctor];
  const Value *xt = NULL;
  const Core *x = infer(c, k->args[0], &xt);
  if (x == NULL)
    return NULL;
  if (!val_is(xt, OP_FAMILY) || xt->inst != ci->family)
    return nul(FAIL(c, "TYPE_MISMATCH", "%s needs a value of the family %s, found a term of type %s", ci->fields[field].name, c->m->families[ci->family].name, show(c, xt)));
  k->type = eval_core(c->m, env_of(c, xt->args, xt->argc), ci->fields[field].type);
  return core_op(c, OP_PROJ, ctor, field, &x, 1);
}

static int find_known(Checker *c, const char *name, Head *h) {
  const Machine *m = c->m;
  uint32_t i;
  uint32_t j;
  for (i = 0; i < m->family_count; i++) {
    if (strcmp(m->families[i].name, name) == 0) {
      h->kind = HEAD_FAMILY;
      h->index = i;
      c->self_uses += i == c->self;
      return 1;
    }
  }
  for (i = 0; i < m->ctor_count; i++) {
    if (strcmp(m->ctors[i].name, name) == 0) {
      h->kind = HEAD_CTOR;
      h->index = i;
      return 1;
    }
  }
  for (i = 0; i < m->ctor_count; i++) {
    for (j = 0; m->families[m->ctors[i].family].ctor_count == 1u && j < m->ctors[i].field_count; j++) {
      if (strcmp(m->ctors[i].fields[j].name, name) == 0) {
        h->kind = HEAD_PROJ;
        h->index = i;
        h->field = j;
        return 1;
      }
    }
  }
  for (i = 0; i < BUILTIN_COUNT; i++) {
    if (strcmp(BUILTINS[i].name, name) == 0) {
      h->kind = HEAD_BUILTIN;
      h->builtin = &BUILTINS[i];
      return 1;
    }
  }
  return 0;
}

/* Core names that S1 removed. FIX is the name that replaces it, or NULL
   when no name does; HINT goes into the message. */
typedef struct {
  const char *name;
  const char *fix;
  const char *hint;
  const char *stage; /* the step that removed NAME */
} Dropped;

static const Dropped DROPPED[] = {
  {"Flag", "Bool", "use Bool", "S1"},
  {"flagYes", "true", "use true", "S1"},
  {"flagNo", "false", "use false", "S1"},
  {"Prod", NULL, "write the product of A and B as A × B", "S1"},
  {"pair", "pack", "use pack (a term of A × B is pack a b)", "S1"},
  {"first", "witness", "use witness", "S1"},
  {"second", "payload", "use payload", "S1"},
  {"List", NULL, "use a vector Fin n → A (S2)", "S1"},
  {"nil", NULL, "use a vector Fin n → A (S2)", "S1"},
  {"cons", NULL, "use a vector Fin n → A (S2)", "S1"},
  {"filter", NULL, "no core name replaces it", "S1"},
  {"unfold", NULL, "no core name replaces it before S2", "S1"},
  {"Eq", "Path", "use Path A x y", "S2"}
};

/* An unknown name at HEAD. When it names this declaration or a later one,
   the use is recursion (rule R3). The span is the name; main.c adds the
   file. */
static int unknown_name(Checker *c, const Term *head) {
  const char *name = head->name;
  const Dropped *drop = NULL;
  const char *p;
  int width = 0;
  size_t i;
  Span span;
  for (i = c->decl_index; i < c->decls->count; i++) {
    if (strcmp(c->decls->items[i].name, name) == 0)
      return FAIL(c, "REFUSE_REC", "%s is this definition or a later one; a definition can use only earlier ones (rule R3)", name);
  }
  for (i = 0; i < sizeof DROPPED / sizeof DROPPED[0]; i++) {
    if (drop == NULL && strcmp(DROPPED[i].name, name) == 0) drop = &DROPPED[i];
  }
  for (p = name; *p != '\0'; p++) width += ((unsigned char)*p & 0xC0u) != 0x80u;
  span = (Span){NULL, head->line, head->col, head->line, head->col + width};
  if (drop == NULL)
    return diag_fail_at(c->m->diag, "NAME_UNKNOWN", c->m->def, span, NULL, name, NULL, "unknown name %s", name);
  return diag_fail_at(c->m->diag, "NAME_UNKNOWN", c->m->def, span, drop->fix, name, drop->fix, "unknown name %s (%s removed it); %s", name, drop->stage, drop->hint);
}

/* Name resolution order: locals (the innermost first), definitions,
   families, constructors, projections, core names. */
static int resolve_head(Checker *c, const Term *head, Head *h) {
  memset(h, 0, sizeof *h);
  h->kind = HEAD_TERM;
  if (head->kind != TERM_VAR || find_local(c, head->name) != NULL || find_def(c->m, head->name) != NO_DEF)
    return 1;
  return find_known(c, head->name, h) ? 1 : unknown_name(c, head);
}

static uint32_t known_arity(const Checker *c, const Head *h) {
  const CtorInfo *ci = h->kind == HEAD_CTOR ? &c->m->ctors[h->index] : NULL;
  switch (h->kind) {
  case HEAD_TERM:
    return 0u;
  case HEAD_FAMILY:
    return c->m->families[h->index].param_count;
  case HEAD_CTOR:
    return ci == NULL ? 0u : c->m->families[ci->family].param_count + ci->field_count;
  case HEAD_PROJ:
    return 1u;
  case HEAD_BUILTIN:
    return h->builtin->arity;
  }
  return 0u;
}

static const char *known_name(const Checker *c, const Head *h) {
  switch (h->kind) {
  case HEAD_TERM:
    return "?";
  case HEAD_FAMILY:
    return c->m->families[h->index].name;
  case HEAD_CTOR:
    return c->m->ctors[h->index].name;
  case HEAD_PROJ:
    return c->m->ctors[h->index].fields[h->field].name;
  case HEAD_BUILTIN:
    return h->builtin->name;
  }
  return "?";
}

static const Core *known_core(Call *k, const Head *h) {
  switch (h->kind) {
  case HEAD_TERM:
    return nul(FAIL(k->c, "INTERNAL", "a plain term reached the core name rules"));
  case HEAD_FAMILY:
    return family_app(k, h->index);
  case HEAD_CTOR:
    return ctor_app(k, h->index);
  case HEAD_PROJ:
    return proj_app(k, h->index, h->field);
  case HEAD_BUILTIN:
    return rule(k);
  }
  return NULL;
}

/* An under-applied core name checked against a function type: WHOLE becomes
   `fun x => WHOLE x`, with a name that the lexer cannot make. */
static const Core *eta(Checker *c, const Term *whole, const Value *hint, const Value **type) {
  char *name = arena_alloc(c->m->arena, 24);
  Term *x = arena_alloc(c->m->arena, sizeof *x);
  Term *app = arena_alloc(c->m->arena, sizeof *app);
  Term *lam = arena_alloc(c->m->arena, sizeof *lam);
  if (name == NULL || x == NULL || app == NULL || lam == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  c->eta_count++;
  snprintf(name, 24, "%%eta%u", c->eta_count);
  x->kind = TERM_VAR;
  x->name = name;
  app->kind = TERM_APP;
  app->left = whole;
  app->right = x;
  lam->kind = TERM_LAM;
  lam->name = name;
  lam->right = app;
  return check_lam(c, lam, hint, type);
}

/* p r, where P has the type PT = PathP A a b and r : I. The ends a and b
   ride in the core, as values carry no types. *TY becomes A r. */
static const Core *path_app(Checker *c, const Core *p, const Value *pt, const Term *arg, const Value **ty) {
  const Core *r = check(c, arg, op0(c, OP_I));
  const Core *args[4];
  if (r == NULL)
    return NULL;
  args[0] = p;
  args[1] = r;
  args[2] = quote_value(c->m, c->level, pt->args[1]);
  args[3] = quote_value(c->m, c->level, pt->args[2]);
  *ty = apply_value(c->m, pt->args[0], here(c, r));
  return core_op(c, OP_PAPP, 0, 0, args, 4);
}

/* Applies K of type *TY to ARG. A path takes a point of I. */
static const Core *apply_one(Checker *c, const Core *k, const Value **ty, const Term *arg) {
  const Value *t = *ty;
  const Core *a;
  if (val_is(t, OP_PATHP))
    return path_app(c, k, t, arg, ty);
  if (t == NULL || t->kind != VAL_PI)
    return nul(FAIL(c, "TYPE_NOT_FUNCTION", "a term of type %s is applied to an argument", show(c, t)));
  a = check_at(c, t->dom->kind == VAL_UNIV ? MULT_ZERO : mult_scale(c->at, t->mult), arg, t->dom);
  *ty = closure_apply(c->m, t, here(c, a));
  return core_app(c, k, a);
}

/* Applies K of type TY to ARGS, one by one. */
static const Core *apply_rest(Checker *c, const Core *k, const Value *ty, const Term *const *args, uint32_t argc, const Value *hint, const Value **type) {
  uint32_t i;
  for (i = 0; i < argc && k != NULL; i++)
    k = apply_one(c, k, &ty, args[i]);
  *type = ty;
  return finish(c, k, hint, type);
}

static const Core *var_core(Checker *c, const char *name, const Value **type) {
  Scope *s = find_local(c, name);
  uint32_t d = find_def(c->m, name);
  if (s != NULL) {
    *type = s->type;
    return use_local(c, s) ? core_index(c, CORE_VAR, c->level - 1u - s->level) : NULL;
  }
  if (d != NO_DEF) {
    if (c->at != MULT_ZERO && c->m->defs[d].runtime_refusal != NULL)
      return nul(FAIL(c, c->m->defs[d].runtime_refusal, "%s is checked at 0 and its body is unsafe at run time; it can only be used in an erased position (PLAN 4.1)", name));
    *type = c->m->defs[d].type;
    return core_index(c, CORE_GLOBAL, d);
  }
  return nul(FAIL(c, "NAME_UNKNOWN", "unknown name %s", name));
}

/* 1 when NAME is a line of a codata (an observation such as `head`, a
   cofield such as `tail`) or its corecursor D.corec (S2 U6c2a, U6c2b). */
static int opens_codata(const Checker *c, const char *name) {
  size_t i;
  size_t j;
  for (i = 0; i < c->decls->count; i++) {
    const Decl *d = &c->decls->items[i];
    size_t n = strlen(d->name);
    if (d->kind == DECL_COFAMILY && strncmp(name, d->name, n) == 0 && strcmp(name + n, ".corec") == 0)
      return 1;
    for (j = 0; d->kind == DECL_COFAMILY && j < d->ctor_count; j++) {
      if (strcmp(d->ctors[j].name, name) == 0)
        return 1;
    }
  }
  return 0;
}

/* REFUSE_SEALED (PLAN 4.2, Q-S3-7 (a)): a line of a codata or D.corec at a
   type argument with a resource sort in it opens or builds a Keystore.
   Only the core opens a Keystore (S5 `next`). */
static const Core *apply_term(Checker *c, const Term *head, const Term *const *args, uint32_t argc, const Value *hint, const Value **type) {
  const Value *ty = NULL;
  const Core *k = head->kind == TERM_VAR ? var_core(c, head->name, &ty) : infer(c, head, &ty);
  uint32_t i;
  int sealed = k != NULL && argc > 0u && head->kind == TERM_VAR && find_local(c, head->name) == NULL && opens_codata(c, head->name);
  if (!sealed)
    return apply_rest(c, k, ty, args, argc, hint, type);
  for (i = 0; i < argc && k != NULL; i++) {
    int type_arg = ty != NULL && ty->kind == VAL_PI && ty->dom->kind == VAL_UNIV;
    k = apply_one(c, k, &ty, args[i]);
    if (type_arg && k != NULL && k->kind == CORE_APP && resource_type(c, here(c, k->b)))
      return nul(FAIL(c, "REFUSE_SEALED", "%s at %s opens or builds a stream with a resource sort in its head, a Keystore; the Keystore is sealed and only the core opens it (PLAN 4.2)", head->name, show(c, here(c, k->b))));
  }
  *type = ty;
  return finish(c, k, hint, type);
}

static const Core *apply_known(Checker *c, const Term *whole, const Head *h, const Term *const *args, uint32_t argc, const Value *hint, const Value **type) {
  uint32_t arity = known_arity(c, h);
  Call k;
  const Core *core;
  if (argc < arity && hint != NULL && hint->kind == VAL_PI)
    return eta(c, whole, hint, type);
  if (argc < arity)
    return nul(FAIL(c, "TYPE_ARITY", "%s needs %u arguments, found %u", known_name(c, h), arity, argc));
  memset(&k, 0, sizeof k);
  k.c = c;
  k.b = h->builtin;
  k.args = args;
  k.argc = argc;
  k.hint = argc == arity ? hint : NULL;
  k.outer = hint;
  k.used = arity;
  core = known_core(&k, h);
  return apply_rest(c, core, k.type, args + k.used, argc - k.used, hint, type);
}

static const Core *elab_spine(Checker *c, const Term *t, const Value *hint, const Value **type) {
  const Term **args;
  const Term *head = t;
  uint32_t argc = 0;
  uint32_t i;
  Head h;
  while (head->kind == TERM_APP) {
    argc++;
    head = head->left;
  }
  if (argc > SPINE_MAX)
    return nul(FAIL(c, "TYPE_ARITY", "an application has more than %u arguments", SPINE_MAX));
  args = arena_alloc(c->m->arena, ((size_t)argc + 1u) * sizeof *args);
  if (args == NULL)
    return nul(FAIL(c, "OOM", "out of memory"));
  for (head = t, i = argc; head->kind == TERM_APP; head = head->left)
    args[--i] = head->right;
  if (!resolve_head(c, head, &h))
    return NULL;
  return h.kind == HEAD_TERM ? apply_term(c, head, args, argc, hint, type) : apply_known(c, t, &h, args, argc, hint, type);
}

static int annotation_ok(Checker *c, const Term *ann, const Value *dom) {
  uint64_t level = 0;
  const Value *v = here(c, check_type_at(c, ann, &level, 1));
  if (v == NULL)
    return 0;
  return converts(c, c->level, v, dom) ? 1 : FAIL(c, "TYPE_MISMATCH", "the binder type %s differs from the expected type %s", show(c, v), show(c, dom));
}

/* λ i → t against E = PathP A a b: t checks against A i under i : I, then
   the ends of the path must be a and b. Both ends hold t, so at 1 each
   1-variable that t uses is REFUSE_AFFINE_PATH; at omega t counts for one
   call, as in check_lam (PLAN 4.2). */
static const Core *check_plam(Checker *c, const Term *t, const Value *e, const Value **type) {
  Mult at = c->at;
  uint32_t *uses = at == MULT_ZERO ? NULL : uses_save(c);
  const Core *body;
  const Core *lam;
  const Value *f;
  const Value *at0;
  const Value *at1;
  if (t->left != NULL && !annotation_ok(c, t->left, op0(c, OP_I)))
    return NULL;
  if (!push(c, t->name, op0(c, OP_I)))
    return NULL;
  body = check_at(c, at == MULT_OMEGA ? MULT_ONE : at, t->right, apply_value(c->m, e->args[0], c->env->value));
  pop(c);
  if (body != NULL && !uses_refuse(c, uses, "REFUSE_AFFINE_PATH", PATH_WHY))
    return NULL;
  if (body != NULL && at == MULT_OMEGA && !uses_omega(c, uses))
    return NULL;
  lam = core_bind(c, CORE_LAM, t->name, NULL, body);
  f = here(c, lam);
  if (f == NULL)
    return NULL;
  at0 = apply_value(c->m, f, op0(c, OP_I0));
  at1 = apply_value(c->m, f, op0(c, OP_I1));
  if (!converts(c, c->level, at0, e->args[1]))
    return nul(FAIL(c, "TYPE_ENDPOINT", "the path at i0 normalizes to %s, the type needs %s", show(c, at0), show(c, e->args[1])));
  if (!converts(c, c->level, at1, e->args[2]))
    return nul(FAIL(c, "TYPE_ENDPOINT", "the path at i1 normalizes to %s, the type needs %s", show(c, at1), show(c, e->args[2])));
  *type = e;
  return op3(c, OP_PLAM, 0, 1, lam, NULL, NULL);
}

static const Core *check_lam(Checker *c, const Term *t, const Value *hint, const Value **type) {
  Mult at = c->at;
  uint32_t *uses = at == MULT_OMEGA ? uses_save(c) : NULL;
  const Core *body;
  if (val_is(hint, OP_PATHP))
    return check_plam(c, t, hint, type);
  if (hint->kind != VAL_PI)
    return nul(FAIL(c, "TYPE_MISMATCH", "a function is not a term of type %s", show(c, hint)));
  if (t->mult != MULT_OMEGA && t->mult != hint->mult)
    return nul(FAIL(c, "TYPE_MISMATCH", "the explicit binder multiplicity does not match the declared function type"));
  if (hint->mult == MULT_OMEGA && resource_type(c, hint->dom))
    return nul(FAIL(c, "REFUSE_AFFINE_OMEGA", "a resource argument cannot have an omega binder (PLAN 4.1)"));
  if (t->left != NULL && !annotation_ok(c, t->left, hint->dom))
    return NULL;
  if (!push_at(c, hint->mult, t->name, hint->dom))
    return NULL;
  body = check_at(c, at == MULT_OMEGA ? MULT_ONE : at, t->right, closure_apply(c->m, hint, c->env->value));
  pop(c);
  if (body != NULL && at == MULT_OMEGA && !uses_omega(c, uses))
    return NULL;
  *type = hint;
  return core_binder(c, CORE_LAM, hint->mult, t->name, NULL, body);
}

static const Core *infer_lam(Checker *c, const Term *t, const Value **type) {
  uint64_t level = 0;
  const Value *dom;
  const Value *bt = NULL;
  const Core *body;
  const Core *quoted;
  Mult at = c->at;
  uint32_t *uses = at == MULT_OMEGA ? uses_save(c) : NULL;
  if (t->left == NULL)
    return nul(FAIL(c, "TYPE_INFER", "fun %s => ... needs a binder type here", t->name));
  dom = here(c, check_type_at(c, t->left, &level, 1));
  if (!push_at(c, t->mult, t->name, dom))
    return NULL;
  c->at = at == MULT_OMEGA ? MULT_ONE : at;
  body = infer(c, t->right, &bt);
  c->at = at;
  quoted = body == NULL ? NULL : quote_value(c->m, c->level, bt);
  pop(c);
  if (body != NULL && at == MULT_OMEGA && !uses_omega(c, uses))
    return NULL;
  if (t->mult == MULT_OMEGA && resource_type(c, dom))
    return nul(FAIL(c, "REFUSE_AFFINE_OMEGA", "the binder %s has the resource type %s at omega; bind it at 1 (@1 or ⊸) or at 0 (@0) (PLAN 4.1)", t->name != NULL ? t->name : "_", show(c, dom)));
  *type = val_binder(c->m, VAL_PI, t->mult, t->name, dom, c->env, quoted);
  return core_binder(c, CORE_LAM, t->mult, t->name, NULL, body);
}

/* 1 when V is the position family D.pos of a codata D (S2 U6c2a). */
static int pos_family(const Checker *c, const Value *v) {
  const char *name = v != NULL && v->kind == VAL_OP && v->op == OP_FAMILY && v->argc == 0u && v->inst < c->m->family_count ? c->m->families[v->inst].name : NULL;
  size_t n = name == NULL ? 0u : strlen(name);
  return n > 4u && strcmp(name + n - 4u, ".pos") == 0;
}

typedef enum { RESOURCE_NONE, RESOURCE_UNKNOWN, RESOURCE_PRESENT } Resource;

static Resource resource_join(Resource a, Resource b) {
  return a > b ? a : b;
}

/* Inspect run-time fields after substituting family parameters. Unknown
   types stay distinct from types known to contain no resources: an
   eliminator must not promote a polymorphic payload to omega. Ordinary
   function types retain the QTT reading with no closure bit. */
static Resource resource_shape(Checker *c, const Value *v, uint32_t level) {
  const FamilyInfo *f;
  const Env *env;
  Resource shape = RESOURCE_NONE;
  uint32_t i;
  uint32_t j;
  if (v == NULL)
    return RESOURCE_UNKNOWN;
  if (v->kind == VAL_UNIV)
    return RESOURCE_NONE;
  if (v->kind == VAL_PI)
    return pos_family(c, v->dom) ? resource_shape(c, closure_apply(c->m, v, val_var(c->m, level)), level + 1u) : RESOURCE_UNKNOWN;
  if (v->kind == VAL_SIGMA)
    return resource_join(resource_shape(c, v->dom, level), resource_shape(c, closure_apply(c->m, v, val_var(c->m, level)), level + 1u));
  if (v->kind != VAL_OP)
    return RESOURCE_UNKNOWN;
  if (v->op == OP_LEAF)
    return RESOURCE_PRESENT;
  if (v->op == OP_NAT || v->op == OP_BOOL || v->op == OP_UNIT || v->op == OP_I || v->op == OP_SSET)
    return RESOURCE_NONE;
  if (v->op == OP_OPTION && v->argc == 1u)
    return resource_shape(c, v->args[0], level);
  if (v->op == OP_PATHP && v->argc == 3u)
    return resource_shape(c, apply_value(c->m, v->args[0], op0(c, OP_I0)), level);
  if (v->op == OP_FLAG_IF && v->argc == 3u)
    return resource_join(resource_shape(c, v->args[1], level), resource_shape(c, v->args[2], level));
  if (v->op != OP_FAMILY || v->inst >= c->m->family_count)
    return RESOURCE_UNKNOWN;
  f = &c->m->families[v->inst];
  env = env_of(c, v->args, v->argc);
  /* Direct recursive fields repeat this family. All other fields form
     an acyclic graph, since forward references are refused. Inspect the
     instantiated fields, including closed families and every parameter. */
  for (i = 0; i < f->ctor_count; i++) {
    const CtorInfo *ctor = &c->m->ctors[f->first_ctor + i];
    for (j = 0; j < ctor->field_count; j++) {
      if (!ctor->fields[j].recursive)
        shape = resource_join(shape, resource_shape(c, eval_core(c->m, env, ctor->fields[j].type), level));
    }
  }
  return shape;
}

static int resource_type(Checker *c, const Value *v) {
  return resource_shape(c, v, c->level) == RESOURCE_PRESENT;
}

static int copyable_type(Checker *c, const Value *v) {
  return resource_shape(c, v, c->level) == RESOURCE_NONE;
}

/* (x : A) -> B and Sigma (x : A) B. */
static const Core *elab_binder(Checker *c, const Term *t, CoreKind kind, const Value *hint, const Value **type) {
  uint64_t la = 0;
  uint64_t lb = 0;
  int pretype = kind == CORE_PI; /* a Pi over I is a pretype; a Sigma refuses one */
  const Core *a = check_type_at(c, t->left, &la, pretype);
  const Core *b;
  if (a != NULL && kind == CORE_PI && t->mult == MULT_OMEGA && resource_type(c, here(c, a)))
    return nul(FAIL(c, "REFUSE_AFFINE_OMEGA", "the binder %s has the resource type %s at omega; bind it at 1 (@1 or ⊸) or at 0 (@0) (PLAN 4.1)", t->name != NULL ? t->name : "_", show(c, here(c, a))));
  if (a == NULL || !push_at(c, t->mult, t->name, here(c, a)))
    return NULL;
  b = check_type_at(c, t->right, &lb, pretype);
  pop(c);
  *type = la == UINT64_MAX || lb == UINT64_MAX ? op0(c, OP_SSET) : val_univ(c->m, la > lb ? la : lb);
  return finish(c, core_binder(c, kind, t->mult, t->name, a, b), hint, type);
}

static const Core *elab_inner(Checker *c, const Term *t, const Value *hint, const Value **type) {
  switch (t->kind) {
  case TERM_VAR:
  case TERM_APP:
    return elab_spine(c, t, hint, type);
  case TERM_NAT:
    *type = nat_type(c);
    return finish(c, core_leaf(c, CORE_NAT, t->nat), hint, type);
  case TERM_LAM:
    return hint != NULL ? check_lam(c, t, hint, type) : infer_lam(c, t, type);
  case TERM_PI:
    return elab_binder(c, t, CORE_PI, hint, type);
  case TERM_SIGMA:
    return elab_binder(c, t, CORE_SIGMA, hint, type);
  case TERM_UNIV:
    if (t->nat >= UNIVERSE_MAX)
      return nul(FAIL(c, "TYPE_UNIVERSE", "Type %llu is above the largest universe Type %u", (unsigned long long)t->nat, UNIVERSE_MAX - 1u));
    *type = val_univ(c->m, t->nat + 1u);
    return finish(c, core_leaf(c, CORE_UNIV, t->nat), hint, type);
  }
  return NULL;
}

static const Core *elab(Checker *c, const Term *t, const Value *hint, const Value **type) {
  const Core *k;
  if (c->depth >= CHECK_DEPTH_LIMIT)
    return nul(FAIL(c, "CHECK_DEPTH", "the term is nested deeper than %u levels", CHECK_DEPTH_LIMIT));
  c->depth++;
  k = elab_inner(c, t, hint, type);
  if (k != NULL && *type != NULL && (*type)->kind == VAL_PI && pos_family(c, (*type)->dom) && resource_type(c, *type)) {
    const Value *v = here(c, k);
    if (v != NULL && v->kind == VAL_LAM)
      k = nul(FAIL(c, "REFUSE_SEALED", "a function cannot construct a stream with a resource sort in its head; only the core builds a Keystore (PLAN 4.2)"));
  }
  c->depth--;
  return k;
}

/* NULL when NAME is free, else the reason it is taken (rule R4). */
static const char *name_owner(const Checker *c, const char *name) {
  const Machine *m = c->m;
  uint32_t i;
  uint32_t j;
  for (i = 0; i < sizeof RESERVED / sizeof RESERVED[0]; i++) {
    if (strcmp(RESERVED[i], name) == 0)
      return "a reserved name";
  }
  for (i = 0; i < BUILTIN_COUNT; i++) {
    if (strcmp(BUILTINS[i].name, name) == 0)
      return "a core name";
  }
  for (i = 0; i < m->family_count; i++) {
    if (strcmp(m->families[i].name, name) == 0)
      return "a domain family";
  }
  for (i = 0; i < m->ctor_count; i++) {
    for (j = 0; j < m->ctors[i].field_count; j++) {
      if (strcmp(m->ctors[i].fields[j].name, name) == 0)
        return "a domain field";
    }
    if (strcmp(m->ctors[i].name, name) == 0)
      return "a domain constructor";
  }
  i = find_def(m, name);
  if (i == NO_DEF)
    return NULL;
  return m->defs[i].origin == ORIGIN_DOMAIN ? "a domain definition" : "already defined";
}

static int name_free(Checker *c, const char *name) {
  const char *why = name_owner(c, name);
  return why == NULL ? 1 : FAIL(c, "REFUSE_NAME", "%s is %s (rule R4)", name, why);
}

/* A family with a type parameter is a type constructor, so it must be a
   cell signature (S3, rule R1): only type parameters, each type parameter
   the one field of exactly one generator constructor, and every other
   field closed data or the family at its own parameters. The S1 grammar
   has no type index and no and block, so the port has no arms for them.
   REFUSE_CELL comes before DOMAIN_FAMILY. */
static int cell_type_argument(const Core *t) {
  while (t->kind == CORE_PI)
    t = t->b;
  return t->kind == CORE_UNIV;
}

static int has_type_param(const FamilyInfo *f) {
  uint32_t k;
  for (k = 0; k < f->param_count; k++)
    if (cell_type_argument(f->param_types[k]))
      return 1;
  return 0;
}

/* The type parameter that T is, or the parameter count when T is not one.
   A field type sees only the parameters, not the earlier fields. */
static uint32_t cell_param(const FamilyInfo *f, const Core *t) {
  uint32_t p = f->param_count;
  uint32_t k = t->kind == CORE_VAR && t->index < p ? p - 1u - t->index : p;
  return k < p && f->param_types[k]->kind == CORE_UNIV ? k : p;
}

static int cell_header(Checker *c, const Decl *d, const FamilyInfo *f) {
  uint32_t k;
  for (k = 0; has_type_param(f) && k < f->param_count; k++)
    if (f->param_types[k]->kind != CORE_UNIV)
      return FAIL(c, "REFUSE_CELL", "%s has a type argument and the parameter %s is not a universe; a cell signature has only type parameters (rule R1)", f->name, d->params[k].name);
  return 1;
}

/* Closed data cannot name a type parameter. Binders inside the field type
   still bind their own variables. */
static int cell_closed(const Core *t, uint32_t depth) {
  uint32_t i;
  int binder;
  if (t == NULL)
    return 1;
  if (t->kind == CORE_VAR)
    return t->index < depth;
  binder = t->kind == CORE_PI || t->kind == CORE_SIGMA || t->kind == CORE_LAM;
  if (!cell_closed(t->a, depth) || !cell_closed(t->b, depth + (uint32_t)binder))
    return 0;
  for (i = 0; i < t->argc; i++)
    if (!cell_closed(t->args[i], depth))
      return 0;
  return 1;
}

/* T is the family FAM at its own parameters. */
static int own_family(const FamilyInfo *f, uint32_t fam, const Core *t) {
  uint32_t k;
  int own = t->kind == CORE_OP && t->op == OP_FAMILY && t->inst == fam && t->argc == f->param_count;
  for (k = 0; own && k < f->param_count; k++)
    own = t->args[k]->kind == CORE_VAR && t->args[k]->index == f->param_count - 1u - k;
  return own;
}

static int cell_field(Checker *c, uint32_t fam, const FieldInfo *out, size_t field_count) {
  const FamilyInfo *f = &c->m->families[fam];
  const Core *t = out->type;
  if (cell_param(f, t) < f->param_count)
    return field_count == 1u ? 1 : FAIL(c, "REFUSE_CELL", "%s is not a cell signature: the field %s has a type parameter as its type, so it must be the one field of its constructor (rule R1)", f->name, out->name);
  if (own_family(f, fam, t) || (c->self_uses == 0u && cell_closed(t, 0u)))
    return 1;
  return FAIL(c, "REFUSE_CELL", "%s is not a cell signature: the field %s must be a type parameter, closed data or %s at its own parameters (rule R1)", f->name, out->name, f->name);
}

/* Each type parameter has exactly one generator (S3, open point QA1:
   two generators for one parameter are refused). */
static int cell_generators(Checker *c, const Decl *d, uint32_t fam) {
  const Machine *m = c->m;
  const FamilyInfo *f = &m->families[fam];
  uint32_t k, i;
  for (k = 0; k < f->param_count; k++) {
    uint32_t gens = 0;
    for (i = 0; i < f->ctor_count; i++)
      gens += m->ctors[f->first_ctor + i].field_count == 1u && cell_param(f, m->ctors[f->first_ctor + i].fields[0].type) == k;
    if (gens == 0u)
      return FAIL(c, "REFUSE_CELL", "%s is not a cell signature: the type parameter %s occurs in no constructor (rule R1)", f->name, d->params[k].name);
    if (gens > 1u)
      return FAIL(c, "REFUSE_CELL", "%s is not a cell signature: the type parameter %s has %u generators (rule R1)", f->name, d->params[k].name, (unsigned)gens);
  }
  return 1;
}

/* A recursive field must be the whole field type: the family itself in a
   family with no parameters, or the family at its own parameters in a
   family with type parameters (the free cell monad, PLAN 2.2). A field of
   a family with a type parameter meets the cell criterion first.
   check_ctor allows one recursive field in each constructor of a family
   with parameters (a spine, O-U6b-1). */
static int check_field(Checker *c, uint32_t fam, const Field *field, FieldInfo *out, size_t field_count) {
  uint64_t level = 0;
  const Core *t;
  int whole;
  if (!name_free(c, field->name))
    return 0;
  c->self_uses = 0;
  t = check_type(c, field->type, &level);
  if (t == NULL)
    return 0;
  if (level > c->m->families[fam].universe)
    c->m->families[fam].universe = level;
  whole = t->kind == CORE_OP && t->op == OP_FAMILY && t->inst == fam &&
          (c->m->families[fam].param_count == 0u || (has_type_param(&c->m->families[fam]) && own_family(&c->m->families[fam], fam, t)));
  out->name = field->name;
  out->type = t;
  out->recursive = c->self_uses > 0u;
  if (has_type_param(&c->m->families[fam]) && !cell_field(c, fam, out, field_count))
    return 0;
  if (out->recursive && !whole)
    return FAIL(c, "DOMAIN_FAMILY", "the recursive field %s must have the type %s itself in a family with no parameters, or %s at its own parameters in a family with type parameters", field->name, c->m->families[fam].name, c->m->families[fam].name);
  return 1;
}

static int check_ctor(Checker *c, uint32_t fam, const Ctor *ctor) {
  Machine *m = c->m;
  FieldInfo *fields = arena_alloc(m->arena, (ctor->field_count + 1u) * sizeof *fields);
  CtorInfo *ci = &m->ctors[m->ctor_count];
  uint32_t recursive = 0;
  size_t j;
  if (fields == NULL)
    return FAIL(c, "OOM", "out of memory");
  if (!name_free(c, ctor->name))
    return 0;
  for (j = 0; j < ctor->field_count; j++) {
    size_t earlier;
    const char *name = ctor->fields[j].name;
    if (strcmp(name, ctor->name) == 0)
      return FAIL(c, "REFUSE_NAME", "%s is the constructor name (rule R4)", name);
    for (earlier = 0; earlier < j; earlier++)
      if (strcmp(name, ctor->fields[earlier].name) == 0)
        return FAIL(c, "REFUSE_NAME", "%s is already a field of this constructor (rule R4)", name);
    if (!check_field(c, fam, &ctor->fields[j], &fields[j], ctor->field_count))
      return 0;
    recursive += fields[j].recursive ? 1u : 0u;
    if (recursive > 1u && m->families[fam].param_count > 0u)
      return FAIL(c, "DOMAIN_FAMILY", "the recursive field %s is the second recursive field of %s; a family with parameters has at most one recursive field in each constructor", name, ctor->name);
  }
  ci->name = ctor->name;
  ci->family = fam;
  ci->fields = fields;
  ci->field_count = (uint32_t)ctor->field_count;
  m->ctor_count++;
  return 1;
}

/* A family is in scope in its own fields, so a field can be recursive. */
static int check_family(Checker *c, const Decl *d) {
  Machine *m = c->m;
  uint32_t fam = m->family_count;
  FamilyInfo *f = &m->families[fam];
  const Core **params = arena_alloc(m->arena, (d->param_count + 1u) * sizeof *params);
  size_t i;
  if (params == NULL)
    return FAIL(c, "OOM", "out of memory");
  for (i = 0; i < d->param_count; i++) {
    uint64_t level = 0;
    params[i] = check_type(c, d->params[i].type, &level);
    if (params[i] == NULL || !push(c, d->params[i].name, here(c, params[i])))
      return 0;
  }
  f->name = d->name;
  f->param_types = params;
  f->param_count = (uint32_t)d->param_count;
  f->first_ctor = m->ctor_count;
  f->ctor_count = (uint32_t)d->ctor_count;
  m->family_count++;
  c->self = fam;
  if (!cell_header(c, d, f))
    return 0;
  for (i = 0; i < d->ctor_count; i++) {
    if (!check_ctor(c, fam, &d->ctors[i]))
      return 0;
  }
  return !has_type_param(f) || cell_generators(c, d, fam);
}

/* The fields of a cocell signature (S2 U6c1, rule R1; intent-lang 79e1898
   cocell_kind): the one observation `A`, a cofield `D A`, or a labelled
   cofield `E → D A` with E closed. */
typedef enum {
  COCELL_OBSERVATION,
  COCELL_COFIELD,
  COCELL_LABELLED
} CocellField;

/* T is the application of the variable of index F to the variable of
   index A. */
static int cocell_app(const Core *t, uint32_t f, uint32_t a) {
  return t->kind == CORE_APP && t->a->kind == CORE_VAR && t->a->index == f && t->b->kind == CORE_VAR && t->b->index == a;
}

/* The kind of FIELD J with the normal type T, in the context D, A, then the
   J earlier fields: A is the variable J and D is the variable J + 1. A
   labelled cofield is an arrow written in the field. */
static int cocell_kind(const Field *field, const Core *t, uint32_t j, CocellField *out) {
  if (t->kind == CORE_VAR && t->index == j) {
    *out = COCELL_OBSERVATION;
    return 1;
  }
  if (cocell_app(t, j + 1u, j)) {
    *out = COCELL_COFIELD;
    return 1;
  }
  if (field->type->kind == TERM_PI && t->kind == CORE_PI && cell_closed(t->a, 0u) && cocell_app(t->b, j + 2u, j + 1u)) {
    *out = COCELL_LABELLED;
    return 1;
  }
  return 0;
}

/* The shape check of `codata D (A : Type n) : Type where` (rule R1): one
   type parameter and each line a kind of CocellField, with exactly one
   observation. Each line is a Ctor with one Field (the parser), so the
   lines are the fields of the one intent-lang clause. The fields are
   checked with D : Type n → Type n and A as locals. The universe of the
   parameter goes to *N, the field kinds to KINDS and the universe of each
   label type to LABELS. The intent-lang and-block, index and clause arms
   are not ported: the leaf grammar has no and block, no index and no
   clause. */
static int cocell_shape(Checker *c, const Decl *d, uint64_t *n, CocellField *kinds, uint64_t *labels) {
  Machine *m = c->m;
  Term *arrow = arena_alloc(m->arena, sizeof *arrow);
  const Core *pt, *pn, *dt;
  uint64_t level = 0;
  uint32_t j, observations = 0;
  if (arrow == NULL)
    return FAIL(c, "OOM", "out of memory");
  if (d->param_count != 1u)
    return FAIL(c, "REFUSE_CELL", "%s is not a cocell signature: it has %u parameters; a cocell signature has one type parameter (rule R1)", d->name, (unsigned)d->param_count);
  pt = check_type(c, d->params[0].type, &level);
  if (pt == NULL)
    return 0;
  pn = quote_value(m, c->level, here(c, pt));
  if (pn == NULL)
    return FAIL(c, "OOM", "out of memory");
  if (pn->kind != CORE_UNIV)
    return FAIL(c, "REFUSE_CELL", "%s is not a cocell signature: the parameter %s is not a universe; a cocell signature has one type parameter (rule R1)", d->name, d->params[0].name);
  *n = pn->nat;
  memset(arrow, 0, sizeof *arrow);
  arrow->kind = TERM_PI;
  arrow->line = d->line;
  arrow->col = d->col;
  arrow->left = d->params[0].type;
  arrow->right = d->params[0].type;
  dt = check_type(c, arrow, &level);
  if (dt == NULL || !push(c, d->name, here(c, dt)) || !push(c, d->params[0].name, here(c, pt)))
    return 0;
  for (j = 0; j < d->ctor_count; j++) {
    const Field *field = &d->ctors[j].fields[0];
    const Core *k = check_type(c, field->type, &level);
    const Core *t = k == NULL ? NULL : quote_value(m, c->level, here(c, k));
    if (k == NULL)
      return 0;
    if (t == NULL)
      return FAIL(c, "OOM", "out of memory");
    if (!cocell_kind(field, t, j, &kinds[j]))
      return FAIL(c, "REFUSE_CELL", "%s is not a cocell signature: the field %s must be the observation %s, the cofield %s %s or a labelled cofield E → %s %s with E closed (rule R1)",
                  d->name, field->name, d->params[0].name, d->name, d->params[0].name, d->name, d->params[0].name);
    labels[j] = 0;
    if (kinds[j] == COCELL_LABELLED && check_type(c, field->type->left, &labels[j]) == NULL)
      return 0;
    if (labels[j] > *n)
      return FAIL(c, "REFUSE_CELL", "%s is not a cocell signature: the label type of %s is in Type %llu, above Type %llu of %s (rule R1)",
                  d->name, field->name, (unsigned long long)labels[j], (unsigned long long)*n, d->params[0].name);
    observations += kinds[j] == COCELL_OBSERVATION;
    if (!push(c, field->name, here(c, k)))
      return 0;
  }
  if (observations != 1u)
    return FAIL(c, "REFUSE_CELL", "%s is not a cocell signature: it has %u observations; a cocell signature has exactly one (rule R1)", d->name, (unsigned)observations);
  for (j = 0; j < d->ctor_count; j++)
    if (!name_free(c, d->ctors[j].name))
      return 0;
  return 1;
}

/* The checker state at the start of the declaration NAME (check_program,
   and each declaration a codata builds, S2 U6c2a). */
static void checker_reset(Checker *c, size_t index, const char *name) {
  c->decl_index = index;
  c->scope = NULL;
  c->env = NULL;
  c->level = 0;
  c->depth = 0;
  c->self = NO_FAMILY;
  c->at = MULT_ZERO;
  c->m->def = name;
}

/* S2 U6c2a (R6 = (b)): an admitted codata builds its declarations as Decl
   and Term values, not as text (intent-lang 79e1898 cocell_text). Each
   built name has a `.`, which no source name has (lexer.c is_ident_char),
   so a built name cannot capture a source name. A built term has the
   source position in G. */
typedef struct {
  Checker *c;
  int line;
  int col;
} Gen;

static const Term *gen_term(Gen *g, TermKind kind, const char *name, uint64_t nat, const Term *left, const Term *right) {
  Term *t = arena_alloc(g->c->m->arena, sizeof *t);
  if (t == NULL)
    return NULL;
  memset(t, 0, sizeof *t);
  t->kind = kind;
  t->line = g->line;
  t->col = g->col;
  t->name = name;
  t->nat = nat;
  t->left = left;
  t->right = right;
  return t;
}

/* The term builders give NULL when a part is NULL (out of memory). */
static const Term *gen_var(Gen *g, const char *name) {
  return name == NULL ? NULL : gen_term(g, TERM_VAR, name, 0, NULL, NULL);
}

static const Term *gen_app(Gen *g, const Term *f, const Term *x) {
  return f == NULL || x == NULL ? NULL : gen_term(g, TERM_APP, NULL, 0, f, x);
}

/* A bare binder: the domain comes from the signature. */
static const Term *gen_lam(Gen *g, const char *name, const Term *body) {
  return body == NULL ? NULL : gen_term(g, TERM_LAM, name, 0, NULL, body);
}

static const Term *gen_pi(Gen *g, const char *name, const Term *a, const Term *b) {
  return a == NULL || b == NULL ? NULL : gen_term(g, TERM_PI, name, 0, a, b);
}

static const Term *gen_univ(Gen *g, uint64_t n) {
  return gen_term(g, TERM_UNIV, NULL, n, NULL, NULL);
}

/* The private name A.B. */
static const char *gen_name(Gen *g, const char *a, const char *b) {
  size_t n = a == NULL || b == NULL ? 0u : strlen(a) + strlen(b) + 2u;
  char *s = n == 0u ? NULL : arena_alloc(g->c->m->arena, n);
  if (s != NULL)
    snprintf(s, n, "%s.%s", a, b);
  return s;
}

/* A domain declaration at G; 0 when a part is NULL. */
static int gen_decl(Gen *g, Decl *out, DeclKind kind, const char *name, const Term *type, const Term *body) {
  memset(out, 0, sizeof *out);
  out->kind = kind;
  out->origin = ORIGIN_DOMAIN;
  out->line = g->line;
  out->col = g->col;
  out->end_line = g->line;
  out->end_col = g->col;
  out->name = name;
  out->type = type;
  out->body = body;
  return name != NULL && type != NULL && (kind == DECL_FAMILY || body != NULL);
}

/* The position constructor D.step.f of the cofield LINE f: a label E for
   a labelled f, then a rest D.pos. A field name is unique over all
   constructors (name_owner), so each field name has the line name. */
static int gen_step(Gen *g, const char *dname, const Ctor *line, CocellField kind, const Term *pos, Ctor *out) {
  Field *fields = arena_alloc(g->c->m->arena, 2u * sizeof *fields);
  size_t r = kind == COCELL_LABELLED ? 1u : 0u;
  if (fields == NULL)
    return 0;
  memset(out, 0, sizeof *out);
  memset(fields, 0, 2u * sizeof *fields);
  out->name = gen_name(g, gen_name(g, dname, "step"), line->name);
  out->fields = fields;
  out->field_count = r + 1u;
  out->line = line->line;
  out->col = line->col;
  fields[r].name = gen_name(g, out->name, "rest");
  fields[r].type = pos;
  fields[r].line = line->line;
  fields[r].col = line->col;
  if (r == 1u) {
    fields[0].name = gen_name(g, out->name, "label");
    fields[0].type = line->fields[0].type->left;
    fields[0].line = line->line;
    fields[0].col = line->col;
  }
  return out->name != NULL && pos != NULL && fields[0].name != NULL && fields[r].name != NULL;
}

/* The destructor of LINE (intent-lang cocell_dtor), named by the line, with
   the type parameter explicit (R3):
     observation  (.A : Type n) → D .A → .A        λ .A .x → .x D.here
     cofield      (.A : Type n) → D .A → D .A      λ .A .x .p → .x (D.step.f .p)
     labelled     (.A : Type n) → D .A → E → D .A  λ .A .x .e .p → .x (D.step.f .e .p) */
static int gen_dtor(Gen *g, const Decl *d, const Ctor *line, CocellField kind, uint64_t n, Decl *out) {
  const Term *a = gen_var(g, ".A");
  const Term *self = gen_app(g, gen_var(g, d->name), a);
  const Term *step = gen_var(g, gen_name(g, gen_name(g, d->name, "step"), line->name));
  const Term *at = gen_app(g, step, gen_var(g, ".p"));
  const Term *res = self;
  const Term *body;
  switch (kind) {
  case COCELL_OBSERVATION:
    res = a;
    at = gen_var(g, gen_name(g, d->name, "here"));
    break;
  case COCELL_COFIELD:
    break;
  case COCELL_LABELLED:
    res = gen_pi(g, NULL, line->fields[0].type->left, self);
    at = gen_app(g, gen_app(g, step, gen_var(g, ".e")), gen_var(g, ".p"));
    break;
  }
  body = gen_app(g, gen_var(g, ".x"), at);
  body = kind == COCELL_OBSERVATION ? body : gen_lam(g, ".p", body);
  body = kind == COCELL_LABELLED ? gen_lam(g, ".e", body) : body;
  return gen_decl(g, out, DECL_DEF, line->name, gen_pi(g, ".A", gen_univ(g, n), gen_pi(g, NULL, self, res)), gen_lam(g, ".A", gen_lam(g, ".x", body)));
}

static int check_def(Checker *c, const Decl *d);

/* One built declaration (intent-lang check_generated): a fresh checker
   state, a free name, then the path of a parsed declaration. */
static int check_built(Checker *c, const Decl *d) {
  checker_reset(c, c->decl_index, d->name);
  if (!name_free(c, d->name))
    return 0;
  return d->kind == DECL_FAMILY ? check_family(c, d) : check_def(c, d);
}

/* The step binders .t.f of the cofields of D around TAIL (intent-lang
   cocell_step): (.t.f : .S → .S), or (.t.f : .S → E → .S) for a labelled
   f. With TYPED 0 they are bare lambda binders. */
static const Term *gen_steps(Gen *g, const Decl *d, const CocellField *kinds, int typed, const Term *tail) {
  const Term *t = tail;
  size_t j = d->ctor_count;
  while (j > 0u) {
    const Ctor *line = &d->ctors[--j];
    const Term *s = gen_var(g, ".S");
    const char *name = gen_name(g, ".t", line->name);
    const Term *step = kinds[j] == COCELL_LABELLED ? gen_pi(g, NULL, s, gen_pi(g, NULL, line->fields[0].type->left, s)) : gen_pi(g, NULL, s, s);
    t = kinds[j] == COCELL_OBSERVATION ? t : typed ? gen_pi(g, name, step, t) : gen_lam(g, name, t);
  }
  return t;
}

/* The walk (intent-lang cocell_walk_case): a fold over D.pos at the motive
   .S → .S, the step first, then the rest of the walk.
     D.walk : (.S : Type n) → steps → D.pos → .S → .S
     D.walk = λ .S steps .p → fold (λ .s → .s) cases .p
   The case of a cofield f is λ .ih .s → .ih (.t.f .s), and of a labelled
   f it is λ .e .ih .s → .ih (.t.f .s .e). */
static int gen_walk(Gen *g, const Decl *d, const CocellField *kinds, uint64_t n, const Term *pos, Decl *out) {
  const Term *s = gen_var(g, ".S");
  const Term *fold = gen_app(g, gen_var(g, "fold"), gen_lam(g, ".s", gen_var(g, ".s")));
  size_t j;
  for (j = 0; j < d->ctor_count; j++) {
    const Term *step = gen_app(g, gen_var(g, gen_name(g, ".t", d->ctors[j].name)), gen_var(g, ".s"));
    const Term *body = gen_app(g, gen_var(g, ".ih"), kinds[j] == COCELL_LABELLED ? gen_app(g, step, gen_var(g, ".e")) : step);
    const Term *fcase = gen_lam(g, ".ih", gen_lam(g, ".s", body));
    fcase = kinds[j] == COCELL_LABELLED ? gen_lam(g, ".e", fcase) : fcase;
    fold = kinds[j] == COCELL_OBSERVATION ? fold : gen_app(g, fold, fcase);
  }
  fold = gen_app(g, fold, gen_var(g, ".p"));
  return gen_decl(g, out, DECL_DEF, gen_name(g, d->name, "walk"),
                  gen_pi(g, ".S", gen_univ(g, n), gen_steps(g, d, kinds, 1, gen_pi(g, NULL, pos, gen_pi(g, NULL, s, s)))),
                  gen_lam(g, ".S", gen_steps(g, d, kinds, 0, gen_lam(g, ".p", fold))));
}

/* The corecursor (intent-lang cocell_text), private by Q-S2-U6c2-1 (a):
     D.corec : (.A : Type n) → (.S : Type n) → (.o : .S → .A) → steps → .S → D .A
     D.corec = λ .A .S .o steps .seed .p → .o (D.walk .S .t.f ... .p .seed) */
static int gen_corec(Gen *g, const Decl *d, const CocellField *kinds, uint64_t n, Decl *out) {
  const Term *s = gen_var(g, ".S");
  const Term *a = gen_var(g, ".A");
  const Term *walk = gen_app(g, gen_var(g, gen_name(g, d->name, "walk")), s);
  size_t j;
  for (j = 0; j < d->ctor_count; j++)
    walk = kinds[j] == COCELL_OBSERVATION ? walk : gen_app(g, walk, gen_var(g, gen_name(g, ".t", d->ctors[j].name)));
  walk = gen_app(g, gen_app(g, walk, gen_var(g, ".p")), gen_var(g, ".seed"));
  return gen_decl(g, out, DECL_DEF, gen_name(g, d->name, "corec"),
                  gen_pi(g, ".A", gen_univ(g, n), gen_pi(g, ".S", gen_univ(g, n), gen_pi(g, ".o", gen_pi(g, NULL, s, a), gen_steps(g, d, kinds, 1, gen_pi(g, NULL, s, gen_app(g, gen_var(g, d->name), a)))))),
                  gen_lam(g, ".A", gen_lam(g, ".S", gen_lam(g, ".o", gen_steps(g, d, kinds, 0, gen_lam(g, ".seed", gen_lam(g, ".p", gen_app(g, gen_var(g, ".o"), walk))))))));
}

/* A codata declaration (S2 U6c2a, rule R1): the shape check, then the
   declarations it builds (S2-HANDOFF U6c2 notes): the family D.pos of
   positions (D.here, and D.step.f for each cofield f) in the highest label
   universe, the def D = λ .A → D.pos → .A, one destructor a line, the
   walk D.walk and the corecursor D.corec (S2 U6c2b). The bisimulation
   needs a dependent elim over D.pos, which leaf does not have (S2-HANDOFF
   R7), so it is deferred. */
static int check_cofamily(Checker *c, const Decl *d) {
  Machine *m = c->m;
  CocellField *kinds = arena_alloc(m->arena, (d->ctor_count + 1u) * sizeof *kinds);
  uint64_t *labels = arena_alloc(m->arena, (d->ctor_count + 1u) * sizeof *labels);
  Ctor *steps = arena_alloc(m->arena, (d->ctor_count + 1u) * sizeof *steps);
  Decl built;
  Gen g;
  const Term *pos;
  uint64_t n = 0;
  uint64_t k = 0;
  size_t j;
  size_t count = 1;
  if (kinds == NULL || labels == NULL || steps == NULL)
    return FAIL(c, "OOM", "out of memory");
  if (!cocell_shape(c, d, &n, kinds, labels))
    return 0;
  g.c = c;
  g.line = d->line;
  g.col = d->col;
  pos = gen_var(&g, gen_name(&g, d->name, "pos"));
  memset(&steps[0], 0, sizeof steps[0]);
  steps[0].name = gen_name(&g, d->name, "here");
  steps[0].line = d->line;
  steps[0].col = d->col;
  for (j = 0; j < d->ctor_count; j++) {
    k = labels[j] > k ? labels[j] : k;
    if (kinds[j] != COCELL_OBSERVATION && !gen_step(&g, d->name, &d->ctors[j], kinds[j], pos, &steps[count++]))
      return FAIL(c, "OOM", "out of memory");
  }
  if (steps[0].name == NULL || !gen_decl(&g, &built, DECL_FAMILY, pos == NULL ? NULL : pos->name, gen_univ(&g, k), NULL))
    return FAIL(c, "OOM", "out of memory");
  built.ctors = steps;
  built.ctor_count = count;
  if (!check_built(c, &built))
    return 0;
  if (!gen_decl(&g, &built, DECL_DEF, d->name, gen_pi(&g, NULL, gen_univ(&g, n), gen_univ(&g, n)), gen_lam(&g, ".A", gen_pi(&g, NULL, pos, gen_var(&g, ".A")))))
    return FAIL(c, "OOM", "out of memory");
  if (!check_built(c, &built))
    return 0;
  for (j = 0; j < d->ctor_count; j++) {
    g.line = d->ctors[j].line;
    g.col = d->ctors[j].col;
    if (!gen_dtor(&g, d, &d->ctors[j], kinds[j], n, &built))
      return FAIL(c, "OOM", "out of memory");
    if (!check_built(c, &built))
      return 0;
  }
  g.line = d->line;
  g.col = d->col;
  if (!gen_walk(&g, d, kinds, n, pos, &built))
    return FAIL(c, "OOM", "out of memory");
  if (!check_built(c, &built))
    return 0;
  if (!gen_corec(&g, d, kinds, n, &built))
    return FAIL(c, "OOM", "out of memory");
  return check_built(c, &built);
}

/* 1 when the type V, after its Pi binders, is a universe, Path or PathP (a
   type, a law or a proof): its definition is checked at 0 (Q-S3-5 (a)). */
static int erased_type(Checker *c, const Value *v) {
  uint32_t level = c->level;
  while (v != NULL && v->kind == VAL_PI) {
    v = closure_apply(c->m, v, val_var(c->m, level));
    level++;
  }
  return v != NULL && (v->kind == VAL_UNIV || val_is(v, OP_PATHP));
}

static int check_def(Checker *c, const Decl *d) {
  Machine *m = c->m;
  uint64_t level = 0;
  /* A Pi over I (a pretype) is a legal signature, as in Cubical Agda: a
     line, or a family of partial elements. A bare pretype is not. */
  const Core *t = check_type_at(c, d->type, &level, d->type->kind == TERM_PI);
  const Value *tv = here(c, t);
  const Core *body = check_at(c, erased_type(c, tv) ? MULT_ZERO : MULT_ONE, d->body, tv);
  DefInfo *info = &m->defs[m->def_count];
  if (body == NULL)
    return 0;
  info->name = d->name;
  info->origin = d->origin;
  info->body = body;
  info->type = tv;
  info->value = NULL;
  info->runtime_refusal = NULL;
  if (erased_type(c, tv)) {
    Checker probe = *c;
    Machine runtime = *m;
    Diag diag;
    diag_init(&diag);
    runtime.diag = &diag;
    probe.m = &runtime;
    /* An erased law may use its inputs freely. Record whether that same
       body also checks at 1 before allowing an executable reference. */
    if (check_at(&probe, MULT_ONE, d->body, tv) == NULL)
      info->runtime_refusal = diag.code != NULL ? diag.code : "REFUSE_AFFINE_REUSE";
  }
  m->def_count++;
  return 1;
}

static int check_decl(Checker *c, const Decl *d) {
  if (d->kind == DECL_FAMILY && d->origin == ORIGIN_PROGRAM)
    return FAIL(c, "REFUSE_DATA", "a program cannot declare a data type (rule R1); only domain/domain.lang can");
  if (d->kind == DECL_COFAMILY && d->origin == ORIGIN_PROGRAM)
    return FAIL(c, "REFUSE_DATA", "a program cannot declare a codata type (rule R1); only domain/domain.lang can");
  if (d->kind == DECL_AXIOM)
    return FAIL(c, "REFUSE_AXIOM", "an axiom has no proof (rule R2)");
  if (d->is_rec)
    return FAIL(c, "REFUSE_REC", "a rec declaration is refused (rule R3); use fold");
  if (!name_free(c, d->name))
    return 0;
  switch (d->kind) {
  case DECL_FAMILY:
    return check_family(c, d);
  case DECL_COFAMILY:
    return check_cofamily(c, d);
  case DECL_DEF:
  case DECL_AXIOM:
    break;
  }
  return check_def(c, d);
}

int check_program(Arena *arena, const DeclList *decls, Machine *m, Diag *diag) {
  Checker c;
  size_t ctors = 0;
  size_t built = 0;
  size_t i;
  memset(m, 0, sizeof *m);
  c.face = NULL;
  m->arena = arena;
  m->diag = diag;
  m->fuel = EVAL_FUEL_STEPS;
  /* A codata with F lines builds at most 2F + 8 declarations and F + 1
     position constructors (S2 U6c2, intent-lang 79e1898 4555..4562). */
  for (i = 0; i < decls->count; i++) {
    size_t f = decls->items[i].ctor_count;
    int co = decls->items[i].kind == DECL_COFAMILY;
    ctors += co ? 2u * f + 1u : f;
    built += co ? 2u * f + 8u : 0u;
  }
  m->defs = arena_alloc(arena, (decls->count + built + 1u) * sizeof *m->defs);
  m->families = arena_alloc(arena, (decls->count + built + 1u) * sizeof *m->families);
  m->ctors = arena_alloc(arena, (ctors + 1u) * sizeof *m->ctors);
  if (m->defs == NULL || m->families == NULL || m->ctors == NULL)
    return diag_fail(diag, "OOM", NULL, "out of memory");
  m->def_cap = (uint32_t)(decls->count + built);
  m->family_cap = (uint32_t)(decls->count + built);
  m->ctor_cap = (uint32_t)ctors;
  memset(&c, 0, sizeof c);
  c.m = m;
  c.decls = decls;
  for (i = 0; i < decls->count; i++) {
    checker_reset(&c, i, decls->items[i].name);
    m->checked = i;
    m->decl_failed = 1;
    if (!check_decl(&c, &decls->items[i]))
      return 0;
  }
  m->def = NULL;
  m->checked = decls->count;
  m->decl_failed = 0;
  return 1;
}

int entry_of(Machine *m, const Value *type, Entry *out) {
  const Value *t = type;
  memset(out, 0, sizeof *out);
  while (t != NULL && t->kind == VAL_PI && out->param_count < ENTRY_PARAMS_MAX) {
    if (!val_is(t->dom, OP_NAT) && !val_is(t->dom, OP_BOOL))
      return 0;
    out->param_bool[out->param_count] = val_is(t->dom, OP_BOOL);
    t = closure_apply(m, t, val_var(m, out->param_count));
    out->param_count++;
  }
  out->result_bool = val_is(t, OP_BOOL);
  return val_is(t, OP_NAT) || val_is(t, OP_BOOL);
}

static int parse_u64(const char *s, uint64_t *out) {
  uint64_t n = 0;
  size_t i;
  if (s[0] == '\0')
    return 0;
  for (i = 0; s[i] != '\0'; i++) {
    unsigned d = (unsigned)(unsigned char)s[i] - (unsigned)'0';
    if (d > 9u || n > (UINT64_MAX - d) / 10u)
      return 0;
    n = n * 10u + d;
  }
  *out = n;
  return 1;
}

/* An entry argument: a Nat in decimal, or a Bool as 0 or 1. */
static const Value *entry_arg(Machine *m, const char *text, int is_bool) {
  uint64_t n = 0;
  if (!parse_u64(text, &n) || (is_bool && n > 1u)) {
    diag_fail(m->diag, "EVAL_ARGS", m->def, "'%s' is not a %s", text, is_bool ? "Bool (0 or 1)" : "Nat (0 to 2^64-1)");
    return NULL;
  }
  return is_bool ? val_make(m, n == 1u ? OP_TRUE : OP_FALSE, 0, 0, NULL, NULL, NULL) : val_nat(m, n);
}

static int print_result(Machine *m, const char *name, const Value *v, FILE *out, FILE *err) {
  switch (v->kind) {
  case VAL_NAT:
    fprintf(out, "%llu\n", (unsigned long long)v->nat);
    return 0;
  case VAL_TRAP:
    fputs("trap\n", out);
    fprintf(err, "langc: EVAL_OVERFLOW: %s: a Nat operation overflowed past 2^64-1\n", name);
    return 0;
  case VAL_OP:
    if (val_is(v, OP_TRUE) || val_is(v, OP_FALSE)) {
      fprintf(out, "%d\n", val_is(v, OP_TRUE));
      return 0;
    }
    break;
  case VAL_UNIV:
  case VAL_LAM:
  case VAL_PI:
  case VAL_SIGMA:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  diag_fail(m->diag, "EVAL_ENTRY", name, "the entry did not compute a number");
  return 1;
}

int eval_command(Machine *m, const char *name, char *const *args, int arg_count, FILE *out, FILE *err) {
  uint32_t d = find_def(m, name);
  Entry entry;
  const Value *v;
  char *buf;
  uint32_t i;
  m->def = name;
  if (d == NO_DEF) {
    diag_fail(m->diag, "EVAL_ENTRY", name, "no definition has this name");
    return 1;
  }
  v = def_value(m, d);
  if (v == NULL)
    return 1;
  if (!entry_of(m, m->defs[d].type, &entry) && arg_count != 0) {
    diag_fail(m->diag, "EVAL_ARGS", name, "the definition is not an entry, so it takes no arguments");
    return 2;
  }
  if (!entry_of(m, m->defs[d].type, &entry)) {
    buf = arena_alloc(m->arena, PRINT_MAX);
    if (buf == NULL)
      return diag_fail(m->diag, "OOM", name, "out of memory") + 1;
    if (!value_print(m, NULL, 0, v, buf, PRINT_MAX)) {
      diag_fail(m->diag, "EVAL_PRINT", name, "the normal form exceeds the printer limits");
      return 1;
    }
    fprintf(out, "%s\n", buf);
    return 0;
  }
  if ((uint32_t)arg_count != entry.param_count) {
    diag_fail(m->diag, "EVAL_ARGS", name, "the entry takes %u arguments, found %d", entry.param_count, arg_count);
    return 2;
  }
  for (i = 0; i < entry.param_count && v != NULL; i++) {
    const Value *arg = entry_arg(m, args[i], entry.param_bool[i]);
    v = arg == NULL ? NULL : apply_value(m, v, arg);
  }
  if (v == NULL)
    return strcmp(m->diag->code != NULL ? m->diag->code : "", "EVAL_ARGS") == 0 ? 2 : 1;
  return print_result(m, name, v, out, err);
}
