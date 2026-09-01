// Holding relics and potions across a run (docs/design/v2-spec.md §3.0.1,
// §3.3, §4.2).

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "run_state.h"

namespace minispire {
namespace {

namespace {

// Find a held relic by id rather than by position. Every run starts holding
// Burning Blood, so relics[0] is the STARTER relic and not whatever the test
// just granted — indexing positionally reads the wrong relic and, because
// Burning Blood's counter is also 0, can do it silently.
const HeldRelic* find_relic(const RunState& run, RelicId id) {
  for (const HeldRelic& r : run.relics) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

}  // namespace

// ------------------------------------------------------------- the starter

// Every Ironclad run begins holding Burning Blood. The relic vocabulary's count
// depends on this: Black Blood is excluded from the 141 precisely because
// relicCanSpawn(BLACK_BLOOD) tests has(BURNING_BLOOD) and finds it true.
TEST(RelicState, ARunStartsHoldingBurningBlood) {
  RunState run = RunState::start(1);
  ASSERT_EQ(run.relics.size(), 1u) << "a run should start with exactly the starter relic";
  EXPECT_EQ(run.relics[0].id, RelicId::BurningBlood);
  EXPECT_EQ(run.relics[0].counter, 0);
}

// -------------------------------------------------------------- acquisition

TEST(RelicState, ObtainingAddsTheRelic) {
  RunState run = RunState::start(1);
  const size_t before = run.relics.size();
  EXPECT_FALSE(run.has_relic(RelicId::Vajra));
  EXPECT_TRUE(run.obtain_relic(RelicId::Vajra));
  EXPECT_TRUE(run.has_relic(RelicId::Vajra));
  EXPECT_EQ(run.relics.size(), before + 1);
}

TEST(RelicState, YouCannotHoldTwoOfTheSameRelic) {
  RunState run = RunState::start(1);
  const size_t before = run.relics.size();
  EXPECT_TRUE(run.obtain_relic(RelicId::Vajra));
  // The second attempt reports failure rather than silently doing nothing —
  // buy_relic depends on that signal to avoid charging for it.
  EXPECT_FALSE(run.obtain_relic(RelicId::Vajra));
  EXPECT_EQ(run.relics.size(), before + 1);
}

TEST(RelicState, RelicsStartWithAZeroCounter) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Nunchaku);
  const HeldRelic* nunchaku = find_relic(run, RelicId::Nunchaku);
  ASSERT_NE(nunchaku, nullptr);
  EXPECT_EQ(nunchaku->counter, 0);
}

// A random draw never offers something already held, so a run cannot be shown
// a relic it cannot take.
TEST(RelicState, RandomDrawsExcludeHeldRelics) {
  RunState run = RunState::start(1);
  std::mt19937 rng(4);
  // Hold every common but one.
  const std::vector<RelicId>& common = relic_pool(RelicTier::Common);
  for (size_t i = 1; i < common.size(); ++i) run.obtain_relic(common[i]);

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(run.random_relic(RelicTier::Common, rng), common[0]);
  }
}

// ------------------------------------------------------------------ potions

TEST(PotionState, ObtainingFillsABeltSlot) {
  RunState run = RunState::start(1);
  EXPECT_TRUE(run.obtain_potion(PotionId::FirePotion));
  EXPECT_EQ(run.potions.size(), 1u);
}

TEST(PotionState, TheBeltHasThreeSlotsByDefault) {
  RunState run = RunState::start(1);
  EXPECT_EQ(run.potion_slots, kBasePotionSlots);
  for (int i = 0; i < kBasePotionSlots; ++i) {
    EXPECT_TRUE(run.obtain_potion(PotionId::FirePotion));
  }
  EXPECT_FALSE(run.obtain_potion(PotionId::FirePotion))
      << "a fourth potion fit in a three-slot belt";
  EXPECT_EQ(run.potions.size(), static_cast<size_t>(kBasePotionSlots));
}

// Unlike relics, duplicate potions are fine — you can carry two Fire Potions.
TEST(PotionState, DuplicatePotionsAreAllowed) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.obtain_potion(PotionId::FirePotion);
  EXPECT_EQ(run.potions.size(), 2u);
}

