# How piles and cards are stored: StS1, sts_lightspeed, and this engine

> **Research note, not a decision.** Written 2026-10-04 by a research agent
> against commit `c1b578f`, as prior art for a possible redesign of the action
> and observation spaces; reviewed and corrected in the main session. Line
> numbers refer to that commit and drift after it. Nothing here has been
> decided; section (e) lists options.
>
> **Corrected after review:** item 8 of section (d) originally said StS writes
> Corruption's 0 cost onto hand Skills only. `actions/common/ApplyPowerAction.java:43-60`
> zeroes the cost of every Skill in all four piles when Corruption is applied;
> item 8 is rewritten accordingly.
>
> **Status since writing:** items 1, 2 and the first half of 6 in section (d)
> were resolved by `v2-spec.md` §6.5 (`347eac8`). Items 3, 4, 5 and the second
> half of 6 are open as tasks #31, #32, #29 and #33.
>
> Decompiled StS1 files were fetched from `aeubanks/sts` and sts_lightspeed
> files from `gamerpuppy/sts_lightspeed` into a session scratchpad that is not
> kept; re-fetch with `gh api repos/<repo>/contents/<path>`. Decompiled paths are
> relative to the package root, as in that repo.

Two facts were verified in the main session and are used here without re-deriving
them: `DrawPileViewScreen.open` shows the real draw-pile card objects, sorted by
rarity/type and then alphabetically unless the player has Frozen Eye; and each card
displays `getCost()` ("X"; "0" if `freeToPlayOnce`; otherwise `costForTurn`), with
`costForTurn` reset to `cost` when the card enters a pile.

This report presents options in section (e). It does not choose.

---

## (a) StS's model

### Piles

`AbstractPlayer` holds six `CardGroup`s and two single-card references
(`characters/AbstractPlayer.java:118-123`, `146-148`):

| field | `CardGroupType` | role |
|---|---|---|
| `masterDeck` | `MASTER_DECK` | the run's deck; persists across fights |
| `drawPile` | `DRAW_PILE` | |
| `hand` | `HAND` | |
| `discardPile` | `DISCARD_PILE` | |
| `exhaustPile` | `EXHAUST_PILE` | |
| `limbo` | `UNSPECIFIED` | cards that are being played but belong to no pile: Double Tap's replay copy (`powers/DoubleTapPower.java:47-57`), `GameActionManager.queueExtraCard` copies (`actions/GameActionManager.java:440-449`) |
| `cardInUse` | (single reference) | the card currently resolving; set in `useCard` after it leaves the hand (`AbstractPlayer.java:1356-1357`) |
| `hoveredCard` | (single reference) | the card under the cursor; `playCard` queues it (`AbstractPlayer.java:1267-1284`) |

`GameActionManager.cardQueue` is an `ArrayList<CardQueueItem>`
(`actions/GameActionManager.java:49`). A `CardQueueItem` holds an `AbstractCard`
reference, the target monster, `energyOnUse`, and flags for autoplay, random target and
end-of-turn autoplay (`cards/CardQueueItem.java:10-17`). The manager plays the head of
the queue by calling `useCard` on that object (`GameActionManager.java:186-272`), and
the cards played this turn and combat are kept as object lists
(`GameActionManager.java:52`, `236-237`).

### `CardGroup`

- Storage: `public ArrayList<AbstractCard> group` and a `CardGroupType type`
  (`cards/CardGroup.java:35`, `46`). Types: `DRAW_PILE, MASTER_DECK, HAND,
  DISCARD_PILE, EXHAUST_PILE, CARD_POOL, UNSPECIFIED` (`CardGroup.java:1160-1168`).
- Top is the end of the list: `addToTop` appends, `addToBottom` inserts at index 0,
  `getTopCard` reads the last element (`CardGroup.java:438-456`), and `draw` takes
  `getTopCard()` (`AbstractPlayer.java:1614-1626`).
- `addToRandomSpot` inserts at `cardRandomRng.random(size - 1)`
  (`CardGroup.java:446-452`). `Random.random(range)` returns `nextInt(range + 1)`,
  i.e. 0..range inclusive (`random/Random.java:49-52`). So a card added to a random
  spot of a non-empty pile lands anywhere except the top.
