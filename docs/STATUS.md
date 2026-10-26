# leaf-lang status

> Write here: one paragraph. Say which milestone the build implements, and
> which milestone it does not complete yet.

## Implemented

> Write here: one item for each feature that works. Name the former (F1 to
> F15), the core type or the core operation.

- <feature>.

## Remaining M0 work

> Write here: one numbered item for each missing feature. Say what each item
> needs first.

1. <work item>.

## Known limits

> Write here: one item for each limit of the current build, with its cause
> (a fuel budget, a host limit, a stack size).

- <limit>.

## Internal boundaries

> Write here: one paragraph for each module group. Say which module holds
> which job, which tests check which tables, and which types a program
> cannot use.

## S1: canonical surface, `langc fmt`, JSON diagnostics

S1 (2026-10-25) gives the Agda-like surface (Q14 (b)) of SPEC.md section
2, the canonical printer `langc fmt` and `langc check --json`
(docs/DIAGNOSTICS.md).

- Lexer: UTF-8 symbols `→`, `λ`, `×`, `ℕ` and their ASCII aliases; columns
  in code points; a comment list for `fmt`.
- Parser: signature and clause, `rec`, `postulate`, `data ... where`, the
  layout rule; new codes `PARSE_CLAUSE_NAME` and `PARSE_CTOR_RESULT`.
- `langc fmt` (src/front/fmt.c) and `W_NONCANON` with a `fix`.
- Kit names: `Flag`, `flagYes`, `flagNo` became `Bool`, `true`, `false` in
  the core. `Prod`, `pair`, `first`, `second`, `List`, `nil`, `cons`,
  `filter` and `unfold` left the builtin table. `NAME_UNKNOWN` names the
  replacement. The reserved names are only `Text`.
- Gate additions (test/gate.sh): canonical and idempotent `fmt` on each
  committed `.lang`, 3 non-canonical pairs in test/fmt/, a parse round trip
  through `build/fronttool dump`, 20 JSON fixtures in test/json/, and the
  code list of docs/DIAGNOSTICS.md against src/. Mutants M1 to M6 are
  killed.

Gate counts (`make check`): examples 10 checked, 0 failed; parse refusals
9; check refusals 18; evals 30; ir 10 lowered; wasm 13 modules valid;
differential 772 cases, 0 mismatches; fmt 50 canonical, 3 non-canonical
pairs; json 20 fixtures; diagnostics 31 codes, 18 with a fixture, 13 with
no fixture; gate 0 failures; review and domain regressions passed.

### Removed cases

Each case below used a kit name that S1 removed. Supported behavior stays
covered: `stackDemand` consumes a trapped recursive rest, and the scalar
inventory entries check all three Item fields and both `isRed` results.
The native C regressions also check JSON encoding of seven malformed
UTF-8 sequences and three valid Unicode scalars.

| File | Case | Reason |
|---|---|---|
| test/check/filter-sum.lang | `TYPE_MISMATCH` from `filter` on `Sum Flag Nat` | `filter` has no replacement. Check refusals 19 to 18. |
| test/eval/expect.txt | `formers xs`, `inventory stockTotal`, `inventory stockRed` | They use `List`. |
| test/eval/expect.txt | `algebra down`, `algebra stop` | They use `List` and `unfold`. Three scalar inventory checks restore projection and enum coverage. Eval lines 32 to 30. |
| test/gate.sh (spine list) | `foldOnes 3:6`, `mapUp 3:18`, `bindUp 3:36`, `filterUp 5:3`, `trapHead 5:1`, `trapSum 5:trap`, `trapSum 2:3`, `filterTrap 2:3`, `filterTrap 5:trap`, `bindTrap 5:1` | They use `List` and `filter`. Two `stackDemand` cases restore demanded-rest trap coverage. Spine cases 13 to 5; differential 813 to 772 cases. |
| test/fronttool.c | the `Prod` eta regression | `Prod` is removed. The Sigma eta regression stays. Regressions 14 to 13. |
| test/fronttool.c | the `first (pair ...)` wrapper of the `Lot` case | `witness (pack ...)` gives `TYPE_INFER`. The case is now `bad = either f f s`, with the same code. |

These definitions left the `.lang` files for the same reason: domain
`total`, `redTotal`; algebra `sumList`, `ones`, `down`, `stop`; formers
`xs`, `empty`; inventory `stock`, `stockTotal`, `stockRed`; laws `xs`,
`noNats`, `mapId`, `foldNil`, `foldSum`; monad `single`, `doubled`,
`small`, `kept`; test/ir/spine.lang `noNats`, `upFrom`, `upTrap`,
`foldOnes`, `mapUp`, `bindUp`, `filterUp`, `trapHead`, `trapSum`,
`filterTrap`, `bindTrap`.

