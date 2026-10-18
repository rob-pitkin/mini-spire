# Card queue — design (task #41)

**Status:** agreed with Rob, 2026-10-10 (§4 rulings). Built in the commit that
adds this doc.

The engine plays every card inline: a card that plays another card (Havoc, a
Double Tap replay) pushes a `PlayCard` action into its own resolution, and that
action resolves the second card on the spot. StS instead holds such cards in a
**card queue** that is only read once every pending action has run. Four
behaviours differ because of it (§2). This doc proposes a card queue on
`CombatState` (§3) and lists what needs a ruling (§4).

## 0. Sources

Decompiled StS1 (`aeubanks/sts`): `actions/GameActionManager` (`getNextAction`,
`addCardQueueItem`, the turn-start block), `actions/common/PlayTopCardAction`,
`actions/utility/NewQueueCardAction`, `actions/utility/UseCardAction`,
`powers/MayhemPower`, `powers/DoubleTapPower`, `powers/DuplicationPower`,
`cards/AbstractCard` (`canUse`, `cardPlayable`). Wiki.gg Mayhem page, checked
2026-10-10.

## 1. What StS does

**Order of work.** `GameActionManager.getNextAction` takes, in priority order:
the next action; else the next pre-turn action; else the next card in
`cardQueue`; else the next monster. So a queued card is played only when no
action is pending.

**Playing a card**, whether the player's or an autoplayed one:
1. `onPlayCard` hooks fire (powers, monsters' powers, relics, cards in piles).
2. `useCard` runs the card's `use()`, which QUEUES its actions, then queues a
   `UseCardAction`. That constructor fires the `onUseCard` hooks on the spot.
3. Double Tap and Duplication answer in `onUseCard`. Each spends a charge and
   calls `addCardQueueItem(copy, inFrontOfQueue = true)`, which inserts at index
   1: right behind the card being played, which still sits at index 0.
4. The card's queue item is removed. Its actions now run to the end before the
   next card in the queue is looked at.

**Taking a card off the draw pile.** Havoc, Mayhem and Distilled Chaos queue a
`PlayTopCardAction(target, exhausts)`. When that ACTION runs, it removes the top
card, reshuffling first if the draw pile is empty. `NewQueueCardAction` then
appends the card to the BACK of the card queue. The card is in limbo, in no pile,
until it is played.

**Mayhem.** `MayhemPower.atStartOfTurn` queues one wrapper action per stack.
When a wrapper runs, it rolls the target and queues the `PlayTopCardAction` at
the bottom. The turn-start block queues the draw and the post-draw relic and
power actions synchronously, right after `applyStartOfTurnPowers`, so they are
already in the queue by then. The take therefore comes after all of them. The
wiki agrees: "Mayhem activates *after* drawing cards at the start of the turn".

## 2. What the engine gets wrong today

1. **Mayhem plays before the turn's draw**, so it plays the card that was on top
   when the turn began. StS plays the card on top after the draw and the
   post-draw draws (Pocketwatch). `colorless-effects.md` claimed the opposite;
   it now carries a correction.
2. **Consecutive top-of-draw plays take their cards one at a time.** Distilled
   Chaos and Mayhem with 2+ stacks run their `PlayTopCardAction`s back to back,
   so StS has every card off the pile before the first is played. The engine
   plays each before taking the next. It differs when a played card draws
   (Pommel Strike draws the next card meant for Distilled Chaos) or shuffles
   (Deep Breath).
3. **Havoc with Duplication.** In StS, Havoc's copy goes to the front of the card
   queue and the card Havoc took goes to the back. So the copy runs first,
   rolling its target and taking its own top card before the first card is
   played. The engine plays the first card to the end before the copy starts.
4. **A replay runs before the original's later actions.** The engine queues
   the replay at translation time, inside the original's resolution. So it runs
   before actions that the original's triggers queue during that resolution,
   and before the late effects (Reaper's heal, Feed, Battle Trance's No Draw).
   In StS those are all actions, and actions run before the next card. Example:
   Battle Trance under Duplication. StS applies the first play's No Draw before
   the replay, so the replay draws nothing: 3 cards in all. The engine draws 6.

## 3. Proposed model

**State.** `CombatState::card_queue`, a fixed-capacity deque of POD entries, so
`clone()` stays a plain copy:

| field | meaning |
|---|---|
| `Card instance` | the copy to play, carrying its instance state |
| `int target` | enemy slot, or `kNoSlot` |
| `bool replay` | a Double Tap / Duplication copy: StS's `purgeOnUse`, so it enters no pile and is never replayed |
| `bool exhausts` | Havoc's `exhaustOnUseOnce` |
| `int x` | the raw X it plays at: a replay's is the original's, a taken card's is the energy when it was taken (§3.2). Chemical X is added at play |

Capacity 16, asserted. The largest realistic load: Distilled Chaos under Sacred
Bark (6), plus Mayhem stacks, plus replays.

**Entering the queue.**
- A replay is pushed to the FRONT, as its charge is spent (T1). The card being
  played is not in the engine's queue while it resolves, so StS's "index 1" is
  the front.
- A top-of-draw play becomes a `TakeTopCard` action. When it runs, it takes the
  top card (reshuffling first if needed) and pushes it to the BACK, with the
  target rolled when the action was queued (T3, T4).

**Leaving the queue.** `play_queued_cards(state)` runs after a top-level
resolution has drained: a card played from hand, a potion, a choice's answer,
the turn start. While the queue is non-empty, no choice is pending and the fight
is on, it pops the front entry and resolves it. Resolving applies T5's
`canUse`, then either plays the card through `handle_play_card` or makes only
its pile move. Every card is resolved this way, never inline.

**Choices.** A queued card can open a choice (Havoc playing Armaments). The
resolution pauses as today; the rest of the queue stays on `CombatState` and is
played after the answer. This is the one case where the agent acts while cards
are queued (§4 Q1).

**Mayhem.** Its start-of-turn hook queues one `MayhemRoll` per stack. When that
runs, it rolls the target and queues the take at the back. To put the take
behind the post-draw hooks' actions, turns 2+ build the queue the way StS's
turn-start block does: relic hooks, then power hooks (Q2), then the draw, then
the post-draw relic hooks, fired as the queue is built rather than from a
`TurnStartPostDraw` action that runs after the draw. StS fires them at that
point too (`applyStartOfTurnPostDrawRelics`, line 351), and their actions still
resolve after the draw because they are queued behind it. With the hook as an
action, its actions landed behind Mayhem's take. They also landed behind
anything the draw itself triggered (Evolve, Fire Breathing), where StS puts
them in front. Pinned by a test with Pocketwatch. Turn 1 keeps
`CombatStartPostDraw`: no power is up at the start of a fight, and Toolbox's
pause needs the opening draw to be an action.

**Fight end.** The queue is cleared, as a pending choice already is.

**Removed.** The `PlayCard` action and its three modes. Card plays from inside a
resolution all go through the queue.

### 3.1 A replay shares its original's identity (#44, Rob 2026-10-12)

StS builds a replay with `makeSameInstanceOf`, so it shares the original's
uuid. Rampage's `ModifyDamageAction(uuid)` reaches every in-battle instance with
that uuid (`GetAllInBattleInstances`: every pile and limbo):
- the replay, while it waits in limbo, takes the original's growth: Rampage +
  Double Tap plays 8 then 13;
- the replay's own growth reaches the original in its pile, which ends at 18;
- with Double Tap and Duplication, the second replay takes both earlier
  growths: it plays at 18, and the original ends at 23.

The engine has no uuid. A replay is built from the grown copy, and it carries a
lineage number shared with its siblings, plus the original's identity (id,
bonus, upgrades) and the pile it went to. When a replay grows, it grows one copy
of that identity, looking in that pile first and then the others in case a
reshuffle moved it. It also grows every queued sibling. Identical copies are
interchangeable, so growing a copy that is not the original cannot be seen.
Rampage is the only card in the pool that grows itself.

