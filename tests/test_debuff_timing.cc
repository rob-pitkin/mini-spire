// When the player's debuffs and Intangible tick (docs/design/debuff-timing.md):
// Vulnerable, Weak, Frail, No Block, Intangible and Duplication at the end of
// the ROUND, after the enemies act; one an enemy has just applied skips that
// tick. The old end-of-turn timing showed the same counts at every decision
// point and differed only DURING the enemy phase, so these tests measure the
// damage the enemies deal there.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "action.h"
#include "enemy.h"
#include "encounter.h"
#include "test_helpers.h"
#include "turn_loop.h"

namespace minispire {
namespace {

using testing::card_action;
using testing::make_minimal_state;

// Queue `move` as an enemy's next intent, overriding its Markov chain.
void set_intent(Enemy& e, MoveName move) {
  e.last_move = move;
  e.consecutive_count = 1;
}

// End the turn and return the HP the enemy phase cost. No block, so every
// point lands.
int hp_lost_to_end_turn(CombatState& s) {
  const int hp0 = s.character.hp;
  EXPECT_TRUE(apply_action(s, kEndTurnAction));
  return hp0 - s.character.hp;
}

TEST(DebuffTiming, IntangibleProtectsTheEnemyPhaseOfItsTurn) {
  CombatState s = make_minimal_state(0);
  set_intent(s.enemies[0], MoveName::Chomp);  // 11
  s.character.powers[Power::Intangible] = 1;
  EXPECT_EQ(hp_lost_to_end_turn(s), 1);
  EXPECT_EQ(get_status(s.character.powers, Power::Intangible), 0)
      << "gone at the end of that round";
}

TEST(DebuffTiming, IncenseBurnersIntangibleProtectsTheTurnItFires) {
  // Turn 2 is the sixth turn the counter has seen: the relic grants
  // Intangible 1 at its start, and the enemy phase that follows is reduced.
  CombatState s = make_minimal_state(0);
  s.relics.push_back(HeldRelic{RelicId::IncenseBurner, kIncenseBurnerTurns - 1});
  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));  // into turn 2: it fires
  ASSERT_EQ(get_status(s.character.powers, Power::Intangible), 1);
  s.character.current_block = 0;
  set_intent(s.enemies[0], MoveName::Chomp);
  EXPECT_EQ(hp_lost_to_end_turn(s), 1);
}

TEST(DebuffTiming, SkullBashsVulnerableCoversTheNextTwoEnemyPhases) {
  // Applied on the enemy turn, so just applied: the end of that round skips
  // it, and Vulnerable 2 is up for two more enemy phases.
  CombatState s = make_minimal_state(0);
  s.character.hp = s.character.max_hp = 200;
  s.enemies.clear();
  std::mt19937 rng(0);
  s.enemies.push_back(make_gremlin_nob(rng));
  set_intent(s.enemies[0], MoveName::SkullBash);
  hp_lost_to_end_turn(s);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Vulnerable), 2)
      << "the round it was applied did not count";

  set_intent(s.enemies[0], MoveName::Rush);  // 14
  EXPECT_EQ(hp_lost_to_end_turn(s), 21);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Vulnerable), 1);

  set_intent(s.enemies[0], MoveName::Rush);
  EXPECT_EQ(hp_lost_to_end_turn(s), 21) << "the second enemy phase";
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Vulnerable), 0);

  set_intent(s.enemies[0], MoveName::Rush);
  EXPECT_EQ(hp_lost_to_end_turn(s), 14);
}

TEST(DebuffTiming, SporeCloudsVulnerableCoversThisAndTheNextEnemyPhase) {
  // Applied on the PLAYER's turn, so not just applied — VulnerablePower sets
  // the flag only after the turn has ended. It is up for this round's enemy
  // phase and the next.
  CombatState s = make_minimal_state(0);
  s.enemies.clear();
  std::mt19937 rng(0);
  s.enemies.push_back(make_fungi_beast(rng));
  s.enemies.push_back(make_jaw_worm(rng));
  s.enemies[0].hp = 1;
  s.enemies[1].hp = 100;
  s.current_hand = {Card{CardId::Strike}};
  ASSERT_TRUE(apply_action(s, card_action(CardId::Strike, 0)));
  ASSERT_LE(s.enemies[0].hp, 0);
  ASSERT_EQ(get_status(s.character.debuffs, Debuff::Vulnerable), 2);

  set_intent(s.enemies[1], MoveName::Chomp);  // 11
  EXPECT_EQ(hp_lost_to_end_turn(s), 16);
  set_intent(s.enemies[1], MoveName::Chomp);
  EXPECT_EQ(hp_lost_to_end_turn(s), 16) << "the next enemy phase";
  set_intent(s.enemies[1], MoveName::Chomp);
  EXPECT_EQ(hp_lost_to_end_turn(s), 11);
}

