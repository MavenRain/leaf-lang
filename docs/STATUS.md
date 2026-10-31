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

## S5: keystore and Wasm signer

S5 (2026-10-27 to 2026-10-28) adds the keystore operations and the Wasm
signer (PLAN section 5.2, section 9 S5 row).

- The keystore and leaf values are two internal core value ops with no
  source name. `langc eval` prints them as `keystore h i` and `leaf h i`,
  with no seed and no public key (Q-S5-13 to Q-S5-15).
- The checker gives the type `Option (Leaf × Keystore)` to the `next`
  core node (Q-S5-16, Q-S5-17). `verifyAt` takes its `Sig` at omega
  (Q-S5-18).
- L1 (the keystore form), L3, L4 and L5 are eval rows at h = 4 (test/eval
  durable and keystore) and refusal fixtures (test/check andthen-tree,
  commit-sealed, keystore-position, next-twice and sign-twice) (Q-S5-10).
- Lowering: a keystore op with closed arguments lowers by eval. Each hash
  op stays stuck and lowers as a hash node. The IR adds the hash node and
  a digit op (Q-S5-21 (b), Q-S5-22 (a)). A stuck keystore op and a
  keystore or leaf value give `REFUSE_RUNTIME_KEYSTORE` (Q-S5-23 to
  Q-S5-25). The row `ir_digest NAME` compares the sha-256 of `langc ir`
  with test/ir/NAME.sha256 (Q-S5-24 (a)).
- Wasm Keccak: one internal function `keccak_f` with 24 unrolled rounds
  over i64 locals. `keccak256` swaps the bytes of the big-endian IR words
  (Q-S5-26 (a), Q-S5-27 (a)). test/ir/hashkat.lang has known-answer rows
  for the module Keccak (Q-S5-30 (b)).
- `langc signer --height H -o FILE` writes the signer module for one
  height H, 0 to 20 (Q-S5-32 (a), Q-S5-33 (b)). The module has the import
  `env.commit` and the exports `keygen`, `publicKey`, `next`, `sign` and
  `verify`. src/wasm.c writes WOTS+, the L-tree and treeHash as hand Wasm
  loops over the internal `keccak256` (Q-S5-31 (a)). This is a hand port
  of the S4 domain algorithms (trusted base). Only `next` calls
  `env.commit`. A second `sign` of one leaf traps (L5 at run time).
- `keygen (id, start, skSeed, skPrf, pubSeed)` starts at the position
  from the state file (Q-S5-33 (b)). `sign (ks, leaf, m)` traps unless
  the leaf is after the last signed leaf and not after the next position
  (Q-S5-38 (a)). `verify (pk, m, sig)` gives 1 or 0, and 0 when the key
  height is not H (Q-S5-39 (a)).
- Harness: test/run-wasm.mjs `FILE --key DIR` reads the stdin lines
  `keygen ID SKSEED SKPRF PUBSEED [START]`, `next`, `rootSum`, `sign LEAF
  M` and `verify M` (Q-S5-35 (a), Q-S5-37 (a)). The `commit` import writes
  the position through a temporary file, fsync, atomic rename and
  directory fsync. An exclusive DIR/.signer.lock serializes writers;
  after a crash it requires explicit recovery once the writer has stopped
  and the state has been checked. Empty or malformed state is refused.
  Wasm key IDs are normalized to u64, and input words must fit u64.
  Then it writes the
  EIP-8310 keystore JSON DIR/ID.json (Q-S5-3 (a), `uuid` Q-S5-36 (a)). A
  position that is not larger than the stored position gives
  `REFUSE_HIGH_WATER`. There is no silent reset.
- Differential at h = 4: test/wasm/signer4.lang and test/wasm/sign4.lang
  give `rootSum`, `sigSum` and the `verify` results in `langc eval`
  (Q-S5-34 (a), D37). test/gate.sh compares them with the module (D38).
  At h = 10 the gate compares only the last line, `verify true` (D39,
  Q-S5-11 (a)).
- IR rows: test/ir digit, hash, hashkat, keystore (`ir_digest`) and
  tweakflag (D40). tweakflag isolates the trap flag of a Tweak field:
  the kind field wraps to 0 under the width check, and the tree field has
  no width check.
- New codes: `REFUSE_RUNTIME_KEYSTORE`, `REFUSE_TARGET_NODE` (no IR node
  gives it now, no fixture, Q-S5-29 (a)) and `REFUSE_HIGH_WATER`.
  docs/DIAGNOSTICS.md has 49 codes (S4: 46).

Gate counts (`make check`): examples 10 checked, 0 failed; parse refusals
12; check refusals 43; evals 162; ir 14 lowered; wasm 19 modules valid;
differential 951 cases, 0 mismatches; signer 2 modules valid, 28 rows;
fmt 115 canonical, 6 non-canonical pairs; json 33 fixtures; diagnostics
49 codes, 31 with a fixture, 18 with no fixture; gate 0 failures; review
and domain regressions passed.

