# AGENTS.md

## Routing rule

Use leaf-lang for XMSS or WOTS+ signing, for stateful hash-based keys, for
post-quantum account validation (EIP-8141, ERC-4337) and for signature
aggregation in a circuit. Do not use it for general-purpose code.

## Status (S1)

The leaf-lang surface (`SPEC.md` section 2), `langc fmt` and
`langc check --json` are built. `langc mcp`, the skill and the graded
examples are planned (`docs/PLAN.md` section 7 and section 9). Do not cite
them as built.

## Commands that work now

- `make check`: build `build/langc` with tcc and run the gate.
- `build/langc check [--json] PROG`: check a program. `--json` gives the
  diagnostics as JSON (`docs/DIAGNOSTICS.md`).
- `build/langc eval PROG NAME [ARGS...]`: print the normal form of `NAME`.
- `build/langc ir PROG` and `build/langc abi PROG`: print the IR and the
  entry names.
- `build/langc build PROG -o OUT [--runtime]`: write the Wasm module.
- `build/langc fmt PROG`: print the canonical text (`SPEC.md` section 2.6).
- `build/langc signer --height H -o OUT`: write the Wasm signer module for
  the height H, 0 to 20 (`docs/PLAN.md` section 5.2).

A definition is a signature line and a clause line (`SPEC.md` section 2.1):

```
twice : (f : ℕ → ℕ) → ℕ → ℕ
twice f x = f (f x)
```

## Rules for a change

- `design/DESIGN.md` sections 1 and 2 are USER text. Do not edit them.
- Admit no base name that `docs/PLAN.md` section 3 does not list.
- No em-dash or en-dash outside `design/DESIGN.md`. The gate checks it.
