/* The evaluator, the conversion test, quote and the printer (see
   front/core.h). SHARED by the tcc kits. */
#include "front/core.h"

#include <string.h>

#define PRINT_DEPTH_MAX 200u
#define PRINT_NAMES_MAX PRINT_DEPTH_MAX
#define VARIES 0xffu

static const char *const OP_NAMES[] = {
  [OP_NAT] = "Nat", [OP_BOOL] = "Bool", [OP_UNIT] = "Unit", [OP_LEAF] = "Leaf",
  [OP_OPTION] = "Option", [OP_FAMILY] = "?",
  [OP_UNIT_VAL] = "unit", [OP_TRUE] = "true", [OP_FALSE] = "false",
  [OP_NONE] = "none", [OP_SOME] = "some",
  [OP_PACK] = "pack", [OP_CTOR] = "?",
  [OP_OPTION_ELIM] = "option", [OP_PURE] = "pure",
  [OP_MAP] = "map", [OP_BIND] = "bind", [OP_FOLD_NAT] = "fold",
  [OP_FOLD_FAMILY] = "fold", [OP_WITNESS] = "witness", [OP_PAYLOAD] = "payload",
  [OP_NAT_ADD] = "natAdd", [OP_NAT_SUB] = "natSub",
  [OP_NAT_MUL] = "natMul", [OP_NAT_EQ] = "natEq", [OP_NAT_LE] = "natLe",
  [OP_FLAG_IF] = "flagIf", [OP_PROJ] = "?", [OP_SSET] = "SSet", [OP_I] = "I",
  [OP_I0] = "i0", [OP_I1] = "i1", [OP_PATHP] = "PathP", [OP_PLAM] = "?",
  [OP_PAPP] = "?", [OP_INEG] = "~", [OP_IMIN] = "∧", [OP_IMAX] = "∨",
  [OP_PARTIAL] = "Partial", [OP_SYSTEM] = "↦", [OP_SYSNIL] = "[]",
  [OP_FROMPARTIAL] = "fromPartial",
  [OP_TRANSP] = "transp", [OP_TPART] = "%tpart", [OP_TCOD] = "%tcod",
  [OP_HCOMP] = "hcomp", [OP_SYSMAP] = "%sysmap", [OP_GLUE] = "Glue",
  [OP_GLUE_ELEM] = "glue", [OP_UNGLUE] = "unglue"
};

/* The number of arguments of each operation. VARIES: it comes from the
   family tables. */
static const unsigned char OP_ARITY[] = {
  [OP_NAT] = 0, [OP_BOOL] = 0, [OP_UNIT] = 0, [OP_OPTION] = 1,
  [OP_FAMILY] = VARIES, [OP_UNIT_VAL] = 0, [OP_TRUE] = 0,
  [OP_FALSE] = 0, [OP_NONE] = 0, [OP_SOME] = 1,
  [OP_PACK] = 2, [OP_CTOR] = VARIES,
  [OP_OPTION_ELIM] = 3, [OP_PURE] = 1, [OP_MAP] = 2, [OP_BIND] = 2,
  [OP_FOLD_NAT] = 3, [OP_FOLD_FAMILY] = VARIES, [OP_WITNESS] = 1,
  [OP_PAYLOAD] = 1, [OP_NAT_ADD] = 2, [OP_NAT_SUB] = 2, [OP_NAT_MUL] = 2,
  [OP_NAT_EQ] = 2, [OP_NAT_LE] = 2, [OP_FLAG_IF] = 3, [OP_PROJ] = 1,
  [OP_SSET] = 0, [OP_I] = 0, [OP_I0] = 0, [OP_I1] = 0, [OP_PATHP] = 3,
  [OP_PLAM] = 1, [OP_PAPP] = 4, [OP_INEG] = 1, [OP_IMIN] = 2, [OP_IMAX] = 2,
  [OP_PARTIAL] = 2, [OP_SYSTEM] = 3, [OP_SYSNIL] = 0, [OP_FROMPARTIAL] = 1,
  [OP_TRANSP] = 3, [OP_TPART] = 1, [OP_TCOD] = 2,
  [OP_HCOMP] = 4, [OP_SYSMAP] = 2, [OP_GLUE] = 3, [OP_GLUE_ELEM] = 3,
  [OP_UNGLUE] = 4
};

typedef enum {
  SCRUT_VALUE,
  SCRUT_TRAP,
  SCRUT_NEUTRAL
} Scrut;

static const Value *oom(Machine *m) {
  diag_fail(m->diag, "OOM", m->def, "the arena limit is reached");
  return NULL;
}

static const Value *internal(Machine *m, const char *what) {
  diag_fail(m->diag, "INTERNAL", m->def, "the evaluator found %s", what);
  return NULL;
}

static int spend(Machine *m, uint64_t steps) {
  if (m->fuel >= steps) {
    m->fuel -= steps;
    return 1;
  }
  m->fuel = 0;
  return diag_fail(m->diag, "EVAL_FUEL", m->def, "the evaluation takes more than %u steps", EVAL_FUEL_STEPS);
}

static int enter(Machine *m) {
  if (m->depth < EVAL_DEPTH_LIMIT) {
    m->depth++;
    return 1;
  }
  return diag_fail(m->diag, "EVAL_DEPTH", m->def, "the evaluation nests deeper than %u levels", EVAL_DEPTH_LIMIT);
}

static Value *new_value(Machine *m, ValKind kind) {
  Value *v = arena_alloc(m->arena, sizeof *v);
  if (v == NULL) {
    oom(m);
    return NULL;
  }
  v->kind = kind;
  return v;
}

const Value *val_nat(Machine *m, uint64_t n) {
  Value *v = new_value(m, VAL_NAT);
  if (v != NULL)
    v->nat = n;
  return v;
}

static const Value *val_trap(Machine *m) {
  return new_value(m, VAL_TRAP);
}

static const Value *overflow(Machine *m) {
  m->overflowed = 1;
  return val_trap(m);
}

const Value *val_var(Machine *m, uint32_t level) {
  Value *v = new_value(m, VAL_VAR);
  if (level >= m->fresh_level)
    m->fresh_level = level + 1u;
  if (v != NULL)
    v->nat = level;
  return v;
}

const Value *val_univ(Machine *m, uint64_t level) {
  Value *v = new_value(m, VAL_UNIV);
  if (v != NULL)
    v->nat = level;
  return v;
}

const Value *val_op(Machine *m, ValKind kind, Op op, uint32_t inst, uint32_t field, const Value *const *args, uint32_t argc) {
  const Value **copy;
  Value *v;
  uint32_t i;
  for (i = 0; i < argc; i++)
    if (args[i] == NULL)
      return NULL;
  copy = arena_alloc(m->arena, ((size_t)argc + 1u) * sizeof *copy);
  if (copy == NULL)
    return oom(m);
  v = new_value(m, kind);
  if (v == NULL)
    return NULL;
  for (i = 0; i < argc; i++)
    copy[i] = args[i];
  v->op = op;
  v->inst = inst;
  v->field = field;
  v->args = copy;
  v->argc = argc;
  return v;
}

static const Value *make(Machine *m, ValKind kind, Op op, uint32_t inst, uint32_t argc, const Value *a, const Value *b, const Value *c) {
  const Value *args[3];
  args[0] = a;
  args[1] = b;
  args[2] = c;
  return val_op(m, kind, op, inst, 0, args, argc);
}

const Value *val_make(Machine *m, Op op, uint32_t inst, uint32_t argc, const Value *a, const Value *b, const Value *c) {
  return make(m, VAL_OP, op, inst, argc, a, b, c);
}

static const Value *constant(Machine *m, Op op) {
  return make(m, VAL_OP, op, 0, 0, NULL, NULL, NULL);
}

static const Value *flag(Machine *m, int yes) {
  return constant(m, yes ? OP_TRUE : OP_FALSE);
}

const Value *val_arrow_mult(Machine *m, Mult mult, const Value *dom, const Value *cod) {
  Value *v;
  if (dom == NULL || cod == NULL)
    return NULL;
  v = new_value(m, VAL_PI);
  if (v == NULL)
    return NULL;
  v->dom = dom;
  v->arg = cod;
  v->mult = mult;
  return v;
}

const Value *val_arrow(Machine *m, const Value *dom, const Value *cod) {
  return val_arrow_mult(m, MULT_OMEGA, dom, cod);
}

const Value *val_binder(Machine *m, ValKind kind, Mult mult, const char *name, const Value *dom, const Env *env, const Core *body) {
  Value *v;
  if (body == NULL || (dom == NULL && kind != VAL_LAM))
    return NULL;
  v = new_value(m, kind);
  if (v == NULL)
    return NULL;
  v->name = name;
  v->mult = mult;
  v->dom = dom;
  v->env = env;
  v->body = body;
  return v;
}

const Value *val_closure(Machine *m, ValKind kind, const char *name, const Value *dom, const Env *env, const Core *body) {
  return val_binder(m, kind, MULT_OMEGA, name, dom, env, body);
}

/* The binder mark of the printer: the same rule as fmt (Q-S3-1 (a)). */
static const char *mult_mark(Mult mult) {
  switch (mult) {
  case MULT_OMEGA:
    return "";
  case MULT_ZERO:
    return "@0 ";
  case MULT_ONE:
    return "@1 ";
  }
  return "";
}

int val_is(const Value *v, Op op) {
  return v != NULL && v->kind == VAL_OP && v->op == op;
}

const Env *env_push(Machine *m, const Env *env, const Value *value) {
  Env *e;
  if (value == NULL)
    return NULL;
  e = arena_alloc(m->arena, sizeof *e);
  if (e == NULL) {
    oom(m);
    return NULL;
  }
  e->value = value;
  e->next = env;
  return e;
}

static const Value *env_lookup(Machine *m, const Env *env, uint32_t index) {
  uint32_t i;
  for (i = 0; i < index && env != NULL; i++)
    env = env->next;
  if (env == NULL)
    return internal(m, "a variable outside its scope");
  return env->value;
}

const Value *def_value(Machine *m, uint32_t index) {
  DefInfo *d;
  if (index >= m->def_count)
    return internal(m, "an unknown definition");
  d = &m->defs[index];
  if (d->value == NULL)
    d->value = eval_core(m, NULL, d->body);
  return d->value;
}

static Scrut scrut_of(const Value *v) {
  switch (v->kind) {
  case VAL_TRAP:
    return SCRUT_TRAP;
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    return SCRUT_NEUTRAL;
  case VAL_NAT:
  case VAL_UNIV:
  case VAL_LAM:
  case VAL_PI:
  case VAL_SIGMA:
  case VAL_OP:
    return SCRUT_VALUE;
  }
  return SCRUT_VALUE;
}

/* 1 when the scrutinee S stops the operation: a trap gives the trap, a
   neutral S gives the stuck operation. */
static int blocked(Machine *m, const Value *s, Op op, uint32_t inst, uint32_t field, const Value *const *a, uint32_t n, const Value **out) {
  switch (scrut_of(s)) {
  case SCRUT_TRAP:
    *out = s;
    return 1;
  case SCRUT_NEUTRAL:
    *out = val_op(m, VAL_STUCK, op, inst, field, a, n);
    return 1;
  case SCRUT_VALUE:
    return 0;
  }
  return 0;
}

const Value *apply_value(Machine *m, const Value *fn, const Value *arg) {
  Value *v;
  if (fn == NULL || arg == NULL)
    return NULL;
  switch (fn->kind) {
  case VAL_LAM:
    return closure_apply(m, fn, arg);
  case VAL_TRAP:
    return fn;
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    v = new_value(m, VAL_APP);
    if (v == NULL)
      return NULL;
    v->dom = fn;
    v->arg = arg;
    return v;
  case VAL_NAT:
  case VAL_UNIV:
  case VAL_PI:
  case VAL_SIGMA:
  case VAL_OP:
    break;
  }
  return internal(m, "an application of a value that is not a function");
}

const Value *closure_apply(Machine *m, const Value *binder, const Value *arg) {
  const Env *env;
  if (binder == NULL || arg == NULL)
    return NULL;
  if (binder->body == NULL)
    return binder->arg;
  env = env_push(m, binder->env, arg);
  if (env == NULL)
    return NULL;
  return eval_core(m, env, binder->body);
}

static const Value *reduce_part(Machine *m, Op op, const Value *const *a, uint32_t n, Op ctor, uint32_t index) {
  const Value *out;
  if (blocked(m, a[0], op, 0, 0, a, n, &out))
    return out;
  if (!val_is(a[0], ctor))
    return internal(m, "a projection of a value of the wrong type");
  return a[0]->args[index];
}

static const Value *reduce_option(Machine *m, const Value *const *a, uint32_t n) {
  const Value *out;
  if (blocked(m, a[2], OP_OPTION_ELIM, 0, 0, a, n, &out))
    return out;
  if (val_is(a[2], OP_NONE))
    return a[0];
  if (val_is(a[2], OP_SOME))
    return apply_value(m, a[1], a[2]->args[0]);
  return internal(m, "an option of a value that is not an Option");
}

static const Value *reduce_pure(Machine *m, uint32_t inst, const Value *x) {
  switch ((Carrier)inst) {
  case CARRIER_OPTION:
    return make(m, VAL_OP, OP_SOME, 0, 1, x, NULL, NULL);
  }
  return internal(m, "an unknown carrier");
}

static const Value *reduce_map(Machine *m, uint32_t inst, const Value *const *a, uint32_t n) {
  const Value *x = a[1];
  const Value *out;
  if (blocked(m, x, OP_MAP, inst, 0, a, n, &out))
    return out;
  switch ((Carrier)inst) {
  case CARRIER_OPTION:
    return val_is(x, OP_SOME) ? make(m, VAL_OP, OP_SOME, 0, 1, apply_value(m, a[0], x->args[0]), NULL, NULL) : x;
  }
  return internal(m, "an unknown carrier");
}

static const Value *reduce_bind(Machine *m, uint32_t inst, const Value *const *a, uint32_t n) {
  const Value *x = a[0];
  const Value *out;
  if (blocked(m, x, OP_BIND, inst, 0, a, n, &out))
    return out;
  switch ((Carrier)inst) {
  case CARRIER_OPTION:
    return val_is(x, OP_SOME) ? apply_value(m, a[1], x->args[0]) : x;
  }
  return internal(m, "an unknown carrier");
}

/* fold step z n: step applied n times to z. */
static const Value *reduce_fold_nat(Machine *m, const Value *const *a, uint32_t n) {
  const Value *acc = a[1];
  const Value *out;
  uint64_t k;
  if (blocked(m, a[2], OP_FOLD_NAT, 0, 0, a, n, &out))
    return out;
  if (a[2]->kind != VAL_NAT)
    return internal(m, "a fold of a value that is not a Nat");
  for (k = 0; k < a[2]->nat && acc != NULL; k++)
    acc = spend(m, 1) ? apply_value(m, a[0], acc) : NULL;
  return acc;
}

static const Value *stuck_fold(Machine *m, uint32_t family, const Value *const *cases, uint32_t ncases, const Value *x) {
  const Value **args = arena_alloc(m->arena, ((size_t)ncases + 1u) * sizeof *args);
  uint32_t i;
  if (args == NULL)
    return oom(m);
  for (i = 0; i < ncases; i++)
    args[i] = cases[i];
  args[ncases] = x;
  return val_op(m, VAL_STUCK, OP_FOLD_FAMILY, family, 0, args, ncases + 1u);
}

