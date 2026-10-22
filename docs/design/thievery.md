# Thievery — design (task #49)

**Status:** AGREED (Rob, 2026-10-21): L1–L4 as recommended (§4). Built as
designed; tests and mutations in §5.

The Looter steals gold when it attacks, gives it back if it dies, and keeps it if
it escapes. The engine models its moves and its escape, but not the gold
(`enemy.cc`, `make_thief`). Three reward rules come with it. The Mugger shares
the code but is an Act 2 enemy, outside the Act 1 pools.

## 1. What StS does

Sources: decompiled `monsters/exordium/Looter`, `powers/ThieveryPower`,
`actions/common/DamageAction` (`stealGold`), `rooms/AbstractRoom` (`update`'s
reward block, lines 292–331; `addPotionToRewards`; `addStolenGoldToRewards`),
`monsters/MonsterGroup.haveMonstersEscaped`, `rewards/RewardItem`
(`STOLEN_GOLD`, `applyGoldBonus`).

**The steal.** Mug (10) and Lunge (12) each queue two things: an action that adds
`min(15, player.gold)` to the Looter's `stolenGold`, then a
`DamageAction(player, info, 15)`. When that `DamageAction` starts, before any
damage, `stealGold` takes `min(15, player.gold)` from the player. So the gold
goes whether or not the hit is blocked. The steal is skipped only if the Looter
is dying when its hit starts. Smoke Bomb (6 block) and Escape steal nothing.

**Gold back on death.** `Looter.die()` puts `stolenGold` on the room's rewards as
a `STOLEN_GOLD` item, merged into one item if one already exists. It is claimed
through `player.gainGold`, so Ectoplasm refuses it, and `applyGoldBonus(theft)`
gives it no Golden Idol bonus. It is on the rewards list during the fight, so it
counts toward the potion drop's "four rewards" suppression.

**Gold gone on escape.** An escaped Looter keeps what it took.
`room.mugged = true` only changes the reward screen's caption.

**A normal fight where every monster escaped** (`haveMonstersEscaped`: all of
them, not just one):
- no gold reward (the `instanceof MonsterRoom && !haveMonstersEscaped()` branch);
- a base potion chance of 0 (`addPotionToRewards`), but the roll is still
  taken, so it misses and drifts +10. The 0 is only the base: White Beast
  Statue's `chance = 100` comes after it, so the statue still drops a potion,
  and the four-reward check comes last;
- the card reward is still given.

An elite room takes the elite branch first, so none of this applies there. No
Act 1 elite escapes anyway. In Act 1 only a lone Looter (the `Looter` encounter)
can make every monster escape: in Exordium Thugs its partner, a louse or a
medium slime, cannot flee.

## 2. What the engine had (before #49)

- `make_thief`'s moves match StS's script, including the 50% at the third turn.
- `EnemyEscape` sets the enemy's HP to 0, and the fight is won when no enemy is
  left. Nothing records that it escaped rather than died.
- **Combat owns no gold.** Hand of Greed records `gold_gained` and `end_combat`
  pays it through `gain_gold` (`colorless-effects.md` D5, Rob 2026-09-15). A
  theft needs the player's current gold, which combat does not have.
- `end_combat` pays `award_combat_gold` for every won fight that was not
  smoked, and `roll_potion_drop(on_screen, …)` counts 1 for the gold plus the
  elite's relics.

## 3. Design

1. **Combat holds the player's gold** (L1): `CombatSetup::gold`, set by the run
   at fight start and handed back in `end_combat`, as `potion_rng` is.
   Hand of Greed adds to it directly, unless Ectoplasm is held. That replaces
   D5's `gold_gained`, and keeps D5's Ectoplasm guarantee. A standalone
   `CombatEnv` starts at 0 unless its setup says otherwise.
2. **A move can steal.** `Move::steals_gold` (15 on Mug and Lunge). Translating
   the move pushes `StealGold` right before each hit. It runs before the hit, so
   block does not matter; it does nothing if the thief is dead. It takes
   `min(15, gold)` and adds it to the thief's `Enemy::stolen_gold`.
3. **Death returns it.** The `CheckDeath` executor, which every death passes
   through, moves a dead thief's `stolen_gold` into
   `CombatState::stolen_gold_reward`. Recorded at death, not read at fight end,
   because a dead enemy's slot can be reused by a split child.
4. **Escape marks it.** `EnemyEscape` also sets `Enemy::escaped`.
5. **`end_combat`:**
   - a stolen-gold reward, if any, is paid through `gain_gold`: Ectoplasm
     applies, Golden Idol does not. It counts 1 toward `on_screen`.
   - a normal fight whose enemies all escaped pays no combat gold and rolls the
     potion from a base chance of 0, which White Beast Statue still overrides.
     The card reward is unchanged.

   > **Corrected 2026-10-21, before commit.** This line first said "at chance
   > 0", and the code applied that 0 after White Beast Statue, so the statue
   > dropped nothing in an escaped fight. The decompiled order (§1) sets it as
   > the base. Found while writing the formula into `v2-spec.md` §4.2.

**A rule Act 1 cannot reach.** The stolen gold's place in the potion's
four-reward count cannot change a result in Act 1: a normal fight shows at most
the combat gold and the stolen gold, two items, and no Act 1 elite has a thief.
It is built for parity. The test that pins it builds a state the Act 1 game
cannot produce, an elite fight with stolen gold, Black Star and White Beast
Statue, because four items are needed before the count matters.

## 4. Questions for Rob

