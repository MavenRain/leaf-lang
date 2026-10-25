# leaf-lang domain design

This file is the only source of the core types and the core operations of
leaf-lang. `docs/PLAN.md` sections 2 and 3 cite it. Do not add a core type
or a core operation that this file and the PLAN admission table (PLAN
section 3) do not give.

Section 1 is the USER text of 2026-10-25, verbatim. The gate lets this file
hold an em-dash for that reason (as bridge-lang, ruling 2026-10-09, and
quorum-lang). Do not edit section 1. Section 2 holds the USER rulings of the
same day as recorded at the time. Section 3 holds the research facts that
the target question used.

## 1. The USER ask (2026-10-25, verbatim)


I want to write a programming language with the following characteristics:

For type constructors/higher-kinded types, admit only cell monad and their duals in a locally presentable locally cartesian closed \((\infty,1)\)-category, as realized computationally in Cubical Type Theory.

For base types and operations, admit the minimum required to satisfy the following description:

```
A post-quantum account language (later)

  - Activity: The Ethereum Foundation formed a post-quantum team in
    January 2026. Its plans:
    - XMSS hash-based signatures for validators
    - per-account quantum-safe signatures through EIP-8141, considered for the
      Hegotá upgrade
    - layer-1 work done around 2029
  - Fit: XMSS signatures are stateful: signing twice with one one-time key
    breaks security. Affine types enforce "use at most once" directly, so
    affine-lang's design carries over.
```

The host language shall be TinyCC.  Give me multiple choice to chose the compilation target(s) based on what you find is appropriate.

Finally, I really want agents to reach for this language and use it when applicable.  To that end, please take the following into consideration:

```
Agent Incentives to Use DSL Programming Languages

**Make using your DSL the rational choice for the agent.** Agents (LLM coding agents, multi-agent systems, or autonomous on-chain agents) do not need "motivation" in the human sense. They respond to lower expected cost (tokens, retries, failure probability, verification effort) and higher expected utility of the resulting artifact (mechanical checkability, composability, downstream value). Design both the language and the surrounding harness/protocol so that the DSL path dominates alternatives.

### 1. Technical superiority as the primary incentive (highest leverage for AI agents)

Novel DSLs normally underperform because of sparse training data. Counter this with designs that are *easier* for models than the general-purpose languages they were heavily trained on.

- **Canonical, constrained surface syntax** (Anka-style and the "syntactic camp" on agentlanguages.dev). Prefer one obvious way to express each operation, explicit named intermediates (`INTO` / named steps), and verbose keywords rather than overloaded operators or implicit state. Anka showed Claude 3.5 Haiku reaching ~99.9% parse success and +40 percentage points accuracy on multi-step pipelines versus Python—purely from in-context learning, with zero prior training on the language.
- **Fast, structured feedback loops**. Ship a parser/typechecker/verifier that returns domain-level, actionable diagnostics (structured JSON with suggested fixes or retry fields, as in Codong and similar systems). Agents that can call this in a tool loop self-correct cheaply; this is the densest practical reward signal available.
- **LLM-friendly surface + mechanical backend**. Borrow patterns from high-resource languages for the surface while keeping the semantics narrow and compositional. The Microsoft Azure guidance on AI coding agents + DSLs emphasizes seeding 3–5 high-quality examples, custom agent instructions, MCP servers that expose the schema/types, and a validator-in-the-loop.

These changes alone often make the DSL preferred because success rate and iteration cost improve.

### 2. Verification and denotational superiority (your strongest unique lever)

Given your formal-methods background (Lean 4, category theory, Cubical TT/HoTT, denotational design, ZK, self-referential systems), embed the payoff in the semantics:

- Every well-formed program in the DSL should come with a clear denotational semantics (or refinement types, resource bounds, effect system, or circuit generation) that is machine-checkable. Agents that emit the DSL obtain free or cheap proofs/properties; agents that emit general-purpose code do not.
- Prefer compositional denotations (monads, Kan extensions, fixed-points, etc.) so large agentic or self-referential constructions remain tractable to reason about. Agents can query or manipulate the meaning rather than reverse-engineering it.
- Multi-target compilation (to Lean, ZK circuits, EVM/TVM IR, etc.) makes "write DSL → verified executable artifact" mechanical. Downstream systems (oracles, executors, marketplaces, other agents) can then preferentially accept, lower fees for, or automatically reward those artifacts.

This turns the DSL into the *only* practical way to produce high-assurance outputs. In a self-referential DAO or agent governance setting, you can encode the preference directly: only (or preferentially) programs inhabiting useful semantic types (e.g., resource-bounded, extractable, self-constituting) are valid or receive higher weight. The incentive becomes a fixed point of the system itself.

### 3. Protocol and economic incentives

- **Exclusion or strong preference at the boundary**. The runtime, executor, or DAO only accepts (or ranks highest) programs written in and verified against the DSL. This is the strongest possible incentive.
- Soft versions: fee discounts, lower collateral, preferential matching, or reward multipliers for verified DSL programs in agent marketplaces, bounty platforms, or OpenCollective-style systems.
- Token-efficient designs further reduce operator cost (context burn and generation cost).
- Explicit bounties or grants for high-quality programs, examples, or small-model fine-tunes on the DSL.
- Reward shaping in any RL/fine-tuning loop: dense credit for successful parse / typecheck / proof-obligation discharge (verifier-grounded rewards of the kind recently explored for Dafny-style verification) rather than binary pass/fail.

### 4. Distribution and bootstrapping

- Package the grammar, type definitions, curated examples, and a small validator as an Agent Skill (SKILL.md + supporting files) or MCP server so Claude Code, Cursor, and similar systems discover and default to it when the domain matches.
- Fine-tune or distill a small model (<500 MB) that is fluent in the DSL and release it as a specialized coding backend. This aligns directly with current small-model and inference work.
- Publish reference implementations, an agent-friendly grammar + examples corpus, and list the language in catalogues such as agentlanguages.dev (syntactic / verification / orchestration taxonomy). Early high-quality examples and clear denotational documentation compound quickly.

### Practical starting sequence

1. Design (or refine) the surface for canonical forms and explicit intermediates; add a fast validator that returns structured domain errors.
2. Give the language a clean denotational semantics (Lean embedding or equivalent) so successful programs carry machine-checkable properties.
3. Expose the tools via MCP / agent skill and seed every relevant agent context with the grammar + 3–5 excellent examples.
4. For any multi-agent or on-chain deployment, make verified DSL programs the preferred or exclusive interface and encode the preference in the protocol or governance rules.
5. Optionally fine-tune a small specialized model and open the skill/validator.

Agents will adopt the DSL when it is simultaneously easier to generate correctly, cheaper to iterate on, and produces artifacts of higher downstream value. Your formal-methods and denotational-design strengths make the verification premium especially powerful—use it.
```


