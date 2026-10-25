# leaf-lang

leaf-lang is a language for post-quantum accounts. A program signs with
XMSS one-time leaves and verifies XMSS signatures. Each leaf is an affine
resource: the checker refuses a program that can use one leaf two times.
The host is TinyCC. The planned targets are an EVM account verifier
(EIP-8141 VERIFY frame and ERC-4337), a Wasm signer with an EIP-8310
keystore, and the zkml-lang Circuit IR for aggregation.

## Status (S0)

This repository is the tcc-wasm kit seed of lang-template, the design and
the plan. No leaf-lang feature is built yet. `make check` passes on the kit
seed. `docs/PLAN.md` section 9 gives the slices.

## When to reach for leaf-lang

| Task | leaf-lang? |
|---|---|
| sign with XMSS or WOTS+ one-time keys | yes (S4, S5) |
| keep a stateful hash-based key so that no leaf is used two times | yes: the affine check (S3) |
| validate a post-quantum account signature on chain (EIP-8141, ERC-4337) | yes (S6) |
| prove many XMSS verifies in one circuit | yes (S7) |
| ECDSA, BLS, lattice signatures, SPHINCS+ | no: refused (`docs/PLAN.md` 3.3) |
| general-purpose code, storage, fees | no |

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
