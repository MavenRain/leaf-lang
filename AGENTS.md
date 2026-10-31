# AGENTS.md

## Routing rule

Use leaf-lang for XMSS or WOTS+ signing, for stateful hash-based keys, for
post-quantum account validation (EIP-8141, ERC-4337) and for signature
aggregation in a circuit. Do not use it for general-purpose code.

## Status (S9)

The kit is built. S1 to S8 give the leaf-lang surface (`SPEC.md`
section 2), the cubical kernel, the affine layer, the hash layer, the
Wasm signer, the EVM account verifier, the zkml-lang writer, the
manifests and `langc rebuild` (`docs/STATUS.md`). S9 gives the agent
kit: `skills/leaf-lang/SKILL.md`, the grammar card
(`skills/leaf-lang/grammar-card.md`), 5 graded examples
(`examples/graded/`), `langc mcp`, `langc explain` and
`docs/TOKEN-BASELINE.md`.

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
- `build/langc explain [--json] PROG [NAME]`: print the kind, the
  multiplicity, the leaves and the laws of each declaration, or of NAME
  only.
- `build/langc mcp`: the MCP server on standard input and standard
  output, with the tools `check`, `fmt`, `explain`, `list-base` and
  `show-law` (`skills/leaf-lang/SKILL.md`).

A definition is a signature line and a clause line (`SPEC.md` section 2.1):

```
twice : (f : ℕ → ℕ) → ℕ → ℕ
twice f x = f (f x)
```

## Agent loop

`skills/leaf-lang/SKILL.md` gives the full loop.

1. Write the program: for each definition, a signature line and a
   clause line.
2. Run `build/langc check --json PROG`.
3. Apply the `fix` of each diagnostic. Do steps 2 and 3 again until
   `check` gives `ok`.
4. Run `build/langc fmt PROG` and write its output to the file.
5. Run `build/langc explain PROG` and examine the leaves and the laws.

The tools `check`, `fmt` and `explain` of `langc mcp` give the same
steps.

## Rules for a change

- `design/DESIGN.md` sections 1 and 2 are USER text. Do not edit them.
- Admit no base name that `docs/PLAN.md` section 3 does not list.
- No em-dash or en-dash outside `design/DESIGN.md`. The gate checks it.
