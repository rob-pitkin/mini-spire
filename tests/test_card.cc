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
  // Any '+' in the name, not just a trailing one (ROB-87). The rung ladders
  // append the card's total damage ("Rampage+ 16"), so a suffix-only test would
  // read every upgraded Rampage rung as a base card missing an upgrade. Base
  // cards never contain '+'; upgraded ones always do.
  return std::string(d.name).find('+') != std::string::npos;
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
  // Searing Blow is the documented exception ("can be upgraded any number of
  // times"), and since ROB-87 that exception is a LADDER of ids rather than an
  // instance counter — so every one of its rungs stays upgradable by design.
  for (const auto& [id, d] : CARD_DATABASE) {
    if (searing_blow_rung(id) > 0) continue;  // a rung, not a dead end
    if (name_is_upgraded(d) || d.type == CardType::Status) {
      EXPECT_FALSE(is_upgradable(id)) << "should not be upgradable: " << d.name;
    }
  }
}

TEST(CardUpgrades, SearingBlowClimbsItsLadderThenSaturates) {
  // ROB-87 replaced the instance counter with real ids: each upgrade is an
  // ordinary CARD_UPGRADES swap, exactly like every other card.
  Card c{CardId::SearingBlow};
  const CardId want[] = {CardId::SearingBlowPlus, CardId::SearingBlow2,
                         CardId::SearingBlow3, CardId::SearingBlow4,
                         CardId::SearingBlow5};
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(upgrade_card_in_place(c));
    EXPECT_EQ(c.card_id, want[i]) << "rung " << (i + 1);
    EXPECT_EQ(c.upgrades, 0) << "below the cap the counter stays unused";
  }
}

