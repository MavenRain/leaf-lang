/* The EVM verifier (D1 to D4, D13; Q-S6-1 (a)): fixed code that C writes
   with the assembler of src/asm.c, with no IR. The core checks one
   signature by PLAN 5.4 (thMsg, the 64 + 3 WOTS+ digits, the 67 chains,
   the L-tree, the auth path) against the pk constants. Three builds share
   the core (D16, D17). The real entry 1 (EIP-8141) reads the sig from
   entry 0 of the tx signatures, the nonce and sigHash by TXPARAM, and
   accepts by APPROVE. The shim (D7) reads them from the calldata (a 64-byte
   prefix, then the sig) and returns a 32-byte word 1. Entry 2 (ERC-4337
   v0.7, D17) reads them from the validateUserOp calldata (userOpHash is
   sigHash) after its head, and returns the word 0. The builds differ only
   in the 6 parts of one table (D16.2) and the entry 2 head. Each fault goes
   to one shared block (D4): REVERT, or the word 1 for entry 2 (Q-S6-3 (a)).
   The stack comments show the stack after the step, the top on the right. */
#include <stdint.h>
#include <stdio.h>

#include "asm.h"
#include "target.h"

/* The pk FILE (D13): the 9 publicKey words, 8 big-endian bytes each. */
enum { PK_ROOT_AT = 0, PK_SEED_AT = 32, PK_HEIGHT_AT = 64 };

/* The sig block (D3): word 0 the index i, 1 to 4 r, 5 to 272 the 67 WOTS+
   values, then the H auth nodes, each word as 8 big-endian bytes. In the
   shim it starts after the 64-byte prefix (nonce, sigHash). The head
   copies it to MEM_SIG (D16.1). */
enum { SIG_R = 8, SIG_VALUES = 40, SIG_AUTH = 2184, SHIM_SIG = 64 };

/* The scratch memory. thChain hashes MEM_SEED to MEM_R (96 bytes), thNode
   MEM_SEED to MEM_R + 32 (128 bytes). */
enum {
  MEM_SEED = 0x00,   /* pubSeed */
  MEM_ADRS = 0x20,   /* the ADRS: 8 big-endian 32-bit words */
  MEM_X = 0x40,      /* x of thChain, l of thNode */
  MEM_R = 0x60,      /* r of thNode */
  MEM_MSG = 0x100,   /* the 160 bytes of thMsg */
  MEM_ENDS = 0x200,  /* the 67 chain ends, then the L-tree nodes in place */
  MEM_DIGITS = 0xb00, /* the 67 digits, one byte each */
  MEM_SIG = 0xc00     /* the sig block (D16.1): 2184 + 32H bytes */
};

/* Entry 2 (D17): the validateUserOp selector. UOP_AT, UOP_HASH and UOP_FUNDS
   are the calldata offsets of the userOp offset t, userOpHash and
   missingAccountFunds. The nonce is at t + UOP_NONCE (the selector and one
   word), and the signature offset word at t + UOP_AT + UOP_SIG (word 8 of
   the PackedUserOperation). */
enum {
  UOP_SELECTOR = 0x19822f7c,
  UOP_AT = 4,
  UOP_HASH = UOP_AT + 32,
  UOP_FUNDS = UOP_AT + 64,
  UOP_NONCE = UOP_AT + 32,
  UOP_SIG = 8 * 32
};

/* The v0.7 EntryPoint (a build constant, PLAN 5.1) as a 32-byte word. */
static const unsigned char ENTRY_POINT[32] = {0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
                                              0,    0,    0,    0,    0,    0x71, 0x72, 0x7d, 0xe2, 0x2e, 0x5e,
                                              0x9d, 0x8b, 0xaf, 0x0e, 0xda, 0xc6, 0xf3, 0x7d, 0xa0, 0x32};

/* PUSH V, then CODE. */
static void push_op(Asm *a, uint64_t v, Op code) {
  asm_push(a, v);
  asm_op(a, code);
}

/* Memory at OFF := the top, by CODE (MSTORE or MSTORE8). */
static void store_at(Asm *a, uint64_t off, Op code) {
  push_op(a, off, code);
}