Review regressions: hash nodes return a validation flag with their digest
address, so unused invalid digests in fold state do not trap. The
hash-lazy differential fixture covers width errors and overflow flags.
signer-review.mjs injects a crash during state replacement, checks stale
lock and empty-state refusal, and exercises key IDs at the i64 boundary.
The C domain regressions also check explicit affine andThen continuations
over a copyable argument, with and without a result hint.

### Mutants

There are 11 mutants on src/wasm.c. 10 are killed. M6 is a known
survivor (Q-S5-40 (a)). Each mutant is an exact-match edit, then `make
-B`, then the rows. U5 mutated the Keccak (the U5 flag mutant survived
and is M1 of U7). U7 mutated the signer.

| Mutant | Site | Edit | Killer |
|---|---|---|---|
| U5 rc | `keccak_f` | round constant 1 is 0x8083, not 0x8082 | ir row hashkat |
| U5 bswap | `keccak256` | the squeeze stores a lane with no byte swap | ir row hashkat |
| U5 pad06 | `keccak256` | the pad byte is 0x06, not 0x01 | ir row hashkat |
| U5 width | `emit_adrs` | a field above 32 bits does not trap | ir rows hash, hashkat |
| M1 flag | `emit_adrs` | the trap flag of a Tweak field is dropped | ir row tweakflag `kindFlag` |
| M2 bound | `sign` | no check of the signed bound | signer row: a second `sign` of one leaf gives no trap |
| M3 sibling | auth path | node k at i >> k, not (i >> k) xor 1 | signer rows `sigSum`, `verify` |
| M4 csum | WOTS+ checksum | digit 65 is csum & 15, not (csum >> 4) & 15 | signer rows `sigSum` |
| M5 steps | `wots_pk_from_sig` | a chain runs 16 - digit steps, not 15 - digit | signer rows `verify` |
| M6 height | `verify` | no check of the key height H | none: known survivor (Q-S5-40 (a)) |
| M7 side | `root_from_path` | an odd node keeps the even order | signer row `verify` at position 7 |

### Open items from S5

- M6 survives: no gate row gives `verify` a public key of another height
  (Q-S5-40 (a), no new harness line).
- The signer loops are a hand port of the S4 domain algorithms (trusted
  base). The h = 4 differential and the mutants check the port. At h =
  10 only the module runs, with no eval compare (Q-S5-11 (a)).
- The module does not zeroize the secrets of a dropped leaf (O6).
- The keystore JSON has the test seeds in clear and no `crypto` block
  (O13).
- A restored backup is outside the checker (O12). The harness refuses
  only a position that is not larger than DIR/ID.state.
- S6 adds the EVM verifier (PLAN section 5.1).

## S6: EVM account verifier

S6 adds the EVM account verifier of PLAN section 5.1. The verifier is
fixed EVM code that C writes, with no IR (Q-S6-1 (a)). The public key
(the root, the public seed and H) is in the code as constants.

- `langc verifier --height H --pk FILE [--entry 8141|4337] [--shim]
  -o OUT` writes the verifier code for one public key. A bad argument
  is a `USAGE` refusal. S6 adds no diagnostic code.
- src/evm.c has the core and the three builds. The core checks one
  signature by PLAN 5.4 (thMsg, the 64 + 3 WOTS+ digits, the 67
  chains, the L-tree, the auth path) against the key constants. Each
  fault goes to one shared block.
- The three builds differ only in the 6 parts of one table and the
  entry 2 head:
  - Entry 1 (EIP-8141, `--entry 8141`): the signature comes from
    entry 0 of the transaction signatures, the nonce and sigHash come
    from TXPARAM. It accepts by APPROVE. The opcodes are a draft (O1).
  - The shim (`--entry 8141 --shim`, O2): the entry 1 core. It reads
    the nonce and sigHash from a 64-byte calldata prefix, then the
    signature. It returns the word 1. A fault reverts.
  - Entry 2 (ERC-4337 v0.7 `validateUserOp`, `--entry 4337`): it
    reads the signature and the nonce from the calldata after its
    head. userOpHash is sigHash. It pays missingAccountFunds and
    returns the word 0. A fault gives the word 1 (Q-S6-3 (a)).
- src/asm.c and src/asm.h: the EVM assembler that src/evm.c uses.
- L2 in the guest: the domain def `accept pk k m s` and its eval rows
  (test/eval/laws.lang, test/eval/expect.txt).
- test/gas.sh and `make gas`: the gas rows (below).

### Gate rows (`make check`)

test/gate.sh has the S6 rows. Each row runs `evm run` on the code of
`langc verifier` at h = 4, with signatures from the Wasm signer
(test/run-wasm.mjs). The full list is in test/gate.sh.

