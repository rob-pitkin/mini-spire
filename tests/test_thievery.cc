// Thievery: the Looter's gold theft and the rewards of a fight whose enemies
// escaped (docs/design/thievery.md).
//
// Fights are built, never sampled: a Looter is placed with make_looter, and the
// run-level tests set the fight's end state directly, since the combat tests
// cover how that state arises.

#include <gtest/gtest.h>

#include <random>

#include "action.h"
#include "card.h"
#include "combat_env.h"
#include "combat_state.h"
#include "enemy.h"
#include "relic.h"
#include "run_state.h"
#include "test_helpers.h"
#include "turn_loop.h"

using namespace minispire;
using namespace minispire::testing;

namespace {

// One Looter with plenty of HP, primed to Mug on its first turn, and the
// player holding `gold`.
CombatState looter_fight(int gold) {
  CombatState s = make_minimal_state(0);
  s.enemies.clear();
  std::mt19937 rng(0);
  Enemy looter = make_looter(rng);
  looter.hp = looter.max_hp = 200;
  s.enemies.push_back(std::move(looter));
  s.gold = gold;
  return s;
}

void drain_one(CombatState& s, const Action& a) {
  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(a);
  drain(s, q, ctx);
}

}  // namespace

// ============================================================================
// The theft (thievery.md §1)
// ============================================================================

TEST(Thievery, AMugStealsFifteenGold) {
  CombatState s = looter_fight(100);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(s.gold, 100 - kThiefGold);
  EXPECT_EQ(s.enemies[0].stolen_gold, kThiefGold);
}

// The gold goes at the start of the hit, so blocking all of it saves the HP
// and not the gold.
TEST(Thievery, AFullyBlockedMugStillSteals) {
  CombatState s = looter_fight(100);
  s.character.current_block = 50;
  const int hp = s.character.hp;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(s.character.hp, hp);
  EXPECT_EQ(s.gold, 100 - kThiefGold);
}

TEST(Thievery, ItTakesOnlyWhatThePlayerHas) {
  CombatState s = looter_fight(10);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(s.gold, 0);
  EXPECT_EQ(s.enemies[0].stolen_gold, 10);
}

// StS skips the steal for an attacker that is dying.
TEST(Thievery, ADeadThiefStealsNothing) {
  CombatState s = looter_fight(100);
  s.enemies[0].hp = 0;
  Action steal{ActionKind::StealGold};
  steal.actor = 0;
  steal.amount = kThiefGold;
  drain_one(s, steal);
  EXPECT_EQ(s.gold, 100);
  EXPECT_EQ(s.enemies[0].stolen_gold, 0);
}

// Killing the Looter moves what it carries to the fight's stolen-gold reward,
// at the death.
TEST(Thievery, KillingTheThiefReturnsItsGoldAsAReward) {
  CombatState s = looter_fight(100);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.enemies[0].stolen_gold, kThiefGold);
  s.enemies[0].hp = 1;
  s.enemies[0].current_block = 0;
  s.character.energy = 3;
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Strike, 0)));

  ASSERT_EQ(s.outcome, Outcome::Won);
  EXPECT_EQ(s.stolen_gold_reward, kThiefGold);
  EXPECT_EQ(s.enemies[0].stolen_gold, 0);
}

// An escaped thief keeps its gold, and is marked as having escaped.
TEST(Thievery, AnEscapedThiefKeepsItsGold) {
  CombatState s = looter_fight(100);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  Action escape{ActionKind::EnemyEscape};
  escape.target = 0;
  drain_one(s, escape);
  EXPECT_TRUE(s.enemies[0].escaped);
  EXPECT_EQ(s.stolen_gold_reward, 0);
  EXPECT_EQ(s.gold, 100 - kThiefGold);
}

// One gold number: Hand of Greed's kill gold can be stolen by the next Mug.
TEST(Thievery, HandOfGreedsGoldCanBeStolenNextTurn) {
  CombatState s = looter_fight(0);
  std::mt19937 rng(0);
  Enemy worm = make_jaw_worm(rng);
  worm.hp = 1;
  worm.current_block = 0;
  s.enemies.push_back(std::move(worm));
  s.character.energy = 3;
  s.current_hand.push_back(Card{CardId::HandOfGreed});

  ASSERT_TRUE(apply_action(s, card_action(CardId::HandOfGreed, 1)));
  ASSERT_EQ(s.gold, 20);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));

  EXPECT_EQ(s.gold, 20 - kThiefGold);
}

TEST(Thievery, TheObservationCarriesGold) {
  CombatState s = looter_fight(100);
  CombatEnv env(s);
  EXPECT_EQ(env.obs()[CombatEnv::kGoldSlot], 100.0f);
  env.step(kEndTurnAction);
  EXPECT_EQ(env.obs()[CombatEnv::kGoldSlot], 100.0f - kThiefGold);
}

// ============================================================================
// The rewards (thievery.md §1, §3 5)
// ============================================================================

