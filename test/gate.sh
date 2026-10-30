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

# keccak_of FILE: the keccak256 of the bytes of FILE (`cast keccak`: 0x, hex).
# cast reads 0x and the hex on stdin: as one argv word, the hex of a big OUT is
# too long ("Argument list too long").
keccak_of() {
  { printf 0x; od -An -v -tx1 "$1" | tr -d ' \n'; } | cast keccak
}

# manifest_ok OUT WORDS SOURCE [SOURCE2] (S8 D2): OUT and OUT.manifest.json
# exist; the manifest is one line with the keys in the D2a order; `command`
# is WORDS (no -o OUT pair); the sources are SOURCE (`<domain>` has the bytes
# of domain/domain.lang) and SOURCE2 with their keccak256; `commit` is 40 hex
# digits; `dirty` is true or false; the output size and keccak256 are those
# of OUT. It prints the cause on stderr.
manifests=0
manifest_ok() {
  if [ ! -f "$1" ] || [ ! -f "$1.manifest.json" ]; then
    echo "manifest: $1 or its manifest is missing" >&2
    return 1
  fi
  m_cmd=
  for m_word in $2; do
    m_cmd="$m_cmd${m_cmd:+,}\"$m_word\""
  done
  m_file=$3
  [ "$3" != "<domain>" ] || m_file=domain/domain.lang
  m_src="{\"path\":\"$3\",\"keccak256\":\"$(keccak_of "$m_file")\"}"
  if [ -n "${4:-}" ]; then
    m_src="$m_src,{\"path\":\"$4\",\"keccak256\":\"$(keccak_of "$4")\"}"
  fi
  m_head="{\"manifest\":1,\"command\":[$m_cmd],\"source\":[$m_src],\"langc\":{\"commit\":\""
  m_tail="\"output\":{\"path\":\"$1\",\"size\":$(wc -c <"$1" | tr -d ' '),\"keccak256\":\"$(keccak_of "$1")\"}}"
  m_text=$(cat "$1.manifest.json")
  case "$m_text" in
    "$m_head"*"},\"laws\":{\"parsed\":\""*"},$m_tail") ;;
    *)
      echo "manifest: $1.manifest.json is not the D2a line: $m_text" >&2
      return 1
      ;;
  esac
  if [ "$(wc -l <"$1.manifest.json" | tr -d ' ')" != 1 ] ||
    ! printf '%s\n' "$m_text" | rg -q '"langc":\{"commit":"[0-9a-f]{40}","dirty":(true|false)\},"laws"'; then
    echo "manifest: $1.manifest.json is not one line, or commit or dirty is wrong" >&2
    return 1
  fi
  manifests=$((manifests + 1))
}
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

# p2_eval NAME WANT [ARGS]: `langc eval --hash poseidon2 test/eval/laws.lang
# NAME ARGS` must exit 0 and print WANT. These are the L1 rows at h = 4 of
# test/eval/expect.txt (Q-S7-7). pk4 is computed (`pkFromSeeds`), so the
# rows hold under each binding.
p2evals=0
p2_eval() {
  name=$1
  want=$2
  shift 2
  status=0
  got=$(build/langc eval --hash poseidon2 test/eval/laws.lang "$name" "$@" 2>"$tmp/err") || status=$?
  p2evals=$((p2evals + 1))
  case "$status|$got" in
    "0|$want") ;;
    *) echo "FAIL eval --hash poseidon2 laws $name${*:+ $*}: want $want, got exit $status: $got $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
  esac
}
p2_eval l1Msg 1
p2_eval l1Zero 1
p2_eval l1Last 1
p2_eval l1Mid 1
# L7 at N = 1 (D6): the valid entry, and the bad signature at position 0.
p2_eval l7 1 1 1
p2_eval l7 0 1 0
# A hash that is not keccak or poseidon2 gives exit 2 and USAGE.
status=0
build/langc eval --hash sha3 examples/entries.lang addPrice 1 1 2>"$tmp/err" >/dev/null || status=$?
p2evals=$((p2evals + 1))
case "$status:$(head -n 1 "$tmp/err")" in
  "2:langc: USAGE: -: the eval hash 'sha3' "*) ;;
  *) echo "FAIL eval --hash sha3: want exit 2 and USAGE, got exit $status"; fail=$((fail + 1)) ;;
