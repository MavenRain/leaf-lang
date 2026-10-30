# leaf-lang plan

leaf-lang is a language for post-quantum accounts. A program signs with
XMSS one-time leaves and verifies XMSS signatures. Each leaf is an affine
resource: the checker refuses a program that can use one leaf two times.
The host is TinyCC. The targets are an EVM account verifier, a Wasm signer
and a zkml-lang program (5.3).

S1 status: the Agda-like surface, `langc fmt` and `langc check --json`
are built on the tcc-wasm kit of lang-template. XMSS, WOTS+ and affine
features are planned. Section 9 gives the slices.

## 1. Rulings and readings

USER rulings (2026-10-25, the request and AskUserQuestion; the words are in
`design/DESIGN.md` sections 1 and 2):

- R1 type formers: for type constructors and higher kinds, only cell monads
  and their duals, in a locally presentable, locally cartesian closed
  (infinity,1)-category, as Cubical TT realizes it. These are the same words
  as intent-lang, resolve-lang, restrict-lang, mandate-lang, quorum-lang
  and virtu-lang.
- R2 base types and operations: the minimum that the pasted post-quantum
  account description needs: XMSS for validators, per-account
  quantum-safe signatures through EIP-8141, and use at most once by affine
  types ("affine-lang's design carries over"; affine-lang is brisk).
- R3 host: TinyCC.
- R4 targets (3): the EVM account verifier (EIP-8141 VERIFY frame: XMSS
  verify, then APPROVE; and an ERC-4337 `validateUserOp` entry); the Wasm
  signer and keystore (keygen and sign, each one-time leaf an affine
  resource, EIP-8310 commit-before-sign state); a zkml-lang program
  (one proof of N XMSS verifies, the leanMultisig role). Not chosen: a C99
  signer library.
- R5 hash: abstract, bound per target. The language has one opaque
  tweakable hash. The laws hold for every hash. v1 binds Keccak-256 on EVM
  and Wasm. A Poseidon2 binding comes in a later slice.

USER rulings on section 11 (2026-10-25, AskUserQuestion in the S1
session):

- Q14 = (b): the Agda-like Unicode surface (section 7.1 and SPEC.md
  section 2). A named step is a top-level definition (signature and
  clause).
- Q1 = (a): the intent-lang ruled reading of R1 (section 2.1).
- Q3 = (a): affine discipline. Multiplicities 0, 1 and omega on binders
  (the QTT shape). 1 is affine: weakening is allowed, contraction is
  refused. Only resource sorts (`Leaf`, `Keystore`) need it.
- Q6 = (a): the v1 tweakable hash is `Keccak-256(seed || tweak || x)`, one
  call per step, with own vectors and an independent cross-check.

USER rulings on the S2 questions (2026-10-25, AskUserQuestion before the
S2 slice):

- S2-Q1 = (a): `Path A x y` is the canonical path type. The infix
  `x ≡ y` waits for implicit arguments (O20).
- S2-Q2 = (b), not the recommended (a): keep `Sum` as a domain CM. This
  closes O16.
- S2-Q3 = (b), not the recommended (a): only the anonymous Sigma
  `(x : A) × B`. S2 adds no `record` form. `Sig`, `PubKey`, `Tweak` and
  `Params` are top-level definitions of an iterated anonymous Sigma type
  (2.2, 3.2).

USER rulings on the S4 questions (2026-10-26, AskUserQuestion in the S4
sessions 1 and 2; the entries are in section 11):

- S4 = (a) for each item, but Q-S4-1 = (c). Q-S4-0: S4 builds on the USER
  commit 47a6f36. Q-S4-1: `cast keccak` is the only cross-check, for the
  single hash calls; WOTS+, L-tree and Merkle use own vectors. Q-S4-2: a
  builtin `digest` of four 64-bit words makes a digest value. Q-S4-3: the
  hash ops compute only in `langc eval`. Q-S4-4: the tweak is the RFC 8391
  ADRS, and only `prf` and `thMsg` have an RFC 8391 prefix (5.4). Q-S4-5:
  the builtins `natDiv` and `natMod` join the Nat builtins (3.2). Q-S4-6:
  the S4 signer is `pkFromSeeds` and `signAt`. Q-S4-7: L1 is an eval check
  at h = 4, with L6 laws at concrete digits. Q-S4-8: v1 fixes n = 32 and
  log2 w = 4, and the height is the index of `Sig` (3.2).

USER rulings on the S5 questions (2026-10-27, AskUserQuestion in the S5
sessions 1 and 2; the entries are in section 11):

- S5 = (a) for each item, but Q-S5-2 = (c) and Q-S5-5 = (b). Q-S5-0: S5
  builds on the USER commit f5e587d.
- Q-S5-1: the Wasm module has one internal `keccak256` function, and the
  IR has one hash node for `thChain`, `thNode`, `thMsg` and `prf` (5.2).
  The hash ops compute in `langc eval` and in the module, not in `check`
  (Q-S4-3 stays).
- Q-S5-2 and Q-S5-4: the one host import is `env.commit (param i64 i64)`,
  a key id and the new position. The key id is a host-chosen i64. The
  signer `keygen` export takes it and keeps it in the keystore state
  (5.2).
- Q-S5-3: test/run-wasm.mjs writes and reads an EIP-8310 v5 keystore JSON
  with leaf-lang values (5.2).
- Q-S5-5 and Q-S5-12: `sign : (l : Leaf) ⊸ Digest → Sig (leafHeight l)`.
  The core observation `leafHeight` has its leaf binder at multiplicity 0,
  so `leafHeight l` is not a use of `l`. A neutral `leafHeight l` and a
  neutral `height pk` do not convert. Thus a program verifies a `sign`
  result with the domain `verifyAt (leafHeight l) pk m`, and S4 `verify
  pk` is `verifyAt (height pk) pk` (3.2, L1 in section 6).
- Q-S5-6: the core base op `andThen` sequences `Durable` trees (the free
  monad bind).
- Q-S5-7: `commit` is sealed. A program use of `commit` is
  `REFUSE_SEALED`, and only `next` makes a `commit` node. Thus L4 holds by
  construction (2.2 row `Durable`, 4.2).
- Q-S5-8: `langc eval` gives a `Durable` tree as a value. A program entry
  gives ℕ or `Bool` through a guest fold over the tree. Only the signer
  exports call `env.commit`.
- Q-S5-9: the signer module exports `memory`, `keygen`, `publicKey`,
  `next`, `sign` and `verify`. A Digest is 32 bytes at an i64 address
  (5.2).
- Q-S5-10: L1 (the keystore form), L3, L4 and L5 are eval rows at h = 4
  and refusal fixtures (section 6).
- Q-S5-11: the differential runs at h = 4. At h = 10 only the module runs
  (5.2).
- Q-S5-13 to Q-S5-15 (U2): the keystore and leaf values are two internal
  core value ops with no source name; `langc eval` prints them as
  `keystore h i` and `leaf h i` (no seed, no public key); the U2 `done
  none` row of `next` uses a closed h = 0 keystore (section 11).
- Q-S5-16 and Q-S5-17 (U2): the core op name stays `next`. R4 refuses a
  declared name that is a core name, so the fixtures that declared
  `next` now declare `addOne`. The checker gives the type `Option (Leaf
  × Keystore)` to the `next` core node as a hidden first arg (source
  arity 1, eval arity 2). A stuck `next ks` prints with that type
  (section 11).
- Q-S5-18 (U2): `verifyAt` takes its Sig at omega. A row or a guest
  program packs the height and the Sig of a leaf into a `(h : ℕ) × Sig
  h` value out of the scope of the leaf, then verifies that value
  (section 11).
- Q-S5-20 and Q-S5-21 (U4, USER 2026-10-27): Q-S5-20 (b), the commit call
  (its IR form, its lowering and a row) moves to U6 with the signer
  exports. Q-S5-21 (b), U4 lowers the keystore and leaf ops: the keystore
  and leaf values as blocks [skSeed, skPrf, pk, i], keygen and sign through
  pkFromSeeds and signAt with the hash node, and a digit op in the IR.
  Q-S5-22 (a) (USER 2026-10-27): U4 lowers `next` and `andThen` with no
  host call (the Durable tree as data, a `fold` over Durable as an
  eliminator); U6 adds the commit host call at the signer `next` export.
  Correction (U4c survey): a keystore op with closed args lowers by eval
  (each hash op stays stuck and lowers as a hash node; keygen at
  v1Params 0 gives 1138 hash statements), so Q-S5-21 (b) and Q-S5-22 (a)
  need no new arm for closed args. Q-S5-23 (a), Q-S5-24 (a) and Q-S5-25
  (a) (USER 2026-10-27): a stuck keystore op (a neutral seed or height)
  and a keystore or leaf value give REFUSE_RUNTIME_KEYSTORE (runtime keys
  in U6, with the signer state and the key id); keystore rows use the
  gate row `ir_digest NAME` (section 11).
  Q-S5-26 (a) to Q-S5-29 (a) (USER 2026-10-27, U5 design): the Wasm
  Keccak-f is one internal function with 24 unrolled rounds over i64
  locals; keccak256 swaps the bytes of the big-endian IR words;
  test/ir/hash.lang and test/ir/keystore.lang join the Wasm
  differential; REFUSE_TARGET_NODE stays with no fixture (section 11).
  Q-S5-31 (a), Q-S5-32 (a), Q-S5-33 (b) and Q-S5-34 (a) (USER
  2026-10-27, U6 design): src/wasm.c writes the signer as hand Wasm (a
  port of the S4 domain algorithms over keccak256, trusted base, section
  6). Only the `next` export calls env.commit, so the IR gets no commit
  node. `langc signer --height H -o FILE` writes the signer module for
  one height. The h = 4 differential compares weighted sums (section 11).
  Q-S5-35 (a) and Q-S5-36 (a) (USER 2026-10-27, U6a): the harness line
  `keygen ID SKSEED SKPRF PUBSEED [START]` sets the start position. With
  no START, the start is the position in DIR/ID.state (0 when there is no
  file). A START below that position models a stale backup. The keystore
  `uuid` is RFC 9562 version 8 with the 16 hex digits of the key id.
  Q-S5-37 (a), Q-S5-38 (a) and Q-S5-39 (a) (USER 2026-10-27, U7): the
  harness lines `sign LEAF M` (it prints `sigSum N`) and `verify M`.
  The sign export takes a leaf (a `next` result). It traps unless the
  leaf is after the last signed leaf and not after the next position.
  The verify export takes the public key. It gives 0 when the key
  height is not H.
  Q-S5-40 (a) (USER 2026-10-28, U7d): the M6 mutant (verify with no H
  check) stays a known survivor; no new harness line.

USER rulings on the S6 questions (2026-10-28, AskUserQuestion in the S6
session 1; the entries are in section 11):

- S6 = (a) for each item. S6 builds on the USER commit fac4ee2 (tree
  fd287276: S5 and the USER review fixes).
- Q-S6-1: the verifier is fixed EVM code that `langc verifier` writes by
  hand in C, with no IR (the S5 signer pattern). The port takes only the
  kit `src/asm.c` and `src/asm.h`, not the kit IR lowering, because the
  kit lowering refuses loops.
- Q-S6-2: entry 1 reads the signature hash and the nonce with TXPARAM
  and the signature from calldata, and runs APPROVE with the scope
  "both". The shim changes only these three table sites: the nonce and
  the signature hash come from a 64-byte calldata prefix, and APPROVE
  becomes a RETURN of the 32-byte word 1 (5.1).
- Q-S6-5: the EIP-8141 signature hash covers the VERIFY frame data (the
  calldata), so entry 1 reads the signature from entry 0 of the
  transaction `signatures` list (scheme ARBITRARY), not from calldata.
  The two builds first copy the signature to one memory region, and the
  core reads it there. The copy is a fourth table site (5.1).
- Q-S6-3: entry 2 returns 1 (`SIG_VALIDATION_FAILED`) on each accept
  failure (signature, root, nonce sequence, nonce key not 0). Malformed
  calldata and a caller that is not the v0.7 EntryPoint (a build
  constant) revert. The account pays `missingAccountFunds` to the
  caller (5.1).
- Q-S6-4: the gas script runs one real H = 16 keygen in the Wasm signer.

USER rulings on the S7 questions (2026-10-28, AskUserQuestion in the S7
sessions 1, 2 and 4):

- Q10: (a) BabyBear. The Poseidon2 binding is over BabyBear (section 11).
  O3 closes.
- Q-S7-1: (a) the zkml target is a zkml-lang program, not a `Circuit`
  term. The writer prints a zkml-lang source file: defs over the prelude
  (field ops on `Nat` words, Poseidon2, `verify`, and the `aggregate`
  statement as an entry). zkml-lang `langc check` and `langc build`
  accept it, and the Wasm run gives the value. L7: that value equals the
  leaf-lang `aggregate` value on each vector. zkml-lang does not change.
  The output has no prover export (O26, 5.3).
- Q-S7-2: (a) the Poseidon2 instance of Plonky3 rev 5fcf8d7c (the
  zkml-lang lock). The vectors come from the tests of that crate and from
  a cargo cross-check outside the sandbox, as `cast keccak` in S4 (5.4).
- Q-S7-3: (a) the 5.4 byte layout and the 4-word `Digest` stay. A sponge
  absorbs the bytes as injective field limbs, and the 8-element output
  packs into `digest w0 w1 w2 w3` (5.4).
- Q-S7-4: (a) the writer output is a fixed zkml-lang text plus data defs
  (5.3).
- Q-S7-5: (a) the Horizen Labs constants of Plonky3,
  `default_babybear_poseidon2_16()` (baby-bear/src/poseidon2.rs:60-128)
  (5.4).
- Q-S7-6: (a) the cargo cross-check crate (outside the repository) can
  fetch its crates from the network.
- Q-S7-7 (2026-10-29): (a) `langc eval [--hash keccak|poseidon2] PROG
  NAME [ARGS...]`. The flag comes before PROG (as `check --json`).
  Keccak is the default. Another value gives USAGE and exit 2 (5.4).
- Q-S7-8 (2026-10-29): (a) `aggregate` takes the count as a parameter,
  `(n : Nat) -> (Fin n -> Signer) -> Bool`, with the domain def `Signer` =
  `(pk : PubKey) * Digest * Sig (height pk)`: the PLAN type had a free `N`,
  and `Sig` takes the height of the key.
- Q-S7-9 (2026-10-29): (a) `langc zkml --count N PROG NAME [ARGS...] -o
  OUT`. The writer evaluates `NAME ARGS k` under the Poseidon2 binding for
  k = 0 to N - 1, and each value is a `Signer`. The L7 vector is `--count N
  test/eval/laws.lang l7Entry BAD`.
- Q-S7-10 (2026-10-29): (a) the fixed text of the writer is a zkml-lang
  file in the repository, `zkml/xmss.lang`, and `gen/embed.c` embeds it in
  `langc`. Before the fixed text, the writer prints the Poseidon2
  constants defs from the C table `poseidon2_babybear16`, so the constants
  have one source.
- Q-S7-11 (2026-10-29): (a) `gen/embed.c` takes an optional third
  argument NAME (`embed IN OUT [NAME]`, default `domain_source`), so one
  generator embeds `domain/domain.lang` and `zkml/xmss.lang`. The old
  calls give the same bytes.
