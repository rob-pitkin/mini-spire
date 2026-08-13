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
See [`roadmap.md`](../../roadmap.md) § "The reordering, and why".

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

### 3.1 `CombatState` IS modified — an earlier draft claimed otherwise

That claim was wrong and is withdrawn. It fails four independent ways, and
pretending otherwise would mislead an implementer on the largest piece of work
in the project:

| # | what forces a change |
|---|---|
| 1 | **The card vocabulary widens 189 → ~250.** `kNumOptionSlots = kNumCardTypes` (`card.h`), and `kEndTurnAction` / `kFirstOptionSlot` / `kDeclineAction` / `kObsSize` all derive from it (`turn_loop.h`). So "combat's action space is unchanged" is **false**. |
| 2 | **Relics hook into combat** (Vajra, Anchor, Kunai, Burning Blood) and need a relic list *and* per-combat counters inside `CombatState`. There is **zero relic code in `src/` today**. |
| 3 | **Potions are used mid-combat**, with effects that must enter the action queue and an action index outside `decode_action`'s current range. |
| 4 | **Curses and colorless cards have in-combat behaviour**, and `CardId` has no curse enumerators. |

**This is accepted, not a problem to argue away.** It is the cost of the v2
scope. Some of v1.0.0's 447 tests will break, and that is expected.

What limits the damage is **ordering, not isolation**: §11's walking skeleton
touches almost none of combat, and relic/potion hooks arrive later and
separately. The scope is safe because the implementation defers touching combat
until the run layer works — not because combat is untouched.

⚠️ **Still needed before implementation: an explicit in-scope list of
`CombatState` / `turn_loop` changes**, and a precise statement of what (if
anything) "unchanged" still claims — struct layout? `clone()` semantics? The
text previously implied both and guaranteed neither.

### 3.2 The handoff contract

`RunState` is the owner of record. A fight is a **projection** of run state into
a `CombatState`, and a **write-back** of the results. Specified field by field so
two implementers cannot disagree.

#### Entering a fight

| run state | becomes |
|---|---|
| `master_deck` | shuffled into `draw_pile` (stream `combat[floor]`) |
| `hp`, `max_hp` | `Character::hp`, `Character::max_hp` |
| `relics` | combat-relevant relics applied as start-of-combat effects |
| everything else | not visible to combat |

#### On fight end

| `CombatState` field | disposition | why |
|---|---|---|
| `hp` | **written back** | HP carries across the run — the core of a run |
| `max_hp` | **written back** | Feed and Neow can raise it mid-fight |
| `block` | **reset** | block does not survive combat |
| debuffs, powers | **reset** | combat-scoped by definition |
| `free_this_turn`, `hp_loss_events`, `combust_casts` | **reset** | per-combat counters |
| relic counters | **per relic** — see §3.5 | some are per-combat (Nunchaku), some per-run (Ink Bottle) |
| draw / discard / hand / exhaust piles | **discarded** | the master deck is the truth |
| status cards generated in combat (Wound, Dazed, Slimed, Burn) | **discarded** | they exist only for that fight |
| **permanent card changes** | **written back by uid** — see below | Feed, Ritual Dagger, a mid-fight Armaments |

#### Card instance identity — DECIDED: `Card` gains a uid (Rob, 2026-08-13)

`Card` currently carries `card_id`, `bonus_damage`, `upgrades` and **no unique
id**. v2 cannot work without one, because these are indistinguishable on
write-back:

| change | persists? |
|---|---|
| Armaments upgrading a card **mid-combat** | **no** — combat-scoped |
| Smithing the same card at a campfire | **yes** |
| Ritual Dagger / Feed growing permanently | **yes** |

Same `card_id`, same `upgrades`, opposite dispositions. Without a uid the engine
cannot tell which copy in the master deck to update, or whether to update at all.

**Decided:** `Card` gains a `uid`, assigned when a card enters the master deck
and preserved through the projection into combat. Write-back matches on `uid`,
and only changes flagged run-scoped are applied.

Implementation notes:

- **Assign from a `RunState` counter, not a hash or a global.** It must be
  reproducible from the run seed, and `clone()` must copy it — MCTS rollouts
  that mint fresh uids would write back to the wrong cards.
- **The uid must NOT enter the observation.** It is bookkeeping, not something a
  human perceives; the obs continues to see card *types* via count planes. Adding
  it would be a parity violation and would reintroduce a false ordinal.
