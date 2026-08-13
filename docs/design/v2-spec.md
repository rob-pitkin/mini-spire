# v2.0.0 — full Act 1 run: implementable spec

**Status: DRAFT.** Scope is settled (see `v2-obs-notes.md` § SCOPE DECIDED);
sizings are computed but not yet ratified. Open questions are in §10 and are
called out inline as ⚠️.

**Audience: an implementer with no prior context.** Everything needed to build
this should be here or linked. Where a number is a planning estimate rather than
a counted fact, it says so.

---

## 1. What v2.0.0 is

A **complete Act 1 run** of Slay the Spire as an Ironclad, as a Gymnasium
environment. One episode = one run: 15 map floors plus a boss, with everything
between fights that a human does — choosing a path, taking card rewards,
shopping, resting, resolving events.

v1.0.0 was one combat. This is the game around it.

**Why:** with a fixed deck against one fight there is no deck construction, and
deck construction is most of what makes Slay the Spire a hard decision problem.
See `roadmap.md` § "The reordering, and why".

### Explicitly not in v2.0.0

Acts 2–4 · boss relics (structurally absent — see §9) · Frozen Eye · shrines as
player decisions · characters other than the Ironclad.

## 2. Episode definition

| | |
|---|---|
| `reset()` | starts a new run at **floor 0 (Neow)**, not in combat |
| `step()` | one decision, in whatever phase is current |
| terminates | player HP reaches 0 (**loss**), or the Act 1 boss dies (**win**) |
| truncates | never in v2.0.0 |
| typical length | ~9 fights × ~18 steps + ~30 non-combat decisions ≈ **200 steps** |

A run does **not** begin in combat. `reset()` returns an observation in the
`neow` phase. This differs from v1.0.0 and is the first thing an implementer
will trip on.

## 3. Architecture

```
RunState                     ← NEW. owns the run.
  ├── floor, act, gold, ascension
  ├── master deck  (vector<Card>)
  ├── relics       (vector<RelicId> — order matters, see §5.4)
  ├── potions      (vector<PotionId>)
  ├── map          (105 nodes + edges + visited)
  ├── card_removal_price
  └── CombatState  ← EXISTING, unchanged. owned, not inherited.
```

`CombatState` is **not modified**. A fight constructs one from `RunState`
(deck, HP, relics) and writes results back on completion. This keeps v1.0.0's
combat engine, its 447 tests, and its `clone()` semantics intact.

`RunState::clone()` must remain a plain copy for MCTS, which means the same
discipline as `CombatState`: POD members, fixed arrays, no heap-owned graphs.

## 4. Phases

A **phase is a mode that persists across steps**, with a varying legal-action
set — exactly how combat already works. It is not one decision.

| # | phase | entered | exits when |
|---|---|---|---|
| 0 | `neow` | run start | blessing chosen |
| 1 | `map` | after any room resolves | a path node is chosen |
| 2 | `combat` | entering a monster/elite/boss node | all enemies dead, or player dies |
| 3 | `reward` | combat ends in a win | all reward choices taken or skipped |
| 4 | `shop` | entering a merchant node | player leaves |
| 5 | `rest` | entering a rest node | an option is taken |
| 6 | `event` | entering an event node | event resolves |
| 7 | `treasure` | entering a treasure node | chest opened |

⚠️ `treasure` may need no decision at all (chests grant a relic automatically).
Kept as a phase for observability; may collapse to a pass-through.

## 5. Observation

Flat `Box(float32)`, one vector. **Unified** — combat and non-combat share one
observation, so a researcher may split it but the environment does not prescribe
one (`observation-space.md` §1; the environment ships the superset).

### 5.1 Layout

Planning vocabulary: `CARDS = 250` ⚠️ (189 today + ~48 colorless + ~13 curses —
**must be counted exactly before implementation**), `RELICS = 170` ⚠️,
`POTIONS = 42` ⚠️.