/* The top n := AT + 32 n: the address of 32-byte item n. */
static void item(Asm *a, uint64_t at) {
  push_op(a, 5, EVM_OP_SHL);
  push_op(a, at, EVM_OP_ADD);
}

/* The build (D16): the mode and the sig length 2184 + 32H. */
typedef struct {
  VerifierMode mode;
  unsigned len;
} Build;

/* The 6 table sites (D16.2, D17), in code order. */
typedef enum { SITE_LEN, SITE_COPY, SITE_NONCE, SITE_SIGHASH, SITE_ACCEPT, SITE_FAIL } Site;

/* A site part: N code bytes. SIG_LEN_HI and SIG_LEN_LO stand for the two
   bytes of the sig length (a PUSH2 immediate). */
enum { SITES = 6, PART_MAX = 15, SIG_LEN_HI = 0x100, SIG_LEN_LO = 0x101 };
typedef struct {
  unsigned n;
  unsigned code[PART_MAX];
} Part;

/* The table (D16.2, PLAN O1), one row for each build. LEN leaves the sig
   length (entry 2: the address of the length word under it, and COPY
   takes that address). COPY copies the sig to MEM_SIG. NONCE and SIGHASH
   leave the word. ACCEPT ends the frame with the word 1 at memory 0:
   APPROVE with the scope 3 (execution and payment), or RETURN; entry 2
   returns the word 0. FAIL ends the frame on a fault: REVERT, or entry 2
   returns the word 1 (Q-S6-3 (a)). */
static const Part TABLE[3][SITES] = {
    [VERIFIER_REAL] = {
        [SITE_LEN] = {4, {EVM_OP_PUSH1, 3, EVM_OP_PUSH0, EVM_OP_SIGPARAM}},
        [SITE_COPY] = {9, {EVM_OP_PUSH0, EVM_OP_PUSH2, SIG_LEN_HI, SIG_LEN_LO, EVM_OP_PUSH0, EVM_OP_PUSH2,
                           MEM_SIG >> 8, MEM_SIG & 0xff, EVM_OP_SIGDATACOPY}},
        [SITE_NONCE] = {3, {EVM_OP_PUSH1, 1, EVM_OP_TXPARAM}},
        [SITE_SIGHASH] = {3, {EVM_OP_PUSH1, 8, EVM_OP_TXPARAM}},
        [SITE_ACCEPT] = {6, {EVM_OP_PUSH1, 3, EVM_OP_PUSH1, 32, EVM_OP_PUSH0, EVM_OP_APPROVE}},
        [SITE_FAIL] = {3, {EVM_OP_PUSH0, EVM_OP_PUSH0, EVM_OP_REVERT}},
    },
    [VERIFIER_SHIM] = {
        [SITE_LEN] = {4, {EVM_OP_PUSH1, SHIM_SIG, EVM_OP_CALLDATASIZE, EVM_OP_SUB}},
        [SITE_COPY] = {9, {EVM_OP_PUSH2, SIG_LEN_HI, SIG_LEN_LO, EVM_OP_PUSH1, SHIM_SIG, EVM_OP_PUSH2,
                           MEM_SIG >> 8, MEM_SIG & 0xff, EVM_OP_CALLDATACOPY}},
        [SITE_NONCE] = {2, {EVM_OP_PUSH0, EVM_OP_CALLDATALOAD}},
        [SITE_SIGHASH] = {3, {EVM_OP_PUSH1, 32, EVM_OP_CALLDATALOAD}},
        [SITE_ACCEPT] = {4, {EVM_OP_PUSH1, 32, EVM_OP_PUSH0, EVM_OP_RETURN}},
        [SITE_FAIL] = {3, {EVM_OP_PUSH0, EVM_OP_PUSH0, EVM_OP_REVERT}},
    },
    [VERIFIER_4337] = {
        [SITE_LEN] = {15, {EVM_OP_PUSH1, UOP_AT, EVM_OP_CALLDATALOAD, EVM_OP_PUSH1, UOP_AT, EVM_OP_ADD, EVM_OP_DUP1,
                           EVM_OP_PUSH2, UOP_SIG >> 8, UOP_SIG & 0xff, EVM_OP_ADD, EVM_OP_CALLDATALOAD, EVM_OP_ADD,
                           EVM_OP_DUP1, EVM_OP_CALLDATALOAD}},
        [SITE_COPY] = {11, {EVM_OP_PUSH2, SIG_LEN_HI, SIG_LEN_LO, EVM_OP_SWAP1, EVM_OP_PUSH1, 32, EVM_OP_ADD,
                            EVM_OP_PUSH2, MEM_SIG >> 8, MEM_SIG & 0xff, EVM_OP_CALLDATACOPY}},
        [SITE_NONCE] = {7, {EVM_OP_PUSH1, UOP_AT, EVM_OP_CALLDATALOAD, EVM_OP_PUSH1, UOP_NONCE, EVM_OP_ADD,
                            EVM_OP_CALLDATALOAD}},
        [SITE_SIGHASH] = {3, {EVM_OP_PUSH1, UOP_HASH, EVM_OP_CALLDATALOAD}},
        [SITE_ACCEPT] = {7, {EVM_OP_PUSH0, EVM_OP_PUSH0, EVM_OP_MSTORE, EVM_OP_PUSH1, 32, EVM_OP_PUSH0, EVM_OP_RETURN}},
        [SITE_FAIL] = {8, {EVM_OP_PUSH1, 1, EVM_OP_PUSH0, EVM_OP_MSTORE, EVM_OP_PUSH1, 32, EVM_OP_PUSH0,
                           EVM_OP_RETURN}},
    },
};