**Ruled 2026-10-21: L1–L4 agreed as recommended.** L4 took option (a): gold is
`CombatEnv::kGoldSlot`, and the player block is 9 floats.

**L1. Combat holds the gold (§3 1), retiring D5's record-and-write-back?**
Recommend yes. The theft needs the current gold, and StS has one number: Hand
of Greed's kill gold can be stolen on the next Mug. The alternative keeps D5 and
computes "available" as start + gained − stolen, with Ectoplasm folded into
`gained`. That gives the same result with two counters where one would do.

**L2. The stolen-gold reward is collected automatically,** as combat gold already
is. Recommend yes.

**L3. The all-escaped rules (§1, §3 5).** These come straight from the source;
listed so the change in rewards is not a surprise. Recommend as described.

**L4. Observing gold.** A human sees gold in the top bar during a fight, and a
theft now changes it there. The combat observation has no gold, and the run
observation is not settled (#9). Options: (a) add `gold` to the player block now
(8 → 9 floats); (b) leave it to #9. Recommend (a): it is visible, it now moves
mid-fight, and whether to kill a Looter before it escapes depends on it. The
Looter's own tally is not shown in StS, so it stays out.

## 5. Tests and mutations

Every fight is built, never sampled: the Looter comes from `make_looter`, and
the run-level tests set the fight's end state directly. Two tests differ from
the plan. The Exordium Thugs case is built as one escaped enemy plus one dead
Jaw Worm, since the encounter's makeup is not what is under test. The
four-reward count is pinned by a drop rather than by the count passed to the
roll (§3, "A rule Act 1 cannot reach").

`tests/test_thievery.cc`, 17 tests:

| test | covers |
|---|---|
| `Thievery.AMugStealsFifteenGold` | §3 2 |
| `Thievery.AFullyBlockedMugStillSteals` | the steal precedes the damage |
| `Thievery.ItTakesOnlyWhatThePlayerHas` | `min(15, gold)` |
| `Thievery.ADeadThiefStealsNothing` | the `isDying` skip |
| `Thievery.KillingTheThiefReturnsItsGoldAsAReward` | §3 3, moved at the death |
| `Thievery.AnEscapedThiefKeepsItsGold` | §3 4 |
| `Thievery.HandOfGreedsGoldCanBeStolenNextTurn` | L1: one gold number |
| `Thievery.TheObservationCarriesGold` | L4 |
| `ThieveryRewards.AKilledThiefsGoldComesBack` | §3 5 |
| `ThieveryRewards.EctoplasmRefusesTheStolenGold` | `gainGold` |
| `ThieveryRewards.GoldenIdolAddsNothingToStolenGold` | `applyGoldBonus(theft)` |
| `ThieveryRewards.ASmokeBombEscapeLosesTheStolenGold` | no rewards when smoked |
| `ThieveryRewards.ANormalFightWhoseEnemiesAllEscapedPaysNoGoldOrPotion` | L3: no gold, base chance 0 with drift +10, card reward kept; a drift of 60 makes the chance 100 without the rule |
| `ThieveryRewards.WhiteBeastStatueStillDropsWhenEveryEnemyEscaped` | the statue's 100 is applied after the base 0 |
| `ThieveryRewards.OneEnemyThatDidNotEscapeIsEnoughToPay` | "all", not "any" |
| `ThieveryRewards.AnEliteFightIsUnaffected` | the elite branch |
| `ThieveryRewards.StolenGoldCountsTowardTheFourRewardLimit` | the four-reward count |

Elsewhere: `Colorless.HandOfGreedGainsNothingUnderEctoplasm` (D5's guarantee,
now in the fight), and `RunState.TheFightIsLentTheRunsGoldAndHandsItBack` and
`RunState.GoldLostInTheFightStaysLost` (the lend and the write-back).

Each mutation below was applied alone, built, run, and reverted.

| # | mutation | caught by |
|---|---|---|
| T1 | `StealGold` ignores a dead thief | `ADeadThiefStealsNothing` |
| T2 | `CheckDeath` does not move `stolen_gold` | `KillingTheThiefReturnsItsGoldAsAReward` |
| T3 | `EnemyEscape` does not set `escaped` | `AnEscapedThiefKeepsItsGold` |
| T4 | stolen gold paid after a Smoke Bomb | `ASmokeBombEscapeLosesTheStolenGold` |
| T5 | stolen gold paid through the Golden Idol bonus | `GoldenIdolAddsNothingToStolenGold` |
| T6 | `all_escaped` ignored | `ANormalFightWhoseEnemiesAllEscapedPaysNoGoldOrPotion` |
| T7 | `all_of` → `any_of` | `OneEnemyThatDidNotEscapeIsEnoughToPay` |
| T8 | `all_escaped` applied to elites | `AnEliteFightIsUnaffected` |
| T9 | stolen gold left out of the potion's reward count | `StolenGoldCountsTowardTheFourRewardLimit` |
| T10 | Hand of Greed ignores Ectoplasm | `HandOfGreedGainsNothingUnderEctoplasm` |
| T11 | the gold slot not written | `TheObservationCarriesGold` |
| T12 | `begin_combat` does not lend the gold | 7 tests, `TheFightIsLentTheRunsGoldAndHandsItBack` among them |
| T13 | the escape's 0 applied after White Beast Statue (the first build) | `WhiteBeastStatueStillDropsWhenEveryEnemyEscaped` |
| T6b | gold rule kept, potion rule dropped | `ANormalFightWhoseEnemiesAllEscapedPaysNoGoldOrPotion`, on its potion assertions alone |