- Moves: `moveToDiscardPile`, `moveToDeck`, `moveToBottomOfDeck` remove the card and
  hand it to `souls` (the `Soul` animation, which resets attributes on arrival);
  `moveToExhaustPile` fires relic and power `onExhaust`, then `addToTop` on the exhaust
  pile; `moveToHand` appends to the hand (`CardGroup.java:819-896`). `removeCard(c)`
  removes the object and, for the master deck, fires `onRemoveFromMasterDeck` and
  relic `onMasterDeckChange` (`CardGroup.java:157-165`).
- Shuffle: `Collections.shuffle(group, new java.util.Random(rng.randomLong()))`, with
  `shuffleRng` by default (`CardGroup.java:544-550`).
- Sorts: by rarity, rarity then status-last, type, cost, name, lockedness
  (`CardGroup.java:995-1021`, comparators `1076-1158`).
- `getRandomCard(Random rng)` picks by list index (`CardGroup.java:481-483`); the
  rarity- and type-filtered variants first sort the candidates by `cardID`
  (`Collections.sort(tmp)`, `CardGroup.java:492-538`).
- Lookup: `findCardById(String)` returns the first object with that id
  (`CardGroup.java:1059-1065`); `getSpecificCard(c)` is an identity check
  (`CardGroup.java:970-975`). No matches for `uuid` in `CardGroup.java`: lookup by
  uuid is done elsewhere (below).

### `AbstractCard` per-instance state

The class extends only `Object` and implements `Comparable` by `cardID`
(`cards/AbstractCard.java:56-57`, `2568-2569`). No matches for `equals(Object` in the
file, so two objects are equal only if they are the same object.

Per-instance fields relevant to play (`AbstractCard.java:59-121`, `268-270`):

| group | fields |
|---|---|
| identity | `cardID`, `uuid` (`UUID.randomUUID()` in the constructor, `AbstractCard.java:319`) |
| cost | `cost`, `costForTurn`, `isCostModified`, `isCostModifiedForTurn`, `freeToPlayOnce`, `energyOnUse`, `ignoreEnergyOnUse` |
| upgrade | `upgraded`, `timesUpgraded`, `upgradedCost/Damage/Block/MagicNumber` |
| numbers | `baseDamage`/`damage`, `baseBlock`/`block`, `baseMagicNumber`/`magicNumber`, `baseHeal`/`heal`, `baseDraw`/`draw`, `baseDiscard`/`discard`, `multiDamage`, `misc` |
| behaviour flags | `retain`, `selfRetain`, `exhaust`, `isEthereal`, `isInnate`, `purgeOnUse`, `exhaustOnUseOnce`, `returnToHand`, `shuffleBackIntoDrawPile`, `isInAutoplay`, `dontTriggerOnUseCard` |
| bottles | `inBottleFlame`, `inBottleLightning`, `inBottleTornado` |

### Copies and uuid

| method | what it does | uuid |
|---|---|---|
| `makeCopy()` | per-card constructor (`new Anger()`; Searing Blow passes `timesUpgraded`, `cards/red/SearingBlow.java:56-58`) | new |
| `makeStatEquivalentCopy()` | `makeCopy()`, re-applies upgrades, then copies name, target, upgrade state, base numbers, `cost`, `costForTurn`, both modified flags, bottle flags, `misc`, `freeToPlayOnce` (`AbstractCard.java:810-834`) | new |
| `makeSameInstanceOf()` | `makeStatEquivalentCopy()` plus `card.uuid = this.uuid` (`AbstractCard.java:804-808`) | kept |

Uses that matter here: Dual Wield replaces the selected card AND adds its copies with
`makeStatEquivalentCopy` (the selected object is never returned to the hand,
`actions/unique/DualWieldAction.java:69-79`), so all of them get new uuids. Anger's copy
is a `makeStatEquivalentCopy` (`cards/red/Anger.java:34`), copied again by
`MakeTempCardInDiscardAction.makeNewCard` (`actions/common/MakeTempCardInDiscardAction.java:50-55`).
Double Tap's replay is `makeSameInstanceOf` (same uuid), autoplayed and purged
(`DoubleTapPower.java:47-57`).

