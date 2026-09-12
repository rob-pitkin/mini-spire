// Relic trigger infrastructure (docs/design/relic-effects.md §3.1).
//
// Batch 0: the hook vocabulary, fire_relic_hooks, and the combat-start
// sub-phases, proved with the relics whose primitives already exist.

#include <gtest/gtest.h>

#include <vector>

#include "run_state.h"
#include "turn_loop.h"

namespace minispire {
namespace {

// A fight with exactly the relics named, at full HP, on the starter deck.
CombatState fight_with(std::vector<RelicId> ids, uint32_t seed = 1) {
  CombatSetup setup;
  setup.seed = seed;
  setup.deck = starter_deck();
  for (RelicId id : ids) setup.relics.push_back(HeldRelic{id, 0});
  return start_combat(setup);
}

// The same fight with no relics, as the control. Every assertion below is a
// DELTA against this rather than an absolute: a bare fight already has block,
// energy and a hand, and hardcoding those numbers would make these tests fail
// for reasons that have nothing to do with relics.
CombatState bare_fight(uint32_t seed = 1) { return fight_with({}, seed); }

// ------------------------------------------------------------ combat start

TEST(RelicTriggers, VajraGrantsStrengthAtCombatStart) {
  const CombatState with = fight_with({RelicId::Vajra});
  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 1);
}

TEST(RelicTriggers, OddlySmoothStoneGrantsDexterity) {
  const CombatState with = fight_with({RelicId::OddlySmoothStone});
  EXPECT_EQ(get_status(with.character.powers, Power::Dexterity), 1);
}

TEST(RelicTriggers, AnchorGrantsBlock) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::Anchor});
  EXPECT_EQ(with.character.current_block, bare.character.current_block + 10);
}

TEST(RelicTriggers, LanternGrantsEnergy) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::Lantern});
  EXPECT_EQ(with.character.energy, bare.character.energy + 1);
}

// Blood Vial heals, so it needs a fight that starts damaged to be visible.
TEST(RelicTriggers, BloodVialHealsAtCombatStart) {
  CombatSetup setup;
  setup.seed = 1;
  setup.deck = starter_deck();
  setup.hp = 50;
  setup.relics = {HeldRelic{RelicId::BloodVial, 0}};
  const CombatState with = start_combat(setup);
  EXPECT_EQ(with.character.hp, 52);
}

// A heal cannot exceed max HP, and combat start is exactly where that is easy
// to get wrong — the fight begins at full HP by default.
TEST(RelicTriggers, BloodVialCannotHealAboveMaxHp) {
  const CombatState with = fight_with({RelicId::BloodVial});
  EXPECT_EQ(with.character.hp, with.character.max_hp);
}

TEST(RelicTriggers, BagOfMarblesVulnerablesEveryEnemy) {
  const CombatState with = fight_with({RelicId::BagOfMarbles});
  ASSERT_FALSE(with.enemies.empty());
  for (const Enemy& e : with.enemies) {
    EXPECT_EQ(get_status(e.debuffs, Debuff::Vulnerable), 1)
        << "an enemy escaped Bag of Marbles";
  }
}

// ------------------------------------------------------ the sub-phase order

// The parity-critical one. Bag of Preparation resolves AFTER the opening hand,
// so it is a second draw of 2 rather than a 7-card opening draw. Firing it in
// the pre-draw sub-phase would still produce 7 cards -- and a different 7.
TEST(RelicTriggers, BagOfPreparationDrawsTwoMore) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::BagOfPreparation});
  EXPECT_EQ(with.current_hand.size(), bare.current_hand.size() + 2);
}

// The ordering proof: because the extra draw happens after the opening hand,
// the first cards drawn are the SAME as a bare fight's. A single 7-card draw
// would also produce 7 cards, but the opening 5 could differ.
TEST(RelicTriggers, BagOfPreparationDrawsAfterTheOpeningHandNotInsteadOfIt) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::BagOfPreparation});
  ASSERT_GE(with.current_hand.size(), bare.current_hand.size());

  for (size_t i = 0; i < bare.current_hand.size(); ++i) {
    EXPECT_TRUE(with.current_hand[i].same_as(bare.current_hand[i]))
        << "opening hand card " << i
        << " differs, so the extra draw resolved before the opening hand";
  }
}

// ------------------------------------------------------------- composition

// Relics fire in acquisition order, and several on one trigger must all land.
TEST(RelicTriggers, SeveralCombatStartRelicsAllFire) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with(
      {RelicId::Vajra, RelicId::Anchor, RelicId::Lantern,
       RelicId::OddlySmoothStone});

  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 1);
  EXPECT_EQ(get_status(with.character.powers, Power::Dexterity), 1);
  EXPECT_EQ(with.character.current_block, bare.character.current_block + 10);
  EXPECT_EQ(with.character.energy, bare.character.energy + 1);
}

// Holding a relic with no wired effect must be inert, not a crash or a stray
// mutation — most of the 140 are in exactly that state during batches 0-2.
TEST(RelicTriggers, AnUnwiredRelicChangesNothing) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::TheBoot});

  EXPECT_EQ(with.character.hp, bare.character.hp);
  EXPECT_EQ(with.character.current_block, bare.character.current_block);
  EXPECT_EQ(with.character.energy, bare.character.energy);
  EXPECT_EQ(with.current_hand.size(), bare.current_hand.size());
}

// The queue must be empty at every agent decision point (§4.2). Combat start
// drains, so a fight handed to an agent has nothing pending.
TEST(RelicTriggers, CombatStartLeavesNoPendingChoice) {
  const CombatState with = fight_with({RelicId::Vajra, RelicId::BagOfMarbles});
  EXPECT_FALSE(with.pending_choice.active());
  EXPECT_EQ(with.outcome, Outcome::InProgress);
}

// A relic held in the run reaches the fight through RunState, not just through
// a hand-built CombatSetup.
TEST(RelicTriggers, RelicsHeldInTheRunFireInTheRunsFights) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Vajra);
  run.begin_combat(EncounterPool::Weak);
  EXPECT_EQ(get_status(run.combat.character.powers, Power::Strength), 1);
}

}  // namespace
}  // namespace minispire