- Q-S7-12 (2026-10-29): (a) the Z check of `zkml/xmss.lang` ran out of
  memory. Z ran each permutation on the neutral words of a signer, and
  the cost grows with the depth of the hashes (8 chained `thNode` or an
  ltree over 8 digests fill the arena). The sponge count in `p2Sponge`
  adds `natMul 0 (p2At limbs 0)`. This term is 0 on known words, so the
  value does not change. On neutral words the count is stuck, and the
  check does not run the permutation. No Z change.
- Q-S7-13 (2026-10-29): (a) the zkml output takes a parameter z. Each
  data word is `natAdd z w`, and the entry is `def zkAggregate : Nat ->
  Nat := fun z => aggregate (zkSigners z)` (D10). On a neutral z the Z
  check does not run the permutation. Z has one arena of 1 GiB for a
  run (the check, then the eval or the build), and it does not release
  the arena between defs: about 5 to 9 hashes fill it. Thus the Z eval
  and the Z build of the entry (about 600 hashes) run out of memory
  (O28). No Z change.
- Q-S7-14 (2026-10-29): (a) U6 adds the 6 Z known-answer evals as gate
  rows in the zkml block (the rows are skipped when zkml-lang is not
  there). kaPermZ, kaChain2Z and kaRoot2Z (z = 0), kaDigDiff and
  kaDigSDiff give 0, and kaAggNil gives 1. Each eval fits the Z arena
  (at most 871 MiB). The L7 run row of D6 waits on O28 (D11). The
  leaf-lang L7 rows stay. No Z change.
- Q-S7-15 (2026-10-29): (a) U7 runs 12 mutants on the S7 checks: on
  zkml/xmss.lang and src/zkml.c (the zkml block of the gate kills them,
  or a detector of the writer data words for the 2 sites that no gate
  row reads), on src/front/poseidon2.c (fronttool) and on src/main.c
  (the `--hash sha3` row). `langc zkml` takes Nat ARGS only: 5.3 says
  so, and the usage text does not change.

Not ruled. The plan takes (a) for each of these:

- Q2 kit: (a) by default; the USER did not rule it.
- Q4 keystore: (a) by default; the USER did not rule it.
- Q5 commit-before-sign: (a) by default; the USER did not rule it.
- Q7 v1 parameters: (a) by default; the USER did not rule it.
- Q8 on-chain one-time: (a) by default; the USER did not rule it.
- Q9 Wasm crypto: (a) by default; the USER did not rule it.
- Q11 proofs: (a) by default; the USER did not rule it.
- Q12 two signers: (a) by default; the USER did not rule it.
- Q13 token counts: (a) by default; the USER did not rule it.

Claude's readings (NOT ruled; each is the (a) of a section 11 item, taken
by default):

- Kit: the seed is lang-template `hosts/tcc-wasm`. The signer is where the
  affine checker works. The EVM writer ports from `hosts/tcc-evm-contract`.
  The zkml writer ports from zkpolicy-lang (Q2).
- Keystore: the dual of a cell monad, a stream of leaves, and affine. A
  copy of a keystore copies its leaves, so the EIP-8310 "silent reset of
  the signing position" becomes a type error in one program (Q4).
- Commit-before-sign: `next` gives a leaf only in the continuation of a
  `commit` node of the `Durable` cell monad. A signature needs a leaf, so
  the commit comes first by data dependency (Q5).
- On chain: the VERIFY frame is a STATICCALL and cannot record a used leaf.
  Thus the account binds the leaf index to the account nonce (Q8). Only the
  affine signer stops two signatures at one nonce.
- Proofs: in the guest language only. A Lean export is not a target (Q11).

Claude's readings for S2 (NOT ruled):

- Domain CM (S2-Q2 (b)): 2.1 says that a CM is a family with a type
  parameter, and only `domain/domain.lang` declares a family. Thus `Sum`
  leaves the builtin ambient set and becomes a declaration in
  `domain/domain.lang`: `data Sum (A B : Type) : Type where` with
  `inl : A → Sum A B` and `inr : B → Sum A B` (S2 unit U6). Each
  constructor has one field, and each type parameter has exactly one
  generator. Thus the intent-lang `REFUSE_CELL` criterion (79e1898
  `src/front/check.c`, `cell_field` and `cell_generators`) admits it. If
  the leaf-lang port of the criterion refuses `Sum`, S2 stops and the
  USER rules. `Unit` stays ambient.
- Cofamily surface (S2-Q3 (b)): there is no `record` form, so a CC is
  declared as `codata D (A : Type) : Type where` with one observation
  `eps : D A → A` and fields of type `D A → D A` or `D A → E → D A` (`E`
  closed data), as the 2.1 CC row says. S2 U6c1 reading: a line is the
  observation type with an unnamed self argument (`head : Stream A →
  A`). The block keeps the `data` shape, not the intent-lang `cofamily D
  params := corec fields` form: the leaf surface has one block form, the
  corecursor has no surface name, and each line reads as the type of its
  observation. The intent-lang shape check (`cocell_shape`) reads the
  lines as the fields of its one clause, so PLAN and the port give the
  same answers. An admitted codata binds D and its line names (S2 U6c2a).

## 2. The reading of R1

### 2.1 The precedent reading

leaf-lang takes the reading that intent-lang (PLAN 2.2, USER ruling S1
Q1 (a) and Q2 (a), 2026-10-22) and resolve-lang (PLAN 2) use (Q1):

| Part | Cubical TT | R1 limit |
|---|---|---|
| Ambient | Pi, Sigma, `Path`, `PathP`, `I`, `i0`, `i1`, `transp`, `hcomp`, `U`, `Glue`, `ua`, the builtin `Unit` (`Sum` is a CM, S2-Q2 (b)) | none: the internal language of the category |
| CM, cell monad | a `family` with a type parameter: the parameter is the one field of one generator `eta : A -> T A`; each other constructor takes closed base data and arguments of type `T A`, or is a path in `T A` | else `REFUSE_CELL` |
| CC, cocell comonad | a `cofamily` with one observation `eps : D A -> A`; each other field has type `D A -> D A` or `D A -> E -> D A` with `E` closed data | else `REFUSE_CELL` |

Why the two adjectives matter: local presentability gives the free algebra
of each set of cells, so each cell signature has its monad. Local cartesian
closure gives Pi and Sigma by base change, so the ambient part exists.

A family with no type parameter is a base type, not a type constructor. R2
limits it, not R1. Only `domain/domain.lang` declares a family or a
cofamily. A program that declares one gets `REFUSE_DATA`.

S2 U6a reading: the S1 grammar has no index form, so `data F : Type 0 →
Type where` is a parse refusal (`PARSE_EXPECT`). The type index and
and-block arms of the intent-lang criterion are not ported. A use of the
builtin names `List`, `nil` and `cons` stays `REFUSE_NAME`. A program gets
`REFUSE_DATA` before the criterion, so the `REFUSE_CELL` rows are in the
test/fronttool.c domain regressions. A recursive field at the own
parameters of a cell family passes the criterion and then gets
`DOMAIN_FAMILY`: S2 U6a has no fold over a recursive family with
parameters.

### 2.2 Every type former leaf-lang needs

| Former | Use in leaf-lang | Class | Reason |
|---|---|---|---|
| Pi, `A -> B` | every function; vectors `Fin n -> Digest` | ambient | LCC |
| `A -o B` | at-most-once function (section 4) | ambient | Pi with multiplicity 1 on the binder: `(@1 _ : A) -> B`, surface `A ⊸ B` (Q-S3-2 (a), 7.1); `A -o B` and `A -> B` do not convert (Q-S3-3 (a)) |
| Sigma, `(x : A) × B` | `Sig`, `PubKey`, `Tweak`, `Params` | ambient | each is a top-level definition of an iterated anonymous Sigma type; there is no `record` form (S2-Q3 (b)) |
| `A * B` at multiplicity 1 | `Leaf * Keystore` | ambient | Sigma with multiplicity 1; `witness` and `payload` each use the pair one time, so the split rule `unpack p (λ a b → t)` gives both halves at 1 (Q-S3-6 (a), S3) |
| `Fin n` | positions in a vector | defined | `Sigma (k : Nat), Path Bool (lt k n) true`; in leaf `Fin n = (k : ℕ) × Path Bool (lt k n) true` with `lt k n = natLe (natAdd k 1) n`, both defs in `domain/domain.lang` (S2 U7; no new builtin or rule) |
| vectors | WOTS+ signature, auth path, chain ends | ambient | `Fin n -> Digest`, no new former |
| `Path`, `PathP`, `I`, `transp`, `hcomp` | laws and proofs | ambient | the identity type of Cubical TT |
| `U`, `Glue`, `ua` | universes; no leaf-lang use yet | ambient | kept as in the precedent |
| `Option A` | `next` on an empty keystore | CM | one 0-cell `none`, generator `some` |
| `Sum A B` | kept from the kit (S2-Q2 (b)); a domain family since S2 U6b2 | CM | declared in `domain/domain.lang` as `data Sum (A : Type 0) (B : Type 0)`; generators `inl : (inlOf : A) → Sum A B` and `inr : (inrOf : B) → Sum A B`, one for each type parameter; the eliminator is `fold` (S2 R1 (b): no `either`, no Sum monad) |
| `Durable A` | the signer effect (commit-before-sign) | CM | declared in `domain/domain.lang` as `data Durable (A : Type 0)` (S3, Q-S3-8 (a)); generator `done : (doneOf : A) → Durable A`; one operation `commit : (at : ℕ) → (cont : Durable A) → Durable A` with closed data `ℕ` and `cont` the family at its own parameter (field name Q-S3-10 (a)) (the free cell monad of one operation, as mandate-lang 4.6); `commit` is public in S3; from S5 a program use of `commit` is `REFUSE_SEALED`, and only `next` makes a `commit` node, so L4 holds by construction (Q-S5-7 (a)); the core base op `andThen` sequences trees (Q-S5-6 (a)) |
| `Trunc A` | "a valid signature exists" in laws | CM | one 1-cell `squash` |
| `Stream A` | the keystore reading (section 4.3) | CC | a codata in `domain/domain.lang` (S3, Q-S3-7 (a)); observation `head : Stream A -> A`, field `tail : Stream A -> Stream A` (shape check S2 U6c1; D, the private D.pos and the destructors S2 U6c2a; the private walk D.walk and corecursor D.corec S2 U6c2b; the bisimulation is deferred: it needs a dependent elim over D.pos, S2-HANDOFF R7) |
| cons `List A` | none | REFUSED | `cons : A -> List A -> List A` uses `A` outside the generator; vectors over `Fin` do the work |
| `Bag A`, free monoid | none | not admitted | no bullet needs a multiset or a sequence of unknown length |
| `Either E A` | none | not admitted | errors are `Option` or `Bool`; no bullet needs an error payload |
| `data Pair (A : U) := mk A A` | none | REFUSED | `REFUSE_CELL` example of the precedent; the ambient `A * A` is the admitted form |

The proof rules (S2 U4e1). `refl`, `symm`, `trans`, `cong` and `transport`
are surface rules only. The check elaborates each one to a Core term of the
cubical layer, and keeps its leaf check and its message:

- `refl a` is `λ i → a` (PLAM).
- `symm p` is `λ i → p (~ i)`.
- `trans p q` is `λ i → hcomp A (~ i ∨ i) (λ j → [ ~ i ↦ a , i ↦ q j ]) (p i)`.
- `cong f p` is `λ i → f (p i)`.
- `transport P e u` is `transp (λ i → P (e i)) i0 u`.

Two results follow. First, the trap flag of the proof drops under
`transport` when the line is regular: `transp` over the line `λ i → ℕ`
gives `u`. This is the zk behavior, where `OP_TRANSP` lowers to
`runtime_transp` only when it stays unreduced. Second, `transp` at a neutral
line (`λ i → P 1200` with `P` a variable) stays stuck. There is no
regularity rule, so `transport P refl u` is not definitionally `u` (as in
Cubical Agda, where `transportRefl` is a path). The eval row `equality
moved` shows this term.

S2 U5c adds `transp` at a Glue type with the CCHM formula (CCHM 6.2):
unglue at `i0`, transport through the base, a fiber of each equivalence at
`i1`, and `glue` of the parts. CCHM Glue does not keep regularity, so leaf
has no constant-line rule at Glue either. zk `transp_glue` gives `u` when
the line is constant, and S2 does not port that rule. `transp` along a
constant Glue line computes by the formula and can give a `glue` term, not
`u`. The rows `uaId3` and `uaSwapped` in `test/eval/glue.lang` transport
along `ua`.

S2 U5 (U5a..U5d) adds Glue types. The core has `Glue A [ φ ↦ pack T e ]`,
`glue [ φ ↦ t ] a` and `unglue g`. `ua A B e` is a domain definition over
Glue, so it is `A` at `i0` and `B` at `i1` by computation. `transp` at
Glue (`transp_glue`, U5c) and `hcomp` at Glue (`hcomp_glue`, U5d) compute.
When a branch face does not name `i`, the face holds for all `i` on a face
delta, and `transp_glue` extends the fiber there (`fiber_extend`). The
rows `transpGl` and `hcompGlue` in `test/eval/glue.lang` show these cases.
S2 does not port `hcomp` at the universe (zk `hcomp_univ`): `hcomp` at
`Type n` stays stuck, and the zk rows that need it (`hcompUAt0`) are not
in leaf. `hcomp` at a pretype is the refusal TYPE_PRETYPE.

S2 U4e2 deletes the old proof Ops `OP_REFL`, `OP_SYMM`, `OP_TRANS`,
`OP_CONG` and `OP_TRANSPORT`, and the code that only they use
(`reduce_proof`, `conv_refl`, the `refl` arm of the path conversion,
`proof_op` and `transport` in the lowering). The Op enum has 50 Ops. The
`op` column of the five BUILTINS rows is what the rule builds: `OP_PLAM`,
and `OP_TRANSP` for `transport`. No rule reads this column.

## 3. The minimal base signature (R2)

### 3.1 The bullets

The pasted description (`design/DESIGN.md` section 1) has four bullets.
Each admitted name cites one:

- B1: "XMSS hash-based signatures for validators".
- B2: "per-account quantum-safe signatures through EIP-8141, considered for
  the Hegotá upgrade".
- B3: "layer-1 work done around 2029".
- B4: "XMSS signatures are stateful: signing twice with one one-time key
  breaks security. Affine types enforce 'use at most once' directly, so
  affine-lang's design carries over."

B3 is a schedule. It admits no name. It is the reason for R5: the hash and
the parameters must not be fixed in the language, because the EF choice
(Poseidon2 for leanSig) is not final before 2029.

### 3.2 Admission table

Kinds: base type, base op (a primitive with a binding per target), hash op
(opaque: the kernel never unfolds it, R5), defined (a guest definition from
admitted names), resource (a base type of the affine sort), target
boundary (an input or an effect that a writer binds; no guest meaning past
its type), law (a guest theorem, section 6).

