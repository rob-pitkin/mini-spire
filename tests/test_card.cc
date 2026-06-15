#include <gtest/gtest.h>

#include "card.h"
#include "status_effect.h"

using namespace minispire;

namespace {

const CardData& data_for(CardId id) {
  auto it = CARD_DATABASE.find(id);
  EXPECT_NE(it, CARD_DATABASE.end()) << "CardId missing from CARD_DATABASE";
  return it->second;
}

}  // namespace

TEST(Card, DatabaseHasEntryForEveryCardId) {
  for (CardId id : {CardId::Strike, CardId::StrikePlus, CardId::Defend,
                    CardId::DefendPlus, CardId::Bash, CardId::BashPlus}) {
    EXPECT_NE(CARD_DATABASE.find(id), CARD_DATABASE.end());
  }
}

TEST(Card, StrikeStats) {
  const CardData& d = data_for(CardId::Strike);
  EXPECT_EQ(d.cost, 1);
  EXPECT_EQ(d.damage, 6);
  EXPECT_EQ(d.block, 0);
  EXPECT_TRUE(d.applies.empty());
  EXPECT_FALSE(d.exhaust);
}

TEST(Card, StrikePlusDamage) {
  EXPECT_EQ(data_for(CardId::StrikePlus).damage, 9);
}

TEST(Card, DefendStats) {
  const CardData& d = data_for(CardId::Defend);
  EXPECT_EQ(d.cost, 1);
  EXPECT_EQ(d.damage, 0);
  EXPECT_EQ(d.block, 5);
  EXPECT_TRUE(d.applies.empty());
}

TEST(Card, DefendPlusBlock) {
  EXPECT_EQ(data_for(CardId::DefendPlus).block, 8);
}

TEST(Card, BashStatsAndVulnerable) {
  const CardData& d = data_for(CardId::Bash);
  EXPECT_EQ(d.cost, 2);
  EXPECT_EQ(d.damage, 8);
  EXPECT_EQ(d.block, 0);
  ASSERT_EQ(d.applies.size(), 1u);
  EXPECT_EQ(d.applies[0].effect, StatusEffect::Vulnerable);
  EXPECT_EQ(d.applies[0].amount, 2);
  EXPECT_EQ(d.applies[0].target, StatusApplication::Target::Enemy);
}

TEST(Card, BashPlusDamageAndVulnerable) {
  const CardData& d = data_for(CardId::BashPlus);
  EXPECT_EQ(d.damage, 10);
  ASSERT_EQ(d.applies.size(), 1u);
  EXPECT_EQ(d.applies[0].effect, StatusEffect::Vulnerable);
  EXPECT_EQ(d.applies[0].amount, 3);
  EXPECT_EQ(d.applies[0].target, StatusApplication::Target::Enemy);
}
