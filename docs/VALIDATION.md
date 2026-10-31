# Validation

Date: 2026-10-30. tcc: 0.9.28rc (2026-09-04 mob@0fb54300, AArch64
Darwin) builds the host executable and the review tools. Apple clang:
21.0.0 (clang-2100.0.123.102), as `cc`, runs the `check-clang` syntax
pass. Host executable: `build/langc` at 2fa3ea9. The working tree had
the S10 changes (docs only), so `build/version.c` has the dirty flag 1,
and the gate rows that are keyed on that flag (the `langc rebuild` rows)
ran with the flag 1.

`make -B check` (serial, no `-j`) passes. It builds `build/langc` and
the review tools (`build/fronttool`, `build/verifier-review`,
`build/zkml-review`, `build/rebuild-review`, `build/mcp-review`,
`build/tokcount`) with tcc, warnings as errors. Then `check-clang` runs
`cc -fsyntax-only` with `-Wextra`, `-Wswitch-enum` and warnings as
errors on the same sources. Then the gate runs `test/gate.sh`,
`build/mcp-review`, `test/review.sh`, `build/fronttool`,
`build/zkml-review` and `build/rebuild-review`. The counted rows are:
examples 10 checked, 0 failed; graded 5 checked; parse refusals 12
checked; check refusals 44 checked; evals 205 checked; poseidon2 evals
7 checked; zkml 1 checked; service 10 checked; verifier ABI regressions
9 passed; explain 4 checked; manifests 23 checked; rebuilds 9 checked;
mcp 32 checked (17 fixed, 15 examples); zkml writer regressions 7
passed; rebuild regressions 8 passed. The gate runs 386 tests with zero
failures: 386 is the sum of the 15 numbers in the "N checked" and "N
passed" rows above (Q-S10-5 (a)). The wall time of the serial gate was
182 seconds (`date +%s` before and after).

Rows with a number that are not in the 386: zkml known answers 6 evals;
ir 14 lowered; wasm 19 modules valid; differential 951 cases, 0
mismatches; signer 2 modules valid, 95 rows; fmt 116 canonical, 6
non-canonical pairs; json 33 fixtures, diagnostics 57 codes (31 with a
fixture, 26 with no fixture); gate 0 failures; the 4 rebuild rows
(`review.wasm`, `review.hex`, `signer.wasm` and `review.zkml` match);
the `langc` `EVAL_ENTRY` refusal row of `build/rebuild-review`. Rows
with no number: token baseline (counts match), token ratio (half up
checked), and the review, domain and signer persistence regressions
(passed).

`make gas` is not part of `make check` (Q-S6-8 (b)). Under Q-S10-4 (a),
one `make gas` run at the heights H = 4, 10 and 16 passes (EXIT 0, wall
time 707 seconds; the H = 16 signer takes 692 seconds). Each row has 16
positions and gives min/mean/max:

| H | Entry | Bytes | Execution | Standard | Floor | Tx |
|---|---|---|---|---|---|---|
| 4 | shim | 2376 | 80823/90970.5/101118 | 37368/37440.0/37500 | 93420/93600.0/93750 | 139263/149410.5/159582 |
| 4 | entry 2 | 2852 | 81111/91258.5/101406 | 39452/39524.0/39584 | 98630/98810.0/98960 | 141635/151782.5/161954 |
| 10 | shim | 2568 | 85897/95237.3/106192 | 40452/40533.8/40596 | 101130/101334.4/101490 | 147373/156771.1/167704 |
| 10 | entry 2 | 3044 | 86185/95525.3/106480 | 42536/42617.8/42680 | 106340/106544.4/106700 | 149745/159143.1/170076 |
| 16 | shim | 2760 | 87281/96044.8/111266 | 43512/43600.5/43656 | 108780/109001.2/109140 | 151889/160645.2/175778 |
| 16 | entry 2 | 3236 | 87569/96332.8/111554 | 45596/45684.5/45740 | 113990/114211.2/114350 | 154261/163017.2/178150 |