### Master deck to combat, and back

- `preBattlePrep` clears the action manager and calls
  `drawPile.initializeDeck(masterDeck)` (`AbstractPlayer.java:1546-1570`).
- `initializeDeck` builds `new CardGroup(masterDeck, DRAW_PILE)`, which makes a
  `makeSameInstanceOf()` of every master card (`CardGroup.java:55-60`), shuffles it
  with `shuffleRng`, then puts Innate and bottled cards on top and queues extra draws
  if they exceed the hand size (`CardGroup.java:911-938`).
- So every combat card is a separate object that shares its master card's uuid.
  Combat changes to the object (Armaments upgrades, cost changes) do not touch the
  master card.
- The link back is by uuid, and only specific actions use it. `RitualDaggerAction`
  loops `masterDeck` for the matching uuid and raises `misc`, then does the same for
  every in-battle copy (`actions/unique/RitualDaggerAction.java:36-47`).
  `IncreaseMiscAction` (Genetic Algorithm) does the same for block
  (`actions/defect/IncreaseMiscAction.java:24-35`). `GetAllInBattleInstances.get(uuid)`
  searches `cardInUse`, draw, discard, exhaust, limbo and hand
  (`helpers/GetAllInBattleInstances.java:12-38`).
- Searing Blow: in combat, Armaments upgrades hand objects only
  (`actions/unique/ArmamentsAction.java:33-80`); no path found that writes a combat
  upgrade back. The master card changes at the campfire: `CampfireSmithEffect` opens
  the grid on `masterDeck.getUpgradableCards()` (a group of the same objects,
  `CardGroup.java:944-951`) and calls `upgrade()` on the selected object
  (`vfx/campfire/CampfireSmithEffect.java:44-58`). Its `timesUpgraded` then reaches
  the next fight through `makeStatEquivalentCopy`.
- Saves store `(cardID, timesUpgraded, misc)` per master card (`cards/CardSave.java`,
  `CardGroup.getCardDeck`, `CardGroup.java:62-68`). The uuid is not saved.

### Select screens

Both screens put object references into `selectedCards`.
`HandCardSelectScreen.selectHoveredCard` removes `hoveredCard` from the hand and adds
that object (`screens/select/HandCardSelectScreen.java:371-402`); a selected card can
be clicked back into the hand before confirming (`HandCardSelectScreen.java:435-443`).
`GridCardSelectScreen` adds `hoveredCard` and tests membership with `contains`
(`screens/select/GridCardSelectScreen.java:139-151`, `250-254`). The calling action
reads `selectedCards` and acts on those objects (Armaments `ArmamentsAction.java:74-80`,
Exhume `ExhumeAction.java:87-98`, Warcry `actions/common/PutOnDeckAction.java:53-56`,
Headbutt `actions/unique/DiscardPileToTopOfDeckAction.java:49-53`, Secret Technique
`actions/unique/SkillFromDeckToHandAction.java:64-76`, smith
`CampfireSmithEffect.java:44-53`).

---

## (b) sts_lightspeed's model

- **Card instance:** `CardInstance` is an 8-byte POD: `CardId id`, `int16 uniqueId`,
  `int16 specialData`, `int8 cost`, `int8 costForTurn`, and bools `upgraded`,
  `freeToPlayOnce`, `retain` (`include/combat/CardInstance.h:27-36`).
- **Piles:** `CardManager` holds the hand as a fixed `std::array<CardInstance, 10>`
  with a `cardsInHand` count, a 10-slot `limbo` used only for retained cards at end of
  turn, and draw/discard/exhaust as `std::vector` (or `fixed_list<…, 64>` under a
  build flag) (`include/combat/CardManager.h:24-42`). It also maintains running counts
  (Strikes, Normality, Pain, Blood cards per pile) so queries do not rescan
  (`CardManager.h:43-48`).
