/* The Wasm back end. It writes a Wasm 1.0 (MVP) binary module directly.

   Module layout of a program (`langc build`):
   - The imports take the first function indices (WASM_IMPORTS of them, 0
     for a program: the import list is empty, Q-S5-32 (a)), so each function
     index below moves up by the import count (Sink.base).
   - Function 0 is the internal allocator `alloc (bytes : i32) -> i64`. It
     bumps the mutable i64 global 0 (the heap pointer) over memory 0. When a
     block does not fit, it grows the memory. When the memory cannot grow (the
     maximum is WASM_MAX_PAGES), it traps.
   - Function 1 is the internal `keccak_f (state : i64)`: the Keccak-f[1600]
     permutation of the 25 words (lanes) of the block at state.
   - Function 2 is the internal `keccak256 (in : i64) (n : i64) (out : i64)`:
     Keccak-256 of the n big-endian words at in (8n bytes). It writes the 32
     bytes of the hash as 4 big-endian words at out. Every module has the
     WASM_INTERNAL functions, also a module with no hash node.
   - Function WASM_INTERNAL + K is IR entry K, exported as its name. Each
     parameter and the result is an i64. Each entry first resets the heap
     pointer to 0, and it traps on a Bool argument larger than 1.
   - A word is 8 bytes. Word K of the block at address A is at A + 8K.

   The signer module (`langc signer --height H`, D18 to D21, D27 to D29)
   has no IR. Function 0 is the import env.commit (id, position), so alloc,
   keccak_f and keccak256 are the functions 1 to 3, and the tree and sign
   functions th_chain to root_from_path are the functions 4 to 16
   (SIGNER_FUNCS). The exports are memory, keygen, publicKey, next, sign
   and verify (D31, D33). Bytes 0 to 4095 are the
   host I/O area. keygen sets the heap pointer to 4096, writes the state
   block (D20), sets the mark (global 1) to the heap pointer and writes the
   root by tree_hash (D28). publicKey and next set the heap pointer to the
   mark, not to 0. Only next calls env.commit, and it writes the new
   position only after the import returns. A tree function sets the heap
   pointer back to its value at entry before it returns, so its scratch
   does not stay.

   Each entry has WASM_SCRATCH i64 scratch locals after the IR locals: S and
   S + 1 hold the operands of SUB, ADD_CARRY and MUL_CARRY, and S + 2 holds
   the address of an ALLOC. An operator reads its operands into S and S + 1
   only after both operand expressions are done, so a nested operator cannot
   overwrite them. S + 3 to S + 12 belong to the hash node (emit_hash).

   Each sized part (a section, a function body) is written in two passes: the
   first pass counts the bytes, the second pass writes them after the size.
   So the back end needs no buffer. */
#include <stdint.h>
#include <string.h>

#include "target.h"

#define WASM_MAX_PAGES 16384u /* 1 GiB, the arena limit of langc eval */
#define WASM_DEPTH_MAX 2000u
/* The import count of a program module. The imports are the functions 0 to
   WASM_IMPORTS - 1; alloc and the entries come after them. */
#define WASM_IMPORTS 0u
/* The internal functions after the imports (D11): call_internal (s, K)
   calls the function Sink.base + K. */
#define WASM_FUNC_ALLOC 0u
#define WASM_FUNC_KECCAK_F 1u
#define WASM_FUNC_KECCAK256 2u
/* The tree and sign functions of the signer module only (D29, D34). The
   index is the table order of SIGNER_FUNCS: keep the two in the same
   order. */
#define WASM_FUNC_TH_CHAIN 3u
#define WASM_FUNC_TH_NODE 4u
#define WASM_FUNC_PRF 5u
#define WASM_FUNC_CHAIN 6u
#define WASM_FUNC_WOTS_PK 7u
#define WASM_FUNC_LTREE 8u
#define WASM_FUNC_TREE_HASH 9u
#define WASM_FUNC_TH_MSG 10u
#define WASM_FUNC_WOTS_DIGITS 11u
#define WASM_FUNC_WOTS_SIGN 12u
#define WASM_FUNC_WOTS_PK_FROM_SIG 13u
#define WASM_FUNC_AUTH_PATH 14u
#define WASM_FUNC_ROOT_FROM_PATH 15u
/* The internal functions alloc, keccak_f and keccak256 (D11). Entry K is the
   function WASM_IMPORTS + WASM_INTERNAL + K. */
#define WASM_INTERNAL 3u
/* The i64 scratch locals of an entry, after the IR locals. */
#define WASM_SCRATCH 13u

enum {
  OP_UNREACHABLE = 0x00,
  OP_BLOCK = 0x02,
  OP_LOOP = 0x03,
  OP_IF = 0x04,
  OP_ELSE = 0x05,
  OP_END = 0x0B,
  OP_BR = 0x0C,
  OP_BR_IF = 0x0D,
  OP_BR_TABLE = 0x0E,
  OP_RETURN = 0x0F,
  OP_CALL = 0x10,
  OP_SELECT = 0x1B,
  OP_LOCAL_GET = 0x20,
  OP_LOCAL_SET = 0x21,
  OP_LOCAL_TEE = 0x22,
  OP_GLOBAL_GET = 0x23,
  OP_GLOBAL_SET = 0x24,
  OP_I64_LOAD = 0x29,
  OP_I64_STORE = 0x37,
  OP_MEMORY_SIZE = 0x3F,
  OP_MEMORY_GROW = 0x40,
  OP_I32_CONST = 0x41,
  OP_I64_CONST = 0x42,
  OP_I32_EQ = 0x46,
  OP_I64_EQZ = 0x50,
  OP_I64_EQ = 0x51,
  OP_I64_NE = 0x52,
  OP_I64_LT_U = 0x54,
  OP_I64_GT_U = 0x56,
  OP_I64_LE_U = 0x58,
  OP_I64_GE_U = 0x5A,
  OP_I64_ADD = 0x7C,
  OP_I64_SUB = 0x7D,
  OP_I64_MUL = 0x7E,
  OP_I64_DIV_U = 0x80,
  OP_I64_REM_U = 0x82,
  OP_I64_AND = 0x83,
  OP_I64_OR = 0x84,
  OP_I64_XOR = 0x85,
  OP_I64_SHL = 0x86,
  OP_I64_SHR_U = 0x88,
  OP_I64_ROTL = 0x89,
  OP_I32_WRAP_I64 = 0xA7,
  OP_I64_EXTEND_I32_U = 0xAD,
  TYPE_I32 = 0x7F,
  TYPE_I64 = 0x7E,
  TYPE_FUNC = 0x60,
  TYPE_EMPTY = 0x40,
  ALIGN_WORD = 3
};

enum {
  SECTION_TYPE = 1,
  SECTION_IMPORT = 2,
  SECTION_FUNCTION = 3,
  SECTION_MEMORY = 5,
  SECTION_GLOBAL = 6,
  SECTION_EXPORT = 7,
  SECTION_CODE = 10
};

/* A byte sink. With out == NULL it only counts. */
typedef struct {
  FILE *out;
  size_t count;
  FILE *err;
  uint32_t base; /* the function index of alloc: the import count (D18) */
} Sink;

typedef int (*Part)(Sink *s, const void *arg);

const char target_name[] = "wasm";

static int put_byte(Sink *s, unsigned v) {
  s->count++;
  return s->out == NULL || fputc((int)(v & 0xFFu), s->out) != EOF;
}

static int put_uleb(Sink *s, uint64_t v) {
  int ok = 1;
  for (; ok && v >= 0x80u; v >>= 7) ok = put_byte(s, (unsigned)(v & 0x7Fu) | 0x80u);
  return ok && put_byte(s, (unsigned)v);
}

/* Signed LEB128 of the two's complement value of V. */
static int put_sleb(Sink *s, uint64_t v) {
  uint64_t fill = (v >> 63) != 0 ? ~(~(uint64_t)0 >> 7) : 0;
  unsigned low = (unsigned)(v & 0x7Fu);
  uint64_t rest = (v >> 7) | fill;
  int last = (rest == 0 && (low & 0x40u) == 0) || (rest == ~(uint64_t)0 && (low & 0x40u) != 0);
  int ok = put_byte(s, last ? low : low | 0x80u);
  return ok && (last || put_sleb(s, rest));
}

static int put_name(Sink *s, const char *name) {
  size_t i;
  size_t len = strlen(name);
  int ok = put_uleb(s, len);
  for (i = 0; i < len && ok; i++) ok = put_byte(s, (unsigned char)name[i]);
  return ok;
}

/* Writes the byte size of PART, then PART. */
static int sized(Sink *s, Part part, const void *arg) {
  Sink count = {NULL, 0, s->err, s->base};
  return part(&count, arg) && put_uleb(s, count.count) && part(s, arg);
}

static int section(Sink *s, unsigned id, Part part, const void *arg) {
  return put_byte(s, id) && sized(s, part, arg);
}

static int op2(Sink *s, unsigned op, uint64_t imm) {
  return put_byte(s, op) && put_uleb(s, imm);
}

static int i64_const(Sink *s, uint64_t v) {
  return put_byte(s, OP_I64_CONST) && put_sleb(s, v);
}

static int i32_const(Sink *s, uint64_t v) {
  return put_byte(s, OP_I32_CONST) && put_sleb(s, v);
}

static int memarg(Sink *s, unsigned op, uint32_t word) {
  return put_byte(s, op) && put_uleb(s, ALIGN_WORD) && put_uleb(s, 8u * (uint64_t)word);
}

/* Calls the internal function K (a WASM_FUNC_* index) of the module of S. */
static int call_internal(Sink *s, unsigned k) {
  return op2(s, OP_CALL, (uint64_t)s->base + k);
}

static int deep(Sink *s) {
  fprintf(s->err, "langc: WASM_DEPTH: -: the IR nests deeper than %u levels\n", WASM_DEPTH_MAX);
  return 0;
}

/* An IR node with no Wasm form (Q-S5-29 (a)). The lowering gives no such node
   now: emit_hash gives it only for a hash node whose fields do not match the
   5.4 layout of its hash. */
static int target_node(Sink *s) {
  fprintf(s->err, "langc: REFUSE_TARGET_NODE: -: the Wasm writer has no form for this IR node\n");
  return 0;
}

static int emit_expr(Sink *s, const IrExpr *e, uint32_t t, unsigned depth);

/* The operands are on the stack. Moves them to the scratch locals T, T + 1. */
static int operands(Sink *s, uint32_t t) {
  return op2(s, OP_LOCAL_SET, t + 1u) && op2(s, OP_LOCAL_SET, t);
}