## 2. The USER rulings (2026-10-25, AskUserQuestion)

- Host TinyCC.
- Targets (3): "EVM account verifier" (EIP-8141 VERIFY frame: XMSS verify then APPROVE, plus an ERC-4337 validateUserOp entry); "Wasm signer + keystore" (keygen + sign, each one-time leaf an affine resource, EIP-8310 commit-before-sign state); "zkml-lang Circuit IR" (one proof of N XMSS verifies, the leanMultisig role). NOT chosen: C99 signer library.
- Hash: "Abstract, bind per target": one opaque tweakable hash in the language, laws proved for every hash; v1 binds Keccak-256 on EVM and Wasm; a leanSig Poseidon2 binding comes in a later slice.

R1 and R2 are the words of section 1. The name `leaf-lang` is Claude's call
(each signature uses one one-time leaf). The USER can change it.

## 3. Research facts (2026-10-25, web)


- EIP-8141 Frame Transactions: CFI for Hegota 2026-03-26; EF Protocol Cluster "must ship" tier 2026-09 (with FOCIL EIP-7805). VERIFY frame = STATICCALL (no state writes), must execute APPROVE (0xaa). Spec still moving.
- EF L1 quantum-resistance goal: December 2029.
- leanSig = Generalized XMSS for the lean consensus layer, Poseidon2 over a 31-bit field (KoalaBear), lifetime 2^32, research-stage, unaudited; signatures ~3112 B; aggregation = leanMultisig (leanVM zkVM).
- EIP-8310 (Draft): keystore for stateful XMSS keys; commit-before-sign durability, reserved leaf ranges for concurrent signers, no silent reset of the signing position.
- poqeth (eprint 2025/091): Solidity WOTS+/XMSS/SPHINCS+/MAYO verifiers; WOTS+ verify ~200k gas (informal forum figure).
- Consequence: the VERIFY frame cannot record a used leaf on chain, so "at most once" lives on the signer side (affine leaves + EIP-8310 commit-before-sign); on chain, bind leaf index to the account nonce.

Sources: eips.ethereum.org/EIPS/eip-8141 (Frame Transactions), the EIP-8310
draft (stateful XMSS keystore), RFC 8391 (XMSS), the leanSig repository
(Generalized XMSS, Poseidon2 over KoalaBear), eprint 2025/091 (poqeth), the
Ethereum Foundation post-quantum team announcement (January 2026). The
spec of EIP-8141 still moves. PLAN section 10 keeps it as an open item.