- **Identity:** at combat start, a master-deck card's `uniqueId` is its deck index;
  cards created in combat take `nextUniqueCardId++`, which starts at the deck size
  (`src/combat/CardManager.cpp:18`, `71-74`, `97`, `104`, `119`). `uniqueId` is used
  for removal by id and for Ritual Dagger-style `findAndUpgradeSpecialData`
  (`CardManager.h:61`, `96`; `CardManager.cpp:152-154`, `491-528`).
- **Order and RNG:** the opening shuffle is `java::Collections::shuffle` seeded with
  `java::Random(shuffleRng.randomLong())`, a port of the game's algorithm
  (`CardManager.cpp:27`). Shuffling a card into the draw pile uses
  `cardRandomRng.random(size - 1)`, as StS does (`CardManager.cpp:215-222`). Top of
  the draw pile is the back of the vector (`CardManager.cpp:140-144`, `202-213`).
- **Master deck:** `Deck` is a `fixed_list<Card, 96>` with `bottleIdxs` holding the
  deck indices of bottled cards (`include/game/Deck.h:25-31`); a master `Card` is
  `{CardId id, int16 misc, bool upgraded}` (`include/game/Card.h:13-20`). Out-of-combat
  select screens list `SelectScreenCard {Card, deckIdx}`
  (`include/game/GameContext.h:71-88`).
- **Card queue:** `CardQueueItem` holds a COPY of the `CardInstance`, with a source
  comment saying this is probably incorrect in some scenarios
  (`include/combat/CardQueue.h:15-29`).
- **Actions:** a play is `(hand index, target)`; a single select is an index into the
  hand, discard, exhaust or draw pile depending on the task; a multi-select is a 10-bit
  mask of hand indices (`src/sim/search/Action.cpp:37-64`, `432-435`, `477-536`).
  Purity-style multi-select is stubbed to "select none" in search
  (`Action.cpp:538-542`).
- **Search state:** `BattleScumSearcher2` keeps the root `BattleContext` and plays out
  copies of it (`include/sim/search/BattleScumSearcher2.h:21-48`). `BattleContext`
  holds six RNG streams, the `CardManager` by value, a 50-slot action queue and the
  card queue (`include/combat/BattleContext.h:61-92`), so the whole state is copied
  per playout. Play actions on a hand card equal to the previous hand card in
  `(id, upgrade count, costForTurn, cost, freeToPlayOnce, specialData)` are pruned
  (`src/sim/search/BattleScumSearcher2.cpp:218-234`).

---

## (c) This engine, and every place it differs

### Storage

- Piles are four `std::vector<Card>` on `CombatState`: `current_hand`, `discard_pile`,
  `draw_pile`, `exhaust_pile` (`src/combat_state.h:103-106`). There is no limbo pile,
  no `cardInUse` field and no card queue (fields read in full, `combat_state.h:100-181`).
- Top of the draw pile is the back: `draw_one` takes `back()`
  (`src/action.cc:225-226`); the bottom is the front (Forethought inserts at `begin()`,
  `action.cc:1992`). Discard and exhaust append (`action.cc:156-164`). These match
  StS's conventions.
- `clone()` is a value copy; the vectors are deep-copied (heap allocation per pile),
  measured at 0.89 µs per clone (`combat_state.h:130-133`).

### Card identity

| | StS | lightspeed | ours |
|---|---|---|---|
| type | `cardID` string + `upgraded`/`timesUpgraded` per object | `CardId` + `upgraded` | `CardId`, with upgrades and rung growth as separate ids (`observation-space.md` §5 A2) |
| instance | object identity; `uuid` shared with the master card | `uniqueId` (deck index or counter) | none in combat. `Card::uid` exists (`card.h:548`) but is minted only in `RunState::add_card` (`run_state.cc:28`) and dropped on every pile move through an action (below) |
| "this copy" in code | object reference | hand/pile index, or `uniqueId` | value match with `same_as`: `take_from_pile` removes the first copy equal in `play_key()` = id, upgrades, bonus and the three cost layers (`action.cc:1520-1531`, `card.h:589-593`) |