static int emit_op(Sink *s, IrOp op, uint32_t t) {
  switch (op) {
    case IR_OP_ADD:
      return put_byte(s, OP_I64_ADD);
    case IR_OP_SUB: /* select(a - b, 0, a >= b) */
      return operands(s, t) && op2(s, OP_LOCAL_GET, t) && op2(s, OP_LOCAL_GET, t + 1u) && put_byte(s, OP_I64_SUB) &&
             i64_const(s, 0) && op2(s, OP_LOCAL_GET, t) && op2(s, OP_LOCAL_GET, t + 1u) && put_byte(s, OP_I64_GE_U) &&
             put_byte(s, OP_SELECT);
    case IR_OP_MUL:
      return put_byte(s, OP_I64_MUL);
    case IR_OP_EQ:
      return put_byte(s, OP_I64_EQ) && put_byte(s, OP_I64_EXTEND_I32_U);
    case IR_OP_LE:
      return put_byte(s, OP_I64_LE_U) && put_byte(s, OP_I64_EXTEND_I32_U);
    case IR_OP_ADD_CARRY: /* a + b < a, modulo 2^64 */
      return operands(s, t) && op2(s, OP_LOCAL_GET, t) && op2(s, OP_LOCAL_GET, t + 1u) && put_byte(s, OP_I64_ADD) &&
             op2(s, OP_LOCAL_GET, t) && put_byte(s, OP_I64_LT_U) && put_byte(s, OP_I64_EXTEND_I32_U);
    case IR_OP_MUL_CARRY:
      /* Exact: a * b > 2^64 - 1 if and only if b > floor((2^64 - 1) / a), for
         a > 0. For a = 0 the divisor is 1, and no b is larger than 2^64 - 1. */
      return operands(s, t) && op2(s, OP_LOCAL_GET, t + 1u) && i64_const(s, ~(uint64_t)0) && i64_const(s, 1) &&
             op2(s, OP_LOCAL_GET, t) && op2(s, OP_LOCAL_GET, t) && put_byte(s, OP_I64_EQZ) && put_byte(s, OP_SELECT) &&
             put_byte(s, OP_I64_DIV_U) && put_byte(s, OP_I64_GT_U) && put_byte(s, OP_I64_EXTEND_I32_U);
    case IR_OP_OR:
      return put_byte(s, OP_I64_OR);
    case IR_OP_DIV: /* the lowering never gives 0 (i64.div_u traps on 0) */
      return put_byte(s, OP_I64_DIV_U);
    case IR_OP_MOD: /* the lowering never gives 0 (i64.rem_u traps on 0) */
      return put_byte(s, OP_I64_REM_U);
    case IR_OP_SHR:
      return put_byte(s, OP_I64_SHR_U);
    case IR_OP_AND:
      return put_byte(s, OP_I64_AND);
  }
  return 0;
}

static int emit_expr(Sink *s, const IrExpr *e, uint32_t t, unsigned depth) {
  if (depth > WASM_DEPTH_MAX) return deep(s);
  switch (e->kind) {
    case IR_EXPR_CONST:
      return i64_const(s, e->value);
    case IR_EXPR_LOCAL:
      return op2(s, OP_LOCAL_GET, e->local);
    case IR_EXPR_BINARY:
      return emit_expr(s, e->left, t, depth + 1u) && emit_expr(s, e->right, t, depth + 1u) && emit_op(s, e->op, t);
    case IR_EXPR_LOAD:
      return emit_expr(s, e->left, t, depth + 1u) && put_byte(s, OP_I32_WRAP_I64) && memarg(s, OP_I64_LOAD, e->field);
  }
  return 0;
}

static int emit_block(Sink *s, const IrBlock *b, uint32_t t, unsigned depth);

/* local := alloc(8 * n); word k := fields[k]. The address waits in T + 2. */
static int emit_alloc(Sink *s, const IrStmt *st, uint32_t t, unsigned depth) {
  size_t k;
  int ok = i32_const(s, 8u * (uint64_t)st->field_count) && call_internal(s, WASM_FUNC_ALLOC) && op2(s, OP_LOCAL_SET, t + 2u);
  for (k = 0; k < st->field_count && ok; k++) {
    ok = op2(s, OP_LOCAL_GET, t + 2u) && put_byte(s, OP_I32_WRAP_I64) && emit_expr(s, st->fields[k], t, depth + 1u) &&
         memarg(s, OP_I64_STORE, (uint32_t)k);
  }
  return ok && op2(s, OP_LOCAL_GET, t + 2u) && op2(s, OP_LOCAL_SET, st->local);
}

/* The parts of a 5.4 hash input, 4 words each (eval.c hash_part). */
typedef enum {
  PART_PREFIX, /* 0, 0, 0, then the prefix of the hash */
  PART_DIGEST, /* the 4 words of a Digest block */
  PART_TWEAK,  /* the RFC 8391 ADRS of a Tweak */
  PART_NAT     /* 0, 0, 0, then the Nat */
} HashPart;

/* The 5.4 input of a hash: its parts. The parts other than the prefix take
   the IR fields in source order (eval.c reduce_hash). */
typedef struct {
  uint64_t prefix;
  size_t part_count;
  size_t field_count;
  HashPart parts[5];
} HashLayout;

static const HashLayout *hash_layout(IrHash h) {
  static const HashLayout th_chain = {0, 3, 3, {PART_DIGEST, PART_TWEAK, PART_DIGEST}};
  static const HashLayout th_node = {0, 4, 4, {PART_DIGEST, PART_TWEAK, PART_DIGEST, PART_DIGEST}};
  static const HashLayout th_msg = {2, 5, 4, {PART_PREFIX, PART_DIGEST, PART_DIGEST, PART_NAT, PART_DIGEST}};
  static const HashLayout prf = {3, 3, 2, {PART_PREFIX, PART_DIGEST, PART_TWEAK}};
  switch (h) {
    case IR_HASH_TH_CHAIN:
      return &th_chain;
    case IR_HASH_TH_NODE:
      return &th_node;
    case IR_HASH_TH_MSG:
      return &th_msg;
    case IR_HASH_PRF:
      return &prf;
  }
  return NULL;
}

/* Pushes word W of the block at local A. */
static int block_word(Sink *s, uint32_t a, uint32_t w) {
  return op2(s, OP_LOCAL_GET, a) && put_byte(s, OP_I32_WRAP_I64) && memarg(s, OP_I64_LOAD, w);
}

/* Writes the ADRS of the Tweak at E at word W of the input block (eval.c
   tweak_fields and adrs_bytes). A Tweak is 5 pack blocks [0, part, flag, next,
   flag]: block B holds field B (layer, tree, kind, leaf, chain) at word 1, and
   block 4 holds step at word 3. S + 5 holds the block, S + 6 + F field F and
   S + 12 the validation flag: a trap flag or a field other than tree at
   2^32 or more sets it, as eval does. It becomes the hash result flag,
   so an unused hash does not trap. The words:
   layer << 32 | tree >> 32, tree << 32 | kind, leaf << 32 | chain,
   step << 32. */
static int emit_adrs(Sink *s, const IrExpr *e, uint32_t t, uint32_t w, unsigned depth) {
  static const uint32_t narrow[5] = {0, 2, 3, 4, 5};
  static const uint32_t high[4] = {0, 1, 3, 5};
  static const uint32_t low[4] = {1, 2, 4, 6};
  static const uint32_t shift[4] = {32, 0, 0, 0};
  uint32_t k;
  int ok = emit_expr(s, e, t, depth + 1u) && op2(s, OP_LOCAL_SET, t + 5u) && i64_const(s, 0) &&
           op2(s, OP_LOCAL_SET, t + 12u);
  for (k = 0; k < 5u && ok; k++) {
    ok = block_word(s, t + 5u, 1) && op2(s, OP_LOCAL_SET, t + 6u + k) && op2(s, OP_LOCAL_GET, t + 12u) &&
         block_word(s, t + 5u, 2) && put_byte(s, OP_I64_OR) && block_word(s, t + 5u, 4) && put_byte(s, OP_I64_OR) &&
         op2(s, OP_LOCAL_SET, t + 12u) && block_word(s, t + 5u, 3) && op2(s, OP_LOCAL_SET, k < 4u ? t + 5u : t + 11u);
  }
  for (k = 0; k < 5u && ok; k++) ok = op2(s, OP_LOCAL_GET, t + 6u + narrow[k]) && (k == 0 || put_byte(s, OP_I64_OR));
  ok = ok && i64_const(s, 32) && put_byte(s, OP_I64_SHR_U) && op2(s, OP_LOCAL_GET, t + 12u) && put_byte(s, OP_I64_OR) &&
       i64_const(s, 0) && put_byte(s, OP_I64_GT_U) && put_byte(s, OP_I64_EXTEND_I32_U) &&
       op2(s, OP_LOCAL_SET, t + 12u);
  for (k = 0; k < 4u && ok; k++) {
    ok = op2(s, OP_LOCAL_GET, t + 3u) && put_byte(s, OP_I32_WRAP_I64) && op2(s, OP_LOCAL_GET, t + 6u + high[k]) &&
         i64_const(s, 32) && put_byte(s, OP_I64_SHL) && op2(s, OP_LOCAL_GET, t + 6u + low[k]) &&
         i64_const(s, shift[k]) && put_byte(s, OP_I64_SHR_U) && put_byte(s, OP_I64_OR) && memarg(s, OP_I64_STORE, w + k);
  }
  return ok;
}

/* Writes part P at word W of the input block in S + 3. E is the IR field of
   the part (NULL for the prefix). S + 5 holds a Digest block. */
static int emit_part(Sink *s, HashPart p, uint64_t prefix, const IrExpr *e, uint32_t t, uint32_t w, unsigned depth) {
  uint32_t i;
  int ok = 1;
  switch (p) {
    case PART_PREFIX:
    case PART_NAT:
      for (i = 0; i < 3u && ok; i++) {
        ok = op2(s, OP_LOCAL_GET, t + 3u) && put_byte(s, OP_I32_WRAP_I64) && i64_const(s, 0) &&
             memarg(s, OP_I64_STORE, w + i);
      }
      return ok && op2(s, OP_LOCAL_GET, t + 3u) && put_byte(s, OP_I32_WRAP_I64) &&
             (p == PART_PREFIX ? i64_const(s, prefix) : emit_expr(s, e, t, depth + 1u)) &&
             memarg(s, OP_I64_STORE, w + 3u);
    case PART_DIGEST:
      ok = emit_expr(s, e, t, depth + 1u) && op2(s, OP_LOCAL_SET, t + 5u);
      for (i = 0; i < 4u && ok; i++) {
        ok = op2(s, OP_LOCAL_GET, t + 3u) && put_byte(s, OP_I32_WRAP_I64) && block_word(s, t + 5u, i) &&
             memarg(s, OP_I64_STORE, w + i);
      }
      return ok;
    case PART_TWEAK:
      return emit_adrs(s, e, t, w, depth);
  }
  return 0;
}

/* D14: local := a new Digest block, keccak256 of the 5.4 input of the hash.
   S + 3 holds the input block (4 words a part), S + 4 the Digest block. */
static int emit_hash(Sink *s, const IrStmt *st, uint32_t t, unsigned depth) {
  const HashLayout *lay = hash_layout(st->hash);
  size_t k;
  size_t arg = 0;
  int ok;
  if (lay == NULL || lay->field_count != st->field_count) return target_node(s);
  ok = i64_const(s, 0) && op2(s, OP_LOCAL_SET, t + 12u) &&
       i32_const(s, 32u * lay->part_count) && call_internal(s, WASM_FUNC_ALLOC) && op2(s, OP_LOCAL_SET, t + 3u) &&
       i32_const(s, 32) && call_internal(s, WASM_FUNC_ALLOC) && op2(s, OP_LOCAL_SET, t + 4u);
  for (k = 0; k < lay->part_count && ok; k++) {
    ok = emit_part(s, lay->parts[k], lay->prefix, lay->parts[k] == PART_PREFIX ? NULL : st->fields[arg], t,
                   4u * (uint32_t)k, depth);
    arg += lay->parts[k] == PART_PREFIX ? 0u : 1u;
  }
  return ok && op2(s, OP_LOCAL_GET, t + 3u) && i64_const(s, 4u * lay->part_count) && op2(s, OP_LOCAL_GET, t + 4u) &&
         call_internal(s, WASM_FUNC_KECCAK256) && op2(s, OP_LOCAL_GET, t + 4u) && op2(s, OP_LOCAL_SET, st->local) &&
         op2(s, OP_LOCAL_GET, t + 12u) && op2(s, OP_LOCAL_SET, st->flag_local);
}

/* Blocks: exit, trap, then arm n - 1 down to arm 0 (the innermost). br_table
   label k ends the block of arm k, so the code of arm k follows. A value of
   n or more takes label n, the trap block. After arm k, label n - k is the
   exit block. */