- The shim and entry 2 accept the signatures of the 16 positions (32
  rows). call-ok, call-u-ok and call-u-funds accept.
- A wrong nonce (call-nonce, call-u-nonce, call-u-key), a wrong
  message (call-m, call-u-m), a wrong root (2 rows) and a changed
  byte (6 rows) are refused.
- The calldata length and offset rows: call-f-len, call-g-len,
  call-g-off.
- The staged review adds test/verifier-review.c: nine C integration
  checks with a real signature whose last byte is zero. They cover a
  complete signature, omitted ABI padding, a missing signature byte,
  a short tuple, overflowing and out-of-range offsets, and valid
  relocated signature and tuple tails. The truncated signature used to
  return 0 because CALLDATACOPY supplied the missing zero. Entry 2 now
  checks tuple and signature spans without arithmetic overflow and
  returns 1 for that input. The extra signer fixture makes 95 signer
  rows; the nine ABI checks report separately.
- The shim build is the real entry 1 build with the 5 table parts in
  the shim form (a byte compare). evm 1.14.12 cannot run the real
  build (O2).
- `--entry 4337 --shim` is refused.

### Mutants

There are 8 mutants on src/evm.c. 8 are killed. Each mutant is an
exact-match edit, then `make -B`, then `make check`. Each mutant
changes the bytes of the h = 4 shim, real entry 1 and entry 2 builds
(key: entry 2 only).

| Mutant | Edit | Killer (FAIL rows) |
|---|---|---|
| nonce | the nonce EQ is POP POP PUSH 1 | 3: call-nonce, call-u-nonce, call-u-key |
| csum | the checksum constant 960 is 945 | 35: the 32 position rows, call-ok, call-u-ok, call-u-funds |
| chainend | the chain end 15 GT is 14 GT | 35: the same rows |
| oddnode | the odd-node JUMPI is POP, JUMP (the even order) | 35: the same rows |
| authbit | the auth bit o is 0 | 30: positions 1 to 15 on both builds |
| root | the root EQ ISZERO is POP POP PUSH 0 (the JUMPI stays) | 10: call-m, call-u-m, the 2 wrong-root rows, the 6 changed-byte rows |
| len | the length compare is POP | 1: call-g-len |
| key | the entry 2 nonce word is SHL 192 SHR 192 | 1: call-u-key |

Position 0 has an even q at each level, so the authbit mutant accepts
it. With no length check, call-f-len and call-g-off also fail: the
zeros of CALLDATACOPY and CALLDATALOAD make the root compare fail.

### Open items from S6

- The real entry 1 has no `evm run` row and no gas row: evm 1.14.12
  has no EIP-8141 opcodes (O2, O21). The shim and the byte compare
  check the entry 1 core.
- The EIP-8141 opcodes and the signature hash are a draft (O1).
- The execution gas is approximately 1.7 times the PLAN 5.1 estimate
  (O22).
- At each height, the EIP-7623 floor is less than standard +
  execution (O10).
- `make check` does not run `make gas` (Q-S6-8 (b)). At H = 16 the
  gas run takes about 17 minutes and 62 MB.
- Out of S6: an `execute` entry for the ERC-4337 account (O23), the
  EIP-8141 SENDER frame (O24), deployment (O25).
- S7 adds the Poseidon2 binding and the zkml writer (PLAN section 9).

### Gas rows (`make gas`)

`make gas` runs test/gas.sh. GAS_H sets the heights (default 4 10 16).
`make check` does not run it (Q-S6-8 (b)). For each height H, the
script makes one key and runs the Wasm signer to 16 positions. At H =
4, these are all 16 positions. At a larger H, position i is
i(2^H - 1)/15, for i = 0 to 15 (D19.2). At each position, `langc
verifier` writes the shim and entry 2, and `evm run --statdump` runs
them on the signature. Each signature must accept. If not, the script
writes a FAIL row and exits 1.

The calldata gas comes from the input bytes (D19.3, EIP-7623), with z
zero bytes and nz other bytes: standard is 4 z + 16 nz, floor is
10 (z + 4 nz), and tx is 21000 + max(standard + execution, floor).

The real entry 1 build has no row (D19.4). evm 1.14.12 has no TXPARAM,
SIGPARAM, SIGDATACOPY or APPROVE (O2). The shim is the entry 1 core,
with the 5 table parts in the shim form. In the real entry 1, the
signature is in the transaction signatures list, not in calldata. Its
calldata gas is a draft only (O1).

Each cell is min/mean/max over the 16 positions. This table records the
original S6 measurements before the staged review added calldata bounds
checks. Review reruns at H = 4 and H = 10 add 160 gas to entry 2 execution
and tx at every position, so entry 2 is now the shim + 288 gas at those
heights. Bytes, calldata gas and shim results are unchanged. The H = 16
keygen has not been rerun after the bounds fix.

