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

// ================================================== campfire relics (batch 2b)

namespace {

RunState at_a_campfire(std::vector<RelicId> ids, uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  run.floor = 6;
  run.phase = Phase::Rest;
  for (RelicId id : ids) run.obtain_relic(id);
  return run;
}

bool offers(const RunState& run, RestOption opt) {
  const std::vector<RestOption> o = run.rest_options();
  return std::find(o.begin(), o.end(), opt) != o.end();
}

}  // namespace

// --------------------------------------------------------- the two removals

TEST(CampfireRelics, CoffeeDripperRemovesRest) {
  EXPECT_TRUE(offers(at_a_campfire({}), RestOption::Rest));
  EXPECT_FALSE(
      offers(at_a_campfire({RelicId::CoffeeDripper}), RestOption::Rest));
}

// And the option being gone must actually stop the heal, not merely hide it —
// a caller that invokes rest_heal directly must be refused too.
TEST(CampfireRelics, CoffeeDripperAlsoRefusesTheHealItself) {
  RunState run = at_a_campfire({RelicId::CoffeeDripper});
  run.hp = 20;
  run.rest_heal();
  EXPECT_EQ(run.hp, 20) << "Coffee Dripper hid Rest but still healed";
}

TEST(CampfireRelics, FusionHammerRemovesSmith) {
  EXPECT_TRUE(offers(at_a_campfire({}), RestOption::Smith));
  EXPECT_FALSE(
      offers(at_a_campfire({RelicId::FusionHammer}), RestOption::Smith));
}

TEST(CampfireRelics, FusionHammerAlsoRefusesTheSmithItself) {
  RunState run = at_a_campfire({RelicId::FusionHammer});
  const std::vector<Card> before = run.master_deck;
  run.rest_smith(0);
  for (size_t i = 0; i < before.size(); ++i) {
    EXPECT_TRUE(before[i].same_as(run.master_deck[i]));
  }
}

// ------------------------------------------------------------ Regal Pillow

TEST(CampfireRelics, RegalPillowAddsAFlatFifteen) {
  RunState bare = at_a_campfire({});
  bare.hp = 10;
  bare.rest_heal();
  const int bare_healed = bare.hp - 10;

  RunState with = at_a_campfire({RelicId::RegalPillow});
  with.hp = 10;
  with.rest_heal();
  EXPECT_EQ(with.hp - 10, bare_healed + kRegalPillowHeal);
}

// The bonus is flat, not a change to the 30% fraction — so it does not scale
// with Max HP. Raising Max HP must move the percentage part and leave the 15.
TEST(CampfireRelics, RegalPillowDoesNotScaleWithMaxHp) {
  RunState with = at_a_campfire({RelicId::RegalPillow});
  with.max_hp = 200;
  with.hp = 10;
  with.rest_heal();
  EXPECT_EQ(with.hp - 10, 60 + kRegalPillowHeal) << "30% of 200, plus a flat 15";
}

TEST(CampfireRelics, RestingStillCannotExceedMaxHp) {
  RunState with = at_a_campfire({RelicId::RegalPillow});
  with.hp = with.max_hp - 1;
  with.rest_heal();
  EXPECT_EQ(with.hp, with.max_hp);
}

// ------------------------------------------------------------------- Girya

TEST(CampfireRelics, GiryaOffersLiftAndCapsAtThree) {
  RunState run = at_a_campfire({RelicId::Girya});
  for (int i = 0; i < kGiryaMaxUses; ++i) {
    EXPECT_TRUE(offers(run, RestOption::Lift)) << "lift " << i;
    run.rest_lift();
    run.phase = Phase::Rest;  // back to a campfire for the next one
  }
  EXPECT_EQ(run.girya_uses(), kGiryaMaxUses);
  EXPECT_FALSE(offers(run, RestOption::Lift)) << "Girya exceeded its 3 uses";
}

TEST(CampfireRelics, LiftIsNotOfferedWithoutGirya) {
  EXPECT_FALSE(offers(at_a_campfire({}), RestOption::Lift));
}

// The counter IS the Strength, and it is read at combat start — so a lifted
// Girya shows up as Strength in the very next fight, and keeps showing up.
TEST(CampfireRelics, GiryaStrengthAppliesAtCombatStart) {
  RunState run = at_a_campfire({RelicId::Girya});
  run.rest_lift();
  run.phase = Phase::Rest;
  run.rest_lift();

  run.begin_combat(EncounterPool::Weak);
  EXPECT_EQ(get_status(run.combat.character.powers, Power::Strength), 2);
}

// An unlifted Girya is worth nothing — it must not push a zero-stack power.
TEST(CampfireRelics, AnUnliftedGiryaGrantsNoStrength) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Girya);
  run.begin_combat(EncounterPool::Weak);
  EXPECT_EQ(get_status(run.combat.character.powers, Power::Strength), 0);
}

