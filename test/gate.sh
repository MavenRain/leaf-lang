#!/bin/sh
# The kit gate. `make check` runs it from the kit directory after the build.
set -eu
fail=0
count=0
for prog in examples/*.lang; do
  out=$(build/langc check "$prog" 2>&1) || true
  count=$((count + 1))
  if [ "$out" != "ok" ]; then
    echo "FAIL $prog: $out"
    fail=$((fail + 1))
  fi
done
echo "examples: $count checked, $fail failed"

tmp=${TMPDIR:-/tmp}/langc-gate.$$
mkdir -p "$tmp"
awk 'BEGIN { printf "deep : ℕ\ndeep = " } { } END {
  for (i = 0; i < 1100; i++) printf "("; printf "1"; for (i = 0; i < 1100; i++) printf ")"; print "" }' </dev/null >"$tmp/deep.lang"

# refusals DIR LABEL: each line of DIR/expect.txt is `FILE CODE`. `langc check`
# must exit 1 and print `langc: CODE: ...` on stderr.
refusals() {
  checked=0
  while read -r file code; do
    path="$1/$file"
    [ "$file" = "deep.lang" ] && path="$tmp/deep.lang"
    status=0
    build/langc check "$path" 2>"$tmp/err" >/dev/null || status=$?
    first=$(head -n 1 "$tmp/err")
    checked=$((checked + 1))
    case "$status:$first" in
      "1:langc: $code: "*) ;;
      *) echo "FAIL $file: want exit 1 and $code, got exit $status: $first"; fail=$((fail + 1)) ;;
    esac
  done <"$1/expect.txt"
  echo "$2 refusals: $checked checked"
}
refusals test/parse parse
refusals test/check check

# A refl refusal shows both normal forms.
build/langc check test/check/bad-refl.lang 2>"$tmp/err" >/dev/null || true
if ! rg -q -F 'the left side normalizes to 1201, the right side normalizes to 1200' "$tmp/err"; then
  echo "FAIL bad-refl.lang: the message does not show both normal forms"
  fail=$((fail + 1))
fi

# Each line of test/eval/expect.txt is `FILE NAME [ARGS] => OUTPUT`. FILE is a
# name in examples/, or a path with a `/` and no `.lang` (test/eval/path).
# `langc eval` must exit 0 and print OUTPUT. A trap also prints
# `langc: EVAL_OVERFLOW: NAME: ...` on stderr.
evals=0
while IFS= read -r line; do
  want=${line#* => }
  set -- ${line%% => *}
  file=$1
  name=$2
  shift 2
  status=0
  case "$file" in
    */*) src="$file.lang" ;;
    *) src="examples/$file.lang" ;;
  esac
  got=$(build/langc eval "$src" "$name" "$@" 2>"$tmp/err") || status=$?
  first=$(head -n 1 "$tmp/err")
  want_err=""
  [ "$want" = "trap" ] && want_err="langc: EVAL_OVERFLOW: $name: "
  evals=$((evals + 1))
  case "$status|$got|$first" in
    "0|$want|$want_err"*) ;;
    *) echo "FAIL eval $file $name: want $want, got exit $status: $got $first"; fail=$((fail + 1)) ;;
  esac
done <test/eval/expect.txt
echo "evals: $evals checked"

# eval_refused STATUS CODE NAME [ARGS]: `langc eval examples/entries.lang NAME
# ARGS` must exit STATUS and print `langc: CODE: NAME: ...` on stderr.
eval_refused() {
  want_status=$1
  code=$2
  shift 2
  status=0
  build/langc eval examples/entries.lang "$@" 2>"$tmp/err" >/dev/null || status=$?
  case "$status:$(head -n 1 "$tmp/err")" in
    "$want_status:langc: $code: $1: "*) ;;
    *) echo "FAIL eval $*: want exit $want_status and $code, got exit $status"; fail=$((fail + 1)) ;;
  esac
}
eval_refused 2 EVAL_ARGS addPrice 1
eval_refused 2 EVAL_ARGS addPrice x 1
eval_refused 2 EVAL_ARGS addPrice 18446744073709551616 1
eval_refused 2 EVAL_ARGS pick 2 4 9
eval_refused 1 EVAL_ENTRY nope