| Name | Kind | Type (sketch) | Bullet | Slice |
|---|---|---|---|---|
| `Bool`, `true`, `false` | base type, base ops | | B1, B2 (a verify result) | S1 |
| `Nat`, `zero`, `suc` | base type, base ops | | B1 (leaf index, chain step, height), B2 (nonce) | S1 |
| `natAdd`, `natSub`, `natMul`, `natDiv`, `natMod`, `natEq`, `natLe`, `lt` | base op (`lt` defined) | `Nat -> Nat -> Nat` (`natEq`, `natLe`, `lt`: `Nat -> Nat -> Bool`) on the machine word (uint64); overflow and division by 0 trap; `natSub` stops at 0; `lt k n = natLe (natAdd k 1) n` in `domain/domain.lang`; `natDiv` and `natMod` are new in S4, the others exist at 47a6f36 (Q-S4-5 (a), O8) | B1 (base-w digits, checksum, path bits) | S4 |
| `Digest` | base type | 32 bytes (v1 n = 32, Q-S4-8 (a)), the hash output; copyable (resource class NONE) | B1 | S4 |
| `LogW`, `logW1`, `logW2`, `logW4`, `logW8`, `logWBits` | base type, base ops | `LogW` is log2 w of WOTS+, copyable (resource class NONE); the values are `logW1`, `logW2`, `logW4`, `logW8`; `logWBits : LogW -> Nat` gives 1, 2, 4 or 8; no bad value exists, so there is no logW trap (Q-S4-U2-d) | B1 (base-w message encoding) | S4 |
| `digest` | base op | `Nat -> Nat -> Nat -> Nat -> Digest`: four 64-bit big-endian words, `w0` = bytes 0..7; a closed hash result is a `digest` value, and the printer prints `digest w0 w1 w2 w3`; no lexer change (Q-S4-2 (a)) | B1 (seeds, vectors) | S4 |
| `digestEq` | base op | `Digest -> Digest -> Bool`; obligation: decides `Path Digest` | B1 (verify compares a root) | S4 |
| `digit` | base op | `LogW -> Digest -> Nat -> Nat` (log2 w, digest, position): the RFC 8391 base_w digit, from byte 0 and the most significant bits first; a position past the digest traps (Q-S4-U2-a, Q-S4-U2-d); obligation: result `< w` | B1 (base-w message encoding) | S4 |
| `Tweak` | defined | the right-nested iterated Sigma `(layer : Nat) × (tree : Nat) × (kind : Nat) × (leaf : Nat) × (chain : Nat) × Nat` (the last part is step); a value is `pack layer (pack tree (pack kind (pack leaf (pack chain step))))`; `thChain`, `thNode` and `prf` take this Sigma and pack it as the 32-byte RFC 8391 ADRS (5.4, Q-S4-4 (a), Q-S4-U3-b) | B1 (RFC 8391 address) | S4 |
| `thChain` | hash op | `Digest -> Tweak -> Digest -> Digest` (public seed, tweak, value) | B1 (WOTS+ chain step F) | S4 |
| `thNode` | hash op | `Digest -> Tweak -> Digest -> Digest -> Digest` | B1 (L-tree and Merkle node H) | S4 |
| `thMsg` | hash op | `Digest -> Digest -> Nat -> Digest -> Digest` (randomness, root, index, message) | B1 (H_msg) | S4 |
| `prf` | hash op | `Digest -> Tweak -> Digest` (secret seed, tweak) | B1 (WOTS+ secret keys, signing randomness) | S4 |
| `Params` | defined | iterated Sigma: `n : Nat`, `logW : LogW`, `h : Nat` (section 5.4, Q-S4-U2-d); the definitions fix n = 32 and logW = 4 (len = 67); `n` and `logW` stay for the EIP-8310 JSON (Q-S4-8 (a)) | B1, B3 | S4 |
| `chain`, `wotsPk`, `wotsSign`, `wotsPkFromSig`, `ltree`, `treeHash`, `authPath`, `rootFromPath` | defined | RFC 8391 algorithms over the hash ops; `ltree` and `treeNode` by fixed nesting (`lTreeUp`, `up`, `lv0` to `lv20`), HMAX = 20; above HMAX, `verify` gives false (section 4.1, Q-S4-U5-a (a)) | B1 | S4 |
| `pkFromSeeds` | defined | `(skSeed : Digest) -> (pubSeed : Digest) -> (p : Params) -> PubKey`; S5 `keygen` uses it (Q-S4-6 (a)) | B1, B3 | S4 |
| `signAt` | defined | `(skSeed : Digest) -> (skPrf : Digest) -> (pk : PubKey) -> (i : Nat) -> (m : Digest) -> Sig (height pk)`; S5 `sign` calls it through the sealed `Leaf` eliminator (Q-S4-6 (a), Q-S4-8 (a)) | B1, B4 | S4 |
| `PubKey` | defined | iterated Sigma: `root`, `pubSeed`, `params`; `height : PubKey -> Nat` gives the `h` field of `params` (Q-S4-8 (a)) | B1, B2 | S4 |
| `Sig` | defined | `(h : Nat) -> Type 0`, an iterated Sigma: `index : Nat`, `rand : Digest`, `wots : Fin 67 -> Digest`, `auth : Fin h -> Digest` (Q-S4-8 (a)) | B1, B2 | S4 |
| `boolElim` | base op | `(P : Bool -> Type 0) -> P true -> P false -> (b : Bool) -> P b`, the dependent Bool eliminator: it computes on `true` and `false`, and a neutral `b` stays stuck; the domain `finAt` uses it to read a `Fin n` field at a fold counter (Q-S4-U4-a (a)) | B1 (the 67 chains, the h auth nodes) | S4 |
| `verify` | defined | `(pk : PubKey) -> Digest -> Sig (height pk) -> Bool`; false when `n` or `logW` is not the v1 value (Q-S4-8 (a)); from S5, `verify pk` is `verifyAt (height pk) pk`, with the same results (Q-S5-12 (a)) | B1, B2 | S4 |
| `Leaf` | resource | one unused one-time key; a core base type with no constructor and no eliminator in S3 (Q-S3-7 (a)); sealed: only `next` makes one (`REFUSE_SEALED`) | B4 | S3 |
| `Keystore` | resource | the secret seeds, the public key and the signing position; a sealed domain definition `Stream (Option Leaf)` (Q4 (a), Q-S3-7 (a)); a program that unfolds it (a `Stream` observation or `D.corec` at `Keystore`) gets `REFUSE_SEALED` | B4, R4 (EIP-8310) | S3 |
| `Durable` | CM (domain) | section 2.2 | B4 (commit-before-sign) | S3 |
| `keygen` | base op | `Digest -> Digest -> Digest -> Params -> Keystore` (secret seed, PRF seed, public seed); it uses `pkFromSeeds`; the signer `keygen` export also takes the key id of the `commit` import, but the guest op does not (Q-S5-4 (a)) | B1, B4 | S5 |
| `publicKey` | base op | `Keystore -o PubKey * Keystore` | B1, B2 | S5 |
| `next` | base op | `Keystore -o Durable (Option (Leaf * Keystore))` | B4 | S5 |
| `sign` | base op | `(l : Leaf) -o Digest -> Sig (leafHeight l)`: it calls `signAt` through the sealed `Leaf` eliminator, with the seeds, the public key and the index in the leaf value; it takes no public key and has no trap (Q-S5-5 (b), Q-S4-6 (a)) | B1, B4 | S5 |
| `leafHeight` | base op | `Leaf -> Nat`, the height of the keystore of the leaf; its leaf binder has multiplicity 0, so `leafHeight l` is not a use of `l`; a neutral `leafHeight l` and a neutral `height pk` do not convert (Q-S5-5 (b)) | B1, B4 | S5 |
| `verifyAt` | defined | `(h : Nat) -> (pk : PubKey) -> Digest -> Sig h -> Bool`: the S4 `verify` algorithm at height `h`; false when `natEq h (height pk)` is false; a program verifies a `sign` result with `verifyAt (leafHeight l) pk m (sign l m)` (Q-S5-12 (a)) | B1, B2 | S5 |
| `andThen` | base op | `Durable A -o (A -o Durable B) -o Durable B`, the free monad bind: `andThen (done a) k` reduces to `k a`, and `andThen (commit i t) k` reduces to `commit i (andThen t k)` (Q-S5-6 (a)) | B4 | S5 |
| `accept` | defined | `PubKey -> Nat -> Digest -> Sig -> Bool` = `natEq (index s) nonce` and `verify` | B2 (nonce binding) | S6 |
| `sigHash`, `txNonce` | target boundary (EVM) | the EIP-8141 signature hash and nonce, or the ERC-4337 `userOpHash` and nonce sequence | B2 | S6 |
| `approve` | target boundary (EVM) | APPROVE (0xaa) when `accept` is true, else REVERT | B2 | S6 |
| `commit` (interpretation) | target boundary (Wasm) | the host import `(import "env" "commit" (func (param i64 i64)))`, a key id and the new position (Q-S5-2 (c)); the key id is the host-chosen i64 of the signer `keygen` export, kept in the keystore state (Q-S5-4 (a)); the import writes the new position durably before it returns; only the signer exports call it (Q-S5-8 (a)); a program cannot build a `commit` node (Q-S5-7 (a)) | B4, R4 | S5 |
| `aggregate` | defined | `(n : Nat) -> (Fin n -> Signer) -> Bool` = all `verify`, with `Signer` = `(pk : PubKey) * Digest * Sig (height pk)` (Q-S7-8 (a)) | B1 (validators, leanMultisig role) | S7 |
| L1 to L7 | law | section 6 | B1, B2, B4 | S4 to S7 |

Readings:

- `Digest` is a base type, not `Fin n -> Byte`. One name is fewer than
  `Byte`, a pack and an unpack. O7 keeps the other choice open.
- `sign` and `verify` are guest definitions inside the domain. `sign` uses
  the sealed eliminator of `Leaf`. A program can call `sign` but cannot
  open a leaf (intent-lang sealed family precedent, rule O7 there).
- `Leaf * Keystore` uses the affine pair. `publicKey` returns the keystore
  so that a program reads the public key without a second use.

### 3.3 Refused candidates

| Candidate | Reason |
|---|---|
| ECDSA, secp256k1 | not quantum-safe; EIP-8141 lets an account leave it |
| BLS12-381 aggregation | not quantum-safe; the zkml target has the aggregation role (B1) |
| Falcon (FN-DSA), ML-DSA (Dilithium) | lattice signatures; the description names XMSS |
| SLH-DSA (SPHINCS+) | stateless; the description is about the stateful XMSS and its affine fit |
| LMS and HSS (SP 800-208) | stateful too, but the description names XMSS |
| XMSS^MT, generalized XMSS with 2^32 lifetime | not needed at `h <= 20`; O4 keeps it for the leanSig parameters |
| leanSig target-sum encoding | belongs to the Poseidon2 slice (O11); v1 uses the RFC 8391 checksum |
| SHA-256, SHAKE as v1 bindings | R5 binds Keccak-256 in v1 |
| a random number source | seeds come from the host; entropy is outside the language (O13) |
| storage, maps, a used-leaf bitmap | the VERIFY frame is a STATICCALL; the nonce binding replaces them |
| fees, gas prices, time, epochs | no bullet; B3 is a schedule |
| general bytes, strings, `Int`, floats | `Digest` and `Nat` are enough |
| `split` of a keystore (EIP-8310 reserved ranges) | no bullet needs two signers on one key; Q12, O5 |
| kit names `Flag`, `Prod`, `List` (cons), `Eq`, `filter` | `Flag` becomes `Bool`; `Prod` is the ambient Sigma; cons is REFUSED (2.2); `Eq` becomes `Path`; `filter` has no use |

The kit `Sum` former is not in the row above. S2 U6b2 moved it to
`domain/domain.lang` (O16). `Either` is not admitted (2.2).

## 4. The affine layer (B4)

### 4.1 Rules

- Each binder has a multiplicity: 0 (erased: types, laws, proofs), 1 (at
  most once) or omega (no limit). The context splits over the premises of
  a rule, as in QTT (Atkey 2018). The count is a count of uses on one
  evaluation path, not a count of names in the text.
- Resource sorts: `Leaf` and `Keystore`. A type is a resource type when a
  resource sort occurs in a runtime position of it: a field of a Sigma, the
  payload of `Option` or `Durable`, the head of a `Stream`. A variable of a
  resource type has multiplicity 0 or 1.
- Weakening is allowed: a program can drop a leaf. A dropped leaf wastes
  capacity and never weakens security. Contraction is refused.
- `A -o B` is `(@1 _ : A) -> B`: a Pi with multiplicity 1 on the binder
  (Q-S3-3 (a), the QTT reading of Q3 (a)). `A -> B` is a Pi with an omega
  binder. A closure type has no multiplicity bit: each use of the closure
  scales the uses of its captures. `A -o B` and `A -> B` do not convert
  (`TYPE_MISMATCH`); v1 has no subsumption.
- A `match` uses its scrutinee once. All arms start from the same use set.
  The result use set is the maximum of the arms (brisk variants rule).
  leaf-lang has no `match` form: the rule applies to the eliminators
  `option`, `flagIf`, `fold`, `witness` and `payload`. The split rule
  `unpack p (λ a b → t)` uses `p` one time and binds `a` and `b` at 1
  (Q-S3-6 (a)).
- Types are checked at multiplicity 0, so `Path Leaf l l` in a type uses
  nothing. Laws quantify over leaves at 0 and never run. A definition
  whose type ends in `Path`, `PathP` or a universe (a law, a proof or a
  type) is checked at 0; each other definition is checked at 1
  (Q-S3-5 (a)).
- A 0-variable in a run-time position is a use above its multiplicity:
  `REFUSE_AFFINE_REUSE` with the message "used 1, allowed 0"
  (Q-S3-4 (a)).

- Eliminator payload binders are affine when their type contains a
  resource, is polymorphic, or is a function that may capture one.
  Concrete resource-free payloads retain omega handlers. This applies
  to `option`, `map`, `bind` and the fields and results passed to `fold`
  cases. Resource classification examines instantiated fields of every
  domain family, including `Sum` and closed resource wrappers.
- A definition checked at 0 can be referenced in executable code only
  when its body also checks at 1. An unsafe erased proof cannot reveal
  its inputs through path application. Type arguments and family
  parameters are checked at 0.
- Explicit lambda marks must match the expected binder multiplicity.
  Domain family parameters, constructor fields and codata observations
  reject marks that their syntax representation cannot retain.
- Resource stream construction is sealed even when its normal form is
  a lambda produced by a polymorphic builder. The codata guard checks
  every type argument, including the private corecursor's seed type.

### 4.2 Refusal codes (`langc check --json`)