| # | block | size | contents |
|---|---|---:|---|
| 1 | player | 34 | unchanged from v1.0.0 |
| 2 | enemies | 220 | unchanged — 5 slots × 44 |
| 3 | combat piles | 5 × CARDS | unchanged shape, wider vocabulary |
| 4 | turn | 1 | |
| 5 | phase | 8 | one-hot over §4 |
| 6 | run scalars | 8 | floor, gold, ascension, card-removal price, potions-held, potion-slots, act, removal-used-this-shop |
| 7 | master deck | CARDS | count per card type |
| 8 | relics held | RELICS | multi-hot |
| 9 | relic counters | RELICS | the number drawn on the relic icon; 0 where none |
| 10 | bottled cards | CARDS | see §5.3 |
| 11 | potions | POTIONS | **count vector**, not slots — see §5.2 |
| 12 | map node types | 840 | 105 × 8 (7 room types + "no room") |
| 13 | map out-edges | 315 | 105 × 3 (edges reach columns c−1, c, c+1 only) |
| 14 | map visited | 105 | path walked so far |
| 15 | map column | 7 | current column; the floor is already in §6 |
| 16 | boss identity | 10 | one-hot; visible from floor 1 |
| 17 | current event | 60 ⚠️ | one-hot; zero outside `event` |
| 18 | offer prices | CARDS+RELICS+POTIONS | see §5.5 |