| H | Build | Bytes | Execution | Standard | Floor | Tx |
|---|---|---|---|---|---|---|
| 4 | shim | 2376 | 80823/90970.5/101118 | 37368/37440/37500 | 93420/93600/93750 | 139263/149410.5/159582 |
| 4 | entry 2 | 2852 | 80951/91098.5/101246 | 39452/39524/39584 | 98630/98810/98960 | 141475/151622.5/161794 |
| 10 | shim | 2568 | 85897/95237.3/106192 | 40452/40533.8/40596 | 101130/101334.4/101490 | 147373/156771.1/167704 |
| 10 | entry 2 | 3044 | 86025/95365.3/106320 | 42536/42617.8/42680 | 106340/106544.4/106700 | 149585/158983.1/169916 |
| 16 | shim | 2760 | 87281/96044.8/111266 | 43512/43600.5/43656 | 108780/109001.2/109140 | 151889/160645.2/175778 |
| 16 | entry 2 | 3236 | 87409/96172.8/111394 | 45596/45684.5/45740 | 113990/114211.2/114350 | 154101/162857.2/177990 |

In these original rows, entry 2 is the shim + 128 gas (min, mean and max) and 476
bytes more. In each row, standard + execution is more than the floor.
Because of this, tx = 21000 + standard + execution. The execution mean
is 90970.5 at H = 4, 95237.3 at H = 10 and 96044.8 at H = 16. Each H has
its own 16 positions and signed digests. The WOTS+ chain steps change with
the digest, so the mean is not linear in H.

The PLAN 5.1 plan is for h = 16. The measured column is the H = 16 shim
mean:

| Part | Plan | Measured |
|---|---|---|
| execution (WOTS+ chains, L-tree, auth path, H_msg) | about 55k | 96044.8 |
| calldata, 2760 bytes | about 43k | standard 43600.5, floor 109001.2 |
| total past the base cost | about 100k | standard + execution 139645.3 |

The floor (109001.2) is less than standard + execution (139645.3), so
tx does not use the floor (O10). The measured execution gas is
approximately 1.7 times the plan estimate (about 55k).

## S7: Poseidon2 binding and zkml writer

S7 adds the Poseidon2 binding of PLAN section 5.4 and the zkml writer
of PLAN section 5.3. The binding uses Poseidon2 over BabyBear at width
16, the Plonky3 instance (Q10). The writer gives a zkml-lang program
for `aggregate`, not a `Circuit` term (Q-S7-1 (a), O26).

- `langc eval [--hash keccak|poseidon2] PROG NAME [ARGS...]`: the
  eval uses the hash binding of `--hash` (Q-S7-7 (a)). A different hash
  name is a `USAGE` refusal.
- `langc zkml --count N PROG NAME [ARGS...] -o OUT` writes the
  zkml-lang program for the def NAME at N entries, N from 1 to 64
  (D9). Each ARG is a Nat (5.3). A bad argument is a `USAGE` refusal.
- src/front/poseidon2.c and src/front/poseidon2.h: the permutation
  and the sponge on the limbs of the input (5.4).
- src/zkml.c and src/zkml.h: the writer. The output has the Poseidon2
  constants, zkml/xmss.lang and the data defs of the entries.
- zkml/xmss.lang: the verify in zkml-lang: the permutation, the
  sponge, the WOTS+ digits, the chains, the root from the auth path
  and `aggregate`.
- domain/domain.lang: `Signer`, `signerOk` and `aggregate` (PLAN 3.2,
  Q-S7-8 (a)).
- L7 in the guest: the `l7` defs and their eval rows at h = 4
  (test/eval/laws.lang, test/eval/expect.txt, D6).

### Gate rows (`make check`)

test/gate.sh and test/fronttool.c have the S7 rows. The full list is
in test/gate.sh.

- poseidon2 evals, 7 rows: `langc eval --hash poseidon2` gives the L1
  rows at h = 4 (l1Msg, l1Zero, l1Last, l1Mid) and L7 at N = 1 (the
  valid entry, and the bad signature at position 0). `--hash sha3`
  gives exit 2 and `USAGE`.
- zkml, 1 row: `langc zkml --count 1` writes the L7 entry, and the
  zkml-lang check of the output prints ok (Q-S7-13 (a)).
- zkml known answers, 6 evals (Q-S7-14 (a)): the head of the output
  (the constants and zkml/xmss.lang) and one tail of test/zkml make
  one file. The zkml-lang eval of the tail def must give the known
  answer: kaPermZ (the permutation), kaChain2Z (2 chain steps),
  kaRoot2Z (a root of h = 2), kaDigDiff and kaDigSDiff (the WOTS+
  digits) give 0, the difference to the known words. kaAggNil
  (`aggregate` on no entry) gives 1.