The 3 shim rows are equal to the S6 rows. In the 3 entry 2 rows, bytes,
standard and floor are equal to the S6 rows; execution and tx are the
S6 values + 160 at min, mean and max. The + 160 is the bounds checks of
the staged S6 review (Q-S10-6 (a)). In execution, entry 2 is the shim +
288 gas. The "### Gas rows" section of `docs/STATUS.md` has the same 6
rows.

Each slice paragraph below gives the gate counts at the close of the
slice (the slice row of `docs/PLAN.md` section 9) and the mutant counts
(`docs/STATUS.md`, "## Mutant summary").

The S1 review (the surface) adds the cases in `test/fmt` and
`test/json`. They check the Agda-like Unicode surface, named steps as
top-level definitions, `langc fmt` (round trip and idempotence) and
`check --json` with progress counts. At the S1 close the gate had parse
refusals 9, check refusals 18, evals 30, fmt 50 canonical and 3
non-canonical pairs, json 20 fixtures and diagnostics 31 codes (18 with
a fixture, 13 with no fixture). `W_NONCANON` marks a non-canonical
form, and the surface drops the refused kit names. The S1 row names the
mutants M1 to M6, all killed.

The S2 review (the kernel port) adds cases in `test/check`,
`test/eval` and `test/json`. They check Path, Sigma, Pi, U, `I`,
`transp`, `hcomp`, `Glue` and `ua`, `Sum` in the domain, the `codata`
keyword with its shape check, and the domain definitions `lt` and
`Fin`. At the S2 close the gate had parse refusals 10, check refusals
32, evals 64, fmt 78 canonical and 5 non-canonical pairs, json 28
fixtures and diagnostics 41 codes (26 with a fixture, 15 with no
fixture). Refusals cover the cell criterion (`REFUSE_CELL`). Mutants: 6
of 6 killed (C1, C1w, C2, C3, U6c2b, U7-M1).

The S3 review (the affine layer) adds cases in `test/check` and
`test/json`. They check multiplicities, `-o`, the resource sorts, the
sealed `Leaf` and `Keystore`, and `Durable` (a spine: one recursive
field in each constructor). At the S3 close the gate had parse refusals
12, check refusals 35, evals 70, fmt 91 canonical and 6 non-canonical
pairs, json 33 fixtures and diagnostics 46 codes (31 with a fixture, 15
with no fixture). Refusals cover the five S3 refusal codes, with one
JSON fixture for each code. Mutants: 14 of 14 killed (the five S3
refusals and the two `Durable` branches).

The S4 review (the hash layer) adds cases in `test/eval` (with
`test/eval/laws.lang`) and `test/check`. They check `Digest`, the
opaque hash operations, WOTS+, the L-tree, Merkle, `sign` and `verify`
with the reference Keccak in `langc eval`: own vectors, a cross-check of
each hash operation with `cast keccak` (Q-S4-1 (c)), and L1 at h = 4
for i = 0 to 15. At the S4 close the gate had check refusals 38 and
evals 142. Refusals cover L6: three `check` rows refuse with
`TYPE_REFL`. Mutants: 10 of 10 killed.

The S5 review (the keystore and the Wasm signer) adds cases in
`test/wasm`, `test/signer-review.mjs` and `test/ir/hashkat.lang`. They
check `keygen`, `publicKey`, `next` and `commit`, and the Wasm signer
(Keccak in the module, the `commit` import, the EIP-8310 JSON in the
harness): Keccak known answers in the module; the h = 4 signer against
`langc eval` (`rootSum`, `sigSum` and `verify` at the positions 0, 7
and 15); at h = 10 the module signs and verifies; L1 (keystore form)
and L3 to L5 as eval rows at h = 4. At the S5 close the gate had check
refusals 43, evals 162, ir 14 lowered, wasm 19 modules valid,
differential 951 cases with 0 mismatches, and signer 2 modules valid
with 28 rows. Refusals cover a reset (`REFUSE_HIGH_WATER`) and the L3
to L5 refusal fixtures (Q-S5-10). Mutants: 11 on `src/wasm.c`, 10
killed; M6 (height) survives (Q-S5-40 (a)).