static int emit_switch(Sink *s, const IrStmt *st, uint32_t t, unsigned depth) {
  size_t k;
  size_t n = st->arm_count;
  int ok = put_byte(s, OP_BLOCK) && put_byte(s, TYPE_EMPTY) && put_byte(s, OP_BLOCK) && put_byte(s, TYPE_EMPTY);
  for (k = 0; k < n && ok; k++) ok = put_byte(s, OP_BLOCK) && put_byte(s, TYPE_EMPTY);
  ok = ok && emit_expr(s, st->expr, t, depth + 1u) && op2(s, OP_LOCAL_TEE, t) && put_byte(s, OP_I32_WRAP_I64) &&
       i32_const(s, n) && op2(s, OP_LOCAL_GET, t) && i64_const(s, n) && put_byte(s, OP_I64_LT_U) &&
       put_byte(s, OP_SELECT) && op2(s, OP_BR_TABLE, n);
  for (k = 0; k <= n && ok; k++) ok = put_uleb(s, k);
  for (k = 0; k < n && ok; k++) {
    ok = put_byte(s, OP_END) && emit_block(s, &st->arms[k], t, depth + 1u) && op2(s, OP_BR, n - k);
  }
  return ok && put_byte(s, OP_END) && put_byte(s, OP_UNREACHABLE) && put_byte(s, OP_END);
}

/* counter := expr; then, at the top of each run, leave when counter = 0 or
   (for WHILE) local = 0. counter decreases by 1 after each run. */
static int emit_loop(Sink *s, const IrStmt *st, int is_while, uint32_t t, unsigned depth) {
  return emit_expr(s, st->expr, t, depth + 1u) && op2(s, OP_LOCAL_SET, st->counter) && put_byte(s, OP_BLOCK) &&
         put_byte(s, TYPE_EMPTY) && put_byte(s, OP_LOOP) && put_byte(s, TYPE_EMPTY) &&
         op2(s, OP_LOCAL_GET, st->counter) && put_byte(s, OP_I64_EQZ) && op2(s, OP_BR_IF, 1) &&
         (!is_while || (op2(s, OP_LOCAL_GET, st->local) && put_byte(s, OP_I64_EQZ) && op2(s, OP_BR_IF, 1))) &&
         emit_block(s, &st->body, t, depth + 1u) && op2(s, OP_LOCAL_GET, st->counter) && i64_const(s, 1) &&
         put_byte(s, OP_I64_SUB) && op2(s, OP_LOCAL_SET, st->counter) && op2(s, OP_BR, 0) && put_byte(s, OP_END) &&
         put_byte(s, OP_END);
}

static int emit_stmt(Sink *s, const IrStmt *st, uint32_t t, unsigned depth) {
  if (depth > WASM_DEPTH_MAX) return deep(s);
  switch (st->kind) {
    case IR_STMT_SET:
      return emit_expr(s, st->expr, t, depth + 1u) && op2(s, OP_LOCAL_SET, st->local);
    case IR_STMT_ALLOC:
      return emit_alloc(s, st, t, depth);
    case IR_STMT_HASH:
      return emit_hash(s, st, t, depth);
    case IR_STMT_IF: /* expr = 0: otherwise, else: body */
      return emit_expr(s, st->expr, t, depth + 1u) && put_byte(s, OP_I64_EQZ) && put_byte(s, OP_IF) &&
             put_byte(s, TYPE_EMPTY) && emit_block(s, &st->otherwise, t, depth + 1u) && put_byte(s, OP_ELSE) &&
             emit_block(s, &st->body, t, depth + 1u) && put_byte(s, OP_END);
    case IR_STMT_SWITCH:
      return emit_switch(s, st, t, depth);
    case IR_STMT_REPEAT:
      return emit_loop(s, st, 0, t, depth);
    case IR_STMT_WHILE:
      return emit_loop(s, st, 1, t, depth);
    case IR_STMT_TRAP:
      return put_byte(s, OP_UNREACHABLE);
    case IR_STMT_RETURN:
      return emit_expr(s, st->expr, t, depth + 1u) && put_byte(s, OP_RETURN);
  }
  return 0;
}

static int emit_block(Sink *s, const IrBlock *b, uint32_t t, unsigned depth) {
  size_t i;
  int ok = depth <= WASM_DEPTH_MAX || deep(s);
  for (i = 0; i < b->count && ok; i++) ok = emit_stmt(s, b->items[i], t, depth + 1u);
  return ok;
}

/* alloc (bytes : i32) -> i64, local 1 = the new end:
   old := heap; end := old + bytes; if end > memory bytes { grow by
   ceil(end / 65536) - pages; trap on -1 }; heap := end; return old. */
static int part_alloc(Sink *s, const void *arg) {
  (void)arg;
  return put_uleb(s, 1) && put_uleb(s, 1) && put_byte(s, TYPE_I64) && op2(s, OP_GLOBAL_GET, 0) &&
         op2(s, OP_GLOBAL_GET, 0) && op2(s, OP_LOCAL_GET, 0) && put_byte(s, OP_I64_EXTEND_I32_U) &&
         put_byte(s, OP_I64_ADD) && op2(s, OP_LOCAL_TEE, 1) && op2(s, OP_MEMORY_SIZE, 0) &&
         put_byte(s, OP_I64_EXTEND_I32_U) && i64_const(s, 16) && put_byte(s, OP_I64_SHL) && put_byte(s, OP_I64_GT_U) &&
         put_byte(s, OP_IF) && put_byte(s, TYPE_EMPTY) && op2(s, OP_LOCAL_GET, 1) && i64_const(s, 65535) &&
         put_byte(s, OP_I64_ADD) && i64_const(s, 16) && put_byte(s, OP_I64_SHR_U) && op2(s, OP_MEMORY_SIZE, 0) &&
         put_byte(s, OP_I64_EXTEND_I32_U) && put_byte(s, OP_I64_SUB) && put_byte(s, OP_I32_WRAP_I64) &&
         op2(s, OP_MEMORY_GROW, 0) && i32_const(s, ~(uint64_t)0) && put_byte(s, OP_I32_EQ) && put_byte(s, OP_IF) &&
         put_byte(s, TYPE_EMPTY) && put_byte(s, OP_UNREACHABLE) && put_byte(s, OP_END) && put_byte(s, OP_END) &&
         op2(s, OP_LOCAL_GET, 1) && op2(s, OP_GLOBAL_SET, 0) && put_byte(s, OP_END);
}

/* The locals of keccak_f: 0 the state address, KECCAK_LANE + K lane K,
   KECCAK_ROW + X word X of the theta columns and then of the chi row,
   KECCAK_TEMP a temp, KECCAK_CARRY the carried lane of rho and pi. */
#define KECCAK_LANE 1u
#define KECCAK_ROW 26u
#define KECCAK_TEMP 31u
#define KECCAK_CARRY 32u
#define KECCAK_LOCALS 32u

/* theta (keccak.c permute): column X is the xor of the 5 lanes of column X;
   each lane of column X ^= column X - 1 ^ ROTL (column X + 1, 1). */
static int theta(Sink *s) {
  uint32_t x;
  uint32_t y;
  int ok = 1;
  for (x = 0; x < 5u && ok; x++) {
    ok = op2(s, OP_LOCAL_GET, KECCAK_LANE + x);
    for (y = 5; y < 25u && ok; y += 5u) ok = op2(s, OP_LOCAL_GET, KECCAK_LANE + y + x) && put_byte(s, OP_I64_XOR);
    ok = ok && op2(s, OP_LOCAL_SET, KECCAK_ROW + x);
  }
  for (x = 0; x < 5u && ok; x++) {
    ok = op2(s, OP_LOCAL_GET, KECCAK_ROW + (x + 4u) % 5u) && op2(s, OP_LOCAL_GET, KECCAK_ROW + (x + 1u) % 5u) &&
         i64_const(s, 1) && put_byte(s, OP_I64_ROTL) && put_byte(s, OP_I64_XOR) && op2(s, OP_LOCAL_SET, KECCAK_TEMP);
    for (y = 0; y < 25u && ok; y += 5u) {
      ok = op2(s, OP_LOCAL_GET, KECCAK_LANE + y + x) && op2(s, OP_LOCAL_GET, KECCAK_TEMP) && put_byte(s, OP_I64_XOR) &&
           op2(s, OP_LOCAL_SET, KECCAK_LANE + y + x);
    }
  }
  return ok;
}

/* rho and pi: the keccak.c walk over the rotation and lane tables. */
static int rho_pi(Sink *s) {
  static const uint32_t rotation[24] = {1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
                                        27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44};
  static const uint32_t lane[24] = {10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
                                    15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1};
  uint32_t k;
  int ok = op2(s, OP_LOCAL_GET, KECCAK_LANE + 1u) && op2(s, OP_LOCAL_SET, KECCAK_CARRY);
  for (k = 0; k < 24u && ok; k++) {
    ok = op2(s, OP_LOCAL_GET, KECCAK_LANE + lane[k]) && op2(s, OP_LOCAL_SET, KECCAK_TEMP) &&
         op2(s, OP_LOCAL_GET, KECCAK_CARRY) && i64_const(s, rotation[k]) && put_byte(s, OP_I64_ROTL) &&
         op2(s, OP_LOCAL_SET, KECCAK_LANE + lane[k]) && op2(s, OP_LOCAL_GET, KECCAK_TEMP) &&
         op2(s, OP_LOCAL_SET, KECCAK_CARRY);
  }
  return ok;
}

/* chi: in each row, lane X := row X ^ (NOT row X + 1 AND row X + 2), with
   NOT as xor -1. */
static int chi(Sink *s) {
  uint32_t x;
  uint32_t y;
  int ok = 1;
  for (y = 0; y < 25u && ok; y += 5u) {
    for (x = 0; x < 5u && ok; x++) ok = op2(s, OP_LOCAL_GET, KECCAK_LANE + y + x) && op2(s, OP_LOCAL_SET, KECCAK_ROW + x);
    for (x = 0; x < 5u && ok; x++) {
      ok = op2(s, OP_LOCAL_GET, KECCAK_ROW + x) && op2(s, OP_LOCAL_GET, KECCAK_ROW + (x + 1u) % 5u) &&
           i64_const(s, ~(uint64_t)0) && put_byte(s, OP_I64_XOR) && op2(s, OP_LOCAL_GET, KECCAK_ROW + (x + 2u) % 5u) &&
           put_byte(s, OP_I64_AND) && put_byte(s, OP_I64_XOR) && op2(s, OP_LOCAL_SET, KECCAK_LANE + y + x);
    }
  }
  return ok;
}

/* keccak_f (state : i64) (D12, Q-S5-26 (a)): the 25 lanes into locals, 24
   rounds as straight-line code (iota: lane 0 ^= the round constant of
   keccak.c), then the lanes back into the state block. */
static int part_keccak_f(Sink *s, const void *arg) {
  static const uint64_t round_constant[24] = {
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull, 0x8000000080008000ull,
    0x000000000000808bull, 0x0000000080000001ull, 0x8000000080008081ull, 0x8000000000008009ull,
    0x000000000000008aull, 0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
    0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull, 0x8000000000008003ull,
    0x8000000000008002ull, 0x8000000000000080ull, 0x000000000000800aull, 0x800000008000000aull,
    0x8000000080008081ull, 0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull};
  uint32_t k;
  int ok = put_uleb(s, 1) && put_uleb(s, KECCAK_LOCALS) && put_byte(s, TYPE_I64);
  (void)arg;
  for (k = 0; k < 25u && ok; k++) ok = block_word(s, 0, k) && op2(s, OP_LOCAL_SET, KECCAK_LANE + k);
  for (k = 0; k < 24u && ok; k++) {
    ok = theta(s) && rho_pi(s) && chi(s) && op2(s, OP_LOCAL_GET, KECCAK_LANE) && i64_const(s, round_constant[k]) &&
         put_byte(s, OP_I64_XOR) && op2(s, OP_LOCAL_SET, KECCAK_LANE);
  }
  for (k = 0; k < 25u && ok; k++) {
    ok = op2(s, OP_LOCAL_GET, 0) && put_byte(s, OP_I32_WRAP_I64) && op2(s, OP_LOCAL_GET, KECCAK_LANE + k) &&
         memarg(s, OP_I64_STORE, k);
  }
  return ok && put_byte(s, OP_END);
}

