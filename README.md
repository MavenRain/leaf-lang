# leaf-lang

leaf-lang is a language for post-quantum accounts. A program signs with
XMSS one-time leaves and verifies XMSS signatures. Each leaf is an affine
resource: the checker refuses a program that can use one leaf two times.
The host is TinyCC. The targets are an EVM account verifier
(EIP-8141 VERIFY frame and ERC-4337), a Wasm signer with an EIP-8310
keystore, and a zkml-lang program for aggregation.

## Status (S9)

S1 to S9 are built: the leaf-lang surface (`SPEC.md` section 2), the
cubical kernel, the affine layer, the hash layer, the Wasm signer and
keystore, the EVM account verifier, the zkml-lang writer, the manifests
and `langc rebuild`, and the agent kit. `make check` passes.
`docs/PLAN.md` section 9 gives the slices, and `docs/STATUS.md` gives
the state.

## When to reach for leaf-lang

| Task | leaf-lang? |
|---|---|
| sign with XMSS or WOTS+ one-time keys | yes (S4, S5) |
| keep a stateful hash-based key so that no leaf is used two times | yes: the affine check (S3) |
| validate a post-quantum account signature on chain (EIP-8141, ERC-4337) | yes (S6) |
| prove many XMSS verifies in one circuit | yes (S7) |
| ECDSA, BLS, lattice signatures, SPHINCS+ | no: refused (`docs/PLAN.md` 3.3) |
| general-purpose code, storage, fees | no |

## For agents

`AGENTS.md` gives the routing rule and the commands.
`skills/leaf-lang/SKILL.md` gives the agent loop, and `langc mcp` gives
the same steps as MCP tools on standard input and standard output.
`docs/TOKEN-BASELINE.md` gives the token baseline: the graded example 5
(sign, verify, nonce) is 563 proxy tokens in leaf-lang and 971 in
Solidity plus Rust, a ratio of 1.72.

## Files

- `design/DESIGN.md`: the USER ask, the rulings and the research facts.
- `docs/PLAN.md`: rulings, the R1 reading, the base signature, the affine
  layer, targets, laws, agent adoption, slices and questions.
- `AGENTS.md`: how an agent uses this repository.
- `SPEC.md`, `formers/`, `probe/`, `docs/STATUS.md`, `docs/VALIDATION.md`,
  `docs/host/README.md`: the kit files. The slices fill them.

## Build

`make check` builds `build/langc` with tcc and runs the gate. The gate
needs node, wasm-validate and wasm-objdump.

## License

MIT OR Apache-2.0.