- The zkml rows run only when zkml-lang is built next to this repo
  (../zkml-lang/build/langc). If not, the gate prints `zkml:
  skipped`.
- fronttool: the Poseidon2 known answers of Plonky3 rev 5fcf8d7c (the
  (ii) instance at seed 1, the (i) instance on 0..15 and on the (ii)
  input, Q-S7-6 (a)), and the 5.4 sponge digests on 96, 128 and 160
  bytes.
- The S7 eval rows make 205 evals. test/check/aggregate-arg.lang
  (TYPE_MISMATCH) makes 44 check refusals.
- The S7 gate: `make check` EXIT 0, gate 0 failures, poseidon2 evals
  7, zkml 1 checked, zkml known answers 6 evals.

### Mutants

There are 12 mutants. 12 are killed. Each mutant is an exact-match
edit, then `make -B`, then the gate part with its check (the zkml
block, the poseidon2 eval rows or fronttool), then a restore (a copy
and a compare).

| Mutant | File | Edit | Killer (FAIL rows) |
|---|---|---|---|
| M1 | src/zkml.c | the n and logw data rows are swapped | the data word check (below) |
| M2 | zkml/xmss.lang | the sponge limb count word is 1, not 0 | 2: kaChain2Z, kaRoot2Z |
| M3 | zkml/xmss.lang | the permutation has no initial linear layer | 2: kaPermZ, kaChain2Z |
| M4 | zkml/xmss.lang | the chain tweak step drops start | 1: kaChain2Z |
| M5 | zkml/xmss.lang | the 2 inputs of the path node are swapped | 1: kaRoot2Z |
| M6 | zkml/xmss.lang | the digit bound is off by one | 2: kaDigDiff, kaDigSDiff |
| M7 | zkml/xmss.lang | the `aggregate` fold starts at 0, not 1 | 1: kaAggNil |
| M8 | src/zkml.c | logw 4 is written as 2 | the data word check (below) |
| M9 | src/zkml.c | the initial round constants are the terminal ones | 2: kaPermZ, kaChain2Z |
| M10 | src/zkml.c | an extra `)` after nil | 1: zkml (the zkml-lang check gives PARSE_EXPECT) |
| M11 | src/front/poseidon2.c | each internal round uses the round 0 constant | 6: the fronttool Poseidon2 rows |
| M12 | src/main.c | each hash name after keccak in byte order is taken | 1: the `--hash sha3` row |

M1 and M8 change data words that no gate row reads: the zkml check
does not evaluate the data defs, and the known-answer evals use only
the head of the output (O28). A check of the data words of the
output, out of `make check`, kills them (Q-S7-15 (a)).

### Open items from S7

- The zkml output is a zkml-lang program, not a `Circuit` term, and it
  has no prover export (O26). A proof of `aggregate` needs a hash op
  in the zkml-lang `Circuit` IR or a prover for the program.
- The Wasm signer has no Poseidon2 (O27). Only `langc eval` makes a
  signature under the Poseidon2 binding.
- The zkml-lang arena holds about 5 to 9 Poseidon2 hashes (O28). The
  zkml output has a zkml-lang check only, and no eval of the L7
  entry. The 6 known-answer evals use 569 to 871 MiB max RSS.
- S7 does not change the open items of S6.

## S8: boundary exclusion: service mode, manifests, rebuild

S8 adds the boundary exclusion of the S8 row of the plan (Q-S8-1 to
Q-S8-7). The signer service signs only a request that a checked
program gives. Each writer output gets a manifest, and `langc
rebuild` runs the writer again and compares the bytes.

- Manifests (Q-S8-2 (a)): each writer (`build`, `signer`, `verifier`,
  `zkml`) writes OUT and a sidecar file `OUT.manifest.json` with a
  fixed key order and fixed bytes. The manifest holds the command and
  its flags, the source path and its keccak256 (the embedded domain
  text when there is no PROG), the langc commit and the dirty flag,
  the law counts of `check --json`, and the output size and keccak256
  (O31). Each writer writes into memory first. After a writer failure,
  langc writes neither file.
- Version (Q-S8-3 (a)): the Makefile writes build/version.c from `git
  rev-parse HEAD` and a dirty flag (`git status --porcelain` is not
  empty). A manifest from a langc of a tree with a change records
  dirty true (O30).
- `langc rebuild MANIFEST` (Q-S8-4 (a)): it checks the langc commit,
  the dirty flags and the source hash. Then it runs the writer again
  in memory and compares the new output with the `output` entry of the
  manifest and with OUT, byte for byte. A difference gives exit 1 and
  a `REFUSE_REBUILD_*` code. A match gives exit 0 and one line
  `rebuild: OUT matches (SIZE bytes, KECCAK)`.
