# Curse effects — design (task #4)

**Status:** agreed with Rob, 2026-10-18: C1–C3 and C5–C8 as recommended; C4
overruled, so card damage is computed when the card is played, as in StS
(§3.8). Built in three steps (C8, with the damage change as its own step):

1. **Built:** §3.1, §3.2, §3.5. The end of turn, Burn, Decay, Regret, Doubt
   and Shame, the HP-loss reactions and Plated Armor. Tests and mutations in §5.
2. **Next:** §3.8, card damage at play time.
3. **Then:** §3.3, §3.4, §3.6, §3.7. Pain, Normality, Velvet Choker, Blue
   Candle and the cards-played counter.

Five curses exist as card data with no reader: Regret, Doubt, Shame, Pain and
Normality (`card.h`'s curse block). Three of them act at the end of the turn on
the same path as Burn and Decay, and that path turns out to have its own parity
bugs, measured in §2. This doc covers the five effects, the path they share,
and the relics that act on curses in combat (Blue Candle, Velvet Choker).

Out of scope: getting curses into the deck (Omamori, Darkstone Periapt, Calling
Bell, Cursed Key's chest curse, Du-Vu Doll's count). That is the next step (§6).

## 0. Sources

Decompiled StS1 (`aeubanks/sts`): `cards/curses/{Regret,Doubt,Shame,Pain,
Normality,Decay}`, `cards/status/Burn`, `cards/AbstractCard` (`canUse`,
`hasEnoughEnergy`, `triggerOnEndOfPlayerTurn`), `cards/CardGroup`
(`triggerOnOtherCardPlayed`), `actions/GameActionManager` (`getNextAction`,
`callEndOfTurnActions`), `actions/utility/UseCardAction`,
`actions/common/DiscardAtEndOfTurnAction`, `ui/buttons/EndTurnButton`,
`rooms/AbstractRoom.endTurn`, `characters/AbstractPlayer` (`useCard`, `damage`),
`core/AbstractCreature.applyEndOfTurnTriggers`, `powers/{Weak,Rupture,
PlatedArmor,Metallicize,Combust,Buffer}Power`, `relics/{BlueCandle,VelvetChoker,
Orichalcum,StoneCalendar,RunicCube}`.

Wiki.gg, checked 2026-10-18: Regret, Doubt, Shame, Pain, Normality, Rupture,
Burn. All agree with the Java on every point below. The Plated Armor page is
marked "TO UPDATE" and says nothing about damage types, so the Java alone
decides §1.4's Plated Armor rule.

The engine's current behaviour in §2 was measured with a throwaway probe linked
against `build/libminispire.a`, not inferred from reading.

## 1. What StS does

### 1.1 The end of the player's turn

1. **End Turn** queues `NewQueueCardAction`, which puts an end-of-turn marker at
   the back of the card queue (`EndTurnButton.disable`).
2. When the marker is reached (every action run, every queued card played),
   `callEndOfTurnActions` (`GameActionManager`:358) runs:
   1. relics' `onPlayerEndTurn`: Orichalcum (reads block now; +6 via
      `addToTop`), Stone Calendar (turn 7; 52 to all via `addToBot`);
   2. powers' `atEndOfTurnPreEndTurnCards`: Metallicize and Plated Armor
      (block via `addToBot`);
   3. each hand card's `triggerOnEndOfTurnForPlayingCard`. Burn, Decay, Regret,
      Doubt and Shame set `dontTriggerOnUseCard` and append themselves to the
      card queue. **Regret sets its magic number to `hand.size()` here**, so it
      counts itself and every other card still in hand, end-of-turn cards
      included (wiki: "Regret counts any copies of the others as being still in
      your hand").
3. The actions from 2.1–2.2 run. Then each end-of-turn card is played from the
   card queue. `canUse || dontTriggerOnUseCard` lets it through. Because
   `dontTriggerOnUseCard` is set, the play skips the `onPlayCard` hooks, the
   cards-played count, the `onUseCard` hooks (Blue Candle) and Pain
   (`AbstractPlayer`:1353). `use()` queues the effect; `UseCardAction` moves the
   card to the discard pile. Each card's actions, and everything they trigger,
   run before the next card is played.
4. With the card queue empty, `AbstractRoom.endTurn` runs: every player power's
   `atEndOfTurn` (Combust, Regen, Rage, player Ritual, The Bomb, Entangle and No
   Draw removal), then `DiscardAtEndOfTurnAction`: ethereal cards exhaust (each
   `addToTop`, so before the discards), and the rest are discarded unless Runic
   Pyramid holds them.

### 1.2 The five curses

| curse | when | effect | notes |
|---|---|---|---|
| Regret | end-of-turn autoplay | `LoseHPAction(player, player, count)`, `addToTop` | count from §1.1 2.3. HP loss: block does not apply; Intangible, Tungsten Rod and Buffer do. Rupture fires (wiki agrees) |
| Doubt | end-of-turn autoplay | `WeakPower(player, 1, isSourceMonster = true)`, `addToTop` | `isSourceMonster` makes a new Weak start just applied, so it skips the end-of-round tick and lasts through the next player turn. Artifact and Ginger block it |
| Shame | end-of-turn autoplay | `FrailPower(player, 1, true)` | as Doubt; Artifact and Turnip block it |
| Pain | `useCard`, after the played card's `use()` and its `UseCardAction` are queued | `LoseHPAction(1)` per Pain in hand, `addToTop` | skips the played card itself (`CardGroup`:979), so a Pain played under Blue Candle does not trigger itself. Skipped for `dontTriggerOnUseCard` plays. Resolves before the played card's actions (wiki: "before Thorns, Sharp Hide, Beat of Death") |
| Normality | `canPlay(card)` of every card in hand, inside `canUse` | no card can be played once `cardsPlayedThisTurn >= 3` | applies to card-queue plays too (wiki: it limits Mayhem). Not a debuff. Only while in hand: exhausting it lifts the cap |

Two Pain details that decide §3.4:

- The played card's damage is computed in `useCard` (`calculateCardDamage`)
  before Pain's HP loss runs. So Strength that Pain's loss triggers (Rupture,
  Red Skull) does not reach the card that triggered it. It reaches the next
  card: wiki Rupture: "if you have Pain in hand and use … Havoc, the first card
  will trigger Pain and therefore Rupture before the Attack takes effect."
- Pain's loss can kill the player before the card's damage lands: at 1 HP,
  playing a lethal Strike with Pain in hand loses the fight.

Doubt's `triggerWhenDrawn` resets `dontTriggerOnUseCard`. It has no visible
effect: `UseCardAction` already clears the flag after every play (line 129).

### 1.3 What counts as a card played

`cardsPlayedThisTurn` goes up for every card-queue play without
`dontTriggerOnUseCard`, before `useCard` (`GameActionManager`:235): cards from
hand, cards Havoc, Mayhem and Distilled Chaos take, Double Tap and Duplication
replays, and curses played under Blue Candle. It does not count end-of-turn
autoplays, queued cards that fail `canUse` (line 288), or potions. It is reset
at the start of the player's turn (line 340). Velvet Choker keeps its own
counter on the same rule (`onPlayCard`, reset in `atTurnStart`), capped at 6.

Wiki Normality agrees: double plays count twice, Distilled Chaos plays count,
and a Blue Candle play of Normality counts.

### 1.4 HP-loss reactions need HP actually lost

In `AbstractPlayer.damage` (lines 1381–1450), Intangible, block, Buffer
(`onAttackedToChangeDamage`) and Tungsten Rod (`onLoseHpLast`) all reduce
`damageAmount` first. Every reaction to losing HP sits inside
`if (damageAmount > 0)`:

- Rupture's `wasHPLost`: fires when `info.owner == player`. Burn and Decay deal
  damage with the player as owner, so their unblocked damage fires Rupture
  (wiki Rupture and Burn pages both say so).
- Plated Armor's `wasHPLost`: loses a stack only when the owner is another
  creature and the type is neither `HP_LOSS` nor `THORNS`. Burn and Decay are
  `THORNS` from the player, so they never reduce it. Buffer zeroing a hit means
  no reduction either.
- Runic Cube (`addToTop` draw), Centennial Puzzle, Self-Forming Clay, and Blood
  for Blood's count (`updateCardsOnDamage`).

Runic Cube's draw from Burn's damage resolves before Burn reaches the discard
pile, so the drawn card is discarded with the hand (wiki Burn: "Cards drawn from
damage are discarded with the rest of their hand").

### 1.5 Blue Candle

`canUse` lets a curse be played when Blue Candle is held (`AbstractCard`:905).
The play is a normal one: `onUseCard` queues `LoseHPAction(1)` (`addToBot`) and
sets the card to exhaust (`UseCardAction`, so Strange Spoon can still save it).
The curse costs no energy (cost -2) and its own `use()` does nothing. It counts
for Normality and Velvet Choker, triggers Pain in hand, and fires Rupture (wiki:
"Playing any Curse with Blue Candle triggers Rupture").

## 2. What the engine does today

Measured (probe, 2026-10-18). Each row ends the fight at the end of the player's
turn where the enemy's attack would hide the result.

| case | engine | StS | |
|---|---|---|---|
| Metallicize 3 + Burn, no block | block first, Burn absorbed | same | ✓ |
| Orichalcum + Burn, no block | block first, Burn absorbed | same | ✓ |
| Combust kills the last enemy, Burn in hand | Burn still lands (HP 77) | 77 | ✓ |
| Regen 5 at full HP + Burn | heal wasted, then Burn | Burn, then heal | ✗ |
| Runic Cube + Burn | the drawn card is kept into the next turn | discarded | ✗ |
| Plated Armor 1 + Burn, 1 unblocked | Plated Armor drops to 0 | stays 1 | ✗ |
| Rupture + Burn, unblocked | no Strength | +1 Strength | ✗ |
| Tungsten Rod + Rupture + Combust (1 HP) | +1 Strength, +1 HP-loss event | neither | ✗ |

**Why.** `handle_end_turn` queues `DiscardHand` first, but its executor
`push_back`s Burn's damage and every card's move, so they run after every
`TurnEndPlayer` power action (Combust, Metallicize, Regen) and every relic
action. The block sources come out right by that accident. Regen comes out
wrong, and whatever Burn's damage triggers is queued behind the hand's moves.

`ordering-notes.md` §7 says `DiscardHand` resolves before the powers. It
resolves after them. The behaviour §7 pins (an ethereal exhaust feeds Feel No
Pain before the enemy attacks) holds either way, and StS also exhausts ethereal
cards after the powers (§1.1 step 4), so only the text is wrong.

The last two ✗ rows are the `LoseHp` executor firing Rupture, Blood for Blood's
count and the any-HP-loss relics unconditionally, where the damage paths already
check that HP was lost. The Plated Armor row is `damage_player` reducing it for
any unblocked damage, before Buffer.

## 3. Design

### 3.1 End of turn in three phases (§1.1)

`handle_end_turn`'s step 1 becomes:

- **Phase A, one drain.** Relic `TurnEndPlayer` hooks (Orichalcum, Stone
  Calendar, the per-turn counter resets). Then a new
  `Hook::TurnEndPlayerPreCards`, appended to the enum, for Metallicize and
  Plated Armor, moved out of `TurnEndPlayer`. Then the snapshot: every hand card
  with an end-of-turn effect, in hand order, is pushed to the back of
  `state.card_queue` as an **end-of-turn autoplay** (a new `QueuedPlay` flag).
  Regret's count, the hand size at this moment, is stored on its entry.
- **Phase B.** `play_queued_cards`. An end-of-turn entry resolves as its own
  drain: the card leaves the hand, its effect runs, then a `DiscardCard`. No
  on-play hooks, no play count, no Pain, no Blue Candle, no energy. Burn and
  Decay: fixed damage to the player, marked as the player's own (§3.5). Regret:
  `LoseHp(count)`. Doubt and Shame: an `ApplyDebuff` with
  `Action::as_source_monster` set, which the executor passes on as
  `from_enemy`. It is StS's `isSourceMonster` argument rather than the real
  source; the field's comment says so.
- **Phase C, one drain.** `TurnEndPlayer` powers (Combust, Rage, Ritual,
  Regen), `TickBombs`, `DiscardHand` (ethereal exhausts, the rest discard,
  Runic Pyramid). `DiscardHand` loses its Burn branch. Step 1b's ticks follow
  as today.

Terminal checks run after each phase, as today after the one drain. A fight
that ends in Phase A (Stone Calendar's 52 on turn 7) ends before the
end-of-turn cards play. They are left unplayed: `apply_action`'s
`play_queued_cards`, which runs after `handle_end_turn` returns, clears the
queue of a finished fight, the rule `card-queue.md` already has for "Fight
end". An explicit clear in Phase A was tried and removed: mutation-testing it
showed it had no effect.

**As built.** `resolve_end_of_turn_card` in `turn_loop.cc`, reached from
`resolve_queued_play` when the entry's `end_of_turn` flag is set.

**Why the card queue.** StS uses its card queue for exactly this, and the
engine's `play_queued_cards` already has the property that matters: each entry
drains, with everything it triggers, before the next entry and before anything
queued after it. That is what puts Runic Cube's draw before the discard. The
queue is empty when End Turn is taken (`apply_action` drains it, and a pending
choice blocks End Turn), and a hand holds at most 10 cards, so the capacity of
16 holds.

**RNG.** Nothing in these phases draws a random number, so no stream changes.

**Unceasing Top.** StS disables it for the last end-of-turn autoplay so an
emptied hand does not draw. The engine's check is in `refresh_hand`, called
once, from `apply_action`, after the next turn's draw. Nothing to change. There
is no test for it: at that point the hand is never empty for this reason, so a
test could not fail.

### 3.2 Doubt and Shame timing

With the just-applied flag, Doubt's Weak 1 is applied at the end of turn T,
skips the end-of-round tick of round T, applies through turn T+1, and is gone
after round T+1. Without the flag it would tick off before the player could
feel it, and Doubt would do nothing. The flag is cleared at the end of round T,
so it is never set at a decision point, and `debuff-timing.md`'s pinned "no
flag at any decision point" test keeps holding.

### 3.3 Normality, Velvet Choker and the play counter

- `Character::cards_played_this_turn`, incremented in the `CardPlayedHook`
  executor. Every `handle_play_card` reaches that executor once: hand plays,
  queued takes, replays, Blue Candle plays. End-of-turn autoplays and unusable
  queued cards never reach it, matching §1.3. StS increments before the card
  resolves and the engine after; nothing reads the count in between, since the
  mask and queued plays are both checked after the drain.
- Reset at the start of the player's turn.
- `is_playable`: false when Normality is in hand and the count is at least 3, or
  Velvet Choker is held and the count is at least 6. `resolve_queued_play`
  already calls `is_playable`, so a queued card that Normality blocks takes the
  existing unusable branch: it only moves to its pile, and a replay vanishes
  (StS line 288).

### 3.4 Pain

In `handle_play_card`, after the card leaves the hand and before its actions are
translated: one `LoseHp(1)` per Pain in hand, at the front. This covers every
play that goes through `handle_play_card`, and none of the plays §1.2 excludes.

Strength that Pain's loss triggers must not reach the card that triggered it
(§1.2). Rupture's response is pushed to the back of the queue and would land
after the card's hits anyway. Red Skull's is staged to the front
(`relic-effects.md` §6.29), and would land before them. With §3.8 in place it
no longer matters: the hits carry the damage computed when the card was played,
so neither response can change them. That is the reason step 2 comes before
this one.

### 3.5 HP-loss reactions

- `lose_player_hp` returns whether HP was lost. The `LoseHp` executor fires
  Blood for Blood's count, `HpLostPlayer` and `PlayerHpLostAny` only when it
  did, as `damage_player`'s callers already do.
- Fixed damage to the player carries whether the player is its source. Burn and
  Decay set it. When set and HP was lost, `HpLostPlayer` fires (Rupture).
- Plated Armor loses a stack only when HP was lost (after Buffer and Tungsten
  Rod) from an enemy attack. Not from the player's own fixed damage. This
  reverses the comment "Deliberately before Buffer" in `damage_player`, which
  has no recorded source (added in `db1fcdc`).

### 3.6 Blue Candle

- `is_playable`: a curse is playable when Blue Candle is held, beside the
  Medical Kit rule. Curses print cost 0, and Confusion already skips unplayable
  cards (`confuse_drawn_card`), so the play is free as in StS.
- `handle_play_card`: a curse played under Blue Candle queues `LoseHp(1)`
  before its pile move, and exhausts, through the same exhaust path as Medical
  Kit, so Strange Spoon applies.

### 3.7 Observation

`cards_played_this_turn` is visible to a human: Normality's text reads "you have
played N cards this turn", and Velvet Choker shows its counter. By
`observation-space.md` §1 and the ROB-40 B1 precedent (Blood for Blood's count,
Combust's casts), it goes in the player block, which grows from 7 to 8 floats.

### 3.8 Card damage is computed when the card is played (C4)

**StS.** `AbstractPlayer.useCard` calls `calculateCardDamage(target)` once,
before `use()`. That one call applies everything: relics' `atDamageModify`,
the player's powers' `atDamageGive` (Strength, Vigor, Pen Nib, Weak), the
target's `atDamageReceive` (Vulnerable), and the final-give and final-receive
passes. It stores `damage`, or `multiDamage[]` for an AoE card. The actions
`use()` queues carry that number: `DamageAction` hands its `DamageInfo` to
`target.damage` without recomputing, and `DamageAllEnemiesAction`,
`WhirlwindAction`, `FiendFireAction` and `VampireDamageAllEnemiesAction` carry
the array or info they were built with. The one exception is
`AttackDamageRandomEnemyAction` (Sword Boomerang). It rolls its target when it
runs and calls `calculateCardDamage` for that target there.

**Engine today.** `ordering-notes.md` §12 already fixes the base damage at play
(Body Slam's block, Perfected Strike's count, Rampage's bonus). The modifiers
are applied later, per hit, in `player_attack_enemy` through
`compute_attack_damage`. So anything that changes Strength, Vigor, Pen Nib, Weak
or Vulnerable between the play and a hit reaches that hit, which StS does not
allow.

**Change.** `handle_play_card` calls `compute_attack_damage` once per target
slot at translation, and each `DealDamage` hit carries the final number. The
executor applies it as it is: block, The Boot, the enemy hooks. The modifier
step is not repeated. Sword Boomerang's `DamageRandomEnemyAttack` keeps computing
at execution, against the target it rolls. `compute_attack_damage` stays the
single function that applies the modifiers; only where it is called from moves.

**What it fixes.** Pain's loss crossing Red Skull's threshold (§3.4). Any
mid-card Strength change, such as an enemy's thorns damage crossing Red Skull's
threshold between hits once those enemies exist. Vigor and Pen Nib are spent in
the `CardPlayedHook`, after the hits, so they are unchanged.

**To check when building.** Every producer of a player-to-enemy `DealDamage`
(cards, and any potion or relic that uses it) must hand over a final number, so
each is read before the executor changes.

## 4. Questions for Rob

Ruled 2026-10-18. Each answer follows its question.

**C1. Restructure the end of turn as §3.1?** Recommend yes. It fixes the Regen
and Runic Cube rows of §2, and it is the faithful path for Regret, Doubt and
Shame. The alternative, adding the three curses to `DiscardHand`'s Burn branch,
gives them the same two bugs. **Ruled: yes, match StS.**

**C2. Fix the HP-loss reactions (§3.5, first two bullets) in this change?**
Recommend yes. Pain with Tungsten Rod is the usual pairing: the wiki says the
Rod cancels each Pain trigger, and today each cancelled trigger would still
give Rupture's Strength and Blood for Blood's discount. Burn and Decay with
Rupture is the other half. **Ruled: yes.**

**C3. Plated Armor (§3.5, last bullet).** Recommend fixing both halves: Burn and
Decay do not reduce it, and Buffer stopping a hit does not reduce it. They are
the same condition in the Java. Only the Burn half is about curses; the Buffer
half could be split into its own task instead. **Ruled: yes, both halves.**

**C4. Red Skull + Pain.** When Pain's loss crosses half HP during an Attack, the
engine gives that Attack Red Skull's +3, and StS does not. Fixing it means
computing a card's damage when it is played, which changes every card.
Recommend accepting it and recording it in `ordering-notes.md`. **Ruled: no —
match StS.** Rob: computing a card's damage should be a modular part of the
engine, not a laborious calculation. It already is: `compute_attack_damage` is
one function, and §3.8 only moves where it is called from.

**C5. Include Blue Candle?** Recommend yes. It is combat-only, and Pain,
Normality and Rupture all have interactions with it on the wiki. It cannot
appear in a run until curses can enter the deck, but configured fights can use
it now. **Ruled: yes.**

**C6. Include Velvet Choker's cap (task #51)?** Recommend yes. It is the same
counter and the same mask rule with 6 in place of 3. Boss relics are not
obtainable yet, so it is latent. **Ruled: yes.**

**C7. Observe `cards_played_this_turn` (§3.7)?** Recommend yes, now. **Ruled:
yes.**

**C8. Commits.** Recommend two: first §3.1, §3.2 and §3.5 (the end of turn,
Burn, Decay, Regret, Doubt, Shame, the HP-loss reactions and Plated Armor);
then §3.3, §3.4, §3.6 and §3.7 (Pain, Normality, Velvet Choker, Blue Candle,
the counter and its observation). **Ruled: yes.** The C4 ruling adds §3.8, an
engine-wide change, as its own commit between the two.

## 5. Tests

### 5.1 Step 1, as built

`tests/test_curse_effects.cc`: `EndOfTurnOrder.*` (7), `Regret.*` (4),
`DoubtAndShame.*` (6), `HpLossReactions.*` (6). In `test_relic_triggers.cc`,
`PlatedArmorPower.KeepsItsStackWhenBurnGetsThrough` and
`KeepsItsStackWhenBufferStopsTheHit`.

- Regen at full HP with Burn: compared with a twin without Regen, the heal is
  worth the 2 Burn took.
- Runic Cube's draw from Burn's damage is discarded with the hand.
- Metallicize, Plated Armor and Orichalcum block lands before Burn.
- A fight Stone Calendar wins in Phase A leaves the Burn unplayed.
- End-of-turn cards are not card plays: Ink Bottle at 9 stays at 9.
- Regret: two Regrets, a Burn and a Strike cost 4 + 4 + 2. Regret ignores
  block, fires Rupture, and leaves the hand under Runic Pyramid.
- Doubt's Weak is there at the next decision point, with no just-applied flag,
  and gone at the one after. Shame's Frail makes the next turn's Defend give 3.
  Doubt onto a held Weak stacks and ticks. Artifact blocks Doubt; Ginger blocks
  it and keeps the Artifact charge; Turnip blocks Shame.
- Tungsten Rod or Buffer taking a loss to nothing fires neither Rupture nor
  Blood for Blood's count; the control loss fires both.
- Burn through block fires Rupture; fully blocked it does not; sourceless fixed
  damage does not.
- Plated Armor: kept against Burn, kept when Buffer stops an enemy hit,
  reduced by an enemy hit that gets through.

**Existing tests changed.** Each one encoded the old mechanism, not the game:
- `Curses.DecayDealsTwoAtEndOfTurnAndLeavesTheHand` and
  `RelicQuery.RunicPyramidDoesNotStrandABurnInHand` drove `DiscardHand`
  directly, which no longer plays Burn or Decay. They go through End Turn now,
  against a twin without the card.
- The three `GeneratedCards` Burn tests set the enemy to 0 HP to skip the enemy
  phase. That now ends the fight before Burn plays, so one failed and two
  passed for that reason alone. They end the fight with Combust in Phase C
  instead.
- `PlatedArmorPower.LosesAStackToUnblockedDamage` and
  `KeepsItsStackWhenFullyBlocked` hit the player with sourceless fixed damage,
  which no longer reduces Plated Armor. With it, the second passed whether or
  not the hit was blocked. Both use an enemy attack now.
- `PlatedArmorPower.IsNotModifiedByDexterity` fires the new pre-card hook.

**Mutations, all caught:**
- the `atEndOfTurn` powers before the end-of-turn cards (the first run of this
  showed the Regen test leaned on Combust's position, so it was rewritten to
  use a twin, and then it caught it on its own);
- Regret's count taken when it plays;
- Doubt and Shame without `as_source_monster`;
- the `LoseHp` reactions ungated;
- Burn's damage without the player as source;
- Plated Armor reduced before Buffer;
- Plated Armor reduced by any unblocked damage;
- Metallicize and Plated Armor back in the late group;
- the end-of-turn relics in Phase C;
- the end-of-turn cards counted as plays;
- Regret dealing damage instead of HP loss;
- Rupture fired for sourceless fixed damage.

One mutation was equivalent: clearing the card queue when the fight ends in
Phase A. `apply_action` already clears it, so the clear was removed (§3.1).

### 5.2 Steps 2 and 3, planned

**Pain**
- One HP per Pain in hand per other card played.
- A Pain played under Blue Candle does not trigger itself; another Pain does.
- Havoc triggers Pain twice: for Havoc and for the card it plays.
- Double Tap's replay triggers it.
- At 1 HP, a lethal Strike with Pain in hand loses the fight.
- With Rupture: the triggering card does not get the Strength; the next does.

**Normality and Velvet Choker**
- After 3 plays, no card is playable; potions and End Turn still are.
- Plays before Normality was drawn count.
- Exhausting Normality lifts the cap.
- Havoc as the third play: the taken card is not played and is exhausted.
- A Double Tap replay of the third play vanishes.
- The count resets next turn.
- Velvet Choker: a 6th play is legal, a 7th is not.

**Blue Candle**
- A curse is playable at no energy, exhausts, and costs 1 HP.
- It fires Rupture, counts as a play, and Strange Spoon can save it.
- Without Blue Candle a curse is still unplayable.

**Observation**
- `cards_played_this_turn` is written to its slot and reset next turn.

**Card damage at play (§3.8)**
- A hit's damage does not change when Strength changes between the play and
  the hit.
- Sword Boomerang still computes against the target each hit rolls.
- Every existing damage test still passes.

**Mutations planned** (each must be caught):
- the play count incremented for end-of-turn autoplays;
- Normality's cap applied only to the mask and not to queued plays;
- Pain triggered by its own play, or by end-of-turn autoplays;
- Pain's loss at the back of the card's queue;
- Blue Candle's curse discarded instead of exhausted.

## 6. Next: curse acquisition

The other half of task #4's relics needs a single path by which a curse enters
the deck. Omamori negates it, Darkstone Periapt gives Max HP, Calling Bell grants
Curse of the Bell plus three relics, and Cursed Key's chests give one. Du-Vu Doll
already counts curses at fight start (`action.cc`, `RelicId::DuVuDoll`) and only
needs them to arrive. This will be its own design once this one is agreed.
