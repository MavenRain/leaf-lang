# leaf-lang plan

leaf-lang is a language for post-quantum accounts. A program signs with
XMSS one-time leaves and verifies XMSS signatures. Each leaf is an affine
resource: the checker refuses a program that can use one leaf two times.
The host is TinyCC. The targets are an EVM account verifier, a Wasm signer
and the zkml-lang Circuit IR.

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
  resource, EIP-8310 commit-before-sign state); the zkml-lang Circuit IR
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

Not ruled. The plan takes (a) for each of these:

- Q2 kit: (a) by default; the USER did not rule it.
- Q4 keystore: (a) by default; the USER did not rule it.
- Q5 commit-before-sign: (a) by default; the USER did not rule it.
- Q7 v1 parameters: (a) by default; the USER did not rule it.
- Q8 on-chain one-time: (a) by default; the USER did not rule it.
- Q9 Wasm crypto: (a) by default; the USER did not rule it.
- Q10 zkml field: (a) by default; the USER did not rule it.
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

## 2. The reading of R1

### 2.1 The precedent reading

leaf-lang takes the reading that intent-lang (PLAN 2.2, USER ruling S1
Q1 (a) and Q2 (a), 2026-10-22) and resolve-lang (PLAN 2) use (Q1):

| Part | Cubical TT | R1 limit |
|---|---|---|
| Ambient | Pi, Sigma, `Path`, `PathP`, `I`, `i0`, `i1`, `transp`, `hcomp`, `U`, `Glue`, `ua`, the builtin `Sum` and `Unit` | none: the internal language of the category |
| CM, cell monad | a `family` with a type parameter: the parameter is the one field of one generator `eta : A -> T A`; each other constructor takes closed base data and arguments of type `T A`, or is a path in `T A` | else `REFUSE_CELL` |
| CC, cocell comonad | a `cofamily` with one observation `eps : D A -> A`; each other field has type `D A -> D A` or `D A -> E -> D A` with `E` closed data | else `REFUSE_CELL` |

Why the two adjectives matter: local presentability gives the free algebra
of each set of cells, so each cell signature has its monad. Local cartesian
closure gives Pi and Sigma by base change, so the ambient part exists.

A family with no type parameter is a base type, not a type constructor. R2
limits it, not R1. Only `domain/domain.lang` declares a family or a
cofamily. A program that declares one gets `REFUSE_DATA`.

### 2.2 Every type former leaf-lang needs