/* Pushes the byte swap of local X (Wasm MVP has no byte swap): swap the bytes
   of each 16-bit unit, then the 16-bit units of each 32-bit unit, then rotate
   by 32. It changes X. */
static int bswap(Sink *s, uint32_t x) {
  static const uint64_t mask[2] = {0x00FF00FF00FF00FFull, 0x0000FFFF0000FFFFull};
  static const uint32_t bits[2] = {8, 16};
  uint32_t k;
  int ok = 1;
  for (k = 0; k < 2u && ok; k++) {
    ok = op2(s, OP_LOCAL_GET, x) && i64_const(s, bits[k]) && put_byte(s, OP_I64_SHR_U) && i64_const(s, mask[k]) &&
         put_byte(s, OP_I64_AND) && op2(s, OP_LOCAL_GET, x) && i64_const(s, mask[k]) && put_byte(s, OP_I64_AND) &&
         i64_const(s, bits[k]) && put_byte(s, OP_I64_SHL) && put_byte(s, OP_I64_OR) && op2(s, OP_LOCAL_SET, x);
  }
  return ok && op2(s, OP_LOCAL_GET, x) && i64_const(s, 32) && put_byte(s, OP_I64_ROTL);
}

/* word W of the block at local A ^= the value that the code between
   xor_open and xor_close pushes. */
static int xor_open(Sink *s, uint32_t a, uint32_t w) {
  return op2(s, OP_LOCAL_GET, a) && put_byte(s, OP_I32_WRAP_I64) && block_word(s, a, w);
}

static int xor_close(Sink *s, uint32_t w) {
  return put_byte(s, OP_I64_XOR) && memarg(s, OP_I64_STORE, w);
}

/* Pushes the address of word (local I) of the block at local A. */
static int word_at(Sink *s, uint32_t a, uint32_t i) {
  return op2(s, OP_LOCAL_GET, a) && op2(s, OP_LOCAL_GET, i) && i64_const(s, 3) && put_byte(s, OP_I64_SHL) &&
         put_byte(s, OP_I64_ADD);
}

/* keccak256 (in, n, out) (D13, Q-S5-27 (a)): keccak.c keccak256 over 8n
   bytes. Local 3 holds the state block, 4 the word k, 5 the lane j (k mod 17:
   the rate is 17 lanes), 6 the address of lane j, 7 a word. A lane is
   little-endian and a word is big-endian, so each word is byte swapped. */
static int part_keccak256(Sink *s, const void *arg) {
  uint32_t k;
  int ok = put_uleb(s, 1) && put_uleb(s, 5) && put_byte(s, TYPE_I64) && i32_const(s, 200) &&
           call_internal(s, WASM_FUNC_ALLOC) && op2(s, OP_LOCAL_SET, 3);
  (void)arg;
  /* The memory is not zero after the heap reset: zero the state. */
  for (k = 0; k < 25u && ok; k++) {
    ok = op2(s, OP_LOCAL_GET, 3) && put_byte(s, OP_I32_WRAP_I64) && i64_const(s, 0) && memarg(s, OP_I64_STORE, k);
  }
  /* Absorb: lane j ^= bswap (word k); after lane 16, keccak_f and j := 0. */
  ok = ok && put_byte(s, OP_BLOCK) && put_byte(s, TYPE_EMPTY) && put_byte(s, OP_LOOP) && put_byte(s, TYPE_EMPTY) &&
       op2(s, OP_LOCAL_GET, 4) && op2(s, OP_LOCAL_GET, 1) && put_byte(s, OP_I64_GE_U) && op2(s, OP_BR_IF, 1) &&
       word_at(s, 0, 4) && put_byte(s, OP_I32_WRAP_I64) && memarg(s, OP_I64_LOAD, 0) && op2(s, OP_LOCAL_SET, 7) &&
       word_at(s, 3, 5) && op2(s, OP_LOCAL_SET, 6) && xor_open(s, 6, 0) && bswap(s, 7) && xor_close(s, 0) &&
       op2(s, OP_LOCAL_GET, 4) && i64_const(s, 1) && put_byte(s, OP_I64_ADD) && op2(s, OP_LOCAL_SET, 4) &&
       op2(s, OP_LOCAL_GET, 5) && i64_const(s, 1) && put_byte(s, OP_I64_ADD) && op2(s, OP_LOCAL_TEE, 5) &&
       i64_const(s, 17) && put_byte(s, OP_I64_EQ) && put_byte(s, OP_IF) && put_byte(s, TYPE_EMPTY) &&
       op2(s, OP_LOCAL_GET, 3) && call_internal(s, WASM_FUNC_KECCAK_F) && i64_const(s, 0) && op2(s, OP_LOCAL_SET, 5) &&
       put_byte(s, OP_END) && op2(s, OP_BR, 0) && put_byte(s, OP_END) && put_byte(s, OP_END);
  /* Pad (Ethereum Keccak, not the SHA-3 0x06): 0x01 at byte 0 of lane j, 0x80
     at byte 7 of lane 16. */
  ok = ok && word_at(s, 3, 5) && op2(s, OP_LOCAL_SET, 6) && xor_open(s, 6, 0) && i64_const(s, 1) && xor_close(s, 0) &&
       xor_open(s, 3, 16) && i64_const(s, (uint64_t)1 << 63) && xor_close(s, 16) && op2(s, OP_LOCAL_GET, 3) &&
       call_internal(s, WASM_FUNC_KECCAK_F);
  /* Squeeze: word w of out := bswap (lane w), for the 4 words of the Digest. */
  for (k = 0; k < 4u && ok; k++) {
    ok = block_word(s, 3, k) && op2(s, OP_LOCAL_SET, 7) && op2(s, OP_LOCAL_GET, 2) && put_byte(s, OP_I32_WRAP_I64) &&
         bswap(s, 7) && memarg(s, OP_I64_STORE, k);
  }
  return ok && put_byte(s, OP_END);
}

/* Traps when the Bool parameter K is larger than 1. */
static int bool_guard(Sink *s, const IrFunc *fn, size_t k) {
  return fn->params[k] == IR_SCALAR_NAT ||
         (op2(s, OP_LOCAL_GET, k) && i64_const(s, 1) && put_byte(s, OP_I64_GT_U) && put_byte(s, OP_IF) &&
          put_byte(s, TYPE_EMPTY) && put_byte(s, OP_UNREACHABLE) && put_byte(s, OP_END));
}

static int part_entry(Sink *s, const void *arg) {
  const IrFunc *fn = arg;
  uint32_t t = fn->local_count;
  size_t k;
  int ok = put_uleb(s, 1) && put_uleb(s, (uint64_t)(fn->local_count - fn->param_count) + WASM_SCRATCH) &&
           put_byte(s, TYPE_I64) && i64_const(s, 0) && op2(s, OP_GLOBAL_SET, 0);
  for (k = 0; k < fn->param_count && ok; k++) ok = bool_guard(s, fn, k);
  return ok && emit_block(s, &fn->body, t, 0) && put_byte(s, OP_UNREACHABLE) && put_byte(s, OP_END);
}

static int part_types(Sink *s, const void *arg) {
  const IrProgram *prog = arg;
  size_t i;
  size_t k;
  int ok = put_uleb(s, WASM_INTERNAL + prog->func_count) && put_byte(s, TYPE_FUNC) && put_uleb(s, 1) &&
           put_byte(s, TYPE_I32) && put_uleb(s, 1) && put_byte(s, TYPE_I64) && put_byte(s, TYPE_FUNC) &&
           put_uleb(s, 1) && put_byte(s, TYPE_I64) && put_uleb(s, 0) && put_byte(s, TYPE_FUNC) && put_uleb(s, 3) &&
           put_byte(s, TYPE_I64) && put_byte(s, TYPE_I64) && put_byte(s, TYPE_I64) && put_uleb(s, 0);
  for (i = 0; i < prog->func_count && ok; i++) {
    ok = put_byte(s, TYPE_FUNC) && put_uleb(s, prog->funcs[i].param_count);
    for (k = 0; k < prog->funcs[i].param_count && ok; k++) ok = put_byte(s, TYPE_I64);
    ok = ok && put_uleb(s, 1) && put_byte(s, TYPE_I64);
  }
  return ok;
}

/* The import list of a program module is empty (WASM_IMPORTS is 0). Only
   the signer module imports env.commit (Q-S5-32 (a)). */
static int part_imports(Sink *s, const void *arg) {
  (void)arg;
  return put_uleb(s, WASM_IMPORTS);
}

static int part_functions(Sink *s, const void *arg) {
  const IrProgram *prog = arg;
  size_t i;
  int ok = put_uleb(s, WASM_INTERNAL + prog->func_count);
  for (i = 0; i < WASM_INTERNAL + prog->func_count && ok; i++) ok = put_uleb(s, i);
  return ok;
}

static int part_memory(Sink *s, const void *arg) {
  (void)arg;
  return put_uleb(s, 1) && put_byte(s, 1) && put_uleb(s, 1) && put_uleb(s, WASM_MAX_PAGES);
}

/* One mutable i64 global with the value 0. */
static int global_i64(Sink *s) {
  return put_byte(s, TYPE_I64) && put_byte(s, 1) && i64_const(s, 0) && put_byte(s, OP_END);
}

static int part_global(Sink *s, const void *arg) {
  (void)arg;
  return put_uleb(s, 1) && global_i64(s);
}

static int part_exports(Sink *s, const void *arg) {
  const IrProgram *prog = arg;
  size_t i;
  int ok = put_uleb(s, prog->func_count);
  for (i = 0; i < prog->func_count && ok; i++) {
    ok = put_name(s, prog->funcs[i].name) && put_byte(s, 0) && put_uleb(s, WASM_IMPORTS + WASM_INTERNAL + i);
  }
  return ok;
}

static int part_code(Sink *s, const void *arg) {
  const IrProgram *prog = arg;
  size_t i;
  int ok = put_uleb(s, WASM_INTERNAL + prog->func_count) && sized(s, part_alloc, NULL) &&
           sized(s, part_keccak_f, NULL) && sized(s, part_keccak256, NULL);
  for (i = 0; i < prog->func_count && ok; i++) ok = sized(s, part_entry, &prog->funcs[i]);
  return ok;
}

int target_abi_line(const IrFunc *fn, FILE *out, FILE *err) {
  (void)err;
  fprintf(out, "%s\n", fn->name);
  return 1;
}