### Per-copy state

Ours (`card.h:531-612`): `card_id`, `bonus_damage` and `upgrades` (overflow above the
rung caps only), `uid`, `combat_cost`/`turn_cost`/`free_once` (StS `cost`,
`costForTurn`, `freeToPlayOnce`), `bottle`. Everything else StS keeps per object is
per-id in our `CardData` (innate, ethereal, exhaust, damage numbers), which is correct
for the current pool because every upgrade is an id swap. `misc` (Ritual Dagger,
Genetic Algorithm) and `retain` have no counterpart; neither card is in the pool.

The queue carries a card as loose fields. `Action::carry` and `as_card` copy `card_id`,
`bonus_damage`, `upgrades` and the three cost layers, and nothing else
(`src/action_types.h:194-219`). `uid` and `bottle` are therefore reset to their
defaults every time a card passes through an `ExhaustCard`, `DiscardCard`,
`AddCardToPile`, `PlaceOnBottomOfDraw` or `ApplyChoice` action
(`action.cc:1816-1837`, `2156-2159`, `1989`, `2337`). The only match for `.bottle`
in combat code is the opening-hand pull (`turn_loop.cc:1017`), which runs before any
such move; the other matches are in `run_state.cc` (171, 198, 262). So the loss of
`bottle` has no effect; for `uid`, the only matches for `.uid`/`uid =` in `src/` are
`card.h:548` and `run_state.cc:28`.

### Deck to combat, and back

- `RunState::begin_combat` copies `master_deck` by value into the fight
  (`run_state.cc:1345`); `start_combat` shuffles it with `std::shuffle` on the combat
  `std::mt19937` (`turn_loop.cc:1098-1099`). StS uses its own shuffle on `shuffleRng`.
- Opening hand: Innate and bottled cards are pulled into the hand first, then the rest
  is drawn up to the hand size (`turn_loop.cc:1014-1029`). StS puts them on top and
  draws, with extra draws for overflow; the result is the same set of cards.
- `end_combat` discards every pile and writes back HP, Max HP, relics, potions and gold
  only (`run_state.cc:1361-1404`). No card change is written back, and the comment
  there notes that the uid path has no callers.
- Run-layer selection (remove, smith, Toke) is by master-deck index
  (`run_state.h:482`, `554`, `589`, `598`, `604`). A bottled card cannot be removed
  (`run_state.cc:259-262`) but can be smithed, as in StS.

### Limbo and the card in use

- Ours: the played card is a local `Card played` in `handle_play_card`, taken out of
  the hand before its effects are queued and returned to a pile by a queued
  `DiscardCard`/`ExhaustCard` (`turn_loop.cc:134-163`). Double Tap's replay uses a
  `PlayContext` with `enters_pile = false` (`turn_loop.h:258-265`); Havoc and Mayhem
  play the top of the draw pile in flight.
- Effects that scan piles therefore never see the card in flight, as in StS, where the
  played object sits in `cardInUse` or `limbo`, not in a pile. Perfected Strike adds 1
  for itself (`query.cc:244-247`); Blood for Blood's per-hit reduction covers hand,
  discard and draw only (`action.cc:171-181`), as StS's `updateCardsOnDamage` does
  (`AbstractPlayer.java:1500-1512`).
- At a decision point the queue is empty, so nothing is in flight, except during a
  paused choice. Then the source card is in flight and is shown in the choice header as
  `source_card` (`combat_env.cc:380`).

### Observation and actions (at `c1b578f`)

- Obs piles: four per-`CardId` count planes (hand, draw, discard, exhaust) plus five
  hand-only cost planes, "hand copies of type T at effective cost k"
  (`combat_env.cc:337-357`). Choice block: a 5-float header and 270 positional slots of
  (occupied, raw CardId, instance damage) (`combat_env.cc:367-394`). Derived
  `kObsSize` = 3,529.
- Actions: flat `Discrete(2,135)`; the Combat block is `CardId × target` (1,350) and
  CardSelect is `CardId` (270) (`turn_loop.h:150-210`). Since `347eac8` both blocks
  carry a cost dimension: 8,615 actions (`v2-spec.md` §6.5).