/* fold case... x over a domain family: the case of the constructor of X,
   applied to its fields. A recursive field arrives folded. */
static const Value *fold_family(Machine *m, uint32_t family, const Value *const *cases, uint32_t ncases, const Value *x) {
  const FamilyInfo *fam;
  const CtorInfo *ctor;
  const Value *acc;
  const Value *field;
  uint32_t i;
  if (x == NULL)
    return NULL;
  switch (scrut_of(x)) {
  case SCRUT_TRAP:
    return x;
  case SCRUT_NEUTRAL:
    return stuck_fold(m, family, cases, ncases, x);
  case SCRUT_VALUE:
    break;
  }
  if (!val_is(x, OP_CTOR) || x->inst >= m->ctor_count || family >= m->family_count)
    return internal(m, "a fold of a value that is not in the family");
  fam = &m->families[family];
  ctor = &m->ctors[x->inst];
  if (x->inst < fam->first_ctor || x->inst - fam->first_ctor >= ncases || x->argc != fam->param_count + ctor->field_count)
    return internal(m, "a fold of a constructor of another family");
  if (!enter(m))
    return NULL;
  acc = cases[x->inst - fam->first_ctor];
  for (i = 0; i < ctor->field_count && acc != NULL; i++) {
    field = x->args[fam->param_count + i];
    acc = apply_value(m, acc, ctor->fields[i].recursive ? fold_family(m, family, cases, ncases, field) : field);
  }
  m->depth--;
  return acc;
}

/* p r, then the ends a[2] and a[3] of the type of p. A neutral p and a
   neutral r give the stuck application. */
static const Value *reduce_papp(Machine *m, const Value *const *a, uint32_t n) {
  if (a[0]->kind == VAL_TRAP || a[1]->kind == VAL_TRAP)
    return a[0]->kind == VAL_TRAP ? a[0] : a[1];
  if (val_is(a[1], OP_I0))
    return a[2];
  if (val_is(a[1], OP_I1))
    return a[3];
  if (val_is(a[0], OP_PLAM))
    return apply_value(m, a[0]->args[0], a[1]);
  if (scrut_of(a[0]) == SCRUT_NEUTRAL && scrut_of(a[1]) == SCRUT_NEUTRAL)
    return val_op(m, VAL_STUCK, OP_PAPP, 0, 0, a, n);
  return internal(m, "a path application on a value that is not a path");
}

/* The interval (former C1): ~ r, r ∧ s and r ∨ s compute to a normal form,
   a join of meets of literals. Literal 2k is the level k and 2k + 1 is
   ~ k. A neutral interval term that is not a variable (an application) is
   an atom; atom i gets the level DNF_ATOM_BASE + i, above every variable.
   A term outside the bounds stays stuck. */
#define DNF_CONJ_MAX FACE_MEETS_MAX
#define DNF_LIT_MAX FACE_LITS_MAX
#define DNF_ATOM_MAX 16u
#define DNF_ATOM_BASE 0x40000000u

typedef struct {
  uint32_t lit[DNF_CONJ_MAX][DNF_LIT_MAX];
  uint32_t len[DNF_CONJ_MAX];
  uint32_t count;
} Dnf;

/* The atom table is local: the atoms of one test share it. LEVEL is the
   next free level for the conversion test of two atoms. */
typedef struct {
  Machine *m;
  uint32_t level;
  const Value *atoms[DNF_ATOM_MAX];
  uint32_t count;
} DnfCtx;

static int interval_form(const Value *v) {
  return v != NULL && (v->kind == VAL_OP || v->kind == VAL_STUCK) && (v->op == OP_INEG || v->op == OP_IMIN || v->op == OP_IMAX);
}

/* Adds the meet of the literals X and Y to D. */
static int dnf_conj(Dnf *d, const uint32_t *x, uint32_t nx, const uint32_t *y, uint32_t ny) {
  uint32_t i;
  if (d->count >= DNF_CONJ_MAX || nx + ny > DNF_LIT_MAX)
    return 0;
  for (i = 0; i < nx; i++)
    d->lit[d->count][i] = x[i];
  for (i = 0; i < ny; i++)
    d->lit[d->count][nx + i] = y[i];
  d->len[d->count] = nx + ny;
  d->count++;
  return 1;
}

static int dnf_meet(const Dnf *x, const Dnf *y, Dnf *out) {
  uint32_t i;
  uint32_t j;
  out->count = 0;
  for (i = 0; i < x->count; i++)
    for (j = 0; j < y->count; j++)
      if (!dnf_conj(out, x->lit[i], x->len[i], y->lit[j], y->len[j]))
        return 0;
  return 1;
}

static int dnf_join(const Dnf *x, const Dnf *y, Dnf *out) {
  uint32_t i;
  *out = *x;
  for (i = 0; i < y->count; i++)
    if (!dnf_conj(out, y->lit[i], y->len[i], NULL, 0))
      return 0;
  return 1;
}

/* ~ (c1 ∨ c2) = ~ c1 ∧ ~ c2 and ~ (l1 ∧ l2) = ~ l1 ∨ ~ l2. */
static int dnf_neg(const Dnf *x, Dnf *out) {
  Dnf acc;
  Dnf one;
  uint32_t i;
  uint32_t j;
  uint32_t lit;
  acc.count = 0;
  if (!dnf_conj(&acc, NULL, 0, NULL, 0))
    return 0;
  for (i = 0; i < x->count; i++) {
    one.count = 0;
    for (j = 0; j < x->len[i]; j++) {
      lit = x->lit[i][j] ^ 1u;
      if (!dnf_conj(&one, &lit, 1, NULL, 0))
        return 0;
    }
    if (!dnf_meet(&acc, &one, out))
      return 0;
    acc = *out;
  }
  *out = acc;
  return 1;
}

/* 0 when V is not an interval value inside the bounds. */
static int dnf_of(DnfCtx *ctx, const Value *v, Dnf *d) {
  Dnf x;
  Dnf y;
  uint32_t lit;
  uint32_t i;
  d->count = 0;
  if (v == NULL)
    return 0;
  if (v->kind == VAL_VAR && v->nat < DNF_ATOM_BASE) {
    lit = (uint32_t)v->nat * 2u;
    return dnf_conj(d, &lit, 1, NULL, 0);
  }
  if (val_is(v, OP_I0))
    return 1;
  if (val_is(v, OP_I1))
    return dnf_conj(d, NULL, 0, NULL, 0);
  if (interval_form(v) && v->op == OP_INEG)
    return dnf_of(ctx, v->args[0], &x) && dnf_neg(&x, d);
  if (interval_form(v))
    return dnf_of(ctx, v->args[0], &x) && dnf_of(ctx, v->args[1], &y)
        && (v->op == OP_IMIN ? dnf_meet(&x, &y, d) : dnf_join(&x, &y, d));
  if (v->kind == VAL_VAR || scrut_of(v) != SCRUT_NEUTRAL)
    return 0;
  for (i = 0; i < ctx->count; i++)
    if (ctx->atoms[i] == v || conv_values(ctx->m, ctx->level, v, ctx->atoms[i]))
      break;
  if (i == ctx->count) {
    if (ctx->count >= DNF_ATOM_MAX)
      return 0;
    ctx->atoms[ctx->count++] = v;
  }
  lit = (DNF_ATOM_BASE + i) * 2u;
  return dnf_conj(d, &lit, 1, NULL, 0);
}

/* Sorts the literals A and removes the duplicates. */
static uint32_t lits_norm(uint32_t *a, uint32_t n) {
  uint32_t i;
  uint32_t j;
  uint32_t k = 0;
  uint32_t t;
  for (i = 1; i < n; i++)
    for (j = i; j > 0 && a[j - 1u] > a[j]; j--) {
      t = a[j];
      a[j] = a[j - 1u];
      a[j - 1u] = t;
    }
  for (i = 0; i < n; i++)
    if (k == 0 || a[k - 1u] != a[i])
      a[k++] = a[i];
  return k;
}

/* 1 when each literal of X is in Y. Both are sorted. */
static int lits_sub(const uint32_t *x, uint32_t nx, const uint32_t *y, uint32_t ny) {
  uint32_t i = 0;
  uint32_t j;
  for (j = 0; i < nx && j < ny; j++)
    i += x[i] == y[j] ? 1u : 0u;
  return i == nx;
}

static int lits_cmp(const uint32_t *x, uint32_t nx, const uint32_t *y, uint32_t ny) {
  uint32_t i;
  for (i = 0; i < nx && i < ny; i++)
    if (x[i] != y[i])
      return x[i] < y[i] ? -1 : 1;
  return nx == ny ? 0 : (nx < ny ? -1 : 1);
}

/* Removes each meet that another meet absorbs. */
static int dnf_norm(Dnf *d, Dnf *out) {
  uint32_t i;
  uint32_t j;
  int keep;
  int sub;
  out->count = 0;
  for (i = 0; i < d->count; i++)
    d->len[i] = lits_norm(d->lit[i], d->len[i]);
  for (i = 0; i < d->count; i++) {
    keep = 1;
    for (j = 0; j < d->count && keep; j++) {
      sub = j != i && lits_sub(d->lit[j], d->len[j], d->lit[i], d->len[i]);
      keep = !sub || (lits_cmp(d->lit[j], d->len[j], d->lit[i], d->len[i]) == 0 && j > i);
    }
    if (keep && !dnf_conj(out, d->lit[i], d->len[i], NULL, 0))
      return 0;
  }
  return 1;
}

static const Value *lit_value(DnfCtx *ctx, uint32_t lit) {
  uint32_t level = lit / 2u;
  const Value *x = level < DNF_ATOM_BASE ? val_var(ctx->m, level) : ctx->atoms[level - DNF_ATOM_BASE];
  return lit % 2u == 0 ? x : make(ctx->m, VAL_STUCK, OP_INEG, 0, 1, x, NULL, NULL);
}

static const Value *conj_value(DnfCtx *ctx, const uint32_t *lits, uint32_t n) {
  const Value *acc = lit_value(ctx, lits[n - 1u]);
  uint32_t i;
  for (i = n - 1u; i > 0; i--)
    acc = make(ctx->m, VAL_STUCK, OP_IMIN, 0, 2, lit_value(ctx, lits[i - 1u]), acc, NULL);
  return acc;
}

/* An interval value for D: the meets in literal order, nested to the
   right. */
static const Value *dnf_value(DnfCtx *ctx, const Dnf *d) {
  uint32_t order[DNF_CONJ_MAX];
  uint32_t i;
  uint32_t j;
  uint32_t t;
  const Value *acc;
  if (d->count == 0)
    return constant(ctx->m, OP_I0);
  for (i = 0; i < d->count; i++) {
    if (d->len[i] == 0)
      return constant(ctx->m, OP_I1);
    order[i] = i;
  }
  for (i = 1; i < d->count; i++)
    for (j = i; j > 0 && lits_cmp(d->lit[order[j - 1u]], d->len[order[j - 1u]], d->lit[order[j]], d->len[order[j]]) > 0; j--) {
      t = order[j];
      order[j] = order[j - 1u];
      order[j - 1u] = t;
    }
  acc = conj_value(ctx, d->lit[order[d->count - 1u]], d->len[order[d->count - 1u]]);
  for (i = d->count - 1u; i > 0; i--)
    acc = make(ctx->m, VAL_STUCK, OP_IMAX, 0, 2, conj_value(ctx, d->lit[order[i - 1u]], d->len[order[i - 1u]]), acc, NULL);
  return acc;
}

static int dnf_apply(Op op, const Dnf *x, const Dnf *y, Dnf *out) {
  if (op == OP_INEG)
    return dnf_neg(x, out);
  return op == OP_IMIN ? dnf_meet(x, y, out) : dnf_join(x, y, out);
}

static void dnf_ctx(DnfCtx *ctx, Machine *m, uint32_t level) {
  ctx->m = m;
  ctx->level = level > DNF_ATOM_BASE ? level : DNF_ATOM_BASE;
  ctx->count = 0;
}

/* ~ r, r ∧ s and r ∨ s. A trap gives the trap; a value outside the bounds
   gives the stuck operation. */
static const Value *reduce_interval(Machine *m, Op op, const Value *const *a, uint32_t n) {
  DnfCtx ctx;
  Dnf x;
  Dnf y;
  Dnf d;
  Dnf out;
  uint32_t i;
  for (i = 0; i < n; i++)
    if (a[i]->kind == VAL_TRAP)
      return a[i];
  dnf_ctx(&ctx, m, 0);
  y.count = 0;
  if (!dnf_of(&ctx, a[0], &x) || (n == 2u && !dnf_of(&ctx, a[1], &y)) || !dnf_apply(op, &x, &y, &d) || !dnf_norm(&d, &out))
    return val_op(m, VAL_STUCK, op, 0, 0, a, n);
  return dnf_value(&ctx, &out);
}

/* 1 when X and Y hold the same meets. Both are in normal form. */
static int dnf_same(const Dnf *x, const Dnf *y) {
  uint32_t i;
  uint32_t j;
  if (x->count != y->count)
    return 0;
  for (i = 0; i < x->count; i++) {
    for (j = 0; j < y->count; j++)
      if (lits_cmp(x->lit[i], x->len[i], y->lit[j], y->len[j]) == 0)
        break;
    if (j == y->count)
      return 0;
  }
  return 1;
}

/* The face normal form: `i ∧ ~ i` is the empty face. The literals of a
   meet are sorted, so 2k and 2k + 1 are next to each other. */
static int face_norm(DnfCtx *ctx, const Value *v, Dnf *out) {
  Dnf d;
  Dnf n;
  uint32_t i;
  uint32_t j;
  int empty;
  out->count = 0;
  if (!dnf_of(ctx, v, &d) || !dnf_norm(&d, &n))
    return 0;
  for (i = 0; i < n.count; i++) {
    empty = 0;
    for (j = 0; j + 1u < n.len[i]; j++)
      empty = empty || (n.lit[i][j] % 2u == 0 && n.lit[i][j + 1u] == n.lit[i][j] + 1u);
    if (!empty && !dnf_conj(out, n.lit[i], n.len[i], NULL, 0))
      return 0;
  }
  return 1;
}

static int dnf_holds(const Dnf *d) {
  uint32_t i;
  for (i = 0; i < d->count; i++)
    if (d->len[i] == 0)
      return 1;
  return 0;
}

int face_holds(Machine *m, uint32_t level, const Value *phi) {
  DnfCtx ctx;
  Dnf d;
  dnf_ctx(&ctx, m, level);
  return face_norm(&ctx, phi, &d) && dnf_holds(&d);
}

int face_equal(Machine *m, uint32_t level, const Value *a, const Value *b) {
  DnfCtx ctx;
  Dnf x;
  Dnf y;
  dnf_ctx(&ctx, m, level);
  return face_norm(&ctx, a, &x) && face_norm(&ctx, b, &y) && dnf_same(&x, &y);
}

int face_meets(Machine *m, uint32_t level, const Value *phi, FaceMeets *out) {
  DnfCtx ctx;
  Dnf d;
  uint32_t i;
  uint32_t j;
  dnf_ctx(&ctx, m, level);
  out->count = 0;
  if (!face_norm(&ctx, phi, &d))
    return 0;
  for (i = 0; i < d.count; i++) {
    out->len[i] = 0;
    for (j = 0; j < d.len[i]; j++)
      if (d.lit[i][j] / 2u < DNF_ATOM_BASE)
        out->lit[i][out->len[i]++] = d.lit[i][j];
  }
  out->count = d.count;
  return 1;
}

