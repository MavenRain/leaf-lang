---
name: leaf-lang
description: Write, check and format leaf-lang programs for XMSS or WOTS+ signing, stateful hash-based keys, post-quantum account validation (EIP-8141, ERC-4337) and signature aggregation in a circuit. Use the langc check, fmt and explain loop or the 5 tools of langc mcp.
---

# leaf-lang

## When to use leaf-lang

The routing rule of `AGENTS.md`: use leaf-lang for XMSS or WOTS+ signing,
for stateful hash-based keys, for post-quantum account validation
(EIP-8141, ERC-4337) and for signature aggregation in a circuit. Do not use
it for general-purpose code.

## Before you start

- `make build` makes `build/langc`. Run `langc` from the repository root.
- `langc` exit codes: 0 is ok, 1 is a refused program, 2 is a usage or IO
  error.
- Read `grammar-card.md` (in this directory) for the forms and the base
  names. Start from the graded example nearest to the task
  (`examples/graded/`).

## The loop

1. Write the program in a `.lang` file: for each definition, a signature
   line and a clause line.
2. Run `build/langc check --json PROG`. Each diagnostic is one JSON line
   (`docs/DIAGNOSTICS.md`, "The JSON record"). The progress object
   (`parsed`, `typed`, `affine`, `laws`) gives the number of declarations
   that pass each stage.
3. Apply the `fix` of each diagnostic where `fix` is not `null`. For
   `W_NONCANON`, the `fix` is the canonical text: replace the lines of the
   `span` with it. Where `fix` is `null`, use `expected`, `found` and
   `message` to change the program. Where `retry` is `false`, a change to
   the program text cannot remove the diagnostic: stop and report it.
4. Run `check` again. Do steps 2 to 4 again until `build/langc check PROG`
   gives `ok` (exit 0).
5. Run `build/langc fmt PROG` and write its output to the file. `fmt`
   prints only the canonical spellings.
6. Run `build/langc explain PROG [NAME]` (or `explain --json`) and examine
   each declaration: its kind, its type, the multiplicity of each binder
   (`0`, `1` or `ω`), its leaves and its laws.

Example (`examples/graded/1-nat-function.lang`):

    $ build/langc explain examples/graded/1-nat-function.lang square
    square : (n : ℕ) → ℕ
      kind: def
      binders: n ω
      leaves: none
      laws: none

## The tools of `langc mcp`

`langc mcp` is an MCP server on standard input and standard output. It has
5 tools:

| Tool | Input | Output |
|---|---|---|
| `check` | `source` (the program text) | the `langc check --json` lines |
| `fmt` | `source` | the canonical text, or the diagnostic as one JSON line |
| `explain` | `source`, `name` (optional: one declaration) | the `langc explain --json` lines |
| `list-base` | none | the names of the base signature, one on each line |
| `show-law` | `id` (`L1` to `L7`) | the law as one JSON line: the text, the slice and the check |

The loop with the tools: `check`, apply each `fix`, `check` again until
the program is ok, then `fmt`, then `explain`.

## Pointers

- `SPEC.md`: the language. Section 2 has the programs, the grammar, the
  aliases and the layout.
- `docs/DIAGNOSTICS.md`: the JSON record, the progress object and each
  code.
- `grammar-card.md`: the short form of the grammar, the multiplicities and
  the base names.
- `examples/graded/`: 5 good examples, from easy (1) to the full task (5).
  `check` gives `ok` on each of them.
