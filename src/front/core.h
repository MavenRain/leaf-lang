/* Core terms, values and the evaluator. SHARED by the tcc kits.

   The checker (front/check.c) writes core terms. Evaluation is normalization
   by evaluation: each value is a normal form. A neutral value (VAL_VAR,
   VAL_APP, VAL_STUCK) is stuck on a variable. A trap (a Nat overflow) is a
   value too. It goes up through each strict position (a Nat operation, the
   scrutinee of an eliminator, the function of an application) and nowhere
   else. So the evaluator and the residual program agree: a trap that no
   strict position reads does not stop the program. */
#ifndef LANG_FRONT_CORE_H
#define LANG_FRONT_CORE_H

#include "front/ast.h"
#include "front/base.h"

#define EVAL_DEPTH_LIMIT 2000u
#define EVAL_FUEL_STEPS 20000000u

/* The core names: type formers, constructors and operations. */
typedef enum {
  OP_NAT,
  OP_BOOL,
  OP_UNIT,
  OP_LEAF, /* the one-time key sort: a resource, no constructor (S3) */
  OP_DIGEST, /* the hash output sort: 32 bytes (S4) */
  OP_LOGW, /* log2 w of WOTS+: 1, 2, 4 or 8 (S4) */
  OP_OPTION,
  OP_FAMILY,
  OP_UNIT_VAL,
  OP_TRUE,
  OP_FALSE,
  OP_NONE,
  OP_SOME,
  OP_PACK,
  OP_CTOR,
  OP_OPTION_ELIM,
  OP_PURE,
  OP_MAP,
  OP_BIND,
  OP_FOLD_NAT,
  OP_FOLD_FAMILY,
  OP_WITNESS,
  OP_PAYLOAD,
  OP_NAT_ADD,
  OP_NAT_SUB,
  OP_NAT_MUL,
  OP_NAT_EQ,
  OP_NAT_LE,
  OP_NAT_DIV,
  OP_NAT_MOD,
  OP_LOGW1,
  OP_LOGW2,
  OP_LOGW4,
  OP_LOGW8,
  OP_LOGW_BITS,
  OP_DIGEST_VAL, /* digest w0 w1 w2 w3: the four words in args */
  OP_DIGEST_EQ,
  OP_DIGIT,
  OP_TH_CHAIN, /* the hash ops (5.4): stuck unless Machine.hash */
  OP_TH_NODE,
  OP_TH_MSG,
  OP_PRF,
  OP_KEYGEN, /* the keystore ops (S5): keygen, publicKey, next, sign, leafHeight */
  OP_PUBLIC_KEY,
  OP_NEXT, /* args: the type Option (Leaf × Keystore), the keystore (Q-S5-17) */
  OP_SIGN,
  OP_LEAF_HEIGHT,
  OP_AND_THEN, /* args: the type B, the tree, the continuation (Q-S5-6 (a)) */
  OP_KEYSTORE_VAL, /* the keystore value: [skSeed, skPrf, pk, i] in args (Q-S5-13) */
  OP_LEAF_VAL, /* the leaf value: [skSeed, skPrf, pk, i] in args (Q-S5-13) */
  OP_FLAG_IF,
  OP_BOOL_ELIM,
  OP_PROJ,
  OP_SSET,  /* the sort of pretypes; no surface name */
  OP_I,     /* the interval */
  OP_I0,
  OP_I1,
  OP_PATHP, /* PathP A a b */
  OP_PLAM,  /* a path lambda: the argument is a function over I */
  OP_PAPP,  /* p r, then the ends a0 and a1 of the type of p */
  OP_INEG,  /* ~ r */
  OP_IMIN,  /* r ∧ s */
  OP_IMAX,  /* r ∨ s */
  OP_PARTIAL,    /* Partial φ A: a pretype */
  OP_SYSTEM,     /* [ φ ↦ u , ... ]: a face, a branch and the other cells */
  OP_SYSNIL,     /* [] */
  OP_FROMPARTIAL, /* fromPartial p, when the face of the type of p holds */
  OP_TRANSP,     /* transp A r u: [line, r, u] */
  OP_TPART,      /* internal: a part of a type, field = the part */
  OP_TCOD,       /* internal: the codomain of a Pi or Sigma at x: [T, x] */
  OP_HCOMP,      /* hcomp A φ side u0: [A, φ, side, u0] */
  OP_SYSMAP,     /* internal: f on each branch of a system: [f, sys] */
  OP_GLUE,       /* Glue A [ φ ↦ pack T e , ... ]: [A, φ, sys] */
  OP_GLUE_ELEM,  /* glue [ φ ↦ t , ... ] a at G = Glue A [ ... ]: [G, sys, a] */
  OP_UNGLUE      /* unglue g at Glue A [ φ ↦ pack T e , ... ]: [A, φ, sys, g] */
} Op;

