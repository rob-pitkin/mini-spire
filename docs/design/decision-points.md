# Decision points: the RL interface for non-combat choices

**Status:** **Accepted (2026-07)** — the Option Slot Channel, §5. **Scope:** how
this environment represents decision points that are not "play a card from
hand" — mid-card choices (Stage 4c) *and* the roadmap (card rewards, map paths,
shops, rest sites, potions, events).

This doc exists because 4c is not really about five cards. The action/obs
encoding is the RL interface: changing it invalidates trained policies and any
published benchmark number, so the choice should be made once, with the
roadmap in view.

**Decision drivers (Rob):**

- The v2.0.0 roadmap (map / shop / events) is **firm**; M2 publication is not
  imminent. Therefore: pay for generality once, now.
- **No temporary solutions that get undone at a later milestone.** This ruled
  out the staged "engine pauses, interface defers" option, whose entire value
  was buying time.
- **Gymnasium-shaped env**: one fixed observation shape and one fixed
  `Discrete` action space, in combat and out of it. This is not just a
  preference — `observation_space` is declared once at construction, SB3
  validates every obs against it, and a varying shape breaks `VecEnv`
  stacking. It independently rules out phase-split designs.
- Intended solvers: **PPO for combat, an LLM for non-combat decisions.** This
  materially changes the cost of the chosen design (see §6).

---

## 1. Decision points to support

| Decision point | When | Shape | Candidates |
|---|---|---|---|
| Play a card | combat (exists) | card type × enemy target | ≤10 in hand |
| End turn | combat (exists) | nullary | 1 |
| **Choose card from a pile** | mid-card (4c) | pick 1 of N card types | hand / discard / exhaust |
| Card reward | post-combat | pick 1 of 3, or skip | 3 + skip |
| Room/path selection | map | pick 1 of ≤4 branches | ≤4 |
| Shop purchase | shop | pick 1 of ~7, or leave | ~7 + leave |
| Rest site | map | rest vs upgrade (→ then pick a card) | 2, then N |
| Potion use | combat | potion × target | ≤3 × targets |
| Event choice | event room | pick 1 of 2–4 options | ≤4 |

Two structural facts: most rows are **"pick 1 of N from a labeled set"**, and
the rest site is **nested** (choose "upgrade", *then* choose a card), so the
mechanism must compose rather than fire once.

The five 4c cards: **Armaments** (hand), **Warcry** (hand), **Dual Wield**
(hand, Attack/Power only), **Headbutt** (discard), **Exhume** (exhaust).

## 2. Hard requirements

- **R1 Clone-safety.** Pending-decision state must be POD; `clone()` stays a
  plain deep copy. Follow `ActionQueue`'s fixed-capacity precedent — no
  `std::vector` in the pause state.
- **R2 Determinism.** Same seed ⇒ same fight, including through pauses. Any
  canonical option ordering becomes load-bearing and must be documented.
- **R3 Legality via mask.** One source of truth.
- **R4 The agent can always tell what it is being asked.** NLE measured this
  failure directly: agents "cannot determine whether a menu is open" and may
  "inadvertently take some actions in an unseen menu." A pending-decision
  indicator in the obs is mandatory.
- **R5 No silent index reuse.** toypiper abandoned his first StS design because
  "each point in action space had several semantic meanings depending on the
  request type." If an index is reused, the disambiguator must be *in the
  observation*.
- **R6 Throughput.** ≤10% regression vs. 838k engine steps/sec.
- **R7 Extensible without re-encoding.** Append-only, per the existing CardId
  rule.
- **R8 MaskablePPO-compatible.** Flat `Discrete` + boolean mask. A conditional
  autoregressive head is a research project, not an env feature.
- **R9 Human-playable.** The TUI must render and drive every decision point.

**Non-goals:** continuous/parameterized actions; distinguishing two *identical*
cards in a pile (interchangeable today — but see §6).

## 3. Verified facts that constrain the design

Checked in the code, not assumed — several overturned earlier assumptions:

| Fact | Location | Consequence |
|---|---|---|
| `valid_actions` walks the **hand**, not the action space | `turn_loop.cc` | **Action-space width is nearly free.** Stage 4a's 39% regression was `CARD_DATABASE` hashing, not width. Designs must NOT be scored on action count. |
| `struct Card { CardId card_id; };` — no instance state | `card.h` | Cards of a type are interchangeable; a card choice *is* a card-type choice. |
| Piles are order-free per-type **counts** in the obs | `combat_env.cc` | The obs cannot express "the 3rd card in discard." Positional choice encoding needs new obs structure; card-type encoding reuses what exists. |
| **No `CardId → CardId` upgrade table exists in the engine** | `src/` (the CSV `upgrade_of` column is unused by C++) | Armaments needs this regardless of encoding. Separable prerequisite. |
| Only Slimed and Dazed lack upgraded forms (both Status) | derived | Armaments is fully well-defined; not a blocker. |
| `CombatEnv::clone()` is `return *this;` | `combat_env.h` | Pause state must be plain-copyable. |
| M1 checkpoints already invalidated by 4a/4b obs growth | — | "Invalidates trained policies" is a **sunk** cost right now. The real freeze deadline is M2 publication. |