The S6 review (the EVM account verifier) adds
`test/verifier-review.c` and `test/gas.sh`. They check the ERC-4337
entry, the EIP-8141 entry with the shim, and the nonce binding L2:
`langc verifier` at h = 4 in `evm run`, where the shim and entry 2
accept the 16 positions; the shim build is the real entry 1 build with
the 5 table parts in the shim form (a byte compare; no EVM has
EIP-8141, O2); L2 as the `accept` eval rows. At the S6 close the gate
had evals 195, signer 2 modules valid with 95 rows, and verifier ABI
regressions 9 passed. Refusals cover a wrong nonce, a wrong message, a
wrong root and a changed byte: the shim reverts, and entry 2 gives the
word 1. Mutants: 8 of 8 killed on `src/evm.c`.

The S7 review (the Poseidon2 binding and the zkml writer) adds cases in
`test/zkml` and `test/zkml-review.c`, and `build/fronttool` checks the
Poseidon2 vectors. They check Poseidon2 (the field of Q10) in `langc
eval`, `aggregate` and L7: poseidon2 evals 7 (the L1 rows at h = 4, L7
at N = 1, the `--hash sha3` USAGE row); zkml 1 checked (zkml-lang
accepts the `langc zkml` output of the L7 entry at N = 1); zkml known
answers 6 evals (Q-S7-14 (a)); in `build/fronttool`, the Poseidon2
vectors of Plonky3 and the 5.4 sponge digests on 96, 128 and 160 bytes.
At the S7 close the gate had check refusals 44 and evals 205. Mutants:
12 of 12 killed on `zkml/xmss.lang`, `src/zkml.c`,
`src/front/poseidon2.c` and `src/main.c`, 2 of them by a data word
check out of `make check`.

The S8 review (the boundary exclusion) adds `test/rebuild-review.c` and
the service mode of `test/run-wasm.mjs`. They check that the signer
service takes programs only, the output manifests and the rebuild
compare: service 10 checked (the good request and 9 bad requests,
Q-S8-1 (a), Q-S8-6 (a)); manifests 23 checked (`OUT.manifest.json` of
each writer, Q-S8-2 (a)); rebuilds 9 checked (the `langc rebuild` rows,
keyed on the dirty flag, Q-S8-5 (a), Q-S8-7); rebuild regressions 8
passed (the clean-tree rows with a clean version fixture); zkml writer
regressions 7 passed. Refusals cover the 9 bad service requests.
Mutants: 19 on `test/run-wasm.mjs`, `src/main.c` and
`src/manifest.c`: 16 killed, 3 EQUIVALENT (the service mutants S8 and
S10, the ws check).

The S9 review (the agent kit) adds `test/mcp-review.c`, `test/mcp`,
`test/explain` and `test/tokcount.c`. They check `SKILL.md`, the
grammar card, the 5 graded examples (graded 5 checked, Q-S9-4 (a)),
`langc explain` (explain 4 checked, Q-S9-3 (a), Q-S9-9 (a)), `langc
mcp` (mcp 32 checked: 17 fixed, 15 examples; `build/mcp-review`, the
MCP stdio test, Q-S9-1 (a), Q-S9-2 (a), Q-S9-6 (a)),
`docs/TOKEN-BASELINE.md` (token baseline: counts match, Q-S9-5 (a),
Q-S9-8 (a)) and the token ratio (half up checked). Mutants: 13 of 13
killed on `src/mcp.c`, the explain code of `src/main.c` and
`test/tokcount.c` (T4 after the token ratio row).

Mutants over S2 to S9: 93, with 89 killed, 3 EQUIVALENT and 1 survivor.