- `REFUSE_REBUILD_DIRTY` (Q-S8-5 (a), S8 D6): `langc rebuild` refuses
  a manifest with dirty true, with the message "is from a langc of a
  tree with a change". It also refuses a manifest when this langc is
  from a tree with a change, with the message "this langc is from a
  tree with a change".
- src/manifest.c and src/manifest.h: the manifest writer, the output
  buffer of the writers and the one `laws` format of `check`.
- src/main.c (`langc rebuild`): only source 0 of a `signer` or
  `verifier` manifest is the embedded domain text `<domain>`. Another
  source with that name is a user file, and rebuild reads that file.
- test/rebuild-review.c (build/rebuild-review; `make check` runs it):
  the clean-tree rows of `langc rebuild`, with a clean version fixture
  (S8 D7, Q-S8-7).
- Service mode (Q-S8-1 (a), Q-S8-6 (a), PLAN section 5.2):
  test/run-wasm.mjs `FILE --service DIR`. A raw `sign` line is refused
  with `REFUSE_RAW_SIGN`, with no module call and no state change. The
  line `request PROG ENTRY` runs `langc check PROG`, then `langc eval
  PROG ENTRY`, with no shell. ENTRY must give a value of the domain
  type `Request`, `request KEY LEAF (digest W0 W1 W2 W3)`, else
  `REFUSE_SIGN_REQUEST`. A LEAF below the next position of the key is
  refused with `REFUSE_HIGH_WATER`, and a LEAF above it with
  `REFUSE_SIGN_REQUEST`. The service uses the S5 state file, lock and
  `commit` import (O29, O32).
- domain/domain.lang: `data Request` with the constructor `request`
  (Q-S8-6 (a)). Only the domain can declare `request` (R1). The
  manifest source hashes of `signer` and `verifier` change with the
  domain text (S8 D4).
- test/wasm/request.lang and test/wasm/request-shape.lang: the request
  fixtures (req, wrongKey, skipLeaf, pastHeight, fnReq and shape).
- docs/DIAGNOSTICS.md: the six `REFUSE_REBUILD_*` rows and the
  `REFUSE_RAW_SIGN` and `REFUSE_SIGN_REQUEST` rows, each with a "no
  fixture:" reason.

### Gate rows (`make check`)

test/gate.sh has the S8 rows. The full list is in test/gate.sh.

- manifests, 23 rows: `manifest_ok` checks the manifest of each `langc
  build` row, of the signer at h = 4 and h = 10, of the real verifier
  (sources `<domain>` and pk4.bin) and of the zkml row. One row writes
  a 0x01 byte in OUT: exit 2 and no file (S8 D2i). The rows need cast
  on the PATH for keccak256. With no cast, the gate gives a FAIL row.
- rebuilds, 9 rows (Q-S8-5 (a), S8 D7): the `langc rebuild` rows of
  the gate. The rows are keyed on the dirty flag of the manifest of
  this langc, so they hold on all trees. DIRTY and the manifest
  message (S8 D6) use a copy of the manifest of a `build` of the first
  example with `"dirty":true`. A copy with `"dirty":false` gets the
  message "this langc is from a tree with a change" when this langc is
  from a tree with a change, and exit 0 (the same bytes) when it is
  from a clean tree. COMMIT (40 zeros), MANIFEST (no end newline;
  `check` as the first word), IO (a missing MANIFEST, exit 2) and
  USAGE (no MANIFEST; two MANIFEST words).
- rebuild regressions, 8 cases (S8 D7): test/rebuild-review.c links
  src/main.c with a clean version fixture (commit 40 zeros, dirty 0),
  so the clean-tree rows run on all trees. The cases: exit 0 for
  `build`, `verifier`, `signer` and `zkml`; SOURCE; WRITER (the zkml
  manifest with the entry word changed); OUTPUT (the zkml manifest
  with the output hash changed); OUTPUT (the zkml output file with one
  more byte).
- service, 10 rows (S8 D4, D5): the good request, the same request
  again (`REFUSE_HIGH_WATER`), a raw `sign` line (`REFUSE_RAW_SIGN`),
  a program with an affine reuse (`REFUSE_AFFINE_REUSE`), an ENTRY of
  type ℕ (shape), a request for another key (wrongKey), a LEAF above
  the next position (skipLeaf), LEAF 2^4 at h = 4 (pastHeight), a PROG
  that langc cannot read (exit 2) and an ENTRY of a function type to
  `Request` (fnReq). `langc eval` prints `fun n => ` before the
  request text of fnReq, so the service refuses it.
- The S8 gate: `make check` EXIT 0, gate 0 failures, service 10
  checked, manifests 23 checked, rebuilds 9 checked, verifier ABI
  regressions 9 passed, zkml writer regressions 7 passed, rebuild
  regressions 8 passed. The other
  counts are as in S7 (evals 205, check refusals 44, poseidon2 evals
  7, zkml 1 checked).

### Mutants