---

## (d) Mismatches that cause, or could cause, parity bugs or observation gaps

Ordered by how directly they affected play at `c1b578f`.

1. **A specific copy could not be chosen in a choice.** Two copies that differ only in
   cost were two options, but the action named a `CardId` and resolved to the first
   (`turn_loop.cc:1383-1387`). StS selects objects (section a). Full analysis in
   [`choice-copy-options.md`](choice-copy-options.md). *Resolved by `v2-spec.md` §6.5
   (`347eac8`).*
2. **A specific copy could not be chosen in a play** (D2): the cheapest copy was played
   (`turn_loop.cc:141-158`) and charged (`turn_loop.cc:115`, `query.cc:80-95`). StS
   plays `hoveredCard` and charges its `costForTurn` (`AbstractPlayer.java:1267-1284`,
   `1360-1362`). *Resolved by §6.5; D2's cheapest-copy rule is retired.*
3. **Shuffling a card into the draw pile can put it on top in ours; never in StS.**
   Ours inserts at a uniform position in [0, size] (`action.cc:2075-2078` for
   generated cards, `action.cc:2171-2174` for `AddCardToPile`), and index `size` is the
   top. StS's `addToRandomSpot` inserts at [0, size-1] (`CardGroup.java:446-452`,
   `Random.java:49-52`), so a shuffled-in card is never the next draw when the pile is
   non-empty. Call chain checked for Wild Strike and Chrysalis:
   `MakeTempCardInDrawPileAction(…, randomSpot = true, …)` →
   `ShowCardAndAddToDrawPileEffect` → `drawPile.addToRandomSpot`
   (`cards/red/WildStrike.java:33`, `cards/colorless/Chrysalis.java:34`,
   `actions/common/MakeTempCardInDrawPileAction.java:44-63`,
   `vfx/cardManip/ShowCardAndAddToDrawPileEffect.java:24-49`). lightspeed matches StS
   (`CardManager.cpp:215-222`). Affects Wild Strike's Wound, Reckless Charge's Dazed,
   Chrysalis and Metamorphosis (`card.h:1430-1440`, `1978-2005`). Ours also draws this
   position from the combat stream `state.rng`, where StS uses `cardRandomRng`; that
   breaks the stream separation `v2-spec.md` §3.5 asks for. *Parity bug; task #31.*
   *Resolved 2026-10-18:* both sites go through `add_to_random_spot` in `action.cc`:
   [0, size-1] on `card_rng`, and no roll for an empty pile. Reckless Charge and Mark
   of Pain were checked too; both are `MakeTempCardInDrawPileAction(…, true, true)`.
   The insert happens in the effect's constructor, so the card is in the pile as soon
   as the action runs. One more difference turned up and is fixed: Chrysalis and
   Metamorphosis pick every card in `use()` before any position is rolled, and the
   engine had alternated the two. Tests: `GeneratedCards.*` in `test_choice.cc`.
4. **Anger's copy does not inherit the played copy's cost.** Ours carries only
   `bonus_damage` and `upgrades` (`turn_loop.cc:416-420`). StS's copy comes from
   `makeStatEquivalentCopy`, which copies `cost`, `costForTurn` and `freeToPlayOnce`
   (`Anger.java:34`, `MakeTempCardInDiscardAction.java:50-55`,
   `AbstractCard.java:822-832`). Anger prints 0, so only Snecko's Confusion can give it
   another cost (`action.cc:231-246`). The copy's inherited cost is visible to a human
   in the discard pile, and changes play when the copy reaches the hand without a
   Confusion reroll: after Orange Pellets removes Confusion (`relic-effects.md`
   §6.28), or when Secret Weapon pulls it from the draw pile. `colorless-effects.md`
   batch 3 states that StS's Anger copy "does not inherit a discount"; the decompiled
   code says it does. For Double Tap the claim's outcome is right for a different
   reason: the replay keeps the cost but is autoplayed, so no energy is paid.
   *Parity bug, narrow; task #32.*
