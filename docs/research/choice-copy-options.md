# Choosing a specific copy: options for card selection (and card plays)

> **Research note, not a decision.** Written 2026-10-04 by a research agent
> against commit `c1b578f`, reviewed and corrected in the main session. Line
> numbers refer to that commit and drift after it. The decision it informed is
> `docs/design/v2-spec.md` §6.5 (B3, implemented in `347eac8`); §12 below records
> the outcome. RL prior art for the options is in
> [`rl-action-representation.md`](rl-action-representation.md).
>
> **Corrected after review:** the original said StS writes Corruption's 0 cost
> onto hand Skills only. `actions/common/ApplyPowerAction.java:43-60` zeroes the
> cost of every Skill in all four piles when Corruption is applied (§6 B3 and
> §8 Q3 are fixed in place).
>
> Decompiled StS1 files were fetched from `aeubanks/sts` and sts_lightspeed
> files from `gamerpuppy/sts_lightspeed` into a session scratchpad that is not
> kept; re-fetch with `gh api repos/<repo>/contents/<path>`. Paths below are
> relative to each repo's root.

This report lays out options. It does not pick one. Section 9 states a lean,
labelled as the agent's.

---

## 1. The problem, stated precisely

1. `build_choice` keeps two copies of one `CardId` as two options when `Card::same_as`
   says they differ (`src/action.cc:2575-2590`). `same_as` compares `play_key()` =
   (`card_id`, `upgrades`, `bonus_damage`, `combat_cost`, `turn_cost`, `free_once`)
   (`src/card.h:589-593`). It does not compare `uid` or `bottle`.
2. The mask lights one action per option, keyed only by `CardId`
   (`src/turn_loop.cc:1257-1264`). Two options with the same id light the same index.
3. `apply_action` resolves a `CardSelect` action to the first option with that id
   (`src/turn_loop.cc:1383-1387`). The second variant cannot be chosen by any action.
4. Which variant is "first" depends on the sort in `build_choice`
   (`src/action.cc:2595-2596`). Since `9414cfc` it orders by the same `play_key()`
   (`Card::sorts_before`, `card.h:599-601`), so the order is total and the same on
   every platform. An unset layer is stored as `kNoCostOverride` = -1
   (`card.h:490`), so it sorts before any explicit value. The reachable copy is the
   one smallest in (`combat_cost`, `turn_cost`, `free_once`) with unset = -1: an
   unmodified copy if one is on offer, then a turn-layer-only copy, then the lowest
   combat-layer value. In practice, when a discounted and an undiscounted copy are
   both offered, the discounted one cannot be chosen.
5. The TUI numbers options by position and answers option `i` with
   `encode_action(CardSelect, options[i].card_id)` (`python/minispire/env.py:149-156`,
   `python/minispire/render/screen.py:551-554`). Picking "(1)" therefore takes
   option 0's copy.
6. The spec's justification for entity indexing was that same-id copies are
   interchangeable (`docs/design/v2-spec.md:1541-1544`; also
   `docs/design/decision-points.md:298` and `docs/design/observation-space.md:17-19`).
   That stopped being true when per-copy cost layers landed
   (`docs/design/colorless-effects.md:121-122` already says a free Strike and a normal
   Strike "stay separate options in a choice"; the action layer was not changed to
   match).

The same root cause already exists for card plays and was ruled on: the engine plays
the cheapest copy (`src/turn_loop.cc:141-158`, `src/query.cc:80-95`; ruling in
`colorless-effects.md` D2 option A; divergence row at `v2-spec.md:1777`).

---

## 2. What can differ between two copies of one `CardId`

| per-copy field | where it can differ | sources |
|---|---|---|
| `combat_cost` | every pile | Snecko Confusion (0-3), Madness, Chrysalis, Metamorphosis, Enlightenment+, Blood for Blood shifts, cost-changing upgrades (`card.h:2273-2319`) |
| `turn_cost` | hand only; reset on entering draw, discard or exhaust (`action.cc:151-164`, `action.cc:1986-1991`, `action.cc:2349-2356`) | Mummified Hand, Enlightenment, Infernal Blade, Discovery, Transmutation |
| `free_once` | every pile; cleared by a Snecko draw (`action.cc:243-245`) | Forethought (`action.cc:1976-1993`) |
| `bonus_damage`, `upgrades` | every pile, above the rung cap only | Rampage past its top rung, Searing Blow past rung 5 (A2 saturation, `observation-space.md` §5 A2) |
| `uid`, `bottle` | not compared by `same_as` | run-layer bookkeeping |