/* The part of the face phi over the variable at level BOUND that holds for
   every value of that variable: the conjunctions of its normal form that
   do not name it. An opaque atom can depend on the variable after a
   substitution, so it keeps the transport neutral (0). zk quotes the atom
   and tests it with core_mentions; leaf has no core_mentions. */
static int face_forall(Machine *m, uint32_t level, const Value *phi, uint32_t bound, const Value **out) {
  DnfCtx ctx;
  Dnf d;
  Dnf kept;
  uint32_t i;
  uint32_t j;
  dnf_ctx(&ctx, m, level);
  if (!face_norm(&ctx, phi, &d))
    return 0;
  kept.count = 0;
  for (i = 0; i < d.count; i++) {
    int names = 0;
    for (j = 0; j < d.len[i]; j++) {
      uint32_t atom = d.lit[i][j] / 2u;
      if (atom >= DNF_ATOM_BASE)
        return 0;
      names = names || atom == bound;
    }
    if (names)
      continue;
    for (j = 0; j < d.len[i]; j++)
      kept.lit[kept.count][j] = d.lit[i][j];
    kept.len[kept.count] = d.len[i];
    kept.count++;
  }
  *out = dnf_value(&ctx, &kept);
  return *out != NULL;
}

/* The environment of the levels below LEVEL, with the end that each of
   the N literals gives to its level. */
static const Env *restricted_env(Machine *m, uint32_t level, const uint32_t *lits, uint32_t n) {
  const Env *env = NULL;
  const Value *v;
  uint32_t i;
  uint32_t j;
  for (i = 0; i < level; i++) {
    v = val_var(m, i);
    for (j = 0; j < n; j++)
      if (lits[j] / 2u == i)
        v = constant(m, lits[j] % 2u == 0 ? OP_I1 : OP_I0);
    env = env_push(m, env, v);
    if (env == NULL)
      return NULL;
  }
  return env;
}

const Value *value_under_face(Machine *m, uint32_t level, const Value *phi, const Value *v) {
  FaceMeets fm;
  uint32_t common[FACE_LITS_MAX];
  uint32_t n = 0;
  uint32_t i;
  uint32_t j;
  uint32_t k;
  const Core *core;
  const Env *env;
  if (v == NULL || phi == NULL || m->diag->set)
    return NULL;
  if (!face_meets(m, level, phi, &fm) || fm.count == 0)
    return m->diag->set ? NULL : v;
  for (i = 0; i < fm.len[0]; i++) {
    for (j = 1; j < fm.count; j++) {
      for (k = 0; k < fm.len[j] && fm.lit[j][k] != fm.lit[0][i]; k++) { }
      if (k == fm.len[j])
        break;
    }
    if (j == fm.count)
      common[n++] = fm.lit[0][i];
  }
  if (n == 0)
    return v;
  core = quote_value(m, level, v);
  env = core == NULL ? NULL : restricted_env(m, level, common, n);
  return core == NULL || (env == NULL && level > 0) ? NULL : eval_core(m, env, core);
}

int conv_under_face(Machine *m, uint32_t level, const Value *phi, const Value *a, const Value *b) {
  FaceMeets fm;
  const Core *ka;
  const Core *kb;
  const Env *env;
  uint32_t i;
  if (a == NULL || b == NULL || phi == NULL || m->diag->set)
    return 0;
  if (conv_values(m, level, a, b))
    return 1;
  if (m->diag->set || !face_meets(m, level, phi, &fm))
    return 0;
  if (fm.count == 0)
    return 1;
  ka = quote_value(m, level, a);
  kb = ka == NULL ? NULL : quote_value(m, level, b);
  if (kb == NULL)
    return 0;
  for (i = 0; i < fm.count; i++) {
    env = restricted_env(m, level, fm.lit[i], fm.len[i]);
    if ((env == NULL && level > 0) || !conv_values(m, level, eval_core(m, env, ka), eval_core(m, env, kb)))
      return 0;
  }
  return 1;
}

/* [ φ ↦ u , rest ]: a cell whose face holds becomes the total cell
   [ i1 ↦ u ] (a Partial value stays apart from its branch); a cell with
   the empty face drops out. A trap argument gives the trap. */
static const Value *reduce_system(Machine *m, const Value *const *a, uint32_t n) {
  DnfCtx ctx;
  Dnf d;
  const Value *total[3];
  uint32_t i;
  for (i = 0; i < n; i++)
    if (a[i]->kind == VAL_TRAP)
      return a[i];
  dnf_ctx(&ctx, m, 0);
  if (!face_norm(&ctx, a[0], &d))
    return val_op(m, VAL_OP, OP_SYSTEM, 0, 0, a, n);
  if (dnf_holds(&d)) {
    total[0] = constant(m, OP_I1);
    total[1] = a[1];
    total[2] = constant(m, OP_SYSNIL);
    return val_op(m, VAL_OP, OP_SYSTEM, 0, 0, total, 3);
  }
  if (val_is(a[2], OP_SYSTEM) && face_holds(m, 0, a[2]->args[0]))
    return a[2];
  return d.count == 0 ? a[2] : val_op(m, VAL_OP, OP_SYSTEM, 0, 0, a, n);
}

/* fromPartial p: the branch of a system whose first face holds. Any
   other p stays stuck. A trap gives the trap. */
static const Value *reduce_from_partial(Machine *m, const Value *const *a, uint32_t n) {
  if (a[0]->kind == VAL_TRAP)
    return a[0];
  if (val_is(a[0], OP_SYSTEM) && face_holds(m, 0, a[0]->args[0]))
    return a[0]->args[1];
  return val_op(m, VAL_STUCK, OP_FROMPARTIAL, 0, 0, a, n);
}

/* The part FIELD of the type T: field 0 is the domain of a Pi or a Sigma,
   field k is the argument k - 1 of a type former. */
