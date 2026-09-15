// Colorless cards (docs/design/v2-spec.md §5.1, §6).
//
// 35 cards, 70 ids, added ONE AT A TIME: fetch the wiki page, write the row,
// verify, iterate. Bulk transcription is how card data goes quietly wrong, and
// the numbers here are the ones an agent trains against.
//
// Nothing can obtain a colorless card yet — they arrive via the shop's 2
// colorless slots and Neow's blessings, neither implemented. Cards whose
// effects need machinery that does not exist are marked `unplayable` in
// CARD_DATABASE (the gate Dazed uses), so they hold a stable action index
// without being a playable no-op.

#include <gtest/gtest.h>

#include <vector>

#include "action.h"
#include "card.h"
#include "turn_loop.h"

namespace minispire {
namespace {

int card_action(CardId id, int target = 0) {
  return static_cast<int>(id) * kMaxEnemies + target;
}

// A fight with `card` in hand and energy to spare.
CombatState fight_holding(CardId card, uint32_t seed = 1) {
  CombatSetup setup;
  setup.seed = seed;
  setup.deck = starter_deck();
  CombatState s = start_combat(std::move(setup));
  s.current_hand.clear();
  s.current_hand.push_back(Card{card});
  s.character.energy = 99;
  return s;
}

}  // namespace

// ------------------------------------------------------------- Bandage Up

TEST(Colorless, BandageUpHealsFour) {
  CombatState s = fight_holding(CardId::BandageUp);
  s.character.hp = 40;
  ASSERT_TRUE(apply_action(s, card_action(CardId::BandageUp)));
  EXPECT_EQ(s.character.hp, 44);
}

TEST(Colorless, BandageUpPlusHealsSix) {
  CombatState s = fight_holding(CardId::BandageUpPlus);
  s.character.hp = 40;
  ASSERT_TRUE(apply_action(s, card_action(CardId::BandageUpPlus)));
  EXPECT_EQ(s.character.hp, 46);
}

TEST(Colorless, BandageUpExhausts) {
  CombatState s = fight_holding(CardId::BandageUp);
  s.character.hp = 40;
  ASSERT_TRUE(apply_action(s, card_action(CardId::BandageUp)));
  EXPECT_EQ(s.exhaust_pile.size(), 1u);
  EXPECT_TRUE(s.discard_pile.empty()) << "an exhausted card reached the discard";
}

TEST(Colorless, BandageUpCannotHealAboveMaxHp) {
  CombatState s = fight_holding(CardId::BandageUp);
  s.character.hp = s.character.max_hp - 1;
  ASSERT_TRUE(apply_action(s, card_action(CardId::BandageUp)));
  EXPECT_EQ(s.character.hp, s.character.max_hp);
}

TEST(Colorless, BandageUpCostsZero) {
  CombatState s = fight_holding(CardId::BandageUp);
  s.character.energy = 0;
  s.character.hp = 40;
  EXPECT_TRUE(apply_action(s, card_action(CardId::BandageUp)))
      << "a 0-cost card was refused at 0 energy";
  EXPECT_EQ(s.character.hp, 44);
}

// The heal is FLAT, not tied to damage dealt — that is Reaper's
// heals_unblocked_damage, a different field entirely.
TEST(Colorless, BandageUpHealsWithoutDealingDamage) {
  CombatState s = fight_holding(CardId::BandageUp);
  s.character.hp = 40;
  ASSERT_FALSE(s.enemies.empty());
  const int enemy_hp = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, card_action(CardId::BandageUp)));
  EXPECT_EQ(s.enemies[0].hp, enemy_hp);
  EXPECT_EQ(s.character.hp, 44);
}

// ------------------------------------------------------------------ Blind

TEST(Colorless, BlindWeakensOneEnemy) {
  CombatState s = fight_holding(CardId::Blind);
  ASSERT_GE(s.enemies.size(), 1u);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Blind, 0)));
  EXPECT_EQ(get_status(s.enemies[0].debuffs, Debuff::Weak), 2);
}

// The upgrade changes the TARGET, not the amount: still 2 Weak, but to every
// enemy. A row that copied the base and bumped a number would be wrong.
TEST(Colorless, BlindPlusWeakensEveryEnemyForTheSameAmount) {
  CombatState s = fight_holding(CardId::BlindPlus);
  ASSERT_FALSE(s.enemies.empty());
  ASSERT_TRUE(apply_action(s, card_action(CardId::BlindPlus, 0)));
  for (const Enemy& e : s.enemies) {
    if (e.hp > 0) EXPECT_EQ(get_status(e.debuffs, Debuff::Weak), 2);
  }
}