Below the rung caps, cost is the only per-copy state that differs. Upgrades and rung
growth are separate `CardId`s (`observation-space.md` §5 A2, §9).

**Bound on distinct variants of one id in one offer** (needed for any fixed-width
option). Hand: at most `HAND_SIZE_LIMIT` = 10 cards (`turn_loop.h:20`). Draw, discard
and exhaust: every entry path read resets the turn layer (discard and exhaust moves,
Warcry, Forethought, reshuffle from an already-reset discard; generated cards arrive
fresh or with a combat-layer cost), so variants differ only in the combat-layer value
(at most 5 values, 0..`kMaxCardCost` = 4, `card.h:407`) and `free_once` (2 values): at
most 10. So **K = 10 covers every reachable case below the rung caps** with no
truncation. Above the caps, variants alias, which the A2 ruling already accepts.

**Same effective cost, different layers.** Two copies can show the same number and
behave differently later. Examples: a Bash at 0 from Mummified Hand (turn layer) and a
Bash at 0 from Madness (combat layer); a 2-cost card at 1 from Enlightenment (turn)
and the same card rolled to 1 by Snecko (combat); a Forethought'd card (`free_once`)
and a Madness'd copy. In piles other than the hand only the third kind can occur,
because the turn layer has been reset.

**Is the layer visible to a human?** Not on the card face. `AbstractCard.renderEnergy`
draws the same green colour for `isCostModified || isCostModifiedForTurn ||
freeToPlay()` (`cards/AbstractCard.java:1952-1957`), and the number is `getCost()`:
"X", "0" if `freeToPlayOnce`, else `costForTurn` (`AbstractCard.java:2032-2040`). A
human can know the layer only by remembering what caused the discount.

---

## 3. How the real game does it (decompiled StS1)

- **Every selection is of a specific card object.**
  `HandCardSelectScreen.selectHoveredCard` removes `hoveredCard` from the hand and adds
  that object to `selectedCards` (`screens/select/HandCardSelectScreen.java:371-402`).
  `GridCardSelectScreen` adds `this.hoveredCard` to `selectedCards` and checks
  membership with `selectedCards.contains(hoveredCard)`
  (`screens/select/GridCardSelectScreen.java:139-151`). `AbstractCard` extends only
  `Object` and implements `Comparable` by `cardID` (`AbstractCard.java:56-57`,
  `2568-2569`); there are no matches for `equals(Object` in `AbstractCard.java`, so
  `contains` is identity. Each copy is its own clickable object; any copy can be chosen.
- **Playing a card is also per object.** `AbstractPlayer.playCard` queues
  `new CardQueueItem(this.hoveredCard, …)` (`characters/AbstractPlayer.java:1267-1284`)
  and `useCard` pays `c.costForTurn` of that object (`AbstractPlayer.java:1360-1362`).
  So D2's cheapest-copy rule is a divergence for plays, as `v2-spec.md:1777` records.
- **The actions read the screen's object references.** Armaments upgrades each object
  in `selectedCards` (`actions/unique/ArmamentsAction.java:74-80`); Exhume moves the
  selected objects (`actions/unique/ExhumeAction.java:87-98`); Warcry's
  `PutOnDeckAction` moves them (`actions/common/PutOnDeckAction.java:53-56`); Headbutt's
  `DiscardPileToTopOfDeckAction` (`actions/unique/DiscardPileToTopOfDeckAction.java:49-53`);
  Secret Technique (`actions/unique/SkillFromDeckToHandAction.java:64-76`); Dual Wield
  makes `makeStatEquivalentCopy()` copies of the selected object
  (`actions/unique/DualWieldAction.java:69-75`), and `makeStatEquivalentCopy` copies
  `cost`, `costForTurn` and `freeToPlayOnce` (`AbstractCard.java:810-834`). So the copy
  you Dual Wield decides the copies' costs.