| Former | Use in leaf-lang | Class | Reason |
|---|---|---|---|
| Pi, `A -> B` | every function; vectors `Fin n -> Digest` | ambient | LCC |
| `A -o B` | at-most-once function (section 4) | ambient | Pi with multiplicity 1 on the binder |
| Sigma, records | `Sig`, `PubKey`, `Tweak`, `Params` | ambient | named iterated Sigma (quorum-lang Q2 shape) |
| `A * B` at multiplicity 1 | `Leaf * Keystore` | ambient | Sigma with multiplicity 1 |
| `Fin n` | positions in a vector | defined | `Sigma (k : Nat), Path Bool (lt k n) true` |
| vectors | WOTS+ signature, auth path, chain ends | ambient | `Fin n -> Digest`, no new former |
| `Path`, `PathP`, `I`, `transp`, `hcomp` | laws and proofs | ambient | the identity type of Cubical TT |
| `U`, `Glue`, `ua` | universes; no leaf-lang use yet | ambient | kept as in the precedent |
| `Option A` | `next` on an empty keystore | CM | one 0-cell `none`, generator `some` |
| `Durable A` | the signer effect (commit-before-sign) | CM | generator `done : A -> Durable A`; one operation `commit : Nat -> Durable A -> Durable A` with closed data `Nat` (the free cell monad of one operation, as mandate-lang 4.6) |
| `Trunc A` | "a valid signature exists" in laws | CM | one 1-cell `squash` |
| `Stream A` | the keystore reading (section 4.3) | CC | observation `head : Stream A -> A`, field `tail : Stream A -> Stream A` |
| cons `List A` | none | REFUSED | `cons : A -> List A -> List A` uses `A` outside the generator; vectors over `Fin` do the work |
| `Bag A`, free monoid | none | not admitted | no bullet needs a multiset or a sequence of unknown length |
| `Either E A` | none | not admitted | errors are `Option` or `Bool`; no bullet needs an error payload |
| `data Pair (A : U) := mk A A` | none | REFUSED | `REFUSE_CELL` example of the precedent; the ambient `A * A` is the admitted form |

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
| `add`, `sub`, `mul`, `div`, `mod`, `lt`, `natEq` | defined | by `Nat` recursion; targets lower them to machine words (O8) | B1 (base-w digits, checksum, path bits) | S4 |
| `Digest` | base type | `n` bytes, the hash output | B1 | S4 |
| `digestEq` | base op | `Digest -> Digest -> Bool`; obligation: decides `Path Digest` | B1 (verify compares a root) | S4 |
| `digit` | base op | `Nat -> Digest -> Nat -> Nat` (log2 w, digest, position); obligation: result `< w` | B1 (base-w message encoding) | S4 |
| `Tweak` | defined | record of `Nat`: layer, tree, kind, leaf, chain, step | B1 (RFC 8391 address) | S4 |
| `thChain` | hash op | `Digest -> Tweak -> Digest -> Digest` (public seed, tweak, value) | B1 (WOTS+ chain step F) | S4 |
| `thNode` | hash op | `Digest -> Tweak -> Digest -> Digest -> Digest` | B1 (L-tree and Merkle node H) | S4 |
| `thMsg` | hash op | `Digest -> Digest -> Nat -> Digest -> Digest` (randomness, root, index, message) | B1 (H_msg) | S4 |
| `prf` | hash op | `Digest -> Tweak -> Digest` (secret seed, tweak) | B1 (WOTS+ secret keys, signing randomness) | S4 |
| `Params` | defined | record: `n`, `logW`, `h` (section 5.4) | B1, B3 | S4 |
| `chain`, `wotsPk`, `wotsSign`, `wotsPkFromSig`, `ltree`, `treeHash`, `authPath`, `rootFromPath` | defined | RFC 8391 algorithms over the hash ops | B1 | S4 |
| `PubKey` | defined | record: `root`, `pubSeed`, `params` | B1, B2 | S4 |
| `Sig` | defined | record: `index : Nat`, `rand : Digest`, `wots : Fin len -> Digest`, `auth : Fin h -> Digest` | B1, B2 | S4 |
| `verify` | defined | `PubKey -> Digest -> Sig -> Bool` | B1, B2 | S4 |
| `Leaf` | resource | one unused one-time key; sealed: only `next` makes one (`REFUSE_SEALED`) | B4 | S3 |
| `Keystore` | resource | the secret seeds, the public key and the signing position; sealed | B4, R4 (EIP-8310) | S3 |
| `Durable` | CM (domain) | section 2.2 | B4 (commit-before-sign) | S3 |
| `keygen` | base op | `Digest -> Digest -> Digest -> Params -> Keystore` (secret seed, PRF seed, public seed) | B1, B4 | S5 |
| `publicKey` | base op | `Keystore -o PubKey * Keystore` | B1, B2 | S5 |
| `next` | base op | `Keystore -o Durable (Option (Leaf * Keystore))` | B4 | S5 |
| `sign` | base op | `Leaf -o Digest -> Sig` | B1, B4 | S5 |
| `accept` | defined | `PubKey -> Nat -> Digest -> Sig -> Bool` = `natEq (index s) nonce` and `verify` | B2 (nonce binding) | S6 |
| `sigHash`, `txNonce` | target boundary (EVM) | the EIP-8141 signature hash and nonce, or the ERC-4337 `userOpHash` and nonce sequence | B2 | S6 |
| `approve` | target boundary (EVM) | APPROVE (0xaa) when `accept` is true, else REVERT | B2 | S6 |
| `commit` (interpretation) | target boundary (Wasm) | the host import that writes the new position durably before it returns | B4, R4 | S5 |
| `aggregate` | defined | `(Fin N -> PubKey * Digest * Sig) -> Bool` = all `verify` | B1 (validators, leanMultisig role) | S7 |
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

The kit `Sum` former is not in the row above. It stays in S1. `Either` is
not admitted (2.2). S2 drops or keeps `Sum` with the `REFUSE_CELL` review
(O16).

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
- The arrow has a multiplicity bit (brisk amendment D1): `A -> B` is
  unrestricted and `A -o B` is at most once. A closure that captures a
  1-variable has type `A -o B`.
- A `match` uses its scrutinee once. All arms start from the same use set.
  The result use set is the maximum of the arms (brisk variants rule).
- Types are checked at multiplicity 0, so `Path Leaf l l` in a type uses
  nothing. Laws quantify over leaves at 0 and never run.

### 4.2 Refusal codes (`langc check --json`)

