# v2.0.0 — full Act 1 run: implementable spec

**Status: DRAFT**, pending human review. Scope is settled
(`v2-obs-notes.md` § SCOPE DECIDED). Remaining open items are in §10.

**Audience: an implementer with no prior context.** Everything needed to build
this should be here or linked.

**How to read this document**

- **Section bodies state what is currently true.** Read them as the spec.
- **⚠️ marks something still open** — a decision not taken, or a number not yet
  verified. Every one of them is listed in §10.
- **Confidence is labelled** wherever a figure is not a counted fact:
  *confirmed* (cross-checked against the wiki), *derived-not-verified* (taken
  from a reimplementation, no wiki source), or *estimate*.
- **§15 is the corrections log.** Several things in here were wrong earlier and
  were fixed; the history lives there rather than inline, so that the spec reads
  as a spec. Consult it when you want to know *why* something is the way it is —
  or before "fixing" something back.

**Sources.** Game mechanics are taken from
[`sts_lightspeed`](https://github.com/gamerpuppy/sts_lightspeed) and
[`sts_map_oracle`](https://github.com/Ru5ty0ne/sts_map_oracle) (both MIT, both
reimplementations, both cited), and **cross-checked against
[wiki.gg](https://slaythespire.wiki.gg)** before entering this spec. That check
is mandatory — see CLAUDE.md and `prior-art-sts-lightspeed.md` §7.5. We transfer
mechanics, not code.

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
| typical length | **~400–700 steps** — see below |

A run does **not** begin in combat. `reset()` returns an observation in the
`neow` phase. This differs from v1.0.0 and is the first thing an implementer
will trip on.

**On episode length.** ~400–700 is an **estimate**, and it is built from:

- **One step is one card**, so a turn playing 3 cards is 4 steps with end-turn.
- **Cards with choices cost 2 steps** (Armaments, Exhume, Warcry) — the v1.0.0
  option-slot mechanism, still true here.
- **~16 floors, not 9**, and elites and the boss run long.
- **Non-combat decisions are more than 30** once shops (multiple purchases) and
  two-phase selection (§6.1) are counted.

⚠️ **Measure this on the walking skeleton rather than estimating again.** It
matters beyond trivia: episode length sets rollout buffer sizing, how far the
terminal reward has to propagate, and whether the §12 cap is anywhere near
normal play.

## 3. Architecture

```
RunState                     ← NEW. owns the run.
  ├── floor, gold
  ├── master deck  (vector<Card>)
  ├── relics       (vector<RelicId> — order matters, see §5.4)
  ├── potions      (vector<PotionId>)
  ├── map          (105 nodes + edges + visited)
  ├── card_removal_price
  ├── next_card_uid            (see §3.2)
  └── CombatState  ← EXISTING, extended. owned, not inherited.
```

### 3.0 Two scope decisions (Rob, 2026-08-13)

**Ascension is hardcoded to 0 and is not in `RunState` or the observation.**
Ascension changes enemy HP, elite counts, starting HP and adds a curse — a real
experimental axis, but v2.0.0 does not yet have all four acts, and carrying an
ascension parameter through every system before that is scope creep. A0 is the
pinned benchmark; the field can be added later without moving anything already
built, because a constant occupies no observation slot.

**`act` is likewise omitted.** v2.0.0 is Act 1. `RunState` gains an act field
when there is a second act to distinguish, not before.

Both are recorded because *omitting* them is the decision — a reader who knows
Slay the Spire will otherwise assume they were forgotten. This also closes §10's
"Ascension is undefined": it is now defined as pinned at 0.

### 3.0.1 Keep the `CombatState` additions non-breaking (Rob, 2026-08-13)

Relics, potions and the wider card vocabulary are additions to `CombatState`.
**Make them additive wherever possible, so v1.x remains a usable standalone
combat simulator** — with relics and potions — rather than becoming a fragment
that only works inside a run.

That has research value on its own: a combat-only environment with relics and
potions is a legitimate benchmark, cheaper to train against, and already
published on PyPI with users. It also disciplines the design — anything that
*cannot* be additive is a genuine coupling worth noticing rather than absorbing.

Concretely: relic and potion state lives in `CombatState` with empty defaults, so
a `CombatEnv` constructed the v1.0.0 way behaves exactly as it does today.

### 3.1 `CombatState` is modified

v2 changes `CombatState`. This is the largest piece of work in the project, and
it is forced four independent ways:

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

**RESOLVED — the in-scope list, and what "unchanged" still claims.**

In scope for `CombatState` / `turn_loop`:

| # | change | when |
|---|---|---|
| 1 | `Card` gains `uid` (§3.2) | Phase 5, first |
| 2 | Card vocabulary 189 → `CARDS`; every derived constant in `turn_loop.h` moves | with the vocabulary count |
| 3 | Relic list + per-combat relic counters, and start-of-combat hooks | after the skeleton |
| 4 | Potion inventory, potion actions in `decode_action`, potion effects into the action queue | after the skeleton |
| 5 | Curse / colorless behaviour | with the vocabulary |
| 6 | Entity-indexed card selection replaces the positional option-slot channel (§6.2) | breaking; own task |

What **"unchanged" still guarantees**, precisely:

- **`clone()` stays a plain copy**, and every added field stays POD / fixed-array
  so it remains one. This is the load-bearing one — MCTS depends on it.
- **The action-queue invariant holds**: the queue is empty at every agent
  decision point, including the new non-combat ones.
- **Combat semantics do not change** for a deck of existing cards with no relics
  and no potions — the v1.0.0 regression suite is the check (§3.0.1).

What it does **not** guarantee: struct layout, the numeric value of any obs or
action constant, or `kObsSize`. Those all move, by design.

⚠️ **Clone stays a plain copy, but "plain" must also stay cheap.** `RunState`
adds a master deck, a 105-node map and relic/potion vectors; a naive deep copy
per MCTS node is a throughput problem, not a correctness one. Measure
`clone()` on `RunState` before and after — v1.0.0 published 438k steps/sec and
that number is now load-bearing for the project's second milestone.

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

`RunState::clone()` must be a plain copy for MCTS.

**The requirement is: no raw pointers, no shared ownership — value semantics
throughout.** `std::vector` members are fine; `CombatState` already clones
correctly with them.

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

**This matches the real game (Rob, 2026-08-13).** Slay the Spire mints one run
seed and derives every system's randomness from it; the same seed with the same
actions replays a run exactly. That is the target behaviour, and the named-stream
partition is *how* it is achieved rather than a departure from it — a single
shared stream would make replay fragile under any content change.

**Required test:** same run seed + same action sequence → identical trajectory,
cross-platform, in CI. Plus a stream-isolation test: toggling an auto-resolve
outcome must not change any combat's card order.

#### Roll timing is part of parity, not an implementation detail

#### Potion card-choices: rolled ON USE, from a stream that rarely advances

**RESOLVED 2026-08-15.** Rob's play observation was that Attack/Skill/Power
Potion options "don't change based on when you use the potion", suggesting they
are rolled at fight start. **The observation is correct; the mechanism is not
what it looks like, and the difference changes the implementation.**

`sts_lightspeed` `Actions::DiscoveryAction` generates the cards **inside the
action's lambda** — that is, when the action *executes*, i.e. when the potion is
drunk:

```cpp
Action Actions::DiscoveryAction(CardType type, int amount) {
    return {[=] (BattleContext &bc) {
        bc.openDiscoveryScreen(
            sts::generateDiscoveryCards(bc.cardRandomRng, bc.player.cc, type), amount);
    }};
}
```

So the roll happens on use. **Both observations are consistent** because
`cardRandomRng` is a *dedicated combat stream that almost nothing advances*.
Drinking the potion on turn 1 or turn 5 draws from the same stream position, so
the same cards come out — which looks exactly like a fight-start roll.

**Why the distinction is load-bearing:** anything else that draws from
`cardRandomRng` *does* change the offer. Infernal Blade uses
`getTrulyRandomCardInCombat(bc.cardRandomRng, …)`, as does another Discovery. So
in the real game, **playing an Infernal Blade before drinking an Attack Potion
changes which cards the potion offers.**

| model | matches "timing doesn't matter"? | matches the Infernal Blade interaction? |
|---|---|---|
| roll at fight start, store | ✅ | ❌ — offer would be frozen |
| **roll on use from `cardRandomRng`** | ✅ | ✅ |

**Implement: roll on use, drawing from a dedicated per-combat `cardRandomRng`.**
The "cannot re-roll by waiting" behaviour then falls out naturally, without
being special-cased — and the obscure interaction that *does* re-roll is
preserved for free.

⚠️ This makes `cardRandomRng` a **named stream in its own right** (§3.5), not a
convenience alias for the combat stream. Which draws land on it is now a parity
question: get the set wrong and potion offers desynchronise from the real game.

#### Roll timing for every random offer — RESOLVED

Each of these is observable to a player who reloads a save, so each is a parity
claim rather than an implementation detail.

| offer | rolled | stream | source |
|---|---|---|---|
| Map (whole act) | at run start | `map` | `sts_map_oracle` |
| `?` room contents | **on entry** | `event` | `getEventRoomOutcomeHelper` (§4.1) |
| Which event a `?` becomes | on entry, after the `?` roll | `event` | `generateEvent` |
| Combat card reward | **at fight end** — `createCombatReward()` builds the reward and opens the screen in the same step | `card` | `GameContext.cpp` |
| Combat gold / potion drop | at fight end, same call | `treasure` / `potion` | as above |
| Elite relic | at fight end | `relic` | `createEliteCombatReward` |
| **Shop stock and prices** | **on entering the shop room** — `screenState = SHOP_ROOM` and `info.shop.setup(*this)` are the same two lines; nothing is generated at map time | `merchant` / `card` | `GameContext.cpp:834–835` |
| Potion card-choices | **on use** — see above | `cardRandom` | `Actions::DiscoveryAction` |

**The pattern: everything is rolled when its screen opens, not when the map is
generated.** The one exception is the map itself. Potion card-choices look like
an exception and are not — they are rolled when *their* screen opens, which is on
use (and appear frozen only because their stream rarely advances).

⚠️ **Consequence for `RunState`:** shop stock is **not** part of map generation
and must not be pre-computed at run start. Doing so would be observable — a
player who reloads before entering a shop gets different stock in our engine and
the same stock in the real game, or vice versa.

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

**RESOLVED (Rob, 2026-08-13): `treasure` is a deterministic pass-through.** An
Act 1 treasure chest grants exactly one relic and the player has no choice, so
the engine awards it and advances. No agent decision, no action-space slot.

The phase one-hot slot is **retained** — the agent should still see that it is
on a treasure floor, and collapsing the enumerator would renumber the others for
no benefit. Which relic the chest grants is drawn from the `relic` stream (§3.5).

## 4.1 Map generation

**Reimplemented in C++, deriving from
[`sts_map_oracle`](https://github.com/Ru5ty0ne/sts_map_oracle) (MIT).** No Rust
enters the build — a whole toolchain for one algorithm is not a trade worth
making. The rules below are read from its `src/lib.rs` so an implementer knows
what "correct" means; the C++ port is the deliverable.

**Cite it.** `sts_map_oracle` is where these rules came from, and the derivation
belongs in the source header, the README's acknowledgements, and any write-up.
Rob's note stands: it is a very good starting point but **not verified as 100%
correct**, so treat divergence from the real game as a bug in our port *or* in
the reference, and check before assuming which.

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

### The `?` resolution distribution

Room types are fixed at generation **except `EventRoom`**, whose contents are
rolled **on entry** (§5.6.1).

**Event is the fallback, and it is the common outcome.** Monster, Shop and
Treasure are rare rolls that drift upward until they hit. *(This is the reverse
of the intuitive reading — see §15.)*

| outcome | base | on failing to occur | on occurring |
|---|---:|---|---|
| **Monster** | 10% | +10% | reset to 10% |
| **Shop** | 3% | +3% | reset to 3% |
| **Treasure** | 2% | +2% | reset to 2% |
| **Event** | — | — | **the fallback: taken when none of the above hits** |

So a first `?` is ~85% an event, and consecutive non-monster `?` rooms make a
monster steadily more likely — the game's pity system. All three counters
**reset at act transitions**, which is unobservable within v2.0.0's single act
but should be implemented anyway so Act 2 does not inherit a silent bug.

**Sources — now confirmed against a third, and it is executable.**
`sts_lightspeed`'s `GameContext::getEventRoomOutcomeHelper` matches the wiki
numbers exactly. Confidence upgraded from "two secondary sources" to
"**two secondary sources plus a working implementation**".

- [wiki.gg — Map Locations](https://slaythespire.wiki.gg/wiki/Map_Locations) (mechanism only)
- [Fandom — Unknown Location](https://slay-the-spire.fandom.com/wiki/Unknown_Location) (numbers)
- [Spire Codex](https://spire-codex.com/mechanics/unknown-rooms) (numbers, but StS2)
- **`sts_lightspeed` `src/game/GameContext.cpp`** — the algorithm below

#### The exact algorithm (port this)

```
roll = eventRng.random()                      // [0,1)
idx  = (int)(roll * 100)

monsterSize  = monsterChance * 100                                  // 10
shopSize     = (lastRoomWasShop ? 0 : shopChance * 100) + monsterSize
treasureSize = treasureChance * 100 + shopSize

idx < monsterSize   -> MONSTER
idx < shopSize      -> SHOP
idx < treasureSize  -> TREASURE
else                -> EVENT

// drift, applied to the FINAL choice
MONSTER  ? monsterChance = 0.10 : monsterChance += 0.10
SHOP     ? shopChance    = 0.03 : shopChance    += 0.03
TREASURE ? treasureChance= 0.02 : treasureChance+= 0.02
```

**Three rules we did not have, all easy to get wrong. All three wiki-confirmed
(2026-08-14):**

1. ✅ **A `?` cannot become a Shop if the previous room was a Shop** —
   `lastRoomWasShop` zeroes the shop band for that roll. The band is *removed*,
   so its probability falls through to Treasure/Event, and `shopChance` still
   increments because the choice was not SHOP.
   ➕ **Wiki adds:** this applies whether the previous shop was a Shop *room* or
   a `?` that resolved to one. So `lastRoomWasShop` is set by both paths.
2. ✅ **Juzu Bracelet converts a MONSTER result into an EVENT.** Wiki: *"Regular
   enemy combats are no longer encountered in ? rooms."*
   ⚠️ **But the reset ordering is simulator-only and no wiki source can settle
   it.** In `sts_lightspeed` the conversion happens *inside* the
   `choice == MONSTER` branch, so `monsterChance` **still resets to 0.10** even
   though the room became an event. An implementation that checks the room type
   *after* the swap would increment instead — and the two drift apart over a run
   while both looking correct in any single-room test.
   **Follow the simulator here**, and record it as a derived-not-verified
   behaviour: it is an internal counter, invisible to a player except through
   long-run distribution, which is precisely why the wiki does not document it.
3. ✅ **Tiny Chest** forces every 4th `?` to TREASURE via a relic counter
   (0→3, reset on fire), *before* the roll — and the forced TREASURE then feeds
   the normal drift block, resetting `treasureChance`. Wiki: *"Every 4th ? room
   is a Treasure room."*

**Shrine vs event:** `generateEvent` rolls `SHRINE_CHANCE = 0.25` first. So a `?`
that resolves to EVENT is 25% a shrine, 75% a regular event, falling back to the
other list when either is exhausted.

⚠️ **`SHRINE_CHANCE = 0.25` is simulator-only — no wiki source states the
shrine-vs-event split.** Implement it, flagged as derived-not-verified.

Note also that several "one-time" events (All-Knowing Skull, The Joust, Secret
Portal) are **internally treated as shrines**, which matches `Events.h` drawing
them through the shrine path rather than the per-act event lists.

#### The shrine re-entry bug — reproduce it

[Correlated Randomness in Slay the Spire](https://forgottenarbiter.github.io/Correlated-Randomness/)
documents a **bug in the real game**: after encountering a shrine event, the
first `?` room re-entered has roughly an **80% chance of being a regular enemy
encounter**, under some circumstances — the result of a coding error, not a
designed mechanic.

**DECIDED (Rob, 2026-08-14): reproduce it. Stay faithful to the game.**

The one real argument against reproducing a bug is that it might be patched,
turning today's parity into tomorrow's divergence. **That argument does not apply
here: Slay the Spire 1 is no longer being updated.** The game is a fixed target,
so its behaviour — including its bugs — is a stable specification rather than a
moving one.

That settles the general principle too, and it is worth stating once:

> **"Faithful to Slay the Spire" means faithful to the shipped artifact, not to
> the designers' intent.** Where they differ, the shipped artifact wins. An agent
> trained against the intended-but-nonexistent version is trained against a game
> nobody plays.

Consistent with what we already do: §3.5 treats Neow's *no-op* `r.random(0,0)`
as load-bearing, which is the same category of unintended-but-real behaviour, and
both `sts_map_oracle` and `sts_lightspeed` chase bit-exact RNG including quirks.

⚠️ **Implementation note:** this needs the *exact* conditions, which
["under some circumstances"](https://forgottenarbiter.github.io/Correlated-Randomness/)
does not give. Read the linked analysis properly before implementing, and treat
the 80% figure as approximate until the mechanism is understood.

**Those exact numbers are not yet verified** and the parity test §5.6.1 mandates
cannot be written without them. This is a remaining blocker.

## 4.2 Run-content generation (ROB-106)

From `sts_lightspeed` `src/game/GameContext.cpp`. This section was previously
listed as "entirely unspecified"; it is now specified except where marked.

### Gold

| source | amount |
|---|---|
| normal combat | `treasureRng.random(10, 20)` |
| elite combat | `treasureRng.random(25, 35)` |
| boss combat | `100 + miscRng.random(-5, 5)` |

Golden Idol adds `round(gold * 0.25)` in all three cases. (Ascension ≥13 scales
boss gold to 75% — not reachable at our pinned A0, §3.0.)

### Potion drops

```
chance = 40 + potionChance          // potionChance starts at 0
if White Beast Statue: chance = 100
if (potionCount + relicCount + goldRewardCount + cardRewardCount) >= 4: chance = 0

if potionRng.random(99) >= chance:  potionChance += 10      // no drop
else:                               drop a potion; potionChance -= 10
```

⚠️ **Two things a naive implementation gets wrong.** The drift is **±10 and
symmetric** — it goes *down* on a successful drop, so it is not a one-way pity
counter. And **a reward screen already holding 4 items suppresses potions
entirely**, which couples potion drops to relic and card rewards rather than
being independent.

### Card reward rarity

```
roll = cardRng.random(99) + cardRarityFactor      // cardRarityFactor starts at 5

if room == BOSS: RARE

rareChance     = (room == ELITE ? 10 : 3)
uncommonChance = (room == ELITE ? 40 : 37)
if room != REST and N'loth's Gift: rareChance *= 3

roll < rareChance                  -> RARE
roll < rareChance + uncommonChance -> UNCOMMON
else                               -> COMMON
```

Drift, applied per card rolled:

| result | effect on `cardRarityFactor` |
|---|---|
| COMMON | `max(factor − 1, −40)` |
| UNCOMMON | **unchanged** |
| RARE | reset to `5` |

⚠️ Note the factor is **added to the roll**, so a *lower* factor makes rares more
likely — it drifts from +5 down to −40. Uncommon leaving it untouched is easy to
miss and would otherwise make rares far too common.

### Elite relics

`returnRandomRelic(returnRandomRelicTierElite(relicRng))`, drawn from `relicRng`.
Black Star adds a second. Burning elites set `emeraldKey` — not applicable to v2
(key/Act 4 content is out of scope).

### 4.3 Shops

From `src/game/Shop.cpp` and `include/game/Shop.h`.

**Cross-checked against [wiki.gg — Shop](https://slaythespire.wiki.gg/wiki/Shop);
every figure below matched exactly** — stock composition, the 75 + 25 removal
progression, and the single 50%-off card. No corrections needed.

**Stock is 5 class cards + 2 colorless + 3 relics + 3 potions + 1 removal.**

| slot | contents |
|---|---|
| 0, 1 | two **Attack** cards, distinct, rarity rolled per slot |
| 2, 3 | two **Skill** cards, distinct |
| 4 | one **Power** card — **rarity upgraded: a COMMON roll becomes UNCOMMON** |
| 5 | colorless **UNCOMMON** |
| 6 | colorless **RARE** |
| 7–9 | relics — slots 0/1 by tier roll, **slot 2 is always the SHOP tier** |
| 10–12 | potions |
| 13 | card removal |

**Shop rarity roll** differs from the combat-reward roll (§4.2):

```
BASE_RARE_CHANCE = 9        // vs 3 for combat rewards
BASE_UNCOMMON_CHANCE = 37
roll = cardRng.random(99) + cardRarityFactor
roll < 9                 -> RARE
roll >= 9 + 37           -> COMMON
else                     -> UNCOMMON
```

⚠️ Note it shares `cardRarityFactor` with combat rewards, so **shop purchases and
card rewards drift each other's rarity odds**. Another coupling to model rather
than treat as independent.

**Pricing**

| item | price |
|---|---|
| class card | `cardRarityPrices[rarity] * merchantRng.random(0.9, 1.1)` |
| colorless uncommon (slot 5) | `75 * random(0.9, 1.1) * 1.2` |
| colorless rare (slot 6) | `150 * random(0.9, 1.1) * 1.2` |
| relic | `relicBasePrice * merchantRng.random(0.95, 1.05)` |
| potion | `potionRarityPrices[rarity] * merchantRng.random(0.95, 1.05)` |

`cardRarityPrices = {50, 75, 150, …}` (common, uncommon, rare).
`relicTierPrices = {150, 250, 300, 999, 150, 300, 400}`.
`potionRarityPrices = {50, 75, 100}`.

**One card slot of the first five is half price**:
`saleIdx = merchantRng.random(4); prices[saleIdx] /= 2`. This is the discount
slot §5.1 block 18 must expose.

**Card removal:**

```
cost = 75 + 25 * shopRemoveCount      // BASE_REMOVE_PRICE + REMOVE_PRICE_INCREASE
Smiling Mask: flat 50
```

`shopRemoveCount` is **per-run, not per-shop** — removal gets permanently more
expensive each time it is used. That is run state, and it belongs in `RunState`.

**Discounts** are multiplicative on all prices: The Courier ×0.80,
Membership Card ×0.50. (Ascension ≥16 ×0.80 — not reachable at A0.)

### 4.4 Neow — and a spec contradiction it exposes

From `src/game/Neow.cpp`, **cross-checked against
[wiki.gg — Neow](https://slaythespire.wiki.gg/wiki/Neow)**.

#### The 4-option ruling, precisely

The wiki and `sts_lightspeed` appear to disagree, and resolving it *strengthens*
§8 rather than undermining it:

| source | says |
|---|---|
| wiki.gg | **two or four** blessings — two if you did not reach the Act 1 boss on your previous run, four if you did |
| `sts_lightspeed` | `getOptions` returns `std::array<Option,4>` **unconditionally** |

They do not actually conflict. `sts_lightspeed` is a **single-run simulator with
no cross-run history**, so it cannot evaluate the condition and always takes the
4-option branch. That is the same reason §8 gives for our ruling.

**So: two independent projects diverge from the real game in the same direction,
for the same structural reason.** Our divergence is confirmed, and now precisely
statable:

> The real game shows 2 blessings to a player who has not yet reached the Act 1
> boss, and 4 otherwise. Mini-spire has no cross-run state, so it always shows 4.
> **A mini-spire run is therefore equivalent to a real run by a player who has
> previously reached the Act 1 boss** — never to a first-ever run.

That belongs in the README divergence table (§9) in exactly those terms, because
it is the difference between "we simplified" and "we pinned a well-defined
branch".

Each option is a `(Bonus, Drawback)` pair. Generation:

| slot | bonus | drawback |
|---|---|---|
| 0 | `random(0,5)` from tier 1 | **NONE** |
| 1 | `6 + random(0,4)` from tier 2 | **NONE** |
| 2 | from a pool **selected by the drawback** | `2 + random(0,3)` |
| 3 | **always `BOSS_RELIC`** | **always `LOSE_STARTER_RELIC`** |

**Tier contents wiki-confirmed (2026-08-14)** — counts match `sts_lightspeed`'s
index ranges exactly: 6 / 5 / 7.

| tier | contents |
|---|---|
| 1 (idx 0–5) | choose 1 of 3 cards · random rare card · remove a card · upgrade a card · transform a card · uncommon colorless |
| 2 (idx 6–10) | 3 potions · random common relic · **+8 Max HP** · Neow's Lament · 100 gold |
| 3 (idx 11–17) | rare colorless · remove 2 · rare relic · choose a rare card · 250 gold · transform 2 · **+16 Max HP** |
| drawbacks | **−8 Max HP** · `floor(hp/10)*3` damage · obtain a curse · lose all gold |

#### ⚠️ Max HP changes are FLAT per character, not percentages

`sts_lightspeed` names these `TEN_PERCENT_HP_BONUS`, `TWENTY_PERCENT_HP_BONUS`
and `TEN_PERCENT_HP_LOSS`. The wiki gives per-character flat values:

| blessing | Ironclad | Silent | Defect | Watcher |
|---|---:|---:|---:|---:|
| tier-2 Max HP gain | **+8** | +6 | +7 | +7 |
| tier-3 Max HP gain | **+16** | +12 | +14 | +14 |
| Max HP loss drawback | **−8** | −7 | −7 | −7 |

**The percentage reading is a coincidence that holds only for the Ironclad.**
80 × 10% = 8 and 80 × 20% = 16 — correct. But the Silent has 70 Max HP, where
10% would be 7 and the real value is 6.

Since v2.0.0 is Ironclad-only the numbers agree either way, so this is not a
present-day bug. **Implement the flat values anyway.** Storing it as a percentage
is a latent defect that would silently produce wrong numbers the moment a second
character is added — and it would look correct in every Ironclad test.

This is the third case of a `sts_lightspeed` *identifier or display string* being
misleading where its behaviour is right (after `"Take 30% Hp damage."` and the
inverted card-rarity sign). Pattern worth naming: **read their arithmetic, not
their names.**

Drawbacks: −10% Max HP · lose all gold · obtain a curse · HP damage ·
lose starter relic.

⚠️ **The damage drawback is not "30%".** `sts_lightspeed` labels it
`"Take 30% Hp damage."`, but wiki.gg gives the actual formula:

```
damage = floor(currentHp / 10) * 3
```

These differ. At 75 HP: `floor(7.5) * 3 = 21`, where a literal 30% would be 22.5.
**Implement the floor-based formula**, and treat the simulator's display string
as a label rather than a spec — a case where the wiki is the better source.

⚠️ **The Neow boss-relic pool excludes one relic.** wiki.gg: *"Cannot switch to
the stronger version of your character's Base Relic."* For the Ironclad that
means **Black Blood is not offered** by Neow (it is the upgraded Burning Blood).
It remains in `RELICS` for the observation, but not in Neow's draw pool.

Slot 2's bonus pool depends on its drawback (e.g. NO_GOLD excludes the
250-gold bonus, CURSE excludes remove-two). `PERCENT_DAMAGE` draws from all of
tier 3.

⚠️ **`r.random(0, 0)` is called at the end of `getOptions` — a draw that changes
nothing but advances the stream.** Our RNG must consume it too or every
subsequent Neow-stream draw desynchronises. Exactly the kind of thing CLAUDE.md
means by treating the RNG stream as an interface.

#### Boss relics reach the run through Neow

Neow's slot 3 is *always* a boss relic, offered on floor 0 in exchange for the
starter relic — so **all 22 are reachable in every run**, before the first fight.
No *post-boss* relic is ever awarded, since the run ends at the Act 1 boss, but
that was never the only source.

Consequences:

1. **`RELICS` includes all 22 boss relics** — see §5.0.
2. **Burning Blood can be lost**, so the relic multi-hot must represent a run
   with no starter relic. Any code assuming Burning Blood is always present is
   wrong.
3. **A boss relic on floor 0 is a large swing** — this is a real strategic
   decision the agent must be able to see and take, not an edge case.

### Still open

- **Per-event option counts** — needs the event logic, not the event list.
- **Act 1 event-relic subset**.

## 5. Observation

Flat `Box(float32)`, one vector. **Unified** — combat and non-combat share one
observation, so a researcher may split it but the environment does not prescribe
one (`observation-space.md` §1; the environment ships the superset).

### 5.1 Layout

Planning vocabulary — see §5.0 for the scoping rules and current confidence.

### 5.0 Vocabulary scoping (ROB-104)

**The counts are not "how many exist in Slay the Spire". They are "how many are
reachable in v2.0.0's scope"**, and the scoping rules are the design decision —
counting is mechanical once they are fixed.

Rules:

1. **Ironclad + colorless only.** Silent / Defect / Watcher cards and
   class-specific relics and potions are unreachable and excluded.
2. **Boss relics included** — Neow offers one on floor 0 of every run (§4.4).
3. **Blights excluded** — not part of the base game's normal run.
4. **Upgraded variants count separately.** v1.0.0's 189 counts `Strike` and
   `Strike+` as distinct ids, and rung ladders add more (§ `observation-space.md` §5).
   Any colorless or curse count must be expanded the same way.
5. **Act 1 reachability** where a pool is act-gated.

#### Source: `sts_lightspeed`, not wiki prose

Counted from
[`gamerpuppy/sts_lightspeed`](https://github.com/gamerpuppy/sts_lightspeed)
`include/constants/` — `CardPools.h`, `RelicPools.h`, `Potions.h`, `Events.h`.
These are **declared array sizes in a working simulator**, so they are
machine-checkable and re-verifiable, unlike a wiki page summary.

An earlier pass used wiki fetches and produced "Total: 19+" for potions with a
note that it was not exhaustive. **A summary that says "19+" is not a count**,
and an incomplete enumeration is indistinguishable from a complete one — the
same discipline CLAUDE.md records for grep. The numbers below replace that pass.

#### CARDS

| group | base | upgrades? | ids |
|---|---:|---|---:|
| Ironclad obtainable pool | 72 | yes | 144 |
| Ironclad starters (Strike, Defend, Bash) | 3 | yes | 6 |
| Searing Blow rung ladder | — | — | ~35 |
| Statuses (Slimed, Dazed, Burn, Wound) | 4 | — | 4 |
| **= v1.0.0 today** | | | **189** ✅ |
| **Colorless** | **35** | yes | **+70** |
| **Curses** (random pool) | **10** | no | **+10** |
| **CARDS** | | | **≈269** |

- Ironclad pool is `RarityCardPool::groupSize[0] = {20, 36, 16}` → 72, matching
  `cardPoolSize[0]`. Independently confirms our 189.
- Colorless is `srcColorlessCardPoolSize = 35`, split `{0 common, 20 uncommon, 15 rare}`.
- Curses are `curseCardPoolSize = 10`: Regret, Injury, Shame, Parasite,
  Normality, Doubt, Writhe, Pain, Decay, Clumsy. **Curses do not upgrade**, so
  the doubling rule does not apply to them.

⚠️ **Special curses** — Curse of the Bell (Calling Bell), Necronomicurse
(Necronomicon), Pride — are outside the random pool and need per-source
reachability checks. **Ascender's Bane is excluded automatically**: it is granted
at Ascension 10+, and §3.0 pins us at 0. A concrete payoff from that decision.

#### RELICS — Ironclad pools

*Confidence: derived-not-verified — see the note below.*

| pool | count |
|---|---:|
| Starter (Burning Blood) | 1 |
| Common | 33 |
| Uncommon | 30 |
| Rare | 28 |
| Shop | 17 |
| Boss | 22 |
| **subtotal** | **131** |
| Event / special | ⚠️ not in these pools; needs the Act 1 subset |
| **RELICS** | **≈131 + event relics** |

Per-class pools differ (Ironclad rare is 28, Defect 26, Watcher 27), so the
**Ironclad-specific arrays are the right source** — a generic total would be
wrong.

⚠️ **Confidence note.** These are `sts_lightspeed`'s declared `std::array`
literals: compiler-checked and re-verifiable, but **not independently confirmed
against the wiki**, and the wiki cannot confirm them by fetch — a summarising
fetch under-counts silently. Real verification means enumerating wiki category
listings item by item. See `prior-art-sts-lightspeed.md` §7.8 and §15.

⚠️ Still unhandled: Circlet / Red Circlet, awarded when every relic is collected.

#### POTIONS = 33

`Potions.h`: `potionPool[4][33]`, `poolSize = 33`. Per class, so 33 for Ironclad.

#### EVENTS = 25 reachable in Act 1

| group | count |
|---|---:|
| `Act1::events` | 11 |
| `Act1::shrines` | 6 |
| `oneTimeEventsAsc0`, **act-gated to Act 1** | **8** of 14 |
| **total** | **25** |

The one-time list is **14 at Ascension 0 and 13 at Ascension 15** (Note For
Yourself drops out) — a second place the §3.0 ascension pin changes a count. Of
those 14, `canAddOneTimeEvent` gates **6 out of Act 1** entirely; see §6.3 for
the per-event table.

**`EVENT_OPTIONS = 58`** — counted per event in §6.3, not estimated.

#### Impact on §5.1

`CARDS ≈ 269` against the planning figure of 250 widens blocks 3, 7, 10 and 18.
**Do not update §5.1's sizes until the event-relic and event-option counts
close** — a second recount is exactly what the layout-versioning decision exists
to make survivable.

#### A gap found while counting

**`CardData` has no `rarity` and no `color` field.** Fields today are name, cost,
damage, hits, block, target, debuffs, powers, type, exhaust, ethereal,
unplayable — plus `CardType` (Attack/Skill/Power/Status/Curse).

v2 needs both: **rarity** drives card-reward rarity rolls (§10 run-content
generation) and shop pricing, and **color** separates Ironclad from colorless in
shop stock. Adding them touches all 189 rows of `CARD_DATABASE`. This belongs in
the vocabulary task, since it is the same pass over the same table.

| # | block | size | contents |
|---|---|---:|---|
| 1 | player | 34 | unchanged from v1.0.0 |
| 2 | enemies | 220 | unchanged — 5 slots × 44 |
| 3 | combat piles | 5 × CARDS | unchanged shape, wider vocabulary |
| 4 | turn | 1 | |
| 5 | phase | 8 | one-hot over §4 |
| 6 | run scalars | 6 | floor, gold, card-removal price, potions-held, potion-slots, removal-used-this-shop (**no ascension, no act — §3.0**) |
| 7 | master deck | CARDS | count per card type |
| 8 | relics held | RELICS | multi-hot |
| 9 | relic counters | RELICS | the number drawn on the relic icon; 0 where none |
| 10 | bottled cards | CARDS | see §5.3 |
| 11 | potions | POTIONS | **count vector**, not slots — see §5.2 |
| 12 | map node types | 840 | 105 × 8 (7 room types + "no room") |
| 13 | map out-edges | 315 | 105 × 3 (edges reach columns c−1, c, c+1 only) |
| 14 | map visited | 105 | path walked so far |
| 15 | map column | 7 | current column; the floor is already in §6 |
| 16 | boss identity | 3 | one-hot — Act 1 has exactly 3 bosses (Slime Boss, Hexaghost, The Guardian); visible from floor 1 |
| 17 | current event | 60 ⚠️ | one-hot; zero outside `event` |
| 18 | offer prices | CARDS+RELICS+POTIONS | see §5.5 |

**Total ≈ 4,200 floats** ⚠️ (2.4× v1.0.0's 1,772), dominated by the pile planes
and the map.

### 5.1.1 How the map is encoded (Rob asked; it is not an adjacency matrix)

The map is three blocks over the same 105 grid positions (15 rows × 7 columns),
plus the current column:

| block | shape | what it holds |
|---|---|---|
| 12 — node types | 105 × 8 | one-hot per position: 6 room types + Unknown + "no room here" |
| 13 — out-edges | **105 × 3** | for each position, does an edge run to column `c−1`, `c`, `c+1` on the row above |
| 14 — visited | 105 × 1 | the path walked so far |
| 15 — column | 7 | current column (the floor is already in block 6) |

**Edges are 3 wide, not 105 wide, because StS edges are structurally local.** A
node can only connect to the three positions diagonally-up-left, up, and
up-right. A full flattened adjacency matrix would be 105 × 105 = 11,025 floats
and would be **99.7% zeros** — every entry outside those three offsets is
structurally impossible, not merely absent.

So block 13 is a *local* edge mask: 315 floats, lossless, no wasted capacity. An
off-grid neighbour (column 0 has no `c−1`) is a hard 0.

### 5.1.3 Layout conventions — PROPOSED (ROB-105), for ratification

`v2-spec.md` gave block sizes but no internal layout, which is enough for two
implementers to produce incompatible buffers. Proposed conventions, each with its
reason:

| convention | choice | why |
|---|---|---|
| **Map flattening** | **floor-major**: `index = floor * 7 + column` | The current floor's 7 positions are then **contiguous**, which is what both the renderer and any incremental-update path want. Column-major scatters them by 15. |
| **Floor ordering** | floor 0 at index 0, ascending | Matches `RunState::floor` directly; no arithmetic to get it wrong. |
| **Edge slot order** | `[c−1, c, c+1]` ascending | Matches the generator's own ordering (`sts_map_oracle` edges reach `c−1, c, c+1`), so the port needs no remapping. |
| **Nonexistent neighbour** | hard `0.0` | Column 0 has no `c−1`. Structurally impossible ≠ "absent"; both read as 0, and no third value is needed because the node-type block already marks empty positions. |
| **Room type one-hot order** | `None, Monster, Elite, Event, Rest, Shop, Treasure, Unknown` | 8 wide. **`None` first at index 0** so a zeroed buffer means "no room", which is the correct default for an unallocated map. |
| **Normalisation** | per-block **fixed constants**, published beside the offsets | Never a varying denominator — `run-reward.md` records the `hp/max_hp` bug. |
| **Offsets** | `constexpr` in a header, surfaced to Python | Exactly as `combat_env.h` does today. Re-deriving offsets caused the `TURN_NUMBER = OBS_SIZE − 1` bug. |

⚠️ **The 8 room types are a *different* 8 from §4's phase enumeration.** Phases
include `neow`, `card_reward` and `combat` which are not map positions; room
types include `None` and `Unknown` which are not phases. **Do not share an
enum between them** — they look interchangeable and are not.

**Required:** a `static_assert` tying each block's published offset to the code
that writes it, mirroring how `kTurnObsIndex` is asserted against its writer
today. That assertion is what turns a layout doc into an enforced contract.

### 5.1.2 Do count vectors and multi-hots hurt learning? (Rob asked)

A fair question, and the honest answer is *not in the way people usually fear,
but there is one real cost.*

**What is fine:**

- **Sparsity is normal.** A 250-wide deck vector with ~15 non-zero entries is
  exactly what DouZero (card matrices), gym-locm and the MTG work feed to dense
  nets. Large sparse binary/count input is routine.
- **Counts carry real information** that a multi-hot destroys: three Strikes
  plays very differently from one. Using counts is correct, not a compromise.
- **The dimensionality itself is cheap.** A 4,200-float input into a 512-unit
  layer is ~2M parameters — small by any modern standard.

**What actually needs care:**

- **Scale mismatch.** One-hots are 0/1; a deck count can be 5; gold reaches ~999.
  Feeding raw gold beside a one-hot means the first layer sees wildly different
  magnitudes. Normalise — but **by a fixed constant, never by a varying
  denominator.** `run-reward.md` records exactly this bug: dividing by current
  `max_hp` made gaining Max HP *lower* the value.

**The one real cost — and it is a genuine one:**

- **No parameter sharing across entities.** In a count vector, index 137 is just
  a coordinate. The network learns "card 137" from scratch, and learns nothing
  about it from having learned card 138 — even if they are Strike and Strike+.
  With ~15 card types seen per run and 250 in the vocabulary, most of the input
  space is visited rarely. §14 flags the same problem for the action space.

**Why the environment should still ship count vectors:** the fix is an
*architecture* — embeddings per card id, summed or attended over, giving
generalisation across cards. That is a feature extractor bolted on top, and by
**principle 3 it is the researcher's choice, not the environment's**. Count
vectors are lossless and algorithm-agnostic; an embedding layer can be built
from them, but a count vector cannot be recovered from someone else's embedding.

So: correct default, real limitation, documented rather than designed around.
This is worth a paragraph in the README when v2 ships — it is precisely the kind
of thing a researcher picking up the env wants told up front.

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

### 6.3 Event options: one global enumeration (Rob's proposal, adopted)

Rob's framing, which is cleaner than the spec's earlier `(event, option)` pair:

> Give all event options an ID. Entering a `?` room with three options exposes a
> `[EVENT_OPTION_SIZE]` action block, masked to just those three.

**Adopted — and it is the same construct stated better.** A global enumeration of
event options *is* a flattening of `(event, option)` pairs; the pair framing
implied two dimensions to index, which invited a needless second action
dimension. One flat block with masking is simpler and identical in expressiveness.

It also inherits the property §6 requires: **action *k* means the same option
forever.** "Take the gold from Big Fish" is one fixed index, never "the second
option on this screen".

#### `EVENT_OPTIONS = 58` — counted, not estimated

From `sts_lightspeed` `GameAction::getValidEventSelectBits`, which returns a
**bitmask of valid options per event** — the compact source for this, rather than
the ~1,400-line `chooseEventOption` switch.

| group | events | option ids |
|---|---|---:|
| Act 1 events | 11 | **29** |
| Act 1 shrines | 6 | **10** |
| One-time, **Act 1 reachable** | 8 | **19** |
| **total** | **25** | **58** |

**One-time events are act-gated, and most are not Act 1.**
`GameContext::canAddOneTimeEvent` gates each one:

| event | gate | Act 1? |
|---|---|:--:|
| Ominous Forge · Bonfire Spirits · Lab · Note For Yourself · We Meet Again | none (default `true`) | ✅ |
| Face Trader | `act == 1 \|\| act == 2` | ✅ |
| The Woman in Blue | `gold >= 50` | ✅ |
| The Divine Fountain | `deck.hasCurse()` | ✅ *(rare at A0 — needs a Neow curse drawback or an event curse, but reachable)* |
| Designer In-Spire | `(act == 2 \|\| act == 3) && gold >= 75` | ❌ |
| Duplicator | `act == 2 \|\| act == 3` | ❌ |
| Knowing Skull | `act == 2 && curHp > 12` | ❌ |
| N'loth | `act == 2 && relics.size() >= 2` | ❌ |
| The Joust | `act == 2 && gold >= 50` | ❌ |
| Secret Portal | `act == 3 && !speedrunPace` | ❌ |

So 6 of the 14 are unreachable in Act 1, removing 19 ids (Designer In-Spire 6,
Knowing Skull 4, N'loth 3, The Joust 2, Secret Portal 2, Duplicator 2).

**Sizing decision: `EVENT_OPTIONS = 58`, exact — do not reserve the other 19.**
The full-game enumeration is 77, but acts 2–3 would change far more than event
ids (new enemies, new encounters, new relic pools), so that is a new layout
version regardless. Reserving buys nothing and inflates every published number.

⚠️ This also settles §5.0's open question in the *opposite* direction from the
safe guess: the earlier note said counting all one-time events in was the safe
choice because a missing id costs a layout change. With the gates known, 6 events
are simply unreachable — including them would reserve dead indices the agent can
never use, which is its own (smaller) parity smell.

Per-event (max bit position used, since conditional options are masked in place):

| ids | events |
|---:|---|
| 1 | Lab · Wheel of Change |
| 2 | Dead Adventurer · World of Goop · The Ssssserpent · Hypnotizing Mushrooms · Scrap Ooze · Shining Light · Transmorgrifier · Purifier · Upgrade Shrine · Duplicator · The Divine Fountain · Note For Yourself · Secret Portal · The Joust |
| 3 | Big Fish · The Cleric · Wing Statue · Living Wall · Golden Shrine · Ominous Forge · Face Trader · N'loth |
| 4 | Knowing Skull · The Woman in Blue · We Meet Again |
| 5 | **Golden Idol** — two phases on *disjoint* bit ranges (`0b11` then `0b11100`) |
| 6 | **Designer In-Spire** — five conditional options plus a fixed "leave" at bit 5 |
| — | **Match and Keep!** — returns 0, handled separately (§9 auto-resolve) |

**Three things this confirms:**

1. **Conditional options are masked in place, not renumbered.** Pleading Vagrant
   returns `0x7` with enough gold and `0b110` without — *the same bit positions*.
   The Cleric, Wing Statue, Living Wall, Purifier and others all follow this
   shape. Independent confirmation of the ruling above.
2. **Golden Idol proves multi-phase events need disjoint id ranges.** Its two
   phases use bits 0–1 and 2–4, commented *"map these to different selections"*.
   One event, five stable ids.
3. **Events carry internal state.** `gc.info.eventData` is a phase counter
   (Colosseum, Cursed Tome, Golden Idol). ⚠️ **`RunState` needs an event-phase
   field**, and it must be in the observation — a player can see which phase of
   an event they are in.

⚠️ **Do not inherit `BONFIRE_SPIRITS = 0`.** The comment says *"we skip the
select phase of this event"* — a deliberate simplification in `sts_lightspeed`,
not game behaviour. Bonfire Spirits asks the player to sacrifice a card, which in
our design is a **card-selection purpose** (§6.1), not an event-option id. It
contributes 0 to the 77 and one entry to the purpose enum.

**Conditionally-offered options get their own ids (Rob, 2026-08-15).** Where an
event branch is only available under a condition — enough gold, a relic held —
it is a **distinct id that the mask hides when unavailable**, not a shared id
whose meaning changes.

Same argument as everywhere else in §6: action *k* means one thing forever. A
shared id would make "pay 50 gold" and "leave" the same index under different
conditions, which is rank-indexing wearing an entity-indexed costume — and it
would alias two genuinely different choices in the policy's output layer.

Note this is also why events need **no bespoke observation machinery**: block 17
holds the current event one-hot, the mask says which options are live, and the
option ids carry their own meaning. The earlier worry about events needing
individual carve-outs applies to their *effects*, not their encoding.

### 6.4 Relic selection is shop-only (Rob asked)

There is no relic *selection* block for treasure or elites. Both grant a relic
automatically (§4), so the engine awards it — no decision, no action.

Relic actions exist only for **shop purchases**, where the player chooses which
of 3 relics to buy against a gold constraint. Boss relics, which *are* a real
choice, are structurally absent because the run ends at the Act 1 boss (§9).

## 7. Reward

Fully specified in [`run-reward.md`](run-reward.md). Summary:

```
Φ(s) = α · floors_cleared(s) + β · (hp / max_hp),   Φ(terminal) = 0
reward = terminal(win/loss) + γ·Φ(s′) − Φ(s)
```

Potential-based, so the optimal policy is provably unchanged for any α, β
(Ng, Harada & Russell 1999). **Two requirements are load-bearing:** `Φ` must be 0
at terminal states, and the shaping `γ` must equal the learner's.

### 7.1 The reward is configurable — required, not optional (Rob, 2026-08-13)

**Users must be able to fall back to plain terminal win/loss, or supply their own
coefficients.** The env takes them as constructor parameters, following
`hp_reward_coeff` (ROB-52):

| parameter | default | effect |
|---|---|---|
| `floor_reward_coeff` (α) | tbd | dense progress signal |
| `hp_reward_coeff_run` (β) | tbd | HP as a resource in Φ |
| `gamma` (γ) | tbd | **must match the learner's** |
| `win_reward` / `loss_reward` | +1 / −1 | terminal |

`α = β = 0` is not a special code path — it makes `Φ ≡ 0`, so the shaping term
is identically zero and the reward *is* pure win/loss. Sparse reward falls out of
the same expression, which is why the knob is safe.

⚠️ **Two things still to specify before implementation:**

1. **How v1.0.0's per-fight ±1 and `hp_reward_coeff` are suppressed inside a
   run.** A won fight is not a won *run*; if the per-fight terminal reward fires
   on every combat, the agent is paid nine times per run for something that is
   not the objective. Almost certainly: per-fight rewards are disabled when the
   env is in run mode, and `hp_reward_coeff` stays a v1.x-only knob.
2. **Defaults.** `run-reward.md` §7 leaves the α:β ratio and their ratio to the
   terminal reward open, with the one firm requirement that **the terminal reward
   dominates**.

`run-reward.md` §5 also requires logging the **raw unshaped return** alongside
the shaped one, so shaping never touches a reported metric.

## 8. Decision points

| phase | options | notes |
|---|---|---|
| Neow | **always 4 blessings** | see below |
| map | 2–4 next nodes | Wing Boots may allow ignoring edges — obs shows edges, mask shows legality |
| card reward | 3 cards (**4** with Question Card), or skip | Singing Bowl adds "+2 Max HP instead" |
| shop | 14 slots: 5 coloured cards, 2 colorless, 3 potions, 3 relics, 1 removal | multiple purchases; Courier restocks; prices are computed like `effective_cost` |
| rest | up to 6 | rest, smith, + Recall/Lift/Toke/Dig from relics |
| event | 2–4 | globally-enumerated option ids — see §6.3 |
| combat | v1.0.0's action space | entity-indexed (§6.2) |

**Neow is always the 4-blessing set (Rob, 2026-08-13).** The real game offers a
reduced set when the *previous* run went badly — but an episode here is one run
with no history, and nothing persists across `reset()`. There is no state that
could select the reduced set, so the environment always presents the default
4-blessing screen.

This is a **deliberate, documented parity divergence**: real StS carries
cross-run state, and mini-spire does not. It belongs in §9's divergence list and
in the README, not buried here.

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

**These must be loud, not buried (Rob, 2026-08-13).** Every divergence in this
section gets its own **README section** once implementation is complete — not a
footnote in a design doc. The full list to publish:

| divergence | why |
|---|---|
| Match and Keep! resolved at random | subset choice over duplicate entities |
| Gambling Chip discards nothing | subset choice |
| Frozen Eye excluded from the shop pool | reveals draw-pile *order*; no way to show it |
| Neow always offers 4 blessings | no cross-run state exists (§8) |
| Relic order is insertion order | §5.4 — a real parity defect |
| Ascension pinned at 0 | §3.0 |

A researcher comparing mini-spire win rates against CommunicationMod or
sts_lightspeed agents **cannot interpret the numbers without this table.** That
is the actual reason it goes in the README rather than politeness about
limitations.

### Boss relics are in scope

**Every run is offered a boss relic on floor 0**, as Neow's slot 3, in exchange
for the starter relic (§4.4). No *post-boss* relic is ever awarded — the run ends
at the Act 1 boss — but Neow makes all 22 reachable before the first fight of
every episode.

Consequences: `RELICS` includes all 22 boss relics (§5.0); **Burning Blood is
losable**, so nothing may assume the starter relic is present; and the Neow
boss-relic swap is a real strategic decision the agent must see and be able to
take.

### Multi-select: costed, and the answer is "defer" (Rob asked)

The earlier claim was that multi-select "would be painful to retrofit", so the
shape should be reserved now. **Rob asked for that to be tested rather than
asserted. Having costed it: it is cheap to retrofit, so defer it.**

What multi-select needs, on top of what §6 already specifies:

| piece | cost | breaking? |
|---|---|---|
| A `confirm` action distinct from `decline` | 1 action | **no** — additive |
| A "currently selected" plane, `CARDS` wide | +250 floats | **no** — additive |
| Selections that accumulate instead of resolving immediately | a flag on the pending-choice record | **no** — new purposes only |

**Nothing on that list changes an existing index**, which is what made the
option-slot channel expensive to change and what makes this different. Under
two-phase selection (§6.1) the purpose already says how a selection resolves, so
a multi-select purpose slots in beside the single-select ones without disturbing
them.

**Decision: do not build it for v2.0.0.** No v2.0.0 decision is a subset choice
(Gambling Chip is auto-resolved, §9), and the retrofit is additive. Revisit when
a real multi-select case arrives.

**Why this is not the mistake `decision-points.md` records making.** That mistake
was shipping a *positional* channel that v2 then had to tear out — the shape was
wrong, and every index moved when it was fixed. Deferring an additive feature
whose cost has been enumerated is a different call, and the enumeration above is
what makes it safe. If any row of that table turns out to be breaking, this
decision should be revisited immediately.

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

**Closed in Rob's review (2026-08-13):**

| item | resolution |
|---|---|
| `?` distribution | **§4.1** — and it was *inverted*: Event is the fallback (~85%), Monster/Shop/Treasure are the drifting rolls at 10/3/2%. Two secondary sources; flagged as derived-not-verified. |
| Ascension | **§3.0** — pinned at 0, not in `RunState` or the obs |
| Act field | **§3.0** — omitted; Act 1 only |
| Neow blessing count | **§8** — always 4; no cross-run state exists |
| `treasure` phase | **§4** — deterministic pass-through, phase slot retained |
| Boss identity size | **§5.1** — 3, not 10 |
| Multi-select | **§9** — costed as additive, therefore deferred |
| Evaluation protocol | **§13** — out of scope; post-v2 |
| Episode length | **§2** — ~400–700, to be measured not estimated |
| Reward configurability | **§7.1** — constructor parameters, sparse falls out at α=β=0 |
| SB3 compatibility | **§10** — reported property, never a design constraint |

**Still blocking implementation:**

1. **Exact vocabularies** — `CARDS`, `RELICS`, `POTIONS`, event options. `CARDS`
   scales six observation blocks *and* the action space, and every published
   number is invalidated by a later change. Count, do not estimate.
2. **Block-internal layout** (§10.11 below) — offsets, flattening order, edge-slot
   order, normalisation convention. Two implementers currently produce
   incompatible buffers.
3. **Per-relic counter lifetimes** (§3.3) — per-combat vs per-run. **Rob: revisit
   once the spec is otherwise final**, since it is enumeration against a relic
   list that the vocabulary count (#1) produces anyway.
4. **Run-content generation** — gold drops, potion drop rates and drift, card
   reward rarity rolls, elite relic rewards. Still entirely unspecified, and
   `?`-room work does not cover it.
5. **Roll timing** (§3.5) — when each random offer is drawn, starting with
   whether StS1 rolls potion card-choices at fight start as StS2 appears to.

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

### The environment is not designed around SB3 (Rob, 2026-08-13)

A standing ruling, because it recurs:

> The env shouldn't be designed around sb3 and the limitations it provides. If
> it's not compatible with out-of-the-box MaskablePPO, that's unfortunate, but we
> shouldn't make the env more complex or worse as a result. Let's be explicit
> about what's not compatible and make appropriate tradeoffs.

So SB3 compatibility is a **reported property, not a design constraint**. Where
they conflict, parity and completeness win, and the incompatibility is
documented.

**Ship a compatibility table** in the README stating what works out of the box
and what needs custom code — e.g. a flat `Discrete` action space with a mask
works with `MaskablePPO` directly, while getting generalisation across cards
(§5.1.2, §14) needs a custom feature extractor that no library provides.

That table is more useful to a researcher than silent compatibility would be: it
says exactly where their work starts.
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

**Rob's assessment (2026-08-13): a true infinite stall is probably not reachable
in Act 1 at this scope** — the Ironclad card pool has no reliable infinite-block
engine at Act 1 card counts, and enemy damage scales past what a stalling deck
sustains.

**Ship the cap anyway, as an unreachable failsafe.** Set it far above any
plausible run (§2 measures ~400–700 steps; a cap around **10,000** is ~15× that)
so it never fires in normal play and exists purely to stop a hung rollout worker.

Two properties are required:

- **`truncated=True`, not `terminated=True`** — the value function must bootstrap
  rather than treat the cut-off as a real terminal state, or the agent learns
  that long runs end worthlessly.
- **Log every truncation.** A cap that fires is a bug report, not a routine
  outcome. If it ever trips, something is wrong with the engine or a policy found
  a stall we did not predict — and either way we want to know rather than absorb
  it silently.

## 13. Evaluation protocol — OUT OF SCOPE for this spec

**Rob's ruling (2026-08-13): this spec is scoped to functional implementation.**
Training and evaluation come after v2.0.0 ships, and specifying an eval protocol
now would be designing against agents that do not exist on an environment that is
not built.

The material below is **retained as input to that later work**, not as a v2.0.0
requirement. Nothing in it blocks implementation.

One item does have a v2.0.0 dependency worth noting: the **same-seed →
same-trajectory CI test** is already required by §3.5, independently of any
evaluation protocol. Build that one now; it is a determinism test, not an eval
test.

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

---

## 15. Corrections log

Things this spec got wrong, and what they were changed to. Kept because the
reasoning is why the current text is trustworthy — and because several of these
look "wrong" to someone who half-remembers the game and would otherwise be
"fixed" back.

The section bodies above state current truth; nothing here needs reading to
implement the spec.

### Design corrections

| # | was | is | why it changed |
|---|---|---|---|
| 1 | `CombatState` unchanged in v2 | **modified**, four ways (§3.1) | Card vocabulary widens, relics hook in, potions enter the action queue, curses need enumerators. "Unchanged" would have misled an implementer on the largest piece of work in the project. |
| 2 | `clone()` needs "POD members, fixed arrays, no heap-owned graphs" | **no raw pointers, no shared ownership** (§3.4) | The old constraint listed three `std::vector`s in the same breath. `CombatState` already clones correctly *with* vectors, so the rule as stated was self-contradicting. |
| 3 | Episode ≈ 200 steps | **~400–700**, to be measured (§2) | Computed as 9 fights × 18. Missed that one step is one card, that choice-cards cost 2 steps, that Act 1 is ~16 floors, and that shops add many decisions. |
| 4 | Positional option-slot channel carries v2's decisions | **entity-indexed everywhere** (§6.2) | `decision-points.md` §5.2 had already identified entity indexing as correct and rejected it because map/shop/event options have no `CardId`. Giving each its own block voids that objection. |
| 5 | Card selection needs a `(purpose, entity)` action dimension | **two-phase selection** (§6.1) | The real UI already chooses purpose first (click *removal service*, then the card). Parity dissolved the collision at zero action-space cost. |
| 6 | Evaluation protocol required in this spec | **out of scope** (§13) | This spec covers functional implementation; training and evaluation come after v2.0.0. The determinism test survives, because it is a determinism test, not an eval one. |
| 7 | Multi-select must be designed now — "painful to retrofit" | **deferred** (§9) | Costed rather than asserted: a confirm action, a selected-cards plane, an accumulate flag. All additive, none moving an existing index. |
| 8 | Ascension a live parameter | **pinned at 0** (§3.0) | Scope creep before all four acts exist. Also removes Ascender's Bane, shrinks the one-time event pool 14→13, and drops the boss-gold scaling branch. |

### Parity corrections — mechanics we had wrong

| # | was | is | source |
|---|---|---|---|
| 9 | `?` event chance starts 10% and drifts **up** | **Event is the fallback** (~85% on a first `?`); Monster 10/+10, Shop 3/+3, Treasure 2/+2 drift (§4.1) | Inverted. wiki.gg + Fandom + `sts_lightspeed` all agree on the corrected form. |
| 10 | Boss relics structurally unreachable | **all 22 reachable** on floor 0 (§4.4, §9) | Neow's slot 3 is unconditionally a boss relic swap. The original reasoning only considered *post-boss* awards. Second "structurally unreachable" claim to have a second source — **distrust that phrase**. |
| 11 | Neow damage drawback = 30% of HP | **`floor(currentHp / 10) * 3`** (§4.4) | `sts_lightspeed`'s *display string* says 30%; its arithmetic and the wiki agree on the floor form. At 75 HP: 21, not 22.5. |
| 12 | Neow Max HP changes are 10% / 20% | **flat per-character**: +8 / +16 / −8 for the Ironclad (§4.4) | Their enum *names* say percent. Coincides only for the Ironclad (80 HP); the Silent's real values are +6/+12/−7. Latent defect that every Ironclad test would pass. |
| 13 | `RELICS ≈ 104` | **≈131 + event relics** (§5.0) | The 104 came from a *summarising wiki fetch* reporting 26 common relics against the declared pool's 33. A fetch summary under-counts silently. |
| 14 | Φ normalised by `hp / max_hp` | **`hp / HP_REF`**, a constant (`run-reward.md` §4) | Dividing by *current* `max_hp` made Φ **fall** when Max HP was gained — a wrong-signed incentive on exactly the resource-vs-investment decisions the reward design exists to protect. |
| 15 | PBRS "cannot bias playstyle" | **does not change the optimal policy** (`run-reward.md` §3) | Ng et al. preserve the optimum, not the learned policy — and under entropy-regularised objectives (PPO's bonus) invariance does not hold at all. |

### Standing traps

Recorded because each is a place where a careful reader, checking against another
source, would "correct" the spec into being wrong.

| trap | |
|---|---|
| **Card-rarity sign** | wiki describes an offset starting −5 rising +1 per common, applied to the *rare chance*. `sts_lightspeed` uses a factor starting +5 falling −1, applied to the *roll*. `offset_wiki = −factor_lightspeed`. Same system; mixing conventions inverts it. |
| **Juzu Bracelet reset** | Converting MONSTER→EVENT still resets `monsterChance` to 0.10, because the conversion happens *inside* the `choice == MONSTER` branch. Checking room type after the swap increments instead. Invisible in any single-room test. |
| **Neow's no-op draw** | `getOptions` ends with `r.random(0, 0)` — consumes a draw, changes nothing. Skip it and every later Neow-stream draw desynchronises. |
| **Read arithmetic, not names** | Three of four defects found by cross-checking `sts_lightspeed` were mislabelled-but-correct code (#11, #12, and the rarity sign). Treat every identifier and display string there as a comment. |
| **Room types ≠ phases** | The 8 map room types and the 8 phases are *different* 8s that look interchangeable. Do not share an enum. |
| **`kEndTurnAction` ≠ `size − 1`** | v1.0.0 precedent (CLAUDE.md): the option-slot channel sits after the combat block. Re-deriving offsets instead of reading published constants broke the TUI and 13 tests. |
