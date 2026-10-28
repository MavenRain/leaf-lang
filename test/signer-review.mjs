// Persistence and integer-boundary regressions for the signer harness.
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, unlinkSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";

const module = process.argv[2];
assert.ok(module);
const dir = mkdtempSync(join(tmpdir(), "leaf-signer-review-"));
const seeds = ["01", "02", "03"].map((s) => s.repeat(32)).join(" ");
const run = (input, preload) => spawnSync(process.execPath,
  [...(preload ? ["--import", preload] : []), "test/run-wasm.mjs", module, "--key", dir],
  { input, encoding: "utf8" });
try {
  let result = run(`keygen 1 ${seeds}\nnext\n`);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(readFileSync(join(dir, "1.state"), "utf8"), "1\n");
  const preload = join(dir, "fault.mjs");
  writeFileSync(preload, `import fs from 'node:fs';
import { syncBuiltinESMExports } from 'node:module';
const open = fs.openSync;
fs.openSync = (path, mode, ...rest) => {
  const fd = open(path, mode, ...rest);
  if (/\\.state(?:\\.next)?$/.test(String(path)) && mode === 'w') process.exit(81);
  return fd;
};
syncBuiltinESMExports();
`);
  result = run(`keygen 1 ${seeds}\nnext\n`, preload);
  assert.equal(result.status, 81);
  assert.equal(readFileSync(join(dir, "1.state"), "utf8"), "1\n", "an interrupted replacement must keep the old position");
  // A crash leaves the lock in place, so reservations fail closed.
  result = run(`keygen 1 ${seeds}\nnext\n`);
  assert.equal(result.status, 1);
  assert.match(result.stdout, /refused REFUSE_HIGH_WATER/);
  unlinkSync(join(dir, ".signer.lock")); // explicit recovery after the writer exited
  result = run(`keygen 1 ${seeds}\nnext\n`);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(readFileSync(join(dir, "1.state"), "utf8"), "2\n");
  writeFileSync(join(dir, "1.state"), "");
  result = run(`keygen 1 ${seeds}\nnext\n`);
  assert.equal(result.status, 1, "an empty state must never mean zero");
  const large = 2n ** 63n;
  result = run(`keygen ${large} ${seeds}\nnext\n`);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(readFileSync(join(dir, `${large}.state`), "utf8"), "1\n");
  result = run(`keygen ${2n ** 64n} ${seeds}\n`);
  assert.equal(result.status, 2, "key ids must fit the Wasm i64 ABI");
  console.log("signer persistence regressions: passed");
} finally {
  rmSync(dir, { recursive: true, force: true });
}
