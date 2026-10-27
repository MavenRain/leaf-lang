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

## S3: affine layer

S3 (2026-10-26) adds the affine layer (PLAN section 4, section 9 S3 row).

- Multiplicities: each binder has 0, 1 or omega. The surface marks are
  `(@0 x : A) → B` and `(@1 x : A) → B`; a binder with no mark is omega
  (Q-S3-1 (a)). `A ⊸ B` is `(@1 _ : A) → B`, and `-o` is an alias with
  `W_NONCANON` (Q-S3-2 (a)). `⊸` and `→` do not convert, and a closure
  type has no multiplicity bit (Q-S3-3 (a)).
- The check counts the uses of each variable on one evaluation path. The
  eliminators `option`, `flagIf`, `fold`, `witness` and `payload` start
  each arm from the same use set and take the maximum. The split rule
  `unpack p (λ a b → t)` uses `p` one time and binds `a` and `b` at 1
  (Q-S3-6 (a)). A definition whose type ends in `Path`, `PathP` or a
  universe is checked at 0; each other definition is checked at 1
  (Q-S3-5 (a)).
- Resource sorts: `Leaf` (a core base type with no constructor and no
  eliminator) and `Keystore` (Q-S3-7 (a)). A variable of a resource type
  has multiplicity 0 or 1. A JSON record gives `affine` as `"k of m"`
  (Q-S3-9 (a)).
- Five refusal codes (PLAN 4.2), each with a row and a JSON fixture in
  docs/DIAGNOSTICS.md: `REFUSE_AFFINE_REUSE`, `REFUSE_AFFINE_OMEGA`,
  `REFUSE_AFFINE_FIX`, `REFUSE_AFFINE_PATH` and `REFUSE_SEALED`.
  docs/DIAGNOSTICS.md has 46 codes (S2: 41).
- `Durable A` is a CM in domain/domain.lang: `done : (doneOf : A) →
  Durable A` and `commit : (at : ℕ) → (cont : Durable A) → Durable A`
  (Q-S3-8 (a), Q-S3-10 (a)). In a family with type parameters, a
  recursive field at the own parameters is whole, and each constructor
  has at most one (a spine, O-U6b-1 (a)). A `fold` over a `Durable` runs
  in `langc eval` and in Wasm as a run-time walk of the spine.
- `Stream A` is a codata in domain/domain.lang (`head`, `tail`).
  `Keystore = Stream (Option Leaf)` is sealed: a codata line or `D.corec`
  at a type argument with a resource sort in it is `REFUSE_SEALED`, at
  each origin and multiplicity (O-U6-1 (a)).
- A `D.corec` step that uses a 1-variable is `REFUSE_AFFINE_OMEGA`, not
  `REFUSE_AFFINE_FIX`: the generated corecursor binders are omega
  (O-U5-1 (a)).

Gate counts (`make check`): examples 10 checked, 0 failed; parse refusals
12; check refusals 35; evals 70; ir 10 lowered; wasm 13 modules valid;
differential 773 cases, 0 mismatches; fmt 91 canonical, 6 non-canonical
pairs; json 33 fixtures; diagnostics 46 codes, 31 with a fixture, 15 with
no fixture; gate 0 failures; review and domain regressions passed.

### Mutants

All 14 are killed. Each site is in src/front/check.c. M1 to M5c are the
five S3 refusals; M6 and M7 are the two `Durable` branches of the family
check.

| Mutant | Site | Edit | Killer |
|---|---|---|---|
| M1 | `use_local` | a second use of a 1-variable passes | json `REFUSE_AFFINE_REUSE`; fronttool rows `pick`, `swap` |
| M2 | `use_local` | a 1-variable at an omega position is not refused | fronttool rows `idMany`, `apply`, the `Stream'corec` step |
| M2b | `elab_binder` | a Pi binder of a resource type at omega is not refused | json `REFUSE_AFFINE_OMEGA`; fronttool rows `keep`, the `Durable` payload, the `Stream` head |
| M2c | `infer_lam` | an annotated λ of a resource type at omega is not refused | fronttool row `hold` (U7) |
| M3 | `fold_family` | a recursive case that uses a 1-variable passes | fronttool rows `Stack` `size`, `Durable` `spend` |
| M3b | `fold_simple` | a `fold` step over `ℕ` that uses a 1-variable passes | json `REFUSE_AFFINE_FIX`; fronttool row `ℕ` `spend` |
| M4 | `refl_free` | a 1-variable in the end point of `refl` passes | fronttool row `pack refl 0` |
| M4b | `path_rule` | the path rules (`transp`, `hcomp`, `Glue` and the derived rules) do not refuse | fronttool row `move` (`transp`) |
| M4c | `check_plam` | `λ i →` at 1 does not refuse | json `REFUSE_AFFINE_PATH`; fronttool row `pack (λ i → l) 0` |
| M5 | `opens_codata` | no codata line and no `D.corec` opens a codata | json `REFUSE_SEALED`; fronttool rows `peek`, `adv`, `mint` |
| M5b | `pos_family` | a `Stream` of a resource sort is not a resource type | fronttool rows `f : Stream Leaf → ℕ`, `f : Keystore → ℕ`, `peek2` (U8) |
| M5c | `apply_term` | a codata line at a resource type argument passes | json `REFUSE_SEALED`; fronttool rows `peek`, `adv`, `mint` |
| M6 | `check_field` | a recursive field at the own parameters is refused | fronttool row `Durable` (admitted) |
| M7 | `check_ctor` | two recursive fields at the own parameters pass | fronttool S2 row `Fm A` (`DOMAIN_FAMILY`) |