- **When the game auto-picks.** Only when the count of eligible CARDS is 1:
  Armaments (`ArmamentsAction.java:51-60`), Dual Wield (`DualWieldAction.java:45-53`),
  Exhume (`ExhumeAction.java:45-63`), Headbutt (`DiscardPileToTopOfDeckAction.java:38-42`),
  Secret Technique (`SkillFromDeckToHandAction.java:39-58`). Warcry moves the whole hand
  when hand size ≤ amount (`PutOnDeckAction.java:42-50`). Two identical copies still
  open the screen in StS; our engine collapses them and auto-resolves
  (`action.cc:2315-2327`). The outcome is the same because the copies are identical.
- **How many at once.** `HandCardSelectScreen` takes `numCardsToSelect` with
  `anyNumber`/`canPickZero`/`upTo` flags; a selected card can be clicked back into the
  hand before confirming (`HandCardSelectScreen.java:435-443`). The grid takes
  `numCards` and also allows deselecting (`GridCardSelectScreen.java:250-254`).
- **Grid order.** Exhume and Headbutt open the grid on the pile itself, in pile order
  (`ExhumeAction.java:83`, `DiscardPileToTopOfDeckAction.java:44`). Secret Technique
  builds a temporary group with `addToRandomSpot`, so its grid is in a random order
  (`SkillFromDeckToHandAction.java:30-34`).

The wiki was not consulted for these UI facts; the decompiled game is the stronger
source, and each action named above was read in full along with the screen it opens.

---

## 4. Prior art

- **sts_lightspeed.** Search actions are positional. A play is `(hand index, target)`
  (`src/sim/search/Action.cpp:432-435`); a single select is an index into the hand,
  discard, exhaust or draw pile depending on the task (`Action.cpp:477-536`); a
  multi-select is a 10-bit mask of hand indices (`Action.cpp:52-64`), and Purity-style
  selection is stubbed to "select none" in search (`Action.cpp:538-542`). Card
  instances carry `cost`, `costForTurn`, `freeToPlayOnce`, and a `uniqueId`
  (`include/combat/CardInstance.h:27-36`). Its MCTS (`BattleScumSearcher2`) prunes
  play actions on a hand card equal to the previous hand card in `(id, upgrade count,
  costForTurn, cost, freeToPlayOnce, specialData)`
  (`src/sim/search/BattleScumSearcher2.cpp:218-234`). So the closest prior art treats
  copies with different cost OR different layer as distinct actions, and copies equal
  in all of them as one.
- **CommunicationMod** (ForgottenArbiter, README lines 41-51): `PLAY CardIndex
  [TargetIndex]` with a 1-based hand position, and `CHOOSE ChoiceIndex|ChoiceName`.
  The name form "selects the first choice index with the matching name", which is the
  same first-match problem as ours; the index form reaches every copy. The state JSON
  lists each card with its `uuid` and `cost`. Agents built on it (bottled_ai and
  others) inherit positional addressing.
- **Other card-game environments** (from their published descriptions; not
  re-verified). Hanabi (Hanabi Learning Environment, OpenSpiel) addresses cards by
  hand position, because the holder cannot see them. gym-locm (Legends of Code and
  Magic) indexes hand and board slots. Hearthstone frameworks (SabberStone, MetaStone)
  enumerate the legal options each step as a list; learned agents score each option
  from entity features. AlphaStar and OpenAI Five select units with attention over an
  entity list (a pointer-network head). DouZero and RLCard's Uno/Mahjong index actions
  by card content, which works because those cards have no per-instance state. The
  project's own record of decapitate-the-spire's failed hand-slot actions is in
  `observation-space.md` §4.1.

---

## 5. What the observation showed (at `c1b578f`, before §6.5)

Derived from the constants: `kObsSize` = 48 (player) + 235 (5 enemies × 47) + 2,430
(9 planes × 270) + 1 (turn) + 815 (choice block) = **3,529**. Actions: **2,135**
(`turn_loop.h:164`).

| what | encoding | per-copy cost visible? |
|---|---|---|
| hand | count plane + 5 cost planes: copies of type T at effective cost 0..4 (`combat_env.cc:345-357`) | yes, as (id, effective cost) counts; layer not shown |
| draw, discard, exhaust | count per type only (`combat_env.cc:341-344`) | **no** |
| choice header | pending, kind, source pile, source card, optional (`combat_env.cc:374-381`) | — |
| choice slots | positional, in canonical sort order: occupied, payload_id (raw CardId), payload_value = instance damage (`combat_env.cc:383-393`) | **no cost float** |
| staged multi-select picks | not written (all of `compute_obs`, `combat_env.cc:211-395`, read) | — |

