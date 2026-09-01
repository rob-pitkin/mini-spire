// The relic and potion vocabularies (docs/design/v2-spec.md §5.1).
//
// These assert the COUNTS and their derivation, because the vocabulary is an
// interface: it sizes observation blocks and action indices, and a silent
// change to it invalidates every published number.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "potion.h"
#include "relic.h"

namespace minispire {
namespace {

// ------------------------------------------------------------------- relics

TEST(Relics, VocabularyIsOneHundredForty) {
  EXPECT_EQ(kNumRelics, 140);
  EXPECT_EQ(static_cast<int>(RelicId::CultistHeadpiece), kNumRelics - 1);
}

// The 140 is a sum with a derivation, not a magic number. If a pool changes,
// this says which one.
TEST(Relics, PoolSizesAddUpToTheVocabulary) {
  EXPECT_EQ(relic_pool(RelicTier::Starter).size(), 1u);
  EXPECT_EQ(relic_pool(RelicTier::Common).size(), 33u);
  EXPECT_EQ(relic_pool(RelicTier::Uncommon).size(), 30u);
  EXPECT_EQ(relic_pool(RelicTier::Rare).size(), 28u);
  EXPECT_EQ(relic_pool(RelicTier::Shop).size(), 17u);
  EXPECT_EQ(relic_pool(RelicTier::Boss).size(), 21u);
  EXPECT_EQ(relic_pool(RelicTier::Special).size(), 10u);

  size_t total = 0;
  for (RelicTier t : {RelicTier::Starter, RelicTier::Common, RelicTier::Uncommon,
                      RelicTier::Rare, RelicTier::Shop, RelicTier::Boss,
                      RelicTier::Special}) {
    total += relic_pool(t).size();
  }
  EXPECT_EQ(total, static_cast<size_t>(kNumRelics));
}

// Every relic appears in exactly one pool — the pools are derived from the tier
// table, so this checks the derivation rather than a hand-kept list.
TEST(Relics, EveryRelicIsInExactlyOnePool) {
  std::set<RelicId> seen;
  for (RelicTier t : {RelicTier::Starter, RelicTier::Common, RelicTier::Uncommon,
                      RelicTier::Rare, RelicTier::Shop, RelicTier::Boss,
                      RelicTier::Special}) {
    for (RelicId id : relic_pool(t)) {
      EXPECT_EQ(seen.count(id), 0u) << relic_name(id) << " is in two pools";
      seen.insert(id);
      EXPECT_EQ(relic_tier(id), t);
    }
  }
  EXPECT_EQ(seen.size(), static_cast<size_t>(kNumRelics));
}

TEST(Relics, TheIroncladStartsWithBurningBlood) {
  const std::vector<RelicId>& starter = relic_pool(RelicTier::Starter);
  ASSERT_EQ(starter.size(), 1u);
  EXPECT_EQ(starter[0], RelicId::BurningBlood);
}

TEST(Relics, NamesAreUniqueAndNonEmpty) {
  std::set<std::string> names;
  for (int i = 0; i < kNumRelics; ++i) {
    const char* n = relic_name(static_cast<RelicId>(i));
    ASSERT_NE(n, nullptr);
    EXPECT_GT(std::string(n).size(), 0u);
    EXPECT_EQ(names.count(n), 0u) << "duplicate relic name: " << n;
    names.insert(n);
  }
}

// The special relics are the ones granted by named Act 1 events. They are NOT
// in any random pool — a roll can never produce them.
TEST(Relics, SpecialRelicsAreTheActOneEventGrants) {
  const std::vector<RelicId>& special = relic_pool(RelicTier::Special);
  const std::set<RelicId> expected = {
      RelicId::NeowsLament,   RelicId::GoldenIdol,       RelicId::OddMushroom,
      RelicId::WarpedTongs,   RelicId::SpiritPoop,       RelicId::FaceOfCleric,
      RelicId::SsserpentHead, RelicId::GremlinVisage,    RelicId::NlothsHungryFace,
      RelicId::CultistHeadpiece};
  ASSERT_EQ(special.size(), expected.size());
  for (RelicId id : special) EXPECT_EQ(expected.count(id), 1u);
}

// Documents the exclusions, which are the part most likely to be "helpfully"
// undone. There is no enumerator for any of these, so the test is that the
// vocabulary stayed at 140 with the pools above — a re-added relic breaks
// PoolSizesAddUpToTheVocabulary and lands here for the explanation.
TEST(Relics, ExcludedRelicsHaveNoEnumerator) {
  // Black Blood: reachable only if Neow granted the boss relic BEFORE removing
  // Burning Blood. It applies the drawback first, so it cannot occur — and the
  // boss pool is 21 rather than 22 because of it.
  EXPECT_EQ(relic_pool(RelicTier::Boss).size(), 21u);

  // Circlet / Red Circlet need every other relic collected; the Act 2-3 event
  // relics need their events. None are Act 1 content, and none are here.
  EXPECT_EQ(kNumRelics, 140);
}

// ------------------------------------------------------------------ potions

TEST(Potions, VocabularyIsThirtyThree) {
  EXPECT_EQ(kNumPotions, 33);
  EXPECT_EQ(static_cast<int>(PotionId::EntropicBrew), kNumPotions - 1);
}

TEST(Potions, RarityPoolsAddUpToTheVocabulary) {
  EXPECT_EQ(potion_pool(PotionRarity::Common).size(), 17u);
  EXPECT_EQ(potion_pool(PotionRarity::Uncommon).size(), 9u);
  EXPECT_EQ(potion_pool(PotionRarity::Rare).size(), 7u);

  const size_t total = potion_pool(PotionRarity::Common).size() +
                       potion_pool(PotionRarity::Uncommon).size() +
                       potion_pool(PotionRarity::Rare).size();
  EXPECT_EQ(total, static_cast<size_t>(kNumPotions));
}

TEST(Potions, EveryPotionIsInExactlyOnePool) {
  std::set<PotionId> seen;
  for (PotionRarity r : {PotionRarity::Common, PotionRarity::Uncommon,
                         PotionRarity::Rare}) {
    for (PotionId id : potion_pool(r)) {
      EXPECT_EQ(seen.count(id), 0u) << potion_name(id) << " is in two pools";
      seen.insert(id);
      EXPECT_EQ(potion_rarity(id), r);
    }
  }
  EXPECT_EQ(seen.size(), static_cast<size_t>(kNumPotions));
}

TEST(Potions, NamesAreUniqueAndNonEmpty) {
  std::set<std::string> names;
  for (int i = 0; i < kNumPotions; ++i) {
    const char* n = potion_name(static_cast<PotionId>(i));
    ASSERT_NE(n, nullptr);
    EXPECT_GT(std::string(n).size(), 0u);
    EXPECT_EQ(names.count(n), 0u) << "duplicate potion name: " << n;
    names.insert(n);
  }
}

// Only the single-target potions ask for a target. Asking the others for one
// would be a dead action index.
TEST(Potions, OnlySingleTargetPotionsNeedATarget) {
  EXPECT_TRUE(potion_targets_enemy(PotionId::FirePotion));
  EXPECT_TRUE(potion_targets_enemy(PotionId::WeakPotion));
  EXPECT_TRUE(potion_targets_enemy(PotionId::FearPotion));

  EXPECT_FALSE(potion_targets_enemy(PotionId::BlockPotion));
  EXPECT_FALSE(potion_targets_enemy(PotionId::StrengthPotion));
  // Explosive Potion hits EVERY enemy, so it needs no target.
  EXPECT_FALSE(potion_targets_enemy(PotionId::ExplosivePotion));
}

TEST(Potions, PricesRiseWithRarity) {
  EXPECT_LT(kPotionRarityPrices[static_cast<int>(PotionRarity::Common)],
            kPotionRarityPrices[static_cast<int>(PotionRarity::Uncommon)]);
  EXPECT_LT(kPotionRarityPrices[static_cast<int>(PotionRarity::Uncommon)],
            kPotionRarityPrices[static_cast<int>(PotionRarity::Rare)]);
}

}  // namespace
}  // namespace minispire