There are 19 mutants. 16 are killed and 3 are EQUIVALENT. The names
S1 to S10 (the service), B1 to B5 (rebuild) and C1 to C3 (the
manifest) are mutant names, not stage names. Each mutant is an
exact-match edit, then `make -B`, then the gate part with its check
(the service rows, the rebuild rows, build/rebuild-review or the full
gate), then a restore (a copy and a compare).

| Mutant | File | Edit | Killer |
|---|---|---|---|
| S1 | test/run-wasm.mjs | `&& service` is `&& false` (the raw sign check) | the raw `sign` row (exit 0, `4096 trap`) |
| S2 | test/run-wasm.mjs | no key check | the wrongKey row (it signs) |
| S3 | test/run-wasm.mjs | no height check | the pastHeight row (exit 0, `4096 trap`) |
| S4 | test/run-wasm.mjs | `leaf > stateWord(1)` is `false` | the skipLeaf row (exit 0, `4096 trap`) |
| S5 | test/run-wasm.mjs | `leaf < stateWord(1)` is `false` | the `REFUSE_HIGH_WATER` row (exit 0, `4096 trap`) |
| S6 | test/run-wasm.mjs | no `next(ks);` | the good request row (exit 2, `4096 trap`) |
| S7 | test/run-wasm.mjs | the request regex has no `^` | the fnReq row (exit 0, it signs) |
| S8 | test/run-wasm.mjs | the request regex has no `\n$` | EQUIVALENT (below) |
| S9 | test/run-wasm.mjs | `run.status === 2` is `false` | the nosuch row (exit 1) |
| S10 | test/run-wasm.mjs | no `langcRun(["check", prog]);` | EQUIVALENT (below) |
| ws | test/run-wasm.mjs | no `ws.some((w) => w >= 2n ** 64n)` test | EQUIVALENT (below) |
| B1 | src/main.c | no commit compare | the COMMIT row (it gets `REFUSE_REBUILD_DIRTY`) |
| B2 | src/main.c | `r->dirty` is 0 | the D6 manifest message row (it gets the langc message) |
| B3 | src/main.c | `langc_dirty` is 0 | the D6 langc message row (exit 0 on the copy) |
| B4 | src/main.c | no source compare | the SOURCE case of build/rebuild-review (exit 0, success) |
| B5 | src/main.c | no byte compare | the output-hash case of build/rebuild-review (exit 0, success) |
| C1 | src/manifest.c | the langc keys are in a different order | 25 FAIL rows: the manifest rows |
| C2 | src/manifest.c | the dirty flag is always false | the D6 manifest message row, as B2 |
| C3 | src/manifest.c | the keccak256 of size - 1 bytes | 23 FAIL rows: the manifest rows |

The EQUIVALENT mutants:

- S8: with `^` kept, the line must start with `request N N (digest N
  N N N)`. Only a closed value of type `Request` starts with the head
  `request`, and only the newline comes after its full form. A partial
  application prints as `fun _x0 => ...`, a wrapper prints its own
  head first, and `REFUSE_NAME` (rule R4) stops a user name that is a
  domain name.
- S10: `langc eval` checks all the declarations of PROG before it
  evaluates ENTRY, and it refuses the same programs with the same
  code. The service keeps the `check` call, because the contract is
  check, then eval (S8 D4).
- ws: a computed word above 2^64 - 1 is a trap, not a word (for
  example `natMul 4294967296 4294967296`). The eval prints `trap` in
  its place, so the request regex refuses it with
  `REFUSE_SIGN_REQUEST`. A literal above 2^64 - 1 is a
  `LEX_NAT_RANGE` refusal of `check`. The ws check stays as a second
  check.

B4 and B5 are not reached by the gate rows on a tree with a change:
the DIRTY refusal comes first. S8 D7 (Q-S8-7): test/rebuild-review.c
links `langc rebuild` with a clean version fixture, so its clean-tree
cases run on all trees, and they kill B4 and B5.

### Open items from S8

- O29, O30, O31 and O32 (docs/PLAN.md).
- The clean-tree rows of `langc rebuild` (exit 0, SOURCE, WRITER and
  OUTPUT) and the mutants B4 and B5: closed by S8 D7 (Q-S8-7). The
  rows are in test/rebuild-review.c, and the DIRTY rows of the gate
  are keyed on the dirty flag.
- S8 does not change the open items of S7.

## S9: agent kit

S9 adds the agent kit of the S9 row of the plan (Q-S9-1 to Q-S9-9
(a); Q-S9-10 OPEN, (a) in force). An agent finds the routing rule in
`AGENTS.md`, the loop in `skills/leaf-lang/SKILL.md`, and the same
steps as MCP tools in `langc mcp`.

- `skills/leaf-lang/SKILL.md`: the front matter (`name`,
  `description`), when to use leaf-lang (the routing rule of
  `AGENTS.md`), the loop (write, `check --json`, apply each `fix`,
  `fmt`, `explain`) and the 5 tools of `langc mcp`.