/* The byte CODE of a part, with the sig length LEN in place of SIG_LEN_HI
   and SIG_LEN_LO. */
static unsigned part_byte(unsigned code, unsigned len) {
  if (code == SIG_LEN_HI) return len >> 8;
  return code == SIG_LEN_LO ? (len & 0xff) : code;
}

/* Site S of the build B (D16.3): JUMPDEST pads up to the width of the
   longer part of the two entry 1 builds, then the part of B. So those two
   runtimes have the same length and the same jump targets. Entry 2 (D17)
   is its own runtime: no pads. */
static void site(Asm *a, const Build *b, Site s) {
  const Part *real = &TABLE[VERIFIER_REAL][s], *shim = &TABLE[VERIFIER_SHIM][s], *own = &TABLE[b->mode][s];
  unsigned pair = real->n > shim->n ? real->n : shim->n;
  unsigned width = b->mode == VERIFIER_4337 ? own->n : pair;
  for (unsigned k = own->n; k < width; k++) asm_op(a, EVM_OP_JUMPDEST);
  for (unsigned k = 0; k < own->n; k++) asm_put(a, part_byte(own->code[k], b->len));
}

/* Keep the offset on the stack and refuse a span outside calldata. Check
   the size before subtraction so neither subtraction nor offset addition
   can wrap. [offset] to [offset]. */
static void calldata_span(Asm *a, unsigned len, Label fail) {
  asm_op(a, EVM_OP_CALLDATASIZE);
  asm_push(a, len);
  asm_op(a, EVM_OP_DUP2);
  asm_op(a, EVM_OP_LT);
  asm_jump_if(a, fail);
  asm_push(a, len);
  asm_op(a, EVM_OP_SWAP1);
  asm_op(a, EVM_OP_SUB);
  asm_op(a, EVM_OP_DUP2);
  asm_op(a, EVM_OP_GT);
  asm_jump_if(a, fail);
}

/* The entry 2 head (D17, Q-S6-3 (a)): a wrong selector or a caller that is
   not the v0.7 EntryPoint reverts. Then missingAccountFunds goes to the
   caller by a CALL with all the gas. The result is not checked (as the
   v0.7 BaseAccount `_payPrefund`), and 0 skips the CALL. [] to []. */