### 3.2 An autoplayed X-cost card's X (#42, Rob 2026-10-17)

A card Havoc, Mayhem or Distilled Chaos plays has X = the player's energy when
the card is taken off the draw pile, and it spends no energy. Chemical X adds 2,
and a replay of it uses the same X.

The decompiled chain, read to the end:
- `PlayTopCardAction` queues `NewQueueCardAction(card, target, false,
  autoplay = true)`. When that runs, it builds the `CardQueueItem` with
  `energyOnUse = EnergyPanel.getCurrentEnergy()`. The reading is taken here, as
  the card enters the card queue, not when it is played.
- `GameActionManager` (198, 208–209) copies `energyOnUse` onto the card and sets
  `ignoreEnergyOnUse` for an autoplayed card, so `AbstractPlayer.useCard` (1345)
  does not lower it to the energy left at play time.
- `useCard` (1348) sets `freeToPlayOnce` on an autoplayed X-cost card.
- `WhirlwindAction` and `TransmutationAction` set `effect = energyOnUse`, add 2
  for Chemical X, and spend energy only when not `freeToPlayOnce`.
- `DoubleTapPower` and `DuplicationPower` queue the replay with the card's
  `energyOnUse`, so the replay of an autoplayed card has the same X.
- Energy is refilled in `DrawCardAction`'s constructor (`PlayerTurnEffect`), at
  the turn start, so Mayhem's take, which runs after the draw, reads the turn's
  full energy.