/* The instance of pure, map and bind (former F5). */
typedef enum {
  CARRIER_OPTION
} Carrier;

typedef enum {
  CORE_VAR,    /* index: de Bruijn index */
  CORE_GLOBAL, /* index: definition number */
  CORE_NAT,    /* nat */
  CORE_TRAP,   /* an overflow; only quote writes it */
  CORE_UNIV,   /* Type nat */
  CORE_LAM,    /* fun name => b */
  CORE_PI,     /* (name : a) -> b */
  CORE_SIGMA,  /* Sigma (name : a) b */
  CORE_APP,    /* a b */
  CORE_OP      /* a core name with all of its arguments */
} CoreKind;

typedef struct Core Core;
struct Core {
  CoreKind kind;
  uint32_t index;
  uint64_t nat;
  Op op;
  /* OP_FAMILY, OP_FOLD_FAMILY: the family. OP_CTOR, OP_PROJ: the
     constructor. OP_PURE, OP_MAP, OP_BIND: the Carrier. */
  uint32_t inst;
  uint32_t field; /* OP_PROJ: the field number */
  const char *name;
  Mult mult; /* LAM, PI, SIGMA: the binder multiplicity */
  const Core *a;
  const Core *b;
  const Core *const *args;
  uint32_t argc;
};

typedef enum {
  VAL_NAT,
  VAL_TRAP,
  VAL_UNIV,
  VAL_LAM,
  VAL_PI,
  VAL_SIGMA,
  VAL_OP,   /* a canonical value or a type */
  VAL_VAR,  /* neutral: a variable at a de Bruijn level */
  VAL_APP,  /* neutral: a neutral function applied to a value */
  VAL_STUCK /* neutral: an operation with a neutral scrutinee */
} ValKind;

typedef struct Value Value;
typedef struct Env Env;

/* The head is de Bruijn index 0. */
struct Env {
  const Value *value;
  const Env *next;
};

struct Value {
  ValKind kind;
  uint64_t nat; /* NAT: the number. UNIV: the level. VAR: the level. */
  Op op;        /* OP, STUCK */
  uint32_t inst;
  uint32_t field;
  const Value *const *args;
  uint32_t argc;
  const char *name; /* LAM, PI, SIGMA: the binder */
  Mult mult;        /* LAM, PI, SIGMA: the binder multiplicity */
  const Value *dom; /* PI, SIGMA: the domain. APP: the function. */
  const Value *arg; /* APP: the argument. A PI with no body: the codomain. */
  const Env *env;   /* LAM, PI, SIGMA: the closure */
  const Core *body;
};

typedef struct {
  const char *name;
  const Core *type; /* in the context of the family parameters */
  int recursive;    /* the field type is the family itself */
} FieldInfo;

typedef struct {
  const char *name;
  uint32_t family;
  const FieldInfo *fields;
  uint32_t field_count;
} CtorInfo;

/* A domain family. Its constructors are ctors[first_ctor ...]. A family
   with one constructor has one projection for each field. */
typedef struct {
  const char *name;
  const Core *const *param_types;
  uint32_t param_count;
  uint32_t first_ctor;
  uint32_t ctor_count;
  uint64_t universe; /* the largest universe of a constructor field */
} FamilyInfo;

typedef struct {
  const char *name;
  Origin origin;
  const Core *body;
  const Value *type;
  const Value *value; /* computed when first used */
  const char *runtime_refusal; /* an erased definition that is unsafe at 1 */
} DefInfo;