- **Cards created during combat** (Wound, Dazed, Slimed, Burn, Infernal Blade's
  grant) get a sentinel uid marking them combat-scoped, so write-back skips them
  without a special case per card.

Scope: this touches `clone()`, `Card` construction everywhere, and every test
that builds a `Card` by hand. It is the single largest change to an existing
type in v2.0.0.

### 3.3 Relic counters: per-combat or per-run

Block 9 of the observation exposes relic counters, but they do not all have the
same lifetime and the spec must say which is which per relic.

| kind | examples | reset |
|---|---|---|
| per-combat | Nunchaku (attacks played), Pen Nib | at fight start |
| per-run | Ink Bottle, Sundial (cards played across the run) | never |

⚠️ **Needs a per-relic table before implementation.** Getting this wrong is
invisible in tests that only play one fight — which is every test that exists
today.

### 3.4 `clone()`

`RunState::clone()` must be a plain copy for MCTS. Note the earlier draft
demanded "POD members, fixed arrays, no heap-owned graphs" and then listed three
`std::vector`s — `CombatState` already clones correctly *with* vectors, so the
constraint as stated was simply wrong. The real requirement is: **no raw
pointers or shared ownership**; value semantics throughout.

### 3.5 RNG streams — independent, named, derived

v1.0.0's determinism is per-`CombatState` (one `seed`, one `mt19937`). A run has
many random systems, and **sharing one stream across them is a trap**: adding a
Courier restock, or §9's random Match-and-Keep resolution, would consume draws
and shift *every downstream fight*. CLAUDE.md calls the RNG stream **an
interface** for exactly this reason.

**Rule: one run seed, from which independent named streams are derived. A draw
in one stream never perturbs another.**

| stream | governs |
|---|---|
| `map` | map generation for the act |
| `encounter` | which monster group each combat node holds |
| `combat[floor]` | shuffles and enemy AI **within one fight** — seeded per floor |
| `card_reward` | reward rarity rolls and card choices |
| `shop` | stock, prices, discount slot |
| `event` | which event a `?` becomes, and its internal rolls |
| `potion` | drop rolls (40% base, with drift) |
| `relic` | which relic a chest or elite grants |
| `auto_resolve` | §9's engine-side policies — **must be its own stream**, so an auto-resolved shrine cannot shift the fights after it |

Derivation: `stream_seed = hash(run_seed, stream_id, index)` — splitmix or
similar. `combat[floor]` being indexed by floor means a fight replays identically
regardless of what happened on other floors, which preserves v1.0.0's guarantee
inside each fight.

**Required test:** same run seed → identical trajectory, cross-platform, in CI.
And a stream-isolation test: toggling an auto-resolve outcome must not change any
combat's card order.

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

## 4.1 Map generation