| Code | Refuses |
|---|---|
| `REFUSE_AFFINE_REUSE` | a variable used more times than its multiplicity on one evaluation path: a 1-variable used two times, or a 0-variable in a run-time position (Q-S3-4 (a)) |
| `REFUSE_AFFINE_OMEGA` | a term that uses a 1-variable (a resource value, or a closure that captures one) at an omega position, or a binder of a resource type at omega (Q-S3-3 (a)); a `fold` step is `REFUSE_AFFINE_FIX`, but a `D.corec` step that uses a 1-variable is `REFUSE_AFFINE_OMEGA`, because the generated corecursor binders are omega (O-U5-1 (a)) |
| `REFUSE_AFFINE_FIX` | a `fold` step, or the case of a constructor with a recursive field, that uses a 1-variable (the step runs one time for each layer, so each unfolding would use it again); a `D.corec` step is `REFUSE_AFFINE_OMEGA` (O-U5-1 (a)) |
| `REFUSE_AFFINE_PATH` | a 1-variable free under an interval binder `<i>`, or in `transp`, `hcomp` or `Glue` at multiplicity 1 (both end points would hold it) |
| `REFUSE_SEALED` | a program that builds or opens a `Leaf` or a `Keystore` outside the domain; in S3, a program that unfolds `Keystore` (a `Stream` observation or `D.corec` at `Keystore`, Q-S3-7 (a)); the check refuses a codata line or `D.corec` at each type argument with a resource sort in it (`Leaf` or `Keystore` as a head type, at each origin and multiplicity), not only at a type that converts with `Keystore` (O-U6-1 (a)); from S5, a program use of the `Durable` constructor `commit` (Q-S5-7 (a)) |

Each code has a JSON record: `code`, `span`, `expected`, `found`, `fix`
and `retry` (section 7).

### 4.3 The keystore and the commit

The reading of `Keystore` is `Stream (Option Leaf)`, a cocell comonad: the
head is the current leaf (or `none` when all `2^h` leaves are used) and
the tail advances the position. The stream is affine. A program that
copies it would have two streams that give the same leaves, so the copy is
`REFUSE_AFFINE_REUSE`. This is the EIP-8310 "silent reset" inside one
program.

The surface does not show the stream. A program uses `next`:

```
next ks = commit (position ks + 1) (done (some (leafAt ks, advance ks)))
```

The leaf is in the continuation of the `commit` node. `sign` needs a leaf.
Thus each signature in a `Durable` tree is under the commit of its leaf
index (L3). The Wasm signer runs the tree and calls the host `commit` import
before it runs the continuation.

### 4.4 What carries over from brisk (verdict lines 160..232)

| brisk | leaf-lang |
|---|---|
| Unr below Aff, two points | three multiplicities: Cubical TT needs 0 for types and proofs |
| D1: one multiplicity bit on the arrow | changed (Q-S3-3 (a)): `A -o B` is a Pi with a binder at 1; a closure type has no bit, and the uses of a closure scale its captures |
| D2: an upper bound at the second use of a variable | kept as the rule behind `REFUSE_AFFINE_REUSE` |
| D3: the drop travels as a witness | not needed in v1: a dropped leaf has no destructor (O6: zeroize) |
| probe 3: a syntactic use is not a use | kept: the count follows activations (`twice f` with a captured leaf is refused) |
| records and variants: Aff if a field is Aff; match joins by max | kept (4.1) |
| handlers: `k` at most once | the `Durable` interpreter runs each continuation once |
| effect rows, row polymorphism, spawn, regions, staging | not carried: no bullet needs them |

### 4.5 The limit of the guarantee

The checker sees one program. It cannot see a keystore file that the
operator restores from a backup, or two signer processes on one file. Two
parts close that gap: the EIP-8310 keystore commits the position before it
releases a signature and never resets it; on chain the nonce binding
accepts a leaf only at its nonce. One window stays: while nonce `k` is
pending, two signatures at leaf `k` would let an attacker forge a third
message at nonce `k`. Only the affine signer stops the two signatures.

## 5. Targets

### 5.1 EVM account verifier

- Entry 1, EIP-8141 VERIFY frame: the contract reads the signature hash
  (TXPARAM 0xb0 with the param 0x08) and the nonce (TXPARAM with the
  param 0x01) (Q-S6-2 (a)). It does not read the signature hash from
  calldata: a calldata hash lets a caller use an old signature again
  over a hash that the caller selects. The signature is the raw bytes of
  entry 0 of the transaction `signatures` list: the scheme ARBITRARY
  (0x0) and an empty `msg`, so the signature hash commits only to the
  length of the bytes (Q-S6-5 (a)). SIGPARAM (0xb4, the param 0x03)
  gives the length and SIGDATACOPY (0xb5) copies the bytes to memory.
  The signature is not in calldata, because the signature hash covers
  the frame data. The contract computes `accept pk nonce sigHash sig`.
  When it is true the contract runs APPROVE (0xaa) with the scope 0x3
  (APPROVE_EXECUTION_AND_PAYMENT, "both"), else it reverts. The frame is
  a STATICCALL, so the contract writes no state. `pk` (root, public
  seed, height) is a constant in the code.
- The shim (O2, Q-S6-2 (a), Q-S6-5 (a)) is a build mode of entry 1. It
  changes only the four table sites: the nonce and the signature hash
  come from a 64-byte calldata prefix (bytes 0 to 31 the nonce, bytes 32
  to 63 the signature hash), the signature is the calldata after the
  prefix, and APPROVE becomes a RETURN of the 32-byte word 1. In the two
  builds the core reads the signature from the same memory region.
- Entry 2, ERC-4337 v0.7 `validateUserOp(userOp, userOpHash,
  missingAccountFunds)`: the same `accept` over `userOpHash` with the
  nonce sequence of key 0 (Q-S6-3 (a)). It returns 0 on success. It
  returns 1 (`SIG_VALIDATION_FAILED`) when `accept` is false: a bad
  signature, a wrong root, a wrong nonce sequence or a nonce key that is
  not 0. A wrong signature length, an out-of-range ABI offset or a
  truncated signature also returns 1 (Q-S6-6 (a)). Tuple and signature
  spans are checked before any signature copy, including offset
  overflow. A wrong selector or a caller other than the v0.7 EntryPoint
  (a build constant) reverts. It pays `missingAccountFunds` to the caller
  before the signature checks.
- The opcodes and the TXPARAM fields of EIP-8141 are a draft (O1). The
  writer keeps them in one table.
- The test EVM of the kit has no EIP-8141 (O2). S6 tests the core and
  entry 2 on it, and entry 1 through a shim that maps APPROVE to a return
  code.
- Port: lang-template `hosts/tcc-evm-contract` `src/asm.c` and
  `src/asm.h` only (Q-S6-1 (a)). `langc verifier` (src/evm.c) writes the
  verifier code by hand in C, with no IR.

Gas estimate for one verify (v1 hash of Q6 (a), n = 32, w = 16, so len =
67; h = 16). S6 measures it; these numbers are a plan only:

| Part | Hash calls | Estimate |
|---|---|---|
| WOTS+ chains, average 7.5 steps of 15 | about 503 | about 45k gas (KECCAK256 over 96 bytes, memory, loop) |
| L-tree | 66 | about 7k gas |
| auth path | 16 | about 2k gas |
| H_msg and compares | 1 | under 1k gas |
| calldata, about 2.7 KB | | about 43k gas at 16 gas per byte (EIP-7623 floor can be higher, O10) |
| total | | about 100k gas past the base cost |

### 5.2 Wasm signer and keystore

- `langc signer --height H -o OUT` writes the signer module for one
  height H, 0 to 20 (D23). Another height or a missing argument is USAGE
  (exit 2). `src/wasm.c` writes the module as hand Wasm (MVP, i64), a
  port of the S4 domain algorithms over keccak256 (trusted base, section
  6, Q-S5-31 (a)). The module has no IR.
- The module exports `memory`, `keygen`, `publicKey`, `next` (S5 U6a),
  `sign` and `verify` (S5 U7, Q-S5-9 (a)). A Digest is 32 bytes at an i64
  address in linear memory, a block of four 64-bit words.
- Keccak-f[1600] is code in the module (i64 lanes), so secrets stay in the
  module (Q9). The internal function `keccak256` is a hand port of
  `src/front/keccak.c`.
- Memory (D19): bytes 0 to 4095 are the host I/O area. The host writes the
  three seeds at 0, 32 and 64. `keygen` sets the heap pointer to 4096,
  writes the state block and sets the mark to the heap pointer.
  `publicKey` and `next` set the heap pointer back to the mark.
- The state block (D20) has 19 words: the key id, the next position, the
  signed bound, skSeed, skPrf, pubSeed and the root. keygen sets the mark,
  then it writes the root: tree_hash (skSeed, pubSeed, H, 0), the RFC 8391
  Algorithm 9 root over the leaves of 5.4 (D28, D29). Each tree function
  frees its scratch, so after keygen the heap pointer is at the mark.
- `keygen (id, start, skSeed, skPrf, pubSeed)` traps when start > 2^H
  (D21, Q-S5-4 (a)). `publicKey (ks)` gives a block of 9 words: the root,
  pubSeed and H (D27). `next (ks)` gives 0 (none) at a position i >= 2^H
  and does not commit. Else it calls env.commit (id, i + 1). Then it
  writes i + 1 to the state block and gives i + 1.
- `sign (ks, leaf, m)` signs the Digest at m with the leaf i = leaf - 1
  (Q-S5-38 (a), D31). leaf is a `next` result. sign traps unless the
  signed bound is less than leaf and leaf is at most the next position. It
  writes leaf to the signed bound before it signs, so a second `sign` of
  one leaf traps (L5). It sets the heap pointer to the mark and gives the
  signature block (D32) of 273 + 4H words: the index i, rand, the 67
  WOTS+ values and the H auth nodes (node k at word 273 + 4k), as signAt
  in 5.4.
- `verify (pk, m, sig)` gives 1 or 0 (Q-S5-39 (a), D33). pk is a
  publicKey block. verify gives 0 when pk word 8 is not H, and it traps
  when the signature index is 2^32 or more (D35). It does not set the heap
  pointer to the mark, and it frees its scratch.
- The one host import is `(import "env" "commit" (func (param i64
  i64)))`, the key id and the new position (Q-S5-2 (c), Q-S5-32 (a)). It
  writes the position durably and returns only after the write. Only
  `next` calls it.
- test/run-wasm.mjs `FILE --key DIR` runs the signer module (D24, no new
  harness file). It reads the stdin lines `keygen ID SKSEED SKPRF PUBSEED
  [START]`, `next` (Q-S5-35 (a)), `rootSum` (D30), `sign LEAF M` and
  `verify M` (Q-S5-37 (a)). The import writes the position to DIR/ID.state
  through a temporary file, fsync, atomic rename and directory fsync.
  One exclusive DIR/.signer.lock serializes harness writers. A crash
  leaves the lock in place; recovery removes it only after the writer has
  stopped and the state has been checked. An empty or malformed state
  refuses with REFUSE_HIGH_WATER. IDs and positions must fit u64; the
  import normalizes signed Wasm IDs to their unsigned value.
  Then it writes the EIP-8310 keystore JSON
  DIR/ID.json (Q-S5-3 (a)): `version` 5, `uuid` (Q-S5-36 (a)), `scheme`
  {`name` "leaf-xmss-keccak", `params` {`hash` "keccak256", `n` 32,
  `winternitz_w` 16, `tree_height` h, `hypertree_layers` 1, `encoding`
  "rfc8391-checksum", `index_mode` "counter", `lifetime_leaves` 2^h}},
  `pubkey` {`root`, `pub_seed`} (each 64 lower case hex digits, from the
  publicKey block) and `state` {`authoritative` false, `high_water`,
  `capacity`}. There is no `crypto` block (test seeds in clear, O13).
- The stdin line `rootSum` prints `rootSum N` for the root of the last
  keygen (D30, Q-S5-34 (a)): acc := (7 acc + digit i) mod (2^61 - 1) for
  the 64 base-16 digits of the root, from acc 0. test/wasm/signer4.lang
  gives the same sum in the language (h = 4, the gate seeds), and
  test/gate.sh compares the two.
- The stdin line `sign LEAF M` (LEAF decimal, M 64 hex digits) writes M to
  I/O byte 0, calls sign (ks, LEAF, 0), keeps the signature and prints
  `sigSum N` (D36, Q-S5-37 (a)): the D30 fold continued over the digests
  of the signature (rand, the 67 WOTS+ values, the H auth nodes), from acc
  0. The line `verify M` writes M to I/O byte 0 and the publicKey block of
  the last keygen to I/O byte 128, calls verify (128, 0, sig) and prints
  `verify true` or `verify false`. A trap prints `trap`.
  test/wasm/sign4.lang gives the sums and the verify results at h = 4 for
  the positions 0, 7 and 15 (D37), and test/gate.sh compares them (D38).
  At h = 10 the gate compares only the last line, `verify true` (D39).
- test/ir/tweakflag.lang isolates the trap flag of a Tweak field (D40).
  In the entry `kindFlag` the kind field is `natMul n 4294967296`: at
  n = 2^32 the machine word wraps to 0, under the width check, so only
  the trap flag gives the trap. In the entry `treeFlag` the tree field
  (no width check) is `natMul n 18446744073709551615`. The row joins
  `ir_expect` and the gate Wasm loop.
- The import refuses a position that is not larger than the position in
  DIR/ID.state (REFUSE_HIGH_WATER, docs/DIAGNOSTICS.md). The harness
  prints `refused REFUSE_HIGH_WATER` and exits 1. There is no silent
  reset.
- `langc eval` and the module agree on every example (the kit
  differential test) at h = 4. At h = 10 only the module runs:
  run-wasm.mjs signs and verifies at 3 positions, with no eval compare
  (Q-S5-11 (a), R1).

### 5.3 zkml-lang program

- A C writer prints the statement of `aggregate` as a zkml-lang source
  file (Q-S7-1 (a), Q-S7-4 (a)): a fixed text plus data defs. The fixed
  text has the BabyBear field ops on `Nat` words, the Poseidon2
  permutation with its constants as a def, the 5.4 sponge, the 4 hash
  ops, the chain, the L-tree, the Merkle path, `verify` and the statement
  as an entry. The data defs have the public keys, the messages and the
  signatures (`langc eval` values).
- `langc zkml` reads each ARG as a Nat: digits only, at most 19. An ARG
  of another type (a Bool) gets a USAGE refusal (Q-S7-15 (a)).
- The output is not a `Circuit` term: the zkml-lang `Circuit` IR has no
  hash op, no gate that multiplies two witness values and no equality
  constraint. zkml-lang does not change. zkml-lang `langc check` and
  `langc build` accept the file, and the Wasm run gives the value (L7).
  There is no prover export (O26). `PIN` records the zkml-lang commit
  (968dce4 at S0).
- Keccak in a circuit is too costly, so this target uses the Poseidon2
  binding over BabyBear (Q10 (a)). zkml-lang has BabyBear and BN254 and
  no Poseidon2 op. leanSig uses KoalaBear.
- Port: zkpolicy-lang `src/zkml.c`, `src/zkml.h`, `src/zkml-names.h`.

### 5.4 Hash bindings and parameters

| Op | `langc eval` | EVM | Wasm | zkml |
|---|---|---|---|---|
| `thChain`, `thNode`, `thMsg`, `prf` | Keccak-256 (`keccak.c`); Poseidon2 by a flag (S7) | Keccak-256 (KECCAK256, 0x20) | Keccak-256 (code in the module) | Poseidon2 (S7) |

v1 construction (Q6 (a)): `th(seed, tweak, x) = Keccak-256(seed || tweak ||
x)`, one call per step (the SPHINCS+ "simple" shape). v1 parameters (Q7
(a)): n = 32, log2 w = 4, h a parameter; the gate uses h = 4 and h = 10;
the default for an account is h = 16 (65536 transactions).