static void entry2_head(Asm *a, unsigned sig_len, Label fail) {
  Label bad = asm_label(a), from_entry_point = asm_label(a), paid = asm_label(a);
  push_op(a, 0, EVM_OP_CALLDATALOAD);
  push_op(a, 224, EVM_OP_SHR);
  push_op(a, UOP_SELECTOR, EVM_OP_EQ);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, bad); /* [] */
  asm_op(a, EVM_OP_CALLER);
  asm_push_word(a, ENTRY_POINT);
  asm_op(a, EVM_OP_EQ);
  asm_jump_if(a, from_entry_point); /* [] */
  asm_jumpdest(a, bad);
  asm_op(a, EVM_OP_PUSH0);
  push_op(a, 0, EVM_OP_REVERT);
  asm_jumpdest(a, from_entry_point);
  push_op(a, UOP_FUNDS, EVM_OP_CALLDATALOAD);
  asm_op(a, EVM_OP_DUP1);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, paid); /* [funds] */
  for (unsigned k = 0; k < 4; k++) asm_op(a, EVM_OP_PUSH0);
  asm_op(a, EVM_OP_DUP5);
  asm_op(a, EVM_OP_CALLER);
  asm_op(a, EVM_OP_GAS);
  asm_op(a, EVM_OP_CALL);
  asm_op(a, EVM_OP_POP); /* [funds] */
  asm_jumpdest(a, paid);
  asm_op(a, EVM_OP_POP); /* [] */
  /* The tuple has nine head words. Bound its relative offset before ADD,
     then bound the signature length word and all signature bytes. EVM
     calldata reads pad missing bytes with zeros, so a length word alone
     does not establish that the signature is present (Q-S6-6 (a)). */
  push_op(a, UOP_AT, EVM_OP_CALLDATALOAD);
  calldata_span(a, UOP_AT + 9 * 32, fail);
  push_op(a, UOP_AT, EVM_OP_ADD); /* [tuple] */
  asm_op(a, EVM_OP_DUP1);
  push_op(a, UOP_SIG, EVM_OP_ADD);
  asm_op(a, EVM_OP_CALLDATALOAD); /* [tuple offset] */
  asm_op(a, EVM_OP_DUP2);
  asm_op(a, EVM_OP_ADD); /* [tuple length_at] */
  asm_op(a, EVM_OP_DUP1);
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_GT);
  asm_jump_if(a, fail); /* tuple > length_at means ADD overflowed */
  calldata_span(a, 32 + sig_len, fail);
  asm_op(a, EVM_OP_POP);
  asm_op(a, EVM_OP_POP); /* [] */
}

/* The LEN site and the length check (D3), the COPY site, then i := sig
   word 0, the NONCE site and the nonce compare (D5), and i < 2^32 (the
   ADRS leaf field). [] to [i]. */
static void head(Asm *a, const Build *b, Label fail) {
  site(a, b, SITE_LEN);
  push_op(a, b->len, EVM_OP_EQ);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, fail);
  site(a, b, SITE_COPY);
  push_op(a, MEM_SIG, EVM_OP_MLOAD);
  push_op(a, 192, EVM_OP_SHR);
  asm_op(a, EVM_OP_DUP1);
  site(a, b, SITE_NONCE);
  asm_op(a, EVM_OP_EQ);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, fail);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 32, EVM_OP_SHR);
  asm_jump_if(a, fail);
}

/* pubSeed to MEM_SEED, then m' = thMsg (r, root, i, sigHash) (sigHash is
   the SIGHASH site) and its 67 digits to MEM_DIGITS. Digit c < 64 is (m' >>
   (252 - 4c)) & 15, the high half of a byte first. The digits 64 to 66 are
   csum >> 8, (csum >> 4) & 15 and csum & 15, with csum = 960 - the sum of
   the digits 0 to 63 (the sum of 15 - digit c). [i] to [i]. */