**Total ≈ 4,200 floats** ⚠️ (2.4× v1.0.0's 1,772), dominated by the pile planes
and the map.

### 5.2 Potions are a count vector, not slots

`POTIONS` wide, value = how many of that potion are held. **Not** `slots ×
types`. Potion Belt (5 slots) and Ascension 11 (2 slots) then work with no shape
change, and potions match the pile-plane pattern instead of being the one
slot-indexed thing in the observation.

### 5.3 Bottled cards fit in one plane

Bottled Flame / Lightning / Tornado each bottle a specific card, which a human
sees. Naively that is 3 × CARDS. It is not needed: **each bottle constrains a
different card type** (Flame→Attack, Lightning→Skill, Tornado→Power), so "this
card is bottled" (block 10) plus "which bottles I hold" (block 8) determines
which bottle holds it. Lossless at one third the cost.

### 5.4 Relic order

Relic trigger order is acquisition order. Block 8 is unordered, so two states
with the same relics acquired in different orders alias.

⚠️ **Known limitation, accepted for v2.0.0.** Encoding order costs
`RELICS × RELICS` or an ordered index list. Revisit only if a real Act 1 relic
pair is found whose order changes an outcome.

### 5.5 Offer prices — and the aliasing trap

One float per entity: **`cost + 1` if currently offered, `0` if not.**

The `+1` is load-bearing. Card rewards, Neow blessings and event rewards are
**free**, so a plain cost would encode 0 — identical to "not offered" — and a
three-card reward screen would be bit-identical to an empty one. That is a
§1.1 aliasing defect. An off-by-one on a magnitude costs nothing; invisible
offers are fatal.

**Do not rely on the action mask to convey what is offered.** Verified in
`sb3-contrib`: `MaskableActorCriticPolicy.forward` computes
`values = self.value_net(latent_vf)` from the observation, then applies masking
to the policy distribution only. **The value head never sees the mask.**

⚠️ This block is ~460 floats, nearly all zero outside a shop. It is the least
satisfying part of the design. Retained because the alternatives all reintroduce
rank-indexing.

### 5.6 Parity rules that are easy to get wrong

1. **Unknown map rooms must stay Unknown.** Room type is rolled *on entry*, not
   at generation. A human sees `?`. Encoding the resolved type gives the agent
   strictly more than a human — a §1 violation *in the opposite direction*,
   which is the failure mode nobody watches for. **Test this.**
2. **Draw-pile order stays hidden.** Unchanged from v1.0.0; the draw pile is a
   count map precisely so order cannot leak.
3. **The boss is visible from floor 1** (block 16). A human plans the act around
   it.

## 6. Action space

Flat `Discrete`, masked. **Stable indexing**: index *k* means the same thing in
every state, forever — never "the *k*th option offered".

| block | size | index means |
|---|---:|---|
| combat: card × target | CARDS × 5 | play card *c* at enemy slot *t* |
| end turn | 1 | |
| map: choose node | 105 | move to grid position *p* |
| card selection | CARDS | pick card *c* (reward / shop / removal — see §6.1) |
| relic selection | RELICS | pick relic *r* |
| potion: use × target | POTIONS × 5 | Fire/Fear/Weak/Poison potions target an enemy |
| potion: discard | POTIONS | |
| event option | 60 ⚠️ | `(event, option)` pair |
| rest option | 6 | rest, smith, recall, lift, toke, dig |
| take Max HP instead | 1 | Singing Bowl |
| decline / skip / leave | 1 | |

**Total ≈ 1,900** ⚠️.

### 6.1 The purpose collision — UNRESOLVED

At a shop, "buy card *X*" and "remove card *X*" are **both legal at once** and
would share index `card_selection + X`. Stable entity indexing is insufficient
when one entity is selectable for two *purposes*, and the phase one-hot is too
coarse to disambiguate.

⚠️ **This is the largest open design question.** Options:

| option | cost |
|---|---|
| Sub-phase indicator, and shops step through purposes | cheap in actions; makes the shop a small state machine |
| Separate action blocks per purpose (`buy_card` / `remove_card`) | +CARDS actions; keeps flat semantics |
| A `purpose` dimension → `(purpose, entity)` | cleanest; stops the space being flat |

Not resolved here. **Must be settled before implementation.**

## 7. Reward

Fully specified in [`run-reward.md`](run-reward.md). Summary:

```
Φ(s) = α · floors_cleared(s) + β · (hp / max_hp),   Φ(terminal) = 0
reward = terminal(win/loss) + γ·Φ(s′) − Φ(s)
```

Potential-based, so the optimal policy is provably unchanged for any α, β
(Ng, Harada & Russell 1999). `α = β = 0` degenerates to pure sparse reward as a
special case. **Two requirements are load-bearing:** `Φ` must be 0 at terminal
states, and the shaping `γ` must equal the learner's.

## 8. Decision points

| phase | options | notes |
|---|---|---|
| Neow | 2 or 4 blessings | 4 unless the previous run was poor; enumerable ids |
| map | 2–4 next nodes | Wing Boots may allow ignoring edges — obs shows edges, mask shows legality |
| card reward | 3 cards (**4** with Question Card), or skip | Singing Bowl adds "+2 Max HP instead" |
| shop | 14 slots: 5 coloured cards, 2 colorless, 3 potions, 3 relics, 1 removal | multiple purchases; Courier restocks; prices are computed like `effective_cost` |
| rest | up to 6 | rest, smith, + Recall/Lift/Toke/Dig from relics |
| event | 2–4 | `(event, option)` ids |
| combat | v1.0.0's action space | unchanged |

## 9. Shape-breakers: auto-resolve

Where a decision cannot be expressed, **the engine resolves it by a fixed
deterministic policy and the agent never sees it.** The content stays in the
game — the event still occupies a floor and consumes a room — where removing it
would change the run's distribution.

| case | policy |
|---|---|
| **Match and Keep!** (shrine: 12 face-down cards, 6 pairs, duplicate entities) | engine picks uniformly at random |
| **Gambling Chip** (discard *any number* — a subset choice) | engine discards nothing |
| **Frozen Eye** (shows draw-pile *order*; informational, so auto-resolve does not apply) | **excluded from the shop pool** — the only true exclusion |

**Honest cost:** a human makes these choices and the agent does not. A bounded,
documented parity divergence, strictly smaller than removing the content.

**Boss relics are absent structurally, not excluded** — they are awarded after an
act boss, and the run ends there.

### Reserve multi-select even though nothing uses it

No v2.0.0 decision is a subset choice, because Gambling Chip is auto-resolved.
**Design the shape anyway.** It is the one mechanism that would be painful to
retrofit, and shipping a design that cannot express it is the exact
"built against v2 rather than with it" mistake `decision-points.md` records
making once already.

Shape: repeated selection + a `confirm` distinct from `decline`, and **the
partial selection in the observation** — without it every prefix of the same
selection aliases.

## 10. Open questions — settle before implementing

1. **§6.1 purpose collision.** The blocker.
2. **Exact vocabularies.** `CARDS`, `RELICS`, `POTIONS`, and the event/option
   count are all planning estimates. One reviewer believes "~60 event options"
   is low by 2–3×. **Count them.**
3. **Does `treasure` need a phase**, or is it a pass-through?
4. **Relic order** (§5.4) — accepted as aliasing. Confirm no Act 1 pair matters.
5. **Throughput.** The observation roughly 2.4×'s. The map (~1,270 floats) is
   constant within a fight, so `compute_obs` must update incrementally rather
   than rewriting the buffer each step. Engineering, not design, but v1.0.0
   published 438k/259k steps-per-second and a regression is visible.

## 11. Suggested implementation order

Each step ends somewhere testable.

1. `RunState` + episode boundary; `reset()` starts at Neow. No map — a linear
   floor counter.
2. Sequential fights with HP and deck carrying across them.
3. Card rewards (simplest decision point; proves the loop).
4. **Walking skeleton complete** — 3 fights, card reward between each, terminates
   after N floors. No map, shop, events, or boss.
5. The map (generation, path choice, the Unknown-stays-Unknown test).
6. Rest sites (first resource-vs-investment tradeoff; the case the reward design
   was built around).
7. Events, then shops (most machinery).
8. Bosses → **M5: an agent that completes a run**.