Consequences:
- For hand choices (Armaments, Warcry, Dual Wield, Burning Pact, Forethought, Purity)
  the agent can infer the variants' costs from the hand cost planes.
- For pile choices (Headbutt, Exhume, Secret Technique, Secret Weapon) the agent sees
  two identical slots (same id, same damage) and nothing distinguishes them. A human
  sees each card's cost in the grid. This is an observation aliasing defect under
  `observation-space.md` §1 regardless of which option is chosen below.
- `combat_env.h:137` documents the slot as "occupied, payload_id, cost"; the code
  writes damage. The comment is wrong.
- Since `c027bc2` the TUI shows every copy's own cost in the hand, the choice panel and
  every pile, including the draw pile (`combat_env.h:28-45`, `ChoiceView::option_costs`
  at `combat_env.h:64-66`). So a human playing through the TUI sees what the agent
  cannot.
- `colorless-effects.md` D1 requires the staged set to be visible; it is not.

---

## 6. The options

Every option below keeps the flat `Discrete` + mask shape (R8) unless it says
otherwise. "Plays" means whether the same scheme can replace D2 for the Combat block.

### Status quo — first match wins (reference)

- **Size:** CardSelect 270; total 2,135.
- **Behaviour:** the variant that sorts first is always taken; the others are
  unreachable. Since `9414cfc` this is deterministic: the unmodified copy wins when
  present (section 1, item 4). So it is option A with a rule nobody chose. For
  example, Exhume can never return a Madness'd 0-cost copy while a printed copy of the
  same card is also in the exhaust pile.
- **Obs:** no change; pile choices stay aliased.
- **Parity:** lost whenever copies differ, which Snecko makes routine.
- **Plays:** D2 stays.

### A — collapse by CardId, pick the copy by a per-ChoiceKind rule (reference)

- **Size:** 2,135, no change.
- **Rule examples:** Exhume takes the cheapest; Warcry takes the copy whose discount
  survives the move. `observation-space.md` §5 A1 already states the objection: "no
  canonical rule is strategy-free". The Warcry case under B3 below shows a rule that is
  wrong in some states.
- **Parity:** lost by construction. Rob said he does not want this.

### B1 — two-phase: pick the CardId, then (only if its variants differ) pick the variant

- **Mechanism:** phase 1 is the CardSelect action as it was. If the chosen id has one
  variant on offer, it resolves at once. If it has two or more, the engine opens a
  sub-choice listing only that id's variants. The sub-choice needs its own small block,
  and B1 does not avoid choosing how that block is addressed:
  - B1-rank: the sub-choice is "variant 0..K-1" in canonical order (K = 10). Exact.
  - B1-cost: the sub-choice is "cost 0..4". Same residual collision as B3.
- **Size:** +10 actions (rank) or +5 (cost): total 2,145 or 2,140. The sub-block is
  shared by choices and plays.
- **Mask:** phase 1 lights every offered id; phase 2 lights that id's variants.
- **Obs:** header gains a phase flag and the selected id (+2 floats). Choice slots need
  a cost float (+270) so the variants are distinguishable when listed. Without pile
  cost information the phase-1 decision for pile choices is still made blind to the
  variants (the same objection `observation-space.md` C4 raised against a "which copy?"
  prompt); the slot cost float fixes that because phase 1 lists every variant.
- **Multi-select:** each pick may take two steps; staging and the rebuild
  (`action.cc:2659-2682`) work unchanged, applied after phase 2.
- **POD/clone:** `PendingChoice` gains `phase` and `selected_id`; fixed-size, so
  `clone()` stays a copy.
- **Determinism:** B1-rank depends on the total comparator, which becomes part of the
  interface again (only within one id).
- **Learning:** phase 1 keeps entity semantics. Phase 2 is rare except under Snecko,
  so it gets few samples; credit for the pair is split across two steps. An
  autoregressive policy head would merge the two steps, but that is a policy-side
  change outside the environment.
- **Plays:** yes. "Play Bash at enemy 2" then, if Bash copies differ, "which Bash".
  This needs a new pause for plays: energy, X and the copy's removal from hand
  (`turn_loop.cc:109-163`) must wait until the copy is chosen. Under Snecko (7-card
  hands, starter-deck duplicates rolled 0-3) this extra step is common, not rare.
