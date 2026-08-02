# Rx Worlds — JSON Schema Layer

Machine-readable schema layer for Rx Worlds, the universe-registration and
traversal system of the Reflexion Multiverse (design canon §5). Authored from
scratch against the canon; the earlier PR #6 draft is not present on this
machine and none of its contents are assumed here.

## Files

- `rx.universe-manifest.v1.schema.json` — JSON Schema (draft 2020-12) for a
  Universe Manifest. Covers every section the canon names in §5.1: canon rules,
  species templates, body foundry, power-system schema, ability grammar,
  transformation trees, faction definitions, equipment rules, visual/audio
  language, character-creation boundaries, competitive cost functions,
  cross-universe permissions, and rights / revenue / revocation policy.
- `rx.provenance-chain.v1.schema.json` — JSON Schema for the character
  provenance chain (canon §5.1).
- `examples/mythology.universe.json` — valid manifest for the first universe,
  Mythology (Reflexion-original roster; §5.3, §9.2, §9.3).
- `examples/invalid_missing_comma.universe.json` — **intentionally malformed**
  input (a missing comma, echoing the PR #6 defect) used to prove the
  validator rejects bad input with a precise error.
- `validate_schemas.py` — standalone validator (see below).

## Hard invariants enforced by the schema

1. **Cross-universe DENY default (canon §5.2).**
   `cross_universe_permissions.default_policy` is `const: "deny"` and cannot be
   overridden. Traversal is the governed exception: each entry in `grants`
   must set `receipt_required: true` and `requires_counterparty_grant: true`
   (both `const`), encoding the rule that a companion or fragment from
   universe A has authority in universe B only when **both** manifests
   explicitly grant it, and every such traversal is receipted. The Mythology
   example ships with an empty `grants` array: nothing may traverse yet.

2. **Integer-only simulation data.** Every numeric field that feeds the
   simulation (attributes, chassis stats, costs, cooldowns, effect magnitudes,
   durations, budgets, weights, revenue basis points, ticks) is declared
   `"type": "integer"`. `canon_rules` pins `deterministic_simulation: true`,
   `numeric_domain: "integer"`, `hash_algorithm: "sha-256"`, and
   `serialization: "canonical-json-v1"` as `const`s.

3. **No pay-to-win (canon §6.2).** `equipment_rules.pay_to_win_prohibited` is
   `const: true`, and a schema-level `if/then` makes any item with
   `real_money_purchasable: true` reject any non-empty `stat_modifiers` — paid
   items must be cosmetic. `competitive_cost_functions.ranked_monetization_separation`
   is likewise `const: true`.

4. **Roster classes stay distinct (canon §5.1).**
   `universe.roster_class` is a closed enum: `official_franchise`,
   `creator_universe`, `original_reflexion`. Cross-universe grants distinguish
   `universal_structural_fragment` (may transfer) from `licensed_expression`
   (remains governed by its home manifest) per §5.4.

5. **IP originality (canon §9).** `visual_audio_language.originality_note` is a
   required string, and `living_traditions_care.respectful_depiction` is
   `const: true` (§9.3). The Mythology example uses only Reflexion-original
   names and designs inspired by public-domain mythology — no Age of Mythology
   expression (§9.2), no Matrix glyph language (§9.1).

## Provenance chain verification

Every character/companion carries a chain of links:
IP owner → approved character package → versioned Arena implementation →
licensed assets and behavior policy → auditable player deployment (canon §5.1).

The **schema** enforces link *shape*: each link has an `index`, a closed-enum
`link_type`, an `issuer` (id + name + public key id), a deterministic
`issued_at_tick` integer, a 64-hex-char `content_hash`, a 64-hex-char
`previous_link_hash`, and an Ed25519 `signature`.

JSON Schema cannot express cross-item ordering, so chain *continuity* is
verified **procedurally** by the receipt verifier (the roadmap directs reusing
the already-ported, hash-chained `RxReceipts`). The verification algorithm:

1. `links[0]` is genesis: `index == 0` and `previous_link_hash` is 64 zeros.
2. For each `i > 0`: `links[i].index == i`, and
   `links[i].previous_link_hash == SHA-256(canonical_json(links[i-1]))`.
3. Each link's `signature.value` verifies under `issuer.public_key_id` over the
   canonical-JSON serialization of the link with the `signature` field
   excluded.
4. Each link's `content_hash` matches the attested payload (package,
   implementation build, license, policy, or deployment record).

Any tampering with any link changes its hash, which breaks every subsequent
`previous_link_hash` — the chain fails `Verify()` exactly as the adversarial
receipt-tamper tests already do.

## Running the validator

```sh
python3 validate_schemas.py
```

From the `Schemas/` directory (paths are resolved relative to the script, so
it works from anywhere). It checks:

1. Both schema files parse as JSON and are valid draft 2020-12 schemas.
2. `examples/mythology.universe.json` **passes** the manifest schema.
3. An inline provenance chain **passes** the provenance schema.
4. `examples/invalid_missing_comma.universe.json` **fails** with a precise
   line/column parse error.

Exit code is 0 only if every expectation holds; a PASS/FAIL summary is
printed. Requires Python 3; uses `jsonschema` if installed, otherwise falls
back to strict `json.load` parse-validation and says so loudly in the output.

Quick manual parse check of any file:

```sh
python3 -m json.tool rx.universe-manifest.v1.schema.json > /dev/null
```

## Underspecified in canon — needs founder ruling

The schema marks these fields optional (or leaves them unmodeled) because the
canon explicitly does not specify them (canon §11 open questions):

1. **Licensed-expression vs universal-structural-fragment adjudication**
   (§5.4, §11 #8). The schema models the `fragment_kinds` distinction on
   grants, but the process that decides what counts as expression vs
   structure — and who signs that ruling — is unspecified. No adjudication
   receipt type is modeled.
2. **Living-traditions content review process** (§9.3, §11 #13).
   `respectful_depiction` is enforced, but reviewers and process are
   unassigned; `review_process_ref` is optional free text.
3. **Power-budget cost weightings** (§11 #9). `component_weights` exists but
   is optional with unconstrained integer values; canon gives no weighting.
4. **Ranked skill sanctioning cost functions and decay rules** (§6.4,
   §11 #10). Not modeled in the manifest beyond the closed cost-component
   enum.
5. **Tokenweave numerics** (§11 #6). `glyph_dialect` is descriptive text only;
   coherence-meter values and interrupt thresholds are unspecified.
6. **Knowledge-tree topology and fragment-decompression mechanics**
   (§11 #2, #3). `transformation_trees` is a manifest-local structure; how
   Compression Fragments socket/decompress into trees is not modeled.
7. **League eligibility vs Ascension state** (§6.3, §11 #5). Out of manifest
   scope; noted here so it is not silently assumed.
