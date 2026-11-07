# Events — design (task #7)

**Status:** AGREED (Rob, 2026-10-22): E1–E11 as recommended (§9); W1 from the
wiki cross-check ruled 2026-11-03. Built in §8's order; step 1 done (§10).

The other half of `v2-spec.md` §11 step 7. A `?` node that resolves to an event
today sets `Phase::Event` and stops there (`run_state.cc`, `enter_room`): nothing
picks an event and nothing leaves the phase. This document specifies the 25
events reachable in Act 1, how one is chosen, and the engine pieces they share.

**Sources.** Decompiled StS1 (`aeubanks/sts`): `rooms/EventRoom`,
`rooms/AbstractRoom` (reward block, `addPotionToRewards`),
`dungeons/AbstractDungeon` (`generateEvent`, `getEvent`, `getShrine`,
`initializeSpecialOneTimeEventList`, `nextRoomTransition`),
`dungeons/Exordium` (`initializeEventList`, `initializeShrineList`),
`helpers/EventHelper.roll`, `events/AbstractEvent`, `events/AbstractImageEvent`,
the 25 event classes under `events/exordium` and `events/shrines`,
`screens/CombatRewardScreen.setupItemReward`, `characters/AbstractPlayer`
(`damage`, `isCursed`, `getRandomPotion`), `helpers/PotionHelper.getRandomPotion`,
`helpers/CardHelper.hasCardWithXDamage`, `helpers/MonsterHelper`,
`monsters/exordium/Lagavulin`, `vfx/cardManip/ShowCardAndObtainEffect`, and the
relics `RunicCube`, `SelfFormingClay`, `CentennialPuzzle`, `TungstenRod`,
`MeatOnTheBone`, `RedSkull`, `Torii`, `Boot`, `Sling`, `PreservedInsect`,
`BlackStar`. All numbers below are Ascension 0 (`v2-spec.md` §3.0).

**Wiki cross-check: done 2026-10-22,** all 25 events on slaythespire.wiki.gg.
Results in §5.0.

## 1. Scope

The 25 events of `v2-spec.md` §5.1, unchanged:

| list | events |
|---|---|
| Act 1 events (11) | Big Fish, The Cleric, Dead Adventurer, Golden Idol, Wing Statue (`Golden Wing`), World of Goop, The Ssssserpent (`Liars Game`), Living Wall, Hypnotizing Colored Mushrooms, Scrap Ooze, Shining Light |
| Act 1 shrines (6) | Match and Keep!, Golden Shrine, Transmogrifier, Purifier, Upgrade Shrine, Wheel of Change |
| one-time, reachable in Act 1 (8) | Ominous Forge (`Accursed Blacksmith`), Bonfire Spirits, Face Trader, The Divine Fountain (`Fountain of Cleansing`), Lab, Note For Yourself, We Meet Again, The Woman in Blue |

Neow is an event in StS (`NeowEvent`) but is not in this document. It has its
own spec section (`v2-spec.md` §4.4) and its own mechanics, the blessing pools and
the boss-relic swap. See E1.

## 2. How a `?` becomes an event

### 2.1 The three lists, and what removes from them

Run state holds three lists of event keys, built at the start of the act:

- `eventList`: the 11 Act 1 events (`Exordium.initializeEventList`).
- `shrineList`: the 6 Act 1 shrines.
- `specialOneTimeEventList`: 14 keys at Ascension 0. Six of them can never pass
  their gate in Act 1 (`v2-spec.md` §6.3); the 8 above can.

An event leaves its list when it is picked, so each event happens at most once
a run. The picked event plays out on the same entry.

### 2.2 `generateEvent`

```
if rng.random(1.0f) < 0.25:                    // shrineChance, set once to 0.25
    if shrineList or specialOneTimeEventList is non-empty: return getShrine(rng)
    if eventList is non-empty:                      return getEvent(rng)
    return null
e = getEvent(rng)
return e if e != null else getShrine(rng)
```

`getEvent` builds a candidate list from `eventList`, skipping any that fail a
gate, and takes `rng.random(size - 1)`. The Act 1 gates:

| event | gate |
|---|---|
| Dead Adventurer | `floorNum > 6` |
| Hypnotizing Colored Mushrooms | `floorNum > 6` |
| The Cleric | `gold >= 35` |

If no candidate passes, `getEvent` returns `getShrine(rng)`.

`getShrine` builds its candidates from all of `shrineList` plus each
`specialOneTimeEventList` key that passes its gate, and takes
`rng.random(size - 1)`. The gates that matter in Act 1:

| event | gate |
|---|---|
| Face Trader | act is 1 or 2 |
| The Woman in Blue | `gold >= 50` |
| The Divine Fountain | `player.isCursed()`: the deck holds a curse other than Necronomicurse, Curse of the Bell and Ascender's Bane |

The rest of the 8 have no gate. Gates are read on entry, against the state at
that moment.

### 2.3 The stream, and the shrine re-entry bug