- **Parity:** full with B1-rank; B1-cost keeps the same-cost/different-layer residual.
- **Cost:** `card.h` (`PendingChoice`), `action.cc` (sub-choice build/resolve),
  `turn_loop.cc` (mask phase, apply, play pause), `combat_env` (header, slot cost),
  bindings, `env.py`, TUI second prompt. Roughly 300-500 lines plus tests. The play
  pause is the larger half.

### B2 — action indexed by (CardId, variant rank), fixed K variants per id

- **Mechanism:** rank = position of the variant among the same-id options in the
  canonical order. If the canonical order puts effective cost first, rank 0 is "the
  cheapest copy", which is what D2 did.
- **Size (K = 10):** CardSelect 270 × 10 = 2,700 → total **4,565**. With plays:
  Combat 270 × 10 × 5 = 13,500 → total **16,715**.
- **Mask:** light (id, rank) for each option.
- **Obs:** the action is laid out per (id, rank) but the choice block is positional per
  option. Either the obs is laid out the same way (270 × 10 × F floats; F = 2 gives
  5,400, live only during a choice) or each positional slot gains a rank float and a
  cost float (+540) and the agent learns the mapping.
- **Multi-select:** ranks shift after each staged pick, because the offer is rebuilt.
- **Determinism:** depends on the total comparator; its key becomes interface.
- **Learning:** (id, 0) has a stable meaning if the order is cost-first; ranks 1+ are
  rare and their meaning ("the second-cheapest Bash") changes with the state. This is
  rank indexing scoped to one id, the thing §6.2 removed, in a smaller form.
- **Plays:** yes; retires D2.
- **Parity:** full below the rung caps (K = 10 is exact; section 2).
- **Cost:** `turn_loop.h` (block widths, a variant dimension in
  `encode_action`/`decode_action` and `DecodedAction`), `turn_loop.cc`, `action.cc`
  (comparator is interface), `combat_env`, bindings, `env.py`, TUI, the four tests that
  pinned 2,135. Roughly 300-400 lines plus tests; more if the obs is laid out per
  (id, rank).

### B3 — action indexed by (CardId, effective cost)

- **Mechanism:** "pick a Bash that costs 0". Columns 0..4 (`kMaxCardCost` + 1 = 5).
  X-cost and unplayable cards have no layers and no variants; they use a fixed column
  (0), documented.
- **Size:** CardSelect 270 × 5 = 1,350 → total **3,215**. With plays: Combat
  270 × 5 × 5 = 6,750 → total **8,615**. Width is cheap here: the mask walks the
  option list and the hand, never the action space (`turn_loop.cc:1254-1256`,
  `decision-points.md` §3).
- **Mask:** choice: light (id, cost) per option. Plays: light (id, cost, target) per
  hand copy if energy ≥ that copy's cost. This replaces the cheapest-copy affordability
  check (`turn_loop.cc:1224`).
- **Obs alignment:** the hand cost planes are already "(id, effective cost) count"
  (`combat_env.cc:345-357`), the same index as the action. For plays and hand choices
  no new obs is needed. For pile choices: add a cost float per choice slot (+270), or
  per-pile cost planes for draw/discard/exhaust (3 × 5 × 270 = +4,050). The pile planes
  are justified by §1 on their own: a human sees each card's cost in the pile views.
  One detail if pile planes are added: Corruption. When Corruption is applied, StS
  zeroes the cost of every Skill in all four piles (`ApplyPowerAction.java:43-60`,
  `modifyCostForCombat(-9)`); Skills that arrive later get `costForTurn` 0 when drawn
  or exhumed (`AbstractPlayer.java:1330-1335`, `ExhumeAction.java:53-55`). So the only
  difference from our query-layer Corruption (`query.cc:71-73`, 0 for every Skill
  everywhere) is a Skill created into a pile AFTER Corruption was applied: StS shows
  its printed cost there until it is drawn.
- **Residual:** copies with the same effective cost but different layers (section 2)
  share one action. The engine needs a tie-break inside (id, cost):
  - For **plays**, a rule that loses nothing appears to exist (the agent's reasoning,
    later adopted): playing a combat-layer or turn-layer copy leaves the other copy's
    discount in place, and playing the turn-layer copy first is at least as good (the
    kept combat copy survives a later Warcry or Runic Pyramid); a `free_once` copy
    should be played last, because playing it spends the discount and playing a combat
    copy does not.
  - For **choices**, no single rule is right. Warcry with a turn-0 Bash and a combat-0
    Bash: placing the turn copy gives {top of draw: 2, discard later: 0}; placing the
    combat copy gives {top: 0, discard later: 2}. Which is better depends on the plan.
    So B3 for choices is exact for different costs and rule-based for same-cost,
    different-layer copies. That residual is option A in a much smaller form.
  - When the residual collapses two options into one, the single-option auto-resolve
    (`action.cc:2315-2327`) would fire where StS would open the screen.