esac
echo "poseidon2 evals: $p2evals checked"

# zkml (Q-S7-13): when zkml-lang is built next to this repo, `langc zkml`
# writes the L7 entry at N = 1 (bad = 1: the valid signature) and the
# zkml-lang check of the output prints ok.
if [ -x ../zkml-lang/build/langc ]; then
  status=0
  build/langc zkml --count 1 test/eval/laws.lang l7Entry 1 -o "$tmp/zk.lang" 2>"$tmp/err" || status=$?
  [ "${status:-0}" -ne 0 ] ||
    manifest_ok "$tmp/zk.lang" "zkml --count 1 test/eval/laws.lang l7Entry 1" test/eval/laws.lang 2>"$tmp/m.err" ||
    { echo "FAIL zkml manifest: $(head -n 1 "$tmp/m.err")"; fail=$((fail + 1)); }
  got=
  [ "$status" -ne 0 ] || got=$(../zkml-lang/build/langc check "$tmp/zk.lang" 2>&1) || status=$?
  case "$status|$got" in
    "0|ok") echo "zkml: 1 checked" ;;
    *) echo "FAIL zkml laws l7Entry 1: want exit 0 and ok, got exit $status: $got $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
  esac
  # zk_eval NAME WANT [ARGS] (Q-S7-14): the head of the output (the lines up
  # to `-- The data defs:`: the Poseidon2 constants and zkml/xmss.lang) and
  # test/zkml/NAME.tail make one file. The zkml-lang eval of NAME ARGS must
  # exit 0 and print WANT. The L7 run row waits on O28 (the Z arena).
  awk 'NR > 1 && /^-- The data defs:/ { exit } NR > 1' "$tmp/zk.lang" >"$tmp/zkhead.lang" 2>"$tmp/err" || true
  zkevals=0
  zk_eval() {
    name=$1
    want=$2
    shift 2
    status=0
    cat "$tmp/zkhead.lang" "test/zkml/$name.tail" >"$tmp/zk-$name.lang" 2>"$tmp/err" || status=$?
    got=
    [ "$status" -ne 0 ] || got=$(../zkml-lang/build/langc eval "$tmp/zk-$name.lang" "$name" "$@" 2>"$tmp/err") || status=$?
    zkevals=$((zkevals + 1))
    case "$status|$got" in
      "0|$want") ;;
      *) echo "FAIL zkml eval $name${*:+ $*}: want $want, got exit $status: $got $(head -n 1 "$tmp/err")"; fail=$((fail + 1)) ;;
    esac
  }
  zk_eval kaPermZ 0 0
  zk_eval kaChain2Z 0 0
  zk_eval kaRoot2Z 0 0
  zk_eval kaDigDiff 0
  zk_eval kaDigSDiff 0
  zk_eval kaAggNil 1
  echo "zkml known answers: $zkevals evals"
else
  echo "zkml: skipped (no zkml-lang)"