`v2-spec.md` §4.1 records Rob's ruling (2026-08-14) to reproduce the "shrine
re-entry bug", and lists its mechanism as a remaining blocker. The decompiled
source settles it:

1. Entering a `?` (`nextRoomTransition`): a copy of `eventRng` is made at its
   current counter, `EventHelper.roll` draws the room roll from the copy, and
   `eventRng` is then **replaced by the copy**. Net effect: `eventRng` advances
   by one draw.
2. If the room is an event (`EventRoom.onPlayerEntry`): **another** copy is made
   at the new counter, and `generateEvent` draws from it: the shrine check, then
   the index. `eventRng` itself does not move.
3. Nothing else in the game reads `eventRng` (code search: `EventRoom`,
   `EventHelper`, `AbstractDungeon`, `SaveFile` only).

So the **next** `?` room's roll, however many other rooms come between, is the
same float as this event's shrine check. When the check passed (below 0.25,
normally a shrine), the monster band at that next `?` is `monsterChance`, which
is at least 0.2 after a non-monster `?` (it resets to 0.1 and rises 0.1 per
non-monster result). So the next `?` is a fight with probability
`monsterChance / 0.25`: 80% at 0.2, 100% from 0.25. When the check failed (0.25
or above, normally a regular event), the next `?` cannot be a fight while
`monsterChance` is at most 0.25. Both effects follow from the stream structure;
neither needs special-casing.

**Design:** the event stream becomes one persistent generator in `RunState`,
seeded once from `(run_seed, RngStream::Event)`. `resolve_unknown_room` draws the
room roll from it, advancing it. Event selection copies it and draws from the
copy. Today `resolve_unknown_room` derives a fresh stream per floor, which breaks
the correlation. See E2.

### 2.4 Event-internal randomness

The events' own rolls do not use `eventRng`. They use `miscRng`, which
`nextRoomTransition` re-seeds on every floor as `Random(seed + floorNum)`;
relics use `relicRng`; potions use `potionRng`; Match and Keep's class cards and
curse use `cardRng` (`getCard`, `returnRandomCurse`) and its colorless card
`shuffleRng` (`returnColorlessCard`), which is re-seeded per floor like
`miscRng`. Every roll below is marked with its StS stream. See E3 for the
engine's mapping.

## 3. The event as an engine phase

### 3.1 State

`Phase::Event` already exists. It needs an event record in `RunState`:

- which event is live (an `EventId` over the 25);
- which screen of it is showing (Golden Idol has two decision screens; Dead
  Adventurer repeats one);
- the values rolled on entry that a human sees or that later screens use: World
  of Goop's gold loss, Dead Adventurer's elite, reward order, search count and
  chance, Scrap Ooze's damage and chance, We Meet Again's three offers, Golden
  Idol's two amounts.

It is part of `RunState`, so `clone()` copies it.

### 3.2 Options are the global ids of `v2-spec.md` §6.3

Each decision screen offers a set of option ids from the event-option action
block. An option that is unavailable is masked in place, never renumbered
(§6.3). A choice applies its effects at once and moves to the next screen.

**A screen with exactly one option is not a decision** and advances on its own.
That covers every Proceed, Continue and Leave screen, Dead Adventurer's "Fight!"
and Mushrooms' "Fight!", Face Trader's and Note For Yourself's intros, Bonfire
Spirits' two single-option screens, Lab's "Search" and Wheel of Change's "Spin".
It is the rule a one-option `RequestChoice` already follows in combat
(`colorless-effects.md` §6, batch 1). Lab and Wheel of Change have no other
option, so their ids would never be live. See E4.

The event ends when its last screen advances. The run then returns to
`Phase::Map`, except where a fight or a reward screen comes first (§4.7, §4.8).

### 3.3 Card choices

Ten events ask for a card from the master deck. StS opens its grid select with
`canCancel = false` in every one, so the pick is mandatory once the option is
taken. The candidates are `masterDeck.getPurgeableCards()` minus bottled cards
for removal and transform, and `getUpgradableCards()` for upgrade.

| purpose | events |
|---|---|
| remove | The Cleric (Purify), Wing Statue (Pray), Living Wall (Forget), Purifier, Wheel of Change (result 4) |
| transform | Living Wall (Change), Transmogrifier |
| upgrade | Living Wall (Grow), Upgrade Shrine, Ominous Forge (Forge) |
| offer | Bonfire Spirits: removed, with a reward set by the card's rarity |
| store | Note For Yourself: removed, after the noted card is obtained |

`PendingDeckChoice` (`run_state.h`) already runs Remove and Transform for
relics, staging picks and applying them together. It records the relic that
opened the choice. Events need three more purposes and a source that can be an
event. See E5.

## 4. Shared mechanics

### 4.1 Damage outside combat

Every event that costs HP calls `AbstractPlayer.damage`, the same function combat
uses. Out of combat it reduces to:

