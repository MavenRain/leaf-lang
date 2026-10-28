/* The first-order IR between the front end and a target. SHARED by the tcc
   kits.

   Each value is one unsigned 64-bit word: a Nat, a Bool (0 or 1), a
   constructor tag, or a heap address. Locals are numbered from 0. The
   parameters of an entry are locals 0 to param_count - 1. A heap block is a
   run of words. A target sets its own word width in memory (Wasm: 8 bytes,
   EVM: 32 bytes) and resets its bump allocator at each call.

   Traps are lazy (the H3 ruling, see the kit README): no expression traps.
   The lowering gives each value a word and a trap flag (0 or 1). ADD and MUL
   wrap; ADD_CARRY and MUL_CARRY give the flag of an overflow. A heap block
   is word 0 = the tag, then for field K the word at 1 + 2K and its flag at
   2 + 2K. A Digest is a block of four words with no tag and no flag words:
   word k is bytes 8k to 8k + 7 of the digest, big-endian. Only an entry
   result is a strict position at run time: when its flag is 1, the entry
   runs TRAP. */
#ifndef LANG_IR_H
#define LANG_IR_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  IR_SCALAR_NAT,
  IR_SCALAR_BOOL
} IrScalar;

typedef enum {
  IR_OP_ADD,       /* the sum modulo 2^64 */
  IR_OP_SUB,       /* truncates at 0 */
  IR_OP_MUL,       /* the product modulo 2^64 */
  IR_OP_EQ,        /* 1 when equal, else 0 */
  IR_OP_LE,        /* 1 when left <= right, else 0 */
  IR_OP_ADD_CARRY, /* 1 when the sum is larger than 2^64 - 1, else 0 */
  IR_OP_MUL_CARRY, /* 1 when the product is larger than 2^64 - 1, else 0 */
  IR_OP_OR,        /* bitwise or (the lowering gives it flags only) */
  IR_OP_DIV,       /* left / right, rounded down (the lowering never gives right = 0) */
  IR_OP_MOD,       /* left modulo right (the lowering never gives right = 0) */
  IR_OP_SHR,       /* left shifted right by right bits (the lowering gives right < 64) */
  IR_OP_AND        /* bitwise and */
} IrOp;

/* The hash of an IR_STMT_HASH (S5): Keccak-256 of the 5.4 input. */
typedef enum {
  IR_HASH_TH_CHAIN, /* thChain seed tweak x */
  IR_HASH_TH_NODE,  /* thNode seed tweak left right */
  IR_HASH_TH_MSG,   /* thMsg r root index message */
  IR_HASH_PRF       /* prf key tweak */
} IrHash;

typedef enum {
  IR_EXPR_CONST,  /* value */
  IR_EXPR_LOCAL,  /* local */
  IR_EXPR_BINARY, /* op left right */
  IR_EXPR_LOAD    /* word number field of the block at address left */
} IrExprKind;

typedef struct IrExpr IrExpr;
struct IrExpr {
  IrExprKind kind;
  uint64_t value;
  uint32_t local;
  IrOp op;
  uint32_t field;
  const IrExpr *left;
  const IrExpr *right;
};

typedef struct IrStmt IrStmt;

typedef struct {
  const IrStmt *const *items;
  size_t count;
} IrBlock;

typedef enum {
  IR_STMT_SET,    /* local := expr */
  IR_STMT_ALLOC,  /* local := address of a new block; word k := fields[k] */
  IR_STMT_HASH,   /* local := address of a new Digest block, the hash over
                     fields in source order (a Digest or a Tweak gives its
                     block address, a Nat its word) */
  IR_STMT_IF,     /* expr != 0: body, else: otherwise */
  IR_STMT_SWITCH, /* run arms[expr]; a value outside 0 to arm_count - 1 traps */
  IR_STMT_REPEAT, /* counter := expr, then run body counter times */
  IR_STMT_WHILE,  /* counter := expr; run body while counter > 0 and local != 0,
                     counter decreasing by 1 at each run; body sets local */
  IR_STMT_TRAP,   /* stop the call: Wasm unreachable, EVM REVERT */
  IR_STMT_RETURN  /* return expr */
} IrStmtKind;

struct IrStmt {
  IrStmtKind kind;
  uint32_t local;
  uint32_t counter;
  const IrExpr *expr;
  const IrExpr *const *fields;
  size_t field_count;
  IrBlock body;
  IrBlock otherwise;
  const IrBlock *arms;
  size_t arm_count;
  IrHash hash; /* IR_STMT_HASH only */
  uint32_t flag_local; /* IR_STMT_HASH: validation flag, checked when used */
};

/* One exported entry. The body block ends in RETURN or TRAP on each path. */
typedef struct {
  const char *name;
  const IrScalar *params;
  size_t param_count;
  IrScalar result;
  uint32_t local_count; /* includes the parameters */
  IrBlock body;
} IrFunc;

typedef struct {
  const IrFunc *funcs;
  size_t func_count;
} IrProgram;

#endif