fi

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
  if ! build/langc build "$f" -o "$w" 2>"$tmp/w.err" || ! manifest_ok "$w" "build $f" "$f" 2>>"$tmp/w.err" || ! wasm-validate "$w" 2>>"$tmp/w.err" ||
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
    manifest_ok "$tmp/signer$h.wasm" "signer --height $h" "<domain>" 2>>"$tmp/w.err" &&
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
# The signer service (S8 D4, docs/PLAN.md 5.2): --service DIR has the lines
# of --key DIR, refuses a raw `sign` line (REFUSE_RAW_SIGN) and signs only a
# request from a checked program (`request PROG ENTRY`, ENTRY of the domain
# type Request). The good request signs as `next` and `sign 1 M` of --key
# mode on a fresh DIR with the same key, and the same request again is
# refused (REFUSE_HIGH_WATER). A refused line leaves DIR/7.state the same.
# service_run DIR STATUS WANT LINE...: the LINEs on stdin of the h = 4 module
# with --service DIR give exit STATUS and the output lines WANT (joined by
# spaces). With STATUS 1, DIR/7.state also stays the same as $tmp/7.state.
service_rows=0
service_run() {
  service_rows=$((service_rows + 1))
  sv_dir=$1
  sv_status=$2
  sv_want=$3
  shift 3
  printf '%s\n' "$@" >"$tmp/in.txt"
  status=0
  node test/run-wasm.mjs "$tmp/signer4.wasm" --service "$sv_dir" <"$tmp/in.txt" >"$tmp/out.txt" 2>"$tmp/w.err" || status=$?
  got=$(paste -s -d ' ' "$tmp/out.txt")
  [ "$status" -eq "$sv_status" ] && [ "$got" = "$sv_want" ] ||
    { echo "FAIL service '$2': want exit $sv_status '$sv_want', got exit $status '$got' $(head -n 1 "$tmp/w.err")"; fail=$((fail + 1)); }
  [ "$sv_status" -eq 0 ] || cmp -s "$sv_dir/7.state" "$tmp/7.state" ||
    { echo "FAIL service '$2': $sv_dir/7.state changed"; fail=$((fail + 1)); }
}
req_m=000000000000000d000000000000000e000000000000000f0000000000000010
mkdir "$tmp/k4q" "$tmp/k4k"
req_sign=$(printf '%s\n' "keygen 7 $seeds" next "sign 1 $req_m" | node test/run-wasm.mjs "$tmp/signer4.wasm" --key "$tmp/k4k" 2>&1 | tail -n 1)
service_run "$tmp/k4q" 0 "4096 $req_sign verify true" "keygen 7 $seeds" "request test/wasm/request.lang req" "verify $req_m"
cp "$tmp/k4q/7.state" "$tmp/7.state" || : >"$tmp/7.state"
[ "$(cat "$tmp/7.state")" = 1 ] || { echo "FAIL service: 7.state is not 1 after the good request"; fail=$((fail + 1)); }
service_run "$tmp/k4q" 1 "4096 refused REFUSE_HIGH_WATER" "keygen 7 $seeds" "request test/wasm/request.lang req"
service_run "$tmp/k4q" 1 "4096 refused REFUSE_RAW_SIGN" "keygen 7 $seeds" "sign 1 $req_m"
service_run "$tmp/k4q" 1 "4096 refused REFUSE_AFFINE_REUSE" "keygen 7 $seeds" "request test/json/REFUSE_AFFINE_REUSE.lang dup"
service_run "$tmp/k4q" 1 "4096 refused REFUSE_SIGN_REQUEST" "keygen 7 $seeds" "request test/wasm/request-shape.lang shape"
service_run "$tmp/k4q" 1 "4096 refused REFUSE_SIGN_REQUEST" "keygen 7 $seeds" "request test/wasm/request.lang wrongKey"
# S8 D5: a LEAF above the next leaf index is refused. A LEAF past the 2^4
# leaves is refused: a keygen with START does not write DIR/7.state, so the
# row copies $tmp/7.state into DIR first. A PROG that langc cannot read
# stops the service with exit 2 (a bad request line).
service_run "$tmp/k4q" 1 "4096 refused REFUSE_SIGN_REQUEST" "keygen 7 $seeds" "request test/wasm/request.lang skipLeaf"
mkdir "$tmp/k4t"
cp "$tmp/7.state" "$tmp/k4t/7.state"
service_run "$tmp/k4t" 1 "4096 refused REFUSE_SIGN_REQUEST" "keygen 7 $seeds 16" "request test/wasm/request.lang pastHeight"
service_run "$tmp/k4q" 2 "4096" "keygen 7 $seeds" "request test/wasm/nosuch.lang req"
# An ENTRY of a function type to Request is refused: eval prints `fun n => `
# before the request text, and the request form must start at ^.
service_run "$tmp/k4q" 1 "4096 refused REFUSE_SIGN_REQUEST" "keygen 7 $seeds" "request test/wasm/request.lang fnReq"
echo "service: $service_rows checked"
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
# The EVM verifier in shim mode (S6 D13): the shim line writes the pk FILE and
# the shim input of the signature at position 0, `langc verifier --shim`
# builds the h = 4 runtime of that pk, and `evm run` returns the word 1. A
# wrong nonce and a wrong M revert. The userop lines write the entry 2 inputs
# (S6 D17) of the same signature: the nonce 0, 1 and 2^64, M and M2, and the
# missingAccountFunds 0 and 7.
signer_run 4 "$tmp/k4s" 0 "4096 1 sigSum $sum0 shim 2376 shim 2376 shim 2376 userop 2852 userop 2852 userop 2852 userop 2852 userop 2852" \
  "keygen 8 $seeds 0" next "sign 1 $msg" \
  "shim 0 $msg $tmp/pk4.bin $tmp/call-ok.hex" "shim 1 $msg $tmp/pk4.bin $tmp/call-nonce.hex" "shim 0 $msg2 $tmp/pk4.bin $tmp/call-m.hex" \
  "userop 0 $msg 0 $tmp/pk4.bin $tmp/call-u-ok.hex" "userop 0 $msg 7 $tmp/pk4.bin $tmp/call-u-funds.hex" \
  "userop 1 $msg 0 $tmp/pk4.bin $tmp/call-u-nonce.hex" "userop 18446744073709551616 $msg 0 $tmp/pk4.bin $tmp/call-u-key.hex" \
  "userop 0 $msg2 0 $tmp/pk4.bin $tmp/call-u-m.hex"