1. Block is 0 and Intangible cannot be held, so neither applies.
2. If `info.owner` is the player, each relic's `onAttackToChangeDamage` runs:
   **The Boot** raises a non-HP_LOSS hit of 1–4 to 5.
3. If `info.owner` is not null, `onAttacked` runs: **Torii** lowers a non-HP_LOSS
   hit of 2–5 to 1.
4. `onLoseHpLast`: **Tungsten Rod** takes 1 off any loss.
5. HP is lost. `wasHPLost` and `onBloodied` reactions run, but Runic Cube,
   Self-Forming Clay, Centennial Puzzle and Red Skull's Strength each check
   `RoomPhase.COMBAT` and do nothing. Meat on the Bone's `onBloodied` only
   changes its icon.
6. At 0 HP: **Fairy in a Bottle** first, then **Lizard Tail**, as task #7's
   requirement records (`potion-effects.md` §5.4). Fairy heals
   `max(1, maxHP * potency / 100)`, Lizard Tail `max(1, maxHP / 2)`, with no Magic
   Flower bonus, since `MagicFlower.onPlayerHeal` checks `RoomPhase.COMBAT`.
   With neither, the run is lost.

Which hits have an owner:

| owner | events |
|---|---|
| the player (2, 3, 4 all apply) | Wing Statue (7), World of Goop (11), Shining Light (`round(maxHP × 0.2)`) |
| none (only 4 applies) | Golden Idol (Smash), Scrap Ooze, Face Trader (Touch) |
| none, HP_LOSS | Wheel of Change (result 5) |

In Act 1 none of the owned hits falls in Torii's or The Boot's range: 7 and 11
are fixed, and Shining Light's is at most 5 only when max HP is at most 27. They
go through the same path anyway, so the rule holds without a special case.

**Max HP loss** (Golden Idol, Hide) is `decreaseMaxHealth`: it lowers max HP and
clamps current HP, and cannot kill. **Healing** out of combat is a plain clamped
heal; no Act 1 relic changes it.

### 4.2 A curse and a relic from the same option

`ShowCardAndObtainEffect` checks **Omamori in its constructor**, and runs the
relics' `onObtainCard` and adds the card **when the effect finishes**
(`curse-effects.md` §6.1 lists the three steps; the source shows when each runs).
Big Fish's Box and Ominous Forge's Rummage create the effect, then obtain the
relic at once. So:

1. Omamori, if already held, may block the curse.
2. The relic is obtained.
3. If the curse was not blocked, `onObtainCard` runs, the new relic included,
   and the curse is added.

Big Fish's Box draws a random relic. An Omamori from the box cannot block the
Regret. A Darkstone Periapt from the box gives +6 Max HP for it, and a Ceramic
Fish gives 9 gold. The engine's `add_card` does the Omamori check and the obtain
together, so these two options split it: check, obtain the relic, then add.

Note For Yourself does not use the effect: it calls `onObtainCard` and adds the
card directly, with no Omamori check (the card is never a curse).

### 4.3 Gold

Gold an event pays directly goes through `player.gainGold`: Ectoplasm refuses
it, and Golden Idol does not add to it (Golden Idol's bonus lives in
`RewardItem.applyGoldBonus`). Gold an event puts on a fight's reward screen is a
gold reward and gets Golden Idol's 25%. Dead Adventurer pays both kinds (§5).

**Gold an event takes does not break Maw Bank** (found while building step 1).
Every event's cost goes through `player.loseGold`, which calls the relics'
`onSpendGold` only `instanceof ShopRoom`; wiki.gg Maw Bank agrees ("will not
break when spending Gold at an Event"). The engine had one spend path,
`spend_gold`, which always trips Maw Bank. Events use `lose_gold`, which does
not. `spend_gold`'s comment had read `MawBank.onSpendGold` alone and concluded
"on anything"; the room check is one call up.

### 4.4 Relics

Most event relics are `returnRandomScreenlessRelic(returnRandomRelicTier())`:
the standard 50/33/17 tier roll, then a relic of that tier that does not open a
screen. That is the helper Calling Bell's three relics use. Dead Adventurer's
fight puts `addRelicToRewards(tier)` on the reward screen instead, which is
not restricted to screenless relics.

The named relics (Golden Idol, Odd Mushroom, Warped Tongs, Spirit Poop, the five
Face Trader faces) are in `RelicId`. Four events fall back to **Circlet** when
the player already holds the relic, but each of those relics comes only from
that same event, which happens once a run. So the fallback is unreachable and
Circlet stays out of the vocabulary (`relic.h`).

### 4.5 Potions

Lab and The Woman in Blue use `PotionHelper.getRandomPotion()`: a **uniform**
pick over every potion, on `potionRng`. A combat drop uses
`returnRandomPotion()`, which rolls rarity first, so these two events are not
rarity-weighted. The potions go on a reward screen, which in the engine means
`offer_potion_rewards` (taken at once if there is room, else waiting, Rob W1).

### 4.6 Event fights

Dead Adventurer and Hypnotizing Colored Mushrooms each end in a fight, inside the
event room. After the fight:

- **No monster gold and no elite gold.** Those branches test
  `instanceof MonsterRoom` and `MonsterRoomElite`, which an `EventRoom` is not.
  The event added its own gold and relic to the room's rewards before the fight.
- **Potion:** `addPotionToRewards`'s `EventRoom` branch, chance `40 + drift`, the
  same formula as a normal fight, with no all-escaped rule (`thievery.md`).
- **Card reward:** given unless the event sets `noCardsInRewards` (neither fight
  does), at the normal-fight odds (rare 3, uncommon 37). Prayer Wheel does not
  add a second: it tests `instanceof MonsterRoom`.
- **Elite effects:** Dead Adventurer sets `eliteTrigger`, which Sling of
  Courage, Preserved Insect and Slaver's Collar read (Slaver's Collar was missed
  in the first draft; the wiki check found it, §5.0). Black Star tests
  `instanceof MonsterRoomElite`, so it adds nothing.