/* The magic number and the version 1. */
static int put_header(Sink *s) {
  static const unsigned char header[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
  size_t i;
  int ok = 1;
  for (i = 0; i < sizeof header && ok; i++) ok = put_byte(s, header[i]);
  return ok;
}

/* Both parts are the module bytes. */
int target_write(const IrProgram *prog, TargetPart part, FILE *out, FILE *err) {
  Sink s = {out, 0, err, WASM_IMPORTS};
  int ok;
  (void)part;
  ok = put_header(&s) && section(&s, SECTION_TYPE, part_types, prog) && section(&s, SECTION_IMPORT, part_imports, NULL) &&
       section(&s, SECTION_FUNCTION, part_functions, prog) &&
       section(&s, SECTION_MEMORY, part_memory, NULL) && section(&s, SECTION_GLOBAL, part_global, NULL) &&
       section(&s, SECTION_EXPORT, part_exports, prog) && section(&s, SECTION_CODE, part_code, prog);
  if (!ok && ferror(out)) fputs("langc: IO: -: cannot write the wasm module\n", err);
  return ok;
}

/* The signer module (D18 to D21, D27). env.commit is function 0, so
   Sink.base is SIGNER_IMPORTS. */
#define SIGNER_IMPORTS 1u
#define SIGNER_FUNC_COMMIT 0u
/* Bytes 0 to SIGNER_IO_BYTES - 1 are the host I/O area (D19). */
#define SIGNER_IO_BYTES 4096u

/* The words of the state block (D20). keygen writes the root (D28). */
enum {
  KS_ID = 0,
  KS_NEXT = 1,
  KS_SIGNED = 2,
  KS_SK_SEED = 3,
  KS_SK_PRF = 7,
  KS_PUB_SEED = 11,
  KS_ROOT = 15,
  KS_WORDS = 19
};

/* The words of the publicKey block (D27). */
enum { PK_ROOT = 0, PK_PUB_SEED = 4, PK_HEIGHT = 8, PK_WORDS = 9 };

enum {
  SIGNER_TYPE_ALLOC,     /* (i32) -> (i64) */
  SIGNER_TYPE_KECCAK_F,  /* (i64) -> () */
  SIGNER_TYPE_KECCAK256, /* (i64 i64 i64) -> () */
  SIGNER_TYPE_COMMIT,    /* (i64 i64) -> (): env.commit (id, position), wots_digits */
  SIGNER_TYPE_KEYGEN,    /* (i64 i64 i64 i64 i64) -> (i64) */
  SIGNER_TYPE_KS,        /* (i64) -> (i64): publicKey, next */
  SIGNER_TYPE_TREE4,     /* (i64 x 4) -> (): prf, wots_pk, ltree, auth_path */
  SIGNER_TYPE_TREE5,     /* (i64 x 5) -> (): th_chain, tree_hash, th_msg, wots_sign, wots_pk_from_sig, root_from_path */
  SIGNER_TYPE_TREE6,     /* (i64 x 6) -> (): th_node, chain */
  SIGNER_TYPE_SIGN       /* (i64 i64 i64) -> (i64): sign, verify */
};

static const struct {
  unsigned param;
  unsigned params;
  unsigned results;
} SIGNER_TYPES[] = {
  {TYPE_I32, 1, 1}, {TYPE_I64, 1, 0}, {TYPE_I64, 3, 0}, {TYPE_I64, 2, 0}, {TYPE_I64, 5, 1}, {TYPE_I64, 1, 1},
  {TYPE_I64, 4, 0}, {TYPE_I64, 5, 0}, {TYPE_I64, 6, 0}, {TYPE_I64, 3, 1},
};

/* Pushes the address of the block at local A, for a store. */
static int block_addr(Sink *s, uint32_t a) {
  return op2(s, OP_LOCAL_GET, a) && put_byte(s, OP_I32_WRAP_I64);
}

/* Word DW + K of the block at local D := word SW + K of the block at local
   A, for K = 0 to N - 1. */
static int copy_words(Sink *s, uint32_t d, uint32_t dw, uint32_t a, uint32_t sw, uint32_t n) {
  uint32_t k;
  int ok = 1;
  for (k = 0; k < n && ok; k++) ok = block_addr(s, d) && block_word(s, a, sw + k) && memarg(s, OP_I64_STORE, dw + k);
  return ok;
}

/* The local declarations of a signer function: N i64 locals. */
static int i64_locals(Sink *s, uint32_t n) {
  return put_uleb(s, 1) && put_uleb(s, n) && put_byte(s, TYPE_I64);
}

/* The heap pointer := the mark (D19). */
static int mark_reset(Sink *s) {
  return op2(s, OP_GLOBAL_GET, 1) && op2(s, OP_GLOBAL_SET, 0);
}

/* Pushes local L + 1. */
static int plus_one(Sink *s, uint32_t l) {
  return op2(s, OP_LOCAL_GET, l) && i64_const(s, 1) && put_byte(s, OP_I64_ADD);
}

/* Pushes local A + 8W: the address of word W of the block at local A. */
static int word_addr(Sink *s, uint32_t a, uint32_t w) {
  return op2(s, OP_LOCAL_GET, a) && i64_const(s, 8u * (uint64_t)w) && put_byte(s, OP_I64_ADD);
}

/* Pushes local A + (local I << SHIFT): the address of item (local I) of a
   block of 2^SHIFT byte items (a digest is 32 bytes: SHIFT 5). */
static int item_addr(Sink *s, uint32_t a, uint32_t i, uint64_t shift) {
  return op2(s, OP_LOCAL_GET, a) && op2(s, OP_LOCAL_GET, i) && i64_const(s, shift) && put_byte(s, OP_I64_SHL) &&
         put_byte(s, OP_I64_ADD);
}

/* Pushes local HI << 32 | local LO. */
static int hi_lo(Sink *s, uint32_t hi, uint32_t lo) {
  return op2(s, OP_LOCAL_GET, hi) && i64_const(s, 32) && put_byte(s, OP_I64_SHL) && op2(s, OP_LOCAL_GET, lo) &&
         put_byte(s, OP_I64_OR);
}

/* Word W of the block at local A := V. */
static int store_const(Sink *s, uint32_t a, uint32_t w, uint64_t v) {
  return block_addr(s, a) && i64_const(s, v) && memarg(s, OP_I64_STORE, w);
}

/* Word W of the block at local A := local L. */
static int store_local(Sink *s, uint32_t a, uint32_t w, uint32_t l) {
  return block_addr(s, a) && op2(s, OP_LOCAL_GET, l) && memarg(s, OP_I64_STORE, w);
}

/* Words W + 2 and W + 3 of the input block at local A := local LC and local
   STEP << 32: the leaf and chain word and the step word of the signer ADRS
   (emit_adrs). Word W is 0 (layer 0, tree 0) and word W + 1 is the kind. */
static int adrs_tail(Sink *s, uint32_t a, uint32_t w, uint32_t lc, uint32_t step) {
  return store_local(s, a, w + 2u, lc) && block_addr(s, a) && op2(s, OP_LOCAL_GET, step) && i64_const(s, 32) &&
         put_byte(s, OP_I64_SHL) && memarg(s, OP_I64_STORE, w + 3u);
}

/* D28: local SAVE := the heap pointer, then local A := alloc (BYTES). */
static int scratch_open(Sink *s, uint32_t save, uint32_t bytes, uint32_t a) {
  return op2(s, OP_GLOBAL_GET, 0) && op2(s, OP_LOCAL_SET, save) && i32_const(s, bytes) &&
         call_internal(s, WASM_FUNC_ALLOC) && op2(s, OP_LOCAL_SET, a);
}

/* D28: the heap pointer := local SAVE, so the scratch after it is free. */
static int scratch_close(Sink *s, uint32_t save) {
  return op2(s, OP_LOCAL_GET, save) && op2(s, OP_GLOBAL_SET, 0);
}

/* keccak256 (local IN, N, local OUT). */
static int hash_words(Sink *s, uint32_t in, uint32_t n, uint32_t out) {
  return op2(s, OP_LOCAL_GET, in) && i64_const(s, n) && op2(s, OP_LOCAL_GET, out) &&
         call_internal(s, WASM_FUNC_KECCAK256);
}

/* Opens block { loop {: br_if 1 leaves the loop, br 0 goes on. */
static int loop_open(Sink *s) {
  return put_byte(s, OP_BLOCK) && put_byte(s, TYPE_EMPTY) && put_byte(s, OP_LOOP) && put_byte(s, TYPE_EMPTY);
}

/* Leaves the loop of loop_open when local I >= local N. */
static int loop_exit(Sink *s, uint32_t i, uint32_t n) {
  return op2(s, OP_LOCAL_GET, i) && op2(s, OP_LOCAL_GET, n) && put_byte(s, OP_I64_GE_U) && op2(s, OP_BR_IF, 1);
}

/* Local I := I + 1, then the next turn of the loop of loop_open. It closes
   the loop. */
static int loop_next(Sink *s, uint32_t i) {
  return plus_one(s, i) && op2(s, OP_LOCAL_SET, i) && op2(s, OP_BR, 0) && put_byte(s, OP_END) && put_byte(s, OP_END);
}

/* th_chain (seed, lc, step, x, out) (D29): keccak256 of the 12 words seed,
   the ADRS (kind 0) and x to out. out can be x. Local 5 holds the saved
   heap pointer, 6 the input block. */
static int part_th_chain(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 2) && scratch_open(s, 5, 96, 6) && copy_words(s, 6, 0, 0, 0, 4) && store_const(s, 6, 4, 0) &&
         store_const(s, 6, 5, 0) && adrs_tail(s, 6, 4, 1, 2) && copy_words(s, 6, 8, 3, 0, 4) &&
         hash_words(s, 6, 12, 4) && scratch_close(s, 5) && put_byte(s, OP_END);
}

/* th_node (seed, kind, lc, step, lr, out) (D29): keccak256 of the 16 words
   seed, the ADRS and the two digests at lr (l, then r) to out. out can be
   lr. Local 6 holds the saved heap pointer, 7 the input block. */
static int part_th_node(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 2) && scratch_open(s, 6, 128, 7) && copy_words(s, 7, 0, 0, 0, 4) && store_const(s, 7, 4, 0) &&
         store_local(s, 7, 5, 1) && adrs_tail(s, 7, 4, 2, 3) && copy_words(s, 7, 8, 4, 0, 8) &&
         hash_words(s, 7, 16, 5) && scratch_close(s, 6) && put_byte(s, OP_END);
}

/* prf (key, lc, step, out) (D29, 5.4): keccak256 of the 12 words toByte (3,
   32), key and the ADRS (kind 0) to out. Local 4 holds the saved heap
   pointer, 5 the input block. */
static int part_prf(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 2) && scratch_open(s, 4, 96, 5) && store_const(s, 5, 0, 0) && store_const(s, 5, 1, 0) &&
         store_const(s, 5, 2, 0) && store_const(s, 5, 3, 3) && copy_words(s, 5, 4, 0, 0, 4) &&
         store_const(s, 5, 8, 0) && store_const(s, 5, 9, 0) && adrs_tail(s, 5, 8, 1, 2) && hash_words(s, 5, 12, 3) &&
         scratch_close(s, 4) && put_byte(s, OP_END);
}

/* chain (seed, lc, x, start, steps, out) (D29, chain): out := x, then
   th_chain (seed, lc, i, out, out) for i = start to start + steps - 1.
   Local 6 holds i, 7 start + steps. */
static int part_chain(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 2) && copy_words(s, 5, 0, 2, 0, 4) && op2(s, OP_LOCAL_GET, 3) && op2(s, OP_LOCAL_SET, 6) &&
         op2(s, OP_LOCAL_GET, 3) && op2(s, OP_LOCAL_GET, 4) && put_byte(s, OP_I64_ADD) && op2(s, OP_LOCAL_SET, 7) &&
         loop_open(s) && loop_exit(s, 6, 7) && op2(s, OP_LOCAL_GET, 0) && op2(s, OP_LOCAL_GET, 1) &&
         op2(s, OP_LOCAL_GET, 6) && op2(s, OP_LOCAL_GET, 5) && op2(s, OP_LOCAL_GET, 5) &&
         call_internal(s, WASM_FUNC_TH_CHAIN) && loop_next(s, 6) && put_byte(s, OP_END);
}

/* wots_pk (skSeed, pubSeed, ots, out) (D29, wotsPk): the 67 digests of the
   WOTS+ public key ots to out. Chain c has lc = ots << 32 | c (wotsTweak
   ots c): its start is prf (skSeed, lc, 0) (wotsSk), then 15 steps. Local 4
   holds c, 5 lc, 6 the address of digest c, 7 the count 67. */