signer_rows=$((signer_rows + 1))
build/langc verifier --height 4 --pk "$tmp/pk4.bin" --shim -o "$tmp/rt4.hex" 2>"$tmp/w.err" ||
  { echo "FAIL verifier h = 4: $(head -n 1 "$tmp/w.err")"; fail=$((fail + 1)); }
# shim_run CALL WANT: `evm run` of the h = 4 runtime on the input file CALL
# prints the last line WANT.
shim_run() {
  signer_rows=$((signer_rows + 1))
  got=$(evm run --codefile "$tmp/rt4.hex" --inputfile "$1" 2>&1 | tail -n 1)
  [ "$got" = "$2" ] || { echo "FAIL shim $1: want '$2', got '$got'"; fail=$((fail + 1)); }
}
shim_run "$tmp/call-ok.hex" "0x$(printf '%063d' 0)1"
shim_run "$tmp/call-nonce.hex" " error: execution reverted"
shim_run "$tmp/call-m.hex" " error: execution reverted"
# The real build (S6 D16.5): `langc verifier` with no --shim builds the same
# pk at h = 4. Each of the 5 table parts in the real form (the sig length
# 2312 is 0x0908) is in it once, and with the shim form in their place it
# is the shim build: the same length and the same bytes.
{ build/langc verifier --height 4 --pk "$tmp/pk4.bin" -o "$tmp/real4.hex" 2>"$tmp/w.err" &&
  manifest_ok "$tmp/real4.hex" "verifier --height 4 --pk $tmp/pk4.bin" "<domain>" "$tmp/pk4.bin" 2>>"$tmp/w.err"; } ||
  { echo "FAIL verifier real h = 4: $(head -n 1 "$tmp/w.err")"; fail=$((fail + 1)); }