### Staged S3 review fixes (2026-10-26)

- Eliminator payloads and folded results keep affine binders when their
  types contain resources, are polymorphic, or can be closures carrying
  resources. Concrete resource-free payloads keep the existing omega
  handlers. The checks cover `option`, `map`, `bind` and both `fold` forms.
- Resource classification inspects instantiated constructor fields of
  every domain family, including `Sum` and closed resource wrappers.
- Erased laws still check at 0. A separate check at 1 records whether a
  runtime reference is safe, so applying a law at `i0` or `i1` cannot
  recover an erased input. Type arguments and family parameters check
  at 0, including arguments passed to an omega type binder.
- A normalized lambda cannot construct a resource stream, including
  through a polymorphic builder. The codata guard checks every type
  argument, including the private corecursor's seed type.
- Explicit lambda marks must match the expected binder. Family
  parameters, constructor fields and codata observations reject marks
  that their syntax representation cannot retain.

All added regressions in test/fronttool.c pass. The full `make check`
gate passes with the counts above. The original staged gate and JSON
diagnostic checks were retained.

### Open items from S3

- Trees at the own parameters (O-U6b-1 (a)): a family with type
  parameters has at most one recursive field in each constructor. The S2
  row `Fm A` (two recursive fields) stays `DOMAIN_FAMILY`, and lower.c
  lowers only a spine (`REFUSE_RUNTIME_TREE`). Trees are a later slice
  item.
- `opens_codata` matches by name (a codata line, or `D.corec`), and
  `pos_family` matches by the `.pos` suffix. Both are generic over each
  codata (O-U6-3).
- The generated codata destructors bind the stream at omega, so a
  1-stream cannot be observed. No S3 rule needs it: the core opens a
  `Keystore` in S5 (O-U6-4).
- `core_free` shifts the index only under `CORE_LAM`, `CORE_PI` and
  `CORE_SIGMA`. It assumes that no Op binds a variable itself. This is
  not checked against each Op (O-U5-2).
- `transp` over a constant line refuses one use of a 1-variable, as the
  PLAN 4.2 rule is written. Over a constant line `transp` gives the
  value, one use, so the rule is too strict there (O-U5-3).
- `path_rule` covers `symm`, `trans`, `cong` and `transport` (each one
  elaborates to `λ i →` or to `transp`, PLAN 3). Only `transp` has a test
  (O-U5-4).

## S4: hash layer

S4 (2026-10-27) adds the hash layer and the XMSS algorithms (PLAN
section 3, section 9 S4 row).

- `Digest` is a core base type: 32 bytes (v1 n = 32, Q-S4-8 (a)). A
  value holds four 64-bit big-endian words in its arguments, and the
  printer prints it as `digest w0 w1 w2 w3` (Q-S4-2 (a)). A `Digest` is
  copyable (resource class NONE). `digestEq : Digest → Digest → Bool`
  compares the four words.
- `digit : LogW → Digest → ℕ → ℕ` gives the RFC 8391 base_w digit, from
  byte 0 and the most significant bits first. A position past the digest
  traps (Q-S4-U2-a). `LogW` has the values `logW1`, `logW2`, `logW4` and
  `logW8`, and `logWBits` gives 1, 2, 4 or 8 (Q-S4-U2-d).
- `natDiv` and `natMod` are base ops on the machine word (Q-S4-5 (a)). A
  division by 0 traps.
- `boolElim` is the dependent `Bool` eliminator. It computes on `true`
  and `false`, and a neutral `b` stays stuck (Q-S4-U4-a (a)). The domain
  `finAt` uses it to read a `Fin n` field at a fold counter.
- Hash ops: `thChain`, `thNode`, `thMsg` and `prf` are builtin rows with
  their own ops. Each op packs its `Tweak` as the 32-byte RFC 8391 ADRS
  (Q-S4-4 (a)) and calls the reference Keccak-256 in src/front/keccak.c
  (the kit Keccak, pad 0x01). A tweak field above 32 bits traps
  (Q-S4-U3-a).
