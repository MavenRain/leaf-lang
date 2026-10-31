# leaf-lang grammar card

The short form of `SPEC.md` section 2. When this card and `SPEC.md` are
different, `SPEC.md` is correct. Each example line is from
`examples/graded/` or `test/fmt/affine.canon`.

## Programs

A program is a list of definitions. Each definition is a signature line
and a clause line. The clause `f x y = t` is `f = λ x y → t`.

    square : (n : ℕ) → ℕ
    square n = natMul n n

- A definition can refer only to an earlier definition. Recursion is only
  by `fold`.
- `check` refuses these forms in a program: `data` (`REFUSE_DATA`),
  `postulate` (`REFUSE_AXIOM`), `rec` or a reference to the same or a later
  definition (`REFUSE_REC`), a core, domain or used name (`REFUSE_NAME`).

## Declaration forms (SPEC.md 2.2)

    decl  := 'postulate' NAME ':' term
           | 'rec'? NAME ':' term NAME NAME* '=' term
           | 'data' NAME group* ':' 'Type' 'where' ctor+
    ctor  := NAME ':' term
    group := '(' NAME+ ':' term ')'

A program uses only the second form, with no `rec`.

## Term forms (SPEC.md 2.2)

    term := 'λ' (NAME | group)+ '→' term | prod ('→' term)?
    prod := group '→' term | group '×' prod | app ('×' prod)?
    app  := atom atom*
    atom := NAME | NAT | 'Type' NAT | '(' term ')'

- `×` binds tighter than `→`. Both nest to the right: `A → B → C` is
  `A → (B → C)`.
- Keywords: `rec`, `postulate`, `data`, `where`, `Type`, `fun`, `Sigma`.
- `NAT` is 0 to 2^64-1. `Type n` is a universe.

| Form | Example line |
|---|---|
| dependent function | `sumOfSquares : (a : ℕ) → (b : ℕ) → ℕ` |
| application | `sumOfSquares a b = natAdd (square a) (square b)` |
| lambda | `orZero s = fold (λ (e : Bool) → 0) (λ (n : ℕ) → n) s` |
| dependent pair | `firstSig : Option ((h : ℕ) × Sig h)` |
| pair | `opened : Durable (Option (Leaf × Keystore)) ⊸ Option (Leaf × Keystore)` |
| universe | `Signed : Type 0` |

## Multiplicities (test/fmt/affine.canon)

- `(@1 x : A) → B`: the function uses `x` at most one time.
- `(@0 y : A) → B`: `y` is erased (types, laws, proofs). A use of `y` in a
  run-time position is refused (`REFUSE_AFFINE_REUSE`).
- A binder with no mark has the multiplicity `ω` (no limit). `langc explain`
  shows the multiplicity of each binder.
- `A ⊸ B` is `(@1 _ : A) → B`: the affine arrow.
- A lambda can mark its binder: `λ (@1 n : ℕ) → n`.
- A resource (`Leaf`, `Keystore`) is used at most one time. A second use is
  refused. An affine value can be dropped.

| Form | Example line |
|---|---|
| marked binders | `keepOne : (@1 x : ℕ) → (@0 y : ℕ) → ℕ` |
| affine arrow | `first : ℕ ⊸ ℕ ⊸ ℕ` |
| marked lambda | `once = λ (@1 n : ℕ) → n` |
| affine resource | `signOnce : (@1 ks : Keystore) → (m : Digest) → Option ((h : ℕ) × Sig h)` |

## Aliases (SPEC.md 2.3)

| Alias | Canonical |
|---|---|
| `->` | `→` |
| `\x -> t`, `fun x => t` | `λ x → t` |
| `A * B`, `Sigma (x : A) B` | `A × B`, `(x : A) × B` |
| `Nat` | `ℕ` |
| `-o` | `⊸` |

`check` gives `W_NONCANON` for an alias, with the canonical text as the
`fix`. `fmt` prints only the canonical spellings.

## Layout and comments (SPEC.md 2.4, 2.5)

- A declaration starts at column 1.
- When a term continues on the next line, that line starts at column 2 or
  more.
- A comment is `--` on a full line directly above a declaration.

## Base sorts and names

`langc mcp` tool `list-base` gives the full list.

| Sort | Names | Example line |
|---|---|---|
| `ℕ` | `natAdd`, `natMul`, `natEq`, `natLe` | `square n = natMul n n` |
| `Bool` | `true`, `false`, `boolElim` | `acceptAt pk k m s = boolElim (λ b → ℕ → Bool) (λ u → verifyAt (witness s) pk m (payload s)) (λ u → false) (natEq (witness (payload s)) k) 0` |
| `Digest` | `digest a b c d`, `digestEq` | `msg = digest 13 14 15 16` |
| `Option A` | `some`, `none`, `option d f o`, `map`, `bind`, `pure` | `nextOf o = option 0 (λ n → natAdd n 1) o` |
| `Sum A B` | `inl A B a`, `inr A B b`; the match is `fold` | `parsed = inr Bool ℕ 7` |
| `Σ` | `pack`, `witness`, `payload`, `unpack p (λ a b → t)` | `pk4 = unpack (publicKey ks4) (λ pk ks → pk)` |
| `Path A x y` | `refl` | |
| `Fin n`, `Type n` | | `Signed : Type 0` |

Resources: `Leaf`, `Keystore` (sealed), `Durable A` (`done A a`). `commit`
is refused in a program: only `next` writes a keystore position.

| Base operation | Type |
|---|---|
| `keygen skSeed skPrf pubSeed (v1Params h)` | `Keystore` |
| `publicKey ks` | a pair of `PubKey` and `Keystore` |
| `next ks` | `Durable (Option (Leaf × Keystore))` |
| `sign l m` | `Sig (leafHeight l)` |
| `andThen d k` | runs `k` under the commit node of `d` |

Defined in `domain/domain.lang`: `verify`, `verifyAt`, `accept` (the nonce
binding), `aggregate`.

    ks4 = keygen skSeed skPrf pubSeed (v1Params 4)
    signNext m ks = andThen (next ks) (λ o → done Signed (option none (λ p → unpack p (λ l kr → some (pack (pack (leafHeight l) (sign l m)) kr))) o))