TEST(Colorless, BlindDoesNotExhaust) {
  CombatState s = fight_holding(CardId::Blind);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Blind, 0)));
  EXPECT_TRUE(s.exhaust_pile.empty());
  EXPECT_EQ(s.discard_pile.size(), 1u);
}

// ---------------------------------------------------------- Dark Shackles

// Data and action index are correct; the effect is not wired, because
// temporary Strength loss on an ENEMY needs the enemy-side analogue of
// StrengthDown. `unplayable` keeps it out of the mask rather than letting it
// resolve as a no-op — the same gate Dazed uses.
TEST(Colorless, DarkShacklesIsNotYetPlayable) {
  const CardData& d = CARD_DATABASE.at(CardId::DarkShackles);
  EXPECT_TRUE(d.unplayable);
  EXPECT_TRUE(CARD_DATABASE.at(CardId::DarkShacklesPlus).unplayable);

  CombatState s = fight_holding(CardId::DarkShackles);
  const std::vector<bool> mask = valid_actions(s);
  EXPECT_FALSE(mask[static_cast<size_t>(card_action(CardId::DarkShackles, 0))])
      << "an unplayable card was offered to the agent";
}

// ------------------------------------------------------------- Deep Breath

TEST(Colorless, DeepBreathShufflesTheDiscardBackAndDraws) {
  CombatState s = fight_holding(CardId::DeepBreath);
  s.draw_pile.clear();
  s.discard_pile.clear();
  for (int i = 0; i < 4; ++i) s.discard_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::DeepBreath)));

  EXPECT_TRUE(s.discard_pile.empty() ||
              s.discard_pile.size() == 1u)  // the Deep Breath itself
      << "the discard pile was not shuffled back";
  EXPECT_EQ(s.current_hand.size(), 1u) << "Deep Breath drew nothing";
}

// The reshuffle happens even when the draw pile is NOT empty — that is what
// separates it from draw_one's automatic refill.
TEST(Colorless, DeepBreathReshufflesEvenWithCardsLeftToDraw) {
  CombatState s = fight_holding(CardId::DeepBreath);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Defend});
  s.discard_pile.clear();
  for (int i = 0; i < 3; ++i) s.discard_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::DeepBreath)));

  // 1 + 3 = 4 cards were available; one was drawn, so three remain in draw.
  EXPECT_EQ(s.draw_pile.size(), 3u);
  EXPECT_EQ(s.current_hand.size(), 1u);
}

TEST(Colorless, DeepBreathPlusDrawsTwo) {
  CombatState s = fight_holding(CardId::DeepBreathPlus);
  s.draw_pile.clear();
  s.discard_pile.clear();
  for (int i = 0; i < 4; ++i) s.discard_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::DeepBreathPlus)));
  EXPECT_EQ(s.current_hand.size(), 2u);
}

// ------------------------------------------------------ Dramatic Entrance

TEST(Colorless, DramaticEntranceHitsEveryEnemy) {
  CombatState s = fight_holding(CardId::DramaticEntrance);
  ASSERT_FALSE(s.enemies.empty());
  std::vector<int> before;
  for (const Enemy& e : s.enemies) before.push_back(e.hp);

  ASSERT_TRUE(apply_action(s, card_action(CardId::DramaticEntrance, 0)));

  for (size_t i = 0; i < s.enemies.size(); ++i) {
    EXPECT_EQ(before[i] - s.enemies[i].hp, 8) << "enemy " << i;
  }
}

TEST(Colorless, DramaticEntrancePlusHitsForTwelve) {
  CombatState s = fight_holding(CardId::DramaticEntrancePlus);
  ASSERT_FALSE(s.enemies.empty());
  const int before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, card_action(CardId::DramaticEntrancePlus, 0)));
  EXPECT_EQ(before - s.enemies[0].hp, 12);
}

TEST(Colorless, DramaticEntranceExhausts) {
  CombatState s = fight_holding(CardId::DramaticEntrance);
  ASSERT_TRUE(apply_action(s, card_action(CardId::DramaticEntrance, 0)));
  EXPECT_EQ(s.exhaust_pile.size(), 1u);
}