5. **Per-copy cost is not in the observation for draw, discard or exhaust.** The cost
   planes are read off the hand only (`combat_env.cc:345-357`). A human sees each card's
   `getCost()` in every pile view, including the draw pile (sorted, so its order stays
   hidden). Combat-layer costs survive in those piles (Snecko rolls, Madness,
   Chrysalis/Metamorphosis, Blood for Blood), and `free_once` too (Forethought puts its
   card in the draw pile). Since `c027bc2` the TUI's `StatePiles` carries per-copy
   costs for every pile, the draw pile as a sorted list per CardId
   (`combat_env.h:28-45`), so a human using the TUI sees them; the observation does
   not. *Observation gap under `observation-space.md` §1; task #29.*
6. **Choice slots carried no cost, and staged picks are not in the observation.** Slots
   held (occupied, CardId, damage) (`combat_env.cc:383-393`); `combat_env.h:137` said
   the third float was a cost, which was wrong. The staged set that
   `colorless-effects.md` D1 requires to be visible is not written anywhere in
   `compute_obs` (`combat_env.cc:211-395`, read in full). *Slot cost added in
   `347eac8`; staged picks open as task #33.*
7. **`uid` and `bottle` are dropped on every pile move through an action**
   (`action_types.h:194-219`). No code reads either after the opening hand, so there
   is no live bug. It blocks any design that relies on stable per-copy identity in
   combat (uid write-back, handle-indexed actions, tests that track one copy). If uuid
   semantics were ever needed, note that StS gives Dual Wield's copies and the selected
   original new uuids (`DualWieldAction.java:69-79`), while Double Tap's replay keeps
   the uuid (`DoubleTapPower.java:47`). *Latent.*
8. **Corruption's cost is a query in ours and per-card writes in StS.**
   `instance_effective_cost` returns 0 for every Skill under Corruption
   (`query.cc:71-73`). StS writes it: when Corruption is applied,
   `ApplyPowerAction` calls `modifyCostForCombat(-9)` on every Skill in hand, draw,
   discard and exhaust (`actions/common/ApplyPowerAction.java:43-60`), so all of them
   show 0; Skills that arrive later get `costForTurn` 0 when drawn or exhumed
   (`AbstractPlayer.java:1330-1335`, `ExhumeAction.java:53-55`, `90-92`). The two
   models differ only for a Skill created into a pile after Corruption was applied: StS
   shows its printed cost there until it is drawn, ours reports 0. Play is unaffected,
   since a card is played from the hand where both agree. It matters only if per-pile
   cost planes are added (item 5). *Observation detail, conditional.*
9. **Shuffle algorithm and stream.** Ours: `std::shuffle` on `std::mt19937`
   (`action.cc:206`, `turn_loop.cc:1099`). StS: `Collections.shuffle` with a
   `java.util.Random` seeded from `shuffleRng` (`CardGroup.java:544-550`). The
   distribution is the same. The differences are cross-platform determinism
   (`std::shuffle` is not specified to agree between standard libraries, per
   CLAUDE.md) and seed compatibility with StS and lightspeed, which ports the Java
   shuffle (`CardManager.cpp:27`). *Not a parity bug; a reproducibility issue.*
10. **RNG draws spent on display order.** Secret Technique and Secret Weapon build
    their grid with `addToRandomSpot`, which advances `cardRandomRng` once per
    eligible card after the first (`SkillFromDeckToHandAction.java:30-34`). Ours builds
    a deduplicated, sorted list and draws nothing. The offer is the same; only later
    `cardRandomRng` values shift. This matches the project's Discovery ruling (fixed
    draw counts). *Accepted kind of divergence.*

Checked and found consistent: draw/discard/exhaust top-of-pile conventions; Forethought
to the bottom; the played card belonging to no pile during resolution; Blood for
Blood's per-hit reduction skipping exhaust and the in-flight card; Innate and bottled
cards in the opening hand.

---

## (e) What an action/observation redesign would need from the engine

Each option says what the engine must provide, what the interface gains, and what it
costs. They can be combined where noted.