Two engine gaps:

- Dead Adventurer's Lagavulin is `new Lagavulin(false)`. It starts awake: no
  8 block, no Metallicize, and its first move is Siphon Soul. The engine's
  Lagavulin always starts asleep.
- "The Mushroom Lair" is three Fungi Beasts; the encounter is not in
  `encounter.cc`.

### 4.7 Reward screens without a fight

Lab, The Woman in Blue and Wheel of Change's relic result open the reward screen
directly, with `noCardsInRewards` set, so no card reward is offered. Lab's and The
Woman in Blue's potions go through `offer_potion_rewards`; Wheel of Change's relic
is obtained.

## 5. The events

### 5.0 Wiki cross-check (2026-10-22)

Each event's wiki.gg page was read against the decompiled class. 21 of the 25
agree on every number the page gives. Four do not match exactly:

| event | wiki | decompiled game | resolution |
|---|---|---|---|
| Big Fish | four options, the fourth "[Leave] Exit without taking any reward" | three `setDialogOption` calls (Banana, Donut, Box); "Leave" is the result screen's only option | **the code (Rob W1, 2026-11-03):** no way to decline. A Note For Yourself's wiki page lists its exit screen as a "[Leave]" option the same way |
| The Cleric | "a shrine Event" | in `Exordium.initializeEventList`, not the shrine list; its gold gate is in `getEvent` | the code: it is a regular event. Only the list it is drawn from depends on this |
| Dead Adventurer | Preserved Insect, Sling of Courage **and Slaver's Collar** work in the fight | `SlaversCollar.beforeEnergyPrep` reads `eliteTrigger` | **the wiki is right; this document had missed it.** Corrected in §4.6 and E7 |
| The Divine Fountain | appears with "a Curse in your deck"; removes "all Curses" | `isCursed()` and the removal both skip Necronomicurse, Curse of the Bell and Ascender's Bane | consistent: those three cannot be removed by anything. The code is the precise statement |

Not on the wiki, so checked against the code only: Wheel of Change's gold (100
in Act 1) and its odds (uniform over six), Dead Adventurer's fight gold (25–35),
Mushrooms' fight gold (20–30), and the order of We Meet Again's three entry
rolls. The wiki states Scrap Ooze's real odds (26%, 36%, 46%…) and Face
Trader's (each of the five faces equally likely), both as the code has them.

"misc" marks a roll on `miscRng`, "relic" a `returnRandomScreenlessRelic` of the
standard tier, "remove/transform/upgrade" a mandatory card choice (§3.3). Option
ids are listed in the order StS shows them. An option masked in place is marked
*(masked: condition)*.

### Act 1 events

**Big Fish.** Banana: heal `maxHP / 3` (integer division). Donut: +5 Max HP
(which heals 5). Box: Regret and a relic, in §4.2's order.

**The Cleric** (gate: gold ≥ 35). Heal: −35 gold, heal `int(maxHP × 0.25)`
*(masked: gold < 35)*. Purify: −50 gold, remove *(masked: gold < 50)*; if no card
can be removed, no gold is taken. Leave.

**Dead Adventurer** (gate: floor > 6). On entry: the order of three rewards
{30 gold, nothing, relic} is shuffled (misc), the elite is chosen (misc 0–2: 3
Sentries, Gremlin Nob, awake Lagavulin), and the fight chance is 25.
- Search: misc 0–99 below the chance starts the fight. Otherwise the next reward
  in the shuffled order is given (30 gold through `gainGold`, nothing, or a
  relic), and the chance rises by 25. After the third reward the event ends.
- The fight's rewards: gold misc 25–35, plus each reward not yet found (30 gold
  as a gold reward, a relic of the standard tier). Then §4.6.
- Leave.

**Golden Idol.** Take: obtain Golden Idol, then a second screen. Outrun: obtain
Injury. Smash: take `int(maxHP × 0.25)` damage (no owner). Hide: lose
`max(1, int(maxHP × 0.08))` Max HP. Both amounts are computed on entry. Leave
(first screen only).