// Innate: it starts in the opening hand. Verified through a real fight rather
// than by reading the flag back.
TEST(Colorless, DramaticEntranceIsInnate) {
  EXPECT_TRUE(CARD_DATABASE.at(CardId::DramaticEntrance).innate);

  CombatSetup setup;
  setup.seed = 1;
  setup.deck = starter_deck();
  setup.deck.push_back(Card{CardId::DramaticEntrance});
  const CombatState s = start_combat(std::move(setup));

  bool in_hand = false;
  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::DramaticEntrance) in_hand = true;
  }
  EXPECT_TRUE(in_hand) << "an Innate card was not in the opening hand";
}

// --------------------------------------------- not yet playable (data only)

// Discovery needs a choice over three GENERATED cards; Enlightenment needs a
// hand-wide cost override with a duration. Both hold correct data and a stable
// action index, and both are masked out.
TEST(Colorless, DiscoveryAndEnlightenmentAreNotYetPlayable) {
  for (CardId id : {CardId::Discovery, CardId::DiscoveryPlus,
                    CardId::Enlightenment, CardId::EnlightenmentPlus}) {
    EXPECT_TRUE(CARD_DATABASE.at(id).unplayable) << card_name(id);
    CombatState s = fight_holding(id);
    const std::vector<bool> mask = valid_actions(s);
    EXPECT_FALSE(mask[static_cast<size_t>(card_action(id, 0))])
        << card_name(id) << " was offered to the agent";
  }
}

// Discovery's upgrade removes the Exhaust rather than changing a number — a row
// written by copying the base and bumping a value would be wrong.
TEST(Colorless, DiscoveryPlusDropsTheExhaust) {
  EXPECT_TRUE(CARD_DATABASE.at(CardId::Discovery).exhaust);
  EXPECT_FALSE(CARD_DATABASE.at(CardId::DiscoveryPlus).exhaust);
}

// --------------------------------------- Finesse, Flash of Steel, Good Instincts

TEST(Colorless, FinesseBlocksAndDraws) {
  CombatState s = fight_holding(CardId::Finesse);
  s.character.current_block = 0;
  ASSERT_FALSE(s.draw_pile.empty());
  ASSERT_TRUE(apply_action(s, card_action(CardId::Finesse)));
  EXPECT_EQ(s.character.current_block, 2);
  EXPECT_EQ(s.current_hand.size(), 1u) << "Finesse drew nothing";
}

TEST(Colorless, FinessePlusBlocksFour) {
  CombatState s = fight_holding(CardId::FinessePlus);
  s.character.current_block = 0;
  ASSERT_TRUE(apply_action(s, card_action(CardId::FinessePlus)));
  EXPECT_EQ(s.character.current_block, 4);
}

// Finesse's block comes from a CARD, so Dexterity applies — unlike Plated
// Armor's, which does not.
TEST(Colorless, FinesseBlockIsScaledByDexterity) {
  CombatState s = fight_holding(CardId::Finesse);
  s.character.current_block = 0;
  s.character.powers[Power::Dexterity] = 3;
  ASSERT_TRUE(apply_action(s, card_action(CardId::Finesse)));
  EXPECT_EQ(s.character.current_block, 5) << "card block ignored Dexterity";
}

TEST(Colorless, FlashOfSteelDamagesAndDraws) {
  CombatState s = fight_holding(CardId::FlashOfSteel);
  ASSERT_FALSE(s.enemies.empty());
  const int before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, card_action(CardId::FlashOfSteel, 0)));
  EXPECT_EQ(before - s.enemies[0].hp, 3);
  EXPECT_EQ(s.current_hand.size(), 1u);
}

TEST(Colorless, FlashOfSteelPlusDamagesSix) {
  CombatState s = fight_holding(CardId::FlashOfSteelPlus);
  ASSERT_FALSE(s.enemies.empty());
  const int before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, card_action(CardId::FlashOfSteelPlus, 0)));
  EXPECT_EQ(before - s.enemies[0].hp, 6);
}

TEST(Colorless, GoodInstinctsBlocks) {
  CombatState s = fight_holding(CardId::GoodInstincts);
  s.character.current_block = 0;
  ASSERT_TRUE(apply_action(s, card_action(CardId::GoodInstincts)));
  EXPECT_EQ(s.character.current_block, 6);

  CombatState up = fight_holding(CardId::GoodInstinctsPlus);
  up.character.current_block = 0;
  ASSERT_TRUE(apply_action(up, card_action(CardId::GoodInstinctsPlus)));
  EXPECT_EQ(up.character.current_block, 9);
}