static int part_wots_pk(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 4) && i64_const(s, 67) && op2(s, OP_LOCAL_SET, 7) && loop_open(s) && loop_exit(s, 4, 7) &&
         hi_lo(s, 2, 4) && op2(s, OP_LOCAL_SET, 5) && item_addr(s, 3, 4, 5) && op2(s, OP_LOCAL_SET, 6) &&
         op2(s, OP_LOCAL_GET, 0) && op2(s, OP_LOCAL_GET, 5) && i64_const(s, 0) && op2(s, OP_LOCAL_GET, 6) &&
         call_internal(s, WASM_FUNC_PRF) && op2(s, OP_LOCAL_GET, 1) && op2(s, OP_LOCAL_GET, 5) &&
         op2(s, OP_LOCAL_GET, 6) && i64_const(s, 0) && i64_const(s, 15) && op2(s, OP_LOCAL_GET, 6) &&
         call_internal(s, WASM_FUNC_CHAIN) && loop_next(s, 4) && put_byte(s, OP_END);
}

/* ltree (pubSeed, ots, pk, out) (D29, RFC 8391 Algorithm 16, lTreeUp): in
   place on the 67 digests at pk. While len > 1, node j of level k + 1 is
   th_node (pubSeed, 1, ots << 32 | k, j, the nodes 2j and 2j + 1) for j <
   len / 2, and an odd last node moves up. Then out := digest 0. Local 4
   holds len, 5 k, 6 j, 7 len / 2, 8 lc, 9 an address. */
static int part_ltree(Sink *s, const void *arg) {
  int ok;
  (void)arg;
  ok = i64_locals(s, 6) && i64_const(s, 67) && op2(s, OP_LOCAL_SET, 4) && loop_open(s) && op2(s, OP_LOCAL_GET, 4) &&
       i64_const(s, 1) && put_byte(s, OP_I64_LE_U) && op2(s, OP_BR_IF, 1) && op2(s, OP_LOCAL_GET, 4) &&
       i64_const(s, 1) && put_byte(s, OP_I64_SHR_U) && op2(s, OP_LOCAL_SET, 7) && hi_lo(s, 1, 5) &&
       op2(s, OP_LOCAL_SET, 8) && i64_const(s, 0) && op2(s, OP_LOCAL_SET, 6) && loop_open(s) && loop_exit(s, 6, 7) &&
       op2(s, OP_LOCAL_GET, 0) && i64_const(s, 1) && op2(s, OP_LOCAL_GET, 8) && op2(s, OP_LOCAL_GET, 6) &&
       item_addr(s, 2, 6, 6) && item_addr(s, 2, 6, 5) && call_internal(s, WASM_FUNC_TH_NODE) && loop_next(s, 6);
  /* An odd len: digest len / 2 := digest len - 1. */
  ok = ok && op2(s, OP_LOCAL_GET, 4) && i64_const(s, 1) && put_byte(s, OP_I64_AND) && put_byte(s, OP_I32_WRAP_I64) &&
       put_byte(s, OP_IF) && put_byte(s, TYPE_EMPTY) && item_addr(s, 2, 7, 5) && op2(s, OP_LOCAL_SET, 6) &&
       op2(s, OP_LOCAL_GET, 4) && i64_const(s, 1) && put_byte(s, OP_I64_SUB) && op2(s, OP_LOCAL_SET, 9) &&
       item_addr(s, 2, 9, 5) && op2(s, OP_LOCAL_SET, 9) && copy_words(s, 6, 0, 9, 0, 4) && put_byte(s, OP_END);
  /* len := (len + 1) / 2 and k := k + 1, then out := digest 0. */
  return ok && plus_one(s, 4) && i64_const(s, 1) && put_byte(s, OP_I64_SHR_U) && op2(s, OP_LOCAL_SET, 4) &&
         loop_next(s, 5) && copy_words(s, 3, 0, 2, 0, 4) && put_byte(s, OP_END);
}

/* tree_hash (skSeed, pubSeed, k, j, out) (D29, RFC 8391 Algorithm 9,
   treeNode k j): node j at height k to out. Leaf L is ltree (pubSeed, L,
   wots_pk (skSeed, pubSeed, L)) for L = j 2^k to j 2^k + 2^k - 1. A stack
   of 21 digests and 21 heights (HMAX + 1): while the two top nodes have
   the same height kk, they become th_node (pubSeed, 2, kk, L >> (kk + 1),
   the two nodes) (nodeTweak). Local 5 holds the saved heap pointer, 6 the
   stack, 7 the heights, 8 i, 9 2^k, 10 L, 11 j 2^k, 12 top, 13 the pk
   block, 14 the heap pointer before it, 15 kk, 16 top - 2, 17 an address. */
static int part_tree_hash(Sink *s, const void *arg) {
  int ok;
  (void)arg;
  ok = i64_locals(s, 13) && scratch_open(s, 5, 32u * 21u, 6) && i32_const(s, 8u * 21u) &&
       call_internal(s, WASM_FUNC_ALLOC) && op2(s, OP_LOCAL_SET, 7) && op2(s, OP_LOCAL_GET, 3) &&
       op2(s, OP_LOCAL_GET, 2) && put_byte(s, OP_I64_SHL) && op2(s, OP_LOCAL_SET, 11) && i64_const(s, 1) &&
       op2(s, OP_LOCAL_GET, 2) && put_byte(s, OP_I64_SHL) && op2(s, OP_LOCAL_SET, 9) && loop_open(s) &&
       loop_exit(s, 8, 9) && op2(s, OP_LOCAL_GET, 11) && op2(s, OP_LOCAL_GET, 8) && put_byte(s, OP_I64_ADD) &&
       op2(s, OP_LOCAL_SET, 10);
  /* Leaf L to stack digest top, with height 0. The pk block is free after it. */
  ok = ok && scratch_open(s, 14, 32u * 67u, 13) && op2(s, OP_LOCAL_GET, 0) && op2(s, OP_LOCAL_GET, 1) &&
       op2(s, OP_LOCAL_GET, 10) && op2(s, OP_LOCAL_GET, 13) && call_internal(s, WASM_FUNC_WOTS_PK) &&
       op2(s, OP_LOCAL_GET, 1) && op2(s, OP_LOCAL_GET, 10) && op2(s, OP_LOCAL_GET, 13) && item_addr(s, 6, 12, 5) &&
       call_internal(s, WASM_FUNC_LTREE) && scratch_close(s, 14) && word_at(s, 7, 12) &&
       put_byte(s, OP_I32_WRAP_I64) && i64_const(s, 0) && memarg(s, OP_I64_STORE, 0) && plus_one(s, 12) &&
       op2(s, OP_LOCAL_SET, 12);
  /* While top >= 2 and height top - 2 = height top - 1 = kk: height top - 2
     := kk + 1, node top - 2 := the parent, top := top - 1. */
  ok = ok && loop_open(s) && op2(s, OP_LOCAL_GET, 12) && i64_const(s, 2) && put_byte(s, OP_I64_LT_U) &&
       op2(s, OP_BR_IF, 1) && op2(s, OP_LOCAL_GET, 12) && i64_const(s, 2) && put_byte(s, OP_I64_SUB) &&
       op2(s, OP_LOCAL_SET, 16) && word_at(s, 7, 16) && op2(s, OP_LOCAL_SET, 17) && block_word(s, 17, 1) &&
       op2(s, OP_LOCAL_TEE, 15) && block_word(s, 17, 0) && put_byte(s, OP_I64_NE) && op2(s, OP_BR_IF, 1) &&
       block_addr(s, 17) && plus_one(s, 15) && memarg(s, OP_I64_STORE, 0) && item_addr(s, 6, 16, 5) &&
       op2(s, OP_LOCAL_SET, 17) && op2(s, OP_LOCAL_GET, 1) && i64_const(s, 2) && op2(s, OP_LOCAL_GET, 15) &&
       op2(s, OP_LOCAL_GET, 10) && plus_one(s, 15) && put_byte(s, OP_I64_SHR_U) && op2(s, OP_LOCAL_GET, 17) &&
       op2(s, OP_LOCAL_GET, 17) && call_internal(s, WASM_FUNC_TH_NODE) && op2(s, OP_LOCAL_GET, 12) &&
       i64_const(s, 1) && put_byte(s, OP_I64_SUB) && op2(s, OP_LOCAL_SET, 12) && op2(s, OP_BR, 0) &&
       put_byte(s, OP_END) && put_byte(s, OP_END);
  return ok && loop_next(s, 8) && copy_words(s, 4, 0, 6, 0, 4) && scratch_close(s, 5) && put_byte(s, OP_END);
}

/* Traps when the i32 on the stack is not 0. */
static int trap_if(Sink *s) {
  return put_byte(s, OP_IF) && put_byte(s, TYPE_EMPTY) && put_byte(s, OP_UNREACHABLE) && put_byte(s, OP_END);
}

/* th_msg (r, root, i, m, out) (D34, 5.4 thMsg): keccak256 of the 20 words
   toByte (2, 32), r, root, toByte (i, 32) and m to out. Local 5 holds the
   saved heap pointer, 6 the input block. */
static int part_th_msg(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 2) && scratch_open(s, 5, 160, 6) && store_const(s, 6, 0, 0) && store_const(s, 6, 1, 0) &&
         store_const(s, 6, 2, 0) && store_const(s, 6, 3, 2) && copy_words(s, 6, 4, 0, 0, 4) &&
         copy_words(s, 6, 8, 1, 0, 4) && store_const(s, 6, 12, 0) && store_const(s, 6, 13, 0) &&
         store_const(s, 6, 14, 0) && store_local(s, 6, 15, 2) && copy_words(s, 6, 16, 3, 0, 4) &&
         hash_words(s, 6, 20, 4) && scratch_close(s, 5) && put_byte(s, OP_END);
}

/* wots_digits (mh, out) (D34, wotsDigit): the 67 base-16 digits of mh as
   67 words to out. Digit c < 64 is the half byte c of mh, the high half of
   a byte first: word c >> 4 of mh, shifted right by 60 - 4 (c & 15). The
   digits 64 to 66 are csum >> 8, (csum >> 4) & 15 and csum & 15, with csum
   the sum of 15 - digit c for c < 64. Local 2 holds c, 3 the count 64, 4
   the digit, 5 csum. */
static int part_wots_digits(Sink *s, const void *arg) {
  int ok;
  (void)arg;
  ok = i64_locals(s, 4) && i64_const(s, 64) && op2(s, OP_LOCAL_SET, 3) && loop_open(s) && loop_exit(s, 2, 3) &&
       op2(s, OP_LOCAL_GET, 0) && op2(s, OP_LOCAL_GET, 2) && i64_const(s, 4) && put_byte(s, OP_I64_SHR_U) &&
       i64_const(s, 3) && put_byte(s, OP_I64_SHL) && put_byte(s, OP_I64_ADD) && put_byte(s, OP_I32_WRAP_I64) &&
       memarg(s, OP_I64_LOAD, 0) && i64_const(s, 60) && op2(s, OP_LOCAL_GET, 2) && i64_const(s, 15) &&
       put_byte(s, OP_I64_AND) && i64_const(s, 2) && put_byte(s, OP_I64_SHL) && put_byte(s, OP_I64_SUB) &&
       put_byte(s, OP_I64_SHR_U) && i64_const(s, 15) && put_byte(s, OP_I64_AND) && op2(s, OP_LOCAL_SET, 4) &&
       word_at(s, 1, 2) && put_byte(s, OP_I32_WRAP_I64) && op2(s, OP_LOCAL_GET, 4) && memarg(s, OP_I64_STORE, 0) &&
       op2(s, OP_LOCAL_GET, 5) && i64_const(s, 15) && op2(s, OP_LOCAL_GET, 4) && put_byte(s, OP_I64_SUB) &&
       put_byte(s, OP_I64_ADD) && op2(s, OP_LOCAL_SET, 5) && loop_next(s, 2);
  /* The 3 checksum digits: the 12-bit csum, the high digit first. */
  return ok && block_addr(s, 1) && op2(s, OP_LOCAL_GET, 5) && i64_const(s, 8) && put_byte(s, OP_I64_SHR_U) &&
         memarg(s, OP_I64_STORE, 64) && block_addr(s, 1) && op2(s, OP_LOCAL_GET, 5) && i64_const(s, 4) &&
         put_byte(s, OP_I64_SHR_U) && i64_const(s, 15) && put_byte(s, OP_I64_AND) && memarg(s, OP_I64_STORE, 65) &&
         block_addr(s, 1) && op2(s, OP_LOCAL_GET, 5) && i64_const(s, 15) && put_byte(s, OP_I64_AND) &&
         memarg(s, OP_I64_STORE, 66) && put_byte(s, OP_END);
}