TEST(DebuffTiming, SporeCloudOnTheEnemyTurnIsJustApplied) {
  // The Fungi Beast bites into Thorns and dies on the ENEMY turn, so its Spore
  // Cloud's Vulnerable is just applied: the Jaw Worm acting after it in the
  // same phase already hits for +50%, and so do the next two enemy phases.
  // The triggered effect has to record the Fungi Beast as its source.
  CombatState s = make_minimal_state(0);
  s.character.hp = s.character.max_hp = 200;
  s.character.powers[Power::Thorns] = 5;
  s.enemies.clear();
  std::mt19937 rng(0);
  s.enemies.push_back(make_fungi_beast(rng));
  s.enemies.push_back(make_jaw_worm(rng));
  s.enemies[0].hp = 3;
  s.enemies[1].hp = 100;
  set_intent(s.enemies[0], MoveName::Bite);   // 6
  set_intent(s.enemies[1], MoveName::Chomp);  // 11
  EXPECT_EQ(hp_lost_to_end_turn(s), 6 + 16);
  ASSERT_LE(s.enemies[0].hp, 0);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Vulnerable), 2);

  set_intent(s.enemies[1], MoveName::Chomp);
  EXPECT_EQ(hp_lost_to_end_turn(s), 16);
  set_intent(s.enemies[1], MoveName::Chomp);
  EXPECT_EQ(hp_lost_to_end_turn(s), 16) << "the second phase after";
  set_intent(s.enemies[1], MoveName::Chomp);
  EXPECT_EQ(hp_lost_to_end_turn(s), 11);
}

TEST(DebuffTiming, StackingOntoAHeldVulnerableKeepsItsTick) {
  // ApplyPowerAction stacks onto the power already held and drops the new
  // one, so the flag stays as it was: the held Vulnerable 1 was not just
  // applied, and Skull Bash's 2 joins it. 3, minus this round's tick, is 2.
  CombatState s = make_minimal_state(0);
  s.character.hp = s.character.max_hp = 200;
  s.enemies.clear();
  std::mt19937 rng(0);
  s.enemies.push_back(make_gremlin_nob(rng));
  s.character.debuffs[Debuff::Vulnerable] = 1;
  set_intent(s.enemies[0], MoveName::SkullBash);
  hp_lost_to_end_turn(s);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Vulnerable), 2);
}

TEST(DebuffTiming, WeakFromAnEnemyStillLastsItsCount) {
  // Weak is just applied too, and matters only on the player's turns, where
  // the skipped tick and the later one cancel out: Weak 1 from Lick is up for
  // the next player turn and gone after it — as before.
  CombatState s = make_minimal_state(0);
  s.enemies.clear();
  std::mt19937 rng(0);
  s.enemies.push_back(make_acid_slime_m(rng));
  set_intent(s.enemies[0], MoveName::Lick);
  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Weak), 1);
  set_intent(s.enemies[0], MoveName::Tackle);
  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Weak), 0);
}

TEST(DebuffTiming, NoJustAppliedFlagIsSetAtAnyDecisionPoint) {
  // Q2: the flag is not observed, which is safe only while it is clear
  // whenever the agent acts. An invariant over random fights in every pool —
  // the assertion holds for every seed, so the sweep is coverage, not odds.
  long long checked = 0, saw_debuffed = 0;
  std::mt19937 pick(7);
  for (uint32_t seed = 0; seed < 150; ++seed) {
    for (EncounterPool pool : {EncounterPool::Weak, EncounterPool::Strong,
                               EncounterPool::Elite}) {
      CombatState s = start_combat(seed, pool, starter_deck());
      for (int step = 0; step < 80 && s.outcome == Outcome::InProgress;
           ++step) {
        for (int d = 0; d < kNumDebuffs; ++d) {
          ASSERT_FALSE(s.character.debuff_just_applied[d])
              << "seed " << seed << " step " << step << " debuff " << d;
        }
        ++checked;
        for (Debuff d : {Debuff::Vulnerable, Debuff::Weak, Debuff::Frail}) {
          if (get_status(s.character.debuffs, d) > 0) {
            ++saw_debuffed;
            break;
          }
        }
        const std::vector<bool> mask = valid_actions(s);
        std::vector<int> legal;
        for (std::size_t a = 0; a < mask.size(); ++a) {
          if (mask[a]) legal.push_back(static_cast<int>(a));
        }
        if (legal.empty()) break;
        apply_action(s, legal[pick() % legal.size()]);
      }
    }
  }
  EXPECT_GT(checked, 5000);
  // The sweep must reach the case it guards: the player carrying a debuff an
  // enemy applied (Lick, Skull Bash, Spore Cloud, Slaver Blue).
  EXPECT_GT(saw_debuffed, 200) << "enemy debuffs barely exercised";
}

}  // namespace
}  // namespace minispire