static void digits(Asm *a, const Build *b, const unsigned char *pk) {
  Label loop = asm_label(a);
  asm_push_word(a, pk + PK_SEED_AT);
  store_at(a, MEM_SEED, EVM_OP_MSTORE);
  asm_push(a, 2);
  store_at(a, MEM_MSG, EVM_OP_MSTORE);
  push_op(a, MEM_SIG + SIG_R, EVM_OP_MLOAD);
  store_at(a, MEM_MSG + 32, EVM_OP_MSTORE);
  asm_push_word(a, pk + PK_ROOT_AT);
  store_at(a, MEM_MSG + 64, EVM_OP_MSTORE);
  asm_op(a, EVM_OP_DUP1);
  store_at(a, MEM_MSG + 96, EVM_OP_MSTORE);
  site(a, b, SITE_SIGHASH);
  store_at(a, MEM_MSG + 128, EVM_OP_MSTORE);
  asm_push(a, 160);
  push_op(a, MEM_MSG, EVM_OP_SHA3); /* [i m'] */
  asm_push(a, 0);
  asm_push(a, 0); /* [i m' sum c] */
  asm_jumpdest(a, loop);
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_DUP2);
  push_op(a, 2, EVM_OP_SHL);
  push_op(a, 252, EVM_OP_SUB);
  asm_op(a, EVM_OP_SHR);
  push_op(a, 15, EVM_OP_AND); /* [i m' sum c d] */
  asm_op(a, EVM_OP_DUP1);
  asm_op(a, EVM_OP_DUP3);
  push_op(a, MEM_DIGITS, EVM_OP_ADD);
  asm_op(a, EVM_OP_MSTORE8);
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_ADD);
  asm_op(a, EVM_OP_SWAP2);
  asm_op(a, EVM_OP_POP); /* [i m' sum c] */
  push_op(a, 1, EVM_OP_ADD);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 64, EVM_OP_GT);
  asm_jump_if(a, loop);
  asm_op(a, EVM_OP_POP);
  push_op(a, 960, EVM_OP_SUB); /* [i m' csum] */
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 8, EVM_OP_SHR);
  store_at(a, MEM_DIGITS + 64, EVM_OP_MSTORE8);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 4, EVM_OP_SHR);
  push_op(a, 15, EVM_OP_AND);
  store_at(a, MEM_DIGITS + 65, EVM_OP_MSTORE8);
  push_op(a, 15, EVM_OP_AND);
  store_at(a, MEM_DIGITS + 66, EVM_OP_MSTORE8);
  asm_op(a, EVM_OP_POP);
}

/* Chain c (D4) steps value c from digit c to 15 by thChain with the ADRS
   kind 0, leaf i, chain c and step j. The end goes to MEM_ENDS + 32c.
   [i] to [i]. */
static void chains(Asm *a) {
  Label chain = asm_label(a), step = asm_label(a), done = asm_label(a);
  asm_push(a, 0); /* [i c] */
  asm_jumpdest(a, chain);
  asm_op(a, EVM_OP_DUP1);
  item(a, MEM_SIG + SIG_VALUES);
  asm_op(a, EVM_OP_MLOAD);
  store_at(a, MEM_X, EVM_OP_MSTORE);
  asm_op(a, EVM_OP_DUP2);
  push_op(a, 96, EVM_OP_SHL);
  asm_op(a, EVM_OP_DUP2);
  push_op(a, 64, EVM_OP_SHL);
  asm_op(a, EVM_OP_OR); /* the ADRS without the step: [i c base] */
  asm_op(a, EVM_OP_DUP2);
  push_op(a, MEM_DIGITS, EVM_OP_ADD);
  asm_op(a, EVM_OP_MLOAD);
  push_op(a, 248, EVM_OP_SHR); /* j := digit c: [i c base j] */
  asm_jumpdest(a, step);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 15, EVM_OP_GT);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, done);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 32, EVM_OP_SHL);
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_OR);
  store_at(a, MEM_ADRS, EVM_OP_MSTORE);
  asm_push(a, 96);
  push_op(a, MEM_SEED, EVM_OP_SHA3);
  store_at(a, MEM_X, EVM_OP_MSTORE);
  push_op(a, 1, EVM_OP_ADD);
  asm_jump(a, step);
  asm_jumpdest(a, done);
  asm_op(a, EVM_OP_POP);
  asm_op(a, EVM_OP_POP); /* [i c] */
  push_op(a, MEM_X, EVM_OP_MLOAD);
  asm_op(a, EVM_OP_DUP2);
  item(a, MEM_ENDS);
  asm_op(a, EVM_OP_MSTORE);
  push_op(a, 1, EVM_OP_ADD);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 67, EVM_OP_GT);
  asm_jump_if(a, chain);
  asm_op(a, EVM_OP_POP);
}

