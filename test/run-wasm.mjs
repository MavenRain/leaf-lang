// Runs entries of a langc Wasm module and prints each result as `langc eval`
// does: a number, or `trap` when the module traps.
// Usage: node test/run-wasm.mjs FILE NAME [ARGS...]
//        node test/run-wasm.mjs FILE -   (one `NAME [ARGS...]` line on stdin per call)
//        node test/run-wasm.mjs FILE --key DIR   (the `langc signer` module)
// Signer mode (docs/PLAN.md 5.2) reads the stdin lines `keygen ID SKSEED
// SKPRF PUBSEED [START]` (each seed 64 hex digits), `next`, `rootSum` (the
// D30 sum of the root of the last keygen), `sign LEAF M` (M 64 hex digits:
// it prints the D36 sum of the signature and keeps the signature) and
// `verify M` (the kept signature against M and the public key of the last
// keygen). It keeps the key position in DIR/ID.state and the EIP-8310
// keystore JSON in DIR/ID.json.
// The env.commit import refuses a position that is not larger than the
// position in DIR/ID.state: it prints `refused REFUSE_HIGH_WATER`, and the
// run stops with exit 1. A bad signer line exits 2.
import { closeSync, existsSync, fsyncSync, openSync, readFileSync, renameSync, unlinkSync, writeFileSync, writeSync } from "node:fs";
import { join } from "node:path";

const [file, name, ...args] = process.argv.slice(2);
if (file === undefined || name === undefined || (name === "--key" && args.length !== 1)) {
  process.stderr.write("usage: node test/run-wasm.mjs FILE NAME [ARGS...] | FILE - | FILE --key DIR\n");
  process.exit(2);
}

const lines = () => readFileSync(0, "utf8").split("\n").filter((line) => line !== "").map((line) => line.split(" "));

const program = () => {
  const instance = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(file)), {});
  const call = ([entry, ...words]) => {
    const fn = instance.exports[entry];
    if (typeof fn !== "function") throw new Error(`no exported entry '${entry}'`);
    try {
      return BigInt.asUintN(64, fn(...words.map(BigInt))).toString();
    } catch (e) {
      if (e instanceof WebAssembly.RuntimeError) return "trap";
      throw e;
    }
  };
  const calls = name === "-" ? lines() : [[name, ...args]];
  process.stdout.write(calls.map((c) => call(c) + "\n").join(""));
};

class Refused extends Error {}
class Usage extends Error {}

