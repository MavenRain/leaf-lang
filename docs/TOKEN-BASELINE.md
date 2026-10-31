# Token baseline (S9 D6)

This file compares the size of one task in leaf-lang with the size of the
same task in Solidity plus Rust. The numbers come from build/tokcount
(test/tokcount.c). The gate (test/gate.sh, row `token baseline: counts
match`) runs the same command and compares its output with the table below.
If a file changes, run the command again and copy its output here.

## The task

The task has 4 steps:

1. keygen: make an XMSS keystore of height 4 from the seeds skSeed, skPrf
   and pubSeed, and give the public key.
2. sign: commit the index of the next leaf (durable) before the leaf signs
   one message.
3. verify: check the signature under the public key.
4. nonce binding: accept the signature only when its leaf index is the
   nonce. The index check comes first, thus a wrong nonce does no hash work.

## The files

- leaf-lang side: examples/graded/5-sign-verify-nonce.lang (all 4 steps;
  `build/langc check` gives `ok`).
- Solidity plus Rust side (Q-S9-8 (a)): own minimal files, counted, not
  built.
  - docs/token-baseline/keygen_sign.rs: keygen and sign, with the commit of
    the next index (write, then sync) before the sign.
  - docs/token-baseline/NonceBinding.sol: verify and the nonce binding
    (acceptAt).

On each side the XMSS primitives come from a library. In leaf-lang, keygen,
publicKey, next, sign and verifyAt are base operations. In Rust, they come
from an XMSS crate (the API in the file is a sketch). In Solidity, verifyAt
is a call to a deployed verifier contract (IXmssVerifier). Thus the counts
are for the task code around the primitives, not for the XMSS code.

The leaf-lang file also has 2 check values (acceptedAt0 and refusedAt1).
The Solidity plus Rust side has no tests, thus these tokens count against
the leaf-lang side only.

## The method (the proxy, Q-S9-5 (a))

The count is a fixed offline proxy, not the tokenizer of a model:

- A token is a run of ASCII letters, digits and `_`, or one ASCII
  punctuation character.
- A non-ASCII code point counts as its UTF-8 byte count (for example, `λ`
  is 2 tokens and `⊸` is 3 tokens).
- Whitespace and control bytes are not tokens.

The tool counts all of each file, comments included. It also gives the
bytes and the code points of each file. The counts are for the files of the
S9 commit (on base commit aa2bfee).

## The table

The command (from the kit directory):

```
build/tokcount examples/graded/5-sign-verify-nonce.lang -- docs/token-baseline/keygen_sign.rs docs/token-baseline/NonceBinding.sol
```

Total 1 is the leaf-lang side. Total 2 is the Solidity plus Rust side. The
ratio is the tokens of total 2 over the tokens of total 1, rounded to 2
decimal places.

<!-- tokcount begin -->
| file | bytes | code points | tokens |
| :--- | ---: | ---: | ---: |
| examples/graded/5-sign-verify-nonce.lang | 1861 | 1807 | 563 |
| total 1 | 1861 | 1807 | 563 |
| docs/token-baseline/keygen_sign.rs | 2227 | 2227 | 661 |
| docs/token-baseline/NonceBinding.sol | 1383 | 1383 | 310 |
| total 2 | 3610 | 3610 | 971 |

ratio of the tokens (total 2 over total 1): 1.72
<!-- tokcount end -->

The same task takes 1.72 times the proxy tokens in Solidity plus Rust.