TEST(PotionState, PotionBeltWidensTheBeltImmediately) {
  RunState run = RunState::start(1);
  for (int i = 0; i < kBasePotionSlots; ++i) run.obtain_potion(PotionId::FirePotion);
  ASSERT_FALSE(run.obtain_potion(PotionId::BlockPotion));

  run.obtain_relic(RelicId::PotionBelt);
  EXPECT_EQ(run.potion_slots, kBasePotionSlots + 2);
  EXPECT_TRUE(run.obtain_potion(PotionId::BlockPotion))
      << "Potion Belt did not open a slot";
}

// Sozu means no potions at all.
TEST(PotionState, SozuBlocksAllPotions) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Sozu);
  EXPECT_FALSE(run.obtain_potion(PotionId::FirePotion));
  EXPECT_TRUE(run.potions.empty());
}

TEST(PotionState, DiscardingFreesTheSlot) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.obtain_potion(PotionId::BlockPotion);
  run.discard_potion(0);

  ASSERT_EQ(run.potions.size(), 1u);
  EXPECT_EQ(run.potions[0], PotionId::BlockPotion);
}

// ------------------------------------------------- projection and write-back

// The fight is CONSTRUCTED with the run's state rather than built fresh and
// patched. Only the construction path is observable from here — the ordering it
// buys (a start-of-combat heal reading real HP, a relic changing the opening
// draw) cannot be asserted until effects exist, which is why the ordering slot
// is commented in start_combat rather than left implicit.
TEST(CombatSetup, TheFightIsBuiltAtTheRunsHpNotAFreshIroncladsHp) {
  CombatSetup setup;
  setup.seed = 1;
  setup.deck = starter_deck();
  setup.hp = 23;
  setup.max_hp = 71;
  setup.relics = {HeldRelic{RelicId::Vajra, 3}};
  setup.potions = {PotionId::FirePotion};

  const CombatState state = start_combat(setup);
  EXPECT_EQ(state.character.hp, 23);
  EXPECT_EQ(state.character.max_hp, 71);
  ASSERT_EQ(state.relics.size(), 1u);
  EXPECT_EQ(state.relics[0].counter, 3) << "the counter did not come along";
  EXPECT_EQ(state.potions.size(), 1u);
}

// v1.0.0's three-argument form still deals a fresh 80/80 Ironclad with nothing
// held, so a CombatEnv built the old way is unchanged (§3.0.1).
TEST(CombatSetup, TheV1FormIsUnchanged) {
  const CombatState state = start_combat(1, EncounterPool::Weak, starter_deck());
  EXPECT_EQ(state.character.hp, IRONCLAD_MAX_HP);
  EXPECT_EQ(state.character.max_hp, IRONCLAD_MAX_HP);
  EXPECT_TRUE(state.relics.empty());
  EXPECT_TRUE(state.potions.empty());
}

// §3.0.1: relics and potions are first-class combat state, so the fight can
// see and use them.
TEST(RelicState, CombatSeesTheRunsRelicsAndPotions) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Vajra);
  run.obtain_potion(PotionId::FirePotion);
  run.begin_combat(EncounterPool::Weak);

  EXPECT_TRUE(run.combat.has_relic(RelicId::Vajra));
  ASSERT_EQ(run.combat.potions.size(), 1u);
  EXPECT_EQ(run.combat.potions[0], PotionId::FirePotion);
}

// §3.3: every relic counter is run-scoped and carries out of the fight.
TEST(RelicState, RelicCountersCarryOutOfCombat) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Nunchaku);
  run.begin_combat(EncounterPool::Weak);

  // By id, not by index — relics[0] is Burning Blood, so bumping it positionally
  // would set the STARTER's counter and then assert on it, passing while testing
  // nothing about Nunchaku.
  for (HeldRelic& r : run.combat.relics) {
    if (r.id == RelicId::Nunchaku) r.counter = 7;
  }
  run.end_combat();

  const HeldRelic* nunchaku = find_relic(run, RelicId::Nunchaku);
  ASSERT_NE(nunchaku, nullptr);
  EXPECT_EQ(nunchaku->counter, 7) << "a relic counter reset at a fight boundary";
}

TEST(RelicState, CountersKeepAccumulatingAcrossFights) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::InkBottle);

  run.begin_combat(EncounterPool::Weak);
  run.combat.relics[0].counter = 5;
  run.end_combat();
  run.skip_card_reward();

  run.advance_to_next_floor(EncounterPool::Weak);
  ASSERT_EQ(run.combat.relics[0].counter, 5) << "the counter did not project in";
  run.combat.relics[0].counter = 9;
  run.end_combat();

  EXPECT_EQ(run.relics[0].counter, 9);
}