| Code | Refuses |
|---|---|
| `REFUSE_AFFINE_REUSE` | a 1-variable used two times on one evaluation path |
| `REFUSE_AFFINE_OMEGA` | a resource value or an `A -o B` value passed where an omega binder or an unrestricted arrow is expected |
| `REFUSE_AFFINE_FIX` | a recursive binder or a corecursive `cofamily` unfold that captures a 1-variable (each unfolding would use it again) |
| `REFUSE_AFFINE_PATH` | a 1-variable free under an interval binder `<i>`, or in `transp`, `hcomp` or `Glue` at multiplicity 1 (both end points would hold it) |
| `REFUSE_SEALED` | a program that builds or opens a `Leaf` or a `Keystore` outside the domain |

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
| D1: one multiplicity bit on the arrow | kept: `A -> B` and `A -o B` |
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

- Entry 1, EIP-8141 VERIFY frame: the frame gives the signature hash, the
  nonce and the signature in calldata. The contract computes `accept pk
  nonce sigHash sig`. When it is true the contract runs APPROVE (0xaa),
  else it reverts. The frame is a STATICCALL, so the contract writes no
  state. `pk` (root, public seed, parameters) is a constant in the code.
- Entry 2, ERC-4337 `validateUserOp(userOp, userOpHash, missingAccountFunds)`:
  the same `accept` over `userOpHash` with the nonce sequence of key 0. It
  returns 0 on success and 1 (`SIG_VALIDATION_FAILED`) on failure.
- The opcodes and the TXPARAM fields of EIP-8141 are a draft (O1). The
  writer keeps them in one table.
- The test EVM of the kit has no EIP-8141 (O2). S6 tests the core and
  entry 2 on it, and entry 1 through a shim that maps APPROVE to a return
  code.
- Port: lang-template `hosts/tcc-evm-contract` (`src/asm.c`, `src/evm.c`,
  `src/keccak.c`, the test harness `test/emittool.c`, `test/dispatch.sh`).

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

- The kit Wasm writer (MVP, i64). The module exports `keygen`,
  `publicKey`, `next`, `sign` and `verify`. The keystore state is in
  linear memory.
- Keccak-f[1600] is code in the module (i64 lanes), so secrets stay in the
  module (Q9). The one host import is `commit(position)`: it writes the
  position durably and returns only after the write. The module runs the
  continuation of a `commit` node only after the import returns.
- The node test harness writes and reads the EIP-8310 keystore JSON
  (format version, parameters, public key, committed position) around the
  module. The harness refuses a position that goes back (no silent reset).
- `langc eval` and the module agree on every example (the kit
  differential test).

### 5.3 zkml-lang Circuit IR

- A C writer prints the statement of `aggregate` as a zkml-lang source
  file: a `Circuit` term with the public keys and messages public and the
  signatures as witness. zkml-lang makes the backend output. `PIN` records
  the zkml-lang commit (968dce4 at S0).
- Keccak in a circuit is too costly, so this target needs the Poseidon2
  binding. zkml-lang has BabyBear and BN254 and no Poseidon2; leanSig uses
  KoalaBear (Q10, O3).
- Port: zkpolicy-lang `src/zkml.c`, `src/zkml.h`, `src/zkml-names.h`.

### 5.4 Hash bindings and parameters

| Op | `langc eval` | EVM | Wasm | zkml |
|---|---|---|---|---|
| `thChain`, `thNode`, `thMsg`, `prf` | Keccak-256 (`keccak.c`) | Keccak-256 (KECCAK256, 0x20) | Keccak-256 (code in the module) | Poseidon2 (S7) |

v1 construction (Q6 (a)): `th(seed, tweak, x) = Keccak-256(seed || tweak ||
x)`, one call per step (the SPHINCS+ "simple" shape). v1 parameters (Q7
(a)): n = 32, log2 w = 4, h a parameter; the gate uses h = 4 and h = 10;
the default for an account is h = 16 (65536 transactions).

## 6. Laws (guest terms)