Wiki.gg agrees. The Mayhem and Havoc pages: "X-cost cards will use your current
Energy for X, without consuming that Energy." The Whirlwind page: "it will play
for the full current Energy without actually using any." Checked 2026-10-17.

The time of the reading shows only when an earlier card in the same batch gains
energy. Distilled Chaos takes [Bloodletting, Whirlwind, Defend] at 3 energy:
Bloodletting raises energy to 5, and Whirlwind still hits 3 times. Batches
like this come from Distilled Chaos, Mayhem with 2+ stacks, and Havoc with
Duplication.

The engine records the energy on the `QueuedPlay` when `TakeTopCard` runs, in
the field that already held a replay's X (`x`, formerly `replay_x`), and
`resolve_queued_play` plays every queued X-cost card at that X.

The X = 0 this replaces had no source. `relic-effects.md` §6.21 attributed it to
the wiki, misreading the Chemical X page's "no Energy is spent on them", which
is about what the card costs, not about X. No test pinned it.

Tests, in `tests/test_card_queue.cc`, each with Whirlwind, so the rolled target
never changes the damage: Havoc at 3 energy plays it at X = 2 and leaves energy
at 2; Distilled Chaos with Bloodletting ahead of it hits 3 times, not 5; a
Double Tap replay of it hits twice more; Chemical X adds 2; Mayhem plays it at
the turn's refilled 3 after the turn ended at 0. Mutation checks, all caught:
X = 0 restored (five tests), X read at play time (the Bloodletting test alone),
the taken card spending its energy, and a taken card's replay at X = 0.

## 4. Rulings (Rob, 2026-10-10)

| # | ruling |
|---|---|
| Q1 | Accept for now: queued cards in limbo are not observed during a choice. Logged as an observation-gap task, to review with #9. |
| Q2 | Match StS: turn-start relic hooks fire before power hooks. |
| Q3 | The behaviour changes to Double Tap, Havoc and Mayhem are wanted. |
| Q4 | X stays 0 for autoplayed X-cost cards until #42. Superseded by §3.2: X is the energy when the card is taken. |

The questions as they were put:

**Q1. What the agent sees during a choice opened by a queued card.** The other
queued cards are in limbo, in no pile, so they drop out of every pile plane. A
human sees them waiting on screen. Options:
- (a) A `queued` plane, a count per card type (+270 floats).
- (b) Accept it and log an observation-gap task. It needs 2+ queued cards with a
  choice card among them: Distilled Chaos, Mayhem 2+, or Havoc + Duplication.

Recommend (b) for now. The case is narrow, and the v2 observation (#9) is due a
review anyway.

**Q2. Turn-start order.** StS runs the relics' `atTurnStart` before the powers'
`atStartOfTurn` (`GameActionManager` 331 then 334). The engine runs powers
first. Once Mayhem rolls when its wrapper runs, this becomes observable: Mercury
Hourglass can kill an enemy before the roll, and StS's roll would skip it.
Recommend aligning to StS here. It is the same block of code, and powers-first
has no source behind it.

**Q3. Behaviour changes to existing cards**, all toward StS, with tests updated
to match: Double Tap's replay moves behind the original's triggered and late
actions (§2.4), Havoc's card waits in the queue, and Mayhem moves after the draw.
Confirm these are wanted rather than kept as they are.

**Q4. X-cost cards played from the queue** keep X = 0 until #42 is settled.

## 5. Tests

`tests/test_card_queue.cc`, every fight built:
- Mayhem takes its card after the turn's draw and Pocketwatch's extra draw.
- Mayhem rolls its target after Mercury Hourglass's damage, for every seed (Q2).
- Distilled Chaos takes all p cards before playing any: the Pommel Strike played
  first draws the card below the third.
- Havoc + Duplication: the copy takes its card before the first card is played.
- A replay goes in FRONT of cards already waiting: Duplication on the first of
  Distilled Chaos's cards plays before the Bash behind it.
- Duplication + Battle Trance draws 3.
- A queued Armaments pauses for its choice. The rest of the queue plays after
  the answer, and a clone taken during the pause resumes identically.
- A fight that ends with cards waiting clears them.

`Colorless.MayhemPlaysTheTopCardAfterTheTurnsDraw` replaced the test that pinned
the old pre-draw timing. Every other Double Tap, Havoc and Mayhem test passed
unchanged.

Mutation checks, all caught: a replay at the back of the queue, powers before
relics at the turn start, Mayhem taking its card at the hook, a taken card
played at once instead of queued, and post-draw hooks fired after the draw.

Mutation testing changed the suite. The back-of-queue mutation first passed
every test. Havoc's copy is queued as Havoc is translated, before its take runs,
so it is first either way, as in StS. Front against back only shows when cards
are already waiting, which is why the front-of-queue test was added.