// A potion drunk during a fight is gone afterwards.
//
// The drop is suppressed deliberately: end_combat writes potions back and THEN
// rolls a post-fight drop, so a naive count assertion here reads 2 and looks
// like the write-back failed. It is the drop, and the two are easy to confuse.
TEST(PotionState, PotionsDrunkInCombatDoNotComeBack) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.obtain_potion(PotionId::BlockPotion);
  run.potion_chance_bonus = -40;  // 40 - 40 = 0%, so no drop can land
  run.begin_combat(EncounterPool::Weak);

  run.combat.potions.erase(run.combat.potions.begin());
  run.end_combat();

  ASSERT_EQ(run.potions.size(), 1u);
  EXPECT_EQ(run.potions[0], PotionId::BlockPotion);
}

// The other half of that interaction, asserted rather than left implicit: a
// fight can both consume a potion and drop a new one.
TEST(PotionState, AFightCanConsumeAPotionAndStillDropOne) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.potion_chance_bonus = 60;  // 40 + 60 = 100%, guaranteed
  run.begin_combat(EncounterPool::Weak);

  run.combat.potions.clear();  // drank it
  run.end_combat();

  EXPECT_EQ(run.potions.size(), 1u) << "the drop did not land after the drink";
}

// ------------------------------------------------------------ elite rewards

TEST(RelicState, ElitesGrantARelic) {
  RunState run = RunState::start(3);
  const size_t before = run.relics.size();
  run.begin_combat(EncounterPool::Elite);
  run.end_combat();
  EXPECT_EQ(run.relics.size(), before + 1) << "an elite paid no relic";
}

TEST(RelicState, NormalFightsGrantNoRelic) {
  RunState run = RunState::start(3);
  const size_t before = run.relics.size();
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  EXPECT_EQ(run.relics.size(), before);
}

// The reward screen is rolled as a unit and only then collected, so a relic won
// from THIS fight cannot change what else this fight offered.
//
// White Beast Statue is the sharp case: it forces the potion drop chance to 100.
// Granting the elite's relic before the potion roll let a fight that dropped the
// statue guarantee its own potion — an ordering bug with no local symptom.
TEST(RelicState, AnElitesOwnRelicCannotAlterThatFightsPotionRoll) {
  // Drive the drop chance to zero so any potion that appears can only have come
  // from a relic granted too early.
  for (uint64_t s = 0; s < 200; ++s) {
    RunState run = RunState::start(s);
    run.potion_chance_bonus = -40;
    run.begin_combat(EncounterPool::Elite);
    run.end_combat();
    if (run.has_relic(RelicId::WhiteBeastStatue)) {
      EXPECT_TRUE(run.potions.empty())
          << "seed " << s
          << ": the elite's own White Beast Statue forced its own potion drop";
    }
  }
}

// A suppressed reward screen still MISSES, and a miss drifts the chance up. The
// spec sets the chance to zero and rolls; returning early instead would skip the
// drift and quietly lower the drop rate of any run that hit full screens.
TEST(PotionState, AFullRewardScreenStillDriftsTheChanceUp) {
  RunState run = RunState::start(1);
  const int before = run.potion_chance_bonus;
  run.roll_potion_drop(/*rewards_already_on_screen=*/4);
  EXPECT_TRUE(run.potions.empty()) << "a full screen dropped a potion anyway";
  EXPECT_EQ(run.potion_chance_bonus, before + 10)
      << "a suppressed screen skipped the miss drift";
}

// White Beast Statue forces the chance to 100, but a full screen still wins.
TEST(PotionState, AFullScreenSuppressesEvenWhiteBeastStatue) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::WhiteBeastStatue);
  run.roll_potion_drop(/*rewards_already_on_screen=*/4);
  EXPECT_TRUE(run.potions.empty());
}

// --------------------------------------------------------------- the chest