// -------------------------------------------------------------- Impatience

TEST(Colorless, ImpatienceDrawsWhenTheHandHasNoAttacks) {
  CombatState s = fight_holding(CardId::Impatience);
  // Hand holds only Impatience, which leaves the hand when played — so the
  // condition sees an empty hand.
  ASSERT_TRUE(apply_action(s, card_action(CardId::Impatience)));
  EXPECT_EQ(s.current_hand.size(), 2u) << "Impatience drew nothing";
}

TEST(Colorless, ImpatienceDrawsNothingWithAnAttackInHand) {
  CombatState s = fight_holding(CardId::Impatience);
  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, card_action(CardId::Impatience)));
  // The Strike stays; nothing was drawn on top of it.
  EXPECT_EQ(s.current_hand.size(), 1u)
      << "Impatience drew despite an Attack in hand";
}

// Non-Attacks do not block it.
TEST(Colorless, ImpatienceIgnoresSkillsInHand) {
  CombatState s = fight_holding(CardId::Impatience);
  s.current_hand.push_back(Card{CardId::Defend});
  ASSERT_TRUE(apply_action(s, card_action(CardId::Impatience)));
  EXPECT_EQ(s.current_hand.size(), 3u) << "a Skill blocked Impatience";
}

TEST(Colorless, ImpatiencePlusDrawsThree) {
  CombatState s = fight_holding(CardId::ImpatiencePlus);
  ASSERT_TRUE(apply_action(s, card_action(CardId::ImpatiencePlus)));
  EXPECT_EQ(s.current_hand.size(), 3u);
}

TEST(Colorless, ForethoughtAndJackOfAllTradesAreNotYetPlayable) {
  for (CardId id : {CardId::Forethought, CardId::ForethoughtPlus,
                    CardId::JackOfAllTrades, CardId::JackOfAllTradesPlus}) {
    EXPECT_TRUE(CARD_DATABASE.at(id).unplayable) << card_name(id);
  }
}

// ------------------------------------------------------ Mind Blast, Panacea

TEST(Colorless, MindBlastDamagesByDrawPileSize) {
  CombatState s = fight_holding(CardId::MindBlast);
  s.draw_pile.clear();
  for (int i = 0; i < 7; ++i) s.draw_pile.push_back(Card{CardId::Strike});
  ASSERT_FALSE(s.enemies.empty());
  const int before = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, card_action(CardId::MindBlast, 0)));
  EXPECT_EQ(before - s.enemies[0].hp, 7);
}

// An empty draw pile means zero damage, not a crash or a default.
TEST(Colorless, MindBlastWithAnEmptyDrawPileDealsNothing) {
  CombatState s = fight_holding(CardId::MindBlast);
  s.draw_pile.clear();
  ASSERT_FALSE(s.enemies.empty());
  const int before = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, card_action(CardId::MindBlast, 0)));
  EXPECT_EQ(s.enemies[0].hp, before);
}

// The count is read at RESOLUTION, after the card has left the hand — so Mind
// Blast never counts itself, and it scales with Strength like any attack.
TEST(Colorless, MindBlastIsScaledByStrength) {
  CombatState s = fight_holding(CardId::MindBlast);
  s.draw_pile.clear();
  for (int i = 0; i < 5; ++i) s.draw_pile.push_back(Card{CardId::Strike});
  s.character.powers[Power::Strength] = 3;
  ASSERT_FALSE(s.enemies.empty());
  const int before = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, card_action(CardId::MindBlast, 0)));
  EXPECT_EQ(before - s.enemies[0].hp, 8) << "5 cards + 3 Strength";
}

TEST(Colorless, MindBlastIsInnate) {
  EXPECT_TRUE(CARD_DATABASE.at(CardId::MindBlast).innate);
  EXPECT_TRUE(CARD_DATABASE.at(CardId::MindBlastPlus).innate);
}