/* wots_sign (skSeed, pubSeed, ots, mh, out) (D34, wotsSign): the 67 WOTS+
   values of mh to out. Value c is chain (pubSeed, lc, prf (skSeed, lc, 0),
   0, digit c), with lc = ots << 32 | c. Local 5 holds the saved heap
   pointer, 6 the digits, 7 c, 8 lc, 9 the address of value c, 10 the count
   67. */
static int part_wots_sign(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 6) && scratch_open(s, 5, 8u * 67u, 6) && op2(s, OP_LOCAL_GET, 3) &&
         op2(s, OP_LOCAL_GET, 6) && call_internal(s, WASM_FUNC_WOTS_DIGITS) && i64_const(s, 67) &&
         op2(s, OP_LOCAL_SET, 10) && loop_open(s) && loop_exit(s, 7, 10) && hi_lo(s, 2, 7) && op2(s, OP_LOCAL_SET, 8) &&
         item_addr(s, 4, 7, 5) && op2(s, OP_LOCAL_SET, 9) && op2(s, OP_LOCAL_GET, 0) && op2(s, OP_LOCAL_GET, 8) &&
         i64_const(s, 0) && op2(s, OP_LOCAL_GET, 9) && call_internal(s, WASM_FUNC_PRF) && op2(s, OP_LOCAL_GET, 1) &&
         op2(s, OP_LOCAL_GET, 8) && op2(s, OP_LOCAL_GET, 9) && i64_const(s, 0) && word_at(s, 6, 7) &&
         put_byte(s, OP_I32_WRAP_I64) && memarg(s, OP_I64_LOAD, 0) && op2(s, OP_LOCAL_GET, 9) &&
         call_internal(s, WASM_FUNC_CHAIN) && loop_next(s, 7) && scratch_close(s, 5) && put_byte(s, OP_END);
}

/* wots_pk_from_sig (pubSeed, ots, mh, sig, out) (D34, wotsPkFromSig): the
   67 digests of the WOTS+ public key of the values at sig to out. Digest c
   is chain (pubSeed, lc, value c, digit c, 15 - digit c), with lc = ots <<
   32 | c. Local 5 holds the saved heap pointer, 6 the digits, 7 c, 8 lc, 9
   digit c, 10 the count 67. */
static int part_wots_pk_from_sig(Sink *s, const void *arg) {
  (void)arg;
  return i64_locals(s, 6) && scratch_open(s, 5, 8u * 67u, 6) && op2(s, OP_LOCAL_GET, 2) &&
         op2(s, OP_LOCAL_GET, 6) && call_internal(s, WASM_FUNC_WOTS_DIGITS) && i64_const(s, 67) &&
         op2(s, OP_LOCAL_SET, 10) && loop_open(s) && loop_exit(s, 7, 10) && hi_lo(s, 1, 7) && op2(s, OP_LOCAL_SET, 8) &&
         word_at(s, 6, 7) && put_byte(s, OP_I32_WRAP_I64) && memarg(s, OP_I64_LOAD, 0) && op2(s, OP_LOCAL_SET, 9) &&
         op2(s, OP_LOCAL_GET, 0) && op2(s, OP_LOCAL_GET, 8) && item_addr(s, 3, 7, 5) && op2(s, OP_LOCAL_GET, 9) &&
         i64_const(s, 15) && op2(s, OP_LOCAL_GET, 9) && put_byte(s, OP_I64_SUB) && item_addr(s, 4, 7, 5) &&
         call_internal(s, WASM_FUNC_CHAIN) && loop_next(s, 7) && scratch_close(s, 5) && put_byte(s, OP_END);
}

/* auth_path (skSeed, pubSeed, i, out) (D34, authPath): the H auth nodes to
   out. Node k is tree_hash (skSeed, pubSeed, k, (i >> k) xor 1): the
   sibling at height k. Local 4 holds k, 5 the count H. */
static int part_auth_path(Sink *s, const void *arg) {
  const unsigned *height = arg;
  return i64_locals(s, 2) && i64_const(s, *height) && op2(s, OP_LOCAL_SET, 5) && loop_open(s) && loop_exit(s, 4, 5) &&
         op2(s, OP_LOCAL_GET, 0) && op2(s, OP_LOCAL_GET, 1) && op2(s, OP_LOCAL_GET, 4) && op2(s, OP_LOCAL_GET, 2) &&
         op2(s, OP_LOCAL_GET, 4) && put_byte(s, OP_I64_SHR_U) && i64_const(s, 1) && put_byte(s, OP_I64_XOR) &&
         item_addr(s, 3, 4, 5) && call_internal(s, WASM_FUNC_TREE_HASH) && loop_next(s, 4) && put_byte(s, OP_END);
}

/* root_from_path (pubSeed, i, leaf, auth, out) (D34, rootFromPath): out :=
   leaf, then for k = 0 to H - 1, with q = i >> k and node a = auth node k
   (pathNode): out := th_node (pubSeed, 2, k, q >> 1, out then a) when q is
   even, else th_node (pubSeed, 2, k, q >> 1, a then out). Local 5 holds
   the saved heap pointer, 6 the two nodes, 7 k, 8 the count H, 9 q, 10 the
   address of node a. */
static int part_root_from_path(Sink *s, const void *arg) {
  const unsigned *height = arg;
  int ok = i64_locals(s, 6) && scratch_open(s, 5, 64, 6) && copy_words(s, 4, 0, 2, 0, 4) && i64_const(s, *height) &&
           op2(s, OP_LOCAL_SET, 8) && loop_open(s) && loop_exit(s, 7, 8) && op2(s, OP_LOCAL_GET, 1) &&
           op2(s, OP_LOCAL_GET, 7) && put_byte(s, OP_I64_SHR_U) && op2(s, OP_LOCAL_SET, 9) && item_addr(s, 3, 7, 5) &&
           op2(s, OP_LOCAL_SET, 10);
  /* The order of the two nodes: an odd q puts node a first. */
  ok = ok && op2(s, OP_LOCAL_GET, 9) && i64_const(s, 1) && put_byte(s, OP_I64_AND) && put_byte(s, OP_I32_WRAP_I64) &&
       put_byte(s, OP_IF) && put_byte(s, TYPE_EMPTY) && copy_words(s, 6, 0, 10, 0, 4) && copy_words(s, 6, 4, 4, 0, 4) &&
       put_byte(s, OP_ELSE) && copy_words(s, 6, 0, 4, 0, 4) && copy_words(s, 6, 4, 10, 0, 4) && put_byte(s, OP_END);
  return ok && op2(s, OP_LOCAL_GET, 0) && i64_const(s, 2) && op2(s, OP_LOCAL_GET, 7) && op2(s, OP_LOCAL_GET, 9) &&
         i64_const(s, 1) && put_byte(s, OP_I64_SHR_U) && op2(s, OP_LOCAL_GET, 6) && op2(s, OP_LOCAL_GET, 4) &&
         call_internal(s, WASM_FUNC_TH_NODE) && loop_next(s, 7) && scratch_close(s, 5) && put_byte(s, OP_END);
}

/* keygen (id, start, skSeed, skPrf, pubSeed) -> ks (D21, Q-S5-33 (b)). It
   traps when start > 2^H. Local 5 holds ks. */
static int part_signer_keygen(Sink *s, const void *arg) {
  const unsigned *height = arg;
  int ok = i64_locals(s, 1) && op2(s, OP_LOCAL_GET, 1) && i64_const(s, (uint64_t)1 << *height) &&
           put_byte(s, OP_I64_GT_U) && put_byte(s, OP_IF) && put_byte(s, TYPE_EMPTY) && put_byte(s, OP_UNREACHABLE) &&
           put_byte(s, OP_END) && i64_const(s, SIGNER_IO_BYTES) && op2(s, OP_GLOBAL_SET, 0) &&
           i32_const(s, 8u * KS_WORDS) && call_internal(s, WASM_FUNC_ALLOC) && op2(s, OP_LOCAL_SET, 5) &&
           block_addr(s, 5) && op2(s, OP_LOCAL_GET, 0) && memarg(s, OP_I64_STORE, KS_ID) && block_addr(s, 5) &&
           op2(s, OP_LOCAL_GET, 1) && memarg(s, OP_I64_STORE, KS_NEXT) && block_addr(s, 5) &&
           op2(s, OP_LOCAL_GET, 1) && memarg(s, OP_I64_STORE, KS_SIGNED) && copy_words(s, 5, KS_SK_SEED, 2, 0, 4) &&
           copy_words(s, 5, KS_SK_PRF, 3, 0, 4) && copy_words(s, 5, KS_PUB_SEED, 4, 0, 4);
  /* D28: the mark, then the root tree_hash (skSeed, pubSeed, H, 0) into the
     state block. After it the tree scratch is free. */
  return ok && op2(s, OP_GLOBAL_GET, 0) && op2(s, OP_GLOBAL_SET, 1) && word_addr(s, 5, KS_SK_SEED) &&
         word_addr(s, 5, KS_PUB_SEED) && i64_const(s, *height) && i64_const(s, 0) && word_addr(s, 5, KS_ROOT) &&
         call_internal(s, WASM_FUNC_TREE_HASH) && mark_reset(s) && op2(s, OP_LOCAL_GET, 5) && put_byte(s, OP_END);
}

/* publicKey (ks) -> pk (D27): a new block of the root, pubSeed and H. Local
   1 holds pk. */
static int part_signer_public_key(Sink *s, const void *arg) {
  const unsigned *height = arg;
  return i64_locals(s, 1) && mark_reset(s) && i32_const(s, 8u * PK_WORDS) && call_internal(s, WASM_FUNC_ALLOC) &&
         op2(s, OP_LOCAL_SET, 1) && copy_words(s, 1, PK_ROOT, 0, KS_ROOT, 4) &&
         copy_words(s, 1, PK_PUB_SEED, 0, KS_PUB_SEED, 4) && block_addr(s, 1) && i64_const(s, *height) &&
         memarg(s, OP_I64_STORE, PK_HEIGHT) && op2(s, OP_LOCAL_GET, 1) && put_byte(s, OP_END);
}

/* next (ks) -> leaf (D21): local 1 holds the position i. At i >= 2^H it
   gives 0 (none) and does not commit. Else it calls env.commit (id, i + 1),
   then it writes i + 1 to the state, and it gives the leaf handle i + 1. */
static int part_signer_next(Sink *s, const void *arg) {
  const unsigned *height = arg;
  return i64_locals(s, 1) && mark_reset(s) && block_word(s, 0, KS_NEXT) && op2(s, OP_LOCAL_SET, 1) &&
         op2(s, OP_LOCAL_GET, 1) && i64_const(s, (uint64_t)1 << *height) && put_byte(s, OP_I64_GE_U) &&
         put_byte(s, OP_IF) && put_byte(s, TYPE_I64) && i64_const(s, 0) && put_byte(s, OP_ELSE) &&
         block_word(s, 0, KS_ID) && plus_one(s, 1) && op2(s, OP_CALL, SIGNER_FUNC_COMMIT) && block_addr(s, 0) &&
         plus_one(s, 1) && memarg(s, OP_I64_STORE, KS_NEXT) && plus_one(s, 1) && put_byte(s, OP_END) &&
         put_byte(s, OP_END);
}

