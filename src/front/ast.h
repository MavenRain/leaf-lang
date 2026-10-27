/* The syntax tree. SHARED by the tcc kits. Terms and types share one syntax. */
#ifndef LANG_FRONT_AST_H
#define LANG_FRONT_AST_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  TERM_VAR,   /* name */
  TERM_NAT,   /* nat */
  TERM_APP,   /* left applied to right */
  TERM_LAM,   /* fun (name : left) => right; left is NULL for a bare binder */
  TERM_PI,    /* (name : left) -> right; name is NULL for left -> right */
  TERM_SIGMA, /* Sigma (name : left) right, or (name : left) * right */
  TERM_UNIV   /* Type nat */
} TermKind;

/* The multiplicity of a binder (S3, Q-S3-1 (a)). A binder with no mark is
   omega. `A ⊸ B` is a TERM_PI at MULT_ONE with no name. */
typedef enum {
  MULT_OMEGA, /* no mark */
  MULT_ZERO,  /* @0 */
  MULT_ONE    /* @1, or the binder of ⊸ */
} Mult;

typedef struct Term Term;
struct Term {
  TermKind kind;
  int line;
  int col;
  const char *name;
  uint64_t nat;
  Mult mult; /* TERM_LAM, TERM_PI and TERM_SIGMA only */
  const Term *left;
  const Term *right;
};

/* A named field of a constructor, or a parameter of a family. */
typedef struct {
  const char *name;
  const Term *type;
  int line;
  int col;
} Field;

typedef struct {
  const char *name;
  const Field *fields;
  size_t field_count;
  int line;
  int col;
} Ctor;

typedef enum {
  DECL_DEF,    /* [rec] name : type, then the clause name args = body */
  DECL_AXIOM,  /* postulate name : type */
  DECL_FAMILY,  /* data name params : Type where, then one constructor a line */
  DECL_COFAMILY /* codata name params : Type where, then one observation a
                   line (S2 U6c1): each Ctor has one Field, the observation
                   type after the self argument */
} DeclKind;

/* A family or a cofamily is legal only in the domain file (rule R1). */
typedef enum {
  ORIGIN_DOMAIN,
  ORIGIN_PROGRAM
} Origin;

typedef struct {
  DeclKind kind;
  Origin origin;
  int is_rec;
  int line;
  int col;
  int end_line; /* the last token of the declaration */
  int end_col;
  const char *name;
  const Term *type;
  const Term *body;
  const Field *params;
  size_t param_count;
  const Ctor *ctors;
  size_t ctor_count;
} Decl;

/* Domain declarations come first, then the program. */
typedef struct {
  Decl *items;
  size_t count;
  size_t cap;
} DeclList;

#endif
