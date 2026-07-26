# Effects Architecture: the Action Queue

**Status:** Accepted (2026-07). Implementation staged; see [Migration plan](#migration-plan).
**Owner:** Rob. **Scope:** the combat engine's effect-resolution spine (ROB-80 Tier C+ and beyond).
**Decision:** all game-state mutations flow through a serialized **action queue** (the
architecture of the real Slay the Spire). Direct-call resolution and the hybrid
mutator-only design were considered and rejected — see [Rejected alternatives](#rejected-alternatives).

---

## 1. Why now

Two forcing functions arrived together at ROB-80 Tier C:

1. **Player powers** (Demon Form, Combust, Juggernaut, Rupture, Feel No Pain, ...)
   introduce *reactive* effects: game events (exhaust, block gain, HP loss, draw)
   must notify listeners that respond with further mutations. The existing
   architecture has no general event mechanism — the enemy `TriggeredEffect`
   system fires from five hand-placed call sites woven into the imperative flow.
2. **Meta-action cards** (Tier E: Double Tap, Havoc, Armaments, Headbutt, Exhume,
   Dual Wield, True Grit+, Warcry) *play or manipulate cards from inside a card's
   resolution*, and several require **player choices mid-resolution** ("upgrade a
   card in your hand", "choose a card from your discard pile").

### Current-state assessment (what is strained)

| Component | Pattern | Problem |
|---|---|---|
| `turn_loop.cc` | Imperative god-file (~900 lines): card resolution, enemy phase, triggers, mask/decode, construction | Every mechanic lands here; all concerns meet in one file. |
| `CardData` | Field-bag (`damage`, `block`, `draw`, `energy`, `lose_hp`, ...) resolved in one fixed hardcoded order | Resolution order is implicit; per-card ordering can't be expressed; each tier adds fields + a step. |
| Enemy `TriggeredEffect` | Data-driven event→action table, fired from 5 hardcoded sites | Sound pattern, but each new *event kind* means weaving a new fire site into the flow. Cannot express modifiers (see §3). |
| Mutations | Scattered direct field writes (`character.current_block += ...` in ≥4 places; HP writes in several) | No reactive system can be built on scattered mutations — Juggernaut needs *every* block gain to pass one point. |

### The empirical argument

The two worst bugs in this project's history were both
**mutation-during-nested-resolution** bugs:

- the Split reallocation use-after-free (`Enemy&` held across `place_child` →
  `push_back` → dangling reference; segfault on Linux CI),
- the same-phase split-child ordering bug (children spawned mid-loop acted in
  the phase they were born).

Reactive powers multiply exactly these interleavings (exhaust → Feel No Pain
gains block → Juggernaut deals damage → kills a splitting slime → children spawn
mid-card...). A serialized queue eliminates this bug class by construction:
every mutation is a flat, sequential step; no live references or open loops span
a mutation.

---

## 2. Requirements

Derived from the actual remaining Ironclad pool (`data/ironclad_cards.csv`).
Four distinct categories — they need different mechanisms and must not be conflated:

### 2.1 Timed hooks (fire at a phase boundary)

| Hook | Cards / effects |
|---|---|
| Start of player turn | Demon Form (+Str), Brutality (lose HP, draw), Berserk (+energy) |
| End of player turn | Combust (lose HP, dmg all), player Metallicize (+block), Rage expiry |
| Start of enemy turn (per enemy) | enemy Ritual, enemy Metallicize *(exists today)* |

### 2.2 Reactive hooks (fire when something happens)

| Event | Listeners |
|---|---|
| Card exhausted | Feel No Pain (+block), Dark Embrace (draw) |
| Player gained block | Juggernaut (dmg random enemy) |
| Player lost HP (own turn) | Rupture (+Str) |
| Status/Curse drawn | Evolve (draw), Fire Breathing (dmg all) |
| Attack played | Rage (turn-scoped +block) |
| Skill played | Gremlin Nob Enrage *(exists today)* |
| Enemy damaged / HP threshold / death / wake / became-last | enemy `TriggeredEffect` *(exists today)* |

### 2.3 Modifiers / queries (alter a computation; do NOT react)

| Modifier | Cards |
|---|---|
| Effective card cost | Corruption (Skills cost 0), Blood for Blood (cost − HP-loss count) |
| Block reset rule | Barricade (block persists), Calipers-like future relics |
| Draw legality | Battle Trance (no more draws this turn) |
| Damage formula | Heavy Blade (Strength ×3/×5), Perfected Strike (+2 per "Strike"), Body Slam (= block) |
| Play legality | Clash (only if hand is all Attacks) |

**`TriggeredEffect` cannot express these** — they are consulted, not fired.
They need a query layer regardless of the scheduling architecture.

### 2.4 Meta-actions (play/manipulate cards from an effect)

| Effect | Cards |
|---|---|
| Play a card again / from a pile | Double Tap, Havoc |
| **Mid-resolution player choice** | Armaments (choose hand card), True Grit+ (choose to exhaust), Headbutt (choose from discard), Exhume (choose from exhaust), Dual Wield (choose attack/power), Warcry (choose to top-deck) |
| Add generated cards | Anger (copy → discard), Infernal Blade (random attack → hand), Power Through (Wounds → hand) |

The choice rows are decisive for the architecture: they require **suspending a
card's resolution to await agent input**. Nested function calls cannot suspend;
a paused queue can (§4.6).

---

## 3. Hard constraints

1. **Clone-safety (MCTS).** `CombatState::clone()` must stay a plain deep copy.
   Therefore: no `std::function`, no virtual dispatch, no listener registration
   in cloned state. *Behavior lives in static code keyed by enums; state holds
   only stacks/counters.* (Established pattern: `TriggeredEffect`, `enemy_name`.)
2. **Determinism.** All ordering — queue discipline, hook firing order, random
   choices — draws from the single seeded RNG and fixed orderings. Same seed,
   same fight, always.
3. **Throughput.** This is an RL environment (~145k steps/sec single-env
   baseline). The queue must not allocate per-action in steady state
   (fixed-capacity buffer) and will be re-benchmarked (§9).
4. **Parity.** Faithful StS behavior; where StS's internal ordering is
   unknowable from the wiki, we document the approximation (§7).
5. **Testability.** Migration is staged and test-guarded; timing-sensitive tests
   are re-verified, not silently rewritten (§8).

---

## 4. The architecture

### 4.1 Action

A small, clone-safe tagged value. No closures, no pointers into state — actors
and targets are slot indices / enums.

```cpp
enum class ActionKind {
  // Mutations
  DealDamage,      // actor(enemy slot | kPlayer), target, base amount, tags (per-hit)
  LoseHp,          // direct HP loss (bypasses block)
  GainBlock, GainEnergy, DrawCards,
  ApplyDebuff, ApplyPower, RemovePower,
  AddCardToPile,   // Slimed/Dazed/Wound/Anger-copy → pile
  ExhaustCard, MoveCard,   // pile manipulation
  // Structure
  PlayCard,        // resolve a card (Double Tap, Havoc re-entry)
  EnemyMove,       // one enemy's turn action
  ChooseCard,      // PAUSES the drain; see §4.6
  // Bookkeeping
  CheckDeath, EndOfTurnUpkeep, ...
};

struct Action {
  ActionKind kind;
  int actor = kNoSlot;      // kPlayer or enemy slot
  int target = kNoSlot;     // enemy slot / pile index / hand index
  int amount = 0;
  CardId card = CardId::Strike;   // for card-carrying kinds
  Debuff debuff = Debuff::None;
  Power power = Power::None;
  // small POD fields only — extend as kinds demand
};
```

### 4.2 Queue lifecycle & the drained invariant

```cpp
class ActionQueue {           // LOCAL to the resolution machinery
  // fixed-capacity ring buffer (no steady-state allocation)
  void push_back(Action);     // ≈ StS addToBottom — the default
  void push_front(Action);    // ≈ StS addToTop — "immediately next"
};
```

**Invariant: the queue is empty at every agent decision point.** `step(action)`
translates the agent's action into initial Action(s), then drains to completion
(or to a `ChooseCard` pause, §4.6). The queue is therefore *never stored in*
`CombatState` — `clone()` is untouched. The only queue-related state that can
persist across `step()` calls is the **pending-choice record** (§4.6), which is
a small POD on `CombatState`.

### 4.3 The drain loop

```cpp
void drain(CombatState& state, ActionQueue& q) {
  while (!q.empty()) {
    Action a = q.pop_front();
    execute(state, a, q);   // executors may push more actions
    if (state.outcome != Outcome::InProgress) return;   // terminal short-circuit
    if (paused_on_choice(state)) return;                // §4.6
  }
}
```

Executors are the **centralized mutators** — the single write-path for every
stat. `execute(DealDamage)` computes the modified damage (§4.5), applies it,
and fires hooks (§4.4), which respond by *pushing actions*, never by mutating
state directly. This is the linearization guarantee: hooks never nest.

### 4.4 Hooks (player + enemy, unified)

One hook vocabulary consulted from inside executors and at phase boundaries:

```cpp
enum class Hook {
  TurnStartPlayer, TurnEndPlayer, TurnStartEnemy,
  CardPlayed,       // payload: card (Rage, Enrage key off CardType)
  CardExhausted,    // Feel No Pain, Dark Embrace
  BlockGainedPlayer,// Juggernaut
  HpLostPlayer,     // Rupture
  CardDrawn,        // Evolve, Fire Breathing (Status/Curse filter)
  EnemyDamaged, EnemyHpThreshold, EnemyDeath, EnemyWake, BecameLastEnemy,
};
```

- **Player powers:** a *static registry* — `fire_player_hooks(state, hook, q)`
  switches over `character.powers` (stack map, already exists; zero new state)
  and pushes response actions. Compiler-checked switch, same anti-fragility as
  `enemy_name`.
- **Enemy effects:** the existing `TriggeredEffect` tables migrate onto the same
  vocabulary; `TriggeredAction` values become pushed `Action`s. The five ad-hoc
  fire sites collapse into hook consultations inside executors.

**Firing order (deterministic):** at any hook — player powers in a fixed
canonical order (Power enum order), then enemies in slot order, each enemy's
effects in vector order. Responses are `push_back` unless a specific StS
interaction is documented otherwise (§7).

### 4.5 Query/modifier layer

Modifiers are *pull*, not *push* — small pure functions consulted at
computation sites, switching over powers/turn-flags:

```cpp
int effective_cost(const CombatState&, CardId);       // Corruption, Blood for Blood
bool block_resets_at_turn_start(const CombatState&);  // Barricade
bool can_draw(const CombatState&);                    // Battle Trance
int modified_attack_damage(const CombatState&, CardId, int target);
    // Strength (xN for Heavy Blade), Perfected Strike count, Body Slam = block,
    // then Weak/Vulnerable — the existing compute_attack_damage absorbs this
bool is_playable(const CombatState&, CardId);         // Clash, unplayable, Entangle
```

`valid_actions` and executors call these; no direct field reads for derived
values.

### 4.6 Pause-on-choice (the RL interface for mid-card choices)

When the drain hits `ChooseCard`:

1. The drain **pauses**; a `PendingChoice` POD is recorded on `CombatState`
   (choice kind, source card, legal option set). This is the *only* persisted
   queue-adjacent state, and it is plain data → clone-safe. **Remaining queued
   actions are also persisted into a small POD vector on the state** (they are
   plain `Action`s) so the drain can resume exactly.
2. `valid_actions` masks to the choice's options; the obs gains a
   *choice-pending* indicator (obs/action-space design TBD in Stage 4 — see
   Open questions).
3. The agent's next `step(action)` supplies the choice; the executor consumes
   it, and the drain resumes from the persisted actions.

For the TUI, this is a natural "select a card" screen — the same interaction
the real game presents.

### 4.7 Turn-scoped state

Rage, Double Tap charges, Battle Trance's no-draw flag, Blood for Blood's
HP-loss count: small counters on `Character`, cleared by the `EndOfTurnUpkeep`
executor (or fight-scoped where StS says so). Plain ints — clone-safe.

---

## 5. Migration plan

Test-guarded stages, each independently green (the ROB-78 playbook). **No big
bang** — the queue arrives under card resolution first, then spreads.

| Stage | Content | Exit criteria |
|---|---|---|
| **1. Mutators** | Centralize every stat write behind `deal_damage / gain_block / lose_hp / gain_energy / draw_card / exhaust_card / add_card_to_pile`. Pure refactor, no queue, no behavior change. | Full suite green, ASan clean. Diff shows zero scattered writes remain. |
| **2. Queue under card resolution** | `handle_play_card` → action translation + drain. Executors wrap the Stage-1 mutators. Enemy phase still imperative. Hook vocabulary introduced; existing triggers repointed. | Suite green; timing-sensitive tests (spore-cloud, split, became-alone) re-verified deliberately (§8). |
| **3. Enemy phase on the queue** | `handle_end_turn` / `apply_move_to_state` emit actions; two-regime period ends. | Suite green; enemy-phase timing tests re-verified. |
| **4. Powers + choices + meta-cards** | Player-power registry (Tier C cards), query layer consumers (Corruption/Barricade/…), pause-on-choice, Double Tap/Havoc/choice cards (Tier D/E). | New card tests; choice-mode obs/mask spec implemented. |

Tier C cards ship at Stage 4 (registry) but the *foundation* work of Stages 1–3
is what they stand on. Stages 1–2 are the critical path.

---

## 6. What happens to existing systems

| System | Fate |
|---|---|
| Enemy `TriggeredEffect` | Kept as the per-enemy data table; its dispatcher becomes "push response actions"; its five fire sites become hook consultations. `TriggeredAction` maps ~1:1 onto `ActionKind`s. |
| `CardData` field-bag | Kept — fields become the *default action translation* (damage→`DealDamage`×hits, block→`GainBlock`, ...). `special`-tagged cards get bespoke translators. |
| `compute_attack_damage` | Absorbed into the modifier layer (§4.5). |
| Deferred-death bookkeeping (`died_slots`) | Replaced by `CheckDeath` actions — the deferral we hand-rolled becomes natural queue ordering. Timing re-verified in Stage 2. |
| `start_combat` / encounter sampling | Unchanged. |
| Obs/action space | Unchanged until Stage 4's choice mode. |

---

## 7. Ordering & parity policy

StS's `GameActionManager` discipline (which effects `addToTop` vs `addToBottom`)
is **not documented on the wiki** — it lives in decompiled source and community
interaction rulings. Policy:

1. Default: responses `push_back` (≈ `addToBottom`), matching StS's common case.
2. Where a specific interaction is community-documented, encode it and cite the
   source in a comment.
3. Divergences discovered later are **data fixes** (change one push site), not
   architecture changes. Keep a `docs/design/ordering-notes.md` ledger as they
   arise.
4. Wiki-first sourcing stands (Rob supplies rulings); decompiled source is a
   last resort for ordering questions, at Rob's discretion.

---

## 8. Testing strategy

- Stages 1–3 must not change observable behavior **except** where current
  behavior is a known approximation; those tests are re-verified against StS
  and changed *deliberately, with a note*, never silently.
- Timing-sensitive tests to re-verify at Stage 2/3: spore-cloud deferral, split
  spawn timing, became-alone, death-before-victory, block-reset phase timing.
- New invariant tests: queue drained at every decision point; no state mutation
  outside executors (grep-able convention: fields written only in
  `actions.cc`); choice-pause round-trip (pause → clone → resume on the clone).
- ASan in every stage gate (our bug history demands it).

## 9. Performance

- Fixed-capacity ring buffer (e.g. 64 actions, grow-never in steady state).
- Benchmark gate: re-run the steps/sec benchmark after Stage 2 and Stage 3;
  regression budget **≤10%** vs. current baseline (open question below if that
  proves tight).

---

## Rejected alternatives

Recorded for the decision log:

- **Extend the status quo** (player-parallel `TriggeredEffect`, ad-hoc fire
  sites): cannot express modifiers (§2.3) or choices (§2.4); fire sites keep
  multiplying inside the imperative flow. Rejected.
- **Synchronous event bus without a queue**: standardizes hook plumbing but
  keeps nested resolution (our worst bug class), and still cannot suspend for
  choices. Rejected.
- **Layered hybrid (mutators + hooks + queries, recursion for meta-cards)**:
  shares Stages 1 and the hook/query layers with this design, ships Tier C
  faster, but (a) mid-card choices require flattening into composite actions —
  a permanent action-space tax on the RL interface, worse for pile-choices;
  (b) depth-first nested chaining re-creates the mutation-during-resolution bug
  conditions and calcifies non-StS timing into tests; (c) if the queue arrives
  later anyway (Tier E pressure), the chaining semantics get paid for twice.
  Rejected in favor of doing the queue once, staged.

## Open questions (carried, not blocking Stages 1–2)

1. **Choice-mode obs/action encoding (Stage 4):** how the agent sees a pending
   choice — dedicated obs flag + reuse of the card-type action dimension for
   "choose card type", or an extended action head. Needs its own mini-design
   before Stage 4.
2. **Timing-parity ambition:** default-`push_back` plus a divergence ledger
   (§7), or actively source StS queue discipline per effect from decompiled
   code. Currently: ledger approach.
3. **Perf budget:** ~~is ≤10% steps/sec regression acceptable for the queue?~~
   **Resolved:** measured −2.6% at the Stage 2 gate and −3.4% cumulative at
   Stage 3 (single-env random agent) — the queue is cheap.
4. **`EnemyMove` granularity (Stage 3):** ~~one action per enemy move vs. per
   effect-within-move.~~ **Resolved (Rob):** per-effect actions on the same
   queue as cards — StS resolves a move's effects sequentially through the one
   manager (visible in-game with multi-debuff and attack+block moves). One
   enemy turn = one translate + drain; see ordering-notes.md §Stage 3.