- `skills/leaf-lang/grammar-card.md`: the grammar card (Q-S9-7 (a)).
- `examples/graded/`: 5 graded examples, 1-nat-function to
  5-sign-verify-nonce (Q-S9-4 (a)). Each one checks with the output
  ok.
- `langc mcp` (src/mcp.c, src/mcp.h; Q-S9-1 (a), Q-S9-2 (a)): an MCP
  server on standard input and standard output with the tools
  `check`, `fmt`, `explain`, `list-base` and `show-law`.
  The server implements MCP 2025-06-18 and selects this version when
  a client requests an unsupported version.
- `langc explain [--json] PROG [NAME]` (src/main.c; Q-S9-3 (a)): the
  kind, the multiplicity, the leaves and the laws of each declaration,
  or of NAME only. `laws` is `["L5"]` when `leaves` is not empty, else
  `[]` (Q-S9-9 (a)).
- `docs/TOKEN-BASELINE.md` and test/tokcount.c (build/tokcount; Q-S9-5
  (a), Q-S9-8 (a)): an offline token proxy. The graded example 5 is
  563 proxy tokens. The same task in Rust
  (docs/token-baseline/keygen_sign.rs) plus Solidity
  (docs/token-baseline/NonceBinding.sol) is 971. The ratio is 1.72.
- test/mcp-review.c (build/mcp-review; Q-S9-6 (a)): the `langc mcp`
  review.
- `AGENTS.md` (final) and `README.md` (S9 D7).

### Gate rows (`make check`)

test/gate.sh has the S9 rows. The full list is in test/gate.sh.

- graded, 5 rows: `langc check` of each file of `examples/graded/`
  gives ok.
- explain, 4 rows: `langc explain` of test/explain/cases.lang
  (`--json`), of its NAME signOnce, of a NAME that is not in the
  program (nosuchname) and of test/check/sign-twice.lang (`--json`).
  The output must be the bytes of test/explain/expect.txt.
- mcp, 32 rows (build/mcp-review): 17 fixed rows (the replies to
  test/mcp/requests.jsonl are the bytes of test/mcp/responses.jsonl)
  and 15 example rows (one `tools/call check` for each file of
  `examples/` and `examples/graded/`: the last line of the text is the
  last line of `langc check --json FILE`).
- token baseline: the output of build/tokcount is the table of
  docs/TOKEN-BASELINE.md (between the tokcount marks).
- token ratio: 201 over 200 tokens must give the ratio 1.01 (rounded
  half up; a truncated ratio gives 1.00).
- The S9 gate: `make check` EXIT 0, gate 0 failures, graded 5 checked,
  explain 4 checked, mcp 32 checked (17 fixed, 15 examples), token
  baseline counts match, token ratio half up checked. The other counts
  are as in S8.

### Mutants

There are 13 mutants. 13 are killed. M1 to M5 are on src/mcp.c, E1 to
E4 on the explain code of src/main.c, and T1 to T4 on test/tokcount.c.
Each mutant is an exact-match edit, then `make -B`, then the kill rows
(build/mcp-review, the explain rows, the token baseline row and the
token ratio row), then a restore (a copy and a compare).

| Mutant | File | Edit | Killer |
|---|---|---|---|
| M1 | src/mcp.c | the tool name `check` is `chek` | build/mcp-review |
| M2 | src/mcp.c | the number id echo is `null` | build/mcp-review |
| M3 | src/mcp.c | the `show-law` lookup `== 0` is `!= 0` | build/mcp-review |
| M4 | src/mcp.c | no string id echo | build/mcp-review |
| M5 | src/mcp.c | the tool name `list-base` is `list-bas` | build/mcp-review |
| E1 | src/main.c | explain prints the multiplicity 1 as `ω` | the explain rows |
| E2 | src/main.c | explain prints the kind `def` as `postulate` | the explain rows, build/mcp-review |
| E3 | src/main.c | explain gives the leaves of all definitions, not of the definition only | the explain rows |
| E4 | src/main.c | the explain name lookup always finds the name | the nosuchname row |
| T1 | test/tokcount.c | code points = bytes | the token baseline row |
| T2 | test/tokcount.c | one token for each word character | the token baseline row |
| T3 | test/tokcount.c | no token for a non-ASCII code point | the token baseline row |
| T4 | test/tokcount.c | the ratio is truncated, not rounded half up | the token ratio row |

T4 was LIVE on the first run: 971 over 563 = 1.7247 gives 1.72 in both
forms. The token ratio row was then added, and it kills T4.

### Open items from S9

- Q-S9-10 (the base rows: (a) the kind and the slice only, (b) also
  the PLAN 3.2 type sketch): OPEN. (a) is in force.
- S9 does not change the open items of S8.