/* The checked program and the evaluation state. No global state: every
   function gets this context. */
typedef struct {
  Arena *arena;
  Diag *diag;
  const char *def; /* the current definition, for diagnostics */
  DefInfo *defs;
  uint32_t def_count;
  uint32_t def_cap;
  FamilyInfo *families;
  uint32_t family_count;
  uint32_t family_cap;
  CtorInfo *ctors;
  uint32_t ctor_count;
  uint32_t ctor_cap;
  unsigned depth;
  uint32_t fresh_level; /* above every variable level allocated by this machine */
  uint64_t fuel;
  int overflowed;
  int hash; /* 1: the hash ops compute; only langc eval sets it (Q-S4-3) */
  size_t checked;  /* check_program: the declarations before the current one */
  int decl_failed; /* check_program: 1 when declaration CHECKED failed */
} Machine;

/* Each function returns NULL (or 0) after a diagnostic. A NULL input gives a
   NULL result, so a caller can test once at the end. */
const Value *val_nat(Machine *m, uint64_t n);
const Value *val_var(Machine *m, uint32_t level);
const Value *val_univ(Machine *m, uint64_t level);
const Value *val_op(Machine *m, ValKind kind, Op op, uint32_t inst, uint32_t field, const Value *const *args, uint32_t argc);
const Value *val_make(Machine *m, Op op, uint32_t inst, uint32_t argc, const Value *a, const Value *b, const Value *c);
/* A non-dependent function type. */
const Value *val_arrow(Machine *m, const Value *dom, const Value *cod);
const Value *val_arrow_mult(Machine *m, Mult mult, const Value *dom, const Value *cod);
const Value *val_closure(Machine *m, ValKind kind, const char *name, const Value *dom, const Env *env, const Core *body);
const Value *val_binder(Machine *m, ValKind kind, Mult mult, const char *name, const Value *dom, const Env *env, const Core *body);
int val_is(const Value *v, Op op);
const Env *env_push(Machine *m, const Env *env, const Value *value);
const Value *def_value(Machine *m, uint32_t index);
const Value *eval_core(Machine *m, const Env *env, const Core *core);
const Value *apply_value(Machine *m, const Value *fn, const Value *arg);
/* The body of a LAM, PI or SIGMA value at ARG. */
const Value *closure_apply(Machine *m, const Value *binder, const Value *arg);
/* 1 when A and B have the same normal form. LEVEL is the next free level. */
int conv_values(Machine *m, uint32_t level, const Value *a, const Value *b);
/* Faces are interval terms. face_holds: 1 when PHI holds (a meet with no
   literal). face_equal: 1 when A and B have the same face normal form. */
int face_holds(Machine *m, uint32_t level, const Value *phi);
int face_equal(Machine *m, uint32_t level, const Value *a, const Value *b);
/* The meets of the face normal form of PHI, with the variable literals
   only (an atom drops out). Literal 2k is (k = i1) and 2k + 1 is
   (k = i0). 0 when PHI is not a face in the bounds. */
#define FACE_MEETS_MAX 16u
#define FACE_LITS_MAX 8u
typedef struct {
  uint32_t lit[FACE_MEETS_MAX][FACE_LITS_MAX];
  uint32_t len[FACE_MEETS_MAX];
  uint32_t count;
} FaceMeets;
int face_meets(Machine *m, uint32_t level, const Value *phi, FaceMeets *out);
/* Conversion after the restriction to each meet of PHI. An atom stays
   free, so the test is conservative there. */
int conv_under_face(Machine *m, uint32_t level, const Value *phi, const Value *a, const Value *b);
/* V at the variable ends that are common to every meet of PHI. */
const Value *value_under_face(Machine *m, uint32_t level, const Value *phi, const Value *v);
const Core *quote_value(Machine *m, uint32_t level, const Value *v);
/* Writes V in the surface syntax. NAMES gives the names of the levels below
   NAME_COUNT. Returns 1 for a complete rendering, 0 on truncation or error. */
int value_print(Machine *m, const char *const *names, uint32_t name_count, const Value *v, char *buf, size_t cap);
const char *op_name(const Machine *m, Op op, uint32_t inst, uint32_t field);

#endif