### E1 — Address equivalence classes of visible state (no handles)

- **Engine provides:** a `visible_signature(card)` = (CardId, displayed cost),
  computed the way StS displays it (item 8); per-pile counts by signature for the obs;
  `take_by_signature(pile, sig)` with a documented tie-break for copies that share a
  signature but differ in a hidden way.
- **Interface:** actions name (CardId, cost), as B3 does (`v2-spec.md` §6.5); the obs
  gains per-pile (CardId, cost) planes.
- **Pros:** piles stay plain value vectors; small change; matches the count-plane obs
  and stable index semantics.
- **Cons:** the tie-break is a rule (rare, but a rule). It cannot express StS's
  object-level effects (uuid write-back), which nothing in the Act 1 pool needs.
- **Status:** the action half is what §6.5 implemented (`cost_column`, `taken_before`).
  The per-pile obs half is task #29.

### E2 — Stable per-copy handles inside the engine, signatures at the interface

- **Engine provides:** every combat card gets a handle: master cards keep their `uid`;
  cards made in combat get one from a combat counter (lightspeed's scheme). `carry` /
  `as_card` carry it (and `bottle`). Copy semantics follow StS:
  `makeStatEquivalentCopy`-style copies (Dual Wield, Anger) get new handles; a
  `makeSameInstanceOf`-style copy (Double Tap's replay) keeps it. `take_from_pile`
  can match by handle.
- **Interface:** unchanged from E1.
- **Pros:** engine correctness for anything that must track one copy (uid write-back,
  tests, the TUI); fixes item 7. `Action` stays POD (one more int).
- **Cons:** bookkeeping at every creation site, with no interface benefit by itself.

### E3 — Handles at the interface (entity-indexed by handle)

- **Engine provides:** E2, plus a fixed capacity H for handles in one fight and a
  per-handle state readout.
- **Interface:** CardSelect = H actions, Combat = H × 5. The obs becomes an entity
  list: per handle, its id (one-hot or an embedding index), location, cost, flags.
  At H = 128 and one-hot ids that is roughly 36,000 floats.
- **Pros:** the StS model exactly: any copy, any pile, and the run layer (bottled
  copies, Searing Blow overflow) with one mechanism.
- **Cons:** `v2-spec.md` §3.2 rules that `uid` must not enter the obs (no false
  ordinal); a handle index has the same problem unless handles are relabelled each
  step, which makes them positional again. Exceeding H is truncation
  (`decision-points.md` §5.1). It assumes an entity-aware policy, which the project
  has said is outside the environment's remit (`observation-space.md` §1).

### E4 — Positional addressing: (pile, position)

- **Engine provides:** a canonical in-pile order for every pile a human can view:
  hand in arrival order (as now), discard and exhaust in pile order (as StS's grid),
  draw pile in a sorted order that hides the real order (as `DrawPileViewScreen`).
- **Interface:** lightspeed's and CommunicationMod's scheme: play = (hand slot,
  target), select = (slot). Obs: per-slot features.
- **Pros:** exact; small action space; simple engine.
- **Cons:** index meaning changes with the state, which `v2-spec.md` §6 forbids;
  decapitate-the-spire reported hand-slot actions as hard to learn
  (`observation-space.md` §4.1).

### E5 — Fixed-capacity pile storage (orthogonal; performance)

- **Engine provides:** hand as `std::array<Card, 10>` plus a count (lightspeed's
  layout); other piles as fixed lists with a capacity bound.
- **Pros:** `clone()` without heap allocation, which matters for MCTS.
- **Cons:** a capacity bound for draw/discard/exhaust (a run's deck can grow;
  lightspeed uses 96 for the deck and 64 per combat pile). Independent of which
  interface is chosen.

### Engine fixes needed under every option

- Items 3 and 4 in section (d) are parity bugs independent of the interface (tasks
  #31, #32).
- If any per-pile cost appears in the obs, the engine needs a "displayed cost" query
  that matches StS per pile, including the Corruption case (item 8).
- The obs needs the staged multi-select set (item 6) whatever the action scheme
  (task #33).