Ported from [`sts_map_oracle`](https://github.com/Ru5ty0ne/sts_map_oracle), a
pitch-perfect reimplementation. **Transcribe it; do not re-derive it.** The rules
below are read from its `src/lib.rs` so an implementer knows what "correct"
means, but the port is the deliverable.

### Parameters (`src/main.rs`)

```
map_height   = 15      // grid rows; floor 16 is the boss, floor 17 the chest
map_width    = 7       // grid columns
path_density = 6       // paths carved, NOT the width — a floor holds ≤6 rooms
```

### Room types

`MonsterRoom` · `MonsterRoomElite` · `EventRoom` (`?`) · `RestRoom` ·
`ShopRoom` · `TreasureRoom` — six, plus a seventh "no room here" state for empty
grid positions, giving the 8-wide one-hot in §5.1 block 12.

### Generation order

1. **Carve `path_density` paths** bottom to top. Edges reach only columns
   `c−1, c, c+1`; first-floor edges sharing a destination are trimmed.
2. **Fixed floors** override everything: floor 1 easy combat, **floor 9
   treasure**, **floor 15 rest**, floor 16 boss.
3. **Quota the remaining rooms** by chance × available count, in the fixed order
   `[Shop, Rest, Elite, Event]`; whatever is left becomes `MonsterRoom`.
   Chances: Monster 53%, Event 22%, Rest 12%, Elite 8%, Shop 5%.
4. **Assign to nodes** in row-major order, taking the first type from the quota
   list that passes all three rules.

### The three placement rules (all must pass)

| rule | constraint |
|---|---|
| `rule_assignable_to_row` | **No Rest or Elite on rows ≤ 4.** **No Rest on rows ≥ 13.** |
| `rule_parent_matches` | A node may not take Rest / Treasure / Shop / Elite if **any parent** already has that type |
| `rule_sibling_matches` | A node may not take Rest / Treasure / Shop / Elite / Monster / Event if **any sibling** (another child of a shared parent) already has it |

Note the asymmetry: the sibling rule covers *all six* types, the parent rule only
four. Do not "tidy" that — it is the game's behaviour.

Row 0 is the documented escape hatch: if no type passes, row 0 takes the first
row-assignable type anyway.

### ⚠️ Still needed: the `?` resolution distribution

Room types are fixed at generation **except `EventRoom`**, whose contents are
rolled **on entry** (§5.6.1). The roll has **stateful drift** — event chance
starts at 0.1, rises by 0.1 per non-event `?`, and resets when an event hits;
the remainder splits across Monster / Shop / Treasure.

**Those exact numbers are not yet verified** and the parity test §5.6.1 mandates
cannot be written without them. This is a remaining blocker.

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

### 6.1 The purpose collision — RESOLVED by parity

**The problem.** Card selection serves five purposes — take a reward, buy at a
shop, remove at a shop, transform (Neow/events), upgrade (Neow/smith). At a shop
"buy card *X*" and "remove card *X*" are legal *simultaneously* and would share
index `card_selection + X`.

**The resolution: selection is two-phase, because that is how the game works.**

In real Slay the Spire you never select a card in a vacuum. You click the removal
*service*, and only then does your deck open to pick from. You choose the Neow
blessing, then the card. You pick *Smith* at a campfire, then the card. **The
purpose is always chosen first, as a menu item, and the entity second.**

So the environment models it the same way:

1. An action in the phase's own menu selects the **purpose**
   (`buy_card` / `remove_card` / `smith` / `transform` / …).
2. The **card-selection block** then resolves, unambiguously, because exactly one
   purpose is live.

This is better than the alternatives on every axis that matters:

| | cost |
|---|---|
| **Two-phase (chosen)** | **no extra card actions; matches the real UI exactly** |
| Separate block per purpose | +5 × CARDS ≈ 1,250 actions, and diverges from the UI |
| `(purpose, entity)` tuple | abandons the flat space for a problem parity already solves |

**Observation requirement:** a `pending_purpose` one-hot (~6 values) so the agent
can tell *why* the card list is open. Without it, "choose a card to remove" and
"choose a card to upgrade" alias — §1.1. This is cheap and mandatory.

### 6.2 Consequence: the positional option-slot channel is superseded

Two-phase selection means the card-selection block is unambiguous, so **card
choices are entity-indexed everywhere — in combat and out.**

That retires v1.0.0's positional option-slot channel, and it is exactly the fix
`decision-points.md` §5.2 identified as correct and rejected:

> "The fix that *would* work is to **index option slots by `CardId`** rather than
> by rank … It was rejected because it is built against v2, not with it. Map
> paths, shop items, events and rest options have no `CardId`."

**That objection is now void.** Map positions, shop slots, events and rest
options each get their own entity-indexed block (§6), so nothing needs the
generic positional channel any more. The reason for the compromise has expired
along with the compromise.

Consequences to carry out:

- `payload_id` disappears — the index *is* the identity.
- Ambiguity check: two cards of the same `CardId` in one pile are genuinely
  interchangeable (and rung IDs already make a grown Rampage a distinct `CardId`),
  so entity-indexing loses nothing.

#### What exactly is superseded in `decision-points.md`

Its **§5.3 "Canonical ordering (R2)"** declares slot order to be *part of the
public interface*:

> - **Card choices:** ascending `CardId`, deduplicated by type…
> - **Non-card choices (v2):** ascending natural index (map branch order, shop
>   slot order, event option order as authored).

That rule exists to *mitigate* rank-indexing — ordering slots by ascending
`CardId` makes slot *k* stable across states that share an option set. Under
§6.2 there are **no positional slots left to order**, so:

| §5.3 clause | status |
|---|---|
| card choices ordered by ascending `CardId` | **moot** — the action index *is* the `CardId` |
| non-card choices by natural index | **superseded** — map/shop/event get their own entity blocks |
| "deduplicated by type; identical cards collapse to one slot" | **survives, and supports this change** — it already establishes that identical cards are interchangeable, which is why entity-indexing loses nothing |

⚠️ **Amend `decision-points.md` §5.3 explicitly** rather than leaving two
shipped design docs in contradiction. The mitigation was sound for v1.0.0; it is
obsoleted by removing the thing it mitigated.

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

Two independent reviews found that §10 as originally written "would let someone
believe the design is one decision away from implementable; it is closer to
four." Revised accordingly, blockers first.

### Blockers

**Resolved since the review** — kept here so the reasoning is findable:

| was | resolution |
|---|---|
| Handoff contract | **§3.2** — field-by-field table. Surfaced a hard requirement: `Card` needs a **uid**, or a mid-combat Armaments upgrade cannot be told from a campfire smith on write-back. |
| RNG stream partition | **§3.5** — one run seed, independent named streams, `combat[floor]` indexed by floor. Auto-resolve gets its own stream so a random shrine cannot shift later fights. |
| Purpose collision | **§6.1** — selection is two-phase (purpose, then entity) because that is how the real UI works. Costs no extra actions. |
| Option-slot channel conflict | **§6.2** — superseded. Card choices are entity-indexed everywhere; `decision-points.md` §5.3 must be amended. |

**Still blocking:**

1. **The `?` room resolution distribution** (§4.1). Map *generation* is now
   specified — grid, path carving, quotas and all three placement rules, read
   from the reference implementation. What remains is the **on-entry roll for
   `EventRoom`** and its stateful drift. §5.6.1 mandates a parity test that
   cannot be written without those numbers, and CLAUDE.md forbids approximating
   them.
2. **The per-relic lifetime table** (§3.3) — which counters are per-combat and
   which per-run. Getting it wrong is invisible to any test that plays one
   fight, which is every test that exists today.
3. **Exact vocabularies** — `CARDS` scales six observation blocks *and* the
   action space, and every published number is invalidated by a later change.

### Substantive

6. **Exact vocabularies.** `CARDS`, `RELICS`, `POTIONS` and the event/option
   count are estimates; one reviewer believes "~60 event options" is low by 2–3×.
   Count them — not for correctness but for **reproducibility**: v1.0.0 froze a
   layout, and any post-hoc change invalidates every previously published number.
   **Version the obs/action layout and report the version with every result.**
7. **Ascension is undefined.** §5.1 has an `ascension` scalar and §5.2 cites
   Ascension 11, but nothing says what the benchmark runs at or whether it is
   pinned. It changes enemy HP, elite counts, starting HP and adds a curse — a
   headline experimental parameter left unstated.
8. **Neow is underspecified.** §8's "4 unless the previous run was poor" is
   undefined in an environment where an episode *is* one run with no history.
   The blessing set is not enumerated, and several blessings (remove / transform
   / upgrade a card, swap the starting relic) are shape-breakers absent from §9.
9. **Run-content generation is missing entirely**: gold drops, potion drops (40%
   base, with drift), card-reward rarity rolls, elite relic rewards.
10. **Reward parameters.** §7 never says the env takes α, β, γ as constructor
    parameters, nor how v1.0.0's per-fight ±1 and `hp_reward_coeff` are
    suppressed inside a run.

### Smaller

11. **Block-internal layout is unspecified** (§5.1 gives sizes, not offsets).
    Map: floor-major or column-major? The 7 room types are never enumerated —
    and are a *different* 7 from §4's phases. Out-edge ordering, and what fills a
    nonexistent neighbour? Normalisation convention? **Two implementers will
    produce incompatible buffers.** Publish per-block offset constants in a
    header, as `combat_env.h` already does.
12. **§6's arithmetic is wrong**: with CARDS=250 the listed blocks sum to
    **~2,096**, not "≈1,900", and it is ambiguous whether the combat block is 189
    or 250 wide.
13. **Boss identity is sized 10; Act 1 has 3 bosses.**
14. `treasure` phase (§4) — pass-through or real? If deleted, say whether the
    one-hot slot is reserved.
15. **Relic order** (§5.4) is a stated **parity defect**, not a "limitation" —
    do not let its framing contradict §1.
16. **§1 excludes "shrines as player decisions" but §9 auto-resolves Match and
    Keep!, a shrine.** Inconsistent wording.
17. **Throughput.** The observation roughly 2.4×'s, and the map (~1,270 floats)
    is constant within a fight, so `compute_obs` must update incrementally.
    Engineering, not design — but v1.0.0 published 438k/259k steps-per-second.
18. **§5.5's justification reasons from `sb3-contrib` internals.** The conclusion
    is right and algorithm-agnostic; restate it as an **aliasing** argument
    (§1.1) with the SB3 verification as a footnote, or the environment looks
    designed around one library.
19. ~~The `roadmap.md` link in §1 is broken.~~ It was a bare code-span, not a
    link, so nothing was broken — but it is now a real relative link.

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

## 12. Non-termination hazard — needs a fix, not a note

§2 says "truncates: never", and `truncated()` is hardcoded `false`
(`combat_env.h`). **Slay the Spire has no turn limit**, so a stalling policy
(block forever, never attack) produces a non-terminating episode and a hung
rollout worker.

Ship a large step cap as **real truncation with correct bootstrapping** —
`truncated=True`, not `terminated=True`, so the value function bootstraps rather
than treating the cut-off as a terminal state.

## 13. Evaluation protocol — MISSING, and required for M3

The spec says nothing about how an agent is evaluated. Without this, M3's
comparison table is not publishable.

| requirement | why |
|---|---|
| **Held-out eval seeds, disjoint from training seeds** | otherwise M3 measures seed memorisation |
| **≥500 eval episodes per point** | run-to-run variance is large |
| **≥5 training seeds**, binomial CIs on win rate | a single run's win rate means little |
| **Secondary metrics**: floors cleared, HP at boss, elite-fight rate, final deck size | win rate alone hides *how* |
| **Shipped baselines**: random-legal, and a scripted heuristic (tier-list drafter + greedy combat) | a PPO number with nothing to compare against is uninterpretable |
| **Simulator-calls-per-decision and wall-clock**, alongside env steps | MCTS spends orders of magnitude more per decision; without this the M3 table compares nothing |
| **Seed semantics stated**, plus a same-seed → same-trajectory CI test | does one seed fix map + encounters + shuffles + rewards + events? |
| **A divergence table** for §9's auto-resolves | win rates are not comparable to CommunicationMod / sts_lightspeed agents without it |

## 14. The biggest research risk

The reviewer's claim was that Act 1 at Ascension 0 is clearable with the starting
deck plus arbitrary junk, so a policy good at combat and random out of it would
complete runs — and **M5 ("an agent that completes a run") would score that as
success**, leaving the project's differentiator unsupported by its own headline
result.

**Rob's correction (2026-08-13, and he is the domain expert here): a fully
random policy does NOT win — not in combat, and not across sequential fights.**
The reviewer overstated it.

But the *narrow* version survives and is the one that matters:

> Does a **competent combat policy with random drafting** clear Act 1?

Not "random everything" — random only at the card-reward, shop and rest
decisions, with combat played well. That is the exact confound M5 cannot see,
because M5 measures run completion and both agents complete runs.

**This is empirically testable before the run layer exists**, and cheaply:
simulate sequential Act 1 fights with HP carrying over, using a scripted combat
heuristic, and compare

| arm | deck |
|---|---|
| A | starter deck only, no additions |
| B | starter + randomly chosen card rewards |
| C | starter + tier-list drafted rewards |

If **B ≈ C**, drafting does not discriminate in Act 1 at A0 and M5 needs
redefining. If **B ≪ C**, drafting matters and M5 is a valid milestone.

**Timing (Rob):** run it once v2.0.0 is built, not before — the honest version
needs the real run layer (sequential fights, HP carry, actual reward pools), and
an approximation against the v1 engine would answer a different question.

The consequence to accept: **M5 stays provisional until that experiment runs.**
If drafting turns out not to discriminate in Act 1 at A0, M5 is measuring combat
competence wearing a run-completion badge, and the milestone — not the
environment — is what needs changing.

The scale of the problem: ~16 draft/shop/rest decisions per ~200-step episode,
each a ~250-arm contextual bandit whose payoff arrives ~100 steps later,
entangled with combat variance and map RNG. The floor shaping does not help —
`α·floors` fires on ~8% of steps and is **identical whichever card was drafted**.

**Pre-register the diagnostic now**, before any result exists: learned drafting
versus (a) random legal drafting and (b) a fixed tier-list drafter, all bolted
onto the *same* trained combat policy. If (a) ≈ learned, the claim fails — and
that is worth knowing before the blog post, not after.

Related: **entity-indexed actions give zero generalisation across cards.** The
policy learns "index 137" from scratch and sees perhaps 10–15 card types per run.
Stable indexing solved *semantics*, not *sample efficiency*, and the attribute
encoding considered in `v2-obs-notes.md` is the lever that would address this.