// It persists: the Strength is per-combat-start, not once per run.
TEST(CampfireRelics, GiryaStrengthAppliesToEveryFightNotJustTheNextOne) {
  RunState run = at_a_campfire({RelicId::Girya});
  run.rest_lift();

  run.begin_combat(EncounterPool::Weak);
  ASSERT_EQ(get_status(run.combat.character.powers, Power::Strength), 1);
  run.combat.character.hp = 50;
  run.end_combat();

  run.floor = 8;
  run.begin_combat(EncounterPool::Weak);
  EXPECT_EQ(get_status(run.combat.character.powers, Power::Strength), 1);
}

// -------------------------------------------------------- Peace Pipe, Shovel

TEST(CampfireRelics, PeacePipeOffersTokeAndRemovesACard) {
  RunState run = at_a_campfire({RelicId::PeacePipe});
  ASSERT_TRUE(offers(run, RestOption::Toke));
  const size_t before = run.master_deck.size();
  run.rest_toke(0);
  EXPECT_EQ(run.master_deck.size(), before - 1);
}

// The same two rules as the shop's removal. They are one helper precisely so
// that this path cannot drift from that one — it already had: both erased the
// deck directly and neither checked the card.
TEST(CampfireRelics, TokeCannotRemoveCurseOfTheBell) {
  RunState run = at_a_campfire({RelicId::PeacePipe});
  run.master_deck.push_back(Card{CardId::CurseOfTheBell});
  const size_t deck = run.master_deck.size();

  run.rest_toke(static_cast<int>(deck) - 1);

  EXPECT_EQ(run.master_deck.size(), deck) << "the Bell was smoked";
  EXPECT_EQ(run.phase, Phase::Rest)
      << "a refused Toke consumed the rest site anyway";
}

TEST(CampfireRelics, TokingParasiteCostsThreeMaxHp) {
  RunState run = at_a_campfire({RelicId::PeacePipe});
  run.master_deck.push_back(Card{CardId::Parasite});
  const int max_before = run.max_hp;
  run.hp = max_before - 20;
  const int hp_before = run.hp;

  run.rest_toke(static_cast<int>(run.master_deck.size()) - 1);

  EXPECT_EQ(run.max_hp, max_before - 3);
  EXPECT_EQ(run.hp, hp_before);
}

TEST(CampfireRelics, TokeIsNotOfferedWithoutPeacePipe) {
  EXPECT_FALSE(offers(at_a_campfire({}), RestOption::Toke));
}

TEST(CampfireRelics, ShovelOffersDigAndPaysARelic) {
  RunState run = at_a_campfire({RelicId::Shovel});
  ASSERT_TRUE(offers(run, RestOption::Dig));
  const size_t before = run.relics.size();
  run.rest_dig();
  EXPECT_EQ(run.relics.size(), before + 1) << "Dig paid nothing";
}

TEST(CampfireRelics, DigIsNotOfferedWithoutShovel) {
  EXPECT_FALSE(offers(at_a_campfire({}), RestOption::Dig));
}

// All five options can be on offer at once, and the enum sizes for it.
TEST(CampfireRelics, EveryOptionCanBeOfferedTogether) {
  RunState run = at_a_campfire(
      {RelicId::Girya, RelicId::PeacePipe, RelicId::Shovel});
  const std::vector<RestOption> o = run.rest_options();
  EXPECT_EQ(o.size(), static_cast<size_t>(kNumRestOptions));
}

// ------------------------------------------------------------ Dream Catcher

// Dream Catcher fires on REST specifically. The run stays for the reward rather
// than walking straight back to the map.
TEST(CampfireRelics, DreamCatcherGivesACardRewardOnRest) {
  RunState run = at_a_campfire({RelicId::DreamCatcher});
  run.hp = 20;
  run.rest_heal();
  EXPECT_FALSE(run.card_reward.empty()) << "Dream Catcher offered no card";
  EXPECT_GT(run.hp, 20) << "the rest itself should still have healed";
}

TEST(CampfireRelics, NoCardRewardWhenRestingWithoutDreamCatcher) {
  RunState run = at_a_campfire({});
  run.hp = 20;
  run.rest_heal();
  EXPECT_TRUE(run.card_reward.empty());
}

// Not on any other option — the wiki is explicit that it is Rest only.
TEST(CampfireRelics, DreamCatcherDoesNotFireOnSmith) {
  RunState run = at_a_campfire({RelicId::DreamCatcher});
  run.rest_smith(0);
  EXPECT_TRUE(run.card_reward.empty())
      << "Dream Catcher fired on a campfire option other than Rest";
}

}  // namespace
}  // namespace minispire
