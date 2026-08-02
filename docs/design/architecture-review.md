# Architecture review (ROB-84) — WORKING DOCUMENT

**Status: IN PROGRESS. Deliberately UNTRACKED until the review is finished** —
do not `git add` it, and watch for `git add -A` sweeping it in by accident.

It lives in the repo working tree rather than the agent scratch directory
because that directory is keyed by session id: a session resumed after a usage
limit cannot find it. This survives.

If you are picking this up cold, read §0 then §3.

**Audit only. No behaviour changes.** Findings become follow-up issues so each
change stays reviewable on its own. Parity questions belong to ROB-85 (done).

---

## 0. How to resume this

1. Read §1 (why) and §2 (rules) — they are short and they are the part that is
   easy to get wrong.
2. Look at §3 for which slices are DONE / IN PROGRESS / NOT STARTED.
3. Findings land in §4, one table row each, appended as they arrive.
4. When all slices are done: triage §4 into follow-up issues, write §5, flip the
   status above, and commit.

Agents append to §4 and mark their slice in §3. Nobody edits another slice's
rows.

---

## 1. Why this review exists

Stages 1–4c migrated the engine onto an action queue, then ~146 cards landed on
top of it quickly. **Two regressions were caught by asking, not by any gate:**

- direct state mutations inside `handle_play_card`, and
- Double Tap / Havoc implemented as **recursion** rather than the `PlayCard`
  action the design doc specified by name.

Every test stayed green through both. That is the point: the suite verifies
*behaviour*, and behaviour was correct while the architecture drifted away from
its own design. This is a deliberate sweep for the drift the gates cannot see.

Two more data points arrived after the issue was filed, both the same shape —
a structure nobody re-examined after the thing around it changed:

- `compute_obs` called `pile_count` once per card type per pile, so it was
  O(card types × deck size) for O(deck size) work, and got *slower as the pool
  grew*. Unnoticed for the life of the project (fixed in ROB-81).
- `kObsCardOrder` was 154 hand-maintained lines whose only guard was a **size**
  assert, so a duplicate paired with a missing entry would compile clean and
  silently corrupt the observation (removed in ROB-87).

Neither was a correctness bug at the time. Both were structures that had
outlived their reasoning. That is the class of thing to look for.

---

## 2. Rules for anyone working on this

### Read the files. Do not grep them.

A grep sweep for "direct mutations outside executors" was attempted while
scoping ROB-84 and **produced a false all-clear**: the pattern covered only
`state.character` / `state.enemies`, only `+=` and `-=`, and could not cross
newlines — so it never examined the card piles at all. A broader pattern returns
53 hits in `turn_loop.cc` alone, most of them benign, which is exactly why the
count is not the finding.

**You cannot establish the absence of a pattern with a regex written to match
what you already expect.** Grep locates and counts. Reading concludes.

If a claim is "there are no X", either it is backed by having read the code, or
it is phrased as what it actually is: *"no matches for pattern P, which would
not have caught cases Q and R."*

### Report, do not fix

Findings only. A fix mixed into an audit is a fix nobody reviewed.

### Severity

| | meaning |
|---|---|
| **regression** | the code contradicts the accepted design; it drifted |
| **erosion** | still correct, but a structure has outlived its reasoning and is now a hazard (the `pile_count` / `kObsCardOrder` class) |
| **debt** | known, deliberate, worth revisiting — not a defect |

An "everything checks out" slice is a real and useful result. Do not pad §4.

---

## 3. Slice status

| # | Slice | Files | Status | Owner |
|---|---|---|---|---|
| 1 | Translation + turn boundaries | `turn_loop.cc` | DONE | |
| 2 | Executors + hook dispatch | `action.cc`, `action.h` | DONE | |
| 3 | Query layer + the field-bag | `query.cc`, `card.h` | DONE | |
| 4 | Obs encoding + bindings | `combat_env.{h,cc}`, `bindings/_core.cc` | DONE | |
| 5 | Enemy phase + triggered effects | `enemy.{h,cc}`, Stage 3 paths | DONE | |

---

## 4. Findings

Append rows here. Keep the seeds below at the top; they are measured, so do not
re-derive them — confirm or extend.

### Seeds (measured while scoping — start here)

| # | Where | Design says | Code does | Severity |
|---|---|---|---|---|
| S1 | `action.h` `Action::amount` | R5: one slot means one thing | `amount` is an untyped union — a magnitude, a `GeneratedPile`, a `ChoiceKind`, and a `PlayCard` mode depending on `kind`. The principle applied rigorously to the action *space* is violated inside the Action *struct*. | debt |
| S2 | `action.h` damage kinds | — | Five: `DealDamage`, `DealFixedDamage`, `DamageAllEnemies`, `DamageRandomEnemy`, `DamageRandomEnemyAttack`. A combinatorial spread of (attack vs fixed) × (single/all/random) grown one card at a time. Check whether the hook-firing differences are essential or incidental. | debt |
| S3 | `card.h` `CardData` | flagged as "already strained" at Stage 1 with ~12 fields | 46 fields, positionally initialised. Needed ad-hoc verification scripts **twice** to catch mis-aligned commas, and a third time for the ROB-87 rung rows. Measured fragility, not aesthetics. | erosion |
| S4 | `action.cc` | Stage 1 complaint: "every mechanic lands in one file" | 1168+ lines — larger than `turn_loop.cc` (893) ever was. The god file moved rather than dissolved. | erosion |

### Slice findings

#### Slice 1 — Translation + turn boundaries (`turn_loop.cc`)