- The hash ops compute only in `langc eval` (Q-S4-3 (a)). In `langc
  check` they stay stuck, so the kernel never unfolds a hash op (L6).
- The S4 traps use `EVAL_OVERFLOW`. Its row in docs/DIAGNOSTICS.md gives
  the four causes. S4 adds no code: docs/DIAGNOSTICS.md has 46 codes
  (S3: 46).
- domain/domain.lang adds `Tweak`, `Params`, `PubKey`, `Sig` and the RFC
  8391 algorithms over the hash ops: `chain`, `wotsPk`, `wotsSign` and
  `wotsPkFromSig` (WOTS+, w = 16, len = 67), `ltree`, `treeHash`,
  `authPath`, `rootFromPath`, `pkFromSeeds`, `signAt` and `verify`.
  `ltree` and `treeNode` use fixed nesting (`lTreeUp`, `up`, `lv0` to
  `lv20`), HMAX = 20 (Q-S4-U5-a (a)). `verify` gives false when `n` or
  `logW` is not the v1 value (Q-S4-8 (a)), or when `h` is above HMAX.
- Vectors (Q-S4-1 (c)): each hash op has one vector from an independent
  implementation, Foundry `cast keccak`, and the empty input has one.
  WOTS+, the L-tree and the Merkle tree use own vectors. The rows are in
  test/eval: digest, nat-div, hash, bool-elim, wots and xmss.
- L1 at h = 4: test/eval/laws.lang checks that `verify pk4 m (signAt sk
  skPrf pk4 i m)` is `true` for i = 0 to 15. S5 checks the keystore
  form.
- L6: test/check/hash-node.lang, hash-root.lang and hash-verify.lang
  state equations that are true in `langc eval`. `langc check` refuses
  each one with `TYPE_REFL`, because the hash ops stay stuck.
- No example entry uses a `Digest` op, so the lowering and the
  differential do not change.

Gate counts (`make check`): examples 10 checked, 0 failed; parse refusals
12; check refusals 38; evals 142; ir 10 lowered; wasm 13 modules valid;
differential 773 cases, 0 mismatches; fmt 101 canonical, 6 non-canonical
pairs; json 33 fixtures; diagnostics 46 codes, 31 with a fixture, 15 with
no fixture; gate 0 failures; review and domain regressions passed.

### Mutants

All 10 are killed. M1 to M6 are in src/front/eval.c, and M7a to M9 are
in domain/domain.lang. The eval rows kill 9 (all but M5). The L6 check
rows kill M5 and M7b.

| Mutant | Site | Edit | Killer |
|---|---|---|---|
| M1 | `reduce_digest_eq` | `digestEq` compares the words 0 to 2 only | eval row digest `other` |
| M2 | `reduce_digit` | `digit` does not mask the digit | eval rows digest `at1`, wots `csumM`, `cDigit0` |
| M3 | `reduce_digit` | a position past the digest does not trap | eval row digest `past` |
| M4 | `nat_div` | `natDiv` and `natMod` by 0 do not trap | eval rows nat-div `divZero`, `modZero` |
| M5 | the hash op reducer | `langc check` computes the hash ops | check rows hash-node, hash-root, hash-verify |
| M6 | `adrs_bytes` | a tweak field above 32 bits does not trap | eval row hash `wide` |
| M7a | `verify` | the root compare uses the `pubSeed` | eval rows xmss `good`, laws `l1Msg`, `l1Zero` |
| M7b | `verify` | the v1 arm gives `true` | check row hash-verify; eval rows xmss `badMsg`, `badIndex` |
| M8 | `wotsCsum` | the checksum uses `natSub 14`, not `natSub 15` | eval rows wots `csumM`, `cDigit0`, `cDigit1` |
| M9 | `pathNode` | the two arms of each `boolElim` change places | eval rows xmss `path0`, `path9`, `good` |

### Open items from S4

- `verify` has no explicit index bound (O-U6-1, kept). RFC 8391
  Algorithm 14 (XMSS_verify) has no idx_sig bound. The root compare
  refuses an index past 2^h - 1 (eval row xmss `badLeaf`).
- L1 is checked at h = 4 only. Key generation at h = 4 is about 16,080
  `thChain` calls in `langc eval`; at h = 10 it is about 64 times more.
- The definitions fix n = 32 and logW = 4. `Params` keeps `n` and
  `logW` for the EIP-8310 JSON (Q-S4-8 (a)).
- The hash ops do not lower. S5 puts Keccak in the Wasm module.
- `check --json` gives "laws" as "0 of 0" (main.c). L1 and L6 do not
  change it.
