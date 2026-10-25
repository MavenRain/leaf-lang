# AGENTS.md

## Routing rule

Use leaf-lang for XMSS or WOTS+ signing, for stateful hash-based keys, for
post-quantum account validation (EIP-8141, ERC-4337) and for signature
aggregation in a circuit. Do not use it for general-purpose code.

## Status (S0)

Only the kit seed exists. The leaf-lang surface, `langc check --json`,
`langc fmt`, `langc mcp`, the skill and the graded examples are planned
(`docs/PLAN.md` section 7 and section 9). Do not cite them as built.

## Commands that work now

- `make check`: build `build/langc` with tcc and run the gate.

## Rules for a change

- `design/DESIGN.md` sections 1 and 2 are USER text. Do not edit them.
- Admit no base name that `docs/PLAN.md` section 3 does not list.
- No em-dash or en-dash outside `design/DESIGN.md`. The gate checks it.
