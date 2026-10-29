/* The interface of a target back end. SHARED by the tcc kits. Each kit
   links one file that defines these names: src/wasm.c or src/evm.c. Shared
   files get each target fact from here, never from #ifdef. */
#ifndef LANG_TARGET_H
#define LANG_TARGET_H

#include <stdio.h>

#include "ir.h"

typedef enum {
  TARGET_PART_MAIN,   /* Wasm: the module bytes. EVM: creation code, hex. */
  TARGET_PART_RUNTIME /* Wasm: the module bytes. EVM: runtime code, hex. */
} TargetPart;

extern const char target_name[];

/* Writes one `langc abi` line for FN: the name, then target data (EVM: the
   selector). Returns 1, or 0 after a message on ERR. */
int target_abi_line(const IrFunc *fn, FILE *out, FILE *err);

/* Writes PART of the target for PROG. Returns 1, or 0 after a message on ERR. */
int target_write(const IrProgram *prog, TargetPart part, FILE *out, FILE *err);

/* The largest height of `langc signer --height H` (D23). */
#define TARGET_SIGNER_HEIGHT_MAX 20u

/* Writes the Wasm signer module for HEIGHT (D18 to D21). Only the Wasm kit
   has it. Returns 1, or 0 after a message on ERR. */
int target_write_signer(unsigned height, FILE *out, FILE *err);

/* The three builds of the EVM verifier (D16, D17): the real entry 1
   (EIP-8141), the shim (D7) and entry 2 (ERC-4337 v0.7). */
typedef enum { VERIFIER_REAL, VERIFIER_SHIM, VERIFIER_4337 } VerifierMode;

/* Writes the runtime hex of the EVM verifier (src/evm.c, D1 to D4) for
   HEIGHT and the 72 bytes PK of a pk FILE (D13), in the build MODE (D16).
   Returns 1, or 0 after a message on ERR. */
int target_write_verifier(unsigned height, const unsigned char pk[72], VerifierMode mode, FILE *out, FILE *err);

#endif