- **Multi-select:** (id, cost) is stable across the rebuild after each pick.
- **POD/clone:** no new state.
- **Determinism:** depends only on the tie-break rule, not on the comparator.
- **Learning:** index meaning is fixed forever ("Bash at cost 0"), readable by a human,
  and shares the obs's existing (id, cost) structure. The Combat block grows 5×; most
  of it is masked in every state.
- **Plays:** yes; retires D2 except for the same-cost residual above.
- **Parity:** exact for everything visible on the card face; the layer residual is
  not visible on the card (section 2).
- **Cost:** `turn_loop.h` (CardSelect and Combat widths; a cost dimension in
  `encode_action`/`decode_action`/`DecodedAction`), `turn_loop.cc`
  (`valid_actions`, `card_action_is_legal`, `apply_action`, `handle_play_card`'s copy
  selection and energy spend), `query.cc` (`effective_cost` cheapest-copy callers),
  `action.cc` (option lookup by (id, cost); tie-break order), `combat_env` (slot cost
  float; optional pile planes), bindings, `env.py` `choice_action`, TUI hand mapping
  and choice panel, the four 2,135 pins, D2 tests, and docs (`v2-spec.md` §6 table,
  §6.2, the divergence row; `colorless-effects.md` D2). Roughly 250-400 lines of C++
  plus tests.

### B3b — (CardId, effective cost, discount durability)

B3 plus a durability class per column: {printed or combat layer, `free_once`, turn
layer}. Resolves the B3 residual exactly.
- **Size:** 270 × 5 × 3 = 4,050 → total 5,915 (choices only); with plays 20,250 →
  total 24,815.
- **Obs:** hand cost planes become (cost × durability): 15 planes instead of 5
  (+2,700). This puts in the obs something the card face does not show (section 2);
  a human knows it only from memory of what caused it. Whether that passes §1 was an
  open question for Rob (answered no; §12).

### B3+ — B3 with a sub-step only on a same-cost collision

B3, and when one (id, cost) still holds two layer-different variants, open a
three-way sub-choice by durability class. +3 actions (total 3,218 or 8,618). The
sub-step is rare. Same §1 question as B3b for the sub-step's obs.

### B4 — positional option slots (reference; what §6.2 retired)

- **Mechanism:** action = "option slot k" of the offer in canonical order, as in
  v1.0.0. The obs choice block is still laid out this way (`combat_env.cc:383-393`),
  so obs and action would align exactly once a cost float is added.
- **Size:** 270 slots, total 2,135. Plays by hand slot: 10 × 5 = 50 → total 835.
- **Why it was retired:** index k means a different card in each state
  (`v2-spec.md` §6, "index k means the same thing in every state"); `payload_id` is a
  categorical written as a magnitude (ROB-89, `decision-points.md:228-264`); and for
  plays, decapitate-the-spire reported slot actions as hard to learn and abandoned them
  (`observation-space.md` §4.1).
- **Parity:** full. **Plays:** yes, with that negative result.

### B5 — make cost part of card identity (extend the rung ladders)

- **Mechanism:** a Bash at effective cost 0 is a different `CardId` from a Bash at 2.
  Every cost-layer write becomes an id swap, as rung growth is.
- **Size (estimate from the database):** 270 ids, of which 4 are X-cost and 14
  unplayable (11 curses, Dazed, Wound, Burn), leaves 252 numeric-cost ids. Each needs
  costs {0,1,2,3} plus its printed cost: about 3 extra ids each, 4 for Blood for Blood:
  about 757 new ids, `kNumCardTypes` ≈ 1,027. Combat ≈ 5,135; CardSelect ≈ 1,027;
  total ≈ 6,677. Obs: the 5 cost planes are no longer needed, 4 pile planes × 1,027 =
  4,108, choice slots 3 × 1,027: obs ≈ 7,478.
- **Gains:** entity indexing stays exact for cost in every block with no new dimension;
  per-copy cost becomes visible in every pile automatically.