TEST(CardUpgrades, SearingBlowStaysUpgradableAtTheCap) {
  // The parity clause. "Can be upgraded any number of times" must hold past the
  // top rung, so the cap keeps accepting upgrades and counts them on the
  // instance. If this regressed, Armaments would silently stop offering the
  // card and nothing else in the suite would notice.
  EXPECT_TRUE(is_upgradable(CardId::SearingBlow5));
  EXPECT_TRUE(is_instance_upgradable(CardId::SearingBlow5));

  Card c{CardId::SearingBlow5};
  for (int i = 1; i <= 3; ++i) {
    ASSERT_TRUE(upgrade_card_in_place(c)) << "refused upgrade past the cap";
    EXPECT_EQ(c.card_id, CardId::SearingBlow5) << "id saturates";
    EXPECT_EQ(c.upgrades, i) << "the overflow counter carries it";
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
  //
  // The ROB-87 rung ladders are exempt: their edges climb within a ladder
  // (Searing Blow+2 -> Searing Blow+3) or cross to the upgraded ladder at the
  // same accumulated bonus (Rampage 13 -> Rampage+ 13), neither of which is a
  // name-suffix relation. RungLaddersMatchTheReachableSet checks those instead,
  // structurally rather than textually.
  for (const auto& [base, upgraded] : CARD_UPGRADES) {
    if (searing_blow_rung(base) > 0 || base == CardId::SearingBlow) continue;
    if (CARD_DATABASE.at(base).bonus_damage_per_play > 0) continue;  // Rampage
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

TEST(CardUpgrades, RungLaddersMatchTheReachableSet) {
  // The database's rung rows were GENERATED; this recomputes the reachable set
  // from the rules and demands an exact match. It is the differential trick the
  // mask oracle uses — an independent derivation, not a restatement of the
  // table — and it is what makes 35 generated rows trustworthy without anyone
  // eyeballing them.
  //
  // Rampage+ is the reason it exists: {5a + 8b} is irregular, and +22, +27 and
  // +35 are NOT reachable. A hand-written list is as likely to wrongly INCLUDE
  // a gap as to mistype a value, and both fail silently in a positional row.
  const int kBaseCap = 30, kPlusCap = 40;
  std::set<int> want_plus;
  for (int a = 0; a * 5 <= kBaseCap; ++a) {
    for (int b = 0; 5 * a + 8 * b <= kPlusCap; ++b) want_plus.insert(5 * a + 8 * b);
  }

  // Collect what the database actually ships, by damage (rows carry 8 + bonus).
  std::set<int> got_base, got_plus;
  for (const auto& [id, d] : CARD_DATABASE) {
    if (d.bonus_damage_per_play == 5) got_base.insert(d.damage - 8);
    if (d.bonus_damage_per_play == 8) got_plus.insert(d.damage - 8);
  }

  std::set<int> want_base;
  for (int b = 0; b <= kBaseCap; b += 5) want_base.insert(b);

  EXPECT_EQ(got_base, want_base) << "Rampage ladder does not match {5a}";
  EXPECT_EQ(got_plus, want_plus) << "Rampage+ ladder does not match {5a + 8b}";
  for (int gap : {22, 27, 35}) {
    EXPECT_EQ(got_plus.count(gap), 0u)
        << "+" << gap << " is unreachable and must not have a rung";
  }

  // Every base rung must be able to upgrade INTO the plus ladder at the same
  // accumulated bonus, or a grown Rampage would have nowhere to go.
  for (int b : want_base) EXPECT_EQ(want_plus.count(b), 1u) << "no +" << b;

  // Searing Blow: rungs 0..5 on the published progression.
  const int want_sb[] = {12, 16, 21, 27, 34, 42};
  for (int n = 0; n <= 5; ++n) {
    Card c{static_cast<CardId>(0)};
    c.card_id = n == 0   ? CardId::SearingBlow
                : n == 1 ? CardId::SearingBlowPlus
                : n == 2 ? CardId::SearingBlow2
                : n == 3 ? CardId::SearingBlow3
                : n == 4 ? CardId::SearingBlow4
                         : CardId::SearingBlow5;
    EXPECT_EQ(searing_blow_rung(c.card_id), n);
    EXPECT_EQ(n * (n + 7) / 2 + 12, want_sb[n]);
  }
}

TEST(CardUpgrades, GrowthLadderIsAcyclicAndTerminates) {
  // Every growth chain must reach a cap. A mistyped edge could loop
  // (+16 -> +16) and hang a fight; walking each chain proves it cannot.
  for (const auto& [start, _] : CARD_GROWTH) {
    Card c{start};
    std::set<CardId> seen{start};
    int steps = 0;
    while (grow_card_in_place(c)) {
      ASSERT_TRUE(seen.insert(c.card_id).second)
          << "growth cycle through " << CARD_DATABASE.at(c.card_id).name;
      ASSERT_LT(++steps, 64) << "growth chain does not terminate";
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
  // 74 base pairs + 11 ROB-87 ladder edges (5 Searing Blow rungs, 6 grown
  // Rampage rungs crossing to their Rampage+ counterparts).
  EXPECT_EQ(CARD_UPGRADES.size(), 85u);
  // Status cards: not upgradable (StS).
  EXPECT_FALSE(is_upgradable(CardId::Slimed));
  EXPECT_FALSE(is_upgradable(CardId::Dazed));
  EXPECT_EQ(upgraded_card(CardId::Slimed), CardId::Slimed);
}

// --- Card descriptions (ROB-97) -------------------------------------------

TEST(Card, CardDescriptionsCoverEveryCard) {
  // card_description() returns "" for an unknown id so callers can render
  // without branching. That is only safe if the empty case never happens in
  // practice — which is this test's whole job. Without it a missing row is a
  // blank panel someone eventually notices mid-fight.
  //
  // 154 rows are authored; the rest are rungs generated from their base, so
  // this also covers the generator silently skipping a ladder step.
  for (const auto& [id, data] : CARD_DATABASE) {
    EXPECT_FALSE(card_description(id).empty())
        << "no description for " << card_name(id)
        << " — authored rows live in card_descriptions.inc, rung rows are "
        << "generated by build_card_descriptions()";
  }
}

TEST(Card, RungDescriptionsCarryTheirOwnDamage) {
  // The point of the generated rows: a grown Rampage must READ as the damage it
  // deals. Before ROB-87 these were one card type, so "Rampage" in the pile view
  // told you nothing about which copy you had.
  EXPECT_EQ(card_description(CardId::Rampage),
            "Deal 8 damage. Increase this card's damage by 5 this combat.");
  EXPECT_EQ(card_description(CardId::Rampage15),
            "Deal 23 damage. Increase this card's damage by 5 this combat.");
  EXPECT_EQ(card_description(CardId::RampagePlus40),
            "Deal 48 damage. Increase this card's damage by 8 this combat.");

  // Searing Blow's ladder is upgrades rather than growth, so it takes the other
  // branch of the generator. Damage follows query.cc's n(n+7)/2 + 12.
  EXPECT_EQ(card_description(CardId::SearingBlow),
            "Deal 12 damage. Can be Upgraded any number of times.");
  EXPECT_EQ(card_description(CardId::SearingBlow3),
            "Deal 27 damage. Can be Upgraded any number of times.");
  EXPECT_EQ(card_description(CardId::SearingBlow5),
            "Deal 42 damage. Can be Upgraded any number of times.");
}

TEST(Card, DescriptionDamageMatchesCardData) {
  // The wiki text came from a page summariser that got several COSTS wrong, so
  // every number in it was cross-checked against CARD_DATABASE before landing.
  // Keeping that check here makes it permanent: a future card edit that changes
  // damage without changing the text now fails a test instead of quietly
  // telling the player the wrong number.
  //
  // Only cards whose damage is a plain stored figure are checked. damage == 0
  // means "computed at play time" (Body Slam reads the block total, Searing Blow
  // uses DamageRule), and those descriptions correctly state no figure or a
  // ladder-derived one.
  for (const auto& [id, data] : CARD_DATABASE) {
    if (data.damage <= 0) continue;
    const std::string& text = card_description(id);
    const std::size_t at = text.find("Deal ");
    if (at == std::string::npos) continue;  // "Gain 5 Block. Deal ..." handled below
    std::size_t i = at + 5;
    std::string digits;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      digits += text[i++];
    }
    if (digits.empty()) continue;  // "Deal damage equal to your Block."
    EXPECT_EQ(std::stoi(digits), data.damage)
        << card_name(id) << ": description says " << digits
        << " damage, CARD_DATABASE says " << data.damage;
  }
}

TEST(Card, DescriptionBlockMatchesCardData) {
  // Same contract for Block. Sentinel+ (8 Block, 3 energy on exhaust) and
  // Power Through+ (20 Block) are the ones where the upgrade changes a number
  // the text states, which is exactly where a stale string would hide.
  for (const auto& [id, data] : CARD_DATABASE) {
    if (data.block <= 0) continue;
    const std::string& text = card_description(id);
    const std::size_t at = text.find("Gain ");
    if (at == std::string::npos) continue;
    std::size_t i = at + 5;
    std::string digits;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      digits += text[i++];
    }
    if (digits.empty() || text.compare(i, 6, " Block") != 0) continue;
    EXPECT_EQ(std::stoi(digits), data.block)
        << card_name(id) << ": description says " << digits
        << " Block, CARD_DATABASE says " << data.block;
  }
}