namespace {

// A run in a normal fight, lent 100 gold, about to end it.
RunState run_in_fight(std::vector<RelicId> relics = {}) {
  RunState run = RunState::start(7);
  run.gold = 100;
  for (RelicId id : relics) run.relics.push_back(HeldRelic{id, 0});
  run.begin_combat(EncounterPool::Weak);
  return run;
}

// The fight's end state after a thief took `stolen` and was then killed.
void thief_took_and_died(RunState& run, int stolen) {
  run.combat.gold -= stolen;
  run.combat.stolen_gold_reward = stolen;
}

// Every enemy in the fight left by escaping.
void all_escaped(RunState& run) {
  for (Enemy& e : run.combat.enemies) {
    e.hp = 0;
    e.escaped = true;
  }
}

}  // namespace

TEST(ThieveryRewards, AKilledThiefsGoldComesBack) {
  RunState control = run_in_fight();
  control.end_combat();
  RunState run = run_in_fight();
  thief_took_and_died(run, kThiefGold);
  run.end_combat();
  EXPECT_EQ(run.gold, control.gold);
}

TEST(ThieveryRewards, EctoplasmRefusesTheStolenGold) {
  RunState run = run_in_fight({RelicId::Ectoplasm});
  thief_took_and_died(run, kThiefGold);
  run.end_combat();
  EXPECT_EQ(run.gold, 100 - kThiefGold);
}

// applyGoldBonus(theft) skips Golden Idol; the fight's own gold still gets it.
TEST(ThieveryRewards, GoldenIdolAddsNothingToStolenGold) {
  RunState control = run_in_fight({RelicId::GoldenIdol});
  control.end_combat();
  RunState run = run_in_fight({RelicId::GoldenIdol});
  thief_took_and_died(run, kThiefGold);
  run.end_combat();
  EXPECT_EQ(run.gold, control.gold);
}

// A Smoke Bomb's screen sets up no rewards, the stolen gold included.
TEST(ThieveryRewards, ASmokeBombEscapeLosesTheStolenGold) {
  RunState run = run_in_fight();
  thief_took_and_died(run, kThiefGold);
  run.combat.outcome = Outcome::Escaped;
  run.end_combat();
  EXPECT_EQ(run.gold, 100 - kThiefGold);
}

// Every enemy escaped: no combat gold, the potion rolled at 0 (a miss, so it
// drifts up 10), and the card reward still given. The drift of 60 makes the
// chance 100 without the rule, so every roll would drop a potion.
TEST(ThieveryRewards, ANormalFightWhoseEnemiesAllEscapedPaysNoGoldOrPotion) {
  RunState run = run_in_fight();
  run.potion_chance_bonus = 60;
  all_escaped(run);
  run.end_combat();
  EXPECT_EQ(run.gold, 100);
  EXPECT_EQ(run.potion_chance_bonus, 70);
  EXPECT_TRUE(run.potions.empty());
  EXPECT_TRUE(run.pending_potion_rewards.empty());
  EXPECT_FALSE(run.card_reward.empty());
}

// The escaped fight's 0 is the base chance, and White Beast Statue's 100 is
// applied after it (AbstractRoom.addPotionToRewards), so the statue still
// drops a potion. The gold stays unpaid.
TEST(ThieveryRewards, WhiteBeastStatueStillDropsWhenEveryEnemyEscaped) {
  RunState run = run_in_fight({RelicId::WhiteBeastStatue});
  const int drift = run.potion_chance_bonus;
  all_escaped(run);
  run.end_combat();
  EXPECT_EQ(run.gold, 100);
  EXPECT_EQ(run.potion_chance_bonus, drift - 10);
  EXPECT_FALSE(run.potions.empty() && run.pending_potion_rewards.empty());
}

// "All": one enemy that did not escape is enough for the fight to pay.
TEST(ThieveryRewards, OneEnemyThatDidNotEscapeIsEnoughToPay) {
  RunState run = run_in_fight();
  all_escaped(run);
  std::mt19937 rng(0);
  Enemy dead = make_jaw_worm(rng);
  dead.hp = 0;
  run.combat.enemies.push_back(std::move(dead));
  run.end_combat();
  EXPECT_GT(run.gold, 100);
}

// An elite room takes its own reward branch, so escapes do not touch its gold.
TEST(ThieveryRewards, AnEliteFightIsUnaffected) {
  RunState run = run_in_fight();
  run.combat_source = RewardSource::Elite;
  all_escaped(run);
  run.end_combat();
  EXPECT_GT(run.gold, 100);
}

// The stolen-gold reward is on the room's list when the potion rolls, so it
// counts toward the four that suppress the drop. An elite with Black Star shows
// gold and two relics; White Beast Statue makes the chance 100; the stolen gold
// is the fourth reward, and only that stops the drop.
TEST(ThieveryRewards, StolenGoldCountsTowardTheFourRewardLimit) {
  auto finish = [](bool with_theft) {
    RunState run = run_in_fight({RelicId::BlackStar, RelicId::WhiteBeastStatue});
    run.combat_source = RewardSource::Elite;
    if (with_theft) thief_took_and_died(run, kThiefGold);
    run.end_combat();
    return run;
  };
  const RunState without = finish(false);
  const RunState with = finish(true);
  ASSERT_FALSE(without.potions.empty() && without.pending_potion_rewards.empty())
      << "three rewards and White Beast Statue should drop a potion";
  EXPECT_TRUE(with.potions.empty());
  EXPECT_TRUE(with.pending_potion_rewards.empty());
}