- **Problems:**
  - It has B3's residual unless identity also encodes the layer (5 × 5 × 2 = 50
    combinations per card, not feasible).
  - Rung growth is monotone and rare; cost changes are frequent and reversible (every
    turn-layer discount reverts at end of turn, every Snecko draw rerolls). Every
    writer (`set_combat_cost`, `set_turn_cost`, `shift_combat_cost`,
    `reset_turn_cost`, `upgrade_card_in_place`, `confuse_drawn_card`, Madness,
    Enlightenment, Forethought) and every `card_id ==` test that means "same card"
    (`find_first_in_hand`, `card_qualifies`, upgrade table, generation pools) changes.
  - Learning: the count planes treat Bash@0 and Bash as unrelated entries, so nothing
    learned about one transfers to the other, across about 3.8× the vocabulary.
- **Plays:** yes, as B3. **Cost:** very large.

### B6 — stable per-copy handles

- **Mechanism:** every card instance in a fight carries a stable handle (0..H-1),
  assigned at combat start in deck order and at creation for generated cards. Actions
  name the handle: CardSelect = H, Combat = H × 5. This is StS's own model (object
  references), lightspeed's `uniqueId`, and CommunicationMod's `uuid`.
- **Size (H = 128):** CardSelect 128, Combat 640, total about 1,283. H must bound every
  card that can exist in one fight; exceeding it is truncation, which
  `decision-points.md` §5.1 engineered out.
- **Obs:** the action is meaningless unless the obs describes each handle: per handle
  id, location and cost. As one-hots that is about 128 × 280 ≈ 36,000 floats; as a raw
  id it is the categorical-as-magnitude error. `v2-spec.md` §3.2 rules that `uid`
  must not enter the obs. So B6 implies an entity-list observation and an
  entity-based policy, which is a redesign, not a fix.
- **Engine:** `Card::uid` exists but is dropped on every pile move: `Action::carry`
  and `as_card` do not carry it (`action_types.h:200-219`). No other matches for
  `.uid` or `uid =` in `src/` beyond `card.h:548`, `run_state.h:248` and
  `run_state.cc:28`, so nothing in combat reads it.
- **Parity:** full, and it also covers the run layer (below).
- **Plays:** yes. **Cost:** largest.

---

## 7. Comparison

| option | CardSelect | total (choices only) | total (+ plays) | obs needed | multi-select | parity | plays (D2) | cost |
|---|---:|---:|---:|---|---|---|---|---|
| status quo | 270 | 2,135 | — | none (pile choices aliased) | variant unreachable | lost | D2 stays | 0 |
| A | 270 | 2,135 | — | none | fine | lost by rule | D2 stays | small |
| B1-rank | 270 + 10 | 2,145 | 2,145 | phase, id, slot cost | 2 steps per pick | full | yes, extra step (common under Snecko) | medium |
| B1-cost | 270 + 5 | 2,140 | 2,140 | as B1 | 2 steps | residual | yes | medium |
| B2 (K=10) | 2,700 | 4,565 | 16,715 | rank + cost per slot, or per-(id,rank) layout | ranks shift | full | yes | medium |
| B3 | 1,350 | 3,215 | 8,615 | slot cost (+270); pile planes (+4,050) recommended by §1 anyway | stable | exact except same-cost layer residual | yes, near-exact | medium |
| B3b | 4,050 | 5,915 | 24,815 | 15 hand planes; §1 question | stable | full | yes | medium |
| B3+ | 1,350 + 3 | 3,218 | 8,618 | as B3 + sub-step | stable | full | yes | medium |
| B4 | 270 slots | 2,135 | 835 | slot cost | positions shift | full | yes, with the known negative result | small |
| B5 | ≈1,027 | ≈6,677 (both) | ≈6,677 | none extra; obs ≈ 7,478 | stable | as B3 | yes | very large |
| B6 (H=128) | 128 | ≈1,283 (both) | ≈1,283 | entity list per handle | stable | full + run layer | yes | largest |

---

## 8. Open questions put to Rob

1. **§1 and discount durability.** The card face shows only the number. Is "this 0 lasts
   the turn / the combat / one play" something a human "sees" for §1 purposes? The
   answer decides between B3 (accept a small rule) and B3b/B3+ (expose durability).