### Port sources read

- agent-lang 06854bf, `src/`: no JSON diagnostic code found. There is no
  `retry`, `"fix"`, `severity` or `--json` diagnostic. `json.c` writes the
  Json target document.
- dil-lang 9e57528, `src/front/check.c`, `parser.c`, `main.c`: no JSON
  diagnostic code found. `--json OUT.json` is a build output path.

Thus the JSON record and `retry` are a leaf-lang definition.

### Open items from S1

- O14 (Claude reading, not ruled): an ASCII alias parses and gives
  `W_NONCANON` with the Unicode form as `fix`; `fmt` prints only the
  Unicode form. Unicode can cost more tokens; the S9 baseline (Q13)
  measures it.
- O15: a `let` form under Q14 (b). Open.
- O16: CLOSED (S2-Q2 (b), S2 U6b2). `Sum`, `inl` and `inr` are a family in
  `domain/domain.lang`; `fold` replaces `either`. `Either` is not admitted
  (PLAN 2.2).
- O17: `flagIf` keeps its kit name (it is not in the S1 drop list). The
  USER rules the name of `if`.
- O18: CLOSED (S2 U2). The Op values and the eval and lowering code for
  `List`, `Prod`, `filter` and `unfold` are removed.
- O19: the value printer and some check messages use the ASCII kit
  spelling (`->`, `fun x =>`, `Sigma (x : A) B`, `Nat`). The parser reads
  it back through the aliases.

## S2: cubical kernel, cell criterion, codata

S2 (2026-10-26) ports the cubical kernel and the cell criterion (PLAN
section 9, S2 row).

- Kernel: `Path`, `PathP`, the interval `I` (`i0`, `i1`, `~`, `∧`, `∨`),
  systems, `transp`, `hcomp`, Pi, Sigma, `Type k`, `Glue`, `glue`,
  `unglue` and `ua` (S2-Q1 (a)). `Eq` is removed. `refl` is the constant
  path. docs/DIAGNOSTICS.md has 41 codes (S1: 31).
- `Sum`, `inl` and `inr` are a CM in domain/domain.lang (S2-Q2 (b)).
  `fold` replaces `either`. The monad rules do not take `Sum` (R1 (b)).
- No `record` form (S2-Q3 (b)). The Sigma `(x : A) × B` is the only form.
- `lt k n = natLe (natAdd k 1) n` and `Fin n = (k : ℕ) × Path Bool (lt k n)
  true` are definitions in domain/domain.lang (PLAN 2.2).
- `REFUSE_CELL`: a `data` with a type parameter must be a cell signature
  (CM). `codata` declares a cocell signature (CC). An admitted codata `D`
  gives `D.pos`, `D`, its destructors, `D.walk` and `D.corec`.
- O18: the kit Ops `pair`, `first`, `second`, `unfold` and the `List`
  paths are removed (S2 U2).

Gate counts (`make check`): examples 10 checked, 0 failed; parse refusals
10; check refusals 32; evals 64; ir 10 lowered; wasm 13 modules valid;
differential 773 cases, 0 mismatches; fmt 78 canonical, 5 non-canonical
pairs; json 28 fixtures; diagnostics 41 codes, 26 with a fixture, 15 with
no fixture; gate 0 failures; review and domain regressions passed.

### Mutants

All are killed. Sites C1 to C3 are in src/front/check.c.

| Mutant | Site | Edit | Killer |
|---|---|---|---|
| C1 | `cell_field` | a type-parameter field beside other fields (the cons shape) is accepted | fronttool rows Seq (`DOMAIN_FAMILY`) and Gp (admitted) |
| C1w | `cell_field` | a field that uses a type parameter in a larger type is accepted | fronttool row Wr (admitted) |
| C2 | `cell_generators` | one generator is refused, so `Sum` is refused | all programs (`REFUSE_CELL` on `Sum`) |
| C3 | `check_cofamily` | `cocell_shape` is not called | fronttool codata rows Z, Two, P, Nt (admitted) |
| U6c2b | `gen_walk` | the walk cofield case drops the step | fronttool row h1 (`TYPE_REFL`) |
| U7-M1 | domain/domain.lang | `lt k n = natLe k n` (off by one) | test/check/fin-bound.lang, eval `fin notBelow` |

### Open items from S2

- `REFUSE_CELL` has no JSON fixture: a program `data` gets `REFUSE_DATA`
  first (PLAN 2.1). The fronttool domain rows test the code.
- The names of the corecursor and the bisimulation (Q-S2-U6c2-1) are not
  ruled. The default is (a): `D.corec` is private.
- The bisimulation is not built. It needs a dependent `elim` over `D.pos`,
  which is a new rule and a name that programs see (R7 (c)).