v1 byte layout (Q-S4-4 (a)). The tweak is the RFC 8391 ADRS: 32 bytes, 8
big-endian 32-bit words: layer, tree (2 words), kind, leaf, chain, step,
0. A field above its width traps. `toByte(v, 32)` is `v` as 32 big-endian
bytes. A digest value `digest w0 w1 w2 w3` is 32 bytes: each word gives 8
big-endian bytes, `w0` first (Q-S4-2 (a)).

- `thChain`: Keccak-256(`pubSeed || ADRS || x`), 96 bytes.
- `thNode`: Keccak-256(`pubSeed || ADRS || l || r`), 128 bytes.
- `prf`: Keccak-256(`toByte(3, 32) || key || ADRS`), 96 bytes.
- `thMsg`: Keccak-256(`toByte(2, 32) || r || root || toByte(index, 32) ||
  m`), 160 bytes.

Only `prf` and `thMsg` have the RFC 8391 prefix. The EVM verifier calls
`thMsg` one time and does not call `prf`, so the chain steps stay at 96
bytes and the 5.1 gas plan does not change.

Hash binding (Q-S4-3 (a)). The hash ops compute only in `langc eval` (a
Machine flag that `langc eval` sets). In `check` and in conversion they
stay stuck, also on `digest` values, so no law can use a Keccak output
(L6). `digestEq` and `digit` are not hash ops: they compute on `digest`
values everywhere.

Poseidon2 binding (S7; Q10 (a), Q-S7-2 (a), Q-S7-3 (a), Q-S7-5 (a)). The
permutation is `Poseidon2BabyBear<16>` of Plonky3 rev 5fcf8d7c: p =
0x78000001 (2^31 - 2^27 + 1), width 16, S-box x^7, 4 + 4 external rounds
and 13 internal rounds, and the Horizen Labs constants
(`default_babybear_poseidon2_16()`, baby-bear/src/poseidon2.rs:60-128).
`src/front/poseidon2.c` has it in C. The sponge is
`PaddingFreeSponge<Perm, 16, 8, 8>`: rate 8, overwrite mode, and the
output is the first 8 state elements. Each 32-byte part of the layout
above is 16 big-endian 16-bit limbs. Each limb is below p, so the map is
injective. Each op has a fixed length, so there is no padding, and the
prefixes above give the domain separation. Thus `thChain` and `prf` use
6 permutations, `thNode` 8 and `thMsg` 10. The output e0..e7 (each below
2^31) packs as `w_j = e_{2j} * 2^32 + e_{2j+1}`, `w0` first.
`langc eval --hash poseidon2` selects the binding, and Keccak is the
default (`--hash keccak` or no flag; Q-S7-7). The EVM verifier and the
Wasm signer stay Keccak (O27). The Q-S4-3 (a) rule holds for both
bindings (L6).

Known answers (test/fronttool.c): (ii) the width-16 vector of Plonky3
baby-bear/src/poseidon2.rs:315-333 (the constants come from
`Xoroshiro128Plus` seed 1), and (i) two vectors of the Horizen Labs
instance from a cargo tool at the same rev (on 0..15 and on the (ii)
input). The sponge and op vectors come from the same cargo tool (S7).

## 6. Laws (guest terms)

| Law | Statement | Slice |
|---|---|---|
| L1 correctness | for each keystore `ks` and message `m`, when `next ks` gives `(l, ks')`, `verifyAt (leafHeight l) (pk ks) m (sign l m)` is `true` (Q-S5-12 (a)) | S4 (U6: an eval check at h = 4 on the guest form `verify pk4 m (signAt sk skPrf pk4 i m)` for i = 0 to 15, test/eval/laws.lang), S5 (the keystore form: an eval check at h = 4 for the positions 0 to 15, and `next` gives `none` at 16, Q-S5-10 (a)) |
| L2 nonce binding | `accept pk k m s` is `true` implies `index s` is `k` | S6 (U3: an eval check at h = 4 on the guest form, test/eval/laws.lang: for i = 0 to 15, `accept pk4 i m (signAt sk skPrf pk4 i m)` is `true` and `accept pk4 (i + 1) m (signAt sk skPrf pk4 i m)` is `false`; at the nonce of the signature, a different `m` gives `false`, D5) |
| L3 commit-before-sign | in each `Durable` tree, each `sign` of a leaf with index `i` is under a `commit` node that writes `i + 1` | S5 (an eval row at h = 4: a guest fold checks that each `some (l, ks')` is under `commit (i + 1)` and that `index (sign l m)` is `i`, Q-S5-10 (a)) |
| L4 no reset | the committed positions on each path of a `Durable` tree increase strictly | S5 (by construction: only `next` makes a `commit` node, Q-S5-7 (a)) |
| L5 distinct leaves | two signatures that one program makes have different indices (from the affine check and L4) | S5 (a refusal fixture: two `sign` of one leaf and two `next` of one keystore are `REFUSE_AFFINE_REUSE`, "reset refused"; an eval row: two signatures of one program have different indices, Q-S5-10 (a)) |
| L6 binding independence | the kernel never unfolds a hash op, so each law that checks holds for each binding | S4 (U6: test/check/hash-node.lang, hash-root.lang and hash-verify.lang refuse with `TYPE_REFL`, because the hash ops stay stuck in check) |
| L7 aggregation | the zkml statement holds exactly when `aggregate` is `true` (at the zkml-lang semantics) | S7 |

Outside the denotation (assumptions, not laws): EUF-CMA security of XMSS
under the second-preimage and PRF properties of the hash; the quality of
the host seeds; side channels; the durability of the host write; the
EIP-8141 opcode semantics; the C writers and the Keccak code (trusted
base, tested against vectors).

## 7. Agent adoption

Each bullet of the pasted "Agent Incentives" brief, with its deliverable,
slice and gate, or NOT DOING and the reason.

| Brief bullet | Deliverable | Slice | Gate |
|---|---|---|---|
| 1. Canonical, constrained surface | Agda-like Unicode grammar (Q14 (b), 7.1), one canonical form per operation; `langc fmt` canonical printer; `W_NONCANON` with a fix for non-canonical input (ASCII spellings included). A named step is a top-level definition (signature plus clause); there is no `step NAME INTO`. A `let` form is O15 (open) | S1 | `fmt` is idempotent and round-trips on each example and on `test/fmt/`; each example is canonical |
| 1. Fast, structured feedback | `langc check --json`: `code`, `span`, `expected`, `found`, `fix`, `retry`; `docs/DIAGNOSTICS.md` | S1, S3 | each refusal code has a test that checks its JSON shape |
| 1. LLM-friendly surface, mechanical backend (3 to 5 examples, instructions, MCP, validator in the loop) | `skills/leaf-lang/SKILL.md`, a grammar card, 5 graded examples, `AGENTS.md`, `langc mcp` | S9 | the MCP test drives `check` on each example by stdio |
| 2. Denotation per program, machine-checkable | `langc check` discharges L1 to L7 for each program that uses them; `langc laws --json` | S4 to S7 | progress counts match the law table |
| 2. Compositional denotations | R1 (CM and CC only); `Durable` and `Stream`; `langc explain --json` (type, multiplicities, leaves used) | S2, S3, S9 | explain output tests |
| 2. Multi-target compilation | EVM, Wasm and zkml writers (R4); each output has a manifest (source hash, langc commit, laws discharged) | S5 to S8 | rebuild gives the same bytes |
| 2. Preference as a fixed point (DAO) | NOT DOING: no DAO in R2 | | |
| 3. Exclusion at the boundary | the signer service takes a signing request only as a leaf-lang program that passes `check` with the affine check; a raw sign call is refused. An account deploys only bytecode that `langc` rebuilds byte for byte from the source in its manifest | S8 | a raw request and a program with `REFUSE_AFFINE_REUSE` are refused; the rebuild compare passes |
| 3. Soft economic incentives (fees, collateral) | NOT DOING: no fee bullet in R2; a marketplace operator sets fees | | |
| 3. Token efficiency | `docs/TOKEN-BASELINE.md`: one task (keygen, sign, verify, nonce binding) in leaf-lang and in Solidity plus Rust, counted by a fixed offline proxy (Q13) | S9 | the counts are in the file with the method |
| 3. Bounties and grants | USER | | |
| 3. Reward shaping | the progress counts in `check --json`: parsed, typed, affine-checked, laws discharged n of m | S1, S3 | counts in each JSON result |
| 4. Agent Skill or MCP server | `SKILL.md` and `langc mcp` (tools: `check`, `fmt`, `explain`, `list-base`, `show-law`), an MCP stdio server in C that tcc builds | S9 | MCP stdio test |
| 4. Small fine-tuned model (< 500 MB) | USER; the repo gives the corpus (`examples/`, the graded examples, the diagnostics) | | |
| 4. Reference implementation, grammar and corpus, catalog listing | the repo, the grammar card, the corpus; the agentlanguages.dev listing is a USER step | S9 | |
| Routing | `AGENTS.md` routing rule and the README table "When to reach for leaf-lang" | S0, S9 | |

### 7.1 Surface spellings (Q14 (b))

The plan text keeps the design names (`Nat`, `->`, `-o`, `*`). This table
maps them to the surface of SPEC.md section 2.3. The parser reads each
ASCII alias; `langc check` gives `W_NONCANON` with the canonical text as
`fix` (O14); `langc fmt` prints only the canonical spelling.