| # | Where | Design says | Code does | Severity |
|---|---|---|---|---|
| 1.1 | `turn_loop.cc:512` (`handle_play_card`) | "nothing mutates game state directly outside an executor" (review brief); `Debuff` applications have one write path, `apply_debuff` (`action.cc:130-151`), which also does Artifact-negation uniformly for every `Debuff` value. | `if (data.no_draw_after) state.character.debuffs[Debuff::NoDraw] = 1;` writes the debuffs map directly after `drain()` returns, bypassing `ApplyDebuff`/`apply_debuff` entirely — no Artifact check, no hook. `Debuff::NoDraw` is a real `Debuff` enum value, explicitly modelled that way because "StS renders it as a debuff icon" (`status_effect.h:18-23`), so nothing in the type system excuses it from the uniform path. Currently unobservable: `Power::Artifact` is granted only to enemies in v1 (`enemy.cc:1003`; no player-facing source exists), so there is no way to test whether Artifact should negate a self-applied NoDraw. Becomes live the instant a player-side Artifact source is added. No comment at the call site or on the field (`card.h:391`) documents the bypass as intentional. | regression |
| 1.2 | `turn_loop.cc:517-524` (`handle_play_card`, post-drain) | Same rule as 1.1; Stage-1 mutators are described as "wrapped by the executors" (`action.h:104-108`), and every gameplay stat mutation is supposed to have an executor wrapper. | Reaper's heal and Feed's max-HP gain call the Stage-1 mutators `heal_player` / `gain_max_hp` directly. Confirmed by reading the full `execute()` switch (`action.cc:625-1081`, all 26 `ActionKind` cases): there is no `Heal` or `GainMaxHp` kind at all — these two effects have no executor to route through even if someone wanted to queue them. No hook currently listens for "player healed" or "gained max HP," so it's inert today, but it is the same "quietly-fine-until-something-reacts-to-it" shape the review doc names as the `pile_count` / `kObsCardOrder` class (§1). | regression |
| 1.3 | `turn_loop.cc:182-196` (`handle_play_card`, True Grit's random exhaust) | Stated in-code convention: random effects roll RNG at **execution** time, "like every other random effect" (`action.cc:805`, Infernal Blade's `MakeCardFree`). The one sanctioned exception (Protect's ally roll) is translation-time and is logged as `ordering-notes.md` note 5. | `pick(state.rng)` for True Grit's random hand-card selection runs during **translation**, inside `handle_play_card`, not inside an executor — and has no corresponding `ordering-notes.md` entry the way Protect's does. The comment ("Rolled here (translation) so the count is known") only justifies knowing the *loop count* early (`data.exhaust_random_from_hand`, a fixed field, needs no RNG) — it does not justify rolling *which card* early. Currently unobservable: nothing else consumes RNG or mutates the hand between the start of translation and where these actions would otherwise execute, so the stream position is identical either way — same reasoning as Protect, just never written down. | debt |
| 1.4 | `turn_loop.cc:648-651, 735-741` (`handle_end_turn`, esp. `free_this_turn.clear()` at 739) | `effects-architecture.md` §4.7: turn-scoped counters (its own example: Battle Trance's no-draw flag) are "cleared by the `EndOfTurnUpkeep` executor." | No `EndOfTurnUpkeep` `ActionKind` exists anywhere in the codebase — confirmed by reading the full `execute()` switch and grepping the string, which appears only in `effects-architecture.md` itself. `free_this_turn.clear()`, the block reset, and the energy refill are all plain imperative writes in `handle_end_turn`, per the *different* boundary `action.h:22-24` actually documents ("Phase orchestration ... stays imperative in turn_loop.cc"). The code matches the settled convention in `action.h`; it is `effects-architecture.md` §4.7 that is stale against what actually shipped. | debt |
| 1.5 | `turn_loop.cc:476-478` comment vs. `action.cc:821-875` (`PlayCard` executor) re-entering `handle_play_card` (`turn_loop.cc:81`) | `effects-architecture.md` §4.1 names `PlayCard` for exactly "Double Tap, Havoc re-entry" — re-entry is sanctioned by design. | The comment at `turn_loop.cc:476-478` claims the nested play is "a flat queue step, not a nested call." It is a nested call: `handle_play_card` → `drain` → `execute(PlayCard)` → `handle_play_card` is genuine C++ recursion on the call stack (verified by reading `action.cc:821-875`, not inferred). It is clone-safe — `Action` is POD with no references into `CombatState`, and each recursion level gets its own local `ActionQueue`/`ResolutionContext` (`drain(state, q, ctx)` at `turn_loop.cc:508` copies `Action a = q.pop_front()` by value in `action.cc:1205`), so this is *not* a repeat of the fixed Split-UAF bug class. Double Tap's own re-trigger is explicitly guarded (`ctx_play.enters_pile` gates the check at `turn_loop.cc:488`, so a replay cannot re-trigger itself). Havoc has no analogous depth guard; chaining is bounded only by how many Havoc copies sit consecutively in the draw pile at execution time, which is correct emergent StS behavior, not a bug, given realistic deck sizes. Net: the comment overclaims flatness where the code is genuinely (safely) recursive. | debt |

`handle_end_turn` (`turn_loop.cc:631-756`) got the line-by-line read it never had. Every actual game effect in it is executor-routed: `DiscardHand` + `TurnEndPlayer` hooks queued and drained (631-647, matches ordering-notes note 7); each enemy's `TurnStartEnemy` hooks + `translate_enemy_move` queued and drained per enemy (676-693); `TurnEndEnemy` hooks queued and drained separately (710-716); `TurnStartPlayer` hooks + the turn-start `DrawCards` queued and drained together (742-750). The only direct mutations are phase orchestration explicitly sanctioned by `action.h:22-24`'s "block resets, acting-slot snapshots, debuff ticks, terminal checks, turn-start draws" list: `tick_debuffs` (649, 719), the phase-start block reset (662-668), the turn-start block reset gated on `block_resets_at_turn_start` (735), the two energy writes (651, 736), the turn/phase counters (660, 740-741), and `select_next_move` advancing the enemy's Markov chain (722, upkeep-adjacent — no hook exists for "enemy chose its next move," unlike `RewriteIntent`, which stays reserved for effects overriding an already-primed intent). Finding 1.4 above is the only note against it, and it's a documentation-staleness issue in a different file, not a code defect.

No `switch` over a scoped enum in `turn_loop.cc` itself — the file contains zero `switch` statements (confirmed by reading the full 956 lines, not grep). The switches consulted for cross-reference while classifying mutations (`execute()` in `action.cc:625-1081`, `ApplyChoice`'s inner switch at `action.cc:987-1035`, `card_qualifies`/`source_pile` at `action.cc:1094-1124`) were each read in full and none has a `default:` case.

#### Slice 2 — Executors + hook dispatch (`action.cc`, `action.h`)

Note on scope: `Action`, `ActionKind`, and `ActionQueue` are not actually defined
in `action.h` — they live in `action_types.h` (158 lines), split out at Stage 4c
so `CombatState` can hold a suspended queue without a circular include. `action.h`
(183 lines, read in full) declares the executors/hooks/mutators that operate on
those types. Both were read in full, along with the entire 1224 lines of
`action.cc` — every `ActionKind` case in `execute()`, not a sampled subset.

| # | Where | Design says | Code does | Severity |
|---|---|---|---|---|
| 2.1 | `action_types.h:94-119` (`Action`) | S1: `amount` is an untyped union, violating decision-points.md R5 inside the `Action` struct. | S1 generalizes beyond `amount`. **`card`** carries two incompatible meanings depending on `kind`: a component of a full `Card` instance (paired with `card_bonus_damage`/`card_upgrades`, reconstructed via `as_card()`) for `ExhaustCard`/`DiscardCard`/`AddCardToPile`/`PlayCard`/`ApplyChoice`; vs. a bare identifier with the instance fields unused for `DealDamage` (the attacking card, set at `turn_loop.cc:264`, read only to pass through to `fire_player_power_hooks`'s `card` param at `action.cc:646`, which `Hook::PlayerAttacked` doesn't even consume), `CardPlayedHook` (`turn_loop.cc:425`, read for its `CardType` at `action.cc:946`), and `RequestChoice` (`turn_loop.cc:451`, the source card, read at `action.cc:959`). **`copies`** is worse — not just unused-per-kind but reused for an unrelated quantity: on `ApplyChoice` it is "how many copies to add" (Dual Wield+, `action.cc:1020`); on `PlayCard` it carries the ORIGINAL play's X value for a Double Tap replay (`turn_loop.cc:497`, `a.copies = x; // reuse the first play's X`, consumed at `action.cc:873`, `pc.forced_x = a.copies;`). Same struct-wide pattern S1 named, confirmed on two more fields. | debt |
| 2.2 | `action.cc:628-700` (`DealDamage`, `DealFixedDamage`, `DamageAllEnemies`, `DamageRandomEnemy`, `DamageRandomEnemyAttack`) | S2: five damage kinds; check whether hook differences are essential or incidental. | Read all five executors and their hook lists. Only two distinct behaviors exist, and both are already centralized in one shared helper each: `player_attack_enemy()` (`action.cc:585-610` — applies Strength/Weak/Vulnerable via `compute_attack_damage`; fires `EnemyDamaged`, `OnAnyDamage`, `EnemyWake` (if was-asleep), `EnemyHpThreshold`) backs `DealDamage`'s player→enemy branch AND `DamageRandomEnemyAttack`, verbatim. `apply_fixed_damage()` (`action.cc:567-581` — no Strength/Weak/Vulnerable; fires `OnAnyDamage`, `EnemyWake` (cond.), `EnemyHpThreshold`, explicitly NOT `EnemyDamaged`) backs `DealFixedDamage`'s to-enemy branch, `DamageAllEnemies`, and `DamageRandomEnemy`, verbatim. The only remaining axis is target selection (explicit `a.target` vs. re-rolled-at-execution random vs. all-living-enemies loop), which is already just a call-site wrapper around the shared helper, not kind-specific hook logic. INCIDENTAL, not essential: collapsible to one `is_attack`-flagged `DealDamage` kind + a target-selector, and the two helpers are already that collapse's implementation, just not exposed as one `ActionKind`. Upgrading S2's severity from the seed's `debt`: the five near-identical names are a live hazard for the next card — picking the wrong one of five silently changes which hooks fire (e.g. an attack-shaped effect built on `DamageRandomEnemy` by name-analogy would silently skip Strength/Weak/Vulnerable and `EnemyDamaged`). | erosion |
| 2.3 | `action.cc:335-381` (`fire_enemy_power_hooks`) | Precedent set by its three siblings: `fire_enemy_hooks` (`action.cc:295-313`) and `fire_player_power_hooks` (`action.cc:433-536`) are `switch`es over `Hook` naming all 16 values (verified exhaustive by reading both in full); ROB-87 relied on exactly this shape (`-Wswitch` catching a missing `Hook::TurnEndEnemy` arm). | `fire_enemy_power_hooks` is not a `switch` at all — it is `if (hook == Hook::TurnEndEnemy) {...; return;} if (hook != Hook::TurnStartEnemy) return; ...`. Adding a `Hook` value this function should react to (a third enemy-power timing) compiles clean with zero warning, unlike its two switch-based siblings and `push_trigger_response`'s `TriggeredAction` switch (`action.cc:230-284`, also verified exhaustive against `enemy.h:175-183`'s 7 values). Same silent-gap shape §1 names for `pile_count`/`kObsCardOrder`. | erosion |
| 2.4 | `action.cc:910-938` (`EnemyEscape`, `EnemySplit`) vs. `action_types.h:97` | The struct's own field comment: `target` is "recipient: kPlayerSlot or an enemy slot"; `actor` is "damage source: kPlayerSlot or an enemy slot" — two fields, two documented roles. | Both kinds set `a.target = actor_slot` (`turn_loop.cc:620`, `turn_loop.cc:626`) to name the enemy that is itself escaping/splitting — the SUBJECT of the action, not a recipient of an effect from elsewhere. `actor` sits unused (`kNoSlot`) on both. Minor next to 2.1, same shape: a field's per-kind meaning drifts without the field's own comment being updated. | debt |
| 2.5 | `action.cc:803-819` (`MakeCardFree`) vs. `action_types.h:70` | Comment on the `ActionKind` itself: "Infernal Blade: `card` costs 0 for the rest of this turn." | The executor never reads `a.card` — it independently rolls its own random Attack (iterates `CARD_DATABASE`, sorts for determinism, `pick(state.rng)`) and marks THAT card free. Confirmed the only construction site (`turn_loop.cc:474`, `Action{ActionKind::MakeCardFree}`) sets no field beyond `kind` — `card` is left at its default (`CardId::Strike`) and is dead for this kind. Harmless today since nothing reads it, but the comment describes a design that isn't what shipped, and would mislead anyone routing a card through this action by setting `.card`. | debt |
| 2.6 | `action.cc`, whole file (S4) | S4: "every mechanic lands in one file," 1168+ lines. | The file's own section-banner comments already name four disjoint seams, none depending on the others except through the shared `CombatState`/`ActionQueue` parameters: (1) `22-173`, Stage-1 mutators — no dependency on `Hook`/`ActionQueue` at all; (2) `179-537`, hook dispatch (`fire_enemy_hooks`, `fire_enemy_power_hooks`, `fire_player_power_hooks` + push-helpers) — ~360 lines, pure "given a hook, push actions," no execution logic; (3) `543-1084`, executors — the largest chunk (~540 lines) and itself one 456-line `execute()` function (`625-1081`) with 28 cases, several long enough to already be pulled into named helpers (`apply_fixed_damage`, `player_attack_enemy`) while others of comparable size (`PlayCard` ~56 lines, `ApplyChoice` ~56 lines) are not; (4) `1089-1201`, mid-card choices (`card_qualifies`, `source_pile`, `build_choice`, `resolve_choice`) — already banner-commented "Mid-card choices (Stage 4c)". These map directly onto `mutators.cc` / `hooks.cc` / `executors.cc` / `choices.cc`, each independently smaller than `turn_loop.cc` ever was. | erosion |
| 2.7 | `action.cc` / `action.h:104-108`, follow-up on 1.2 | `action.h:104-108`: every Stage-1 mutator is "wrapped by the executors ... and still called directly by the (Stage-3-pending) imperative enemy phase" — i.e. direct calls are sanctioned only in the enemy phase. | Grepped and then read every call site of all 12 Stage-1 mutators across `src/` and `bindings/`. Confirms 1.2 independently from the `action.cc` side: `heal_player`/`gain_max_hp` have no `ActionKind` and are called directly from `turn_loop.cc:518,523` — inside `handle_play_card`, not the enemy phase, so this is outside even the comment's own stated exception. Two more mutators share the shape: `spend_energy`/`spend_all_energy` (`turn_loop.cc:93,95`) are never wrapped by an `ActionKind` either — called only at card-cost payment, before that card's queue exists. Plausibly deliberate (cost paid at translation, like `effective_cost`/target selection, ordering-notes #12; StS has no "on energy spent" trigger to make the gap observable) — but it is still uncovered by `action.h`'s own stated exception, so it's recorded rather than assumed different. Every other Stage-1 mutator (`gain_block`, `lose_player_hp`, `gain_energy`, `apply_debuff`, `apply_power`, `move_to_exhaust`, `move_to_discard`, `add_card_to_hand`) is called ONLY from inside `action.cc`'s executors — no bypass found. `draw_one` is called directly once outside an executor, from `draw_opening_hand` (`turn_loop.cc:49`), already commented as sanctioned pre-combat construction. | debt |

**Re-entrancy (question 5, follow-up on 1.5):** read `ApplyChoice` (`action.cc:982-1037`), `EnemySplit` (`920-938`), `Wake` (`779-783`), and `resolve_choice` (`1163-1201`) in full. None of `ApplyChoice`/`EnemySplit`/`Wake` calls `drain` or `handle_play_card` — no additional recursion beyond the one already logged in 1.5. `resolve_choice` does call `drain` (`action.cc:1199`), but only after the pausing `drain` call's frame has already returned — `state.pending_choice.active()` causes the original `drain` to return up through `handle_play_card`/`translate_enemy_move` before `resolve_choice` is ever invoked from `apply_action`, so `resolve_choice`'s `drain` is a fresh top-level call, not nested on top of an open frame. Cross-checked the four `drain` calls in `handle_end_turn` (`turn_loop.cc:641,693,715,750`): each returns (with terminal/pause checks) before the next begins — sequential, not nested. Clean result.

**Clone-safety (question 6):** all dispatch in `action.cc` is `switch`/`if` over scoped enums — `fire_enemy_hooks`, `fire_enemy_power_hooks`, `fire_player_power_hooks`, `execute()`, and the `GeneratedPile`/`ChoiceKind` inner switches. No `std::function`, no virtual dispatch, no type erasure anywhere in the file. `Action` (`action_types.h:94-119`) is POD with no pointers into state. `ActionQueue` (`action_types.h:125-155`) is a fixed `std::array<Action, 128>` ring buffer, no heap allocation. `CombatState::suspended_queue` (`combat_state.h:100`) is a plain `ActionQueue` value member — confirmed `clone()`-safe by construction, not by trusting the comment. Clean result.

**`default:` sweep (question 7):** every scoped-enum `switch` in `action.cc` was read and checked against its enum's full value list: `execute()` over `ActionKind` (28/28 cases, `action_types.h:39-89`), `fire_enemy_hooks` over `Hook` (16/16), `fire_player_power_hooks` over `Hook` (16/16), `push_trigger_response` over `TriggeredAction` (7/7, `enemy.h:175-183`), the `AddCardToPile` inner switch over `GeneratedPile` (3/3, `card.h:308-312`), the `ApplyChoice` inner switch over `ChoiceKind` (7/7), `card_qualifies` and `source_pile` over `ChoiceKind` (7/7 each, `card.h:286-295`). None has a `default:` case. The one gap found is 2.3 above — `fire_enemy_power_hooks` isn't a `switch` at all, so it was never a candidate for this check in the first place, which is itself the finding.

#### Slice 3 — Query layer + the field-bag (`query.cc`, `card.h`)

Note on scope: `query.cc` (137 lines) and `query.h` (53 lines) were read in full.
`card.h` was measured at 981 lines (`wc -l`, matching the brief) and read in
full end to end, including every one of the 189 `CARD_DATABASE` rows — not a
sample. `turn_loop.cc`, `action.cc`, `combat_env.cc` and `bindings/_core.cc`
were read only at the specific call sites needed to confirm who consumes a
query function or a `CardData` field (per the brief: those three files are a
different ticket's concurrent edit and not this slice — line numbers cited
into them are current as of this read but could drift). `docs/design/effects-architecture.md`
§4.5/§2.3 and `ordering-notes.md` notes 12-17 (the Stage 4b entries) were
re-read against what shipped.

| # | Where | Design says | Code does | Severity |
|---|---|---|---|---|
| 3.1 | `card.h:559-568` (`searing_blow_rung`) | Review doctrine (§2.3, ROB-87): a `switch` over a scoped enum with no `default:` gets compiler-checked exhaustiveness; a `default:` silently gives that up. | The only `switch` in `card.h` is over `CardId` — a 189-enumerator scoped enum — and it has a `default: return 0;`. This is the sole `switch`-with-`default:` in the whole slice (`query.cc`'s one `switch`, `base_card_damage` over `DamageRule`, is 4/4 cases with no `default:` — confirmed against the enum's full 4-value list at `card.h:317-325`). The default here is a reasonable, deliberate necessity — nobody should hand-enumerate 189 `CardId`s to ask "which of 5 rungs is this" — but the exposure is concrete, not hypothetical: `is_instance_upgradable` (`card.h:627-629`) already documents that Searing Blow keeps accumulating rungs past `SearingBlow5` via an overflow counter, i.e. the ladder is designed to be extended. If a future `SearingBlow6` enumerator were added and its `case` omitted here, this function compiles clean and silently returns rung 0 for it; `instance_card_damage` (`query.cc:104-105`) then computes `n = 0 + card.upgrades` instead of `n = 6 + card.upgrades` — a silently wrong damage number for the top rung of the game's highest-scaling card, with no warning anywhere in the build. | debt |
| 3.2 | `query.cc:82-87` (`base_card_damage`, `DamageRule::SearingBlow` case) vs. `query.cc:92-106` (`instance_card_damage`) | `query.h:38-41`: `instance_card_damage` is "the only correct entry point" for Searing Blow; `base_card_damage` is the type-level fallback. | Both functions encode the Searing Blow formula independently. `instance_card_damage` computes it generally: `n = searing_blow_rung(card.card_id) + card.upgrades; return n*(n+7)/2+12;`. `base_card_damage`'s own `SearingBlow` case doesn't call `searing_blow_rung` or reuse that expression — it hardcodes the first two terms of the same sequence as literals: `return card == CardId::SearingBlowPlus ? 16 : 12;`. Traced reachability: `base_card_damage` has exactly one caller anywhere in `src/`, `bindings/`, or `python/` — `instance_card_damage` itself (`query.cc:109`) — and that call sits *after* `instance_card_damage`'s own `if (data.damage_rule == DamageRule::SearingBlow)` branch already returns, so no Searing-Blow-family `CardId` can reach `base_card_damage`'s `SearingBlow` case through any path that exists today. It is defensive dead code (the comment says as much: "Reaching here means a caller used the type-level query on a per-instance card"), and were it ever reached by a future caller, it would be *wrong* for 4 of the ladder's 6 rungs (only rungs 0 and 1 match 12/16; `SearingBlow2..5` need 21/27/34/42). Two encodings of one rule, the unreachable one incomplete. | debt |
| 3.3 | `card.h:682-685` (`upgraded_card`) | Its own comment: "Total — never throws, so callers that upgrade a whole pile need no guard" — i.e. built for a whole-pile-upgrade caller. | `upgraded_card` has zero callers in `src/`, `bindings/`, or `python/` — the only place it is ever invoked is `tests/test_card.cc` (three tests). The whole-pile-upgrade caller its own comment anticipates exists — Armaments+'s `upgrades_whole_hand` (`ActionKind::UpgradeHand`, `action.cc:798-801`) and Armaments' single-card choice (`action.cc:1002`) — but both call the *mutating* `upgrade_card_in_place` directly on each `Card&`, never the pure query. Not a live bug (the function is correct for what it claims), but it's untouched production API kept alive only by its own test — and that test (`UpgradedCardIsTotalAndIdentityWhenNotUpgradable`, `test_card.cc:179-189`) has to `continue` past `SearingBlow5` to pass, because `upgraded_card`'s "identity if not upgradable" contract is actually false for that one id (it *is* upgradable, via the instance-counter path the function doesn't model) — the test works around the gap rather than the function documenting it. | debt |
| 3.4 | `card.h:359-447` (`CardData`) vs. `card.h:689-967` (`CARD_DATABASE`) | S3 seed: 46 positionally-initialized fields, "measured fragility, not aesthetics." ROB-87: a mid-struct insertion silently shifts every later positional value in rows that specify it, type-compatible and therefore silent. | Recounted the struct field-by-field (not trusting the seed): **46 fields exactly**, `name` through `generates_random_attack`. Parsed all 189 `CARD_DATABASE` rows with a brace-depth-aware comma counter (cross-checked by hand against 5 rows — Dropkick=25, BattleTrance=24, Clash=21, HeavyBlade=19, BloodForBlood=20 — before trusting it at scale; the longest rows, `InfernalBlade`/`InfernalBladePlus`, hit exactly 46, which is itself a consistency check against the field count). Result: **88 of 189 rows (47%) specify more than 20 fields positionally; 90 (48%) specify 20 or more** — field 20, `cost_drops_per_hp_loss`, is the first Stage-4b query-layer flag, so essentially every card from Tier D onward is exposed. Concretely: inserting one new field anywhere before position ~20 would silently reinterpret the trailing values of roughly half the database — bools and ints and enums are all mutually convertible in this list, so it compiles clean, exactly the ROB-87 shape. Of the three remedies the brief lists, the file already has a live precedent for the third: the 42 ROB-87 rung rows (`SearingBlow2..5`, `Rampage5..30`, `RampagePlus5..40`) are commented as "GENERATED by scratch/gen_rungs.py ... not typed by hand" (`card.h:923-927`) — so 22% of the table is already machine-written; the fragility lives entirely in the remaining 147 hand-typed rows. | erosion |
| 3.5 | `card.h`, whole file | Stage-1-era complaint pattern (cf. S4/2.6): a file mixing concerns that change at different rates. | 981 lines holding four things with different lifecycles, confirmed by reading start to finish rather than inferring from section comments: (a) enum/type declarations (`CardId` and its 9 sibling enums, plus `Card`/`CardData`/`PendingChoice`) — lines 1-357 and 655-679, ~380 lines, changes only when a new mechanism is invented; (b) the upgrade/growth subsystem — `CARD_UPGRADES`, `CARD_GROWTH`, `searing_blow_rung`, `grow_card_in_place`, `is_instance_upgradable`, `upgrade_card_in_place`, `is_upgradable`, `upgraded_card` — lines 449-685 (~235 lines), a self-contained module depending on nothing in the file but `CardId`/`Card`/`CardData`; (c) `CARD_DATABASE` itself — lines 689-967, ~280 lines (29% of the file) for 189 rows, changes with every new card and is the thing 3.4 is about; (d) two display/query one-liners (`card_targets_enemy`, `card_name`) stranded after the entire database, 12 lines. (b) and (c) are the clearest seams: the file's own comment at 923-927 already treats part of (c) as generated data distinct from the rest, so there's an internal boundary the file structure doesn't reflect — splitting (c) out (as `card_database.h`, hand rows and generated rows together or separately) would also make 3.4's fix land more naturally than patching the table in place. | erosion |

**Pull/push split (question 1):** all seven `query.cc` functions
(`effective_cost`, `block_resets_at_turn_start`, `can_draw`,
`base_card_damage`, `instance_card_damage`, `strength_multiplier`,
`is_playable`) take `const CombatState&` (or no state at all for
`strength_multiplier`), never write through it, and never touch RNG — `query.h`
promises "free of RNG" and grepping the file for `rng`/`random` returns nothing
but that promise's own comment. Traced every call site: `effective_cost`
(`turn_loop.cc:116,929`), `block_resets_at_turn_start` (`turn_loop.cc:791`),
`can_draw` (`action.cc:733`, gating the `DrawCards` executor — read, not
written), `instance_card_damage` (`turn_loop.cc:254`, `combat_env.cc:329,369`),
`strength_multiplier` (`turn_loop.cc:278,290`), `is_playable`
(`turn_loop.cc:925`, inside `card_action_is_legal`, whose own comment at
`turn_loop.cc:913-914` says it is "the single source of truth for legality:
`valid_actions` loops it, `apply_action` calls it once" — so the mask/resolution
parity invariant the query layer exists to buy was confirmed structurally, by
following the call graph, not assumed from the docstring). Checked the
converse for the two debuffs `is_playable` reads (Entangle, NoDraw): neither is
independently re-implemented anywhere outside `apply_debuff`'s uniform write
path and this query — no duplicate mask logic found. Clean result.

**Vestigial fields (question 3, the struct side):** grepped all 46 field names
for `.field` outside `card.h`, then read the actual call site for every field
that came back with fewer than 3 hits (23 of the 46) to confirm each is a
genuine `CardData` read and not a same-named field on an unrelated struct or a
test-only reference. All 46 resolve to a real consumer in `turn_loop.cc`,
`action.cc`, or `query.cc` — most single-mechanic flags (`plays_top_of_draw` at
`turn_loop.cc:504`, `energy_when_exhausted` at `action.cc:789`,
`generates_random_attack` at `turn_loop.cc:498`, etc.) have exactly one call
site, which is the expected shape for a flag that exists for one card, not a
sign of dead weight. Zero vestigial `CardData` fields found — 3.4's fragility
is purely positional, not compounded by unread data. `CARD_DATABASE`'s row
count (189) and `kNumCardTypes` (189) still agree, and the file's own
comment/`static_assert` chain (`combat_env.cc:40`) still enforces it — no drift
there either.

#### Slice 5 — Enemy phase + triggered effects (`enemy.h`, `enemy.cc`, Stage 3 paths)

Note on scope: `enemy.h` (313 lines) and `enemy.cc` (1038 lines) were both read in full — every one of the 23 `make_*` factories, not a sample of five, plus `select_next_move`, `max_consecutive_for`, `sample_from_distribution`, and `validate_transitions`. `translate_enemy_move` and `handle_end_turn` (`turn_loop.cc:561-756`) were read in full, cross-referenced against `execute()`, `fire_enemy_hooks`, `fire_enemy_power_hooks`, `fire_player_power_hooks`, and `push_trigger_response` in `action.cc` (all re-read for this slice, not taken on Slice 2's word). The brief's pre-measured counts were checked rather than trusted (see 5.3) — one was wrong, which is itself relevant to a slice whose brief is "don't trust unverified numbers."

| # | Where | Design says | Code does | Severity |
|---|---|---|---|---|
| 5.1 | `action.cc:1063-1079` (`CheckDeath`) vs. `turn_loop.cc:428`, `636-647`, `676-693` | `effects-architecture.md` §4.4 lists `EnemyDeath` and `BecameLastEnemy` as hooks in the unified vocabulary with no scope restriction; `action.h:31` frames `CheckDeath` as *the* mechanism deaths are "processed by." | `CheckDeath` — the only action that ever reads `ResolutionContext::died_slots`/`died_count` and turns them into `Hook::EnemyDeath`/`Hook::BecameLastEnemy` firings — is queued from exactly one call site in the entire codebase: `turn_loop.cc:428`, inside `handle_play_card`. Two other drains can also kill an enemy via `apply_fixed_damage` (which correctly calls `ctx.record_death`, `action.cc:580`) but never follow up with a `CheckDeath`: (1) the end-of-player-turn drain (`turn_loop.cc:636-647`) — Combust's `DamageAllEnemies` (`action.cc:461`), and the block's own comment reads "Combust can kill the player, and can clear the room"; (2) each enemy's per-turn drain (`turn_loop.cc:676-693`) — Flame Barrier's retaliation (`Hook::PlayerAttacked` → `DealFixedDamage` at `a.actor`, `action.cc:517-525`), which targets the very enemy whose attack triggered it and executes inside that enemy's own drain. In both cases the `ResolutionContext ctx` is function-local and silently discarded once the block exits. Traced the full chain for the second case: `translate_enemy_move` pushes `DealDamage(actor=slot, target=Player)` → `execute()`'s player-facing branch calls `fire_player_power_hooks(..., Hook::PlayerAttacked, q, a.card, a.actor)` → Flame Barrier pushes `DealFixedDamage(target=slot)` onto the *same* queue → next drain iteration runs `apply_fixed_damage(slot, ...)` → if it kills, `ctx.record_death(slot)` fires and is never read again. Concretely: Fungi Beast's Spore Cloud (`Trigger::OnDeath`) does not apply its Vulnerable if the Beast dies to Combust or to Flame-Barrier retaliation instead of card damage; Shield Gremlin's `Protect`→`ProtectAlone` rewrite (`Trigger::BecameLastEnemy`) does not fire if its last ally dies the same way, so it falls back to self-blocking Protect forever (via `translate_enemy_move`'s `blocks_ally` fallback) instead of ever reaching Shield Bash. Win/loss detection is unaffected (`check_enemy_terminal`/`check_character_terminal` scan `hp` directly), which is exactly why this is invisible to outcome-based tests. Confirmed untested: `FlameBarrierRetaliatesAgainstAttackerEvenWhenBlocked` (`test_turn_loop.cc:2249`) checks only the HP delta; the one test exercising Shield Gremlin's `BecameLastEnemy` rewrite (`KillingAllyRewritesShieldIntentToProtectAlone`, `test_turn_loop.cc:1496`) kills the ally with a Strike card — the one call site that *does* queue `CheckDeath`. Both Combust and Flame Barrier are Stage 4a additions, arriving after Stage 3 built the enemy-phase drain; the death-bookkeeping action never got extended to the two new places that can now kill enemies. | regression |
| 5.2 | `enemy.h:163-173` (`Trigger`, 7 values) vs. `action.h:53-71` (`Hook`, 16 values) vs. `action.cc:289-333` (`fire_enemy_hooks`'s mapping switch) | `action.cc:290-292` and `effects-architecture.md` §4.4 frame the split as temporary: "the existing `TriggeredEffect` tables migrate onto the same vocabulary... the two enums unify when the two-regime period ends." | Every one of `Trigger`'s 7 values maps 1:1 onto a `Hook` value (`OnDamaged`→`EnemyDamaged`, `OnAnyDamage`→`OnAnyDamage`, `HpAtOrBelow`→`EnemyHpThreshold`, `BecameLastEnemy`→`BecameLastEnemy`, `OnPlayerSkill`→`CardPlayed`, `OnDeath`→`EnemyDeath`, `OnWake`→`EnemyWake`), and the mapping switch does nothing but rename — no branching logic depends on which vocabulary is in play. `Hook` already carries every enemy-relevant event `Trigger` needs and has for as long as it's had all 16 cases; both switches that consume it (`fire_enemy_hooks`, `fire_player_power_hooks`) are already exhaustive. There is nothing left to migrate — the target of the migration already fully exists. What the split still buys: `TriggeredEffect.trigger : Trigger` cannot *name* a player-only event (`TurnStartPlayer`, `CardExhausted`, ...) — a construction-time restriction. Collapsed onto `Hook`, that mistake would compile cleanly and silently no-op inside `fire_enemy_hooks`'s switch (which already `return`s early for the 9 non-enemy hooks) instead of failing to typecheck. Cost of unifying: delete `Trigger`, delete the 44-line mapping switch, retarget `TriggeredEffect::trigger` to `Hook`, and rename the `Trigger::X` literals at every `triggered_effects.push_back` call site across 7 factories (`make_louse` (shared), `make_fungi_beast`, `make_acid_slime_l`, `make_spike_slime_l`, `make_mad_gremlin`, `make_shield_gremlin`, `make_lagavulin` (×3), `make_gremlin_nob`) — mechanical, not a redesign. | erosion |
| 5.3 | `enemy.h:42-107` (`MoveName`) | Brief (measured to save calls): "`MoveName` has 49 enumerators." ROB-76 comment (`enemy.h:64-67`): pseudo-states "encode turn/phase/cycle position ... sharing another move's data." | Mechanically recounted twice (full read, then an `awk`-strip-comments-and-count pass over the enum body): **54 enumerators**, 53 excluding the `None` sentinel — not 49. Classified all 53: **32 real, distinct moves** (own damage/block/debuff/power signature or own behavior flag, even where the *name* repeats across enemies — Bellow for Jaw Worm and Gremlin Nob, Bite/Tackle/Lick across Louse/Slime families). **17 pseudo/position-marker states**: OpenerStab, CycleScrape1, CycleScrape2, CycleStab, Mug1, Mug2, Charge1, Charge2, Charge3a/b/c, ProtectAlone, Sleep1, Sleep2, Sleep3, LagavulinAttack1, LagavulinAttack2 — note `CycleStab` is missing from the brief's own illustrative list even though `enemy.h:64-67`'s comment names it as one of the four Red Slaver pseudo-states alongside `OpenerStab`/`CycleScrape1`/`CycleScrape2`, which is itself a small case of the miscount problem. Of the 17, one is not a pure alias: `Sleep3` is built from its own `Move` object with `wakes_on_resolve = true` (`enemy.cc:886-887`), genuinely different data from the shared `sleep` object backing `Sleep`/`Sleep1`/`Sleep2`, because the wake trigger has to key off one specific turn — so "position marker sharing another move's data" doesn't hold uniformly; at least one pseudo-state needed real data of its own. **4 are dead**: bare `Mug`, `Charge`, `Sleep`, `LagavulinAttack` each sit in their factory's `moves` map with real (if all-zero) `Move` data (`enemy.cc:578-580` Looter/Mugger, `784-787` Gremlin Wizard, `884-897` and `889-900` Lagavulin) but are never named by any `first_turn_move` or `transitions` entry — confirmed by grepping every one of `src/`, `bindings/`, `python/`, `tests/` for each identifier; every hit is the two lines inside the identifier's own factory. These are the "base" moves the pseudo-states were introduced to alias, left behind, structurally unreachable via `select_next_move`. The scheme is not hypothetically a hazard — it has already produced 4 unreachable enumerators (roughly 7-8% of the whole `MoveName` vocabulary), concretely from being reused 5 times past the enemy (Red Slaver) it was invented for. | erosion |
| 5.4 | `enemy.cc:256-264` (`validate_transitions`), `enemy.cc:44-72` (`select_next_move`), `CMakeLists.txt:8-9` / `pyproject.toml:67` | Brief: "what can it actually detect, and what can it not?" | `validate_transitions` (and its two inlined equivalents in `make_jaw_worm`/`make_cultist`, `enemy.cc:109-116`, `159-165`) only sums `probability` across every `MoveTransition` in a `dist` that is *already a value present in the `transitions` map* and asserts the sum is ~1.0. **Catches:** a distribution present in the table whose probabilities don't sum to 1. **Misses, by construction:** (a) a `next_move` naming a `MoveName` absent from `e.moves` — nothing cross-references `MoveTransition::next_move` against `moves`'s keys; a bad reference surfaces only as a `std::out_of_range` throw from `moves.at(*last_move)` (`turn_loop.cc:687`), and only once that exact state is reached; (b) an unreachable `(MoveName, consecutive)` key — no reachability walk from `first_turn_move` is performed, so the 4 dead moves in 5.3 are invisible to this check by the same blind spot that would hide a genuine typo; (c) a *missing* `(move, consecutive)` key that leaves a reachable enemy with no legal next move — only `select_next_move`'s own `assert(it != enemy.transitions.end())` (`enemy.cc:60-61`) would ever catch this, and only at the moment gameplay reaches that exact state, never at construction time. Compounding (c): `CMakeLists.txt:8-9` defaults `CMAKE_BUILD_TYPE` to `Release`, and `pyproject.toml:67` pins the scikit-build-core extension to `cmake.build-type = "Release"` — the binary Python actually imports for RL training is built with `NDEBUG` defined, where `assert()` compiles to nothing. In that build, a missing table entry would not trip the assert at all: `enemy.transitions.find(...)` returns `end()`, and `sample_from_distribution(it->second, rng)` dereferences that `end()` iterator — undefined behavior, not a clean failure, in the exact build that continuously samples random enemy AI during training. The `(void)sum;` line in `validate_transitions` and its two inlined siblings exists specifically because `sum` becomes otherwise-unused once the assert consuming it is compiled away — the code already acknowledges its own check disappears in Release. | erosion |

**Queue coverage (question 4):** `translate_enemy_move` (`turn_loop.cc:561-629`) was read in full: damage, block (including the `blocks_ally` random-ally roll), debuffs, powers, `adds_to_discard`, `wakes_on_resolve`, `escapes`, and `splits` are all built as `Action` values and pushed to `q` — zero direct `CombatState` writes. The only direct mutation reachable from the enemy-phase path is `select_next_move`'s own `last_move`/`consecutive_count` advance (`enemy.cc:65-70`), called from `handle_end_turn:722` — already classified by Slice 1 as sanctioned upkeep (no hook exists for "enemy chose its next move"); confirmed independently from the `enemy.cc` side here, not re-reported as a new finding. The 23 `make_*` factories mutate a freshly-constructed `Enemy` at pre-combat construction time, the same sanctioned category as `draw_opening_hand` (2.7) — not resolution-time state. The one gap in "does every enemy effect go through the queue" is not a direct-mutation regression but a *missing-action* regression: 5.1.

**`default:` sweep, enemy side (question 5):** `enemy.cc` itself contains **zero** `switch` statements — all AI selection is table-driven (`moves`/`transitions` maps), so there is no `switch` over `MoveName` anywhere in the codebase to lose exhaustiveness on (confirmed by grep across `src/`, `bindings/`, `python/`, `tests/`; the absence is structural — Move behavior is data on the struct, e.g. `escapes`/`splits`/`blocks_ally`/`wakes_on_resolve`, never a per-name branch, so a 54th or 55th `MoveName` needs no switch update anywhere). No `switch` over `Trigger` exists either — it's consulted only by equality (`fx.trigger != which`) inside `fire_enemy_hooks`'s loop. The only `switch` over `TriggeredAction` is `push_trigger_response` (`action.cc:230-284`): 7/7 cases, no `default:`. The only `switch` over `EnemyKind` is `enemy_name` (`enemy.h:285-310`): 23/23 cases, no `default:` — the trailing `return "?";` sits *outside* the switch, so it is not a `default:` label and does not suppress the `-Wswitch` warning ROB-79's own comment relies on. Clean.

**Recursion (question 6):** traced every path that can push an `Action` during the enemy phase: `translate_enemy_move` (pre-drain, builds the move's own actions), `fire_enemy_hooks`/`push_trigger_response` (called both at translation time for `wakes_on_resolve` and at execution time from inside `apply_fixed_damage`/`player_attack_enemy`), and `fire_player_power_hooks`'s `PlayerAttacked` arm (Flame Barrier retaliation, pushed from inside the `DealDamage` executor). All three only ever `push_back`/`push_front` onto the *same* queue instance already being drained by the one top-level `drain(state, q, ctx)` call in `handle_end_turn`'s per-enemy loop (`turn_loop.cc:693`) — none calls `drain` or `handle_play_card` re-entrantly. No enemy `Move` or `TriggeredAction` ever constructs a `PlayCard`/`RequestChoice` action, so the two known recursive paths (1.5's Double Tap/Havoc replay, and `ApplyChoice`'s `resolve_choice`, both Slice 2) are unreachable from the enemy phase. Clean — no recursive or nested resolution path found in the enemy phase.

#### Slice 4 — Obs encoding + bindings (`combat_env.{h,cc}`, `bindings/_core.cc`)

Note on scope: `combat_env.h` (197 lines) and `combat_env.cc` (389 lines) were both
read in full, including every `static_assert`, not a sampled subset. So was the
entire 516 lines of `bindings/_core.cc`. Cross-referenced against `enemy.h`
(the `Move`/`Enemy` structs), `combat_state.h` (`Character`/`CombatState`
field-by-field), `status_effect.h` (`Debuff`/`Power` layout contract),
`card.h`'s `PendingChoice`, `docs/design/observation-space.md`, and
`docs/design/decision-points.md`. Also read `tests/test_combat_env.cc` in full
(536 lines) and `python/tests/test_bindings.py` in full, to check what existing
coverage actually exercises rather than assume from the brief's description.

| # | Where | Design says | Code does | Severity |
|---|---|---|---|---|
| 4.1 | `combat_env.cc:224-268` (enemy intent block in `compute_obs`) vs. `enemy.h:111-140` (`Move`), `enemy.cc:584,636,692` (`Escape`/`Split`) | `observation-space.md` §1: "the observation should match what a human player can see"; "'the agent cannot see X, which a human can' is a defect." `combat_env.h:65-69` frames the 4-float tuple as *the* intent encoding (is_attacking, atk_dmg, is_blocking, is_buffing). | The intent block reads only `m.damage`, `m.block`, `m.applies_debuffs`/`m.applies_powers` — never `m.escapes`, `m.splits`, or `m.adds_to_discard`. Confirmed by construction: Looter/Mugger's Escape (`enemy.cc:584`, `Move{Escape, 0, 0, {}}`) and Acid Slime L / Spike Slime L's Split moves (`enemy.cc:636,692`, `Move{Split, 0, 0, {}}`) both produce the intent tuple `(0,0,0,0)` — bit-for-bit identical to an enemy that will do nothing this turn. Real StS renders Escape and Split as their own distinct intent icons, not attack/block/buff; a human sees "this enemy is about to leave the fight" or "this enemy is about to become two enemies" before it happens, and the observation does not. Related, same root cause: `adds_to_discard` (Acid Slime L's Corrosive Spit adds 2 Slimed, `enemy.cc:631-632`) is also unrepresented — an attack that also curses the discard pile is observationally identical to a plain attack of the same damage. Also related: Shield Gremlin's Protect (`blocks_ally=true`, `enemy.cc:827-828`) sets `is_blocking=1` on the ACTING enemy's own intent slot, but the block actually lands on a randomly-chosen living ally — the signal is on the wrong enemy's slot whenever an ally is alive (only correct in the alone-fallback case). No test exercises any of this: `test_combat_env.cc`'s only intent test, `ObsIntentFirstTurnIsChompAttack`, checks a plain attack; nothing constructs an Escape/Split/Protect scenario and inspects the intent floats. | regression |
| 4.2 | `combat_env.cc` (`compute_obs`, whole function) vs. `combat_env.h:173-175` (`enemy_kinds()` comment: "The obs doesn't carry kind (not a stat)") | `observation-space.md` §1, same governing rule. A human sees the enemy's name/sprite from the first frame of every fight and plans around identity (Enrage vs. Skills, an eventual Split, an eventual Escape). | `Enemy::kind` never reaches any obs float — `enemy_kinds()` (`combat_env.h:173-175`, `combat_env.cc:381-386`) is a debug-only TUI accessor, explicitly not part of the training observation. This is a real aliasing hazard, not a theoretical one: enemy `max_hp` is also withheld (ROB-59, deliberately — current hp + intent covers lethality, a reasoned exemption), so the only per-enemy signals left are hp, block, status stacks, and the (per 4.1, incomplete) 4-float intent. Red Louse (HP 10-15, `enemy.cc:242`) and Green Louse (HP 11-17, `enemy.cc:249`) HP ranges overlap at 11-15, and their moves differ (Grow vs. SpitWeb) — two fights can reach the same current-HP, same-empty-status, same-coarse-intent state while being different enemies with different full transition tables, and nothing in the observation lets the agent use that identity for anything beyond the single already-telegraphed move. Neither `observation-space.md` §3's coverage audit (6 gaps, all now fixed) nor `decision-points.md` discusses this omission — unlike enemy `max_hp`, it is not a documented, ruled exemption; "not a stat" is not one of §1's stated exemptions (only "a human cannot see it either" is, and a human plainly can see which enemy this is). | regression |
| 4.3 | `combat_env.cc:46-85` (`kObsDebuffOrder`, `kObsEnemyPowerOrder`, `kObsPlayerPowerOrder`) vs. `status_effect.h:74-76` ("static_asserts enforce the counts match") | ROB-87's own lesson, restated in this review's §1: a size-only assert lets a duplicate-plus-missing pair compile clean and silently corrupt the observation. | All three static_asserts (`combat_env.cc:53-54,67-68,79-80`) check `.size() == kNum...` only — none constrains content. A hand-edit listing `Power::Strength` twice in `kObsEnemyPowerOrder` and omitting `Power::Dexterity` still satisfies `size()==6`, compiles clean, and silently duplicates enemy Strength into the Dexterity slot while making enemy Dexterity permanently unreadable — the identical `kObsCardOrder` failure mode, just on 6- and 22-entry tables instead of 154. The fourth assert (`combat_env.cc:83-85`) — `kObsEnemyPowerOrder[kNumEnemyPowers-1] == kObsPlayerPowerOrder[kNumEnemyPowers-1]` — does check content, but only the one SHARED LAST INDEX, not the prefix relationship the comment above it claims ("the enemy order must be a prefix of the player order… so a stack means the same thing in both blocks"). Concretely: swapping `Power::Strength` and `Power::Dexterity` within `kObsEnemyPowerOrder`'s first two entries leaves index 5 (`Artifact`) untouched in both arrays, so this assert still passes, while the enemy block would now read Strength where the player block calls it Dexterity and vice versa. No test closes the gap: `test_bindings.py`'s `test_every_debuff_is_bound` / `test_every_player_power_is_bound` are explicitly count-only — unlike `test_card_id_values_are_dense_and_contiguous`, whose own comment states it exists to be "stronger than a count" for exactly this reason, a strengthening never extended to `Debuff`/`Power`. `test_combat_env.cc` has no test that applies a specific named `Debuff`/`Power` and checks it lands at the index its own enum identity predicts. | erosion |
| 4.4 | `combat_env.h:82-83` (`kEnemyObsStride` formula) vs. `combat_env.cc:229` (`kStatusOff`) | `combat_env.cc:195-196`'s own framing: "Section offsets are derived from constants so the layout has no hand-maintained magic numbers." | The leading "3" (is_alive + hp + block) is a bare literal in two places with no shared symbol tying them together: once inside `kEnemyObsStride`'s formula in the header, again as `kStatusOff = 3` in the .cc. No static_assert connects them; they currently agree only because nobody has touched either since Stage 4a. Adding a fourth enemy base-stat float (the enemy-side analogue of the player block's `hp_loss_events`/`combust_casts`) would require updating both literals in lockstep with no compiler check if one were missed — status/intent would silently read from the wrong offset. The same shape recurs at smaller scale and lower risk in the player block (`o[0..6]` against `kPlayerBaseSize=7`) and the choice block (`o[kChoiceBase+0..4]` against `kChoiceHeaderSize=5`; `o[base+0..2]` against `kChoiceSlotStride=3`) — lower risk because in those cases the literal and its size constant sit in the same function, not split across a header and its .cc. | erosion |
| 4.5 | `bindings/_core.cc:45-56` (`make_info`) vs. `combat_env.cc:176-183` (`CombatEnv::step`, `Outcome::Won` branch) | `CLAUDE.md`: "Keep pybind11 bindings thin — no game logic in bindings/." | `make_info`'s `info["hp_fraction"]` independently re-derives `c.max_hp > 0 ? hp/max_hp : 0.0f` — the identical guarded division `CombatEnv::step()` already computes for the win-reward shaping term. Harmless and non-mutating (a presentation float, not a simulated game rule), so it does not rise to "game logic" the way an executor would — but it is a formula duplicated across the pybind11 boundary rather than read from one source; a future change to the guard or the shaping formula in one spot would not be caught by the other. Every other binding helper (`card_data`, `card_targets_enemy`, `card_name`, `enemy_name`, `is_upgradable`, `upgraded_card`, the deck-conversion lambda in `CombatEnv`'s `py::init`) is a thin, direct forward to an existing engine function or table — no other duplicated logic found. | debt |

**Zero-copy safety (question 4):** `obs_view`/`mask_view` (`bindings/_core.cc:23-40`) construct `py::array_t` directly around `self.obs().data()` / `self.action_mask().data()` with `py::cast(&self, py::return_value_policy::reference)` as the `base` handle — the standard pybind11 keep-alive idiom, so the wrapping Python `CombatEnv` object (and therefore the C++ instance behind its `unique_ptr` holder) cannot be garbage-collected while a view exists. No copy in either function. `obs_buffer_` is `std::array<float, kObsSize>` (`combat_env.h:181`), a fixed-size inline member that structurally cannot reallocate — its address is stable for the object's lifetime by construction. `mask_buffer_` is `std::vector<uint8_t>` (`combat_env.h:184`), which *could* reallocate in general, but is sized exactly once, identically, in both constructors' member-init lists (`combat_env.cc:117,143`, both `kNumActions`) and is never resized, `.reserve()`d, `.push_back()`ed, or reassigned anywhere else (confirmed by reading every use of `mask_buffer_` in both files) — `compute_mask()` only writes through existing indices. So the invariant holds, but it is discipline-based (no resize call exists) rather than type-guaranteed (unlike `obs_buffer_`'s `std::array`) — worth noting as a weaker form of the same guarantee, not a bug. `clone()` (`CombatEnv clone() const { return *this; }`) is the compiler-generated copy: it deep-copies `mask_buffer_` (new heap allocation) and copies `obs_buffer_`'s floats into the new object's own inline array, so a clone's buffers are independent of the original's and neither invalidates the other's outstanding numpy views. Clean result.

**`default:` sweep (question 6):** only two `switch` statements exist in this slice's files, and `bindings/_core.cc` has zero. `choice_source_pile` (`combat_env.cc:90-105`) switches over `ChoiceKind`'s 7 values (`None`, `UpgradeCardInHand`, `HandToTopOfDraw`, `DiscardToTopOfDraw`, `ExhaustToHand`, `CopyAttackOrPowerInHand`, `ExhaustCardInHand`, per `card.h:286-295`) — all 7 are explicit cases (4 fall through to `return 0`, one each for discard/exhaust, `None` breaks to the trailing `return 0`), no `default:` label. `CombatEnv::step`'s `switch (state_.outcome)` (`combat_env.cc:174-187`) covers all 3 `Outcome` values, no `default:`. Both would lose `-Wswitch` coverage the instant a new enum value (e.g. a v2.0.0 `ChoiceKind`) is added without a matching arm. Clean result.

**Layout derivation (question 5):** most section offsets in `compute_obs` are genuinely derived — `kEnemyBase = kPlayerObsSize`, `kPileBase = kEnemyBase + kMaxEnemies*kEnemyObsStride`, `kStride = kNumCardTypes`, `kTurnOff = kPileBase + kPileObsSize`, `kChoiceBase = kTurnOff + 1`, `kIntentOff = kStatusOff + kEnemyStatusSize` are all named-constant arithmetic, not hand-counted. The one place this breaks down is `kStatusOff`'s literal `3`, covered as 4.4 above — the only *cross-file* duplication found, and the only one with zero static_assert linking it to its sibling.

**What was not read, and why:** `query.cc`'s `instance_card_damage` (Slice 3's territory) was read only at its two call sites in this slice (`combat_env.cc:329`, `combat_env.cc:369`) to confirm the choice-block payload wiring, not audited internally for correctness — that belongs to Slice 3. `card.h`'s `CardData`/`PendingChoice` were read for field lists, not for the fragility already logged as S3. The 100+ `.value()` enum-binding blocks in `bindings/_core.cc` (`CardId`, `EnemyKind`, `Power`, `Debuff`, `ChoiceKind`) were read for exhaustiveness by cross-referencing `test_bindings.py`'s existing count tests rather than hand-counted a second time — `test_bindings.py` already does this mechanically and correctly for `CardId` (both count and dense/contiguous); the 4.3 finding is specifically that the same strengthening was never applied to `Debuff`/`Power`. Python-side consumers of `CombatEnv` (`python/minispire/env.py`, the TUI) were not read — out of scope for this slice's two files.

---

## 5. Triage

All five slices are done. Slices 3 and 4 were deferred at first to get the
v1.0.0 correctness path moving, then run under ROB-94; their findings are 3.1–3.5
and 4.1–4.5 above. 16 findings.

### Fixed since the review

| finding | issue | what shipped |
|---|---|---|
| **5.1** `CheckDeath` from one site | ROB-90 | `process_deaths()` after the end-of-player-turn and enemy-phase drains. |
| **1.1 / 1.2** direct writes after `drain` | ROB-91 | `NoDraw` routed through `ApplyDebuff`; new `Heal` / `GainMaxHp` ActionKinds for Reaper and Feed. |
| **4.1** intent block incomplete | ROB-96 | Intent widened 4 → 7. `is_escaping` / `is_splitting` added; `is_buffing` split into buff (self) and debuff (player), which is where `adds_to_discard` lands — StS shows Corrosive Spit as a debuff intent, so it needs no bit of its own. |
| **4.2** `Enemy::kind` absent from the obs | ROB-96 | One-hot over `kNumEnemyKinds` per slot. `OBS_SIZE` 1642 → 1772. |

**4.1's Protect sub-point is NOT a defect, and is now moot anyway.** The slice
argued that Shield Gremlin's Protect sets `is_blocking` on the acting gremlin
while the block lands on a random ally, so "the signal is on the wrong enemy's
slot". But StS floats the intent icon above the enemy *taking the action*, and a
human watching a Shield Gremlin telegraph Protect is likewise not told which
ally receives it. Parity holds. What was genuinely missing is that the agent
could not tell it was looking at a Shield Gremlin at all — which is 4.2, and the
kind one-hot now makes "this creature's block goes elsewhere" learnable.
**Confirmed against the game by Rob (2026-08-02): the intent shows over the
Shield Gremlin.** Current model is correct; no change needed.

### Verified by the lead (read the code, not the report)

| finding | verified | why it matters |
|---|---|---|
| **5.1** `CheckDeath` queued from one site only | `grep` for every queue site returns exactly `turn_loop.cc:428` | **A live parity bug.** An enemy killed by Combust or Flame Barrier never fires `EnemyDeath`/`BecameLastEnemy`, so Fungi Beast's Spore Cloud silently does not go off. The wiki explicitly covers this case ("will still activate ... ex. from Thorns damage"). No test caught it because win/loss scans HP directly. |
| **1.5** `PlayCard` re-entrancy | the executor calls `handle_play_card`, which builds its own `ActionQueue` (turn_loop.cc:145) and drains it (:508) | The migration MOVED the recursion rather than removing it. The comment at `action.cc:822` asserts the opposite, which is worse than no comment. |
| **1.1** `NoDraw` written directly | `turn_loop.cc:512` writes the debuff map after `drain()` | Bypasses `apply_debuff`'s Artifact gate. Introduced same-day by ROB-88; unobservable only because nothing grants the player Artifact in v1. |
| **2.2** five damage kinds | all five funnel into `player_attack_enemy` or `apply_fixed_damage` | Two behaviours × three target selectors, already centralised. A real consolidation, not taste. |
| **2.3** `fire_enemy_power_hooks` is `if`-based | its two siblings use `switch (hook)`; it does not | It has no `-Wswitch` protection. ROB-87 *relied* on that protection when adding `Hook::TurnEndEnemy` — the warnings fired in the two switches, and this function needed hand-editing with no warning. |
| **5.3** `MoveName` count | 54, not the 49 the lead pre-measured | The lead's regex matched one enumerator per line; several lines declare three. Handed to the agent as a saved-work measurement, and wrong — the exact failure §2 warns about. |

### Proposed disposition

**Fix before v1.0.0** (correctness, small, testable):

- **5.1** — `CheckDeath` after any drain that can damage an enemy. Needs a
  regression test per path: Combust kill, Flame Barrier kill, card kill.
- **1.1** — route `NoDraw` through `ApplyDebuff`.
- **1.2** — add `Heal` / `GainMaxHp` ActionKinds, or document why Reaper and Feed
  are exempt from the single-write-path rule.

**Fix soon, not release-blocking:**

- **2.3** — make `fire_enemy_power_hooks` a `switch` so it gets exhaustiveness
  checking like its siblings. One-line shape change, removes a whole failure
  class.
- **5.4** — `validate_transitions` is `assert`-based and the project builds
  Release (`NDEBUG`) by default, so in the binary used for training a malformed
  transition table is undefined behaviour rather than a clean failure. Either
  make it a real runtime check or a compile-time one.
- **1.5 / action.cc:822** — correct the comment. It currently tells the reader a
  solved problem where there is an open one.

**Consolidations — own issue, post-release:**

- **2.2** five damage kinds → one kind + target-selector + `is_attack`.
- **5.2** unify `Trigger` into `Hook` (the "two-regime period" has ended; the
  mapping switch is pure renaming).
- **5.3** delete the 4 structurally dead `MoveName` enumerators (`Mug`,
  `Charge`, `Sleep`, `LagavulinAttack` — real `Move` data unreachable via any
  transition).
- **S1 / 2.1** typed `Action` fields instead of the `amount`/`card`/`copies`
  unions.
- **S3** `CardData`'s 46 positional fields.
- **S4 / 2.6** split `action.cc` along its own section banners.

**Doc corrections:** 1.4 (`effects-architecture.md` §4.7 promises a nonexistent
`EndOfTurnUpkeep` executor), 2.4, 2.5 (comments describing fields the code does
not read).

### The deferral was wrong, and slice 4 is why

Slices 3 and 4 were deferred on the reasoning that S3's `CardData` finding was
already well characterised, and that slice 4's ground had been "rewritten
wholesale by ROB-87/88/89 and is freshly covered" — so neither should block
v1.0.0.

That second claim was exactly backwards. Slice 4 found the two worst defects in
the review (4.1, 4.2, both fixed in ROB-96): the enemy block carried no identity
at all, and encoded a fleeing Looter identically to an enemy doing nothing.
Recently-rewritten code was treated as *less* likely to need review, when
ROB-87/88/89 had just moved every landmark in it. Freshness is not coverage.

Keep this in mind for the next review: "we just changed it" argues for looking,
not for skipping.

### Method note for whoever runs the rest

Every slice found something in code the lead had touched that same day, and the
one number the lead pre-measured for the agents was wrong. The review works
because it reads rather than samples. Do not shortcut that.

---

## Appendix: specific things to verify by reading

- **Every mutation site in `turn_loop.cc`**, classified as executor-routed,
  deliberate upkeep (phase block reset, energy refill, terminal checks),
  construction (`start_combat`, `draw_opening_hand`), in-flight card removal, or
  **regression**. The 53-hit sweep is a starting list, not an answer.
- `handle_end_turn` never got the line-by-line audit `handle_play_card` did.
- Things deliberately outside the queue — is that still deliberate? The
  `no_draw_this_turn` handling (now `Debuff::NoDraw`), the hand-exhaust
  partition, terminal checks, phase upkeep.
- **Any other recursive or nested resolution paths** besides the two already
  fixed.
- Does every `ChoiceKind` / `ActionKind` / `Power` / `Debuff` appear in every
  switch that consumes it? Scoped enums without `default:` give compile-time
  coverage — confirm no switch has grown a `default:`, which silently disables
  it. (ROB-87 relied on this working: adding `Hook::TurnEndEnemy` produced
  `-Wswitch` warnings in exactly the right places.)
- `ordering-notes.md` divergences — still accurate after 4a–4c and after
  ROB-85/87/88 moved Ritual, Metallicize and the pile planes?
- `compute_mask` allocates a `std::vector<bool>` per step and copies it into the
  byte buffer; `compute_obs` zeroes all 1,642 floats before overwriting most of
  them. Both known (ROB-81), both deferred because they need an API change to
  `valid_actions`, which the mask oracle deliberately calls.