2. **Plays in the same change?** Doing the Combat block together with CardSelect
   breaks the interface once instead of twice. `turn_loop.h` notes that renumbering
   after a policy trains is the expensive kind of change.
3. **Per-copy costs in draw, discard and exhaust.** A human sees them in the pile views
   (DrawPileViewScreen shows the real cards; each shows `getCost()`). The obs has none.
   Fix this independently of the option chosen? If so, the pile planes must show a
   Skill created into a pile after Corruption at its printed cost (§6 B3).
4. **Run-layer selection.** v2's CardSelect block will also serve smith, remove,
   transform and Neow over the master deck. There, same-id copies can differ by
   `bottle` (a bottled copy cannot be removed, `run_state.cc:259-262`, but can be
   smithed) and by Searing Blow overflow. B3 does nothing for that; B1-rank, B2, B4 and
   B6 do. Does the chosen option need to cover the run layer too?
5. **Tie-break rules** if B3 is chosen: the play rule in section 6 and one per
   choice kind, each with a test.
6. **Staged picks in the obs.** `colorless-effects.md` D1 requires them; they are not
   written. Part of this change or separate?
7. **Above-cap overflow** (Rampage past +30/+40, Searing Blow past rung 5) produces
   the same unreachable-variant bug in choices. Still accepted under A2?

---

## 9. Lean (the agent's, not a decision)

**B3 for both choices and plays, plus a cost float per choice slot, plus per-pile cost
planes as a separate §1 fix.** Reasons:
- Index meaning is fixed for all states ("Bash at cost 0"), so §6's stability rule
  holds.
- It reuses the obs's existing (id, effective cost) structure for the hand, so plays
  and hand choices need no new obs.
- It retires D2 in the same interface change.
- Multi-select needs no new state and ranks do not shift.
- Widths (3,215 or 8,615) are affordable because masks walk the hand and the option
  list.

Its cost is the same-cost, different-layer residual. If durability counted as visible,
B3+ would remove the residual with a 3-action sub-step that almost never fires. For
zero rules and a small action space, B1-rank is the alternative; its cost is an extra
step on many plays under Snecko. B5 and B6 are larger redesigns than this problem
needs on its own; B6 is the natural fit if the separate pile/card redesign
(`pile-card-storage.md`) moves to per-copy handles anyway.

---

## 10. Side findings (not options)

- `combat_env.h:137` said the choice slot held a cost; `combat_env.cc:391-392` wrote
  instance damage. (Fixed in `347eac8`: the slot now has a fourth float, the cost.)
- The obs does not show staged multi-select picks (D1 requirement). Open: task #33.
- Anger's self-copy does not carry the played copy's cost layers
  (`turn_loop.cc:416-420`); StS copies `cost`, `costForTurn` and `freeToPlayOnce`
  through `makeStatEquivalentCopy` (`cards/red/Anger.java:34`,
  `actions/common/MakeTempCardInDiscardAction.java:50-55`). Matters under Snecko
  (Anger's printed 0 can roll 1-3). Details in `pile-card-storage.md`. Open: task #32.

---

## 11. RL prior art by option

Moved to [`rl-action-representation.md`](rl-action-representation.md), which has the
table, sources, and what the literature suggests.

---

## 12. Outcome (main session, 2026-10-04)

Rob's rulings, recorded in `v2-spec.md` §6.5 and implemented in `347eac8`:

| question | answer |
|---|---|
| Q1 durability visible? | **No.** It is a second-degree fact a human reasons about from memory, so it stays out of the obs. B3b and B3+ are out. With durability invisible, B1 can only show cost in its second step, so B1 and B3 offer the agent the same choices and differ only in factorization (joint vs sequential). |
| Option | **B3**, for **plays and choices** (Q2: yes). 8,615 actions. |
| Q5 tie-break | **Least durable discount first**, for plays and choices alike: turn layer, then printed/combat, then `free_once`; then `play_key` order. Dual Wield is the known case where it is not the best pick; accepted, since the agent cannot see the difference. |
| choice slot cost | **Added** (slot stride 3 → 4). |
| Q3 pile cost planes | Open: task #29. |
| Q4 run layer | Open: noted on task #9 (run env design). B3 does not separate a bottled copy from an unbottled one at the same cost. |
| Q6 staged picks | Open: task #33. |
| Q7 above-cap overflow | Accepted under A2: copies differing only above the rung cap collapse into one (card, cost) option, as they already alias in the obs. |
