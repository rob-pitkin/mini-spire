#include <gtest/gtest.h>

#include <set>
#include <string>

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
  EXPECT_TRUE(d.applies_debuffs.empty());
  EXPECT_TRUE(d.applies_powers.empty());
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
  EXPECT_TRUE(d.applies_debuffs.empty());
  EXPECT_TRUE(d.applies_powers.empty());
}

TEST(Card, DefendPlusBlock) {
  EXPECT_EQ(data_for(CardId::DefendPlus).block, 8);
}

TEST(Card, BashStatsAndVulnerable) {
  const CardData& d = data_for(CardId::Bash);
  EXPECT_EQ(d.cost, 2);
  EXPECT_EQ(d.damage, 8);
  EXPECT_EQ(d.block, 0);
  ASSERT_EQ(d.applies_debuffs.size(), 1u);
  EXPECT_EQ(d.applies_debuffs[0].effect, Debuff::Vulnerable);
  EXPECT_EQ(d.applies_debuffs[0].amount, 2);
  EXPECT_EQ(d.applies_debuffs[0].target, Target::Enemy);
}

TEST(Card, BashPlusDamageAndVulnerable) {
  const CardData& d = data_for(CardId::BashPlus);
  EXPECT_EQ(d.damage, 10);
  ASSERT_EQ(d.applies_debuffs.size(), 1u);
  EXPECT_EQ(d.applies_debuffs[0].effect, Debuff::Vulnerable);
  EXPECT_EQ(d.applies_debuffs[0].amount, 3);
  EXPECT_EQ(d.applies_debuffs[0].target, Target::Enemy);
}

// ============================================================================
// CARD_UPGRADES (Stage 4c step 1). These are INVARIANT tests, not spot checks:
// a hand-written 50-row table must be verified by rules that keep holding as
// the pool grows, not by re-asserting the rows it already has.
// ============================================================================

namespace {
bool name_is_upgraded(const CardData& d) {
  const std::string n = d.name;
  return !n.empty() && n.back() == '+';
}
}  // namespace

TEST(CardUpgrades, EveryBaseCardIsUpgradable) {
  // Every card that is neither already-upgraded nor a Status card must have an
  // upgrade. Catches a card added to CARD_DATABASE but forgotten here.
  for (const auto& [id, d] : CARD_DATABASE) {
    if (name_is_upgraded(d) || d.type == CardType::Status) continue;
    EXPECT_TRUE(is_upgradable(id)) << "no upgrade for base card: " << d.name;
  }
}

TEST(CardUpgrades, UpgradedAndStatusCardsAreNotUpgradable) {
  // StS: you cannot upgrade an already-upgraded card, nor a Status card.
  // Searing Blow is the documented exception — it upgrades without limit via
  // its instance counter, so even Searing Blow+ stays upgradable.
  for (const auto& [id, d] : CARD_DATABASE) {
    if (is_instance_upgradable(id)) continue;
    if (name_is_upgraded(d) || d.type == CardType::Status) {
      EXPECT_FALSE(is_upgradable(id)) << "should not be upgradable: " << d.name;
    }
  }
}

TEST(CardUpgrades, SearingBlowUpgradesWithoutLimitOnTheInstance) {
  // The one card whose upgrades live on the copy rather than in CARD_UPGRADES.
  EXPECT_TRUE(is_instance_upgradable(CardId::SearingBlow));
  EXPECT_TRUE(is_instance_upgradable(CardId::SearingBlowPlus));
  EXPECT_TRUE(is_upgradable(CardId::SearingBlowPlus));  // unlike other "+"
  EXPECT_EQ(CARD_UPGRADES.count(CardId::SearingBlow), 0u);

  Card c{CardId::SearingBlow};
  for (int i = 1; i <= 5; ++i) {
    ASSERT_TRUE(upgrade_card_in_place(c));
    EXPECT_EQ(c.card_id, CardId::SearingBlow);  // id never changes
    EXPECT_EQ(c.upgrades, i);                   // the counter does
  }
}

TEST(CardUpgrades, UpgradeInPlaceSwapsIdForNormalCards) {
  Card c{CardId::Strike};
  ASSERT_TRUE(upgrade_card_in_place(c));
  EXPECT_EQ(c.card_id, CardId::StrikePlus);
  EXPECT_EQ(c.upgrades, 0);  // normal cards don't use the counter
  // A "+" card cannot be upgraded again.
  EXPECT_FALSE(upgrade_card_in_place(c));
}

TEST(CardUpgrades, EveryMappingGoesToItsOwnPlusForm) {
  // Verifies X -> "X+" BY NAME, which is what catches a transcription swap
  // (e.g. Clash accidentally mapped to Clothesline+).
  for (const auto& [base, upgraded] : CARD_UPGRADES) {
    const std::string base_name = CARD_DATABASE.at(base).name;
    const std::string upg_name = CARD_DATABASE.at(upgraded).name;
    EXPECT_EQ(upg_name, base_name + "+")
        << base_name << " upgrades to the wrong card";
  }
}

TEST(CardUpgrades, NoTwoCardsShareAnUpgradeTarget) {
  std::set<CardId> targets;
  for (const auto& [base, upgraded] : CARD_UPGRADES) {
    EXPECT_TRUE(targets.insert(upgraded).second)
        << "duplicate upgrade target: " << CARD_DATABASE.at(upgraded).name;
  }
}

TEST(CardUpgrades, UpgradedCardIsTotalAndIdentityWhenNotUpgradable) {
  // upgraded_card never fails: callers upgrading a whole pile need no guard.
  for (const auto& [id, d] : CARD_DATABASE) {
    if (is_instance_upgradable(id)) continue;  // upgrades via the instance
    if (is_upgradable(id)) {
      EXPECT_EQ(upgraded_card(id), CARD_UPGRADES.at(id));
    } else {
      EXPECT_EQ(upgraded_card(id), id) << "not identity for " << d.name;
    }
  }
}

TEST(CardUpgrades, KnownPairsAreCorrect) {
  // A few spot checks across tiers, so the invariants above can't all pass
  // vacuously on an empty table.
  EXPECT_EQ(upgraded_card(CardId::Strike), CardId::StrikePlus);
  EXPECT_EQ(upgraded_card(CardId::Bash), CardId::BashPlus);
  EXPECT_EQ(upgraded_card(CardId::Whirlwind), CardId::WhirlwindPlus);
  EXPECT_EQ(upgraded_card(CardId::DemonForm), CardId::DemonFormPlus);
  EXPECT_EQ(upgraded_card(CardId::Corruption), CardId::CorruptionPlus);
  EXPECT_EQ(CARD_UPGRADES.size(), 71u);
  // Status cards: not upgradable (StS).
  EXPECT_FALSE(is_upgradable(CardId::Slimed));
  EXPECT_FALSE(is_upgradable(CardId::Dazed));
  EXPECT_EQ(upgraded_card(CardId::Slimed), CardId::Slimed);
}