/* The L-tree (D4, RFC 8391 Algorithm 16) in place on the 67 ends. While
   len > 1: node j of level k + 1 is thNode (ADRS kind 1, leaf i, chain k,
   step j) of the nodes 2j and 2j + 1 for j < len / 2, an odd last node
   moves to len / 2, then len := (len + 1) / 2 and k := k + 1. The leaf is
   node 0. [i] to [i]. */
static void ltree(Asm *a) {
  Label level = asm_label(a), node = asm_label(a), nodes_done = asm_label(a), even = asm_label(a),
        done = asm_label(a);
  asm_push(a, 67);
  asm_push(a, 0); /* [i len k] */
  asm_jumpdest(a, level);
  asm_op(a, EVM_OP_DUP2);
  push_op(a, 1, EVM_OP_LT);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, done);
  asm_op(a, EVM_OP_DUP3);
  push_op(a, 96, EVM_OP_SHL);
  asm_op(a, EVM_OP_DUP2);
  push_op(a, 64, EVM_OP_SHL);
  asm_op(a, EVM_OP_OR);
  asm_push(a, 1);
  push_op(a, 128, EVM_OP_SHL);
  asm_op(a, EVM_OP_OR);
  asm_push(a, 0); /* [i len k base j] */
  asm_jumpdest(a, node);
  asm_op(a, EVM_OP_DUP4);
  push_op(a, 1, EVM_OP_SHR);
  asm_op(a, EVM_OP_DUP2);
  asm_op(a, EVM_OP_LT);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, nodes_done);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 32, EVM_OP_SHL);
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_OR);
  store_at(a, MEM_ADRS, EVM_OP_MSTORE);
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 6, EVM_OP_SHL);
  push_op(a, MEM_ENDS, EVM_OP_ADD); /* the address of node 2j */
  asm_op(a, EVM_OP_DUP1);
  asm_op(a, EVM_OP_MLOAD);
  store_at(a, MEM_X, EVM_OP_MSTORE);
  push_op(a, 32, EVM_OP_ADD);
  asm_op(a, EVM_OP_MLOAD);
  store_at(a, MEM_R, EVM_OP_MSTORE);
  asm_push(a, 128);
  push_op(a, MEM_SEED, EVM_OP_SHA3);
  asm_op(a, EVM_OP_DUP2);
  item(a, MEM_ENDS);
  asm_op(a, EVM_OP_MSTORE);
  push_op(a, 1, EVM_OP_ADD);
  asm_jump(a, node);
  asm_jumpdest(a, nodes_done);
  asm_op(a, EVM_OP_POP);
  asm_op(a, EVM_OP_POP); /* [i len k] */
  asm_op(a, EVM_OP_DUP2);
  push_op(a, 1, EVM_OP_AND);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, even);
  asm_push(a, 1);
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_SUB);
  item(a, MEM_ENDS);
  asm_op(a, EVM_OP_MLOAD);
  asm_op(a, EVM_OP_DUP3);
  push_op(a, 1, EVM_OP_SHR);
  item(a, MEM_ENDS);
  asm_op(a, EVM_OP_MSTORE);
  asm_jumpdest(a, even);
  push_op(a, 1, EVM_OP_ADD);
  asm_op(a, EVM_OP_SWAP1);
  push_op(a, 1, EVM_OP_ADD);
  push_op(a, 1, EVM_OP_SHR);
  asm_op(a, EVM_OP_SWAP1);
  asm_jump(a, level);
  asm_jumpdest(a, done);
  asm_op(a, EVM_OP_POP);
  asm_op(a, EVM_OP_POP);
}

/* The auth path (D4): node := the leaf, then for k < H, with q = i >> k
   and a = auth node k, node := thNode (ADRS kind 2, chain k, step q >> 1)
   of node then a for an even q, else of a then node. Then the root
   compare. [i] to []. */