status=0
build/langc 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL usage: want exit 2, got $status"; fail=$((fail + 1)); }
status=0
build/langc check "$tmp/missing.lang" 2>/dev/null || status=$?
[ "$status" -eq 2 ] || { echo "FAIL missing file: want exit 2, got $status"; fail=$((fail + 1)); }

# The IR: each example lowers, with one func line for each entry.
irs=0
for f in examples/*.lang; do
  irs=$((irs + 1))
  status=0
  build/langc ir "$f" > "$tmp/ir.txt" 2> "$tmp/ir.err" || status=$?
  funcs=$(rg -c '^func ' "$tmp/ir.txt" || echo 0)
  entries=$(build/langc abi "$f" | rg -c '.' || echo 0)
  [ "$status" -eq 0 ] && [ "$funcs" -eq "$entries" ] || { echo "FAIL ir $f: exit $status, $funcs funcs for $entries entries: $(head -n 1 "$tmp/ir.err")"; fail=$((fail + 1)); }
done
# ir_expect NAME: `langc ir test/ir/NAME.lang` is byte-equal to test/ir/NAME.ir.
ir_expect() {
  irs=$((irs + 1))
  status=0
  build/langc ir "test/ir/$1.lang" > "$tmp/ir.txt" 2> "$tmp/ir.err" || status=$?
  [ "$status" -eq 0 ] && cmp -s "$tmp/ir.txt" "test/ir/$1.ir" || { echo "FAIL ir $1.lang: exit $status, not test/ir/$1.ir: $(head -n 1 "$tmp/ir.err")"; fail=$((fail + 1)); }
}
ir_expect hash
ir_expect digit
ir_expect tweakflag
# ir_digest NAME: the sha-256 of `langc ir test/ir/NAME.lang` is the one line
# of test/ir/NAME.sha256 (Q-S5-24). A keystore IR is too large for an .ir
# file. The tool is `shasum -a 256` (macOS), else `sha256sum`.
ir_digest() {
  irs=$((irs + 1))
  status=0
  build/langc ir "test/ir/$1.lang" > "$tmp/ir.txt" 2> "$tmp/ir.err" || status=$?
  if command -v shasum > /dev/null; then
    sum=$(shasum -a 256 < "$tmp/ir.txt")
  else
    sum=$(sha256sum < "$tmp/ir.txt")
  fi
  [ "$status" -eq 0 ] && [ "${sum%% *}" = "$(cat "test/ir/$1.sha256")" ] || { echo "FAIL ir $1.lang: exit $status, sha-256 ${sum%% *} is not test/ir/$1.sha256: $(head -n 1 "$tmp/ir.err")"; fail=$((fail + 1)); }
}
ir_digest keystore
echo "ir: $irs lowered"
# ir_refused NAME CODE: `langc check test/ir/NAME.lang` accepts it, and
# `langc ir` must exit 1 and print `langc: CODE: ...` on stderr.
ir_refused() {
  status=0
  build/langc check "test/ir/$1.lang" > /dev/null || status=$?
  [ "$status" -eq 0 ] || { echo "FAIL $1.lang: check exit $status"; fail=$((fail + 1)); }
  status=0
  first=$(build/langc ir "test/ir/$1.lang" 2>&1 > /dev/null) || status=$?
  case "$status:$first" in
    "1:langc: $2: "*) ;;
    *) echo "FAIL $1.lang: want exit 1 and $2, got exit $status: $first"; fail=$((fail + 1)) ;;
  esac
}
ir_refused closure REFUSE_RUNTIME_CLOSURE
ir_refused keystore-seed REFUSE_RUNTIME_KEYSTORE
got=$(build/langc eval test/ir/lazy.lang lazy 3 2>&1) || true
[ "$got" = "3" ] || { echo "FAIL lazy.lang: want 3, got $got"; fail=$((fail + 1)); }
build/langc ir test/ir/lazy.lang > /dev/null || { echo "FAIL lazy.lang: ir"; fail=$((fail + 1)); }
for want in "stackSum 3:15" "stackTrap 0:7" "stackTrap 2:5" "stackDemand 0:trap" "stackDemand 2:trap"; do
  call=${want%%:*}
  got=$(build/langc eval test/ir/spine.lang ${call% *} ${call#* } 2>"$tmp/err") || true
  want_err=""
  [ "${want#*:}" = "trap" ] && want_err="langc: EVAL_OVERFLOW: ${call% *}: "
  case "$got|$(head -n 1 "$tmp/err")" in
    "${want#*:}|$want_err"*) ;;
    *) echo "FAIL spine.lang $call: want ${want#*:}, got $got"; fail=$((fail + 1)) ;;
  esac
done
build/langc ir test/ir/spine.lang > /dev/null || { echo "FAIL spine.lang: ir"; fail=$((fail + 1)); }
ir_refused tree REFUSE_RUNTIME_TREE
ir_refused path REFUSE_RUNTIME_PATH

# The Wasm target: each module builds and is valid. Then the differential: on
# the grid of test/grid.awk, node and `langc eval` give the same result for
# each entry (trap == trap). The stderr of eval is not merged into stdout.
wasms=0
cases=0
mismatches=0
for f in examples/*.lang test/ir/spine.lang test/ir/lazy.lang test/ir/proof-trap.lang test/ir/digit.lang \
  test/ir/hash.lang test/ir/keystore.lang test/ir/hashkat.lang test/ir/tweakflag.lang test/ir/hash-lazy.lang; do
  w="$tmp/$(basename "$f" .lang).wasm"
  wasms=$((wasms + 1))
  if ! build/langc build "$f" -o "$w" 2>"$tmp/w.err" || ! wasm-validate "$w" 2>>"$tmp/w.err" ||
    ! wasm-objdump -x "$w" >/dev/null 2>>"$tmp/w.err"; then
    echo "FAIL wasm $f: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  build/langc ir "$f" | awk -f test/grid.awk >"$tmp/cases.txt"
  : >"$tmp/want.txt"
  while read -r call; do
    status=0
    want=$(build/langc eval "$f" $call 2>/dev/null) || status=$?
    [ "$status" -eq 0 ] || want="eval-exit-$status"
    echo "$want" >>"$tmp/want.txt"
  done <"$tmp/cases.txt"
  if ! node test/run-wasm.mjs "$w" - <"$tmp/cases.txt" >"$tmp/got.txt" 2>"$tmp/w.err"; then
    echo "FAIL node $f: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
    continue
  fi
  cases=$((cases + $(rg -c '.' "$tmp/cases.txt" || echo 0)))
  paste -d '|' "$tmp/cases.txt" "$tmp/want.txt" "$tmp/got.txt" | awk -F '|' '$2 != $3' >"$tmp/bad.txt"
  bad=$(rg -c '.' "$tmp/bad.txt" || echo 0)
  mismatches=$((mismatches + bad))
  [ "$bad" -eq 0 ] || { echo "FAIL differential $f: $bad mismatches, first $(head -n 1 "$tmp/bad.txt")"; fail=$((fail + 1)); }
done
echo "wasm: $wasms modules valid"
echo "differential: $cases cases, $mismatches mismatches"
# A Bool argument larger than 1 traps (langc eval refuses it with EVAL_ARGS).
got=$(node test/run-wasm.mjs "$tmp/entries.wasm" pick 2 4 9 2>&1) || true
[ "$got" = "trap" ] || { echo "FAIL wasm pick 2 4 9: want trap, got $got"; fail=$((fail + 1)); }
# The signer module (docs/PLAN.md 5.2): `langc signer` writes one valid module
# for each height. run-wasm.mjs --key DIR runs keygen and next on it. Each
# next writes DIR/ID.state before the module goes on, and the harness refuses
# a position that goes back (REFUSE_HIGH_WATER, exit 1).
signers=0
signer_rows=0
for h in 4 10; do
  if build/langc signer --height "$h" -o "$tmp/signer$h.wasm" 2>"$tmp/w.err" &&
    wasm-validate "$tmp/signer$h.wasm" 2>>"$tmp/w.err" && wasm-objdump -x "$tmp/signer$h.wasm" >/dev/null 2>>"$tmp/w.err"; then
    signers=$((signers + 1))
  else
    echo "FAIL signer h = $h: $(head -n 1 "$tmp/w.err")"
    fail=$((fail + 1))
  fi
done
for bad in "--height 21 -o $tmp/bad.wasm" "-o $tmp/bad.wasm" "--height 4"; do
  signer_rows=$((signer_rows + 1))
  status=0
  build/langc signer $bad 2>/dev/null || status=$?
  [ "$status" -eq 2 ] || { echo "FAIL signer $bad: want exit 2, got $status"; fail=$((fail + 1)); }
done
# signer_run H DIR STATUS WANT LINE...: the LINEs on stdin of the h = H module
# with --key DIR give exit STATUS and the output lines WANT (joined by spaces).
signer_run() {
  signer_rows=$((signer_rows + 1))
  sh_h=$1
  sh_dir=$2
  sh_status=$3
  sh_want=$4
  shift 4
  printf '%s\n' "$@" >"$tmp/in.txt"
  status=0
  node test/run-wasm.mjs "$tmp/signer$sh_h.wasm" --key "$sh_dir" <"$tmp/in.txt" >"$tmp/out.txt" 2>"$tmp/w.err" || status=$?
  got=$(paste -s -d ' ' "$tmp/out.txt")
  [ "$status" -eq "$sh_status" ] && [ "$got" = "$sh_want" ] ||
    { echo "FAIL signer h = $sh_h '$1': want exit $sh_status '$sh_want', got exit $status '$got' $(head -n 1 "$tmp/w.err")"; fail=$((fail + 1)); }
}
# signer_has FILE TEXT: FILE has a line TEXT (after the indent, before a comma).
signer_has() {
  signer_rows=$((signer_rows + 1))
  rg -q "^ *$2,?\$" "$1" 2>/dev/null || { echo "FAIL signer $1: no line $2"; fail=$((fail + 1)); }
}
seeds="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f 202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f 404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"
mkdir "$tmp/k4" "$tmp/k10"
signer_run 4 "$tmp/k4" 0 "4096 $(seq 16 | paste -s -d ' ' -) 0" "keygen 1 $seeds" $(seq 17 | awk '{ print "next" }')
signer_has "$tmp/k4/1.state" 16
signer_has "$tmp/k4/1.json" '"uuid": "00000000-0000-8000-8000-000000000001"'
signer_has "$tmp/k4/1.json" '"tree_height": 4'
signer_has "$tmp/k4/1.json" '"lifetime_leaves": 16'
signer_has "$tmp/k4/1.json" '"high_water": 16'
signer_has "$tmp/k4/1.json" '"capacity": 0'
# A stale copy of the key (START 0) is refused, and the state stays.
signer_run 4 "$tmp/k4" 1 "4096 refused REFUSE_HIGH_WATER" "keygen 1 $seeds 0" next
signer_has "$tmp/k4/1.state" 16
signer_run 4 "$tmp/k4" 0 "trap" "keygen 1 $seeds 17"
signer_run 4 "$tmp/k4" 0 "4096 0" "keygen 1 $seeds 16" next
signer_has "$tmp/k4/1.state" 16
# The keygen root (D28 to D30): the harness rootSum of the h = 4 root is the
# eval rootSum of test/wasm/signer4.lang (the same seeds), and the JSON has
# the pubkey block.
root_sum=$(build/langc eval test/wasm/signer4.lang rootSum 2>&1) || true
mkdir "$tmp/k4r"
signer_run 4 "$tmp/k4r" 0 "4096 rootSum $root_sum" "keygen 1 $seeds" rootSum
signer_has "$tmp/k4r/1.json" '"pub_seed": "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"'
signer_run 10 "$tmp/k10" 0 "4096 1" "keygen 2 $seeds" next
signer_has "$tmp/k10/2.state" 1
signer_has "$tmp/k10/2.json" '"capacity": 1023'
# sign and verify (D36 to D39): at h = 4 the harness sigSum of the signature
# at the position P is the eval sumP of test/wasm/sign4.lang (the same seeds
# and M), and `verify M` gives the eval okP. A second sign of one leaf traps
# (L5), and `verify M2` gives the eval bad. At h = 10 there is no eval value
# (Q-S5-11 (a)), so only the last line is compared (D39).
# signer_last H DIR WANT LINE...: as signer_run with exit 0, but the last
# output line only is compared with WANT.
signer_last() {
  signer_rows=$((signer_rows + 1))
  sl_h=$1
  sl_dir=$2
  sl_want=$3
  shift 3
  printf '%s\n' "$@" >"$tmp/in.txt"
  status=0
  node test/run-wasm.mjs "$tmp/signer$sl_h.wasm" --key "$sl_dir" <"$tmp/in.txt" >"$tmp/out.txt" 2>"$tmp/w.err" || status=$?
  got=$(tail -n 1 "$tmp/out.txt")
  [ "$status" -eq 0 ] && [ "$got" = "$sl_want" ] ||
    { echo "FAIL signer h = $sl_h '$1': want exit 0 last line '$sl_want', got exit $status '$got' $(head -n 1 "$tmp/w.err")"; fail=$((fail + 1)); }
}
# sign_bool NAME: the eval Bool NAME of test/wasm/sign4.lang as true or false.
sign_bool() {
  case "$(build/langc eval test/wasm/sign4.lang "$1" 2>&1)" in
    1) echo true ;;
    0) echo false ;;
    *) echo "eval-error" ;;
  esac
}
msg=6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d
msg2=6e6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d
mkdir "$tmp/k4s"
sum0=$(build/langc eval test/wasm/sign4.lang sum0 2>&1) || true
sum7=$(build/langc eval test/wasm/sign4.lang sum7 2>&1) || true
sum15=$(build/langc eval test/wasm/sign4.lang sum15 2>&1) || true
signer_run 4 "$tmp/k4s" 0 "4096 1 sigSum $sum0 verify $(sign_bool ok0)" "keygen 3 $seeds 0" next "sign 1 $msg" "verify $msg"
signer_run 4 "$tmp/k4s" 0 "4096 8 sigSum $sum7 verify $(sign_bool ok7)" "keygen 4 $seeds 7" next "sign 8 $msg" "verify $msg"
signer_run 4 "$tmp/k4s" 0 "4096 16 sigSum $sum15 verify $(sign_bool ok15)" "keygen 5 $seeds 15" next "sign 16 $msg" "verify $msg"
signer_run 4 "$tmp/k4s" 0 "4096 1 sigSum $sum0 trap" "keygen 6 $seeds" next "sign 1 $msg" "sign 1 $msg"
signer_run 4 "$tmp/k4s" 0 "4096 8 sigSum $sum7 verify $(sign_bool bad)" "keygen 7 $seeds 7" next "sign 8 $msg" "verify $msg2"
for p in 0 511 1023; do
  signer_last 10 "$tmp/k10" "verify true" "keygen 3 $seeds $p" next "sign $((p + 1)) $msg" "verify $msg"
done
echo "signer: $signers modules valid, $signer_rows rows"
build/langc signer --height 1 -o "$tmp/signer-review.wasm"
node test/signer-review.mjs "$tmp/signer-review.wasm"
# The canonical printer. Each committed .lang file that parses is canonical:
# `langc fmt F` equals F byte for byte, so fmt is idempotent on it. The
# fronttool AST dump of F equals the dump of `fmt F` (the round trip). Each
# test/fmt/X.lang formats to test/fmt/X.canon (X.canon is in the canonical
# list), the two dumps are equal, and `langc check` gives W_NONCANON on
# X.lang and no warning on X.canon.
same_dump() {
  build/fronttool dump "$1" >"$tmp/d1.txt" 2>&1 && build/fronttool dump "$2" >"$tmp/d2.txt" 2>&1 &&
    cmp -s "$tmp/d1.txt" "$tmp/d2.txt"
}
canon=0
for f in domain/domain.lang examples/*.lang test/check/*.lang test/eval/*.lang test/ir/*.lang test/json/*.lang test/fmt/*.canon; do
  case "$f" in
    test/json/LEX_*.lang | test/json/PARSE_*.lang | test/json/W_NONCANON.lang) continue ;;
  esac
  canon=$((canon + 1))
  status=0
  build/langc fmt "$f" >"$tmp/fmt.out" 2>"$tmp/fmt.err" || status=$?
  if [ "$status" -ne 0 ] || ! cmp -s "$f" "$tmp/fmt.out"; then
    echo "FAIL fmt $f: not canonical, exit $status $(head -n 1 "$tmp/fmt.err")"
    fail=$((fail + 1))
  elif ! same_dump "$f" "$tmp/fmt.out"; then
    echo "FAIL round trip $f: the AST dump changes after fmt"
    fail=$((fail + 1))
  fi
done
pairs=0
for f in test/fmt/*.lang; do
  c="${f%.lang}.canon"
  pairs=$((pairs + 1))
  status=0
  build/langc fmt "$f" >"$tmp/fmt.out" 2>"$tmp/fmt.err" || status=$?
  if [ "$status" -ne 0 ] || ! cmp -s "$c" "$tmp/fmt.out"; then
    echo "FAIL fmt $f: the output is not $c, exit $status $(head -n 1 "$tmp/fmt.err")"
    fail=$((fail + 1))
  fi
  same_dump "$f" "$c" || { echo "FAIL round trip $f: the AST dump differs from $c"; fail=$((fail + 1)); }
  out=$(build/langc check "$f" 2>"$tmp/err") || true
  first=$(head -n 1 "$tmp/err")
  case "$out|$first" in
    "ok|langc: W_NONCANON: "*) ;;
    *) echo "FAIL check $f: want ok and W_NONCANON, got $out|$first"; fail=$((fail + 1)) ;;
  esac
  out=$(build/langc check "$c" 2>"$tmp/err") || true
  if [ "$out" != "ok" ] || [ -s "$tmp/err" ]; then
    echo "FAIL check $c: want ok and no warning, got $out|$(head -n 1 "$tmp/err")"
    fail=$((fail + 1))
  fi
done
echo "fmt: $canon canonical, $pairs non-canonical pairs"

# JSON shape: `langc check --json` on each test/json/*.lang gives the lines of
# test/json/expect.txt exactly (with the exit code and any stderr line).
jsons=0
for f in $(printf '%s\n' test/json/*.lang | LC_ALL=C sort); do
  jsons=$((jsons + 1))
  status=0
  build/langc check --json "$f" >"$tmp/j.out" 2>"$tmp/j.err" || status=$?
  echo "== $(basename "$f") exit $status"
  cat "$tmp/j.out"
  if [ -s "$tmp/j.err" ]; then echo "stderr: $(head -n 1 "$tmp/j.err")"; fi
done >"$tmp/json.txt"
if ! cmp -s test/json/expect.txt "$tmp/json.txt"; then
  echo "FAIL json: the output is not test/json/expect.txt: $(diff test/json/expect.txt "$tmp/json.txt" | rg '^[<>]' | head -n 2 | cut -c1-160 | tr '\n' ' ')"
  fail=$((fail + 1))
fi
# The progress line: typed "k of m" with k < m (a later declaration fails),
# and an all-pass file.
for want in 'progress-part:{"parsed":"3 of 3","typed":"2 of 3","affine":"2 of 3","laws":"0 of 0"}' \
  'progress-pass:{"parsed":"2 of 2","typed":"2 of 2","affine":"2 of 2","laws":"0 of 0"}'; do
  got=$(build/langc check --json "test/json/${want%%:*}.lang" 2>/dev/null | tail -n 1) || true
  [ "$got" = "${want#*:}" ] || { echo "FAIL progress ${want%%:*}: want ${want#*:}, got $got"; fail=$((fail + 1)); }
done

# docs/DIAGNOSTICS.md has one row for each code in src/ and in
# test/run-wasm.mjs. Each code has test/json/CODE.lang (its first JSON line
# has that code), or its row gives "no fixture:" and a reason; not the two.
rg -o -I -N -r '$1$2$3' '"((?:LEX|PARSE|REFUSE|TYPE|NAME|EVAL|GLUE|UNGLUE|W)_[A-Z_]+)"|"(OOM|IO|IO_SIZE|USAGE|INTERNAL)"|langc: ((?:EVAL|REFUSE|TYPE)_[A-Z_]+):' src test/run-wasm.mjs |
  LC_ALL=C sort -u >"$tmp/src-codes.txt"
rg -o -N -r '$1' '^\| `([A-Z_]+)` \|' docs/DIAGNOSTICS.md | LC_ALL=C sort -u >"$tmp/doc-codes.txt"
if ! cmp -s "$tmp/src-codes.txt" "$tmp/doc-codes.txt"; then
  echo "FAIL docs/DIAGNOSTICS.md: the code list is not the src/ list: $(diff "$tmp/src-codes.txt" "$tmp/doc-codes.txt" | rg '^[<>]' | tr '\n' ' ')"
  fail=$((fail + 1))
fi
codes=0
fixtures=0
reasons=0
while read -r code; do
  codes=$((codes + 1))
  row=$(rg "^\| \`$code\` \|" docs/DIAGNOSTICS.md) || row=""
  case "$row" in *"no fixture: "*) reason=1 ;; *) reason=0 ;; esac
  if [ -f "test/json/$code.lang" ] && [ "$reason" -eq 0 ]; then
    fixtures=$((fixtures + 1))
    first=$(build/langc check --json "test/json/$code.lang" 2>/dev/null | head -n 1) || true
    case "$first" in
      "{\"code\":\"$code\","*) ;;
      *) echo "FAIL test/json/$code.lang: the first JSON line is not code $code: $(echo "$first" | cut -c1-120)"; fail=$((fail + 1)) ;;
    esac
  elif [ ! -f "test/json/$code.lang" ] && [ "$reason" -eq 1 ]; then
    reasons=$((reasons + 1))
  else
    echo "FAIL $code: want test/json/$code.lang or a \"no fixture:\" reason in docs/DIAGNOSTICS.md, not the two"
    fail=$((fail + 1))
  fi
done <"$tmp/doc-codes.txt"
for f in $(printf '%s\n' test/json/*.lang | rg '^test/json/[A-Z_]+\.lang$'); do
  rg -q -x -F "$(basename "$f" .lang)" "$tmp/doc-codes.txt" || { echo "FAIL $f: the code is not in docs/DIAGNOSTICS.md"; fail=$((fail + 1)); }
done
echo "json: $jsons fixtures; diagnostics: $codes codes, $fixtures with a fixture, $reasons with no fixture"

rm -rf "$tmp"

# design/DESIGN.md is verbatim and can hold a dash (as bridge-lang, ruling 2026-10-09).
if rg -n -g '!design/DESIGN.md' '\x{2013}|\x{2014}' . >/dev/null; then
  echo "FAIL: an em-dash or en-dash is in the kit"
  fail=$((fail + 1))
fi
echo "gate: $fail failures"
[ "$fail" -eq 0 ]