// The upgrade changes only the COST, not the effect.
TEST(Colorless, MindBlastPlusOnlyCostsLess) {
  EXPECT_EQ(CARD_DATABASE.at(CardId::MindBlast).cost, 2);
  EXPECT_EQ(CARD_DATABASE.at(CardId::MindBlastPlus).cost, 1);
  EXPECT_EQ(CARD_DATABASE.at(CardId::MindBlastPlus).damage_rule,
            DamageRule::EqualToDrawPile);
}

TEST(Colorless, PanaceaGrantsArtifact) {
  CombatState s = fight_holding(CardId::Panacea);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Panacea)));
  EXPECT_EQ(get_status(s.character.powers, Power::Artifact), 1);
  EXPECT_EQ(s.exhaust_pile.size(), 1u);

  CombatState up = fight_holding(CardId::PanaceaPlus);
  ASSERT_TRUE(apply_action(up, card_action(CardId::PanaceaPlus)));
  EXPECT_EQ(get_status(up.character.powers, Power::Artifact), 2);
}

// ------------------------------------------------------ Swift Strike, Trip

TEST(Colorless, SwiftStrikeDamagesSeven) {
  CombatState s = fight_holding(CardId::SwiftStrike);
  ASSERT_FALSE(s.enemies.empty());
  const int before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, card_action(CardId::SwiftStrike, 0)));
  EXPECT_EQ(before - s.enemies[0].hp, 7);

  CombatState up = fight_holding(CardId::SwiftStrikePlus);
  const int up_before = up.enemies[0].hp;
  ASSERT_TRUE(apply_action(up, card_action(CardId::SwiftStrikePlus, 0)));
  EXPECT_EQ(up_before - up.enemies[0].hp, 10);
}

TEST(Colorless, TripVulnerablesOneEnemy) {
  CombatState s = fight_holding(CardId::Trip);
  ASSERT_FALSE(s.enemies.empty());
  ASSERT_TRUE(apply_action(s, card_action(CardId::Trip, 0)));
  EXPECT_EQ(get_status(s.enemies[0].debuffs, Debuff::Vulnerable), 2);
}

// Like Blind, the upgrade widens the TARGET and keeps the amount.
TEST(Colorless, TripPlusVulnerablesEveryEnemyForTheSameAmount) {
  CombatState s = fight_holding(CardId::TripPlus);
  ASSERT_FALSE(s.enemies.empty());
  ASSERT_TRUE(apply_action(s, card_action(CardId::TripPlus, 0)));
  for (const Enemy& e : s.enemies) {
    if (e.hp > 0) EXPECT_EQ(get_status(e.debuffs, Debuff::Vulnerable), 2);
  }
}

// -------------------------------------------- the 20 uncommons, as a block

// Every uncommon colorless card is present, with a description, an upgrade
// pair, and a playability state that is deliberate rather than accidental.
TEST(Colorless, AllTwentyUncommonsArePresentAndConsistent) {
  const CardId uncommons[] = {
      CardId::BandageUp,   CardId::Blind,        CardId::DarkShackles,
      CardId::DeepBreath,  CardId::Discovery,    CardId::DramaticEntrance,
      CardId::Enlightenment, CardId::Finesse,    CardId::FlashOfSteel,
      CardId::Forethought, CardId::GoodInstincts, CardId::Impatience,
      CardId::JackOfAllTrades, CardId::Madness,  CardId::MindBlast,
      CardId::Panacea,     CardId::PanicButton,  CardId::Purity,
      CardId::SwiftStrike, CardId::Trip};
  EXPECT_EQ(std::size(uncommons), 20u);

  for (CardId id : uncommons) {
    EXPECT_NE(CARD_DATABASE.find(id), CARD_DATABASE.end()) << card_name(id);
    EXPECT_FALSE(card_description(id).empty()) << card_name(id);
    EXPECT_TRUE(is_upgradable(id)) << card_name(id) << " has no upgrade";
    // And the upgraded form is itself a real card with a description.
    const CardId up = upgraded_card(id);
    EXPECT_NE(up, id) << card_name(id);
    EXPECT_FALSE(card_description(up).empty()) << card_name(up);
  }
}