const signer = (dir) => {
  const heights = new Map(); // key id -> the tree height H
  const statePath = (id) => join(dir, `${id}.state`);
  const jsonPath = (id) => join(dir, `${id}.json`);
  const position = (id) => {
    if (!existsSync(statePath(id))) return 0n;
    const text = readFileSync(statePath(id), "utf8");
    if (!/^[0-9]+\n$/.test(text)) throw new Refused("REFUSE_HIGH_WATER");
    return BigInt(text.trim());
  };
  // RFC 9562 version 8 with the 16 hex digits of the key id (Q-S5-36 (a)).
  const uuid = (id) => {
    const h = id.toString(16).padStart(16, "0");
    return `00000000-0000-800${h[0]}-8${h.slice(1, 4)}-${h.slice(4)}`;
  };
  const keyState = (height, p) => ({ authoritative: false, high_water: Number(p), capacity: 2 ** height - Number(p) });
  const writeJson = (id, json) => writeFileSync(jsonPath(id), JSON.stringify(json, null, 2) + "\n");
  // env.commit (id, position): the durable write comes before the module goes on.
  const commit = (id, pos) => {
    id = BigInt.asUintN(64, id);
    if (pos <= position(id)) throw new Refused("REFUSE_HIGH_WATER");
    const pending = `${statePath(id)}.next`;
    const fd = openSync(pending, "w", 0o600);
    try {
      writeFileSync(fd, `${pos}\n`);
      fsyncSync(fd);
    } finally {
      closeSync(fd);
    }
    renameSync(pending, statePath(id));
    const directory = openSync(dir, "r");
    try {
      fsyncSync(directory);
    } finally {
      closeSync(directory);
    }
    writeJson(id, { ...JSON.parse(readFileSync(jsonPath(id), "utf8")), state: keyState(heights.get(id), pos) });
  };
  const instance = new WebAssembly.Instance(new WebAssembly.Module(readFileSync(file)), { env: { commit } });
  const { memory, keygen, publicKey, next, sign, verify } = instance.exports;
  const view = () => new DataView(memory.buffer);
  const trapOr = (f) => {
    try {
      return f();
    } catch (e) {
      if (e instanceof WebAssembly.RuntimeError) return "trap";
      throw e;
    }
  };
  const decimal = /^[0-9]+$/;
  const word = (s) => decimal.test(s ?? "") && BigInt(s) < 2n ** 64n;
  const seed = /^[0-9a-fA-F]{64}$/;
  let ks = null;
  let root = null; // the 4 root words of the last keygen
  let pkWords = null; // the 9 publicKey words of the last keygen (D27)
  let sig = null; // the address of the kept signature (D32)
  // D30: acc := (7 acc + digit i) mod (2^61 - 1) for the 64 base-16 digits of
  // the 4 words ws, from byte 0 and the high half of a byte first (eval.c
  // digit), from the start value acc.
  const sumFrom = (acc, ws) =>
    Array.from({ length: 64 }, (_, i) => (ws[i >> 4] >> BigInt(60 - 4 * (i & 15))) & 15n).reduce(
      (a, d) => (7n * a + d) % (2n ** 61n - 1n), acc);
  const rootSum = () => sumFrom(0n, root);
  // D36: the D30 fold continued over the 68 + H digests of the kept signature
  // from word 1: rand, the 67 WOTS+ values and the H auth nodes (D32).
  const sigSum = () =>
    Array.from({ length: 68 + Number(pkWords[8]) }, (_, j) =>
      [0, 1, 2, 3].map((i) => view().getBigUint64(Number(sig) + 8 * (1 + 4 * j + i), true))).reduce(
      (acc, ws) => sumFrom(acc, ws), 0n);
  // A digest of 64 hex digits goes to the address at. Word w is the
  // big-endian value of bytes 8w to 8w + 7.
  const put = (at, h) =>
    [0, 1, 2, 3].forEach((w) => view().setBigUint64(at + 8 * w, BigInt(`0x${h.slice(16 * w, 16 * w + 16)}`), true));
  // The seeds go to 0, 32 and 64.
  const keygenLine = ([id, ...rest]) => {
    if (!word(id) || rest.length < 3 || rest.length > 4 || !rest.slice(0, 3).every((h) => seed.test(h)) ||
      (rest.length === 4 && !word(rest[3]))) {
      throw new Usage(`bad keygen line: keygen ${[id, ...rest].join(" ")}`);
    }
    rest.slice(0, 3).forEach((h, j) => put(32 * j, h));
    const key = BigInt(id);
    const p = position(key);
    const start = rest.length === 4 ? BigInt(rest[3]) : p;
    ks = null;
    root = null;
    pkWords = null;
    sig = null;
    return trapOr(() => {
      const k = keygen(key, start, 0n, 32n, 64n);
      // The publicKey block (D27): the root at words 0 to 3, pubSeed at 4 to 7, H at 8.
      const pk = Number(publicKey(k));
      const words = (w) => [0, 1, 2, 3].map((i) => view().getBigUint64(pk + 8 * (w + i), true));
      const hex = (ws) => ws.map((v) => v.toString(16).padStart(16, "0")).join("");
      const height = Number(view().getBigUint64(pk + 64, true));
      heights.set(key, height);
      writeJson(key, {
        version: 5,
        uuid: uuid(key),
        scheme: {
          name: "leaf-xmss-keccak",
          params: { hash: "keccak256", n: 32, winternitz_w: 16, tree_height: height, hypertree_layers: 1,
            encoding: "rfc8391-checksum", index_mode: "counter", lifetime_leaves: 2 ** height },
        },
        pubkey: { root: hex(words(0)), pub_seed: hex(words(4)) },
        state: keyState(height, p),
      });
      ks = k;
      root = words(0);
      pkWords = Array.from({ length: 9 }, (_, w) => view().getBigUint64(pk + 8 * w, true));
      return k.toString();
    });
  };
  // D36: M goes to 0. sign (ks, LEAF, 0) gives the signature block.
  const signLine = ([leaf, m]) => {
    put(0, m);
    sig = null;
    return trapOr(() => {
      sig = sign(ks, BigInt(leaf), 0n);
      return `sigSum ${sigSum()}`;
    });
  };
  // D36: M goes to 0 and the publicKey block of the last keygen to 128.
  const verifyLine = ([m]) => {
    put(0, m);
    pkWords.forEach((v, w) => view().setBigUint64(128 + 8 * w, v, true));
    return trapOr(() => `verify ${verify(128n, 0n, sig) === 1n}`);
  };
  const step = ([word, ...rest]) => {
    if (word === "keygen") return keygenLine(rest);
    if (word === "next" && rest.length === 0 && ks !== null) return trapOr(() => BigInt.asUintN(64, next(ks)).toString());
    if (word === "rootSum" && rest.length === 0 && root !== null) return `rootSum ${rootSum()}`;
    if (word === "sign" && rest.length === 2 && decimal.test(rest[0]) && BigInt(rest[0]) < 2n ** 64n && seed.test(rest[1]) && ks !== null) return signLine(rest);
    if (word === "verify" && rest.length === 1 && seed.test(rest[0]) && sig !== null) return verifyLine(rest);
    throw new Usage(`bad signer line: ${[word, ...rest].join(" ")}`);
  };
  const lockPath = join(dir, ".signer.lock");
  let lock;
  try {
    // Hold one writer across the read, reservation and durable replacement.
    // A crashed writer leaves a lock that requires explicit recovery.
    try {
      lock = openSync(lockPath, "wx", 0o600);
    } catch (e) {
      if (e.code === "EEXIST") throw new Refused("REFUSE_HIGH_WATER");
      throw e;
    }
    lines().forEach((words) => writeSync(1, `${step(words)}\n`));
  } catch (e) {
    if (e instanceof Refused) {
      writeSync(1, `refused ${e.message}\n`);
      process.exitCode = 1;
    } else if (e instanceof Usage) {
      writeSync(2, `run-wasm: ${e.message}\n`);
      process.exitCode = 2;
    } else {
      throw e;
    }
  } finally {
    if (lock !== undefined) {
      closeSync(lock);
      unlinkSync(lockPath);
    }
  }
};

if (name === "--key") signer(args[0]);
else program();
