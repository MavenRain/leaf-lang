#!/bin/sh
# The S6 gas rows (docs/PLAN.md 5.1, Q-S6-8 (b)). `make gas` runs it from the
# kit directory after the build. `make check` does not run it: the h = 16
# keygen is long. For each height H in the arguments (default 4 10 16) one
# keygen runs in the Wasm signer (run-wasm.mjs --key, a new key directory).
# The signer signs the sample positions: at h = 4 all 16, at a larger h 16
# positions from the first to the last. Each signature goes to the shim build
# and to the entry 2 build of that pk (`evm run --statdump`) and must accept:
# the shim returns the word 1, entry 2 the word 0. For each H and build it
# prints one row with the min, mean and max of the input bytes, the execution
# gas, the calldata gas of EIP-7623 (the standard 4 per zero byte and 16 per
# nonzero byte, the floor 10 per token with 4 tokens per nonzero byte) and the
# transaction gas 21000 + max(standard + execution, floor).
set -eu
tmp=${TMPDIR:-/tmp}/langc-gas.$$
rm -rf "$tmp"
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT
[ $# -gt 0 ] || set -- 4 10 16
seeds="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f 202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f 404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"
msg=6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d6d
entry_point=0x0000000071727de22e5e9d8baf0edac6f37da032
word0="0x$(printf '%064d' 0)"
word1="0x$(printf '%063d' 0)1"
fail=0
# gas_run H BUILD CODE CALL SENDER WANT: `evm run` of CODE on the input file
# CALL (from SENDER when it is not empty) prints the last line WANT. The row
# "execution standard floor tx bytes" goes to the rows file of H and BUILD.
gas_run() {
  evm run --codefile "$3" --inputfile "$4" ${5:+--sender $5} --statdump >"$tmp/evm.out" 2>&1 || true
  got=$(tail -n 1 "$tmp/evm.out")
  if [ "$got" != "$6" ]; then
    echo "FAIL gas h = $1 $2 $(basename "$4"): want '$6', got '$got'"
    fail=$((fail + 1))
    return
  fi
  awk -v e="$(awk '/gas used:/ { print $NF }' "$tmp/evm.out")" '
    { for (i = 1; i < length($0); i += 2) if (substr($0, i, 2) == "00") z++; else nz++ }
    END {
      std = 4 * z + 16 * nz
      floor = 10 * (z + 4 * nz)
      tx = 21000 + (e + std > floor ? e + std : floor)
      printf "%d %d %d %d %d\n", e, std, floor, tx, z + nz
    }' "$4" >>"$tmp/rows-$1-$2.txt"
}
# gas_row H BUILD NAME: the row of H and BUILD (min/mean/max of each column).
gas_row() {
  awk -v h="$1" -v name="$3" '
    {
      for (c = 1; c <= 5; c++) {
        s[c] += $c
        if (NR == 1 || $c < lo[c]) lo[c] = $c
        if (NR == 1 || $c > hi[c]) hi[c] = $c
      }
    }
    END {
      f = "%d/%.1f/%d"
      printf "gas h = %s %s: %d positions, bytes " f ", execution " f ", standard " f ", floor " f ", tx " f "\n",
        h, name, NR, lo[5], s[5] / NR, hi[5], lo[1], s[1] / NR, hi[1], lo[2], s[2] / NR, hi[2],
        lo[3], s[3] / NR, hi[3], lo[4], s[4] / NR, hi[4]
    }' "$tmp/rows-$1-$2.txt"
}
for h in "$@"; do
  n=$((1 << h))
  if [ "$n" -le 16 ]; then
    positions=$(seq 0 $((n - 1)))
  else
    positions=$(for i in $(seq 0 15); do echo $((i * (n - 1) / 15)); done)
  fi
  build/langc signer --height "$h" -o "$tmp/signer$h.wasm"
  mkdir -p "$tmp/k$h"
  # One keygen, then next up to each sample position p and the sign, shim and
  # userop lines of p (the nonce p).
  {
    echo "keygen 8 $seeds 0"
    at=0
    for p in $positions; do
      yes next | head -n $((p + 1 - at))
      at=$((p + 1))
      echo "sign $at $msg"
      echo "shim $p $msg $tmp/pk$h.bin $tmp/s$h-$p.hex"
      echo "userop $p $msg 0 $tmp/pk$h.bin $tmp/u$h-$p.hex"
    done
  } >"$tmp/in$h.txt"
  start=$(date +%s)
  node test/run-wasm.mjs "$tmp/signer$h.wasm" --key "$tmp/k$h" <"$tmp/in$h.txt" >"$tmp/out$h.txt" ||
    { echo "FAIL gas h = $h: the signer stopped: $(tail -n 1 "$tmp/out$h.txt")"; exit 1; }
  echo "gas h = $h signer: $(wc -l <"$tmp/in$h.txt" | tr -d ' ') lines, $(($(date +%s) - start)) s"
  build/langc verifier --height "$h" --pk "$tmp/pk$h.bin" --shim -o "$tmp/rt$h.hex"
  build/langc verifier --height "$h" --pk "$tmp/pk$h.bin" --entry 4337 -o "$tmp/e2-$h.hex"
  for p in $positions; do
    gas_run "$h" shim "$tmp/rt$h.hex" "$tmp/s$h-$p.hex" "" "$word1"
    gas_run "$h" entry2 "$tmp/e2-$h.hex" "$tmp/u$h-$p.hex" "$entry_point" "$word0"
  done
  [ ! -s "$tmp/rows-$h-shim.txt" ] || gas_row "$h" shim shim
  [ ! -s "$tmp/rows-$h-entry2.txt" ] || gas_row "$h" entry2 "entry 2"
done
[ "$fail" -eq 0 ] || { echo "gas: $fail FAIL rows"; exit 1; }
