# leaf-lang diagnostics

`langc` stops at the first error. `langc check` can also give one warning,
`W_NONCANON`. In text mode, each diagnostic is one line on stderr:
`langc: CODE: NAME: message`. NAME is the definition, or `-` when there is
no definition. With `langc check --json FILE`, each diagnostic is one JSON
object on one line of stdout. The error comes first (at most one), then the
warning (at most one), then the progress object. The exit code is the same
in the two modes: 0 ok, 1 refused, 2 usage or IO. A warning does not change
the exit code.

## The JSON record

    {"code":"CODE","severity":"error","span":{"file":"F","line":1,"col":1,"end_line":1,"end_col":2},"expected":null,"found":null,"fix":null,"retry":true,"message":"..."}

- `severity`: `"warning"` for a code that starts with `W_`, else `"error"`.
- `span`: lines and columns start at 1. A column counts code points, not
  bytes. The value is `null` when the diagnostic has no position.
- `expected`, `found`, `fix`: strings, or `null` when the code does not set
  them. The strings are JSON-escaped: valid UTF-8 stays raw, and a control
  byte or an invalid UTF-8 byte is `\u00XX`.
- `retry`: `false` for `OOM`, `IO`, `IO_SIZE`, `USAGE`, `INTERNAL` and each
  `EVAL_` code (a change to the program text cannot remove them), `true`
  for all other codes.
- `message`: the last part of the text-mode line `langc: CODE: NAME: message`.

The S1 reading of the port sources (agent-lang 06854bf, dil-lang 9e57528)
found nothing to port (S1 HANDOFF). docs/STATUS.md records the read.

## The progress object

    {"parsed":"3 of 3","typed":"2 of 3","affine":"0 of 0","laws":"0 of 0"}

- m (the second number) is the number of program declarations. After a parse
  error, m is the complete declarations plus the one that failed. After a lex
  error, m is 0.
- `parsed`: the declarations that parse. `typed`: the declarations that
  type-check before the first check error (all m when the program is ok).
- `affine` and `laws` stay `"0 of 0"` until S3 and S4.

## Codes

The gate (test/gate.sh) compares this list with the codes in src/. Each code
has the fixture test/json/CODE.lang, or its row gives `no fixture:` and a
reason. test/json/expect.txt holds the exact JSON lines of each fixture.