static const Value *reduce_tpart(Machine *m, uint32_t field, const Value *const *a, uint32_t n) {
  const Value *t = a[0];
  switch (t->kind) {
  case VAL_PI:
  case VAL_SIGMA:
    if (field == 0u)
      return t->dom;
    break;
  case VAL_OP:
    if (field >= 1u && field - 1u < t->argc)
      return t->args[field - 1u];
    break;
  case VAL_TRAP:
    return t;
  case VAL_NAT:
  case VAL_UNIV:
  case VAL_LAM:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  return val_op(m, VAL_STUCK, OP_TPART, 0, field, a, n);
}

/* The codomain of the Pi or Sigma T at x. */
static const Value *reduce_tcod(Machine *m, const Value *const *a, uint32_t n) {
  const Value *t = a[0];
  switch (t->kind) {
  case VAL_PI:
  case VAL_SIGMA:
    return closure_apply(m, t, a[1]);
  case VAL_TRAP:
    return t;
  case VAL_NAT:
  case VAL_UNIV:
  case VAL_LAM:
  case VAL_OP:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  return val_op(m, VAL_STUCK, OP_TCOD, 0, 0, a, n);
}

/* Core templates: the reductions of transp build a small Core term over
   an env [line, r, u, ...] and evaluate it. Indices are de Bruijn. */
static Core *tmpl_new(Machine *m, CoreKind kind) {
  Core *c = arena_alloc(m->arena, sizeof *c);
  if (c == NULL) {
    oom(m);
    return NULL;
  }
  c->kind = kind;
  c->index = 0;
  c->nat = 0;
  c->op = OP_NAT;
  c->inst = 0;
  c->field = 0;
  c->name = NULL;
  c->a = NULL;
  c->b = NULL;
  c->args = NULL;
  c->argc = 0;
  return c;
}

static const Core *tmpl_var(Machine *m, uint32_t index) {
  Core *c = tmpl_new(m, CORE_VAR);
  if (c != NULL)
    c->index = index;
  return c;
}

static const Core *tmpl_lam(Machine *m, const char *name, const Core *body) {
  Core *c = body == NULL ? NULL : tmpl_new(m, CORE_LAM);
  if (c != NULL) {
    c->name = name;
    c->b = body;
  }
  return c;
}

static const Core *tmpl_app(Machine *m, const Core *f, const Core *x) {
  Core *c = f == NULL || x == NULL ? NULL : tmpl_new(m, CORE_APP);
  if (c != NULL) {
    c->a = f;
    c->b = x;
  }
  return c;
}

/* An operation with argc (at most 3) of the arguments a, b, c. */
static const Core *tmpl_op(Machine *m, Op op, uint32_t field, uint32_t argc, const Core *a, const Core *b, const Core *x) {
  const Core *given[3];
  const Core **args = arena_alloc(m->arena, 3u * sizeof *args);
  Core *c;
  uint32_t i;
  given[0] = a;
  given[1] = b;
  given[2] = x;
  if (args == NULL) {
    oom(m);
    return NULL;
  }
  for (i = 0; i < argc; i++) {
    if (given[i] == NULL)
      return NULL;
    args[i] = given[i];
  }
  c = tmpl_new(m, CORE_OP);
  if (c != NULL) {
    c->op = op;
    c->field = field;
    c->args = args;
    c->argc = argc;
  }
  return c;
}

/* A Core op with four arguments (hcomp, the path application). */
static const Core *tmpl_op4(Machine *m, Op op, const Core *a, const Core *b, const Core *x, const Core *y) {
  const Core **args = arena_alloc(m->arena, 4u * sizeof *args);
  Core *c;
  if (args == NULL) {
    oom(m);
    return NULL;
  }
  if (a == NULL || b == NULL || x == NULL || y == NULL)
    return NULL;
  args[0] = a;
  args[1] = b;
  args[2] = x;
  args[3] = y;
  c = tmpl_new(m, CORE_OP);
  if (c != NULL) {
    c->op = op;
    c->field = 0;
    c->args = args;
    c->argc = 4u;
  }
  return c;
}

/* Evaluate t in the env [vals 0, ..., vals k - 1]; vals k - 1 is index 0. */
static const Value *tmpl_run(Machine *m, const Core *t, const Value *const *vals, uint32_t k) {
  const Env *env = NULL;
  uint32_t i;
  if (t == NULL)
    return NULL;
  for (i = 0; i < k; i++) {
    env = env_push(m, env, vals[i]);
    if (env == NULL)
      return NULL;
  }
  return eval_core(m, env, t);
}

/* fun i => TPART field (line i), with the line at index l. */
static const Core *tmpl_part_line(Machine *m, uint32_t field, uint32_t l) {
  return tmpl_lam(m, "i", tmpl_op(m, OP_TPART, field, 1u, tmpl_app(m, tmpl_var(m, l + 1u), tmpl_var(m, 0)), NULL, NULL));
}

/* The inverse filler of the Pi rule at the point p (pj: p under one more
   binder): transp (fun j => TPART0 (line (p ∨ ~ j))) (p ∨ r) x. */
static const Core *tmpl_xt(Machine *m, const Core *pj, const Core *p, uint32_t l, uint32_t r, uint32_t x) {
  const Core *at = tmpl_op(m, OP_IMAX, 0, 2u, pj, tmpl_op(m, OP_INEG, 0, 1u, tmpl_var(m, 0), NULL, NULL), NULL);
  const Core *line = tmpl_lam(m, "j", tmpl_op(m, OP_TPART, 0, 1u, tmpl_app(m, tmpl_var(m, l + 1u), at), NULL, NULL));
  return tmpl_op(m, OP_TRANSP, 0, 3u, line, tmpl_op(m, OP_IMAX, 0, 2u, p, tmpl_var(m, r), NULL), tmpl_var(m, x));
}

/* The witness filler of the Sigma rule at the point p:
   transp (fun j => TPART0 (line (p ∧ j))) (~ p ∨ r) (witness u). */
static const Core *tmpl_wf(Machine *m, const Core *pj, const Core *p, uint32_t l, uint32_t r, uint32_t u) {
  const Core *line = tmpl_lam(m, "j", tmpl_op(m, OP_TPART, 0, 1u, tmpl_app(m, tmpl_var(m, l + 1u), tmpl_op(m, OP_IMIN, 0, 2u, pj, tmpl_var(m, 0), NULL)), NULL, NULL));
  const Core *phi = tmpl_op(m, OP_IMAX, 0, 2u, tmpl_op(m, OP_INEG, 0, 1u, p, NULL, NULL), tmpl_var(m, r), NULL);
  return tmpl_op(m, OP_TRANSP, 0, 3u, line, phi, tmpl_op(m, OP_WITNESS, 0, 1u, tmpl_var(m, u), NULL, NULL));
}

/* Env [line, r, f].
   fun x => transp (fun i => TCOD (line i) (xt i)) r (f (xt i0)). */
static const Core *tmpl_pi(Machine *m) {
  const Core *i0 = tmpl_op(m, OP_I0, 0, 0, NULL, NULL, NULL);
  const Core *line = tmpl_lam(m, "i", tmpl_op(m, OP_TCOD, 0, 2u, tmpl_app(m, tmpl_var(m, 4u), tmpl_var(m, 0)), tmpl_xt(m, tmpl_var(m, 1u), tmpl_var(m, 0), 4u, 3u, 1u), NULL));
  const Core *start = tmpl_app(m, tmpl_var(m, 1u), tmpl_xt(m, i0, i0, 3u, 2u, 0));
  return tmpl_lam(m, "x", tmpl_op(m, OP_TRANSP, 0, 3u, line, tmpl_var(m, 2u), start));
}

/* Env [line, r, u].
   pack (wf i1) (transp (fun i => TCOD (line i) (wf i)) r (payload u)). */
static const Core *tmpl_sigma(Machine *m) {
  const Core *i1 = tmpl_op(m, OP_I1, 0, 0, NULL, NULL, NULL);
  const Core *line = tmpl_lam(m, "i", tmpl_op(m, OP_TCOD, 0, 2u, tmpl_app(m, tmpl_var(m, 3u), tmpl_var(m, 0)), tmpl_wf(m, tmpl_var(m, 1u), tmpl_var(m, 0), 3u, 2u, 1u), NULL));
  const Core *second = tmpl_op(m, OP_TRANSP, 0, 3u, line, tmpl_var(m, 1u), tmpl_op(m, OP_PAYLOAD, 0, 1u, tmpl_var(m, 0), NULL, NULL));
  return tmpl_op(m, OP_PACK, 0, 2u, tmpl_wf(m, i1, i1, 2u, 1u, 0), second, NULL);
}

/* Env [line, r, field 0, ...]: the constructor of u with each field moved
   along TPART part (part 0: along the line itself). */
static const Core *tmpl_ctor(Machine *m, const Value *u, uint32_t part0, uint32_t part1) {
  const uint32_t k = 2u + u->argc;
  const Core *f[2] = {NULL, NULL};
  const uint32_t parts[2] = {part0, part1};
  uint32_t j;
  Core *c;
  for (j = 0; j < u->argc && j < 2u; j++)
    f[j] = tmpl_op(m, OP_TRANSP, 0, 3u, parts[j] == 0u ? tmpl_var(m, k - 1u) : tmpl_part_line(m, parts[j], k - 1u), tmpl_var(m, k - 2u), tmpl_var(m, k - 3u - j));
  c = (Core *)tmpl_op(m, u->op, 0, u->argc, f[0], f[1], NULL);
  if (c != NULL)
    c->inst = u->inst;
  return c;
}

static const Value *stuck_transp(Machine *m, const Value *const *a, uint32_t n) {
  return val_op(m, VAL_STUCK, OP_TRANSP, 0, 0, a, n);
}

/* A canonical u along a data line that varies. */
static const Value *transp_ctor(Machine *m, const Value *const *a, uint32_t n) {
  const Value *u = a[2];
  const Value *vals[4];
  vals[0] = a[0];
  vals[1] = a[1];
  vals[2] = u->argc > 0u ? u->args[0] : NULL;
  vals[3] = u->argc > 1u ? u->args[1] : NULL;
  switch (u->op) {
  case OP_NONE:
    return u;
  case OP_SOME:
    return tmpl_run(m, tmpl_ctor(m, u, 1u, 0), vals, 3u);
  /* Family params that vary need a neutral transport. */
  case OP_NAT:
  case OP_BOOL:
  case OP_UNIT:
  case OP_LEAF:
  case OP_OPTION:
  case OP_FAMILY:
  case OP_UNIT_VAL:
  case OP_TRUE:
  case OP_FALSE:
  case OP_PACK:
  case OP_CTOR:
  case OP_OPTION_ELIM:
  case OP_PURE:
  case OP_MAP:
  case OP_BIND:
  case OP_FOLD_NAT:
  case OP_FOLD_FAMILY:
  case OP_WITNESS:
  case OP_PAYLOAD:
  case OP_NAT_ADD:
  case OP_NAT_SUB:
  case OP_NAT_MUL:
  case OP_NAT_EQ:
  case OP_NAT_LE:
  case OP_FLAG_IF:
  case OP_PROJ:
  case OP_SSET:
  case OP_I:
  case OP_I0:
  case OP_I1:
  case OP_PATHP:
  case OP_PLAM:
  case OP_PAPP:
  case OP_INEG:
  case OP_IMIN:
  case OP_IMAX:
  case OP_PARTIAL:
  case OP_SYSTEM:
  case OP_SYSNIL:
  case OP_FROMPARTIAL:
  case OP_TRANSP:
  case OP_TPART:
  case OP_TCOD:
  case OP_HCOMP:
  case OP_SYSMAP:
  case OP_GLUE:
  case OP_GLUE_ELEM:
  case OP_UNGLUE:
    break;
  }
  return stuck_transp(m, a, n);
}

/* Data with args: regularity when the line is constant, else
   constructor-wise on a canonical u. */
static const Value *transp_data(Machine *m, const Value *h, const Value *const *a, uint32_t n) {
  const Value *start = apply_value(m, a[0], constant(m, OP_I0));
  if (start == NULL)
    return NULL;
  if (conv_values(m, m->fresh_level, h, start))
    return a[2];
  switch (a[2]->kind) {
  case VAL_TRAP:
    return a[2];
  case VAL_OP:
    return transp_ctor(m, a, n);
  case VAL_NAT:
  case VAL_UNIV:
  case VAL_LAM:
  case VAL_PI:
  case VAL_SIGMA:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  return stuck_transp(m, a, n);
}

/* Env [line, phi, side, u0]. comp line side u0 in the CCHM form:
   hcomp (line i1) phi (fun j => SYSMAP (fun b => transp (fun i =>
   line (i \/ j)) j b) (side j)) (transp line i0 u0). */
static const Core *tmpl_comp(Machine *m) {
  /* Under j, b and i: i 0, b 1, j 2, u0 3, side 4, phi 5, line 6. */
  const Core *at = tmpl_op(m, OP_IMAX, 0, 2u, tmpl_var(m, 0), tmpl_var(m, 2u), NULL);
  const Core *f = tmpl_lam(m, "b", tmpl_op(m, OP_TRANSP, 0, 3u, tmpl_lam(m, "i", tmpl_app(m, tmpl_var(m, 6u), at)), tmpl_var(m, 1u), tmpl_var(m, 0)));
  const Core *side = tmpl_lam(m, "j", tmpl_op(m, OP_SYSMAP, 0, 2u, f, tmpl_app(m, tmpl_var(m, 2u), tmpl_var(m, 0)), NULL));
  const Core *a1 = tmpl_app(m, tmpl_var(m, 3u), tmpl_op(m, OP_I1, 0, 0, NULL, NULL, NULL));
  const Core *base = tmpl_op(m, OP_TRANSP, 0, 3u, tmpl_var(m, 3u), tmpl_op(m, OP_I0, 0, 0, NULL, NULL, NULL), tmpl_var(m, 0));
  return tmpl_op4(m, OP_HCOMP, a1, tmpl_var(m, 2u), side, base);
}

/* comp LINE SIDE U0 over the face PHI, always in the CCHM form. */
static const Value *comp_value(Machine *m, const Value *line, const Value *phi, const Value *side, const Value *u0) {
  const Value *vals[4];
  if (line == NULL || phi == NULL || side == NULL || u0 == NULL)
    return NULL;
  vals[0] = line;
  vals[1] = phi;
  vals[2] = side;
  vals[3] = u0;
  return tmpl_run(m, tmpl_comp(m), vals, 4u);
}

/* The comp template of comp_value at the Core args LINE PHI SIDE U0, under
   any binders: (fun line phi side u0 => COMP) LINE PHI SIDE U0. */
static const Core *tmpl_comp_at(Machine *m, const Core *line, const Core *phi, const Core *side, const Core *u0) {
  const Core *f = tmpl_lam(m, "line", tmpl_lam(m, "phi", tmpl_lam(m, "side", tmpl_lam(m, "u0", tmpl_comp(m)))));
  return tmpl_app(m, tmpl_app(m, tmpl_app(m, tmpl_app(m, f, line), phi), side), u0);
}

/* p @ j at the start type of the line:
   PAPP [p, j, TPART2 (line i0), TPART3 (line i0)]. */
static const Core *tmpl_papp_start(Machine *m, uint32_t p, uint32_t j, uint32_t l) {
  const Core *start = tmpl_app(m, tmpl_var(m, l), tmpl_op(m, OP_I0, 0, 0, NULL, NULL, NULL));
  return tmpl_op4(m, OP_PAPP, tmpl_var(m, p), tmpl_var(m, j), tmpl_op(m, OP_TPART, 2u, 1u, start, NULL, NULL), tmpl_op(m, OP_TPART, 3u, 1u, start, NULL, NULL));
}

/* Env [line, r, p], line i = PathP (P i) (a i) (b i).
   <j> comp (fun i => P i j) (fun k => [ ~ j |-> a k , j |-> b k , r |-> p @ j ])
     (p @ j) over the face ~ j \/ j \/ r. */
static const Core *tmpl_transp_path(Machine *m) {
  /* Under j and k (or j and i): k 0, j 1, p 2, r 3, line 4. */
  const Core *lk = tmpl_app(m, tmpl_var(m, 4u), tmpl_var(m, 0));
  const Core *line = tmpl_lam(m, "i", tmpl_app(m, tmpl_op(m, OP_TPART, 1u, 1u, lk, NULL, NULL), tmpl_var(m, 1u)));
  const Core *rp = tmpl_op(m, OP_SYSTEM, 0, 3u, tmpl_var(m, 3u), tmpl_papp_start(m, 2u, 1u, 4u), tmpl_op(m, OP_SYSNIL, 0, 0, NULL, NULL, NULL));
  const Core *bk = tmpl_op(m, OP_SYSTEM, 0, 3u, tmpl_var(m, 1u), tmpl_op(m, OP_TPART, 3u, 1u, lk, NULL, NULL), rp);
  const Core *not_j = tmpl_op(m, OP_INEG, 0, 1u, tmpl_var(m, 1u), NULL, NULL);
  const Core *side = tmpl_lam(m, "k", tmpl_op(m, OP_SYSTEM, 0, 3u, not_j, tmpl_op(m, OP_TPART, 2u, 1u, lk, NULL, NULL), bk));
  /* Under j: j 0, p 1, r 2, line 3. */
  const Core *both = tmpl_op(m, OP_IMAX, 0, 2u, tmpl_op(m, OP_INEG, 0, 1u, tmpl_var(m, 0), NULL, NULL), tmpl_var(m, 0), NULL);
  const Core *phi = tmpl_op(m, OP_IMAX, 0, 2u, both, tmpl_var(m, 2u), NULL);
  const Core *body = tmpl_comp_at(m, line, phi, side, tmpl_papp_start(m, 1u, 0, 3u));
  return tmpl_op(m, OP_PLAM, 0, 1u, tmpl_lam(m, "j", body), NULL, NULL);
}

/* transp at a type former h, the line at a fresh point. */
#define GLUE_BRANCH_MAX FACE_MEETS_MAX
static const Value *witness_of(Machine *m, const Value *v);
static const Value *payload_of(Machine *m, const Value *v);

/* The index of the domain definition NAME, or UINT32_MAX. */
static uint32_t def_index(const Machine *m, const char *name) {
  uint32_t i;
  for (i = 0; i < m->def_count; i++)
    if (m->defs[i].name != NULL && strcmp(m->defs[i].name, name) == 0)
      return i;
  return UINT32_MAX;
}

/* v over the variable at level LVL, as the line λ i → v. The context
   levels below LVL bind to themselves. */
static const Value *line_of(Machine *m, uint32_t lvl, const Value *v) {
  const Core *body = quote_value(m, lvl + 1u, v);
  const Env *env = NULL;
  uint32_t l;
  if (body == NULL)
    return NULL;
  for (l = 0; l < lvl; l++) {
    env = env_push(m, env, val_var(m, l));
    if (env == NULL)
      return NULL;
  }
  return val_closure(m, VAL_LAM, "i", NULL, env, body);
}

/* The env variable at position P of N, under D binders. */
static const Core *tmpl_at(Machine *m, uint32_t n, uint32_t p, uint32_t d) {
  return tmpl_var(m, n - 1u - p + d);
}

static const Core *tmpl_global(Machine *m, uint32_t index) {
  Core *c = tmpl_new(m, CORE_GLOBAL);
  if (c != NULL)
    c->index = index;
  return c;
}

static const Core *tmpl_const(Machine *m, Op op) {
  return tmpl_op(m, op, 0, 0, NULL, NULL, NULL);
}

static const Core *tmpl_neg(Machine *m, const Core *x) {
  return tmpl_op(m, OP_INEG, 0, 1, x, NULL, NULL);
}

static const Core *tmpl_imax(Machine *m, const Core *x, const Core *y) {
  return tmpl_op(m, OP_IMAX, 0, 2, x, y, NULL);
}

static const Core *tmpl_system(Machine *m, const Core *face, const Core *v, const Core *rest) {
  return tmpl_op(m, OP_SYSTEM, 0, 3, face, v, rest);
}

/* The fiber env: 0 A line, 1 r, 2 u, 3 T line, 4 w line, 5 a1', 6 A1,
   7 T1, 8 f1, 9 center, 10 contraction, 11 delta, 12 t1', 13 omega. */
#define Q(p, d) tmpl_at(m, 14u, (p), (d))

/* The fiber element (t1', λ j → omega (~ j)) over a1', under D binders. */
static const Core *fib_elem(Machine *m, uint32_t d) {
  const Core *w1t = tmpl_app(m, tmpl_app(m, Q(4, d + 1u), tmpl_const(m, OP_I1)), Q(12, d + 1u));
  const Core *path = tmpl_op4(m, OP_PAPP, Q(13, d + 1u), tmpl_neg(m, tmpl_var(m, 0)), Q(5, d + 1u), w1t);
  return tmpl_op(m, OP_PACK, 0, 2, Q(12, d), tmpl_op(m, OP_PLAM, 0, 1, tmpl_lam(m, "j", path), NULL, NULL), NULL);
}

/* A branch whose face holds for all i: t1' = transp T r u, omega the path
   from a1' to w i1 t1' in A i1 (CCHM pres), and the fiber over a1' extended
   from (t1', sym omega) on delta by the contraction of the equivalence at
   i1. Off delta the center stays, so the hcomp is total. */
static const Value *fiber_extend(Machine *m, const Value **q, uint32_t fib_def) {
  DnfCtx ctx;
  Dnf faces;
  const Value *vals[3 + 3 * GLUE_BRANCH_MAX];
  const Value *u0 = q[2];
  const Core *fill;
  const Core *side;
  const Core *omega;
  const Core *z;
  const Core *fibt;
  const Core *sys;
  uint32_t k;
  uint32_t nv;
  dnf_ctx(&ctx, m, m->fresh_level);
  if (!face_norm(&ctx, q[11], &faces))
    return NULL;
  q[12] = q[5];
  q[13] = q[5];
  fibt = tmpl_app(m, tmpl_app(m, tmpl_app(m, tmpl_app(m, tmpl_global(m, fib_def), Q(7, 0)), Q(6, 0)), Q(8, 0)), Q(5, 0));
  vals[0] = tmpl_run(m, fibt, q, 14u);
  vals[1] = q[9];
  vals[2] = q[11];
  /* Values and their types may use different partitions of this cover. */
  for (k = 0; k < faces.count; k++) {
    const Value *face = faces.len[k] == 0 ? constant(m, OP_I1) : conj_value(&ctx, faces.lit[k], faces.len[k]);
    q[2] = value_under_face(m, m->fresh_level, face, u0);
    if (q[2] == NULL)
      return NULL;
    q[12] = q[5];
    q[13] = q[5];
    q[12] = tmpl_run(m, tmpl_op(m, OP_TRANSP, 0, 3, Q(3, 0), Q(1, 0), Q(2, 0)), q, 14u);
    /* Under j and i: fill i = transp (λ k → T (i ∧ k)) (~ i ∨ r) u. */
    fill = tmpl_op(m, OP_TRANSP, 0, 3,
                   tmpl_lam(m, "k", tmpl_app(m, Q(3, 3), tmpl_op(m, OP_IMIN, 0, 2, tmpl_var(m, 1), tmpl_var(m, 0), NULL))),
                   tmpl_imax(m, tmpl_neg(m, tmpl_var(m, 0)), Q(1, 2)), Q(2, 2));
    /* omega = λ j → hcomp A1 (j ∨ r) (λ i → [ j ↦ transp (λ k → A (i ∨ k)) (i ∨ r) (w i (fill i)),
       r ↦ w i1 u ]) a1'. */
    side = tmpl_lam(m, "i", tmpl_system(m, tmpl_var(m, 1),
                    tmpl_op(m, OP_TRANSP, 0, 3,
                            tmpl_lam(m, "k", tmpl_app(m, Q(0, 3), tmpl_imax(m, tmpl_var(m, 1), tmpl_var(m, 0)))),
                            tmpl_imax(m, tmpl_var(m, 0), Q(1, 2)),
                            tmpl_app(m, tmpl_app(m, Q(4, 2), tmpl_var(m, 0)), fill)),
                    tmpl_system(m, Q(1, 2), tmpl_app(m, tmpl_app(m, Q(4, 2), tmpl_const(m, OP_I1)), Q(2, 2)), tmpl_const(m, OP_SYSNIL))));
    omega = tmpl_op(m, OP_PLAM, 0, 1,
                    tmpl_lam(m, "j", tmpl_op4(m, OP_HCOMP, Q(6, 1), tmpl_imax(m, tmpl_var(m, 0), Q(1, 1)), side, Q(5, 1))), NULL, NULL);
    q[13] = tmpl_run(m, omega, q, 14u);
    if (q[12] == NULL || q[13] == NULL)
      return NULL;
    z = fib_elem(m, 0u);
    vals[3 + 3 * k] = face;
    vals[5 + 3 * k] = tmpl_run(m, z, q, 14u);
    vals[4 + 3 * k] = apply_value(m, q[10], vals[5 + 3 * k]);
    if (vals[4 + 3 * k] == NULL || vals[5 + 3 * k] == NULL)
      return NULL;
  }
  nv = 3u + 3u * faces.count;
  sys = tmpl_const(m, OP_SYSNIL);
  for (k = faces.count; k > 0; k--) {
    uint32_t b = 3u + 3u * (k - 1u);
    sys = tmpl_system(m, tmpl_at(m, nv, b, 1),
                      tmpl_op4(m, OP_PAPP, tmpl_at(m, nv, b + 1u, 1), tmpl_var(m, 0), tmpl_at(m, nv, 1, 1), tmpl_at(m, nv, b + 2u, 1)), sys);
  }
  side = tmpl_lam(m, "j", sys);
  return vals[0] == NULL ? NULL : tmpl_run(m, tmpl_op4(m, OP_HCOMP, tmpl_at(m, nv, 0, 0), tmpl_at(m, nv, 2, 0), side, tmpl_at(m, nv, 1, 0)), vals, nv);
}

#undef Q

/* transp along a Glue line (CCHM 6.2): a0 = unglue u at i0 and a1' =
   transp A r a0. Each branch at i1 gets the fiber of its equivalence over
   a1', extended from the transport through T where the face holds for all
   i. a1 corrects a1' by the fiber paths, and the result is glue of the
   parts. CCHM Glue is not regular, so a constant line takes the formula
   too (S2 R2: the zk rule that gives u there is not ported). */
static const Value *transp_glue(Machine *m, const Value *const *a, uint32_t n) {
  const Value *i0 = constant(m, OP_I0);
  const Value *i1 = constant(m, OP_I1);
  uint32_t lvl = m->fresh_level;
  const Value *pt = val_var(m, lvl);
  const Value *g = pt == NULL ? NULL : apply_value(m, a[0], pt);
  const Value *al;
  const Value *phil;
  const Value *sysl;
  const Value *a0;
  const Value *a1p;
  const Value *sys;
  const Value *vals[4 + 4 * GLUE_BRANCH_MAX];
  const Value *q[14];
  const Value *r;
  const Core *tsys;
  const Core *asys;
  const Core *t;
  uint32_t fib_def = def_index(m, "fiber");
  uint32_t k = 0;
  uint32_t nv;
  uint32_t p;
  if (g == NULL || !val_is(g, OP_GLUE))
    return stuck_transp(m, a, n);
  al = line_of(m, lvl, g->args[0]);
  phil = line_of(m, lvl, g->args[1]);
  sysl = line_of(m, lvl, g->args[2]);
  if (al == NULL || phil == NULL || sysl == NULL || fib_def == UINT32_MAX)
    return stuck_transp(m, a, n);
  vals[0] = al;
  vals[1] = phil;
  vals[2] = sysl;
  vals[3] = a[2];
  a0 = tmpl_run(m, tmpl_op4(m, OP_UNGLUE, tmpl_app(m, tmpl_at(m, 4u, 0, 0), tmpl_const(m, OP_I0)),
                            tmpl_app(m, tmpl_at(m, 4u, 1, 0), tmpl_const(m, OP_I0)),
                            tmpl_app(m, tmpl_at(m, 4u, 2, 0), tmpl_const(m, OP_I0)), tmpl_at(m, 4u, 3, 0)), vals, 4u);
  vals[1] = a[1];
  vals[2] = a0;
  a1p = a0 == NULL ? NULL : tmpl_run(m, tmpl_op(m, OP_TRANSP, 0, 3, tmpl_at(m, 3u, 0, 0), tmpl_at(m, 3u, 1, 0), tmpl_at(m, 3u, 2, 0)), vals, 3u);
  vals[0] = apply_value(m, al, i1);
  vals[1] = apply_value(m, phil, i1);
  vals[2] = a1p;
  if (a1p == NULL || vals[0] == NULL || vals[1] == NULL)
    return stuck_transp(m, a, n);
  for (sys = g->args[2]; val_is(sys, OP_SYSTEM); sys = sys->args[2]) {
    const Value *facel = line_of(m, lvl, sys->args[0]);
    const Value *bl = line_of(m, lvl, sys->args[1]);
    const Value *face1;
    const Value *b1;
    const Value *e1;
    const Value *pa;
    const Value *fib;
    const Value *delta;
    if (k == GLUE_BRANCH_MAX || facel == NULL || bl == NULL)
      return stuck_transp(m, a, n);
    face1 = apply_value(m, facel, i1);
    if (face1 == NULL || !face_forall(m, lvl + 1u, sys->args[0], lvl, &delta))
      return stuck_transp(m, a, n);
    if (face_equal(m, m->fresh_level, face1, i0))
      continue;
    b1 = apply_value(m, bl, i1);
    e1 = payload_of(m, b1);
    pa = apply_value(m, payload_of(m, e1), a1p);
    fib = witness_of(m, pa);
    if (!val_is(delta, OP_I0)) {
      q[0] = al;
      q[1] = a[1];
      q[2] = a[2];
      q[3] = line_of(m, lvl, witness_of(m, sys->args[1]));
      q[4] = line_of(m, lvl, witness_of(m, payload_of(m, sys->args[1])));
      q[5] = a1p;
      q[6] = vals[0];
      q[7] = witness_of(m, b1);
      q[8] = witness_of(m, e1);
      q[9] = fib;
      q[10] = payload_of(m, pa);
      q[11] = delta;
      fib = q[3] == NULL || q[4] == NULL ? NULL : fiber_extend(m, q, fib_def);
      if (fib == NULL)
        return stuck_transp(m, a, n);
    }
    vals[3 + 4 * k] = face1;
    vals[4 + 4 * k] = witness_of(m, fib);
    vals[5 + 4 * k] = payload_of(m, fib);
    vals[6 + 4 * k] = apply_value(m, witness_of(m, e1), witness_of(m, fib));
    k++;
  }
  vals[3u + 4u * k] = apply_value(m, a[0], i1);
  if (vals[3u + 4u * k] == NULL)
    return NULL;
  nv = 4u + 4u * k;
  /* glue [ face_k ↦ t1_k ] (hcomp A1 (φ1 ∨ ~ φ1) (λ j → [ face_k ↦ pf_k (~ j) , ~ φ1 ↦ a1' ]) a1'). */
  tsys = tmpl_const(m, OP_SYSNIL);
  asys = tmpl_system(m, tmpl_neg(m, tmpl_at(m, nv, 1, 1)), tmpl_at(m, nv, 2, 1), tmpl_const(m, OP_SYSNIL));
  for (p = k; p > 0; p--) {
    uint32_t b = 3u + 4u * (p - 1u);
    tsys = tmpl_system(m, tmpl_at(m, nv, b, 0), tmpl_at(m, nv, b + 1u, 0), tsys);
    asys = tmpl_system(m, tmpl_at(m, nv, b, 1),
                       tmpl_op4(m, OP_PAPP, tmpl_at(m, nv, b + 2u, 1), tmpl_neg(m, tmpl_var(m, 0)), tmpl_at(m, nv, b + 3u, 1), tmpl_at(m, nv, 2, 1)),
                       asys);
  }
  t = tmpl_op(m, OP_GLUE_ELEM, 0, 3, tmpl_at(m, nv, nv - 1u, 0), tsys,
              tmpl_op4(m, OP_HCOMP, tmpl_at(m, nv, 0, 0), tmpl_imax(m, tmpl_at(m, nv, 1, 0), tmpl_neg(m, tmpl_at(m, nv, 1, 0))),
                       tmpl_lam(m, "j", asys), tmpl_at(m, nv, 2, 0)));
  r = tmpl_run(m, t, vals, nv);
  return r == NULL ? stuck_transp(m, a, n) : r;
}

static const Value *transp_op(Machine *m, const Value *h, const Value *const *a, uint32_t n) {
  switch (h->op) {
  case OP_NAT:
  case OP_BOOL:
  case OP_UNIT:
  case OP_LEAF:
    return a[2];
  case OP_OPTION:
  case OP_FAMILY:
    return transp_data(m, h, a, n);
  case OP_PATHP:
    return tmpl_run(m, tmpl_transp_path(m), a, 3u);
  case OP_GLUE:
    return transp_glue(m, a, n);
  /* The others are not types of Type n. */
  case OP_UNIT_VAL:
  case OP_TRUE:
  case OP_FALSE:
  case OP_NONE:
  case OP_SOME:
  case OP_PACK:
  case OP_CTOR:
  case OP_OPTION_ELIM:
  case OP_PURE:
  case OP_MAP:
  case OP_BIND:
  case OP_FOLD_NAT:
  case OP_FOLD_FAMILY:
  case OP_WITNESS:
  case OP_PAYLOAD:
  case OP_NAT_ADD:
  case OP_NAT_SUB:
  case OP_NAT_MUL:
  case OP_NAT_EQ:
  case OP_NAT_LE:
  case OP_FLAG_IF:
  case OP_PROJ:
  case OP_SSET:
  case OP_I:
  case OP_I0:
  case OP_I1:
  case OP_PLAM:
  case OP_PAPP:
  case OP_INEG:
  case OP_IMIN:
  case OP_IMAX:
  case OP_PARTIAL:
  case OP_SYSTEM:
  case OP_SYSNIL:
  case OP_FROMPARTIAL:
  case OP_TRANSP:
  case OP_TPART:
  case OP_TCOD:
  case OP_HCOMP:
  case OP_SYSMAP:
  case OP_GLUE_ELEM:
  case OP_UNGLUE:
    break;
  }
  return stuck_transp(m, a, n);
}

/* transp line r u: u when the face r holds, else by the head of the line
   at a fresh point. */
static const Value *reduce_transp(Machine *m, const Value *const *a, uint32_t n) {
  const Value *point;
  const Value *h;
  if (face_holds(m, 0, a[1]))
    return a[2];
  point = val_var(m, m->fresh_level);
  h = point == NULL ? NULL : apply_value(m, a[0], point);
  if (h == NULL)
    return NULL;
  switch (h->kind) {
  case VAL_UNIV:
    return a[2];
  case VAL_TRAP:
    return h;
  case VAL_PI:
    return tmpl_run(m, tmpl_pi(m), a, 3u);
  case VAL_SIGMA:
    return tmpl_run(m, tmpl_sigma(m), a, 3u);
  case VAL_OP:
    return transp_op(m, h, a, n);
  case VAL_NAT:
  case VAL_LAM:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  return stuck_transp(m, a, n);
}

static const Value *stuck_hcomp(Machine *m, const Value *const *a, uint32_t n) {
  return val_op(m, VAL_STUCK, OP_HCOMP, 0, 0, a, n);
}

/* SYSMAP f sys: f on each branch of the system sys. reduce_system
   rebuilds each cell. */
static const Value *reduce_sysmap(Machine *m, const Value *const *a, uint32_t n) {
  const Value *cell[3];
  const Value *tail[2];
  if (val_is(a[1], OP_SYSNIL))
    return a[1];
  if (!val_is(a[1], OP_SYSTEM))
    return val_op(m, VAL_STUCK, OP_SYSMAP, 0, 0, a, n);
  tail[0] = a[0];
  tail[1] = a[1]->args[2];
  cell[0] = a[1]->args[0];
  cell[1] = apply_value(m, a[0], a[1]->args[1]);
  cell[2] = reduce_sysmap(m, tail, 2u);
  if (cell[1] == NULL || cell[2] == NULL)
    return NULL;
  return reduce_system(m, cell, 3u);
}

/* A constant with no fields: a numeral, a flag, unit or none. */
static int fieldless(const Value *v) {
  if (v->kind == VAL_NAT)
    return 1;
  return val_is(v, OP_TRUE) || val_is(v, OP_FALSE) || val_is(v, OP_UNIT_VAL) || val_is(v, OP_NONE);
}

/* hcomp at data: u0 when u0 is a constant with no fields and each branch
   of side at a fresh j is conv to u0, else stuck. */
static const Value *hcomp_data(Machine *m, const Value *const *a, uint32_t n) {
  const Value *point;
  const Value *sys;
  if (!fieldless(a[3]))
    return stuck_hcomp(m, a, n);
  point = val_var(m, m->fresh_level);
  sys = point == NULL ? NULL : apply_value(m, a[2], point);
  if (sys == NULL)
    return NULL;
  for (; val_is(sys, OP_SYSTEM); sys = sys->args[2])
    if (!conv_values(m, m->fresh_level, sys->args[1], a[3]))
      return stuck_hcomp(m, a, n);
  return val_is(sys, OP_SYSNIL) ? a[3] : stuck_hcomp(m, a, n);
}

/* Env [A, phi, side, u0].
   fun x => hcomp (TCOD A x) phi (fun j => SYSMAP (fun b => b x) (side j)) (u0 x). */
static const Core *tmpl_hcomp_pi(Machine *m) {
  const Core *at = tmpl_lam(m, "b", tmpl_app(m, tmpl_var(m, 0), tmpl_var(m, 2u)));
  const Core *side = tmpl_lam(m, "j", tmpl_op(m, OP_SYSMAP, 0, 2u, at, tmpl_app(m, tmpl_var(m, 3u), tmpl_var(m, 0)), NULL));
  const Core *cod = tmpl_op(m, OP_TCOD, 0, 2u, tmpl_var(m, 4u), tmpl_var(m, 0), NULL);
  return tmpl_lam(m, "x", tmpl_op4(m, OP_HCOMP, cod, tmpl_var(m, 3u), side, tmpl_app(m, tmpl_var(m, 1u), tmpl_var(m, 0))));
}

/* q @ i at the type A = PathP P a b (index t): PAPP [q, i, TPART2 A, TPART3 A]. */
static const Core *tmpl_papp_at(Machine *m, const Core *q, uint32_t i, uint32_t t) {
  const Core *a = tmpl_op(m, OP_TPART, 2u, 1u, tmpl_var(m, t), NULL, NULL);
  const Core *b = tmpl_op(m, OP_TPART, 3u, 1u, tmpl_var(m, t), NULL, NULL);
  return tmpl_op4(m, OP_PAPP, q, tmpl_var(m, i), a, b);
}

/* Env [A, phi, side, u0], A = PathP P a b.
   <i> hcomp (P i) (~ i \/ i \/ phi)
     (fun j => [ ~ i |-> a , i |-> b , SYSMAP (fun q => q @ i) (side j) ]) (u0 @ i). */
static const Core *tmpl_hcomp_path(Machine *m) {
  /* Under i and j: j 0, i 1, u0 2, side 3, phi 4, A 5. q adds 1. */
  const Core *at = tmpl_lam(m, "q", tmpl_papp_at(m, tmpl_var(m, 0), 2u, 6u));
  const Core *rest = tmpl_op(m, OP_SYSMAP, 0, 2u, at, tmpl_app(m, tmpl_var(m, 3u), tmpl_var(m, 0)), NULL);
  const Core *ends = tmpl_op(m, OP_SYSTEM, 0, 3u, tmpl_var(m, 1u), tmpl_op(m, OP_TPART, 3u, 1u, tmpl_var(m, 5u), NULL, NULL), rest);
  const Core *not_i = tmpl_op(m, OP_INEG, 0, 1u, tmpl_var(m, 1u), NULL, NULL);
  const Core *side = tmpl_lam(m, "j", tmpl_op(m, OP_SYSTEM, 0, 3u, not_i, tmpl_op(m, OP_TPART, 2u, 1u, tmpl_var(m, 5u), NULL, NULL), ends));
  /* Under i: i 0, u0 1, side 2, phi 3, A 4. */
  const Core *line = tmpl_app(m, tmpl_op(m, OP_TPART, 1u, 1u, tmpl_var(m, 4u), NULL, NULL), tmpl_var(m, 0));
  const Core *both = tmpl_op(m, OP_IMAX, 0, 2u, tmpl_op(m, OP_INEG, 0, 1u, tmpl_var(m, 0), NULL, NULL), tmpl_var(m, 0), NULL);
  const Core *phi = tmpl_op(m, OP_IMAX, 0, 2u, both, tmpl_var(m, 3u), NULL);
  const Core *body = tmpl_op4(m, OP_HCOMP, line, phi, side, tmpl_papp_at(m, tmpl_var(m, 1u), 0, 4u));
  return tmpl_op(m, OP_PLAM, 0, 1u, tmpl_lam(m, "i", body), NULL, NULL);
}

/* hcomp at a Glue type: glue of the hcomp at each T, over the hcomp at A
   of the unglued sides and of w applied to the filler at each T, so the
   boundary holds by the filler at i1. */
static const Value *hcomp_glue(Machine *m, const Value *const *a, uint32_t n) {
  const Value *vals[7 + 5 * GLUE_BRANCH_MAX];
  const Value *sys;
  uint32_t lvl = m->fresh_level;
  const Value *point = val_var(m, lvl);
  const Value *sidev = point == NULL ? NULL : apply_value(m, a[2], point);
  const Value *r;
  const Core *tsys;
  const Core *asys;
  const Core *t;
  uint32_t k = 0;
  uint32_t nv;
  uint32_t p;
  if (sidev == NULL)
    return NULL;
  vals[0] = a[0]->args[0];
  vals[1] = a[0]->args[1];
  vals[2] = a[0]->args[2];
  vals[3] = a[1];
  vals[4] = a[2];
  vals[5] = a[3];
  /* Split each branch into meets and restrict u0 and the sides there.
     Their systems may reorder or subdivide the type's cells. */
  for (sys = a[0]->args[2]; val_is(sys, OP_SYSTEM); sys = sys->args[2]) {
    DnfCtx ctx;
    Dnf faces;
    uint32_t j;
    dnf_ctx(&ctx, m, m->fresh_level);
    if (!face_norm(&ctx, sys->args[0], &faces))
      return stuck_hcomp(m, a, n);
    for (j = 0; j < faces.count; j++) {
      const Value *face = faces.len[j] == 0 ? constant(m, OP_I1) : conj_value(&ctx, faces.lit[j], faces.len[j]);
      const Value *sidek = value_under_face(m, m->fresh_level, face, sidev);
      if (k == GLUE_BRANCH_MAX || sidek == NULL)
        return stuck_hcomp(m, a, n);
      vals[6 + 5 * k] = face;
      vals[7 + 5 * k] = value_under_face(m, m->fresh_level, face, witness_of(m, sys->args[1]));
      vals[8 + 5 * k] = value_under_face(m, m->fresh_level, face, witness_of(m, payload_of(m, sys->args[1])));
      vals[9 + 5 * k] = value_under_face(m, m->fresh_level, face, a[3]);
      vals[10 + 5 * k] = line_of(m, lvl, sidek);
      if (vals[7 + 5 * k] == NULL || vals[8 + 5 * k] == NULL || vals[9 + 5 * k] == NULL || vals[10 + 5 * k] == NULL)
        return stuck_hcomp(m, a, n);
      k++;
    }
  }
  /* A system with a neutral tail has cells we cannot see: stuck. */
  if (!val_is(sys, OP_SYSNIL))
    return stuck_hcomp(m, a, n);
  vals[6u + 5u * k] = a[0];
  nv = 7u + 5u * k;
  tsys = tmpl_const(m, OP_SYSNIL);
  /* Under i: the sides on phi, unglued, as a system map. */
  asys = tmpl_op(m, OP_SYSMAP, 0, 2,
                 tmpl_lam(m, "g", tmpl_op4(m, OP_UNGLUE, tmpl_at(m, nv, 0, 2), tmpl_at(m, nv, 1, 2), tmpl_at(m, nv, 2, 2), tmpl_var(m, 0))),
                 tmpl_app(m, tmpl_at(m, nv, 4, 1), tmpl_var(m, 0)), NULL);
  for (p = k; p > 0; p--) {
    uint32_t b = 6u + 5u * (p - 1u);
    /* Under i and l: hfill T phi side u0 i = hcomp T (phi \/ ~ i) (fun l => [ ~ i |-> u0 ] ++ side (l /\ i)) u0. */
    const Core *hfill = tmpl_op4(m, OP_HCOMP, tmpl_at(m, nv, b + 1u, 1), tmpl_imax(m, tmpl_at(m, nv, 3, 1), tmpl_neg(m, tmpl_var(m, 0))),
                                 tmpl_lam(m, "l", tmpl_system(m, tmpl_neg(m, tmpl_var(m, 1)), tmpl_at(m, nv, b + 3u, 2),
                                                              tmpl_app(m, tmpl_at(m, nv, b + 4u, 2), tmpl_op(m, OP_IMIN, 0, 2, tmpl_var(m, 0), tmpl_var(m, 1), NULL)))),
                                 tmpl_at(m, nv, b + 3u, 1));
    tsys = tmpl_system(m, tmpl_at(m, nv, b, 0),
                       tmpl_op4(m, OP_HCOMP, tmpl_at(m, nv, b + 1u, 0), tmpl_at(m, nv, 3, 0), tmpl_at(m, nv, b + 4u, 0), tmpl_at(m, nv, b + 3u, 0)), tsys);
    asys = tmpl_system(m, tmpl_at(m, nv, b, 1), tmpl_app(m, tmpl_at(m, nv, b + 2u, 1), hfill), asys);
  }
  t = tmpl_op(m, OP_GLUE_ELEM, 0, 3, tmpl_at(m, nv, nv - 1u, 0), tsys,
              tmpl_op4(m, OP_HCOMP, tmpl_at(m, nv, 0, 0), tmpl_imax(m, tmpl_at(m, nv, 3, 0), tmpl_at(m, nv, 1, 0)), tmpl_lam(m, "i", asys),
                       tmpl_op4(m, OP_UNGLUE, tmpl_at(m, nv, 0, 0), tmpl_at(m, nv, 1, 0), tmpl_at(m, nv, 2, 0), tmpl_at(m, nv, 5, 0))));
  r = tmpl_run(m, t, vals, nv);
  return r == NULL ? stuck_hcomp(m, a, n) : r;
}

/* hcomp at a type former A. */
static const Value *hcomp_op(Machine *m, const Value *const *a, uint32_t n) {
  switch (a[0]->op) {
  case OP_NAT:
  case OP_BOOL:
  case OP_UNIT:
  case OP_LEAF:
  case OP_OPTION:
  case OP_FAMILY:
    return hcomp_data(m, a, n);
  case OP_PATHP:
    return tmpl_run(m, tmpl_hcomp_path(m), a, 4u);
  case OP_GLUE:
    return hcomp_glue(m, a, n);
  /* The others are not types of Type n. */
  case OP_UNIT_VAL:
  case OP_TRUE:
  case OP_FALSE:
  case OP_NONE:
  case OP_SOME:
  case OP_PACK:
  case OP_CTOR:
  case OP_OPTION_ELIM:
  case OP_PURE:
  case OP_MAP:
  case OP_BIND:
  case OP_FOLD_NAT:
  case OP_FOLD_FAMILY:
  case OP_WITNESS:
  case OP_PAYLOAD:
  case OP_NAT_ADD:
  case OP_NAT_SUB:
  case OP_NAT_MUL:
  case OP_NAT_EQ:
  case OP_NAT_LE:
  case OP_FLAG_IF:
  case OP_PROJ:
  case OP_SSET:
  case OP_I:
  case OP_I0:
  case OP_I1:
  case OP_PLAM:
  case OP_PAPP:
  case OP_INEG:
  case OP_IMIN:
  case OP_IMAX:
  case OP_PARTIAL:
  case OP_SYSTEM:
  case OP_SYSNIL:
  case OP_FROMPARTIAL:
  case OP_TRANSP:
  case OP_TPART:
  case OP_TCOD:
  case OP_HCOMP:
  case OP_SYSMAP:
  case OP_GLUE_ELEM:
  case OP_UNGLUE:
    break;
  }
  return stuck_hcomp(m, a, n);
}

/* Env [A, phi, side, u0], A a Sigma. The witness filler (hfill):
   v = fun i => hcomp (TPART0 A) (phi \/ ~ i)
     (fun j => [ ~ i |-> witness u0 , SYSMAP witness (side (i /\ j)) ]) (witness u0). */
static const Core *tmpl_sigma_v(Machine *m) {
  /* Under i and j: j 0, i 1, u0 2, side 3. b adds 1. */
  const Core *wit = tmpl_lam(m, "b", tmpl_op(m, OP_WITNESS, 0, 1u, tmpl_var(m, 0), NULL, NULL));
  const Core *rest = tmpl_op(m, OP_SYSMAP, 0, 2u, wit, tmpl_app(m, tmpl_var(m, 3u), tmpl_op(m, OP_IMIN, 0, 2u, tmpl_var(m, 1u), tmpl_var(m, 0), NULL)), NULL);
  const Core *cell = tmpl_op(m, OP_SYSTEM, 0, 3u, tmpl_op(m, OP_INEG, 0, 1u, tmpl_var(m, 1u), NULL, NULL), tmpl_op(m, OP_WITNESS, 0, 1u, tmpl_var(m, 2u), NULL, NULL), rest);
  /* Under i: i 0, u0 1, side 2, phi 3, A 4. */
  const Core *phi = tmpl_op(m, OP_IMAX, 0, 2u, tmpl_var(m, 3u), tmpl_op(m, OP_INEG, 0, 1u, tmpl_var(m, 0), NULL, NULL), NULL);
  const Core *ty = tmpl_op(m, OP_TPART, 0, 1u, tmpl_var(m, 4u), NULL, NULL);
  return tmpl_lam(m, "i", tmpl_op4(m, OP_HCOMP, ty, phi, tmpl_lam(m, "j", cell), tmpl_op(m, OP_WITNESS, 0, 1u, tmpl_var(m, 1u), NULL, NULL)));
}

/* Env [A, phi, side, u0, v]. The payload line fun i => TCOD A (v i). */
static const Core *tmpl_sigma_line(Machine *m) {
  return tmpl_lam(m, "i", tmpl_op(m, OP_TCOD, 0, 2u, tmpl_var(m, 5u), tmpl_app(m, tmpl_var(m, 1u), tmpl_var(m, 0)), NULL));
}

/* Env [A, phi, side, u0, v]. The payload side fun j => SYSMAP payload (side j). */
static const Core *tmpl_sigma_side(Machine *m) {
  const Core *pay = tmpl_lam(m, "b", tmpl_op(m, OP_PAYLOAD, 0, 1u, tmpl_var(m, 0), NULL, NULL));
  return tmpl_lam(m, "j", tmpl_op(m, OP_SYSMAP, 0, 2u, pay, tmpl_app(m, tmpl_var(m, 3u), tmpl_var(m, 0)), NULL));
}

/* hcomp at a Sigma A: pack (v i1) (comp (fun i => TCOD A (v i)) phi
   (fun j => SYSMAP payload (side j)) (payload u0)), v the witness filler. */
static const Value *hcomp_sigma(Machine *m, const Value *const *a) {
  const Value *env[5];
  const Value *out[2];
  env[0] = a[0];
  env[1] = a[1];
  env[2] = a[2];
  env[3] = a[3];
  env[4] = tmpl_run(m, tmpl_sigma_v(m), a, 4u);
  out[0] = env[4];
  out[1] = env[4] == NULL ? NULL : comp_value(m, tmpl_run(m, tmpl_sigma_line(m), env, 5u), a[1], tmpl_run(m, tmpl_sigma_side(m), env, 5u), tmpl_run(m, tmpl_op(m, OP_PAYLOAD, 0, 1u, tmpl_var(m, 1u), NULL, NULL), env, 5u));
  if (out[1] == NULL)
    return NULL;
  return tmpl_run(m, tmpl_op(m, OP_PACK, 0, 2u, tmpl_app(m, tmpl_var(m, 1u), tmpl_op(m, OP_I1, 0, 0, NULL, NULL, NULL)), tmpl_var(m, 0), NULL), out, 2u);
}

/* hcomp A phi side u0: the side at i1 when the face phi holds, else by
   the head of A. The universe stays stuck (no hcomp_univ). */
static const Value *reduce_hcomp(Machine *m, const Value *const *a, uint32_t n) {
  const Value *point;
  const Value *top;
  if (face_holds(m, 0, a[1])) {
    point = constant(m, OP_I1);
    top = point == NULL ? NULL : apply_value(m, a[2], point);
    return top == NULL ? NULL : reduce_from_partial(m, &top, 1u);
  }
  switch (a[0]->kind) {
  case VAL_TRAP:
    return a[0];
  case VAL_PI:
    return tmpl_run(m, tmpl_hcomp_pi(m), a, 4u);
  case VAL_OP:
    return hcomp_op(m, a, n);
  case VAL_SIGMA:
    return hcomp_sigma(m, a);
  case VAL_UNIV:
  case VAL_NAT:
  case VAL_LAM:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  return stuck_hcomp(m, a, n);
}

/* Compares two interval terms with one atom table: two reductions can meet
   the neutral atoms in different orders. -1: a term is outside the bounds,
   so the caller compares the structure. */
static int conv_interval(Machine *m, uint32_t level, const Value *a, const Value *b) {
  DnfCtx ctx;
  Dnf x;
  Dnf y;
  Dnf nx;
  Dnf ny;
  dnf_ctx(&ctx, m, level);
  if (!dnf_of(&ctx, a, &x) || !dnf_of(&ctx, b, &y) || !dnf_norm(&x, &nx) || !dnf_norm(&y, &ny))
    return -1;
  return dnf_same(&nx, &ny);
}

/* 1 when both arguments are numbers. Else *OUT is the trap or the stuck
   operation. */
static int nat_args(Machine *m, Op op, const Value *const *a, uint32_t n, uint64_t *x, uint64_t *y, const Value **out) {
  *out = a[0]->kind == VAL_TRAP ? a[0] : a[1];
  if (a[0]->kind == VAL_TRAP || a[1]->kind == VAL_TRAP)
    return 0;
  if (blocked(m, a[0], op, 0, 0, a, n, out) || blocked(m, a[1], op, 0, 0, a, n, out))
    return 0;
  if (a[0]->kind != VAL_NAT || a[1]->kind != VAL_NAT) {
    *out = internal(m, "a Nat operation on a value that is not a Nat");
    return 0;
  }
  *x = a[0]->nat;
  *y = a[1]->nat;
  return 1;
}

static const Value *nat_add(Machine *m, const Value *const *a, uint32_t n) {
  const Value *out;
  uint64_t x = 0;
  uint64_t y = 0;
  if (!nat_args(m, OP_NAT_ADD, a, n, &x, &y, &out))
    return out;
  return x + y < x ? overflow(m) : val_nat(m, x + y);
}

static const Value *nat_sub(Machine *m, const Value *const *a, uint32_t n) {
  const Value *out;
  uint64_t x = 0;
  uint64_t y = 0;
  if (!nat_args(m, OP_NAT_SUB, a, n, &x, &y, &out))
    return out;
  return val_nat(m, x < y ? 0 : x - y);
}

static const Value *nat_mul(Machine *m, const Value *const *a, uint32_t n) {
  const Value *out;
  uint64_t x = 0;
  uint64_t y = 0;
  if (!nat_args(m, OP_NAT_MUL, a, n, &x, &y, &out))
    return out;
  return y != 0 && x > UINT64_MAX / y ? overflow(m) : val_nat(m, x * y);
}

static const Value *nat_test(Machine *m, Op op, const Value *const *a, uint32_t n) {
  const Value *out;
  uint64_t x = 0;
  uint64_t y = 0;
  if (!nat_args(m, op, a, n, &x, &y, &out))
    return out;
  return flag(m, op == OP_NAT_EQ ? x == y : x <= y);
}

static const Value *reduce_flag_if(Machine *m, const Value *const *a, uint32_t n) {
  const Value *out;
  if (blocked(m, a[0], OP_FLAG_IF, 0, 0, a, n, &out))
    return out;
  if (val_is(a[0], OP_TRUE))
    return a[1];
  if (val_is(a[0], OP_FALSE))
    return a[2];
  return internal(m, "a flagIf of a value that is not a Bool");
}

static const Value *reduce_proj(Machine *m, uint32_t inst, uint32_t field, const Value *const *a, uint32_t n) {
  const Value *x = a[0];
  const Value *out;
  uint32_t at;
  if (blocked(m, x, OP_PROJ, inst, field, a, n, &out))
    return out;
  if (!val_is(x, OP_CTOR) || x->inst != inst || inst >= m->ctor_count || m->ctors[inst].family >= m->family_count)
    return internal(m, "a projection of a value of the wrong family");
  at = m->families[m->ctors[inst].family].param_count + field;
  return at < x->argc ? x->args[at] : internal(m, "a projection of a missing field");
}

static uint32_t varied_arity(const Machine *m, Op op, uint32_t inst) {
  const CtorInfo *c = inst < m->ctor_count ? &m->ctors[inst] : NULL;
  const FamilyInfo *f = inst < m->family_count ? &m->families[inst] : NULL;
  if (op == OP_CTOR && c != NULL && c->family < m->family_count)
    return m->families[c->family].param_count + c->field_count;
  if (op == OP_FAMILY && f != NULL)
    return f->param_count;
  if (op == OP_FOLD_FAMILY && f != NULL)
    return f->ctor_count + 1u;
  return UINT32_MAX;
}

static int arity_ok(const Machine *m, Op op, uint32_t inst, uint32_t n) {
  if ((size_t)op >= sizeof OP_ARITY)
    return 0;
  if (OP_ARITY[op] != VARIES)
    return n == (uint32_t)OP_ARITY[op];
  return n == varied_arity(m, op, inst);
}

/* The branch of a system whose first face holds, else NULL. */
static const Value *held_branch(Machine *m, const Value *sys) {
  return val_is(sys, OP_SYSTEM) && face_holds(m, 0, sys->args[0]) ? sys->args[1] : NULL;
}

/* witness v, with a neutral v left stuck. */
static const Value *witness_of(Machine *m, const Value *v) {
  return val_is(v, OP_PACK) ? v->args[0] : reduce_part(m, OP_WITNESS, &v, 1u, OP_PACK, 0);
}

/* payload v, with a neutral v left stuck. */
static const Value *payload_of(Machine *m, const Value *v) {
  return val_is(v, OP_PACK) ? v->args[1] : reduce_part(m, OP_PAYLOAD, &v, 1u, OP_PACK, 1);
}

/* Glue A φ sys: T of the branch pack T e whose face holds, else a
   canonical type. */
static const Value *reduce_glue(Machine *m, const Value *const *a, uint32_t n) {
  const Value *b = held_branch(m, a[2]);
  return b != NULL ? witness_of(m, b) : val_op(m, VAL_OP, OP_GLUE, 0, 0, a, n);
}

/* glue G sys a: the branch whose face holds, else a canonical value. */
static const Value *reduce_glue_elem(Machine *m, const Value *const *a, uint32_t n) {
  const Value *b = held_branch(m, a[1]);
  return b != NULL ? b : val_op(m, VAL_OP, OP_GLUE_ELEM, 0, 0, a, n);
}

/* unglue A φ sys g: a for g = glue G sys a; on a face that holds, the
   function witness e of the branch pack T e, applied to g; else stuck. */
static const Value *reduce_unglue(Machine *m, const Value *const *a, uint32_t n) {
  const Value *b = held_branch(m, a[2]);
  const Value *out;
  /* On a face that holds, g can be a glue at the branch type. */
  if (b != NULL)
    return apply_value(m, witness_of(m, payload_of(m, b)), a[3]);
  if (val_is(a[3], OP_GLUE_ELEM))
    return a[3]->args[2];
  if (blocked(m, a[3], OP_UNGLUE, 0, 0, a, n, &out))
    return out;
  return val_op(m, VAL_STUCK, OP_UNGLUE, 0, 0, a, n);
}

static const Value *reduce_op(Machine *m, Op op, uint32_t inst, uint32_t field, const Value *const *a, uint32_t n) {
  if (!arity_ok(m, op, inst, n))
    return internal(m, "an operation with the wrong number of arguments");
  switch (op) {
  case OP_NAT:
  case OP_BOOL:
  case OP_UNIT:
  case OP_LEAF:
  case OP_OPTION:
  case OP_FAMILY:
  case OP_UNIT_VAL:
  case OP_TRUE:
  case OP_FALSE:
  case OP_NONE:
  case OP_SOME:
  case OP_PACK:
  case OP_CTOR:
  case OP_SSET:
  case OP_I:
  case OP_I0:
  case OP_I1:
  case OP_PATHP:
  case OP_PLAM:
  case OP_PARTIAL:
  case OP_SYSNIL:
    return val_op(m, VAL_OP, op, inst, field, a, n);
  case OP_PAPP:
    return reduce_papp(m, a, n);
  case OP_INEG:
  case OP_IMIN:
  case OP_IMAX:
    return reduce_interval(m, op, a, n);
  case OP_SYSTEM:
    return reduce_system(m, a, n);
  case OP_FROMPARTIAL:
    return reduce_from_partial(m, a, n);
  case OP_TRANSP:
    return reduce_transp(m, a, n);
  case OP_TPART:
    return reduce_tpart(m, field, a, n);
  case OP_TCOD:
    return reduce_tcod(m, a, n);
  case OP_HCOMP:
    return reduce_hcomp(m, a, n);
  case OP_SYSMAP:
    return reduce_sysmap(m, a, n);
  case OP_GLUE:
    return reduce_glue(m, a, n);
  case OP_GLUE_ELEM:
    return reduce_glue_elem(m, a, n);
  case OP_UNGLUE:
    return reduce_unglue(m, a, n);
  case OP_WITNESS:
    return reduce_part(m, op, a, n, OP_PACK, 0);
  case OP_PAYLOAD:
    return reduce_part(m, op, a, n, OP_PACK, 1);
  case OP_OPTION_ELIM:
    return reduce_option(m, a, n);
  case OP_PURE:
    return reduce_pure(m, inst, a[0]);
  case OP_MAP:
    return reduce_map(m, inst, a, n);
  case OP_BIND:
    return reduce_bind(m, inst, a, n);
  case OP_FOLD_NAT:
    return reduce_fold_nat(m, a, n);
  case OP_FOLD_FAMILY:
    return fold_family(m, inst, a, n - 1u, a[n - 1u]);
  case OP_NAT_ADD:
    return nat_add(m, a, n);
  case OP_NAT_SUB:
    return nat_sub(m, a, n);
  case OP_NAT_MUL:
    return nat_mul(m, a, n);
  case OP_NAT_EQ:
  case OP_NAT_LE:
    return nat_test(m, op, a, n);
  case OP_FLAG_IF:
    return reduce_flag_if(m, a, n);
  case OP_PROJ:
    return reduce_proj(m, inst, field, a, n);
  }
  return internal(m, "an unknown operation");
}

static const Value *eval_op(Machine *m, const Env *env, const Core *c) {
  const Value **args = arena_alloc(m->arena, ((size_t)c->argc + 1u) * sizeof *args);
  uint32_t i;
  if (args == NULL)
    return oom(m);
  for (i = 0; i < c->argc; i++) {
    args[i] = eval_core(m, env, c->args[i]);
    if (args[i] == NULL)
      return NULL;
  }
  return reduce_op(m, c->op, c->inst, c->field, args, c->argc);
}

static const Value *eval_inner(Machine *m, const Env *env, const Core *c) {
  switch (c->kind) {
  case CORE_VAR:
    return env_lookup(m, env, c->index);
  case CORE_GLOBAL:
    return def_value(m, c->index);
  case CORE_NAT:
    return val_nat(m, c->nat);
  case CORE_TRAP:
    return val_trap(m);
  case CORE_UNIV:
    return val_univ(m, c->nat);
  case CORE_LAM:
    return val_binder(m, VAL_LAM, c->mult, c->name, NULL, env, c->b);
  case CORE_PI:
    return val_binder(m, VAL_PI, c->mult, c->name, eval_core(m, env, c->a), env, c->b);
  case CORE_SIGMA:
    return val_binder(m, VAL_SIGMA, c->mult, c->name, eval_core(m, env, c->a), env, c->b);
  case CORE_APP:
    return apply_value(m, eval_core(m, env, c->a), eval_core(m, env, c->b));
  case CORE_OP:
    return eval_op(m, env, c);
  }
  return internal(m, "an unknown core term");
}

const Value *eval_core(Machine *m, const Env *env, const Core *c) {
  const Value *v;
  if (c == NULL || !spend(m, 1) || !enter(m))
    return NULL;
  v = eval_inner(m, env, c);
  m->depth--;
  return v;
}

/* Eta: a function is equal to G when the two agree on a fresh variable. */
static int conv_fun(Machine *m, uint32_t level, const Value *a, const Value *b) {
  const Value *x = val_var(m, level);
  return conv_values(m, level + 1u, apply_value(m, a, x), apply_value(m, b, x));
}

static int conv_binder(Machine *m, uint32_t level, const Value *a, const Value *b) {
  const Value *x = val_var(m, level);
  return conv_values(m, level, a->dom, b->dom) && conv_values(m, level + 1u, closure_apply(m, a, x), closure_apply(m, b, x));
}

static int same_head(const Value *a, const Value *b) {
  return a->op == b->op && a->inst == b->inst && a->field == b->field && a->argc == b->argc;
}

/* Sigma eta: compare a pack with the projections of a neutral. */
static int conv_pair(Machine *m, uint32_t level, const Value *pair, const Value *neutral) {
  return conv_values(m, level, pair->args[0], make(m, VAL_STUCK, OP_WITNESS, 0, 1, neutral, NULL, NULL))
      && conv_values(m, level, pair->args[1], make(m, VAL_STUCK, OP_PAYLOAD, 0, 1, neutral, NULL, NULL));
}

/* Path eta: <i> f i against a neutral q compares f x with q x. The ends
   of q x are fillers: x is not i0 or i1. */
static int conv_plam(Machine *m, uint32_t level, const Value *plam, const Value *neutral) {
  const Value *x = val_var(m, level);
  const Value *args[4];
  args[0] = neutral;
  args[1] = x;
  args[2] = x;
  args[3] = x;
  return conv_values(m, level + 1u, apply_value(m, plam->args[0], x), reduce_papp(m, args, 4u));
}

/* The last argument of an operation is compared in the loop, so a long
   list does not nest. */
/* Glue eta: the base against unglue of the neutral at the stored Glue
   type, then each branch under its face. The unglue lets the base use the
   other eta rules. */
static int conv_glue_elem(Machine *m, uint32_t level, const Value *g, const Value *neutral) {
  const Value *type = g->args[0];
  const Value *args[4];
  const Value *sys;
  if (!val_is(type, OP_GLUE))
    return 0;
  args[0] = type->args[0];
  args[1] = type->args[1];
  args[2] = type->args[2];
  args[3] = neutral;
  if (!conv_values(m, level, g->args[2], reduce_unglue(m, args, 4u)))
    return 0;
  for (sys = g->args[1]; val_is(sys, OP_SYSTEM); sys = sys->args[2])
    if (!spend(m, 1) || !conv_under_face(m, level, sys->args[0], sys->args[1], neutral))
      return 0;
  return 1;
}

static int conv_loop(Machine *m, uint32_t level, const Value *a, const Value *b) {
  uint32_t i;
  while (a != NULL && b != NULL && spend(m, 1)) {
    if (a->kind == VAL_LAM || b->kind == VAL_LAM)
      return conv_fun(m, level, a, b);
    if (val_is(a, OP_PACK) && scrut_of(b) == SCRUT_NEUTRAL)
      return conv_pair(m, level, a, b);
    if (val_is(b, OP_PACK) && scrut_of(a) == SCRUT_NEUTRAL)
      return conv_pair(m, level, b, a);
    if (val_is(a, OP_PLAM) && scrut_of(b) == SCRUT_NEUTRAL)
      return conv_plam(m, level, a, b);
    if (val_is(b, OP_PLAM) && scrut_of(a) == SCRUT_NEUTRAL)
      return conv_plam(m, level, b, a);
    if (val_is(a, OP_GLUE_ELEM) && scrut_of(b) == SCRUT_NEUTRAL)
      return conv_glue_elem(m, level, a, b);
    if (val_is(b, OP_GLUE_ELEM) && scrut_of(a) == SCRUT_NEUTRAL)
      return conv_glue_elem(m, level, b, a);
    if (interval_form(a) || interval_form(b)) {
      int same = conv_interval(m, level, a, b);
      if (same >= 0)
        return same;
    }
    if (a->kind == VAL_STUCK && b->kind == VAL_STUCK && a->op == OP_PAPP && b->op == OP_PAPP) {
      /* The ends come from the types; only p and r count. */
      if (!conv_values(m, level, a->args[0], b->args[0]))
        return 0;
      a = a->args[1];
      b = b->args[1];
      continue;
    }
    if (a->kind != b->kind)
      return 0;
    switch (a->kind) {
    case VAL_NAT:
    case VAL_UNIV:
    case VAL_VAR:
      return a->nat == b->nat;
    case VAL_TRAP:
      return 1;
    case VAL_LAM:
      return 0;
    case VAL_PI:
    case VAL_SIGMA:
      return a->mult == b->mult && conv_binder(m, level, a, b); /* Q-S3-3 (a): no subsumption */
    case VAL_APP:
      if (!conv_values(m, level, a->dom, b->dom))
        return 0;
      a = a->arg;
      b = b->arg;
      break;
    case VAL_OP:
    case VAL_STUCK:
      if (!same_head(a, b))
        return 0;
      if (a->argc == 0)
        return 1;
      for (i = 0; i + 1u < a->argc; i++)
        if (!conv_values(m, level, a->args[i], b->args[i]))
          return 0;
      a = a->args[a->argc - 1u];
      b = b->args[b->argc - 1u];
      break;
    }
  }
  return 0;
}

int conv_values(Machine *m, uint32_t level, const Value *a, const Value *b) {
  int same;
  if (a == NULL || b == NULL || !enter(m))
    return 0;
  same = conv_loop(m, level, a, b);
  m->depth--;
  return same;
}

static Core *new_core(Machine *m, CoreKind kind) {
  Core *c = arena_alloc(m->arena, sizeof *c);
  if (c == NULL) {
    oom(m);
    return NULL;
  }
  c->kind = kind;
  return c;
}

static const Core *core_leaf(Machine *m, CoreKind kind, uint64_t nat) {
  Core *c = new_core(m, kind);
  if (c != NULL)
    c->nat = nat;
  return c;
}

static const Core *quote_var(Machine *m, uint32_t level, const Value *v) {
  Core *c;
  if (v->nat >= level) {
    internal(m, "a variable above its level");
    return NULL;
  }
  c = new_core(m, CORE_VAR);
  if (c != NULL)
    c->index = level - 1u - (uint32_t)v->nat;
  return c;
}

static const Core *quote_app(Machine *m, uint32_t level, const Value *v) {
  const Core *fn = quote_value(m, level, v->dom);
  const Core *arg = quote_value(m, level, v->arg);
  Core *c;
  if (fn == NULL || arg == NULL)
    return NULL;
  c = new_core(m, CORE_APP);
  if (c == NULL)
    return NULL;
  c->a = fn;
  c->b = arg;
  return c;
}

static const Core *quote_binder(Machine *m, uint32_t level, const Value *v, CoreKind kind) {
  const Core *dom = kind == CORE_LAM ? NULL : quote_value(m, level, v->dom);
  const Core *body = quote_value(m, level + 1u, closure_apply(m, v, val_var(m, level)));
  Core *c;
  if (body == NULL || (dom == NULL && kind != CORE_LAM))
    return NULL;
  c = new_core(m, kind);
  if (c == NULL)
    return NULL;
  c->name = v->name;
  c->mult = v->mult;
  c->a = dom;
  c->b = body;
  return c;
}

static const Core *quote_op(Machine *m, uint32_t level, const Value *v) {
  const Core **args = arena_alloc(m->arena, ((size_t)v->argc + 1u) * sizeof *args);
  Core *c;
  uint32_t i;
  if (args == NULL) {
    oom(m);
    return NULL;
  }
  for (i = 0; i < v->argc; i++) {
    args[i] = quote_value(m, level, v->args[i]);
    if (args[i] == NULL)
      return NULL;
  }
  c = new_core(m, CORE_OP);
  if (c == NULL)
    return NULL;
  c->op = v->op;
  c->inst = v->inst;
  c->field = v->field;
  c->args = args;
  c->argc = v->argc;
  return c;
}

static const Core *quote_inner(Machine *m, uint32_t level, const Value *v) {
  switch (v->kind) {
  case VAL_NAT:
    return core_leaf(m, CORE_NAT, v->nat);
  case VAL_TRAP:
    return core_leaf(m, CORE_TRAP, 0);
  case VAL_UNIV:
    return core_leaf(m, CORE_UNIV, v->nat);
  case VAL_VAR:
    return quote_var(m, level, v);
  case VAL_APP:
    return quote_app(m, level, v);
  case VAL_LAM:
    return quote_binder(m, level, v, CORE_LAM);
  case VAL_PI:
    return quote_binder(m, level, v, CORE_PI);
  case VAL_SIGMA:
    return quote_binder(m, level, v, CORE_SIGMA);
  case VAL_OP:
  case VAL_STUCK:
    return quote_op(m, level, v);
  }
  internal(m, "an unknown value");
  return NULL;
}

const Core *quote_value(Machine *m, uint32_t level, const Value *v) {
  const Core *c;
  if (v == NULL || !enter(m))
    return NULL;
  c = quote_inner(m, level, v);
  m->depth--;
  return c;
}

const char *op_name(const Machine *m, Op op, uint32_t inst, uint32_t field) {
  if (op == OP_FAMILY)
    return inst < m->family_count ? m->families[inst].name : "?";
  if (op == OP_CTOR)
    return inst < m->ctor_count ? m->ctors[inst].name : "?";
  if (op == OP_PROJ)
    return inst < m->ctor_count && field < m->ctors[inst].field_count ? m->ctors[inst].fields[field].name : "?";
  return (size_t)op < sizeof OP_NAMES / sizeof OP_NAMES[0] ? OP_NAMES[op] : "?";
}

/* The printer keeps len + 4 <= cap, so "..." always fits. */
typedef struct {
  Machine *m;
  char *buf;
  size_t cap;
  size_t len;
  int full;
  const char *const *names;
  uint32_t name_count;
  const char *extra[PRINT_NAMES_MAX];
  uint32_t extra_count;
  unsigned depth;
} Printer;

static void put(Printer *p, const char *s) {
  size_t n = strlen(s);
  if (p->full)
    return;
  if (p->len + n + 4u <= p->cap) {
    memcpy(p->buf + p->len, s, n + 1u);
    p->len += n;
    return;
  }
  memcpy(p->buf + p->len, "...", 4u);
  p->len += 3u;
  p->full = 1;
}

static const char *var_name(const Printer *p, uint64_t level) {
  if (level < p->name_count && p->names[level] != NULL)
    return p->names[level];
  if (level >= p->name_count && level - p->name_count < p->extra_count
      && level - p->name_count < PRINT_NAMES_MAX)
    return p->extra[level - p->name_count];
  return "_";
}

static void print_value(Printer *p, const Value *v, int atom);

static int is_chain(const Value *v) {
  return v != NULL && (v->kind == VAL_OP || v->kind == VAL_STUCK) && v->argc > 0;
}

/* A printed binder must not capture another variable or a global name. */
static int name_used(const Printer *p, const char *name) {
  uint32_t i;
  uint32_t j;
  if (name[0] == '%')
    return 1; /* checker-generated eta names are not surface identifiers */
  for (i = 0; i < p->name_count; i++)
    if (p->names[i] != NULL && strcmp(name, p->names[i]) == 0)
      return 1;
  for (i = 0; i < p->extra_count && i < PRINT_NAMES_MAX; i++)
    if (strcmp(name, p->extra[i]) == 0)
      return 1;
  for (i = 0; i < sizeof OP_NAMES / sizeof OP_NAMES[0]; i++)
    if (strcmp(name, OP_NAMES[i]) == 0)
      return 1;
  for (i = 0; i < p->m->def_count; i++)
    if (strcmp(name, p->m->defs[i].name) == 0)
      return 1;
  for (i = 0; i < p->m->family_count; i++)
    if (strcmp(name, p->m->families[i].name) == 0)
      return 1;
  for (i = 0; i < p->m->ctor_count; i++) {
    const CtorInfo *c = &p->m->ctors[i];
    if (strcmp(name, c->name) == 0)
      return 1;
    for (j = 0; j < c->field_count; j++)
      if (strcmp(name, c->fields[j].name) == 0)
        return 1;
  }
  return 0;
}

static void print_binder(Printer *p, const Value *v) {
  uint32_t level = p->name_count + p->extra_count;
  const char *name = v->name != NULL ? v->name : "x";
  char fresh[32];
  uint32_t suffix = 0;
  const Value *body = closure_apply(p->m, v, val_var(p->m, level));
  int binder_dom = v->dom != NULL && (v->dom->kind == VAL_PI || v->dom->kind == VAL_SIGMA || v->dom->kind == VAL_LAM || v->dom->kind == VAL_UNIV);
  while (name_used(p, name)) {
    snprintf(fresh, sizeof fresh, "_x%u", suffix++);
    name = fresh;
  }
  switch (v->kind) {
  case VAL_LAM:
    put(p, "fun ");
    put(p, name);
    put(p, " => ");
    break;
  case VAL_PI:
    if (v->name == NULL && v->mult != MULT_ZERO) {
      print_value(p, v->dom, binder_dom);
      put(p, v->mult == MULT_ONE ? " -o " : " -> ");
      break;
    }
    put(p, "(");
    put(p, mult_mark(v->mult));
    put(p, v->name == NULL ? "_" : name);
    put(p, " : ");
    print_value(p, v->dom, 0);
    put(p, ") -> ");
    break;
  case VAL_SIGMA:
    put(p, "Sigma (");
    put(p, mult_mark(v->mult));
    put(p, name);
    put(p, " : ");
    print_value(p, v->dom, 0);
    put(p, ") ");
    break;
  case VAL_NAT:
  case VAL_TRAP:
  case VAL_UNIV:
  case VAL_OP:
  case VAL_VAR:
  case VAL_APP:
  case VAL_STUCK:
    break;
  }
  if (p->extra_count < PRINT_NAMES_MAX)
    p->extra[p->extra_count] = name;
  p->extra_count++;
  print_value(p, body, v->kind == VAL_SIGMA);
  p->extra_count--;
}

/* A path lambda prints as its function and a path application p r as p
   and r: the ends come from the type. ~ r, r ∧ s and r ∨ s print in the
   surface form. A system prints as [ φ ↦ u , ψ ↦ v ] (print_system).
   Glue and hcomp omit their internal type and face arguments. */
static int special_form(const Value *v) {
  return interval_form(v) || (v != NULL && (v->kind == VAL_OP || v->kind == VAL_STUCK)
      && (v->op == OP_PLAM || v->op == OP_PAPP || v->op == OP_SYSTEM || v->op == OP_GLUE
          || v->op == OP_GLUE_ELEM || v->op == OP_UNGLUE || v->op == OP_HCOMP));
}

/* 1 when the operand X of OP (on the LEFT or the right) needs parentheses:
   ∧ binds tighter than ∨, and both group to the right. */
static int interval_paren(const Value *x, Op op, int left) {
  if (!interval_form(x) || x->op == OP_INEG)
    return 0;
  return x->op == OP_IMAX ? op == OP_IMIN || left : op == OP_IMIN && left;
}

static void print_special(Printer *p, const Value *v) {
  const Value *head = v->args[0];
  int bare = head->kind == VAL_APP || head->kind == VAL_VAR || (head->kind == VAL_STUCK && head->op == OP_PAPP);
  if (v->op == OP_GLUE || v->op == OP_GLUE_ELEM || v->op == OP_UNGLUE || v->op == OP_HCOMP) {
    uint32_t start = v->op == OP_GLUE ? 0u : v->op == OP_GLUE_ELEM ? 1u : v->op == OP_HCOMP ? 2u : 3u;
    put(p, op_name(p->m, v->op, v->inst, v->field));
    for (uint32_t i = start; i < v->argc; i++) {
      if (v->op == OP_GLUE && i == 1u) continue;
      put(p, " ");
      print_value(p, v->args[i], 1);
    }
    return;
  }
  if (v->op == OP_INEG) {
    put(p, "~ ");
    print_value(p, head, 1);
    return;
  }
  if (v->op == OP_IMIN || v->op == OP_IMAX) {
    print_value(p, head, interval_paren(head, v->op, 1));
    put(p, v->op == OP_IMIN ? " ∧ " : " ∨ ");
    print_value(p, v->args[1], interval_paren(v->args[1], v->op, 0));
    return;
  }
  if (v->op == OP_PLAM) {
    print_value(p, head, 0);
    return;
  }
  print_value(p, head, !bare);
  put(p, " ");
  print_value(p, v->args[1], 1);
}

/* The brackets delimit a system, so it needs no parentheses. A tail
   that is not a system prints as the last item. */
static void print_system(Printer *p, const Value *v) {
  const Value *s;
  put(p, "[ ");
  for (s = v; val_is(s, OP_SYSTEM); s = s->args[2]) {
    if (s != v)
      put(p, " , ");
    print_value(p, s->args[0], 0);
    put(p, " ↦ ");
    print_value(p, s->args[1], 0);
  }
  if (!val_is(s, OP_SYSNIL)) {
    put(p, " , ");
    print_value(p, s, 1);
  }
  put(p, " ]");
}

static void print_other(Printer *p, const Value *v, int atom) {
  char num[32];
  if (v == NULL) {
    put(p, "?");
    return;
  }
  switch (v->kind) {
  case VAL_NAT:
    snprintf(num, sizeof num, "%llu", (unsigned long long)v->nat);
    put(p, num);
    return;
  case VAL_TRAP:
    put(p, "trap");
    return;
  case VAL_VAR:
    put(p, var_name(p, v->nat));
    return;
  case VAL_OP:
  case VAL_STUCK:
    if (v->op == OP_SYSTEM) {
      print_system(p, v);
      return;
    }
    if (!special_form(v)) {
      put(p, op_name(p->m, v->op, v->inst, v->field));
      return;
    }
    break;
  case VAL_UNIV:
  case VAL_APP:
  case VAL_LAM:
  case VAL_PI:
  case VAL_SIGMA:
    break;
  }
  if (atom)
    put(p, "(");
  switch (v->kind) {
  case VAL_UNIV:
    snprintf(num, sizeof num, "Type %llu", (unsigned long long)v->nat);
    put(p, num);
    break;
  case VAL_APP:
    print_value(p, v->dom, v->dom->kind != VAL_APP && v->dom->kind != VAL_VAR);
    put(p, " ");
    print_value(p, v->arg, 1);
    break;
  case VAL_LAM:
  case VAL_PI:
  case VAL_SIGMA:
    print_binder(p, v);
    break;
  case VAL_NAT:
  case VAL_TRAP:
  case VAL_VAR:
    break;
  case VAL_OP:
  case VAL_STUCK:
    print_special(p, v);
    break;
  }
  if (atom)
    put(p, ")");
}

/* An operation prints as its name and its arguments. The last argument is
   printed in the loop, so a long list does not nest. */
static void print_value(Printer *p, const Value *v, int atom) {
  uint32_t open = 0;
  uint32_t i;
  if (p->depth >= PRINT_DEPTH_MAX) {
    put(p, "...");
    p->full = 1;
    return;
  }
  p->depth++;
  while (is_chain(v) && !special_form(v) && !p->full) {
    if (atom) {
      put(p, "(");
      open++;
    }
    put(p, op_name(p->m, v->op, v->inst, v->field));
    for (i = 0; i + 1u < v->argc; i++) {
      put(p, " ");
      print_value(p, v->args[i], 1);
    }
    put(p, " ");
    v = v->args[v->argc - 1u];
    atom = 1;
  }
  print_other(p, v, atom);
  for (i = 0; i < open; i++)
    put(p, ")");
  p->depth--;
}

int value_print(Machine *m, const char *const *names, uint32_t name_count, const Value *v, char *buf, size_t cap) {
  Printer p;
  if (cap == 0)
    return 0;
  buf[0] = '\0';
  if (cap < 4u)
    return 0;
  memset(&p, 0, sizeof p);
  p.m = m;
  p.buf = buf;
  p.cap = cap;
  p.names = names;
  p.name_count = name_count;
  print_value(&p, v, 0);
  return !p.full && !m->diag->set;
}