static void auth(Asm *a, unsigned height, const unsigned char *pk, Label fail) {
  Label level = asm_label(a), done = asm_label(a);
  push_op(a, MEM_ENDS, EVM_OP_MLOAD);
  asm_push(a, 0); /* [i node k] */
  asm_jumpdest(a, level);
  asm_push(a, height);
  asm_op(a, EVM_OP_DUP2);
  asm_op(a, EVM_OP_LT);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, done);
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_DUP2);
  asm_op(a, EVM_OP_SHR); /* [i node k q] */
  asm_op(a, EVM_OP_DUP1);
  push_op(a, 1, EVM_OP_SHR);
  push_op(a, 32, EVM_OP_SHL);
  asm_op(a, EVM_OP_DUP3);
  push_op(a, 64, EVM_OP_SHL);
  asm_op(a, EVM_OP_OR);
  asm_push(a, 2);
  push_op(a, 128, EVM_OP_SHL);
  asm_op(a, EVM_OP_OR);
  store_at(a, MEM_ADRS, EVM_OP_MSTORE);
  asm_op(a, EVM_OP_DUP2);
  item(a, MEM_SIG + SIG_AUTH);
  asm_op(a, EVM_OP_MLOAD); /* [i node k q a] */
  asm_op(a, EVM_OP_SWAP1);
  push_op(a, 1, EVM_OP_AND);
  push_op(a, 5, EVM_OP_SHL); /* o := 32 (q & 1): [i node k a o] */
  asm_op(a, EVM_OP_SWAP1);
  asm_op(a, EVM_OP_DUP2);
  push_op(a, MEM_R, EVM_OP_SUB);
  asm_op(a, EVM_OP_MSTORE); /* a to MEM_R - o: [i node k o] */
  asm_op(a, EVM_OP_DUP3);
  asm_op(a, EVM_OP_SWAP1);
  push_op(a, MEM_X, EVM_OP_ADD);
  asm_op(a, EVM_OP_MSTORE); /* node to MEM_X + o: [i node k] */
  asm_push(a, 128);
  push_op(a, MEM_SEED, EVM_OP_SHA3);
  asm_op(a, EVM_OP_SWAP2);
  asm_op(a, EVM_OP_POP);
  push_op(a, 1, EVM_OP_ADD);
  asm_jump(a, level);
  asm_jumpdest(a, done);
  asm_op(a, EVM_OP_POP); /* [i node] */
  asm_push_word(a, pk + PK_ROOT_AT);
  asm_op(a, EVM_OP_EQ);
  asm_op(a, EVM_OP_ISZERO);
  asm_jump_if(a, fail);
  asm_op(a, EVM_OP_POP);
}

int target_write_verifier(unsigned height, const unsigned char pk[72], VerifierMode mode, FILE *out, FILE *err) {
  uint64_t pk_height = 0;
  for (unsigned b = 0; b < 8; b++) pk_height = pk_height << 8 | pk[PK_HEIGHT_AT + b];
  if (pk_height != height) {
    fprintf(err, "langc: USAGE: -: the pk height %llu is not --height %u\n", (unsigned long long)pk_height, height);
    return 0;
  }
  Asm rt;
  asm_init(&rt);
  Label fail = asm_label(&rt);
  Build build = {mode, SIG_AUTH + 32u * height};
  if (mode == VERIFIER_4337) entry2_head(&rt, build.len, fail);
  head(&rt, &build, fail);
  digits(&rt, &build, pk);
  chains(&rt);
  ltree(&rt);
  auth(&rt, height, pk, fail);
  /* The word 1 to memory 0, then the ACCEPT site (entry 2 returns the word 0). */
  asm_push(&rt, 1);
  store_at(&rt, 0, EVM_OP_MSTORE);
  site(&rt, &build, SITE_ACCEPT);
  /* The one failure block (D4): the FAIL site. */
  asm_jumpdest(&rt, fail);
  site(&rt, &build, SITE_FAIL);
  if (!asm_finish(&rt, err)) return 0;
  if (rt.size > ASM_RUNTIME_MAX) {
    fprintf(err, "langc: EVM_SIZE: -: the runtime has %zu bytes, the limit is %d\n", rt.size, ASM_RUNTIME_MAX);
    return 0;
  }
  return asm_write_hex(&rt, out, err);
}