| Code | Severity | Stage | Meaning | Fixture |
|---|---|---|---|---|
| `LEX_CHAR` | error | lex | A character that starts no token: a non-ASCII character other than `→`, `λ`, `×` and `ℕ`, or an ASCII character that the lexer does not use. | test/json/LEX_CHAR.lang |
| `LEX_NAT_RANGE` | error | lex | A numeral larger than 2^64-1. | test/json/LEX_NAT_RANGE.lang |
| `PARSE_EXPECT` | error | parse | A token that the grammar does not permit at that position. | test/json/PARSE_EXPECT.lang |
| `PARSE_DEPTH` | error | parse | The terms nest deeper than the parser limit. | test/json/PARSE_DEPTH.lang |
| `PARSE_CLAUSE_NAME` | error | parse | A clause name is not the name of the signature above it, or a clause has no signature. | test/json/PARSE_CLAUSE_NAME.lang |
| `PARSE_CTOR_RESULT` | error | parse | A constructor type does not end in the data type applied to its parameters in order, or a field has no name. | test/json/PARSE_CTOR_RESULT.lang |
| `REFUSE_CELL` | error | check | A domain codata is not a cocell signature (one type parameter, exactly one observation, each other line `D A` or `E → D A` with `E` closed, the label universe at most the parameter universe), or a domain family with a type parameter is not a cell signature: a parameter is not a universe, a type parameter has no generator or two, or a field is not a type parameter, closed data or the family at its own parameters. | no fixture: a program gets `REFUSE_DATA` first; the test/fronttool.c domain regressions check it |
| `REFUSE_DATA` | error | check | A program declares a data or codata type. Only the domain can declare them. | test/json/REFUSE_DATA.lang |
| `REFUSE_AXIOM` | error | check | A program declares a postulate. | test/json/REFUSE_AXIOM.lang |
| `REFUSE_REC` | error | check | A `rec` declaration, or a definition that refers to itself or to a later definition (rule R3). Use `fold`. | test/json/REFUSE_REC.lang |
| `REFUSE_AFFINE_FIX` | error | check | A `fold` step, or the case of a constructor with a recursive field, that uses a 1-variable. The step runs one time for each layer, so it uses the variable at omega (R2). Use the variable in the `z` arm, in a case with no recursive field, or outside the `fold`. | test/json/REFUSE_AFFINE_FIX.lang |
| `REFUSE_AFFINE_OMEGA` | error | check | A binder of a resource type (`Leaf`, or a type with `Leaf` in a run-time position) at omega, or a 1-variable used at omega: an argument at an omega binder, or a closure that captures it and goes to one, for example a `D.corec` step (the generated corecursor binders are omega, O-U5-1 (a)). Bind it at 1 (`@1` or `⊸`) or at 0 (`@0`). | test/json/REFUSE_AFFINE_OMEGA.lang |
| `REFUSE_AFFINE_PATH` | error | check | A 1-variable free under an interval binder at 1: in `λ i →` or `refl`, in the derived `symm`, `trans`, `cong` or `transport`, or in `transp`, `hcomp`, `Glue`, `glue` or `unglue`. Both end points of the path hold it. Keep the variable out of the path, or bind it at 0 (`@0`). A law is checked at 0 and uses nothing. | test/json/REFUSE_AFFINE_PATH.lang |
| `REFUSE_AFFINE_REUSE` | error | check | A variable used more times than its multiplicity allows on one evaluation path: a 1-variable used two times, or a 0-variable at run time (Q-S3-4 (a)). Use a 1-variable one time. A type position uses nothing. | test/json/REFUSE_AFFINE_REUSE.lang |
| `REFUSE_SEALED` | error | check | A term that opens or builds a `Keystore`: a line of a codata (the observation `head` or the cofield `tail` of `Stream`) or the private corecursor `D.corec`, at a type argument with a resource sort in it (`Keystore` is `Stream (Option Leaf)`, Q-S3-7 (a)). The `Keystore` is sealed. Only the core opens it (S5 `next`). | test/json/REFUSE_SEALED.lang |
| `REFUSE_NAME` | error | check | A declaration uses a core name, a domain name, a reserved word or a name that is already declared. | test/json/REFUSE_NAME.lang |
| `TYPE_REFL` | error | check | `refl` where the two sides do not have the same normal form. The message gives the two normal forms. | test/json/TYPE_REFL.lang |
| `TYPE_MISMATCH` | error | check | A term does not have the type that its position needs. | test/json/TYPE_MISMATCH.lang |
| `NAME_UNKNOWN` | error | check | A name that is not in scope. The span is the name. For a kit name that S1 or S2 removed, the message says what to use. `fix` is the new name for `Flag` (`Bool`), `flagYes` (`true`), `flagNo` (`false`), `pair` (`pack`), `first` (`witness`), `second` (`payload`) and `Eq` (`Path`). `fix` is `null` for `Prod`, `List`, `nil`, `cons`, `filter` and `unfold`, because a name cannot replace them. | test/json/NAME_UNKNOWN.lang |
| `TYPE_UNIVERSE` | error | check | A universe level that is out of range. | test/json/TYPE_UNIVERSE.lang |
| `TYPE_INFER` | error | check | The checker cannot find the type of a term at that position. Add a type: an annotated binder or a signature. | test/json/TYPE_INFER.lang |
| `TYPE_ARITY` | error | check | A term has the wrong number of arguments. | test/json/TYPE_ARITY.lang |
| `TYPE_NOT_FUNCTION` | error | check | An application of a term that is not a function. | test/json/TYPE_NOT_FUNCTION.lang |
| `TYPE_PRETYPE` | error | check | A pretype where the checker needs a type in a universe: the type of a definition, a field or a parameter, a part of a Σ type, or the type argument of `Path` or `Option`. The pretypes are `I` and the function types over `I`. Their sort is SSet, which is in no universe. | test/json/TYPE_PRETYPE.lang |
| `TYPE_ENDPOINT` | error | check | A path `λ i → t` where `t` at `i0` or at `i1` does not have the same normal form as the end that the `Path` or `PathP` type gives. The message gives the two normal forms. | test/json/TYPE_ENDPOINT.lang |
| `TYPE_SYSTEM` | error | check | A system `[ φ ↦ u , ... ]` that does not agree with its `Partial ψ A` type: the join of the cell faces is not `ψ`, two branches do not have the same normal form where their faces meet, a face overlap is not in the face bounds (16 meets of 8 literals), or the system has more than 16 cells. The message gives the faces and the two normal forms. | test/json/TYPE_SYSTEM.lang |
| `TYPE_LINE` | error | check | A `transp A r u` where the line `A` is not constant where `r` holds. The checker compares `A i` with `A i0` on each meet of `r` and of the faces of the enclosing system cells. An atom in a meet stays free, so the test is conservative there. A face that is not in the face bounds needs a constant line. The message gives the line. | test/json/TYPE_LINE.lang |
| `GLUE_EQUIV` | error | check | A branch of `Glue A [ φ ↦ pack T e , ... ]` that is not `pack T e` with `T : Type 0` and `e : Equiv T A`. The message gives the code and the message of the failure in the branch. | test/json/GLUE_EQUIV.lang |
| `GLUE_COVER` | error | check | A `glue [ φ ↦ t , ... ] a` whose faces do not join to the face of its type `Glue A [ ... ]` (with the faces of the enclosing system cells). The message gives the two faces. | test/json/GLUE_COVER.lang |
| `GLUE_BOUNDARY` | error | check | A `glue [ φ ↦ t , ... ] a` where, on a face φ, `unglue t` (the function of the equivalence of that branch, applied to `t`) does not have the normal form of `a`. The message gives the face and the two normal forms. | test/json/GLUE_BOUNDARY.lang |
| `UNGLUE_TYPE` | error | check | An `unglue g` where the type of `g` is not a type `Glue A [ ... ]`. At a face that holds, the Glue type is `T`, so `unglue` there is refused too. The message gives the type of `g`. | test/json/UNGLUE_TYPE.lang |
| `W_NONCANON` | warning | check | The first declaration (or the tail of the file) whose text is not the `langc fmt` text. `found` is the input text; `expected` and `fix` are the canonical text. | test/json/W_NONCANON.lang |
| `REFUSE_RUNTIME_CLOSURE` | error | lowering | A function value must exist at run time. The Wasm target cannot hold one (HOST-LIMIT). | no fixture: `langc ir` and `langc build` have no JSON mode; test/gate.sh checks test/ir/closure.lang |
| `REFUSE_RUNTIME_TREE` | error | lowering | A fold over a tree must run at run time. The Wasm target cannot run it (HOST-LIMIT). | no fixture: `langc ir` and `langc build` have no JSON mode; test/gate.sh checks test/ir/tree.lang |
| `REFUSE_RUNTIME_PATH` | error | lowering | An interval point or a path value must exist at run time, for example in the state of a fold. A path is a proof. It has no run-time form. | no fixture: `langc ir` and `langc build` have no JSON mode; test/gate.sh checks test/ir/path.lang |
| `EVAL_ARGS` | error | eval | The arguments of `langc eval` do not agree with the entry (count or range). | no fixture: `langc eval` has no JSON mode; test/gate.sh checks it (`eval_refused`) |
| `EVAL_ENTRY` | error | eval | `langc eval` names no definition, or the entry does not compute a number. | no fixture: `langc eval` has no JSON mode; test/gate.sh checks it (`eval_refused`) |
| `EVAL_PRINT` | error | eval | The value is too large to print. | no fixture: `langc eval` has no JSON mode; test/review.sh checks large.lang |
| `EVAL_OVERFLOW` | error | eval | A ℕ operation overflows past 2^64-1. | no fixture: `langc eval` has no JSON mode; the `trap` lines of test/eval/expect.txt check it |
| `EVAL_DEPTH` | error | eval, lowering | The evaluation or the lowering nests deeper than 2000 levels. | no fixture: no S1 `check` input reaches this limit, and `langc eval` has no JSON mode |
| `EVAL_FUEL` | error | eval | The evaluation takes more than 20000000 steps. | no fixture: a fixture needs more than 20000000 steps, which is too slow for the gate |
| `INTERNAL` | error | check, eval, lowering | An internal rule of `langc` fails. This is a defect in `langc`. | no fixture: no valid input reaches it |
| `IO` | error | all | `langc` cannot read the program file or write its output. | no fixture: the cause is the file system, not the program text; test/gate.sh checks a missing file (exit 2) |
| `IO_SIZE` | error | all | The program file is larger than the size limit. | no fixture: the fixture would be larger than the size limit |
| `OOM` | error | all | `langc` has no free memory. | no fixture: no program text causes it reliably |
| `USAGE` | error | all | The command line is not valid. | no fixture: the cause is the command line, not the program text; test/gate.sh checks exit 2 |