real_hex=$(cat "$tmp/real4.hex")
shim_hex=$(cat "$tmp/rt4.hex")
# real_part REAL SHIM: REAL is in the real build once; it becomes SHIM.
real_part() {
  signer_rows=$((signer_rows + 1))
  after=${real_hex#*"$1"}
  { [ "$after" != "$real_hex" ] && [ "${after#*"$1"}" = "$after" ]; } ||
    { echo "FAIL real part $1: want it once"; fail=$((fail + 1)); return; }
  real_hex="${real_hex%%"$1"*}$2$after"
}
real_part 60035fb4 60403603
real_part 5f6109085f610c00b5 6109086040610c0037
real_part 6001b0 5b5f35
real_part 6008b0 602035
real_part 600360205faa 5b5b60205ff3
signer_rows=$((signer_rows + 1))
[ "$real_hex" = "$shim_hex" ] || { echo "FAIL real h = 4: not the shim build with the 5 parts"; fail=$((fail + 1)); }
# Entry 2 (S6 D17, ERC-4337 v0.7): `langc verifier --entry 4337` builds the
# h = 4 runtime of the same pk. From the v0.7 EntryPoint, the signature at
# position 0 returns the word 0 (also with missingAccountFunds 7: the CALL
# fails, no balance). A wrong nonce sequence, the nonce key 1 and a wrong
# userOpHash return the word 1 (Q-S6-3 (a)). Another caller and a wrong
# selector revert. --entry 4337 refuses --shim.
build/langc verifier --height 4 --pk "$tmp/pk4.bin" --entry 4337 -o "$tmp/e2-4.hex" 2>"$tmp/w.err" ||
  { echo "FAIL verifier entry 2 h = 4: $(head -n 1 "$tmp/w.err")"; fail=$((fail + 1)); }
entry_point=0x0000000071727de22e5e9d8baf0edac6f37da032
# entry2_run CALL SENDER WANT: `evm run` of the entry 2 runtime on the input
# file CALL from SENDER prints the last line WANT.
entry2_run() {
  signer_rows=$((signer_rows + 1))
  got=$(evm run --codefile "$tmp/e2-4.hex" --inputfile "$1" --sender "$2" 2>&1 | tail -n 1)
  [ "$got" = "$3" ] || { echo "FAIL entry 2 $1: want '$3', got '$got'"; fail=$((fail + 1)); }
}
word0="0x$(printf '%064d' 0)"
word1="0x$(printf '%063d' 0)1"
entry2_run "$tmp/call-u-ok.hex" $entry_point "$word0"
entry2_run "$tmp/call-u-funds.hex" $entry_point "$word0"
entry2_run "$tmp/call-u-nonce.hex" $entry_point "$word1"
entry2_run "$tmp/call-u-key.hex" $entry_point "$word1"
entry2_run "$tmp/call-u-m.hex" $entry_point "$word1"
entry2_run "$tmp/call-u-ok.hex" 0x00000000000000000000000000000000000000aa " error: execution reverted"
{ printf 19822f7d; cut -c 9- "$tmp/call-u-ok.hex"; } >"$tmp/call-u-sel.hex"
entry2_run "$tmp/call-u-sel.hex" $entry_point " error: execution reverted"
signer_rows=$((signer_rows + 1))
! build/langc verifier --height 4 --pk "$tmp/pk4.bin" --entry 4337 --shim -o "$tmp/no.hex" 2>/dev/null ||
  { echo "FAIL verifier: --entry 4337 took --shim"; fail=$((fail + 1)); }
# The D10 rows (S6 U6) at h = 4. Each position 0 to 15 accepts: keygen 9
# writes the pk of keygen 8 again, signs the 16 positions, and the shim line
# and the userop line of each go to the two builds of that pk. The wrong
# nonce rows are above (call-nonce and call-u-nonce).
set -- "keygen 9 $seeds 0"
for p in $(seq 0 15); do
  set -- "$@" next "sign $((p + 1)) $msg" "shim $p $msg $tmp/pk4p.bin $tmp/call-p$p.hex" "userop $p $msg 0 $tmp/pk4p.bin $tmp/call-q$p.hex"
done
signer_last 4 "$tmp/k4s" "userop 2852" "$@"
signer_rows=$((signer_rows + 1))
cmp -s "$tmp/pk4.bin" "$tmp/pk4p.bin" || { echo "FAIL keygen 9: not the pk of keygen 8"; fail=$((fail + 1)); }
for p in $(seq 0 15); do
  shim_run "$tmp/call-p$p.hex" "$word1"
  entry2_run "$tmp/call-q$p.hex" $entry_point "$word0"
done
# A wrong root: the two builds of the pk with byte 0 (the root) changed. The
# signature at position 0 reverts (the shim) and returns the word 1 (entry 2).
b0=$(od -An -tu1 -N1 "$tmp/pk4.bin" | tr -d ' ')
{ printf "\\$(printf '%03o' $((b0 % 2 ? b0 - 1 : b0 + 1)))"; tail -c +2 "$tmp/pk4.bin"; } >"$tmp/pk4r.bin"
signer_rows=$((signer_rows + 1))
{ ! cmp -s "$tmp/pk4.bin" "$tmp/pk4r.bin" &&
  build/langc verifier --height 4 --pk "$tmp/pk4r.bin" --shim -o "$tmp/rt4r.hex" 2>"$tmp/w.err" &&
  build/langc verifier --height 4 --pk "$tmp/pk4r.bin" --entry 4337 -o "$tmp/e2r.hex" 2>"$tmp/w.err"; } ||
  { echo "FAIL verifier root h = 4: $(head -n 1 "$tmp/w.err")"; fail=$((fail + 1)); }
# code_run CODE CALL WANT: `evm run` of the runtime CODE on the input file
# CALL from the v0.7 EntryPoint prints the last line WANT.
code_run() {
  signer_rows=$((signer_rows + 1))
  got=$(evm run --codefile "$1" --inputfile "$2" --sender $entry_point 2>&1 | tail -n 1)
  [ "$got" = "$3" ] || { echo "FAIL evm $1 $2: want '$3', got '$got'"; fail=$((fail + 1)); }
}
code_run "$tmp/rt4r.hex" "$tmp/call-ok.hex" " error: execution reverted"
code_run "$tmp/e2r.hex" "$tmp/call-u-ok.hex" "$word1"
# The userop input of test/run-wasm.mjs: the tuple at t = 96, the signature
# offset word at byte 356 (hex digit 713) holds s = 384, the length word is
# at byte 484 and the sig at byte 516 (hex digit 1033). The shim input has
# the sig at hex digit 129.
signer_rows=$((signer_rows + 1))
sig_at=$(awk -v s="$(cut -c 129- "$tmp/call-ok.hex")" '{ print index($0, s) }' "$tmp/call-u-ok.hex")
[ "$sig_at" = 1033 ] || { echo "FAIL userop layout: the sig at hex digit '$sig_at', want 1033"; fail=$((fail + 1)); }
# hex_flip IN OUT N: the hex file IN with the hex digit N (from 1) changed
# goes to OUT.
hex_flip() {
  awk -v n="$3" '{ h = "0123456789abcdef"; d = index(h, substr($0, n, 1)) - 1; d = d % 2 ? d - 1 : d + 1; printf "%s", substr($0, 1, n - 1) substr(h, d + 1, 1) substr($0, n + 1) }' "$1" >"$2"
}
# One changed byte in r (sig byte 8), in the chain node 0 (sig byte 40) and
# in the auth node 0 (sig byte 2184) fails: the shim reverts, entry 2
# returns the word 1.
for at in 16 80 4368; do
  hex_flip "$tmp/call-ok.hex" "$tmp/call-f$at.hex" $((129 + at))
  shim_run "$tmp/call-f$at.hex" " error: execution reverted"
  hex_flip "$tmp/call-u-ok.hex" "$tmp/call-g$at.hex" $((1033 + at))
  entry2_run "$tmp/call-g$at.hex" $entry_point "$word1"
done
# A wrong length: the shim input one byte short reverts. Entry 2 returns the
# word 1 (Q-S6-6 (a)) for the length word 0x909 and for the signature offset
# word out of range (2^252 + 384: the length word reads as zero).
awk '{ printf "%s", substr($0, 1, length($0) - 2) }' "$tmp/call-ok.hex" >"$tmp/call-f-len.hex"
shim_run "$tmp/call-f-len.hex" " error: execution reverted"
hex_flip "$tmp/call-u-ok.hex" "$tmp/call-g-len.hex" 1032
entry2_run "$tmp/call-g-len.hex" $entry_point "$word1"
hex_flip "$tmp/call-u-ok.hex" "$tmp/call-g-off.hex" 713
entry2_run "$tmp/call-g-off.hex" $entry_point "$word1"
# A real signature whose final byte is zero distinguishes a truncated
# payload from CALLDATACOPY's zero padding. The C checks also exercise
# offset overflow and valid relocated tuple and signature tails.
zero_tail_seed=00000000000000000000000000000000000000000000000000000000000002c0
set -- $seeds
signer_last 4 "$tmp/k4s" "userop 2852" "keygen 10 $1 $2 $zero_tail_seed 0" next "sign 1 $msg" \
  "userop 0 $msg 0 $tmp/pk-zero-tail.bin $tmp/call-zero-tail.hex"
build/verifier-review "$tmp/pk-zero-tail.bin" "$tmp/call-zero-tail.hex" "$tmp" || fail=$((fail + 1))
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

# D2i: a byte below 0x20 in a word or a path gives USAGE (exit 2), and langc
# writes neither OUT nor the manifest.
ctl_out="$tmp/$(printf 'c\001')out.wasm"
ctl_status=0
build/langc signer --height 4 -o "$ctl_out" 2>/dev/null || ctl_status=$?
if [ "$ctl_status" -ne 2 ] || [ -e "$ctl_out" ] || [ -e "$ctl_out.manifest.json" ]; then
  echo "FAIL manifest: a control byte in OUT gives exit $ctl_status, or a file is left"
  fail=$((fail + 1))
fi
if ! command -v cast >/dev/null; then
  echo "FAIL manifest: cast is not on the PATH"
  fail=$((fail + 1))
fi
echo "manifests: $manifests checked"

# D3f (Q-S8-5 (a)): `langc rebuild` refusals on a tree with a change. The
# rows for exit 0, SOURCE, WRITER and OUTPUT need a clean tree (S8 U6); on a
# clean tree the DIRTY row becomes the exit 0 row.
rebuilds=0
# rebuild_row STATUS CODE [WORDS...]: `langc rebuild WORDS` gives exit STATUS
# and CODE on stderr.
rebuild_row() {
  rb_want=$1
  rb_code=$2
  shift 2
  rb_status=0
  build/langc rebuild "$@" >/dev/null 2>"$tmp/rb.err" || rb_status=$?
  rebuilds=$((rebuilds + 1))
  if [ "$rb_status" -ne "$rb_want" ] || ! rg -q -F "$rb_code" "$tmp/rb.err"; then
    echo "FAIL rebuild $*: want exit $rb_want and $rb_code, got exit $rb_status: $(head -n 1 "$tmp/rb.err")"
    fail=$((fail + 1))
  fi
}
rb_src=$(printf '%s\n' examples/*.lang | head -n 1)
rb_out="$tmp/rb.wasm"
if ! build/langc build "$rb_src" -o "$rb_out" 2>"$tmp/rb.err"; then
  echo "FAIL rebuild: build $rb_src gives an error: $(head -n 1 "$tmp/rb.err")"
  fail=$((fail + 1))
fi
rebuild_row 1 REFUSE_REBUILD_DIRTY "$rb_out.manifest.json"
# S8 D6: the two message rows of the DIRTY refusal. They hold only on a tree
# with a change, so S8 U6 changes them with the DIRTY row. The manifest of
# this langc has "dirty":true, so rebuild refuses it with the manifest
# message. A copy with "dirty":false gets the message of this langc.
rebuild_row 1 "is from a langc of a tree with a change" "$rb_out.manifest.json"
awk '{ sub(/"dirty":true/, "\"dirty\":false"); print }' "$rb_out.manifest.json" >"$tmp/rb-clean.json"
rebuild_row 1 "this langc is from a tree with a change" "$tmp/rb-clean.json"
awk '{ z = sprintf("%040d", 0); sub(/"commit":"[0-9a-f]+"/, "\"commit\":\"" z "\""); print }' "$rb_out.manifest.json" >"$tmp/rb-commit.json"
rebuild_row 1 REFUSE_REBUILD_COMMIT "$tmp/rb-commit.json"
printf '%s' "$(cat "$rb_out.manifest.json")" >"$tmp/rb-line.json"
rebuild_row 1 REFUSE_REBUILD_MANIFEST "$tmp/rb-line.json"
awk '{ sub(/"command":\["build"/, "\"command\":[\"check\""); print }' "$rb_out.manifest.json" >"$tmp/rb-word.json"
rebuild_row 1 REFUSE_REBUILD_MANIFEST "$tmp/rb-word.json"
rebuild_row 2 IO "$tmp/rb-missing.json"
rebuild_row 2 USAGE
rebuild_row 2 USAGE "$rb_out.manifest.json" "$rb_out.manifest.json"
echo "rebuilds: $rebuilds checked"

rm -rf "$tmp"

# design/DESIGN.md is verbatim and can hold a dash (as bridge-lang, ruling 2026-10-09).
if rg -n -g '!design/DESIGN.md' '\x{2013}|\x{2014}' . >/dev/null; then
  echo "FAIL: an em-dash or en-dash is in the kit"
  fail=$((fail + 1))
fi
echo "gate: $fail failures"
[ "$fail" -eq 0 ]