// A treasure room is a pass-through: it opens and EXITS. Every generated map
// hard-sets row 8 to Treasure, so a room that set its phase without leaving
// would strand every run on floor 9 — and because leave_room is also where the
// win check lives, a chest on the final floor would never record a win.
TEST(Chest, ATreasureRoomOpensAndThenLeaves) {
  RunState run = RunState::start(1);
  run.floor = 5;
  const size_t before = run.relics.size();

  run.enter_room(RoomType::Treasure);

  EXPECT_EQ(run.relics.size(), before + 1) << "the chest paid nothing";
  EXPECT_NE(run.phase, Phase::Treasure)
      << "the run is stranded in the treasure phase with no way out";
  EXPECT_EQ(run.phase, Phase::Map);
}

// A chest rolls a SIZE first, and the size decides the tier odds. A large
// chest can never give a common and a small can never give a rare — which is a
// different distribution from an elite's, and conflating them is easy.
TEST(Chest, TierRespectsChestSizeBounds) {
  int commons = 0, rares = 0;
  for (uint64_t s = 0; s < 300; ++s) {
    RunState run = RunState::start(s);
    run.floor = 9;
    run.open_chest();
    ASSERT_EQ(run.relics.size(), 2u) << "starter relic + the chest's";
    const RelicTier t = relic_tier(run.relics.back().id);
    EXPECT_TRUE(t == RelicTier::Common || t == RelicTier::Uncommon ||
                t == RelicTier::Rare);
    if (t == RelicTier::Common) ++commons;
    if (t == RelicTier::Rare) ++rares;
  }
  // Both extremes must occur: a distribution stuck on one tier would pass a
  // weaker assertion.
  EXPECT_GT(commons, 0);
  EXPECT_GT(rares, 0);
}

TEST(Chest, SometimesPaysGoldAsWell) {
  int with_gold = 0;
  for (uint64_t s = 0; s < 200; ++s) {
    RunState run = RunState::start(s);
    run.floor = 9;
    run.open_chest();
    if (run.gold > 0) ++with_gold;
  }
  EXPECT_GT(with_gold, 0);
  EXPECT_LT(with_gold, 200) << "every chest paid gold — is the roll being used?";
}

TEST(Chest, IsDeterministicPerFloor) {
  auto opened = [](uint64_t seed, int floor) {
    RunState run = RunState::start(seed);
    run.floor = floor;
    run.open_chest();
    return std::pair<int, int>{static_cast<int>(run.relics[0].id), run.gold};
  };
  EXPECT_EQ(opened(5, 9), opened(5, 9));
  EXPECT_NE(opened(5, 9), opened(6, 9));
}

// ----------------------------------------------------------- potion drops

TEST(PotionDrops, SomeFightsDropPotionsAndSomeDoNot) {
  int drops = 0;
  for (uint64_t s = 0; s < 200; ++s) {
    RunState run = RunState::start(s);
    run.begin_combat(EncounterPool::Weak);
    run.end_combat();
    if (!run.potions.empty()) ++drops;
  }
  EXPECT_GT(drops, 0);
  EXPECT_LT(drops, 200);
}

// The drift is SYMMETRIC — it falls after a drop. Modelling it as a one-way
// pity counter would make potions far too common.
TEST(PotionDrops, TheChanceFallsAfterADropAndRisesAfterAMiss) {
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 0;

  // Force a miss: nothing can drop at 0% less the bonus.
  run.potion_chance_bonus = -40;
  const int before_miss = run.potion_chance_bonus;
  run.roll_potion_drop(0);
  EXPECT_GT(run.potion_chance_bonus, before_miss) << "a miss did not raise the chance";

  // Force a hit: guaranteed at 100%.
  RunState hit = RunState::start(1);
  hit.potion_chance_bonus = 60;  // 40 + 60 = 100
  hit.roll_potion_drop(0);
  ASSERT_EQ(hit.potions.size(), 1u);
  EXPECT_LT(hit.potion_chance_bonus, 60) << "a drop did not lower the chance";
}

// Potions are coupled to the other rewards: a screen already holding four
// suppresses the drop entirely.
TEST(PotionDrops, AFullRewardScreenSuppressesTheDrop) {
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 60;  // would otherwise be guaranteed
  run.roll_potion_drop(4);
  EXPECT_TRUE(run.potions.empty());
}

TEST(PotionDrops, WhiteBeastStatueGuaranteesADrop) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::WhiteBeastStatue);
  run.potion_chance_bonus = -40;  // would otherwise never drop
  run.roll_potion_drop(0);
  EXPECT_EQ(run.potions.size(), 1u);
}

}  // namespace
}  // namespace minispire