| Design name | Canonical spelling | ASCII alias |
|---|---|---|
| Pi `(x : A) -> B`, `A -> B` | `(x : A) → B`, `A → B` | `->` for `→` |
| Lambda | `λ x → t`, `λ (x : A) → t` | `\` for `λ`; `fun x => t` |
| Sigma `(x : A) * B`, `A * B` | `(x : A) × B`, `A × B` | `*` for `×`; `Sigma (x : A) B` |
| `Nat` | `ℕ` | `Nat` |
| Universe `Type n` | `Type n` | none |
| `A -o B` (S3) | `A ⊸ B` (U+22B8), read as `(@1 _ : A) → B`; `fmt` prints `⊸` when the binder name is not used | `-o` for `⊸` (Q-S3-2 (a)) |
| binder multiplicity (S3) | `(@0 x : A)`, `(@1 x : A)`; a binder with no mark is omega (Q-S3-1 (a)) | none |
| `Path` (S2) | not in S1 | not in S1 |

The core names do not change: `ℕ` and `Nat` give the one core name `Nat`.
SPEC.md section 2.1 gives the declaration forms (signature and clause,
`rec`, `postulate`, `data ... where`) and the forms that `check` refuses.

## 8. Port sources (pinned 2026-10-25)

| Repo | SHA | What |
|---|---|---|
| lang-template | 5265a2a | `hosts/tcc-wasm` (the seed); `hosts/tcc-evm-contract` `src/{asm,evm,keccak}.c` and test harness |
| intent-lang | 79e1898 | `src/front/check.c` REFUSE_CELL, cofamily, sealed family; PLAN 2.2 |
| resolve-lang | 251c1ca | `src/front/check.c` REFUSE_CELL (second implementation) |
| zkpolicy-lang | 1d263fb | kernel with HITs, Glue and `ua` (`src/front/{check,eval,lower}.c`); zkml writer `src/zkml.c` |
| zkml-lang | 968dce4 | the zkml-lang program target (5.3) |
| agent-lang | 06854bf | `src/front/parser.c` JSON diagnostics |
| dil-lang | 9e57528 | `src/front/{parser,check}.c` JSON diagnostics |
| brisk | 24e3383 | the affine design (verdict lines 160..232) |
| mandate-lang | 0fb3760 | PLAN shape; 4.6 the free cell monad of operations |

## 9. Slices

One session each. Each slice: scope, gate (`make check`, EXIT 0), mutants
on the new checks, docs, close (stage, commit message, next prompt).

| Slice | Scope | Gate and mutants |
|---|---|---|
| S0 | kit seed, `design/DESIGN.md`, this plan, README, `AGENTS.md` | `make check` EXIT 0: 13 Wasm modules valid, differential 813 cases 0 mismatches, gate 0 failures, review and domain regressions passed |
| S1 | surface: Agda-like Unicode (Q14 (b), 7.1), named steps as top-level definitions, `langc fmt`, `W_NONCANON`; `check --json` with progress counts; drop the refused kit names | `make check` EXIT 0: examples 10 checked, 0 failed; parse refusals 9; check refusals 18; evals 30; ir 10 lowered; wasm 13 modules valid; differential 772 cases, 0 mismatches; fmt 50 canonical, 3 non-canonical pairs (round trip and idempotence); json 20 fixtures, diagnostics 31 codes (18 with a fixture, 13 with no fixture); gate 0 failures; review and domain regressions passed; mutants M1 to M6 killed (docs/STATUS.md, S1) |
| S2 | kernel port: Path, Sigma, Pi, U, `I`, `transp`, `hcomp`, `Glue` and `ua` (S2-Q1 (a)), O18 (kit Ops removed, U2), no `record` form (S2-Q3 (b)), `Sum` as a domain CM (S2-Q2 (b), done in U6b2), `REFUSE_CELL`, cofamily (`codata` keyword and shape check done in U6c1), `Fin` (domain defs `lt` and `Fin`, done in U7) | `make check` EXIT 0: examples 10 checked, 0 failed; parse refusals 10; check refusals 32; evals 64; ir 10 lowered; wasm 13 modules valid; differential 773 cases, 0 mismatches; fmt 78 canonical, 5 non-canonical pairs (round trip and idempotence); json 28 fixtures, diagnostics 41 codes (26 with a fixture, 15 with no fixture); gate 0 failures; review and domain regressions passed; `REFUSE_CELL` mutants C1, C1w, C2, C3 and mutants U6c2b, U7-M1 killed (docs/STATUS.md, S2) |
| S3 | affine layer: multiplicities, `-o`, resource sorts, sealed `Leaf` and `Keystore`, `Durable` (a spine: one recursive field in each constructor, O-U6b-1 (a)), the five refusal codes | `make check` EXIT 0: examples 10 checked, 0 failed; parse refusals 12; check refusals 35; evals 70; ir 10 lowered; wasm 13 modules valid; differential 773 cases, 0 mismatches; fmt 91 canonical, 6 non-canonical pairs (round trip and idempotence); json 33 fixtures, diagnostics 46 codes (31 with a fixture, 15 with no fixture); gate 0 failures; review and domain regressions passed; one JSON fixture for each S3 code; 14 mutants on the five S3 refusals and the two `Durable` branches, 14 of 14 killed (docs/STATUS.md, S3) |
| S4 | `Digest`, hash ops (opaque), WOTS+, L-tree, Merkle, `sign`, `verify`; reference Keccak in `langc eval`; L1, L6 | `make check` EXIT 0: examples 10 checked, 0 failed; parse refusals 12; check refusals 38; evals 142; ir 10 lowered; wasm 13 modules valid; differential 773 cases, 0 mismatches; fmt 101 canonical, 6 non-canonical pairs (round trip and idempotence); json 33 fixtures, diagnostics 46 codes (31 with a fixture, 15 with no fixture); gate 0 failures; review and domain regressions passed; own vectors plus a cross-check of each hash op with `cast keccak` (Q-S4-1 (c)); L1 at h = 4 for i = 0 to 15 (test/eval/laws.lang); L6: three `check` rows refuse with `TYPE_REFL`; 10 mutants on the S4 checks, 10 of 10 killed (docs/STATUS.md, S4) |
| S5 | `keygen`, `publicKey`, `next`, `commit`; Wasm signer (Keccak in the module, `commit` import, EIP-8310 JSON in the harness); L3 to L5 | `make check` EXIT 0: examples 10 checked, 0 failed; parse refusals 12; check refusals 43; evals 162; ir 14 lowered; wasm 19 modules valid; differential 951 cases, 0 mismatches; signer 2 modules valid, 28 rows; fmt 115 canonical, 6 non-canonical pairs (round trip and idempotence); json 33 fixtures, diagnostics 49 codes (31 with a fixture, 18 with no fixture); gate 0 failures; review and domain regressions passed; Keccak known answers in the module (test/ir/hashkat.lang); the h = 4 signer against `langc eval` (`rootSum`, `sigSum` and `verify` at the positions 0, 7 and 15); reset refused (`REFUSE_HIGH_WATER`); at h = 10 the module signs and verifies; L1 (keystore form), L3 to L5 as eval rows at h = 4 and refusal fixtures (Q-S5-10); 11 mutants on src/wasm.c, 10 killed, M6 height a known survivor (Q-S5-40 (a)) (docs/STATUS.md, S5) |
| S6 | EVM verifier: ERC-4337 entry, EIP-8141 entry with the shim, nonce binding L2; measured gas | `make check` EXIT 0: examples 10 checked, 0 failed; parse refusals 12; check refusals 43; evals 195; ir 14 lowered; wasm 19 modules valid; differential 951 cases, 0 mismatches; signer 2 modules valid, 95 rows; verifier ABI regressions 9 passed; fmt 115 canonical, 6 non-canonical pairs (round trip and idempotence); json 33 fixtures, diagnostics 49 codes (31 with a fixture, 18 with no fixture); gate 0 failures; review and domain regressions passed; `langc verifier` at h = 4 in `evm run`: the shim and entry 2 accept the 16 positions; a wrong nonce, a wrong message, a wrong root or a changed byte is refused (the shim reverts, entry 2 gives the word 1); the shim build is the real entry 1 build with the 5 table parts in the shim form (a byte compare; no EVM with EIP-8141, O2); L2 as the `accept` eval rows; `make gas` rows at H = 4, 10 and 16 (shim and entry 2; not in `make check`, Q-S6-8 (b)); 8 mutants on src/evm.c, 8 of 8 killed (docs/STATUS.md, S6) |
| S7 | Poseidon2 binding (field of Q10) in `langc eval` and the zkml writer; `aggregate`; L7 | `make check` EXIT 0: examples 10 checked, 0 failed; parse refusals 12; check refusals 44; evals 205; poseidon2 evals 7 (the L1 rows at h = 4, L7 at N = 1, the `--hash sha3` USAGE row); zkml 1 checked (zkml-lang accepts the `langc zkml` output of the L7 entry at N = 1); zkml known answers 6 evals (Q-S7-14 (a)); ir 14 lowered; wasm 19 modules valid; differential 951 cases, 0 mismatches; signer 2 modules valid, 95 rows; verifier ABI regressions 9 passed; fmt 116 canonical, 6 non-canonical pairs (round trip and idempotence); json 33 fixtures, diagnostics 49 codes (31 with a fixture, 18 with no fixture); gate 0 failures; review, signer persistence and domain regressions passed; fronttool: the Poseidon2 vectors of Plonky3 (the (ii) instance at seed 1, the (i) instance on 0..15 and on the (ii) input) and the 5.4 sponge digests on 96, 128 and 160 bytes; 12 mutants on zkml/xmss.lang, src/zkml.c, src/front/poseidon2.c and src/main.c, 12 of 12 killed, 2 of them by a data word check out of `make check` (docs/STATUS.md, S7) |
| S8 | boundary exclusion: the signer service takes programs only; output manifests; rebuild compare | refusal tests; byte-identical rebuild |
| S9 | agent kit: `SKILL.md`, grammar card, 5 graded examples, `langc mcp`, `langc explain`, `TOKEN-BASELINE.md`, final `AGENTS.md` | MCP stdio test; examples pass `check` |
| S10 | close: `docs/STATUS.md`, `docs/VALIDATION.md`, full gate, mutant summary | full gate EXIT 0 |

## 10. Open items

- O1: EIP-8141 is a draft (APPROVE 0xaa, TXPARAM 0xb0, SIGPARAM 0xb4,
  SIGDATACOPY 0xb5, the signature hash). The writer keeps the opcodes in
  one table. Each slice that touches the EVM entry checks the spec again
  (last check: S6 U4, 2026-10-28).
- O2: the kit EVM harness has no EIP-8141. Entry 1 is tested through a
  shim until an EVM with EIP-8141 exists.
- O3: CLOSED (Q10 (a), 2026-10-28). The Poseidon2 field is BabyBear.
  zkml-lang has BabyBear and BN254. leanSig uses KoalaBear.
- O4: XMSS^MT or generalized XMSS for the leanSig lifetime 2^32.
- O5: two signers on one key (EIP-8310 reserved leaf ranges, an affine
  `split`). Q12.
- O6: zeroize the secrets of a dropped leaf in the Wasm module.
- O7: `Digest` as a base type or as `Fin n -> Byte`.
- O8: the targets lower the `Nat` builtins (`natDiv`, `natMod` and the
  others, Q-S4-5 (a)) to machine words. The lowering is trusted, as
  zkml-lang `natDiv`.
- O9: `REFUSE_AFFINE_PATH` can be too strict for a path whose end points
  are equal by definition. S3 records each refused case it meets.
- O10: calldata of about 2.7 KB per signature against the EIP-7623 floor.
  S6 measured it (h = 16, the shim, means of 16 positions): 2760
  bytes, standard 43600.5 gas, floor 109001.2 gas. Standard +
  execution (139645.3) is more than the floor, so the transaction does
  not pay the floor. The floor applies if the execution gas goes below
  65400.7 (the floor minus standard).
- O11: the leanSig target-sum encoding against the RFC 8391 checksum.
- O12: reuse across runs (a restored backup) is outside the checker
  (section 4.5).
- O13: seed entropy comes from the host.
- O14 (Claude reading, not ruled): an ASCII alias parses and gives
  `W_NONCANON` with the Unicode form as `fix`; `fmt` prints only the
  Unicode form. Unicode can cost more tokens; the S9 baseline (Q13)
  measures it.
- O15: a `let` form under Q14 (b). Open.
- O16: CLOSED (S2-Q2 (b), 2026-10-25). `Sum` stays as a CM in
  `domain/domain.lang` (section 1). `Either` is not admitted (2.2).
- O17: `flagIf` keeps its kit name (it is not in the S1 drop list). The
  USER rules the name of `if`.
- O18: dead core paths. The Op values and the eval and lowering code for
  `List`, `Prod`, `filter` and `unfold` stay, but no program reaches them.
  S2 removes them.
- O19: the value printer and some check messages use the ASCII kit
  spelling (`->`, `fun x =>`, `Sigma (x : A) B`, `Nat`). The parser reads
  it back through the aliases.
- O20: the infix path type `x ≡ y` (the Cubical Agda spelling) waits for
  implicit arguments. Until then `Path A x y` is the only spelling
  (S2-Q1 (a)).
- O21: the real entry 1 build has no `evm run` row and no gas row
  (D19.4). evm 1.14.12 has no TXPARAM, SIGPARAM, SIGDATACOPY or
  APPROVE (O2). The gate checks that the shim build is the real build
  with the 5 table parts in the shim form (a byte compare). An EVM
  with EIP-8141 closes it.
- O22: the measured execution gas is more than the 5.1 estimate. At
  h = 16 the shim mean is 96044.8 gas, approximately 1.7 times the
  estimate of about 55k (docs/STATUS.md, S6 gas rows).
- O23: an `execute` entry for the ERC-4337 account. The S6 account
  can validate a UserOperation, but it cannot act (D12).
- O24: the EIP-8141 SENDER frame. Entry 1 has only the VERIFY frame
  (D12).
- O25: deployment of the verifier code (D12).
- O26: the zkml output is a zkml-lang program, not a `Circuit` term, and
  it has no prover export (Q-S7-1 (a), 5.3). A proof of `aggregate` needs
  a hash op in the zkml-lang `Circuit` IR or a prover for the program.
- O27: the Wasm signer has no Poseidon2. In v1 only `langc eval` makes a
  signature under the Poseidon2 binding, for aggregation (5.4).
- O28: the Z arena (1 GiB for a run, not released between defs) holds
  about 5 to 9 Poseidon2 hashes. `langc eval` and `langc build` of the
  zkml entry run out of memory, so the zkml output has a Z check only
  (Q-S7-13 (a)). A fix needs a Z change (Q-S7-1 (a)). The measures: a
  Z eval of the D10 form fits 2 chain steps and a root of h = 2, and it
  does not fit 5 steps and a root of h = 4 (Session 59). The 6 Z
  known-answer evals of the gate (Q-S7-14 (a)) use 569 to 871 MiB max
  RSS (Session 63).

## 11. Questions for the USER

Each item has options. (a) is RECOMMENDED. Marks (2026-10-25): RULED
(letter) is a USER ruling; DEFAULT (a) means (a) by default, the USER did
not rule it (section 1).

- Q1 R1 reading. RULED (a). (a) the intent-lang ruled reading (2.1):
  ambient Pi, Sigma, Path, U, Glue are not limited; declared type
  constructors are only CM and CC; the cell criterion is syntactic; cons
  is refused. (b) the same without Glue and `ua`.
- Q2 kit. DEFAULT (a). (a) tcc-wasm seed; EVM writer from
  tcc-evm-contract; zkml writer from zkpolicy-lang. (b) tcc-evm seed and a
  Wasm port.
- Q3 affine discipline. RULED (a). (a) QTT multiplicities 0, 1, omega;
  affine (weakening allowed); resource sorts `Leaf` and `Keystore` only.
  (b) linear: a leaf must be used or burned on purpose. (c) uniqueness
  types.
- Q4 keystore. DEFAULT (a). (a) the reading `Stream (Option Leaf)`, a
  cocell comonad, affine, with `next` in `Durable`. (b) an opaque base
  type with `next`, no comonad reading.
- Q5 commit-before-sign. DEFAULT (a). (a) by data dependency: the leaf
  comes out of the continuation of `commit`. (b) a law checked on traces,
  with a free `sign`.
- Q6 v1 tweakable hash. RULED (a). (a) `Keccak-256(seed || tweak || x)`,
  one call per step; own vectors and an independent cross-check. (b) the
  RFC 8391 bitmask construction with Keccak, three calls per step.
  (c) RFC 8391 with SHA-256 exactly (official vectors; outside R5).
- Q7 v1 parameters. DEFAULT (a). (a) n = 32, w = 16, h a parameter; gate
  at h = 4 and 10; account default h = 16. (b) h = 10 fixed. (c) h = 20.
- Q8 on-chain one-time. DEFAULT (a). (a) leaf index = account nonce.
  (b) a used-leaf bitmap in storage (ERC-4337 only; a VERIFY frame cannot
  write).
- Q9 Wasm crypto. DEFAULT (a). (a) Keccak in the module; `commit` is the
  one host import. (b) a host-import crypto ABI.
- Q10 zkml field. RULED (a). (a) Poseidon2 over BabyBear (zkml-lang has
  it). (b) add KoalaBear to zkml-lang first (leanSig parameters).
- Q11 proofs. DEFAULT (a). (a) guest only; no Lean export. (b) a Lean
  export in a later slice.
- Q12 two signers. DEFAULT (a). (a) not in v1 (O5). (b) admit an affine
  `split` in S5.
- Q13 token counts. DEFAULT (a). (a) a fixed offline proxy. (b) API
  counts (USER refresh).
- Q14 surface. RULED (b). (a) ASCII verbose keywords with
  `step NAME INTO`. (b) Agda-like Unicode (section 7.1).

S3 questions (S3-HANDOFF section 3). Marks (2026-10-26, S3 U1): the USER
ruled each one.

- Q-S3-1 binder multiplicity spelling. RULED (a). (a) Agda style:
  `(@0 x : A) → B` and `(@1 x : A) → B`; no mark is omega. (b) Idris 2
  style: `(0 x : A) → B`, `(1 x : A) → B`. (c) (a) and a mark `@ω` for an
  explicit omega.
- Q-S3-2 affine arrow spelling. RULED (a). (a) `A ⊸ B` (U+22B8) is
  canonical and means `(@1 _ : A) → B`; the ASCII alias `-o` parses with
  `W_NONCANON` (O14); fmt prints `A ⊸ B` when the binder name is not used.
  (b) only `-o`. (c) no arrow sugar: `(@1 x : A) → B` only.
- Q-S3-3 reading of `⊸`. RULED (a). (a) QTT (Q3 (a), Atkey 2018): `⊸` is
  a binder at 1; a closure type has no bit, because the uses of the
  closure scale its captures; `A ⊸ B` and `A → B` do not convert
  (`TYPE_MISMATCH`, no subsumption in v1). (b) brisk D1: an `A ⊸ B` value
  is an affine value, and the binder multiplicity is separate. (c) (a)
  with the subsumption `A ⊸ B` below `A → B`.
- Q-S3-4 a 0-variable in a run-time position. RULED (a).
  (a) `REFUSE_AFFINE_REUSE` (used more than its multiplicity; the message
  gives "used 1, allowed 0"). (b) a sixth code `REFUSE_AFFINE_ERASED`.
- Q-S3-5 definitions checked at 0. RULED (a). (a) a definition whose type
  ends in `Path`, `PathP` or a universe (a law, a proof or a type) is
  checked at 0; each other definition at 1. (b) an explicit `@0` on the
  signature name.
- Q-S3-6 affine pair split. RULED (a). (a) a new rule
  `unpack p (λ a b → t)`: `p` is used one time, `a` and `b` are bound at 1
  (the name pairs with `pack`). (b) no split in S3; S5 adds it with
  `next`.
- Q-S3-7 `Leaf`, `Keystore` and `REFUSE_SEALED`. RULED (a). (a) `Leaf` is
  a core base type with no constructor and no eliminator; `Stream` is a
  codata in `domain/domain.lang` (`head`, `tail`); `Keystore` is a sealed
  domain definition `Stream (Option Leaf)` (Q4 (a)); a program that
  unfolds `Keystore` gets `REFUSE_SEALED` (fixture `peek`). (b) both core
  base types; `REFUSE_SEALED` has no program fixture in S3, and a
  fronttool regression tests it. (c) both core base types and three sealed
  core ops `leafAt`, `advance`, `position` (4.3) that only the domain can
  call.
- Q-S3-8 `Durable` in `domain/domain.lang`. RULED (a).
  (a) `data Durable (A : Type 0) : Type where`, `done : (doneOf : A) →
  Durable A`, `commit : (at : ℕ) → (rest : Durable A) → Durable A`; it
  meets the cell criterion; `commit` is public in S3, and S5 decides if a
  program can build a `commit` node. (b) the same with other field names.
- Q-S3-9 progress field `affine`. RULED (a). (a) `"k of m"`: the
  declarations that pass the use count before the first affine error, as
  `typed`; the 28 json expect lines change. (b) count only the
  declarations that have a multiplicity mark or a resource type.
- Q-S3-10 the name of the `Durable` continuation field. RULED (a)
  (USER, 2026-10-26, U6b). `rest` is REFUSE_NAME in the real domain (rule
  R4: `Stack` has a field `rest`). (a) `cont`, the continuation of the
  commit node (section 4.3). (b) keep `rest` for `Durable` and rename the
  field of `Stack` to `below`. (c) `commitRest`.
- O-U5-1 the code of a `D.corec` step that uses a 1-variable. RULED (a)
  (USER, 2026-10-26, U8). (a) `REFUSE_AFFINE_OMEGA`: the generated
  corecursor binders are omega, and R2 keeps `REFUSE_AFFINE_FIX` out of
  the generic application (section 4.2). (b) mark the generated
  corecursor and give `REFUSE_AFFINE_FIX`.
- O-U6-1 the types that `REFUSE_SEALED` refuses. RULED (a) (USER,
  2026-10-26, U8). (a) each type argument with a resource sort in it, as
  a head type, at each origin and multiplicity (section 4.2). (b) only a
  type that converts with `Keystore`.
- O-U6b-1 trees at the own parameters. RULED (a) (USER, 2026-10-26, U8).
  (a) a family with type parameters has at most one recursive field in
  each constructor (a spine, as `Durable`; else `DOMAIN_FAMILY`); trees
  at the own parameters are a later slice item (docs/STATUS.md, S3).
  (b) admit trees in S3.

S4 questions (S4-HANDOFF section 3). Marks (2026-10-26, S4 sessions 1
and 2): the USER ruled each one. For Q-S4-0 and Q-S4-1 (session 1) this
block keeps only the ruled option.

- Q-S4-0 S4 base. RULED (a). (a) build on the USER commit 47a6f36 (tree
  71ded360: S3 and the USER "Staged S3 review fixes").
- Q-S4-1 cross-check. RULED (c). (c) Foundry `cast keccak` only, for the
  single hash calls `thChain`, `thNode`, `thMsg` and `prf`; WOTS+, L-tree
  and Merkle use own vectors only.
- Q-S4-2 digest values. RULED (a). (a) a builtin `digest : ℕ → ℕ → ℕ → ℕ →
  Digest` (four 64-bit big-endian words, w0 = bytes 0..7); a closed hash
  result is a `digest` value; the printer prints `digest w0 w1 w2 w3`; no
  lexer change. (b) a hex literal token `0x` and 64 hex digits (lexer,
  parser, fmt, printer). (c) (a) and (b).
- Q-S4-3 where the hash binds. RULED (a). (a) the hash ops compute only in
  `langc eval` (a Machine flag that `langc eval` sets); in `check` and in
  conversion they stay stuck, also on `digest` values (L6: no law can use
  a Keccak output); `digestEq` and `digit` are not hash ops and compute on
  `digest` values everywhere (5.4). (b) the hash ops compute on literals
  everywhere; L6 holds only for neutral arguments.
- Q-S4-4 byte layout (Q6 (a) `Keccak-256(seed || tweak || x)`). RULED
  (a). (a) the tweak is the RFC 8391 ADRS, 32 bytes, 8 big-endian 32-bit
  words: layer, tree (2 words), kind, leaf, chain, step, 0; a field above
  its width traps. thChain = Keccak(pubSeed || ADRS || x), 96 bytes;
  thNode = Keccak(pubSeed || ADRS || l || r), 128 bytes; prf =
  Keccak(toByte(3, 32) || key || ADRS), 96 bytes; thMsg = Keccak(toByte(2,
  32) || r || root || toByte(index, 32) || m), 160 bytes (5.4). (b) the
  same with no prefixes. (c) RFC 8391 prefixes on each call (a chain step
  is 128 bytes; this changes the 5.1 gas plan).
- Q-S4-5 Nat operations. RULED (a). (a) keep the builtins `natAdd`,
  `natSub`, `natMul`, `natEq`, `natLe` and the domain `lt`; add the
  builtins `natDiv` and `natMod` (RULE_NAT2; division by 0 traps, as
  overflow does; O8); the 3.2 row has these names. (b) domain definitions
  with the names `add`, `sub`, `mul`, `div`, `mod` (by fold; slow; `natEq`
  stays a builtin).
- Q-S4-6 signing in S4 (no Leaf source before S5 `next`). RULED (a). (a)
  domain definitions `pkFromSeeds : (skSeed : Digest) → (pubSeed : Digest)
  → (p : Params) → PubKey` and `signAt : (skSeed : Digest) → (skPrf :
  Digest) → (pk : PubKey) → (i : ℕ) → (m : Digest) → Sig (height pk)` (the
  index is from Q-S4-8 (a)); S5 `sign : Leaf ⊸ Digest → Sig` calls
  `signAt` through the sealed `Leaf` eliminator, and S5 `keygen` uses
  `pkFromSeeds`. (b) the RFC 8391 names `xmssPk` and `treeSig`. (c) no S4
  signer; S4 tests `verify` on vectors only.
- Q-S4-7 L1 at h = 4. RULED (a). (a) L1 is an eval check at the Keccak
  binding: a guest entry runs `verify (pkFromSeeds ..) m (signAt .. i m)`
  for each of the 16 indices and three messages and gives 1; negative rows
  (wrong index, wrong message, wrong root, one flipped auth node) give 0.
  L6 evidence: guest laws at concrete digits with neutral seeds (chain
  composition `chain (chain x 0 a) a b ≡ chain x 0 (a + b)` for literal
  `a`, `b`) check at 0; a check fixture where a Path needs a hash
  unfolding is refused (`TYPE_MISMATCH`). (b) a dependent eliminator for ℕ
  (a new RULE) and a proof of L1 for all messages (a large kernel change;
  the `digit` bound becomes an obligation that the kernel trusts). (c) the
  eval check only.
- Q-S4-8 the parameters in the types. RULED (a). (a) v1 fixes n = 32 and
  log2 w = 4 (len = 67) in the definitions; `Params` keeps the three
  fields `n`, `logW`, `h` for the EIP-8310 JSON, and `verify` gives false
  when `n` or `logW` is not the v1 value; the height is the index of `Sig`
  and of the auth path: `Sig : (h : ℕ) → Type 0` with `wots : Fin 67 →
  Digest` and `auth : Fin h → Digest`; `verify : (pk : PubKey) → Digest →
  Sig (height pk) → Bool`. (b) `Params` holds only `h`. (c) each
  definition has the full `Params` as its index. The S5 rows (`sign` in
  3.2, L1 in section 6) get the index `Sig (height pk)` in S5.
- Q-S4-U2-a the bit order of `digit`. RULED (a) (S4 U2). (a) RFC 8391
  base_w: from byte 0, the most significant bits first.
- Q-S4-U2-b the lowering of the S4 names. RULED (a). (a) the existing
  path: `Digest` and `LogW` go with `Leaf`, and an operation gets "an
  operation with no IR form (O8)"; no new IR code.
- Q-S4-U2-c the print of a Digest result. RULED (a). (a) `digest w0 w1 w2
  w3`.
- Q-S4-U2-d the type of log2 w. RULED (a). (a) a base type `LogW`
  (copyable) with the values `logW1`, `logW2`, `logW4`, `logW8` and
  `logWBits : LogW → ℕ`; `digit : LogW → Digest → ℕ → ℕ`; the `Params`
  field `logW : LogW`. No bad logW exists, so there is no logW trap.
- Q-S4-U3-a the text of a trap (O-U2-1). RULED (a) (S4 U3). (a) the code
  stays `EVAL_OVERFLOW`, and the text is "a Nat operation trapped
  (overflow, division by 0, a position past the digest, a tweak field above
  32 bits)". (b) the old text. (c) a new code `EVAL_TRAP`.
- Q-S4-U3-b the guest form of `Tweak`. RULED (a). (a) the right-nested
  Sigma `(layer : ℕ) × (tree : ℕ) × (kind : ℕ) × (leaf : ℕ) × (chain : ℕ) ×
  ℕ` (the last part is step); `thChain`, `thNode` and `prf` take it, and
  the domain name `Tweak` (U4) converts to it. (b) a core base type
  `Tweak` with a builtin `tweak`.
- Q-S4-U4-a how a fold reads a `Fin`-indexed field. RULED (a) (S4 U4).
  `fold step z n` iterates over ℕ, but its counter is neutral in check, so
  no term makes a `Fin n` element from it (`flagIf` is not dependent).
  (a) a kernel op `boolElim : (P : Bool → Type 0) → P true → P false → (b
  : Bool) → P b` (it computes on `true` and `false`; a neutral `b` stays
  stuck); the domain defines `finAt`, and `Sig` keeps `Fin 67` and `Fin
  h`. (b) `ℕ → Digest` fields in `Sig`. (c) 67 unrolled `wots` cases and
  `auth : ℕ → Digest`.
- Q-S4-U5-a the form of `ltree` and `treeNode`. RULED (a) by the USER
  (S4 U5b, 2026-10-27). (a) fixed nesting. `ltree` is 7 fixed levels
  (len 67 is fixed in v1). `treeNode` selects one of the heights 0 to
  HMAX by a `boolElim` chain over fixed-nesting levels. Above HMAX,
  `treeNode` gives a fixed digest, and `verify` gives false. There is no
  checker change: the affine rule (section 4.1) stays. HMAX = 20 (the
  largest RFC 8391 XMSS height).

S5 questions (S5-HANDOFF section 3). Marks (2026-10-27, S5 sessions 1
and 2): the USER ruled each one. For Q-S5-0 to Q-S5-3 (session 1) this
block keeps only the ruled option. Q-S5-12 is new in session 2, because
the ruling Q-S5-5 (b) needs it. The USER ruled Q-S5-10 after Q-S5-12,
with the new text.

- Q-S5-0 S5 base. RULED (a). (a) build on the USER commit f5e587d (tree
  696e82b3: S4 and the USER review fixes).
- Q-S5-1 Keccak in the Wasm module. RULED (a). (a) wasm.c writes one
  internal keccak256 function over linear memory (Keccak-f[1600], i64
  lanes, a hand port of src/front/keccak.c, trusted base, section 6); the
  IR gets one hash node for thChain, thNode, thMsg and prf that packs the
  5.4 bytes and calls it; vectors: module output = `langc eval` on each
  hash op.
- Q-S5-2 the `commit` import. RULED (c). (c) `(import "env" "commit"
  (func (param i64 i64)))`, a key id and the new position (Q-S5-4).
- Q-S5-3 the EIP-8310 JSON. RULED (a). (a) the v5 shape with leaf-lang
  values (5.2); no `crypto` block (test seeds in clear, O13); the commit
  import writes the authoritative position to a separate file; the code
  is in test/run-wasm.mjs (extended, no new harness file).
- Q-S5-4 the key id of `commit`. RULED (a). (a) a host-chosen i64 that the
  signer `keygen` export takes as an extra argument and keeps in the
  keystore state; the harness maps it to the `uuid` and the state file.
  (b) word w0 of the root. (c) the address of the keystore state in linear
  memory.
- Q-S5-5 the type of `sign` (`Leaf` has no height; `Sig` is indexed by h,
  Q-S4-8 (a)). RULED (b), not the recommended (a). (a) `sign : (pk :
  PubKey) → Leaf ⊸ Digest → Sig (height pk)`: the leaf value holds its own
  seeds, public key and index; sign traps when `pk` is not the public key
  of the leaf; `verify pk m (sign pk l m)` checks for a neutral keystore.
  (b) `sign : (l : Leaf) ⊸ Digest → Sig (leafHeight l)` with a 0-use core
  observation `leafHeight`; a neutral leaf and a neutral pk do not
  convert. (c) `sign : Leaf ⊸ Digest → (h : ℕ) × Sig h`.
- Q-S5-6 sequencing in `Durable` (a guest bind is REFUSE_AFFINE_FIX).
  RULED (a). (a) a core base op `andThen : Durable A ⊸ (A ⊸ Durable B) ⊸
  Durable B` (free monad bind: reduces on `done`, goes under `commit`).
  (b) extend the core `bind` carrier to `Durable` (CARRIER_DURABLE). (c)
  no sequencing in v1: one `next` per program.
- Q-S5-7 a program `commit` node (2.2 row `Durable`). RULED (a). (a) no:
  a program use of `commit` is REFUSE_SEALED; only `next` makes one, so L4
  holds by construction and a reset cannot be written. (b) `commit` stays
  public; L4 is a guest check on traces (`increasing : Durable A → Bool`).
  (c) public, plus a check-time refusal of a literal commit that does not
  increase.
- Q-S5-8 who runs a `Durable` tree. RULED (a). (a) `langc eval` gives the
  tree as a value; program entries return ℕ or Bool through a guest fold
  over the tree (last committed position, increasing, signature ok), and
  the differential compares these; only the signer exports call
  env.commit, driven by run-wasm.mjs with the JSON. (b) `langc eval`
  prints each commit as a trace line and the module calls env.commit in
  the differential too (the grid compares traces). (c) a core `run` op.
- Q-S5-9 the signer ABI. RULED (a). (a) the signer module exports `memory`
  and `keygen`, `publicKey`, `next`, `sign`, `verify`; a Digest is 32
  bytes at an i64 address; the keystore state is a block in linear
  memory; Digest lowers inside the module as a 4-word heap block. (b) each
  Digest as four i64 parameters, no memory export. (c) only a `signNext
  (key, msgAddr) -> sigAddr` export.
- Q-S5-10 the law checks (asked again after Q-S5-12). RULED (a). (a) eval
  rows at h = 4: L1 keystore form `verifyAt (leafHeight l) pk m (sign l
  m)` is true for the positions 0 to 15, and `next` gives `none` at 16; L3
  as a guest fold that checks each `some (l, ks')` is under `commit (i +
  1)` with `index (sign l m)` = i; L4 by Q-S5-7 (a); L5: a refusal fixture
  (two `sign` of one leaf is REFUSE_AFFINE_REUSE, two `next` of one
  keystore is REFUSE_AFFINE_REUSE = "reset refused") and an eval row that
  two signatures of one program have different indices. (b) (a) and a
  trace law in `check --json` "laws". (c) eval rows only, no refusal
  rows.
- Q-S5-11 the Wasm gate height (Q7 (a) names h = 4 and h = 10). RULED
  (a). (a) the differential at h = 4; at h = 10 the module only
  (run-wasm.mjs signs and verifies at 3 positions, no eval compare, R1).
  (b) h = 4 only. (c) raise EVAL_FUEL_STEPS for an h = 10 eval compare.
- Q-S5-12 the verify of a `sign` result (new, from Q-S5-5 (b): `Sig
  (leafHeight l)` does not convert with `Sig (height pk)` for a neutral
  leaf, so the L1 keystore form `verify (pk ks) m (sign l m)` does not
  check). RULED (a). (a) a domain definition `verifyAt : (h : ℕ) → (pk :
  PubKey) → Digest → Sig h → Bool`: the S4 verify algorithm at height h,
  false when `natEq h (height pk)` is false; S4 `verify pk` becomes
  `verifyAt (height pk) pk` (same results, the S4 rows stay); L1 keystore
  form: `verifyAt (leafHeight l) pk m (sign l m)` is true. (b) a core base
  op `sigAt : (pk : PubKey) → (h : ℕ) → Sig h → Option (Sig (height pk))`:
  `some s` when `natEq h (height pk)`, else `none` (trusted base);
  `verify` stays. (c) no guest verify of a `sign` result: the L1 keystore
  form is checked only in the signer module run (run-wasm.mjs: the
  `verify` export over the `sign` export output); the eval rows check
  `index` only.
- Q-S5-13 the keystore and leaf values in the core (new in U2). RULED
  (a). (a) two internal core value ops with no source name and no Rule
  row: the keystore value [skSeed, skPrf, pk, position] and the leaf value
  [skSeed, skPrf, pk, index], all data in the args (R5); only `keygen`
  and `next` make them; a program cannot write one, so there is no new
  refusal site. (b) source names with a Rule row that refuses each
  program use with `REFUSE_SEALED`. (c) one internal value op with a tag
  arg (0 keystore, 1 leaf).
- Q-S5-14 the print form of a keystore and a leaf value in `langc eval`
  (new in U2). RULED (a). (a) `keystore h i` and `leaf h i`: the height
  and the position or index only; no seed and no public key goes to
  stdout or to a log. (b) the public key and the position. (c) all the
  args, the secret seeds too.
- Q-S5-15 the `done none` row of `next` in U2 (new in U2). RULED (a). (a)
  the row uses a closed h = 0 keystore (one leaf: `next` at 0 gives
  `commit 1 (...)`, `next` at 1 gives `done none`); h = 4 stays for
  `keygen`, `publicKey`, `next` at 0, the `sign` index and `verifyAt`;
  the h = 4 walk to position 16 is the U3 L1 keystore row with `andThen`
  (Q-S5-10 (a)). (b) a test-only guest walk of 16 `Durable` trees at h =
  4 in U2. In U2 the printed tree shows the family param of each node,
  as Q-S5-17 (a) shows the type of a stuck `next`: `next` at 0 prints
  `commit A 1 (done A (some (pack (leaf 0 0) (keystore 0 1))))`, with A
  the printed form of `Option (Leaf × Keystore)` (the Keystore def
  unfolds; test/eval/expect.txt).
- Q-S5-16 the core op name `next` and the fixtures that declare `next`
  (new in U2c). RULED (a): the core op name stays `next` (3.2, the
  signer export, L1). R4 (check.c name_owner) refuses a declared name
  that is a core name, so examples/functions.lang, test/fmt/comment.lang
  and test/fmt/comment.canon rename their `next` to `addOne`. R4 is
  unchanged.
- Q-S5-17 the type of the `done` and `commit` nodes that `next` makes
  (new in U2c). RULED (a): the checker gives the type `Option (Leaf ×
  Keystore)` to the OP_NEXT core node as a hidden first arg. The source
  arity is 1 and the eval arity is 2 (type, keystore). A stuck `next ks`
  prints with the type (`next (Option (Leaf × Keystore)) ks`). There is
  no new name and no domain helper.
- Q-S5-18 the `verifyAt` row of a leaf from `next` (new in U2e). RULED
  (a). The sig binder of `verifyAt` is omega, so `verifyAt (leafHeight
  l) pk m (sign l m)` in the scope of the leaf `l` scales the leaf use
  of `sign` to omega (REFUSE_AFFINE_OMEGA). (a) keep: a row or a guest
  program packs `pack (leafHeight l) (sign l m)` into a `(h : ℕ) × Sig
  h` value out of the scope of the leaf, and gives the `witness` and the
  `payload` of that value to `verifyAt` (test/eval/keystore.lang sig4).
  No checker or domain change. (b) a domain helper that gives the pair
  from a leaf and a message. (c) the sig binder of `verifyAt` at 1, with
  a body rewrite (the body uses `sig` more than once).
- Q-S5-20 the commit call in U4. RULED (b) by the USER 2026-10-27: the
  commit call moves to U6 with the signer exports; its IR form, its
  lowering and a row come in one slice. No IR form without a row.
- Q-S5-21 the keystore ops in the lowering (OP_KEYGEN, OP_PUBLIC_KEY,
  OP_NEXT, OP_SIGN, OP_LEAF_HEIGHT, OP_AND_THEN, OP_KEYSTORE_VAL,
  OP_LEAF_VAL). RULED (b) by the USER 2026-10-27: U4 lowers them. The
  keystore and leaf values are blocks [skSeed, skPrf, pk, i] (and the key
  id); keygen and sign go through their domain bodies (pkFromSeeds,
  signAt) with the hash node. This needs a digit op in the IR (no shift or
  div today). The text of (b) also names `next` with the commit call,
  which Q-S5-20 (b) moves to U6: Q-S5-22 (U4c) rules the `next` lowering.
- Q-S5-22 the `next` lowering (new in U4b). RULED (a) by the USER
  2026-10-27: U4 lowers `next` and `andThen` with no host call. The
  Durable tree is data (`commit at cont` and `done x` as ctor blocks), and
  a `fold` over Durable is an eliminator, so a Nat or Bool entry over
  `opened (next ks)` lowers. U6 adds the commit host call at the signer
  `next` export (Q-S5-20 (b)).
- Correction (U4c survey, 2026-10-27): a keystore op with closed args
  lowers by eval. `langc ir` runs eval with no hash, so each hash op stays
  stuck and lowers as a hash node, and keygen, publicKey, next, andThen,
  leafHeight and sign unfold their domain bodies. keygen at v1Params 0
  gives 1138 hash statements (67 x 16 + 66). Q-S5-21 (b) and Q-S5-22 (a)
  hold for closed args with no new arm.
- Q-S5-23 a stuck keystore op (new in U4c). RULED (a) by the USER
  2026-10-27: a stuck keygen, publicKey, next, andThen, leafHeight or sign
  (a neutral seed or a neutral height) gives the refusal
  REFUSE_RUNTIME_KEYSTORE ("a keystore operation needs closed seeds and a
  closed height in a lowered entry (runtime keys: S5 U6)"), with a
  DIAGNOSTICS.md row and an ir_refused row. U6 designs runtime keys with
  the signer state.
- Q-S5-24 the keystore rows (new in U4c). RULED (a) by the USER
  2026-10-27: a new gate.sh row `ir_digest NAME`. The sha-256 of `langc ir
  test/ir/NAME.lang` is equal to test/ir/NAME.sha256 (one line), and the
  row adds 1 to "ir: N lowered". A hand check of the hash statement count
  goes with it.
- Q-S5-25 a canonical keystore or leaf value (new in U4c). RULED (a) by
  the USER 2026-10-27: a canonical KEYSTORE_VAL or LEAF_VAL in the
  lowering gives REFUSE_RUNTIME_KEYSTORE too. U6 picks the block layout
  with the signer state and the key id.
- Q-S5-26 the Keccak form in the Wasm module (new in U5). RULED (a) by
  the USER 2026-10-27: one internal function `keccak_f (state)`. It loads
  the 25 lanes into i64 locals, runs the 24 rounds as straight-line code
  (each round constant is an i64.const from the keccak.c table) and
  stores the lanes back. No data section and no reserved memory.
- Q-S5-27 the byte order of the hash input (new in U5). RULED (a) by the
  USER 2026-10-27: the hash node writes the 5.4 input as big-endian IR
  words in a new block (a Digest gives its 4 words, a Tweak gives 4 ADRS
  words with the 32-bit width traps of `langc eval`, `toByte(v, 32)`
  gives `[0, 0, 0, v]`). `keccak256 (in, n, out)` swaps the bytes of each
  word when it XORs the word into a lane, and swaps lanes 0 to 3 into the
  output Digest words.
- Q-S5-28 the U5 differential set (new in U5). RULED (a) by the USER
  2026-10-27: test/ir/hash.lang and test/ir/keystore.lang join the gate
  Wasm loop (wasm 14 to 16). The U5b probe first records the size, the
  run time and the memory of the keystore module. Over about 60 s or over
  4 GB, U5b stops and asks.
- Q-S5-29 REFUSE_TARGET_NODE after U5 (new in U5). RULED (a) by the USER
  2026-10-27: the code stays as the Wasm writer arm for a node with no
  Wasm form (no IR node gives it now). The gate row `build_refused hash`
  goes, and DIAGNOSTICS.md says that no IR node gives the code now.
- Q-S5-31 the signer exports (new in U6). RULED (a) by the USER
  2026-10-27: src/wasm.c writes the five signer exports and internal
  WOTS+, ltree and treeHash functions as hand Wasm loops over the
  internal keccak256. This is a hand port of the S4 domain algorithms
  (trusted base, section 6, as Q-S5-1 (a)). Only the `next` export calls
  env.commit, so the IR gets no commit node: Q-S5-20 (b) holds with no IR
  form. A second `sign` of one leaf traps (L5 at run time). The h = 4
  differential and the U7 mutants check the port. The reason: the
  lowering unrolls each hash for a closed height, and node refuses the
  h = 2 keystore module (one function is 8317063 bytes, the limit is
  7654321). (b) the guest domain lowered with run-time loops (Digest and
  keystore entry parameters, no heap reset, an IR store and an indexed
  load, a loop form of treeHash and authPath) and (c) the signer at h = 0
  and h = 1 only, with no h = 10 run: not taken.
- Q-S5-32 the signer build command (new in U6). RULED (a) by the USER
  2026-10-27: a new subcommand `langc signer` with no source file. Only
  this module has the env.commit import and the signer exports. A `langc
  build` module keeps the empty import section, so run-wasm.mjs gives no
  import to it. (b) `langc build --signer` and (c) the signer exports in
  each module: not taken.
- Q-S5-33 the height and a restart (new in U6). RULED (b) by the USER
  2026-10-27, NOT the recommended (a): `langc signer --height H -o FILE`
  writes a module for one height H, so the gate builds two modules (h = 4
  and h = 10). The `keygen` export takes the key id, the start position
  and the addresses of the three seeds: `keygen (id, start, skSeed, skPrf,
  pubSeed) -> ks`. The start position is the authoritative position from
  the state file (0 for a new key), so a restart continues there and does
  not use a leaf again. keygen traps when start is larger than 2^H. (a)
  the height as a run-time keygen argument (one module) and (c) no start
  argument (a restarted key cannot sign): not taken.
- Q-S5-34 the h = 4 differential (new in U6). RULED (a) by the USER
  2026-10-27: a fixture test/wasm/signer4.lang (h = 4, fixed seeds) with
  the Nat entries `rootSum` and `sigSum p`: a weighted sum of the digits
  of the root, or of the signature at position p, modulo 2^61 - 1, as a
  guest fold. `langc eval` gives the value, and run-wasm.mjs gives the
  same sum in BigInt over the module memory. The module `verify` gives 1
  at each position, and `next` gives none at position 16. The probe stops
  at 60 s or 4 GB. (b) a seventh export `keccak256` and a guest Digest
  fingerprint and (c) one Bool entry for each digest (about 1150 eval
  calls): not taken.
- Q-S5-35 the start position of a harness key (new in U6a). RULED (a) by
  the USER 2026-10-27: the harness line `keygen ID SKSEED SKPRF PUBSEED
  [START]`. With no START, the start is the position in DIR/ID.state (0
  when there is no file). START models a stale backup. At h = 4 the line
  with START 17 traps. (b) a run flag `--start N`: not taken.
- Q-S5-36 the keystore `uuid` (new in U6a). RULED (a) by the USER
  2026-10-27: RFC 9562 version 8, `00000000-0000-800X-8XXX-XXXXXXXXXXXX`
  with the 16 hex digits of the key id in the X places (id 1 gives
  `00000000-0000-8000-8000-000000000001`). (b) a version 5 SHA-1 name and
  (c) the padded id with no version bits: not taken.
- Q-S5-37 the sign and verify harness lines (new in U7). RULED (a) by the
  USER 2026-10-27: `sign LEAF M` prints `sigSum N` and keeps the
  signature, and `verify M` checks the kept signature against M and the
  public key of the last keygen. The bad signature row is `verify M2`.
  There is no new export. (b) a `tamper K` line and (c) `sign M` with no
  LEAF: not taken.
- Q-S5-38 the sign leaf argument (new in U7). RULED (a) by the USER
  2026-10-27: `sign (ks, leaf, m)`, where leaf is a `next` result. It
  traps unless the signed bound is less than leaf and leaf is at most the
  next position. The guest form of sign keeps the leaf as an argument. (b)
  `sign (ks, m)` for the leaf of the last `next`: not taken.
- Q-S5-39 the verify public key argument (new in U7). RULED (a) by the
  USER 2026-10-27: `verify (pk, m, sig)` with a publicKey block (D27), so
  a verifier needs no keystore. (b) `verify (ks, m, sig)`: not taken.
- Q-S5-40 the M6 height mutant (new in U7d). RULED (a) by the USER
  2026-10-28: M6 (verify with no check of the key height H) stays a
  known survivor, with no new harness line. No gate row gives verify a
  public key of another height. (b) a harness line that gives verify a
  public key of a wrong height: not taken.

S6 questions (W/S6-PROGRESS.md, Session 29). Marks (2026-10-28, S6
session 1): the USER ruled each one. This block keeps only the ruled
option.

- Q-S6-1 the EVM writer. RULED (a) by the USER 2026-10-28: the
  verifier is hand-written bytecode, `langc verifier --height H --pk
  FILE [--entry 8141|4337] [--shim] -o FILE`, with no IR (the S5 signer
  pattern). The port takes the kit `src/asm.c` and `src/asm.h` only.
- Q-S6-2 entry 1 inputs. RULED (a) by the USER 2026-10-28: the real
  entry 1 reads the signature hash and the nonce with TXPARAM, the
  signature from calldata, and runs APPROVE with the scope "both". The
  shim swaps the three table sites (a 64-byte calldata prefix, RETURN
  of the word 1). Q-S6-5, RULED (a) by the USER 2026-10-28 (S6 U4),
  corrects the signature source: entry 0 of the `signatures` list
  (ARBITRARY), read with SIGPARAM 0xb4 and SIGDATACOPY 0xb5, because the
  signature hash covers the frame data. The shim copies the calldata
  after the prefix. The two builds copy the signature to one memory
  region (5.1).
- Q-S6-3 entry 2 failures. RULED (a) by the USER 2026-10-28: entry 2
  returns 1 on each accept failure (signature, root, nonce sequence,
  nonce key not 0). Malformed calldata and a caller that is not the
  v0.7 EntryPoint (a build constant) revert. It pays
  `missingAccountFunds` to the caller. The section 9 S6 gate reads
  "return 1" for entry 2. Q-S6-6 RULED (a) and (a) by the USER
  2026-10-28 makes this exact: a wrong signature length and ABI
  offsets out of range are accept failures (return 1), so only a wrong
  selector and another caller revert. The `missingAccountFunds` CALL
  comes before the core. Q-S6-7 RULED (a) by the USER 2026-10-28: the
  D10 root differential is the 32 accept rows (the EVM root is equal to
  the pk root that the Wasm signer writes) and the 2 wrong-root rows.
  There is no root-out build.
- Q-S6-4 the H = 16 gas rows. RULED (a) by the USER 2026-10-28: the gas
  script runs one real H = 16 keygen in the Wasm signer.
- Q-S6-8 the gas script. RULED (b) by the USER 2026-10-28: a new script
  in R, test/gas.sh, run by `make gas`, not by `make check`.