| Law | Statement | Slice |
|---|---|---|
| L1 correctness | for each keystore `ks` and message `m`, when `next ks` gives `(l, ks')`, `verify (pk ks) m (sign l m)` is `true` | S4, S5 |
| L2 nonce binding | `accept pk k m s` is `true` implies `index s` is `k` | S6 |
| L3 commit-before-sign | in each `Durable` tree, each `sign` of a leaf with index `i` is under a `commit` node that writes `i + 1` | S5 |
| L4 no reset | the committed positions on each path of a `Durable` tree increase strictly | S5 |
| L5 distinct leaves | two signatures that one program makes have different indices (from the affine check and L4) | S5 |
| L6 binding independence | the kernel never unfolds a hash op, so each law that checks holds for each binding | S4 |
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
| `-o` (S3), `Path` (S2) | not in S1 | not in S1 |

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
| zkml-lang | 968dce4 | the Circuit IR (the target) |
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
| S2 | kernel port: Path, Sigma, Pi, U, `I`, `transp`, `hcomp`, records, `REFUSE_CELL`, cofamily, `Fin` | port tests; `REFUSE_CELL` mutants |
| S3 | affine layer: multiplicities, `-o`, resource sorts, sealed `Leaf` and `Keystore`, `Durable`, the five refusal codes | one test per code; mutants on each refusal |
| S4 | `Digest`, hash ops (opaque), WOTS+, L-tree, Merkle, `sign`, `verify`; reference Keccak in `langc eval`; L1, L6 | own vectors plus a cross-check with an independent implementation; L1 at h = 4 |
| S5 | `keygen`, `publicKey`, `next`, `commit`; Wasm signer (Keccak in the module, `commit` import, EIP-8310 JSON in the harness); L3 to L5 | differential eval against Wasm; reset refused |
| S6 | EVM verifier: ERC-4337 entry, EIP-8141 entry with the shim, nonce binding L2; measured gas | bytecode runs in the kit EVM harness; wrong nonce and wrong root revert |
| S7 | Poseidon2 binding (field of Q10) in `langc eval` and the zkml writer; `aggregate`; L7 | zkml-lang accepts the output; Poseidon2 vectors |
| S8 | boundary exclusion: the signer service takes programs only; output manifests; rebuild compare | refusal tests; byte-identical rebuild |
| S9 | agent kit: `SKILL.md`, grammar card, 5 graded examples, `langc mcp`, `langc explain`, `TOKEN-BASELINE.md`, final `AGENTS.md` | MCP stdio test; examples pass `check` |
| S10 | close: `docs/STATUS.md`, `docs/VALIDATION.md`, full gate, mutant summary | full gate EXIT 0 |

## 10. Open items

- O1: EIP-8141 is a draft (APPROVE 0xaa, TXPARAM, the signature hash). The
  writer keeps the opcodes in one table. Each slice that touches the EVM
  entry checks the spec again.
- O2: the kit EVM harness has no EIP-8141. Entry 1 is tested through a
  shim until an EVM with EIP-8141 exists.
- O3: Poseidon2 field. zkml-lang has BabyBear and BN254. leanSig uses
  KoalaBear. Q10 picks.
- O4: XMSS^MT or generalized XMSS for the leanSig lifetime 2^32.
- O5: two signers on one key (EIP-8310 reserved leaf ranges, an affine
  `split`). Q12.
- O6: zeroize the secrets of a dropped leaf in the Wasm module.
- O7: `Digest` as a base type or as `Fin n -> Byte`.
- O8: the targets lower the `Nat` definitions (`div`, `mod`) to machine
  words. The lowering is trusted, as zkml-lang `natDiv`.
- O9: `REFUSE_AFFINE_PATH` can be too strict for a path whose end points
  are equal by definition. S3 records each refused case it meets.
- O10: calldata of about 2.7 KB per signature against the EIP-7623 floor.
- O11: the leanSig target-sum encoding against the RFC 8391 checksum.
- O12: reuse across runs (a restored backup) is outside the checker
  (section 4.5).
- O13: seed entropy comes from the host.
- O14 (Claude reading, not ruled): an ASCII alias parses and gives
  `W_NONCANON` with the Unicode form as `fix`; `fmt` prints only the
  Unicode form. Unicode can cost more tokens; the S9 baseline (Q13)
  measures it.
- O15: a `let` form under Q14 (b). Open.
- O16: the kit `Sum` former stays in S1. `Either` is not admitted (2.2).
  S2 drops or keeps `Sum` with the `REFUSE_CELL` review.
- O17: `flagIf` keeps its kit name (it is not in the S1 drop list). The
  USER rules the name of `if`.
- O18: dead core paths. The Op values and the eval and lowering code for
  `List`, `Prod`, `filter` and `unfold` stay, but no program reaches them.
  S2 removes them.
- O19: the value printer and some check messages use the ASCII kit
  spelling (`->`, `fun x =>`, `Sigma (x : A) B`, `Nat`). The parser reads
  it back through the aliases.

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
- Q10 zkml field. DEFAULT (a). (a) Poseidon2 over BabyBear (zkml-lang has
  it). (b) add KoalaBear to zkml-lang first (leanSig parameters).
- Q11 proofs. DEFAULT (a). (a) guest only; no Lean export. (b) a Lean
  export in a later slice.
- Q12 two signers. DEFAULT (a). (a) not in v1 (O5). (b) admit an affine
  `split` in S5.
- Q13 token counts. DEFAULT (a). (a) a fixed offline proxy. (b) API
  counts (USER refresh).
- Q14 surface. RULED (b). (a) ASCII verbose keywords with
  `step NAME INTO`. (b) Agda-like Unicode (section 7.1).