## 4. Prior art

| System | Approach | Lesson |
|---|---|---|
| [RLCard](https://rlcard.org/games.html) | Flat index space everywhere. Gin Rummy: 110 indices spanning score/draw/discard/knock — heterogeneous decision types in **disjoint index blocks**. Uno/Mahjong: the index **is** the card/tile. | Flat + mask is the convention, not a compromise. Content-addressed indices are the norm for card games. |
| [sts2-rl-agent](https://github.com/zhiyue/sts2-rl-agent) | Closest analogue: `Discrete(61)` combat, `Discrete(100)` full run (map, rewards, shop, rest, potions). ~28k steps/sec, ~92% Act 1 win rate, MaskablePPO. | A *unified* flat run-space demonstrably works for exactly our roadmap. |
| [NLE / ICLR 2026 blogpost](https://iclr-blogposts.github.io/2026/blog/2026/revisiting-the-nle/) | Positional `menu_option_1..n` + menu contents in obs. | Source of R4. But measured payoff was only "modest," and they faced *arbitrary text menus*; we have a fixed 102-id vocabulary. |
| [DouZero](https://arxiv.org/abs/2106.06135) | Rejected both a 27k-wide head and RLCard's lossy abstraction; scores `Q(state, action)` with the action as an *input feature*. | Right answer at huge scale, but a custom value head — fails R8. |
| [gym-locm](https://github.com/ronaldosvieira/gym-locm) | Draft and battle as **separate envs**. | Phase-splitting is proven — but cannot express one end-to-end run policy. |
| Miles Oram (StS DQN) | 296 card-type-indexed actions; **simulates** sub-choices and picks the best. | Needs a forward model + value function: viable for MCTS/DQN, not a PPO policy. |
| OpenAI Five | Hard-coded item buying, still superhuman. | Removing a decision from the policy can be legitimate — if disclosed. |
| [Gym-µRTS](https://arxiv.org/abs/2105.13807) / [PufferLib](https://arxiv.org/abs/2406.12905) | µRTS **deprecated** its better-scoring decomposed encoding for implementation cost; PufferLib's contribution is *flattening* structured actions. | Even the structured-action camp ships a flattening shim. |

## 5. The accepted design: the Option Slot Channel

> **As built.** The worked numbers throughout this document are the 102-card
> pool it was written against. They are kept as written — they are the reasoning
> that produced the decision, and rewriting them would destroy the record while
> guaranteeing a fresh drift.
>
> **Do not restate the current numbers here.** Read them from the code:
> `kTotalActions`, `kEndTurnAction`, `kFirstOptionSlot`, `kDeclineAction`
> (`src/turn_loop.h`) and `CombatEnv::kObsSize` (`src/combat_env.h`), all
> exposed to Python. A previous version of this note pinned an "as built"
> column at 112 cards; it was stale within weeks.
>
> What is worth recording is that the pool has now moved **four times** — 102 at
> design time, 112 with Tier E's choice cards, 154 with the full Ironclad pool,
> 189 once per-instance state became card identity (ROB-87) — and **not one of
> those required an interface change.** Every constant is derived from
> `kNumCardTypes`; nothing was re-encoded, and no truncation rule was ever
> needed. That is the property the design was chosen for, and it has now been
> tested by a 1.85× growth in the card pool rather than argued for on paper.

One generic mechanism for **every** decision point that is not "play a card" or
"end turn". Derived from NLE's `menu_option_1..n` + contents-in-observation,
sized so that truncation is impossible.

### 5.1 Action space

Symbolically — the indices below are derived, never written down. (The worked
numbers in brackets are the 102-card pool this was designed against; see the
note in §5.)

```
[0 .. kEndTurnAction-1]   play card: card_idx * kMaxEnemies + enemy_idx  [0..509]
[kEndTurnAction]          end turn                                       [510]
[kFirstOptionSlot ..      option_slot_0 .. option_slot_{kNumOptionSlots-1}
 kDeclineAction-1]                                                       [511..612]
[kDeclineAction]          option_skip / decline                          [613]
                                              → kTotalActions            [614]
```

**Never re-derive these.** Computing end turn as `kTotalActions - 1` was a real
bug: it is the *decline* action, because the option channel follows the combat
block. It broke the TUI and 13 Python tests at once.

The combat path (0–510) is **byte-identical to today**. PPO trains on exactly
the indices it trains on now; the slot channel is fully masked off during
normal combat, and the combat indices are fully masked off during a pause.

**Slot count = `kNumCardTypes`, and that is a load-bearing choice.**
A pile cannot contain more *distinct card types* than there are card types, so
**overflow is structurally impossible** — no runtime guard, no truncation rule,
no parity risk. This matters because v1.0.0 ships a configurable deck: a user
may legitimately build a deck holding every distinct card, and Exhume choosing
from the exhaust pile would then need a slot for each. A 20- or 32-slot block
would have required a documented truncation rule, which *is* a parity violation
(the human can pick any card; the agent could not). Action width is nearly free
(§3), so buying provable safety costs almost nothing.

Stated as a count this would already be stale twice over — hence the invariant
rather than the number.

`kNumOptionSlots` is defined as `kNumCardTypes` so the two cannot drift.

### 5.2 Observation additions

Appended (existing feature indices never move):

```
header (kChoiceHeaderSize = 5 floats)
  [0] choice_pending          0 / 1          ← R4, mandatory
  [1] choice_kind             which decision  (ChoiceKind enum)
  [2] source_pile             hand/draw/discard/exhaust/external
  [3] source_card             the card that caused the pause (CardId)
  [4] choice_is_optional      0 / 1          ← is option_skip legal

slots (kNumOptionSlots × kChoiceSlotStride = 3 floats)
  [0] occupied                0 / 1
  [1] payload_id              CardId now; node/item/event id in v2
  [2] payload_value           instance damage now; gold cost in v2

obs: 523 → 523 + 5 + 306 = 834   [102-card pool; read kObsSize for current]
```

> **As-built divergence — values are written RAW, not normalized.** This block
> originally specified `source_card / 102`, `payload_id / 255`, `cost / 200`.
> The implementation (`combat_env.cc`) writes the unscaled integer in every
> case. The spec is corrected above to match what actually ships.
>
> Whether that is *right* is an open question, deliberately left open rather
> than silently normalized here. Feeding a CardId as a scalar gives the network
> a false ordinal relationship between unrelated cards — id 5 is not "between"
> 4 and 6 — which is the exact modelling error `observation-space.md` §5.2
> rejects sts2-rl-agent for. Nothing else in our obs does this: piles are count
> vectors, statuses are per-effect slots. The choice channel is the one place
> a categorical is encoded as a magnitude.
>
> Not changed as part of ROB-89, which is a documentation pass. It wants its
> own decision (one-hot per slot is `kNumCardTypes` floats × slots — far too
> wide; an embedding is a policy-side change, which §1 of the observation doc
> says is not the environment's problem). Filed rather than fixed.

**Payload fields are reserved now and always written** (zero for card choices),
so v2.0.0's map / shop / event work is **pure data** — new `ChoiceKind` values
and new payload semantics, with **no obs shape change**. That is the direct
answer to "no rework at a later milestone": the shape that ships in v1.0.0 is
the shape v2.0.0 uses.

### 5.3 Canonical ordering (R2)

Slot order is a deterministic function of state and is **part of the public
interface**, not an implementation detail:

- **Card choices:** ascending `CardId`, deduplicated by type. Two identical
  cards collapse to one slot (they are interchangeable — see §7 for when that
  stops being true).
- **Non-card choices (v2):** ascending natural index (map branch order, shop
  slot order, event option order as authored).

Because ordering is ascending `CardId`, slot *k* is stable across states that
share an option set — which recovers much of the index-stability that a pure
positional design gives up (§6).

### 5.4 Engine mechanism

```cpp
enum class ChoiceKind {
  None,
  UpgradeCardInHand,     // Armaments
  HandToTopOfDraw,       // Warcry
  DiscardToTopOfDraw,    // Headbutt
  ExhaustToHand,         // Exhume
  CopyAttackOrPowerInHand,  // Dual Wield
  // v2.0.0: CardReward, MapPath, ShopItem, RestOption, EventOption, Potion
};

struct PendingChoice {                       // POD — clone() stays a plain copy
  ChoiceKind kind = ChoiceKind::None;
  CardId source_card = CardId::Strike;
  bool is_optional = false;
  int num_options = 0;
  std::array<CardId, kNumOptionSlots> options{};   // fixed, no heap (R1/R6)
};
```

The pause state lives on `CombatState` alongside a **fixed-capacity** array of
resume `Action`s, following `ActionQueue`'s existing ring-buffer precedent — no
`std::vector`, so `clone()` remains `return *this;`.

**The engine builds the candidate list.** A `ChoiceKind`-driven builder applies
the per-card filter (Dual Wield → Attack/Power only; Armaments → upgradable
only) and the canonical ordering, and the mask is then simply "slot is
occupied". Legality logic stays in one place, consistent with the 4b query
layer whose whole purpose was preventing mask/resolution divergence.

`resolve_choice(state, option_index)` is public: the RL path, the TUI, and
tests are all callers. R9 is satisfied by construction.

**Masking must walk the option list (≤102), never the 614-wide action space** —
walking the action space is what cost 39% at Stage 4a.

### 5.5 Prerequisite: the upgrade table

Armaments needs a `CardId → CardId` upgrade map, which **does not exist in the
engine** (the CSV's `upgrade_of` column is unused by C++). Derivable
mechanically; only Slimed and Dazed lack upgraded forms, and both are Status
cards that correctly cannot be upgraded. Separable from the encoding work and
should land first.

## 6. Why this design's known weakness is acceptable here

The standard objection to positional slots is **credit assignment**: slot *k*
means something different across episodes, so nothing is shared with the
"play card *k*" representation, and an MLP must learn to compare unaligned
blocks. That is a real cost — and it is largely defused by two facts specific
to this project:

1. **PPO never sees these decisions.** The intended solver is PPO for combat +
   an **LLM for non-combat decisions**. The LLM reads a *rendered menu*, which
   is exactly what a positional slot list is ("1. Strike  2. Defend  3. Bash").
   The design whose weakness is "hard for an MLP" and whose strength is
   "self-describing menu" is well matched to an LLM consumer. The combat path
   PPO does train on is untouched.
2. **Ascending-CardId ordering restores much of the stability anyway** (§5.3),
   and every slot publishes its `payload_id`, so a policy that *does* want to
   learn these decisions can read identity directly rather than infer it from
   position.

## 7. Known long-horizon risk

`card.h` anticipates **per-instance card state** (Searing Blow's cumulative
upgrades, Ritual Dagger's accumulated damage). Today `struct Card { CardId; }`
makes cards of a type interchangeable, which is what licenses dedup-by-type in
§5.3.

When per-instance state lands, two "identical" cards stop being
interchangeable. **This design survives that better than a card-type-indexed
one**: slots are positional, so the fix is to stop deduplicating and let each
*instance* occupy its own slot — the 102-slot block is already wide enough for
any single pile, and the obs payload gains an instance-state float. No action
space change, no re-encoding. This was the deciding long-horizon argument.

## 8. Requirements check

| | Verdict |
|---|---|
| R1 clone-safety | ✅ `PendingChoice` is POD with a fixed `std::array`; resume actions in a fixed-capacity buffer. |
| R2 determinism | ✅ Canonical ordering documented as interface (§5.3). |
| R3 mask legality | ✅ Mask = occupied slots; engine builds the list, single source of truth. |
| R4 agent knows | ✅ Explicit `choice_pending` + `choice_kind` + published contents — the NLE prescription. |
| R5 no silent reuse | ✅ Slot indices are disjoint from combat indices and their contents are published every step. No index ever carries a hidden second meaning. |
| R6 throughput | ✅ +103 mask entries and +311 obs floats, all fixed-size and allocation-free; masking walks the option list. Re-benchmark at the gate. |
| R7 extensible | ✅ Best in class — v2 decisions are new `ChoiceKind` values and payload semantics, with **no shape change**. |
| R8 MaskablePPO | ✅ Flat `Discrete(614)` + boolean mask. |
| R9 human-playable | ✅ TUI calls `resolve_choice()` directly and renders the slot list as a menu. |

Gymnasium shape contract: **one fixed obs (834) and one fixed `Discrete(614)`
in every phase**, combat or not.

## 9. Implementation order

1. `CardId → CardId` upgrade table (§5.5) — independent, unblocks Armaments.
2. `ChoiceKind`, `PendingChoice`, resume-action buffer, `resolve_choice()`,
   plus the pause → clone → resume round-trip test.
3. Obs/mask encoding: the 5-float header + 102×3 slot block; `valid_actions`
   choice-mode branch.
4. The five 4c cards, each with a candidate-builder filter.
5. TUI choice screen.
6. Gate: full suite + ASan + differential mask verify + benchmark.

v2.0.0 then adds `ChoiceKind` values only — no interface change.