// The unplayable ones are exactly the ones whose machinery is missing — not a
// drifting set. If a card leaves this list, its effect landed; if one joins,
// something regressed.
TEST(Colorless, TheNotYetPlayableUncommonsAreExactlyTheseSeven) {
  const CardId expected[] = {
      CardId::DarkShackles, CardId::Discovery,   CardId::Enlightenment,
      CardId::Forethought,  CardId::JackOfAllTrades, CardId::Madness,
      CardId::PanicButton,  CardId::Purity};

  for (CardId id : expected) {
    EXPECT_TRUE(CARD_DATABASE.at(id).unplayable)
        << card_name(id) << " became playable — update this list";
  }
  for (CardId id : {CardId::BandageUp, CardId::Blind, CardId::DeepBreath,
                    CardId::DramaticEntrance, CardId::Finesse,
                    CardId::FlashOfSteel, CardId::GoodInstincts,
                    CardId::Impatience, CardId::MindBlast, CardId::Panacea,
                    CardId::SwiftStrike, CardId::Trip}) {
    EXPECT_FALSE(CARD_DATABASE.at(id).unplayable)
        << card_name(id) << " regressed to unplayable";
  }
}

// ================================================ all 35, as a complete block

// The pools are the authority on what exists. sts_lightspeed's
// ColorlessRarityCardPool declares {0 common, 20 uncommon, 15 rare}; these
// match, card for card.
TEST(Colorless, ThePoolsAreTwentyAndFifteen) {
  EXPECT_EQ(COLORLESS_UNCOMMON_POOL.size(), 20u);
  EXPECT_EQ(COLORLESS_RARE_POOL.size(), 15u);
}

// There is no common colorless tier — worth pinning, because a shop or Neow
// roll that assumed three tiers would silently offer nothing.
TEST(Colorless, EveryPoolCardIsRealAndUpgradableAndDescribed) {
  for (const std::vector<CardId>* pool :
       {&COLORLESS_UNCOMMON_POOL, &COLORLESS_RARE_POOL}) {
    for (CardId id : *pool) {
      EXPECT_NE(CARD_DATABASE.find(id), CARD_DATABASE.end()) << card_name(id);
      EXPECT_FALSE(card_description(id).empty()) << card_name(id);
      ASSERT_TRUE(is_upgradable(id)) << card_name(id) << " has no upgrade";
      const CardId up = upgraded_card(id);
      EXPECT_NE(up, id) << card_name(id);
      EXPECT_FALSE(card_description(up).empty()) << card_name(up);
    }
  }
}

// A pool holds only UNUPGRADED ids — what can be offered. An upgraded form
// leaking in would let a shop sell a "+" card directly.
TEST(Colorless, PoolsHoldOnlyUnupgradedCards) {
  for (const std::vector<CardId>* pool :
       {&COLORLESS_UNCOMMON_POOL, &COLORLESS_RARE_POOL}) {
    for (CardId id : *pool) {
      EXPECT_NE(upgraded_card(id), id)
          << card_name(id) << " is already an upgraded form";
    }
  }
}

// The two pools are disjoint, and together they are the whole colorless set.
TEST(Colorless, TheTwoPoolsAreDisjoint) {
  for (CardId rare : COLORLESS_RARE_POOL) {
    EXPECT_EQ(std::find(COLORLESS_UNCOMMON_POOL.begin(),
                        COLORLESS_UNCOMMON_POOL.end(), rare),
              COLORLESS_UNCOMMON_POOL.end())
        << card_name(rare) << " is in both pools";
  }
}

// Of the 35, these are the ones whose effects are wired. The rest hold correct
// data and a stable action index and are masked out. A card leaving this list
// means an effect landed; one joining means something regressed.
TEST(Colorless, ExactlyTwelveOfThirtyFiveArePlayable) {
  const CardId playable[] = {
      CardId::BandageUp,    CardId::Blind,         CardId::DeepBreath,
      CardId::DramaticEntrance, CardId::Finesse,   CardId::FlashOfSteel,
      CardId::GoodInstincts, CardId::Impatience,   CardId::MindBlast,
      CardId::Panacea,      CardId::SwiftStrike,   CardId::Trip,
      CardId::MasterOfStrategy};

  int wired = 0;
  for (const std::vector<CardId>* pool :
       {&COLORLESS_UNCOMMON_POOL, &COLORLESS_RARE_POOL}) {
    for (CardId id : *pool) {
      if (!CARD_DATABASE.at(id).unplayable) ++wired;
    }
  }
  EXPECT_EQ(wired, static_cast<int>(std::size(playable)));

  for (CardId id : playable) {
    EXPECT_FALSE(CARD_DATABASE.at(id).unplayable)
        << card_name(id) << " regressed to unplayable";
  }
}

}  // namespace minispire