**Wing Statue.** Pray: take 7 damage (owner: player), then remove. Destroy: gain
misc 50–80 gold *(masked: no Attack in the deck with base damage ≥ 10,
`hasCardWithXDamage` reads `baseDamage`)*. Leave.

**World of Goop.** On entry: the loss is misc 20–50, capped at the player's gold.
Gather: take 11 damage (owner: player), then gain 75 gold. Leave: lose the
rolled amount.

**The Ssssserpent.** Agree: gain 175 gold and obtain Doubt. Disagree.

**Living Wall.** Forget: remove. Change: transform (misc). Grow: upgrade
*(masked: no upgradable card)*.

**Hypnotizing Colored Mushrooms** (gate: floor > 6). Stomp: fight The Mushroom
Lair; rewards gold misc 20–30 and Odd Mushroom, then §4.6. Eat: heal
`int(maxHP × 0.25)`, obtain Parasite.

**Scrap Ooze.** Reach In: take the damage (3, no owner), then misc 0–99: at least
`99 − chance` (chance starts 25) gives a relic and ends the event. Otherwise
damage +1 and chance +10, and the screen repeats. The comparison makes the real
odds `chance + 1` percent: 26, 36, 46… Leave.

**Shining Light.** Enter: take `round(maxHP × 0.2)` damage (owner: player), then
upgrade two random upgradable cards (one if only one exists; misc shuffle)
*(masked: no upgradable card)*. Leave.

### Shrines

**Match and Keep!** Auto-resolved (`v2-spec.md` §9). Twelve face-down cards, two
each of: a Rare, an Uncommon and a Common Ironclad card, an Uncommon colorless
card, a curse, and Bash (`getStartCardForEvent`). Shuffled with misc; cards from
the streams in §2.4. Five attempts; each matched pair adds one copy to the deck.
See E9.

**Golden Shrine.** Pray: +100 gold. Desecrate: +275 gold, obtain Regret. Leave.

**Transmogrifier.** Pray: transform (misc). Leave.

**Purifier.** Pray: remove. Leave.

**Upgrade Shrine.** Pray: upgrade *(masked: no upgradable card)*. Leave.

**Wheel of Change.** Spin (one option, so automatic): misc 0–5.
0: +100 gold. 1: a relic, through the reward screen. 2: heal to full.
3: obtain Decay. 4: remove (skipped if no card can be removed).
5: lose `int(maxHP × 0.10)` HP (HP_LOSS, no owner). No card reward.

### One-time events

**Ominous Forge.** Forge: upgrade *(masked: no upgradable card)*. Rummage:
obtain Pain and Warped Tongs, in §4.2's order. Leave.

**Bonfire Spirits.** Offer (automatic): choose a card to offer; it is removed.
By its rarity: curse, Spirit Poop; basic, nothing; common or special, heal 5;
uncommon, heal to full; rare, +10 Max HP then heal to full. With no removable
card, nothing happens. The engine has rarity pools, not a rarity field
(`card.h`), so this needs a lookup that also answers basic, special and curse.

**Face Trader** (gate: act 1 or 2). Touch: gain 75 gold, then take
`max(1, maxHP / 10)` damage (no owner). Trade: a random face relic the player
does not hold, from Cultist Headpiece, Face of Cleric, Gremlin Visage, N'loth's
Hungry Face and Ssserpent Head (misc shuffle). Leave.

**The Divine Fountain** (gate: §2.2). Drink: remove every curse except
Necronomicurse, Curse of the Bell and Ascender's Bane. Leave.

**Lab.** Search (one option, so automatic): three random potions (§4.5) on a
reward screen. No card reward.

**Note For Yourself.** Take and Give: obtain the noted card, then store a card
from the deck (removed). Ignore. The noted card comes from the player's profile:
the card stored in the previous run, `"Iron Wave"` with 0 upgrades on a profile
that has none. See E8.

