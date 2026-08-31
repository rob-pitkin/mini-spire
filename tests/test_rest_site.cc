// Campfires (docs/design/v2-spec.md §8).
//
// The first real resource-vs-investment tradeoff, and the case run-reward.md
// was designed around: healing spends a floor to keep HP, smithing spends the
// same floor on a permanently better deck.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "run_state.h"

namespace minispire {
namespace {

// Puts a run at a campfire without walking there.
RunState at_a_campfire(uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  run.floor = 6;
  run.phase = Phase::Rest;
  return run;
}

// ------------------------------------------------------------------- options

// Act 1 at Ascension 0 offers exactly rest and smith. Lift, Toke and Dig need
// relics that do not exist yet; Recall is Act 4 and has no slot at all.
TEST(RestSite, OffersRestAndSmith) {
  RunState run = at_a_campfire();
  const std::vector<RestOption> options = run.rest_options();

  ASSERT_EQ(options.size(), 2u);
  EXPECT_EQ(options[0], RestOption::Rest);
  EXPECT_EQ(options[1], RestOption::Smith);
}

TEST(RestSite, OffersNothingOutsideACampfire) {
  RunState run = RunState::start(1);
  EXPECT_TRUE(run.rest_options().empty());
}

// The enum is part of the action space's interface, and Recall is deliberately
// absent — it is Act 4 content and would be a dead index.
TEST(RestSite, OptionNumberingHasNoDeadSlots) {
  EXPECT_EQ(static_cast<int>(RestOption::Rest), 0);
  EXPECT_EQ(static_cast<int>(RestOption::Smith), 1);
  EXPECT_EQ(static_cast<int>(RestOption::Lift), 2);
  EXPECT_EQ(static_cast<int>(RestOption::Toke), 3);
  EXPECT_EQ(static_cast<int>(RestOption::Dig), 4);
  EXPECT_EQ(static_cast<int>(RestOption::Dig) + 1, kNumRestOptions);
}

// ---------------------------------------------------------------------- rest

TEST(RestSite, RestHealsThirtyPercentOfMaxHp) {
  RunState run = at_a_campfire();
  run.max_hp = 80;
  run.hp = 20;
  run.rest_heal();

  EXPECT_EQ(run.hp, 20 + 24);  // 30% of 80
}

TEST(RestSite, RestNeverExceedsMaxHp) {
  RunState run = at_a_campfire();
  run.max_hp = 80;
  run.hp = 75;
  run.rest_heal();

  EXPECT_EQ(run.hp, 80);
}

// Truncated, not rounded — the distinction only shows at a max HP that is not
// a multiple of 10.
TEST(RestSite, TheHealIsTruncated) {
  RunState run = at_a_campfire();
  run.max_hp = 85;  // 30% = 25.5
  run.hp = 1;
  run.rest_heal();

  EXPECT_EQ(run.hp, 1 + 25);
}

TEST(RestSite, RestingLeavesTheCampfire) {
  RunState run = at_a_campfire();
  run.rest_heal();
  EXPECT_EQ(run.phase, Phase::Map);
}

// --------------------------------------------------------------------- smith

TEST(RestSite, SmithUpgradesTheChosenCard) {
  RunState run = at_a_campfire();
  const std::vector<int> targets = run.smithable_cards();
  ASSERT_FALSE(targets.empty());

  const int index = targets[0];
  const CardId before = run.master_deck[index].card_id;
  run.rest_smith(index);

  EXPECT_NE(run.master_deck[index].card_id, before);
  EXPECT_EQ(run.phase, Phase::Map);
}

// A campfire smith is PERMANENT. It mutates the master deck directly and never
// passes through a fight — which is exactly why a mid-combat Armaments upgrade
// and this one never need telling apart at the handoff (§3.2).
TEST(RestSite, SmithingPersistsIntoLaterFights) {
  RunState run = at_a_campfire();
  const int index = run.smithable_cards()[0];
  run.rest_smith(index);
  const CardId upgraded = run.master_deck[index].card_id;

  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 40;
  run.end_combat();

  EXPECT_EQ(run.master_deck[index].card_id, upgraded)
      << "a campfire upgrade was lost across a fight";
}

// Smithing does not change which card instance it is.
TEST(RestSite, SmithingPreservesCardIdentity) {
  RunState run = at_a_campfire();
  const int index = run.smithable_cards()[0];
  const int uid = run.master_deck[index].uid;
  run.rest_smith(index);

  EXPECT_EQ(run.master_deck[index].uid, uid);
}

TEST(RestSite, SmithIgnoresAnOutOfRangeIndex) {
  RunState run = at_a_campfire();
  const size_t size = run.master_deck.size();

  run.rest_smith(-1);
  run.rest_smith(999);
  EXPECT_EQ(run.master_deck.size(), size);
  EXPECT_EQ(run.phase, Phase::Rest) << "an ignored smith still left the campfire";
}

// Smith drops out of the option list when the whole deck is upgraded — the one
// condition that can remove it in Act 1 without relics.
TEST(RestSite, SmithDisappearsWhenNothingCanBeUpgraded) {
  RunState run = at_a_campfire();
  // Upgrade everything upgradable, repeatedly, until nothing is left.
  for (int pass = 0; pass < 8; ++pass) {
    const std::vector<int> targets = run.smithable_cards();
    if (targets.empty()) break;
    for (int i : targets) upgrade_card_in_place(run.master_deck[i]);
  }

  // Searing Blow can be upgraded forever, so a starter deck WILL run out.
  ASSERT_TRUE(run.smithable_cards().empty())
      << "the starter deck should be fully upgradable";
  const std::vector<RestOption> options = run.rest_options();
  ASSERT_EQ(options.size(), 1u);
  EXPECT_EQ(options[0], RestOption::Rest);
}

// ------------------------------------------------------------- the tradeoff

// The decision the reward design exists to make interesting: the same floor
// buys either HP or a better deck, never both.
TEST(RestSite, RestAndSmithAreMutuallyExclusiveOnAFloor) {
  RunState run = at_a_campfire();
  run.hp = 30;
  const int index = run.smithable_cards()[0];
  const CardId before = run.master_deck[index].card_id;

  run.rest_heal();

  EXPECT_GT(run.hp, 30);
  EXPECT_EQ(run.master_deck[index].card_id, before)
      << "resting also upgraded a card";
  // The campfire is spent: a second choice does nothing.
  run.rest_smith(index);
  EXPECT_EQ(run.master_deck[index].card_id, before);
}

}  // namespace
}  // namespace minispire