/* sign (ks, leaf, m) -> sig (D31, D32, Q-S5-38 (a)): leaf is a next result
   i + 1. It traps unless signed < leaf <= next (D20 words 2 and 1), then it
   writes signed := leaf before it signs. The sig block (273 + 4H words
   above the mark): word 0 i, 1 to 4 rand = prf (skPrf, 0, i), 5 to 272 the
   67 WOTS+ values of thMsg (rand, root, i, m), 273 and up the H auth nodes.
   Local 3 holds i, 4 sig, 5 the saved heap pointer, 6 the digest mh. */
static int part_signer_sign(Sink *s, const void *arg) {
  const unsigned *height = arg;
  int ok = i64_locals(s, 4) && op2(s, OP_LOCAL_GET, 1) && block_word(s, 0, KS_SIGNED) && put_byte(s, OP_I64_LE_U) &&
           trap_if(s) && op2(s, OP_LOCAL_GET, 1) && block_word(s, 0, KS_NEXT) && put_byte(s, OP_I64_GT_U) &&
           trap_if(s) && block_addr(s, 0) && op2(s, OP_LOCAL_GET, 1) && memarg(s, OP_I64_STORE, KS_SIGNED) &&
           op2(s, OP_LOCAL_GET, 1) && i64_const(s, 1) && put_byte(s, OP_I64_SUB) && op2(s, OP_LOCAL_SET, 3) &&
           mark_reset(s) && i32_const(s, 8u * (273u + 4u * *height)) && call_internal(s, WASM_FUNC_ALLOC) &&
           op2(s, OP_LOCAL_SET, 4) && store_local(s, 4, 0, 3) && word_addr(s, 0, KS_SK_PRF) && i64_const(s, 0) &&
           op2(s, OP_LOCAL_GET, 3) && word_addr(s, 4, 1) && call_internal(s, WASM_FUNC_PRF);
  /* mh in scratch, the WOTS+ values, then the auth path. */
  return ok && scratch_open(s, 5, 32, 6) && word_addr(s, 4, 1) && word_addr(s, 0, KS_ROOT) &&
         op2(s, OP_LOCAL_GET, 3) && op2(s, OP_LOCAL_GET, 2) && op2(s, OP_LOCAL_GET, 6) &&
         call_internal(s, WASM_FUNC_TH_MSG) && word_addr(s, 0, KS_SK_SEED) && word_addr(s, 0, KS_PUB_SEED) &&
         op2(s, OP_LOCAL_GET, 3) && op2(s, OP_LOCAL_GET, 6) && word_addr(s, 4, 5) &&
         call_internal(s, WASM_FUNC_WOTS_SIGN) && scratch_close(s, 5) && word_addr(s, 0, KS_SK_SEED) &&
         word_addr(s, 0, KS_PUB_SEED) && op2(s, OP_LOCAL_GET, 3) && word_addr(s, 4, 273) &&
         call_internal(s, WASM_FUNC_AUTH_PATH) && op2(s, OP_LOCAL_GET, 4) && put_byte(s, OP_END);
}

/* verify (pk, m, sig) -> 1 or 0 (D33, D35, Q-S5-39 (a), verifyAt): 0 when
   pk word 8 is not H. It traps when the sig index is 2^32 or more (the
   leaf word of wotsTweak). Else 1 when root_from_path of the L-tree of the
   WOTS+ public key of the sig values is the pk root. It does not set the
   heap pointer to the mark: it allocs its scratch at the heap pointer and
   sets the heap pointer back (D28). Local 3 holds i, 4 the saved heap
   pointer, 5 the scratch: mh at word 0, the 67 digests at 4, the leaf at
   272, the root at 276. */
static int part_signer_verify(Sink *s, const void *arg) {
  const unsigned *height = arg;
  int ok = i64_locals(s, 3) && block_word(s, 0, PK_HEIGHT) && i64_const(s, *height) && put_byte(s, OP_I64_NE) &&
           put_byte(s, OP_IF) && put_byte(s, TYPE_I64) && i64_const(s, 0) && put_byte(s, OP_ELSE) &&
           block_word(s, 2, 0) && i64_const(s, (uint64_t)1 << 32) && put_byte(s, OP_I64_GE_U) && trap_if(s) &&
           block_word(s, 2, 0) && op2(s, OP_LOCAL_SET, 3) && scratch_open(s, 4, 32u * 70u, 5) &&
           word_addr(s, 2, 1) && word_addr(s, 0, PK_ROOT) && op2(s, OP_LOCAL_GET, 3) && op2(s, OP_LOCAL_GET, 1) &&
           op2(s, OP_LOCAL_GET, 5) && call_internal(s, WASM_FUNC_TH_MSG) && word_addr(s, 0, PK_PUB_SEED) &&
           op2(s, OP_LOCAL_GET, 3) && op2(s, OP_LOCAL_GET, 5) && word_addr(s, 2, 5) && word_addr(s, 5, 4) &&
           call_internal(s, WASM_FUNC_WOTS_PK_FROM_SIG) && word_addr(s, 0, PK_PUB_SEED) && op2(s, OP_LOCAL_GET, 3) &&
           word_addr(s, 5, 4) && word_addr(s, 5, 272) && call_internal(s, WASM_FUNC_LTREE) &&
           word_addr(s, 0, PK_PUB_SEED) && op2(s, OP_LOCAL_GET, 3) && word_addr(s, 5, 272) && word_addr(s, 2, 273) &&
           word_addr(s, 5, 276) && call_internal(s, WASM_FUNC_ROOT_FROM_PATH);
  /* digestEq: the or of the 4 word xors is 0. */
  return ok && block_word(s, 5, 276) && block_word(s, 0, PK_ROOT) && put_byte(s, OP_I64_XOR) &&
         block_word(s, 5, 277) && block_word(s, 0, PK_ROOT + 1u) && put_byte(s, OP_I64_XOR) && put_byte(s, OP_I64_OR) &&
         block_word(s, 5, 278) && block_word(s, 0, PK_ROOT + 2u) && put_byte(s, OP_I64_XOR) && put_byte(s, OP_I64_OR) &&
         block_word(s, 5, 279) && block_word(s, 0, PK_ROOT + 3u) && put_byte(s, OP_I64_XOR) && put_byte(s, OP_I64_OR) &&
         put_byte(s, OP_I64_EQZ) && put_byte(s, OP_I64_EXTEND_I32_U) && scratch_close(s, 4) && put_byte(s, OP_END) &&
         put_byte(s, OP_END);
}

/* The functions after the import, in index order. The tree and sign
   functions (D29, D34) come before keygen, in the WASM_FUNC_* order. The
   exports sign and verify come after next (D31, D33). */
static const struct {
  const char *name; /* the export name, or NULL */
  unsigned type;    /* the index in SIGNER_TYPES */
  Part body;
} SIGNER_FUNCS[] = {
  {NULL, SIGNER_TYPE_ALLOC, part_alloc},
  {NULL, SIGNER_TYPE_KECCAK_F, part_keccak_f},
  {NULL, SIGNER_TYPE_KECCAK256, part_keccak256},
  {NULL, SIGNER_TYPE_TREE5, part_th_chain},
  {NULL, SIGNER_TYPE_TREE6, part_th_node},
  {NULL, SIGNER_TYPE_TREE4, part_prf},
  {NULL, SIGNER_TYPE_TREE6, part_chain},
  {NULL, SIGNER_TYPE_TREE4, part_wots_pk},
  {NULL, SIGNER_TYPE_TREE4, part_ltree},
  {NULL, SIGNER_TYPE_TREE5, part_tree_hash},
  {NULL, SIGNER_TYPE_TREE5, part_th_msg},
  {NULL, SIGNER_TYPE_COMMIT, part_wots_digits},
  {NULL, SIGNER_TYPE_TREE5, part_wots_sign},
  {NULL, SIGNER_TYPE_TREE5, part_wots_pk_from_sig},
  {NULL, SIGNER_TYPE_TREE4, part_auth_path},
  {NULL, SIGNER_TYPE_TREE5, part_root_from_path},
  {"keygen", SIGNER_TYPE_KEYGEN, part_signer_keygen},
  {"publicKey", SIGNER_TYPE_KS, part_signer_public_key},
  {"next", SIGNER_TYPE_KS, part_signer_next},
  {"sign", SIGNER_TYPE_SIGN, part_signer_sign},
  {"verify", SIGNER_TYPE_SIGN, part_signer_verify},
};

#define SIGNER_TYPE_COUNT (sizeof SIGNER_TYPES / sizeof SIGNER_TYPES[0])
#define SIGNER_FUNC_COUNT (sizeof SIGNER_FUNCS / sizeof SIGNER_FUNCS[0])

static int part_signer_types(Sink *s, const void *arg) {
  size_t i;
  unsigned k;
  int ok = put_uleb(s, SIGNER_TYPE_COUNT);
  (void)arg;
  for (i = 0; i < SIGNER_TYPE_COUNT && ok; i++) {
    ok = put_byte(s, TYPE_FUNC) && put_uleb(s, SIGNER_TYPES[i].params);
    for (k = 0; k < SIGNER_TYPES[i].params && ok; k++) ok = put_byte(s, SIGNER_TYPES[i].param);
    ok = ok && put_uleb(s, SIGNER_TYPES[i].results) && (SIGNER_TYPES[i].results == 0 || put_byte(s, TYPE_I64));
  }
  return ok;
}

static int part_signer_imports(Sink *s, const void *arg) {
  (void)arg;
  return put_uleb(s, SIGNER_IMPORTS) && put_name(s, "env") && put_name(s, "commit") && put_byte(s, 0) &&
         put_uleb(s, SIGNER_TYPE_COMMIT);
}

static int part_signer_functions(Sink *s, const void *arg) {
  size_t i;
  int ok = put_uleb(s, SIGNER_FUNC_COUNT);
  (void)arg;
  for (i = 0; i < SIGNER_FUNC_COUNT && ok; i++) ok = put_uleb(s, SIGNER_FUNCS[i].type);
  return ok;
}

/* Global 0 is the heap pointer, global 1 the mark (D19). */
static int part_signer_globals(Sink *s, const void *arg) {
  (void)arg;
  return put_uleb(s, 2) && global_i64(s) && global_i64(s);
}

/* memory, then each function with a name. */
static int part_signer_exports(Sink *s, const void *arg) {
  size_t i;
  size_t count = 1;
  int ok;
  (void)arg;
  for (i = 0; i < SIGNER_FUNC_COUNT; i++) count += SIGNER_FUNCS[i].name != NULL ? 1u : 0u;
  ok = put_uleb(s, count) && put_name(s, "memory") && put_byte(s, 2) && put_uleb(s, 0);
  for (i = 0; i < SIGNER_FUNC_COUNT && ok; i++) {
    ok = SIGNER_FUNCS[i].name == NULL ||
         (put_name(s, SIGNER_FUNCS[i].name) && put_byte(s, 0) && put_uleb(s, SIGNER_IMPORTS + i));
  }
  return ok;
}

/* ARG is the height H. */
static int part_signer_code(Sink *s, const void *arg) {
  size_t i;
  int ok = put_uleb(s, SIGNER_FUNC_COUNT);
  for (i = 0; i < SIGNER_FUNC_COUNT && ok; i++) ok = sized(s, SIGNER_FUNCS[i].body, arg);
  return ok;
}

int target_write_signer(unsigned height, FILE *out, FILE *err) {
  Sink s = {out, 0, err, SIGNER_IMPORTS};
  int ok = put_header(&s) && section(&s, SECTION_TYPE, part_signer_types, NULL) &&
           section(&s, SECTION_IMPORT, part_signer_imports, NULL) &&
           section(&s, SECTION_FUNCTION, part_signer_functions, NULL) && section(&s, SECTION_MEMORY, part_memory, NULL) &&
           section(&s, SECTION_GLOBAL, part_signer_globals, NULL) &&
           section(&s, SECTION_EXPORT, part_signer_exports, NULL) && section(&s, SECTION_CODE, part_signer_code, &height);
  if (!ok && ferror(out)) fputs("langc: IO: -: cannot write the wasm module\n", err);
  return ok;
}