**We Meet Again.** On entry, three offers (misc, in this order): a random held
potion; gold, 0 if gold < 50, else misc 50 to `min(gold, 150)`; a random deck
card that is neither basic nor a curse. Give Potion, Give Gold and Give Card each
cost their offer and give a relic *(each masked when its offer is absent)*.
Attack: leave with nothing. While the event is open, potions can be neither drunk
nor discarded (task #7's requirement, `potion-effects.md` §6.1).

**The Woman in Blue** (gate: gold ≥ 50). Buy 1, 2 or 3 random potions (§4.5) for
20, 30 or 40 gold, on a reward screen. Leave. No card reward.

### Option count

Counted from the list above, with one-option screens automatic (§3.2):

| group | events | ids |
|---|---|---:|
| Act 1 events | 11 | 29 |
| shrines | 6 | 9 |
| one-time | 8 | 18 |
| **total** | **25** | **56** |

Per event: Big Fish 3, The Cleric 3, Dead Adventurer 2, Golden Idol 5, Wing
Statue 3, World of Goop 2, The Ssssserpent 2, Living Wall 3, Mushrooms 2, Scrap
Ooze 2, Shining Light 2; Match and Keep 0, Golden Shrine 3, Transmogrifier 2,
Purifier 2, Upgrade Shrine 2, Wheel of Change 0; Ominous Forge 3, Bonfire
Spirits 0, Face Trader 3, The Divine Fountain 2, Lab 0, Note For Yourself 2, We
Meet Again 4, The Woman in Blue 4.

`v2-spec.md` §6.3 has 58. The difference is Lab's Search and Wheel of Change's
Spin, which §6.3 counts as one id each (E4).

## 6. What the agent must see

Under `observation-space.md` §1, everything on an event screen that a human
reads is a requirement. Blocks 17 (event one-hot) and 6b (event parameters) are
specified in `v2-spec.md` §5.2 and §5.2.1 and are part of the run observation,
which task #9 sequences. Checking 6b's enumeration against the source finds three
values it does not have:

| value | event | why a human sees it |
|---|---|---|
| the gold offered | We Meet Again | the option text states it; rolled on entry |
| damage and chance | Scrap Ooze | the option text states both; they rise with each failed attempt |
| which elite | Dead Adventurer | the intro text describes the body (`DESCRIPTIONS[3..5]`), which tells a player which elite it is |

Dead Adventurer's search count, which sets its fight chance (25, 50, 75), is in
6b already (slot 2). The entity offers, We Meet Again's potion and card and Note For
Yourself's card, are block 19 marks (§5.9). See E10.

## 7. Engine work this needs

1. `EventId` over the 25, the three lists in `RunState`, and selection (§2).
2. The persistent event stream (§2.3) and a per-floor stream for event rolls
   (§2.4).
3. The event record (§3.1), the option dispatcher and its mask, one-option
   screens advancing on their own.
4. A run-layer damage function (§4.1), with the death intercept.
5. The deck-choice purposes upgrade, offer and store, and a non-relic source
   (§3.3).
6. The curse-then-relic order (§4.2).
7. A card-rarity lookup that covers basic, special and curse (Bonfire Spirits,
   We Meet Again).
8. Event fights (§4.6): a reward source for them, the awake Lagavulin, The
   Mushroom Lair.
9. The We Meet Again potion lock.
10. Ssserpent Head on every `?` node before the roll (task #17), which Face
    Trader makes reachable.
11. The event-option action block resized if E4 is accepted.

## 8. Implementation order (proposal)

Each step is one commit, tested and mutation-checked:

1. **Selection and the simple events.** Items 1–4 and 6 of §7, plus the events
   with no card choice, fight or reward screen: Big Fish, Golden Idol, World of
   Goop, The Ssssserpent, Scrap Ooze, Shining Light, Golden Shrine, The Divine
   Fountain, Face Trader (with Ssserpent Head, #17).
2. **Card-choice events.** Item 5: The Cleric, Wing Statue, Living Wall,
   Transmogrifier, Purifier, Upgrade Shrine, Ominous Forge, Bonfire Spirits
   (item 7), Note For Yourself, Wheel of Change.
3. **Reward-screen events.** Lab, The Woman in Blue, We Meet Again (item 9).
4. **Event fights.** Item 8: Dead Adventurer, Hypnotizing Colored Mushrooms.
5. **Match and Keep!** The auto-resolve policy (E9).

Tests build the event they test (`begin_event(EventId)`) rather than sampling a
seed until a `?` lands on it. Selection is tested on the candidate lists as pure
functions, and the shrine correlation by its invariant (the next `?` roll equals
the shrine check), never by counting outcomes over seeds.

**Between steps,** a `?` can already draw an event a later step builds. It ends
at once with no effect (`begin_event` finishes an event that offers nothing),
so the selection and its RNG are final from step 1 and a run never stalls.

## 10. As built

### Step 1 (2026-11-05)

- `src/event.h`, `src/event.cc`: `EventId` (25), `EventOption` (56, asserted
  equal to the action block), the amounts, `scrap_ooze_finds_relic`,
  `event_damage_after_relics`, `face_trader_faces`.
- `RunState`: `event_rng` (one stream, seeded in `start`), the three lists,
  `EventState`, `floor_event_rng` on the new `RngStream::EventMisc`;
  `event_candidates`, `shrine_candidates`, `generate_event`, `begin_event`,
  `event_options`, `choose_event_option`, `finish_event`, `take_event_damage`,
  `is_cursed`, `obtain_curse_then_relic` (with `add_card` split into
  `omamori_negates` and `add_card_past_omamori`), `lose_gold`,
  `random_screenless_relic` (Calling Bell now uses it), and
  `upgrade_random_cards` over a caller's stream.
- `resolve_unknown_room` draws from `event_rng`; `enter_room` and
  `fire_room_entry_relics` take `unknown_node`, which Ssserpent Head reads.
- The nine step-1 events. `kNumEventOptions` 58 → 56, `kTotalActions` 8,615 →
  8,613.
- Face of Cleric and N'loth's Hungry Face, which Face Trader can give (Rob,
  2026-11-07; `relic-effects.md` §7): a `GainMaxHp` on the combat-end hook, and
  `open_chest` dropping the first relic on the chest's list.

`tests/test_events.cc`, 80 tests: the lists and gates, the pick and the stream
(including the shrine correlation, asserted from the shared number for every
seed), Ssserpent Head, the damage path and the death intercept, the
curse-then-relic order, and each event. `test_path_choice.cc`'s run walk now
plays events through their options.

Mutations, each applied and reverted, all caught (M16–M28 in groups of two or
three on separate code, each caught by its own test):

| # | mutation | caught by |
|---|---|---|
| M1 | the pick draws from `event_rng` itself | `ThePickLeavesTheEventStreamWhereItWas`, `TheNextQuestionRollReadsTheShrineChecksNumber` |
| M2 | the `?` roll from a fresh per-floor stream | `EachQuestionRollTakesTheStreamsNextNumber`, `TheNextQuestionRollReadsTheShrineChecksNumber` |
| M3 | the shrine check inverted | `TheShrineCheckDecidesWhichListIsDrawn` |
| M4 | an event pick not removed from its list | `APickedEventLeavesItsList`, `PaysBeforeTheEventGatesAreRead` |
| M4b | a shrine pick not removed | `APickedShrineLeavesItsList` (added when M4 showed the first test could not reach this path) |
| M5 | floor gate `>=` | `DeadAdventurerAndMushroomsWaitForFloorSeven` |
| M6 | `is_cursed` counts Curse of the Bell | `TheDivineFountainNeedsACurseItCanRemove` |
| M7 | no fallback to a shrine | `NoEventPassingItsGateFallsBackToAShrine`, `APickedShrineLeavesItsList` |
| M8 | Ssserpent Head on a `?` that stays an event only | `PaysOnAQuestionNodeThatBecomesAFight` |
| M9 | Ssserpent Head's gold after the gates | `PaysBeforeTheEventGatesAreRead` |
| M10 | The Boot on every hit | `TheBootRaisesOnlyThePlayersOwnHits` |
| M11 | Torii on every hit | `ToriiLowersOnlyHitsWithAnOwner`, `SmashIsDamageWithNoOwner`, `TouchHasNoOwner` |
| M12 | Lizard Tail before the Fairy | `TheFairyComesBeforeLizardTail` |
| M13 | the curse before the relic | `ADarkstoneFromTheSameOptionGivesMaxHp`, `ACeramicFishFromTheSameOptionPaysForTheCurse` |
| M14 | the relic before the Omamori check | `AnOmamoriFromTheSameOptionCannotBlockIt` |
| M15 | World of Goop's loss through `spend_gold` | `LosingGoldHereDoesNotBreakMawBank` |
| M16 | Goop's loss uncapped | `TheLossIsCappedAtTheGoldHeld` |
| M17 | Hide without its minimum of 1 | `HideLosesAtLeastOneMaxHp` |
| M18 | Scrap Ooze `>` for `>=` | `TheOddsAreOneMoreThanTheChanceShown` |
| M19 | Scrap Ooze's damage fixed at 3 | `EachFailureMakesTheNextAttemptCostOneMore` |
| M20 | Shining Light's hit unowned | `ItsHitIsOwnedByThePlayer` |
| M21 | Shining Light's damage truncated | `TheDamageIsAFifthOfMaxHpRounded` |
| M22 | Shining Light upgrades one | `EnterUpgradesTwoCardsAfterTheDamage` |
| M23 | the Fountain stops after one curse | `DrinkRemovesEveryCurseThatCanLeave` |
| M24 | Face Trader may give a face already held | `TradeGivesAFaceNotAlreadyHeld` |
| M25 | Omamori's block also stops the Ssssserpent's gold | `OmamoriBlocksTheDoubtButNotTheGold` |
| M26 | the Fairy not used up | `FairyInABottleRevivesOutsideCombat`, `TheFairyComesBeforeLizardTail` |
| M27 | Banana heals a quarter | `BananaHealsAThirdOfMaxHp` |
| M28 | Shining Light's Enter never masked | `EnterIsMaskedWithNothingToUpgrade` |
| F1 | Face of Cleric does nothing | `EachWinRaisesMaxHpByOne`, `ASmokeBombEscapeCountsToo`, `MagicFlowerBoostsItsHeal` |
| F2 | Face of Cleric raises Max HP without the heal | `EachWinRaisesMaxHpByOne`, `MagicFlowerBoostsItsHeal` |
| N1 | N'loth's Hungry Face takes the last relic, not the first | `WithMatryoshkaItTakesMatryoshkasRelic` |
| N2 | its charge never spent | `TheNextChestDropsItsRelic`, `OnlyTheNextChest` |

Two tests were rewritten before the first run, having been vacuous: Golden
Idol's Smash and World of Goop's Gather were asserted at damages outside
Torii's range, so they could not show whether the hit had an owner. Smash is now
tested at 16 Max HP (a hit of 4); Goop's ownership is not observable at A0 (its
11 is fixed), so that test was dropped and Shining Light (at 20 Max HP) and Face
Trader carry the ownership checks.

**Found while wiring Face of Cleric (task #63, fixed separately):** StS's
`endBattle` runs Meat on the Bone's `onTrigger` before `player.onVictory()`, so
Meat on the Bone checks HP before Burning Blood heals. The engine had them the
other way round. Rob ruled to match the source (2026-11-07); see
`relic-effects.md` §6.7.

## 9. Questions for Rob

**Ruled 2026-10-22: E1–E11 agreed as recommended.** E9 took option (a).

**E1. Scope: the 25 events now; Neow as its own design after them?** Recommend
yes. Neow shares the option mechanism but not much else: its blessing pools, the
boss-relic swap and the curse drawbacks are §4.4 work of their own size. Task #55
(the closed-chest choice) waits on Neow either way.

**E2. One persistent event stream, which reproduces the shrine re-entry bug
(§2.3)?** Recommend yes. It is the 2026-08-14 ruling made concrete, and it
removes the blocker `v2-spec.md` §4.1 records. Cost: every run's `?` outcomes
change from today's, and tests that predict a `?` roll from a per-floor stream
need updating.

**E3. The engine's streams for event rolls.** StS uses `miscRng`, re-seeded per
floor. Recommend a new per-floor stream for event-internal rolls (appended to
`RngStream`; `Event` cannot serve, since §2.3 makes it persistent). Relics,
potions and cards use the paths their kind already uses: the relic grants of
chests and elites, the floor's potion generator, and the card-reward stream.

**E4. One-option screens advance on their own (§3.2), so Lab and Wheel of Change
have no option ids and `EVENT_OPTIONS` goes from 58 to 56?** Recommend yes. An
id the agent can never choose is a dead index (`v2-spec.md` §5.1 rule 5), and the
alternative, a decision with one legal action, is a step that teaches nothing.

**E5. Event card choices extend `PendingDeckChoice`:** purposes upgrade, offer
and store; a source that is a relic or an event; mandatory picks, as in StS.
Recommend yes. Offer and store are removals, but they need their own purposes
because the consequence differs and the agent must see it (block 18).

**E6. One run-layer damage function (§4.1)**, taking the hit's amount, whether it
has an owner (the player), and whether it is HP_LOSS; applying The Boot, Torii
and Tungsten Rod; then the Fairy-then-Lizard-Tail intercept, or a lost run.
Recommend yes. It is the requirement task #7 already carries, made specific.

**E7. Event fights (§4.6):** a reward source of their own, with the event's
rewards, the normal-fight card odds, the potion roll, and no monster or elite
gold; Dead Adventurer's fight counts as elite only for Sling of Courage and
Preserved Insect; and the awake Lagavulin and The Mushroom Lair are added to the
enemy layer. Recommend yes. *(Corrected after the ruling, 2026-10-22: Slaver's
Collar reads the same flag, so the list is three relics. Same rule, a missed
member; §5.0.)*

**E8. Note For Yourself without cross-run state.** The noted card is whatever the
previous run stored. An episode has no previous run, so the card is always the
fresh-profile default, an unupgraded Iron Wave, and storing a card removes it for
nothing. The reasoning is the same as Neow's four blessings (`v2-spec.md` §8),
and it goes in the §9 divergence table for the README. Recommend yes.

**E9. Match and Keep's policy.** §9 rules "the engine picks uniformly at random"
but does not say what is picked. Options:
(a) each of the five attempts flips two face-down cards chosen uniformly from
those left, with no memory;
(b) a policy that remembers what it has seen and flips a known pair when it can,
which is closer to how a person plays.
(a) is what the ruling says, and it is simple; it finds fewer pairs than a person
would. Recommend (a), on the `AutoResolve` stream, with the gap recorded beside
the §9 entry.

**E10. Observation:** record §6's requirements now and build blocks 17 and 6b
with task #9, adding the three values §6 found. Recommend yes. The 2026-09-14
rulings put the observation outside current work, and "blocks are added as their
data sources land" argues for sizing 6b once, when it is built.

**E11. Implementation order (§8).** Recommend as listed. Rob may re-sequence.

**W1. Big Fish has no Leave option?** (from the wiki check, §5.0) The wiki lists
four options; the decompiled constructor sets three, and "Leave" appears only as
the result screen's single option. Recommend following the code: Big Fish's three
ids stay, and a player who enters must take Banana, Donut or Box. A play check
would settle it if you remember the screen.

**Ruled (Rob, 2026-11-03): no Leave.** Three options, then the exit screen; the
event cannot be skipped. Also confirmed: The Cleric is drawn from the event list
with its 35-gold gate (§2.2).
