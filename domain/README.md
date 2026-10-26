# Domain slot

Replace the sample domain with the design of your language.
`domain/domain.lang` holds the domain. At build time, `tcc -run
gen/embed.c` copies it into `build/domain.c`, so the domain is part of
`build/langc`. `langc` loads the domain first, then the program.

## What the file holds

- `data` declarations. Only this file can declare a data type. In a
  program, `data` is refused with `REFUSE_DATA` (rule R1).
- Operations, written in the language. Each operation is a signature
  line and a clause line, as in a program (`SPEC.md` section 2.1).
- Line comments that start with `--`.

Each data type, constructor, field and definition name in this file
becomes a core name. A program cannot use one of them as a new definition
name (rule R4, `REFUSE_NAME`).

## Data type syntax

```
data NAME (param : T)* : Type where
  ctor : (field : T) → (field : T) → NAME param*
  ctor : NAME param*
```

`SPEC.md` section 2.1 gives the full grammar.

- Each constructor is on its own line after the `data` line. A
  constructor gives its fields as named Pi binders and ends in the type
  that it makes.
- A data type with only nullary constructors is an enum.
- A data type with one constructor is a record. It gets one projection
  for each field name.
- A field can have the type of its own data type. Such a field is
  recursive.
- A parameter is an index (former F15). A constructor takes the indices
  first: `makeLot 3 250 : Lot 3`.
- A `fold` over a data type takes one argument for each constructor, in
  declaration order. A constant constructor takes a value. A constructor
  with fields takes a function. A recursive field arrives folded.

In the IR, a constructor value is a heap block. Word 0 is the constructor
index, in declaration order. Each field has a word and a trap flag.
A `fold` over a data type with one recursive field lowers to a loop.
A data type with two or more recursive fields cannot be folded at run
time (`REFUSE_RUNTIME_TREE`, see `docs/host/CAPABILITY.md`).

## The sample

| Data type | Kind | Use |
|---|---|---|
| `Color` | enum: `red`, `green`, `blue` | `isRed`, and `colorCode` in `examples/algebra.lang` |
| `Item` | record: `itemColor`, `itemPrice`, `itemCount` | `lineTotal` |
| `Stack` | one recursive field, `rest` | `depth`, and the spine loop in `test/ir/spine.lang` |
| `Tree` | two recursive fields | the tree mutant `test/ir/tree.lang` |
| `Lot (size : ℕ)` | indexed record | `examples/inventory.lang`, `examples/laws.lang` |

The definitions are `lineTotal`, `isRed` and `depth`.

`data Tree` exists only for the tree mutant. The mutant checks that the
lowering refuses a fold over a run-time tree. Rule R1 lets only this file
declare a data type, so the data type must be here.

## How to replace the domain

1. Write your data types and definitions in `domain/domain.lang`. Keep
   the names apart from the core names in `docs/host/README.md`.
2. Change the examples and the tests that use the sample names:
   `examples/algebra.lang`, `examples/inventory.lang`,
   `examples/laws.lang`, `test/eval/expect.txt`,
   `test/check/domain-name.lang`, `test/ir/spine.lang` and
   `test/ir/tree.lang`.
3. Keep a data type with two recursive fields for `test/ir/tree.lang`, or
   remove that file and its block in `test/gate.sh`.
4. Run `make check` in the repository root. The build makes
   `build/domain.c` again when `domain/domain.lang` changes.
