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

#include <algorithm>
#include <vector>

#include "action.h"
#include "card.h"
#include "query.h"
#include "turn_loop.h"

namespace minispire {
namespace {

int card_action(CardId id, int target = 0) {
  return encode_action(ActionBlock::Combat, static_cast<int>(id), target);
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

// "Enemy loses 9 Strength this turn." StS expresses the duration as a permanent
// loss PLUS Shackled, which hands the Strength back at the end of the enemy's
// turn — so the loss covers the enemy's own attack, which is the whole card.
TEST(Colorless, DarkShacklesDropsEnemyStrength) {
  CombatState s = fight_holding(CardId::DarkShackles);
  s.enemies[0].powers[Power::Strength] = 5;

  ASSERT_TRUE(apply_action(s, card_action(CardId::DarkShackles)));

  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Strength), -4);
  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Shackled), 9);
}

TEST(Colorless, DarkShacklesPlusDropsFifteen) {
  CombatState s = fight_holding(CardId::DarkShacklesPlus);
  s.enemies[0].powers[Power::Strength] = 5;

  ASSERT_TRUE(apply_action(s, card_action(CardId::DarkShacklesPlus)));

  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Strength), -10);
  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Shackled), 15);
}

TEST(Colorless, DarkShacklesReturnsTheStrengthAtTheEndOfTheEnemyTurn) {
  CombatState s = fight_holding(CardId::DarkShackles);
  s.enemies[0].powers[Power::Strength] = 5;
  ASSERT_TRUE(apply_action(s, card_action(CardId::DarkShackles)));

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Strength), 5)
      << "Shackled must give back exactly what was taken";
  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Shackled), 0)
      << "Shackled removes itself once it has paid out";
}

// Artifact negates the loss outright, and StS then applies NO Shackled — there
// is nothing to give back, so a give-back would be a permanent Strength GAIN.
// The card checks this when played, which is why it is read at translation.
TEST(Colorless, DarkShacklesIsEatenByArtifactAndLeavesNoShackled) {
  CombatState s = fight_holding(CardId::DarkShackles);
  s.enemies[0].powers[Power::Strength] = 5;
  s.enemies[0].powers[Power::Artifact] = 1;

  ASSERT_TRUE(apply_action(s, card_action(CardId::DarkShackles)));

  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Strength), 5);
  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Shackled), 0);
  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Artifact), 0)
      << "the charge was spent negating the Strength loss";
}

// ---------------------------------------------------------- Thinking Ahead

// Warcry's shape: the draw resolves first, so a just-drawn card is a legal
// option to put back.
TEST(Colorless, ThinkingAheadDrawsTwoThenOffersTheHand) {
  CombatState s = fight_holding(CardId::ThinkingAhead);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Cleave});
  s.draw_pile.push_back(Card{CardId::Bash});  // back() is drawn first

  ASSERT_TRUE(apply_action(s, card_action(CardId::ThinkingAhead)));

  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.kind, ChoiceKind::HandToTopOfDraw);
  bool saw_drawn = false;
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == CardId::Bash) saw_drawn = true;
  }
  EXPECT_TRUE(saw_drawn) << "the choice must come after the draw";
}

TEST(Colorless, ThinkingAheadPutsTheChosenCardOnTopOfTheDraw) {
  CombatState s = fight_holding(CardId::ThinkingAhead);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Cleave});
  s.draw_pile.push_back(Card{CardId::Bash});
  ASSERT_TRUE(apply_action(s, card_action(CardId::ThinkingAhead)));
  ASSERT_TRUE(s.pending_choice.active());

  const CardId chosen = s.pending_choice.options[0].card_id;
  ASSERT_TRUE(resolve_choice(s, 0));

  ASSERT_FALSE(s.draw_pile.empty());
  EXPECT_EQ(s.draw_pile.back().card_id, chosen);
}

TEST(Colorless, ThinkingAheadExhaustsAndThePlusDoesNot) {
  for (CardId id : {CardId::ThinkingAhead, CardId::ThinkingAheadPlus}) {
    CombatState s = fight_holding(id);
    s.draw_pile.clear();
    // TWO distinct cards, so the put-back is a real prompt: a choice with one
    // legal option auto-resolves instead of pausing (see the test below).
    s.draw_pile.push_back(Card{CardId::Defend});
    s.draw_pile.push_back(Card{CardId::Strike});
    ASSERT_TRUE(apply_action(s, card_action(id))) << card_name(id);
    ASSERT_TRUE(s.pending_choice.active()) << card_name(id);
    ASSERT_TRUE(resolve_choice(s, 0)) << card_name(id);

    const bool exhausted =
        !s.exhaust_pile.empty() && s.exhaust_pile.back().card_id == id;
    const bool discarded =
        !s.discard_pile.empty() && s.discard_pile.back().card_id == id;
    EXPECT_EQ(exhausted, id == CardId::ThinkingAhead) << card_name(id);
    EXPECT_EQ(discarded, id == CardId::ThinkingAheadPlus) << card_name(id);
  }
}

// One legal option is no decision, so the engine applies it without pausing —
// StS does the same ("if there is only one card ... it will automatically be
// placed on top of your draw pile"). Worth pinning here because Thinking Ahead
// reaches it often: draw 2 from a nearly empty pile and one card is all you get.
TEST(Colorless, ThinkingAheadAutoResolvesWhenOnlyOneCardCouldBePutBack) {
  CombatState s = fight_holding(CardId::ThinkingAhead);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::ThinkingAhead)));

  EXPECT_FALSE(s.pending_choice.active()) << "a one-option choice must not pause";
  ASSERT_FALSE(s.draw_pile.empty());
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Strike)
      << "the only candidate went back on top by itself";
  EXPECT_TRUE(s.current_hand.empty());
}

// ------------------------------------------------------------- Apotheosis

TEST(Colorless, ApotheosisUpgradesEveryPile) {
  CombatState s = fight_holding(CardId::Apotheosis);
  s.current_hand.push_back(Card{CardId::Strike});
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Defend});
  s.discard_pile.clear();
  s.discard_pile.push_back(Card{CardId::Bash});
  s.exhaust_pile.clear();
  s.exhaust_pile.push_back(Card{CardId::Cleave});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Apotheosis)));

  EXPECT_EQ(s.current_hand.back().card_id, CardId::StrikePlus);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::DefendPlus);
  EXPECT_EQ(s.discard_pile.back().card_id, CardId::BashPlus);
  EXPECT_EQ(s.exhaust_pile.front().card_id, CardId::CleavePlus)
      << "the exhaust pile is upgraded too";
}

// It is in flight while it resolves, so it is in no pile to find itself.
TEST(Colorless, ApotheosisDoesNotUpgradeItself) {
  CombatState s = fight_holding(CardId::Apotheosis);
  s.exhaust_pile.clear();

  ASSERT_TRUE(apply_action(s, card_action(CardId::Apotheosis)));

  ASSERT_EQ(s.exhaust_pile.size(), 1u);
  EXPECT_EQ(s.exhaust_pile.back().card_id, CardId::Apotheosis)
      << "Apotheosis upgraded itself on the way to the exhaust pile";
}

TEST(Colorless, ApotheosisLeavesStatusCardsAlone) {
  CombatState s = fight_holding(CardId::Apotheosis);
  s.current_hand.push_back(Card{CardId::Dazed});
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Slimed});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Apotheosis)));

  EXPECT_EQ(s.current_hand.back().card_id, CardId::Dazed);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Slimed);
}

// --------------------------------------------------------------- Violence

TEST(Colorless, ViolencePullsThreeAttacksOutOfTheDrawPile) {
  CombatState s = fight_holding(CardId::Violence);
  s.draw_pile.clear();
  for (int i = 0; i < 3; ++i) s.draw_pile.push_back(Card{CardId::Strike});
  for (int i = 0; i < 2; ++i) s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Violence)));

  int strikes_in_hand = 0;
  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::Strike) ++strikes_in_hand;
  }
  EXPECT_EQ(strikes_in_hand, 3);
  EXPECT_EQ(s.draw_pile.size(), 2u) << "only the Defends should remain";
  for (const Card& c : s.draw_pile) EXPECT_EQ(c.card_id, CardId::Defend);
}

TEST(Colorless, ViolencePlusPullsFour) {
  CombatState s = fight_holding(CardId::ViolencePlus);
  s.draw_pile.clear();
  for (int i = 0; i < 5; ++i) s.draw_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::ViolencePlus)));

  EXPECT_EQ(s.current_hand.size(), 4u);
  EXPECT_EQ(s.draw_pile.size(), 1u);
}

TEST(Colorless, ViolenceTakesWhatItCanWhenAttacksAreScarce) {
  CombatState s = fight_holding(CardId::Violence);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});
  s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Violence)));

  EXPECT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand.back().card_id, CardId::Strike);
}

TEST(Colorless, ViolenceOverflowsIntoTheDiscardWhenTheHandIsFull) {
  CombatState s = fight_holding(CardId::Violence);
  // Nine more cards: the hand is at the limit with Violence still in it, so
  // exactly one pulled card fits once Violence leaves.
  for (int i = 0; i < HAND_SIZE_LIMIT - 1; ++i) {
    s.current_hand.push_back(Card{CardId::Defend});
  }
  s.draw_pile.clear();
  for (int i = 0; i < 3; ++i) s.draw_pile.push_back(Card{CardId::Strike});
  s.discard_pile.clear();

  ASSERT_TRUE(apply_action(s, card_action(CardId::Violence)));

  EXPECT_EQ(s.current_hand.size(), static_cast<std::size_t>(HAND_SIZE_LIMIT));
  EXPECT_EQ(s.discard_pile.size(), 2u) << "the overflow goes to the discard";
  for (const Card& c : s.discard_pile) EXPECT_EQ(c.card_id, CardId::Strike);
}

// ----------------------------------------------------------- Panic Button

TEST(Colorless, PanicButtonBlocksThirtyAndBansCardBlockAfterwards) {
  CombatState s = fight_holding(CardId::PanicButton);
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, card_action(CardId::PanicButton)));
  EXPECT_EQ(s.character.current_block, 30) << "its own block is not banned";
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::NoBlock), 2);

  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  EXPECT_EQ(s.character.current_block, 30) << "Defend gained nothing";
}

TEST(Colorless, PanicButtonPlusBlocksForty) {
  CombatState s = fight_holding(CardId::PanicButtonPlus);
  ASSERT_TRUE(apply_action(s, card_action(CardId::PanicButtonPlus)));
  EXPECT_EQ(s.character.current_block, 40);
}

// Only block FROM CARDS. Plated Armor grants at end of turn and must survive.
TEST(Colorless, NoBlockLeavesPowerBlockAlone) {
  CombatState s = fight_holding(CardId::PanicButton);
  s.character.powers[Power::PlatedArmor] = 7;
  ASSERT_TRUE(apply_action(s, card_action(CardId::PanicButton)));
  const int before = s.character.current_block;

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  EXPECT_GT(before, 0);
  EXPECT_GT(get_status(s.character.powers, Power::PlatedArmor), 0)
      << "Plated Armor should still be granting block";
}

// Two turns, counting the one it was played on.
TEST(Colorless, NoBlockExpiresAfterTheFollowingTurn) {
  CombatState s = fight_holding(CardId::PanicButton);
  ASSERT_TRUE(apply_action(s, card_action(CardId::PanicButton)));
  ASSERT_EQ(get_status(s.character.debuffs, Debuff::NoBlock), 2);

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::NoBlock), 1)
      << "still banned on the turn after";

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::NoBlock), 0);

  s.current_hand.push_back(Card{CardId::Defend});
  s.character.energy = 99;
  const int before = s.character.current_block;
  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  EXPECT_GT(s.character.current_block, before) << "the ban has lifted";
}

// No Block is a DEBUFF in StS, so the player's own Artifact eats it — Panacea
// into Panic Button is 30 block with no drawback.
TEST(Colorless, ArtifactNegatesNoBlock) {
  CombatState s = fight_holding(CardId::PanicButton);
  s.character.powers[Power::Artifact] = 1;
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, card_action(CardId::PanicButton)));
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::NoBlock), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::Artifact), 0);

  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  EXPECT_GT(s.character.current_block, 30) << "Defend still blocks";
}

// --------------------------------------------------------- Hand of Greed

TEST(Colorless, HandOfGreedRecordsGoldWhenTheHitIsFatal) {
  CombatState s = fight_holding(CardId::HandOfGreed);
  s.enemies[0].hp = 5;

  ASSERT_TRUE(apply_action(s, card_action(CardId::HandOfGreed, 0)));

  EXPECT_EQ(s.enemies[0].hp, 0);
  EXPECT_EQ(s.gold_gained, 20);
}

TEST(Colorless, HandOfGreedPlusRecordsTwentyFive) {
  CombatState s = fight_holding(CardId::HandOfGreedPlus);
  s.enemies[0].hp = 5;

  ASSERT_TRUE(apply_action(s, card_action(CardId::HandOfGreedPlus, 0)));

  EXPECT_EQ(s.gold_gained, 25);
}

// "If Fatal" — damage alone pays nothing.
TEST(Colorless, HandOfGreedRecordsNothingWhenTheEnemySurvives) {
  CombatState s = fight_holding(CardId::HandOfGreed);
  s.enemies[0].hp = 60;

  ASSERT_TRUE(apply_action(s, card_action(CardId::HandOfGreed, 0)));

  EXPECT_GT(s.enemies[0].hp, 0);
  EXPECT_EQ(s.gold_gained, 0);
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

// Enlightenment landed in batch 3 (the hand-wide cap). Kept as the inverse of
// the test it replaces: the card is now offered to the agent.
TEST(Colorless, EnlightenmentIsOfferedToTheAgent) {
  for (CardId id : {CardId::Enlightenment, CardId::EnlightenmentPlus}) {
    EXPECT_FALSE(CARD_DATABASE.at(id).unplayable) << card_name(id);
    CombatState s = fight_holding(id);
    const std::vector<bool> mask = valid_actions(s);
    EXPECT_TRUE(mask[static_cast<size_t>(card_action(id, 0))])
        << card_name(id) << " is implemented but still masked out";
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

// Forethought landed in batch 4. Only the + is still out, because "any number"
// is a multi-select (batch 6) — see ForethoughtPlusIsStillMultiSelectAndUnplayable.

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
TEST(Colorless, PurityIsTheLastUnplayableUncommon) {
  const CardId expected[] = {CardId::Purity};

  for (CardId id : expected) {
    EXPECT_TRUE(CARD_DATABASE.at(id).unplayable)
        << card_name(id) << " became playable — update this list";
  }
  for (CardId id : {CardId::BandageUp, CardId::Blind, CardId::DarkShackles,
                    CardId::DeepBreath, CardId::Discovery,
                    CardId::DramaticEntrance, CardId::Enlightenment,
                    CardId::Finesse, CardId::FlashOfSteel,
                    CardId::Forethought, CardId::GoodInstincts,
                    CardId::Impatience, CardId::JackOfAllTrades,
                    CardId::Madness, CardId::MindBlast, CardId::Panacea,
                    CardId::PanicButton, CardId::SwiftStrike, CardId::Trip}) {
    EXPECT_FALSE(CARD_DATABASE.at(id).unplayable)
        << card_name(id) << " regressed to unplayable";
  }
}

// ======================================= batch 2: random in-combat generation

namespace {

bool in_pool(const std::vector<CardId>& pool, CardId id) {
  return std::find(pool.begin(), pool.end(), id) != pool.end();
}

}  // namespace

TEST(Colorless, JackOfAllTradesAddsOneColorlessCardAtFullPrice) {
  CombatState s = fight_holding(CardId::JackOfAllTrades);

  ASSERT_TRUE(apply_action(s, card_action(CardId::JackOfAllTrades)));

  ASSERT_EQ(s.current_hand.size(), 1u);
  const Card& made = s.current_hand[0];
  EXPECT_TRUE(in_pool(generatable_colorless_pool(), made.card_id))
      << card_name(made.card_id) << " is not a generatable colorless card";
  EXPECT_EQ(made.cost_override, kNoCostOverride)
      << "Jack of All Trades adds cards at full price, unlike Transmutation";
}

TEST(Colorless, JackOfAllTradesPlusAddsTwo) {
  CombatState s = fight_holding(CardId::JackOfAllTradesPlus);

  ASSERT_TRUE(apply_action(s, card_action(CardId::JackOfAllTradesPlus)));

  EXPECT_EQ(s.current_hand.size(), 2u);
  for (const Card& c : s.current_hand) {
    EXPECT_TRUE(in_pool(generatable_colorless_pool(), c.card_id));
  }
}

// StS patch 44: no source of random in-combat generation may produce a healing
// card. Bandage Up is the colorless one; Feed and Reaper are the class ones.
TEST(Colorless, GenerationNeverRollsAHealingCard) {
  EXPECT_FALSE(in_pool(generatable_colorless_pool(), CardId::BandageUp));
  EXPECT_FALSE(in_pool(generatable_class_pool(), CardId::Feed));
  EXPECT_FALSE(in_pool(generatable_class_pool(), CardId::Reaper));
  EXPECT_FALSE(in_pool(generatable_class_attack_pool(), CardId::Feed));
  EXPECT_FALSE(in_pool(generatable_class_attack_pool(), CardId::Reaper));
  EXPECT_EQ(generatable_colorless_pool().size(), 34u)
      << "35 colorless cards minus Bandage Up";

  // And in play, across many rolls.
  for (uint32_t seed = 0; seed < 60; ++seed) {
    CombatState s = fight_holding(CardId::JackOfAllTradesPlus, seed);
    ASSERT_TRUE(apply_action(s, card_action(CardId::JackOfAllTradesPlus)));
    for (const Card& c : s.current_hand) {
      EXPECT_FALSE(card_is_healing(c.card_id)) << card_name(c.card_id);
    }
  }
}

TEST(Colorless, TransmutationAddsOneCardPerEnergyFreeForTheTurn) {
  CombatState s = fight_holding(CardId::Transmutation);
  s.character.energy = 3;

  ASSERT_TRUE(apply_action(s, card_action(CardId::Transmutation)));

  EXPECT_EQ(s.current_hand.size(), 3u) << "X = the energy spent";
  for (const Card& c : s.current_hand) {
    EXPECT_TRUE(in_pool(generatable_colorless_pool(), c.card_id));
    EXPECT_EQ(c.cost_override, 0);
    EXPECT_EQ(c.cost_duration, CostDuration::ThisTurn);
    EXPECT_EQ(instance_effective_cost(s, c), 0);
  }
  EXPECT_EQ(s.character.energy, 0) << "X-cost spends everything";
}

// The upgrade changes WHAT is generated, not how many.
TEST(Colorless, TransmutationPlusGeneratesUpgradedCards) {
  CombatState s = fight_holding(CardId::TransmutationPlus);
  s.character.energy = 2;

  ASSERT_TRUE(apply_action(s, card_action(CardId::TransmutationPlus)));

  ASSERT_EQ(s.current_hand.size(), 2u);
  for (const Card& c : s.current_hand) {
    EXPECT_FALSE(in_pool(generatable_colorless_pool(), c.card_id))
        << card_name(c.card_id) << " is the unupgraded form";
    EXPECT_EQ(upgraded_card(c.card_id), c.card_id)
        << card_name(c.card_id) << " can still be upgraded, so it is not a +";
  }
}

// The discount is for THIS turn, so it must be gone next turn — on whichever
// pile the card ended up in.
TEST(Colorless, TransmutationsDiscountExpiresAtEndOfTurn) {
  CombatState s = fight_holding(CardId::Transmutation);
  s.character.energy = 2;
  ASSERT_TRUE(apply_action(s, card_action(CardId::Transmutation)));

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  for (const std::vector<Card>* pile :
       {&s.current_hand, &s.draw_pile, &s.discard_pile, &s.exhaust_pile}) {
    for (const Card& c : *pile) {
      EXPECT_EQ(c.cost_override, kNoCostOverride) << card_name(c.card_id);
      EXPECT_EQ(c.cost_duration, CostDuration::None) << card_name(c.card_id);
    }
  }
}

TEST(Colorless, MagnetismAddsAColorlessCardEachTurnAtFullPrice) {
  CombatState s = fight_holding(CardId::Magnetism);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Magnetism)));
  ASSERT_EQ(get_status(s.character.powers, Power::Magnetism), 1);

  const std::size_t before = s.current_hand.size();
  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  // The new turn drew a hand AND gained one generated card.
  int colorless_in_hand = 0;
  for (const Card& c : s.current_hand) {
    if (in_pool(generatable_colorless_pool(), c.card_id)) {
      ++colorless_in_hand;
      EXPECT_EQ(c.cost_override, kNoCostOverride)
          << "Magnetism adds at full price";
    }
  }
  EXPECT_GE(colorless_in_hand, 1);
  EXPECT_GT(s.current_hand.size(), before);
}

// --------------------------------------------------------------- Discovery

TEST(Colorless, DiscoveryOffersThreeDistinctClassCards) {
  CombatState s = fight_holding(CardId::Discovery);

  ASSERT_TRUE(apply_action(s, card_action(CardId::Discovery)));

  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.kind, ChoiceKind::DiscoverCard);
  ASSERT_EQ(s.pending_choice.num_options, 3);
  for (int i = 0; i < 3; ++i) {
    const CardId id = s.pending_choice.options[i].card_id;
    EXPECT_TRUE(in_pool(generatable_class_pool(), id)) << card_name(id);
    for (int j = i + 1; j < 3; ++j) {
      EXPECT_NE(id, s.pending_choice.options[j].card_id)
          << "the three offers must be distinct";
    }
  }
}

TEST(Colorless, DiscoveryAddsTheChosenCardFreeForTheTurn) {
  CombatState s = fight_holding(CardId::Discovery);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Discovery)));
  ASSERT_TRUE(s.pending_choice.active());
  const CardId chosen = s.pending_choice.options[1].card_id;

  ASSERT_TRUE(resolve_choice(s, 1));

  bool found = false;
  for (const Card& c : s.current_hand) {
    if (c.card_id != chosen) continue;
    found = true;
    EXPECT_EQ(c.cost_override, 0);
    EXPECT_EQ(c.cost_duration, CostDuration::ThisTurn);
  }
  EXPECT_TRUE(found) << "the chosen card joins the hand";
}

// "Adding one is mandatory" — there is no skip.
TEST(Colorless, DiscoveryCannotBeDeclined) {
  CombatState s = fight_holding(CardId::Discovery);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Discovery)));
  ASSERT_TRUE(s.pending_choice.active());

  EXPECT_FALSE(s.pending_choice.is_optional);
  EXPECT_FALSE(resolve_choice(s, kDeclineChoice));
  EXPECT_TRUE(s.pending_choice.active()) << "the choice is still open";
}

TEST(Colorless, DiscoveryExhaustsAndThePlusDoesNot) {
  for (CardId id : {CardId::Discovery, CardId::DiscoveryPlus}) {
    CombatState s = fight_holding(id);
    ASSERT_TRUE(apply_action(s, card_action(id))) << card_name(id);
    ASSERT_TRUE(resolve_choice(s, 0)) << card_name(id);

    const bool exhausted =
        !s.exhaust_pile.empty() && s.exhaust_pile.back().card_id == id;
    EXPECT_EQ(exhausted, id == CardId::Discovery) << card_name(id);
  }
}

// ------------------------------------------------- Infernal Blade's pool fix

// It generates an IRONCLAD ATTACK. Before batch 2 it rolled over every
// Attack-typed id in CARD_DATABASE, which after the colorless block included
// colorless attacks (Hand of Greed, unplayable at the time), upgraded ids and
// the Rampage / Searing Blow rung ladders.
TEST(Colorless, InfernalBladeRollsOnlyGeneratableClassAttacks) {
  for (uint32_t seed = 0; seed < 60; ++seed) {
    CombatState s = fight_holding(CardId::InfernalBlade, seed);
    ASSERT_TRUE(apply_action(s, card_action(CardId::InfernalBlade)));
    ASSERT_EQ(s.current_hand.size(), 1u);
    const CardId got = s.current_hand[0].card_id;
    EXPECT_TRUE(in_pool(generatable_class_attack_pool(), got))
        << card_name(got) << " is not a generatable Ironclad Attack";
    EXPECT_EQ(CARD_DATABASE.at(got).type, CardType::Attack);
    EXPECT_FALSE(CARD_DATABASE.at(got).unplayable)
        << card_name(got) << " cannot be played, so it is a dead card";
  }
}

// Generation draws from its OWN stream (§3.5). Changing only the card seed must
// change what is generated while leaving the shuffle untouched — that isolation
// is what keeps an Attack Potion's offer stable while an Infernal Blade shifts
// it.
TEST(Colorless, GenerationUsesItsOwnRngStream) {
  auto fight = [](uint32_t card_seed) {
    CombatSetup setup;
    setup.seed = 4;
    setup.card_seed = card_seed;
    setup.deck = starter_deck();
    CombatState s = start_combat(std::move(setup));
    s.current_hand.clear();
    s.current_hand.push_back(Card{CardId::JackOfAllTrades});
    s.character.energy = 99;
    return s;
  };

  CombatState a = fight(1);
  CombatState b = fight(2);
  ASSERT_EQ(a.draw_pile.size(), b.draw_pile.size());
  for (std::size_t i = 0; i < a.draw_pile.size(); ++i) {
    ASSERT_EQ(a.draw_pile[i].card_id, b.draw_pile[i].card_id)
        << "the shuffle must not depend on the generation stream";
  }

  ASSERT_TRUE(apply_action(a, card_action(CardId::JackOfAllTrades)));
  ASSERT_TRUE(apply_action(b, card_action(CardId::JackOfAllTrades)));
  EXPECT_NE(a.current_hand[0].card_id, b.current_hand[0].card_id)
      << "a different generation seed should roll differently";
}

// ========================== batch 3: the "this combat" cost duration

TEST(Colorless, MadnessDiscountsExactlyOneCardInHand) {
  CombatState s = fight_holding(CardId::Madness);
  s.current_hand.push_back(Card{CardId::Bash});   // cost 2
  s.current_hand.push_back(Card{CardId::Strike});  // cost 1

  ASSERT_TRUE(apply_action(s, card_action(CardId::Madness)));

  int discounted = 0;
  for (const Card& c : s.current_hand) {
    if (c.cost_override == kNoCostOverride) continue;
    ++discounted;
    EXPECT_EQ(c.cost_override, 0);
    EXPECT_EQ(c.cost_duration, CostDuration::ThisCombat);
  }
  EXPECT_EQ(discounted, 1) << "exactly one copy, not one card TYPE";
}

// "This combat", so unlike Transmutation's it must survive the turn boundary.
TEST(Colorless, MadnessDiscountSurvivesTheTurn) {
  CombatState s = fight_holding(CardId::Madness);
  s.draw_pile.clear();
  s.current_hand.push_back(Card{CardId::Bash});
  ASSERT_TRUE(apply_action(s, card_action(CardId::Madness)));

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  bool found = false;
  for (const std::vector<Card>* pile :
       {&s.current_hand, &s.draw_pile, &s.discard_pile}) {
    for (const Card& c : *pile) {
      if (c.card_id == CardId::Bash && c.cost_override == 0) {
        found = true;
        EXPECT_EQ(c.cost_duration, CostDuration::ThisCombat);
      }
    }
  }
  EXPECT_TRUE(found) << "a this-combat discount must not expire with the turn";
}

// X-cost cards carry a sentinel rather than a number, and StS never targets
// them.
TEST(Colorless, MadnessIgnoresXCostCards) {
  CombatState s = fight_holding(CardId::Madness);
  s.current_hand.push_back(Card{CardId::Whirlwind});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Madness)));

  for (const Card& c : s.current_hand) {
    EXPECT_EQ(c.cost_override, kNoCostOverride) << card_name(c.card_id);
  }
}

// The second tier: when every card is already free, Madness still lands on one
// whose PRINTED cost is above 0 — which is how it works on a card another
// effect discounted this turn.
TEST(Colorless, MadnessFallsBackToCardsAlreadyDiscountedThisTurn) {
  CombatState s = fight_holding(CardId::Madness);
  s.current_hand.push_back(Card{CardId::Bash});
  s.current_hand.back().cost_override = 0;
  s.current_hand.back().cost_duration = CostDuration::ThisTurn;

  ASSERT_TRUE(apply_action(s, card_action(CardId::Madness)));

  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Bash);
  EXPECT_EQ(s.current_hand[0].cost_duration, CostDuration::ThisCombat)
      << "the this-turn discount was upgraded to a this-combat one";
}

// ----------------------------------------------------------- Enlightenment

TEST(Colorless, EnlightenmentCapsHandCostsAtOne) {
  CombatState s = fight_holding(CardId::Enlightenment);
  s.current_hand.push_back(Card{CardId::Bash});        // cost 2 -> 1
  s.current_hand.push_back(Card{CardId::Strike});      // cost 1, untouched
  s.current_hand.push_back(Card{CardId::SwiftStrike}); // cost 0, untouched

  ASSERT_TRUE(apply_action(s, card_action(CardId::Enlightenment)));

  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::Bash) {
      EXPECT_EQ(c.cost_override, 1) << "capped, not zeroed";
      EXPECT_EQ(c.cost_duration, CostDuration::ThisTurn);
    } else {
      EXPECT_EQ(c.cost_override, kNoCostOverride)
          << card_name(c.card_id) << " already cost 1 or less";
    }
  }
}

TEST(Colorless, EnlightenmentLastsOnlyTheTurnAndThePlusTheCombat) {
  for (CardId id : {CardId::Enlightenment, CardId::EnlightenmentPlus}) {
    CombatState s = fight_holding(id);
    s.draw_pile.clear();
    s.current_hand.push_back(Card{CardId::Bash});
    ASSERT_TRUE(apply_action(s, card_action(id))) << card_name(id);

    ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

    bool still_capped = false;
    for (const std::vector<Card>* pile :
         {&s.current_hand, &s.draw_pile, &s.discard_pile}) {
      for (const Card& c : *pile) {
        if (c.card_id == CardId::Bash && c.cost_override == 1) {
          still_capped = true;
        }
      }
    }
    EXPECT_EQ(still_capped, id == CardId::EnlightenmentPlus) << card_name(id);
  }
}

// A cap never raises a cost: a Madness-discounted card stays at 0.
TEST(Colorless, EnlightenmentDoesNotUndoACheaperDiscount) {
  CombatState s = fight_holding(CardId::Enlightenment);
  s.current_hand.push_back(Card{CardId::Bash});
  s.current_hand.back().cost_override = 0;
  s.current_hand.back().cost_duration = CostDuration::ThisCombat;

  ASSERT_TRUE(apply_action(s, card_action(CardId::Enlightenment)));

  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].cost_override, 0);
  EXPECT_EQ(s.current_hand[0].cost_duration, CostDuration::ThisCombat);
}

TEST(Colorless, EnlightenmentIgnoresXCostCards) {
  CombatState s = fight_holding(CardId::Enlightenment);
  s.current_hand.push_back(Card{CardId::Whirlwind});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Enlightenment)));

  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].cost_override, kNoCostOverride);
  EXPECT_EQ(effective_cost(s, CardId::Whirlwind), kXCost);
}

// ------------------------------------------------- Chrysalis / Metamorphosis

TEST(Colorless, ChrysalisShufflesThreeFreeSkillsIntoTheDrawPile) {
  CombatState s = fight_holding(CardId::Chrysalis);
  s.draw_pile.clear();

  ASSERT_TRUE(apply_action(s, card_action(CardId::Chrysalis)));

  ASSERT_EQ(s.draw_pile.size(), 3u);
  for (const Card& c : s.draw_pile) {
    EXPECT_EQ(CARD_DATABASE.at(c.card_id).type, CardType::Skill);
    EXPECT_TRUE(in_pool(generatable_class_skill_pool(), c.card_id))
        << card_name(c.card_id);
    EXPECT_EQ(c.cost_override, 0);
    EXPECT_EQ(c.cost_duration, CostDuration::ThisCombat);
  }
}

TEST(Colorless, ChrysalisPlusShufflesFive) {
  CombatState s = fight_holding(CardId::ChrysalisPlus);
  s.draw_pile.clear();

  ASSERT_TRUE(apply_action(s, card_action(CardId::ChrysalisPlus)));

  EXPECT_EQ(s.draw_pile.size(), 5u);
}

TEST(Colorless, MetamorphosisShufflesAttacksNotSkills) {
  CombatState s = fight_holding(CardId::Metamorphosis);
  s.draw_pile.clear();

  ASSERT_TRUE(apply_action(s, card_action(CardId::Metamorphosis)));

  ASSERT_EQ(s.draw_pile.size(), 3u);
  for (const Card& c : s.draw_pile) {
    EXPECT_EQ(CARD_DATABASE.at(c.card_id).type, CardType::Attack);
    EXPECT_TRUE(in_pool(generatable_class_attack_pool(), c.card_id))
        << card_name(c.card_id);
  }
}

TEST(Colorless, MetamorphosisPlusShufflesFive) {
  CombatState s = fight_holding(CardId::MetamorphosisPlus);
  s.draw_pile.clear();

  ASSERT_TRUE(apply_action(s, card_action(CardId::MetamorphosisPlus)));

  EXPECT_EQ(s.draw_pile.size(), 5u);
}

// The whole reason these use the combat duration: their cards sit in the DRAW
// pile, so most are drawn on a later turn. A this-turn discount would have
// expired before the card was ever seen.
TEST(Colorless, ShuffledInCardsAreStillFreeNextTurn) {
  CombatState s = fight_holding(CardId::Chrysalis);
  s.draw_pile.clear();
  ASSERT_TRUE(apply_action(s, card_action(CardId::Chrysalis)));

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  int free_cards = 0;
  for (const std::vector<Card>* pile :
       {&s.current_hand, &s.draw_pile, &s.discard_pile}) {
    for (const Card& c : *pile) {
      if (c.cost_override == 0 && c.cost_duration == CostDuration::ThisCombat) {
        ++free_cards;
      }
    }
  }
  EXPECT_EQ(free_cards, 3) << "all three survive into the next turn";
}

// ================================= batch 4: choices over the draw pile

// StS greys the card out when the draw pile holds nothing of its type, so this
// is a mask rule rather than an effect that fizzles.
TEST(Colorless, SecretTechniqueIsMaskedOutWithoutASkillInTheDrawPile) {
  CombatState s = fight_holding(CardId::SecretTechnique);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});  // an Attack, not a Skill

  EXPECT_FALSE(valid_actions(s)[static_cast<std::size_t>(
      card_action(CardId::SecretTechnique))]);

  s.draw_pile.push_back(Card{CardId::Defend});  // now a Skill is there
  EXPECT_TRUE(valid_actions(s)[static_cast<std::size_t>(
      card_action(CardId::SecretTechnique))]);
}

TEST(Colorless, SecretWeaponIsMaskedOutWithoutAnAttackInTheDrawPile) {
  CombatState s = fight_holding(CardId::SecretWeapon);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Defend});

  EXPECT_FALSE(valid_actions(s)[static_cast<std::size_t>(
      card_action(CardId::SecretWeapon))]);

  s.draw_pile.push_back(Card{CardId::Strike});
  EXPECT_TRUE(valid_actions(s)[static_cast<std::size_t>(
      card_action(CardId::SecretWeapon))]);
}

// One candidate is no decision, so it resolves without a prompt (as StS does).
TEST(Colorless, SecretTechniqueTakesTheOnlySkillWithoutPrompting) {
  CombatState s = fight_holding(CardId::SecretTechnique);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});
  s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, card_action(CardId::SecretTechnique)));

  EXPECT_FALSE(s.pending_choice.active());
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Defend);
  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile[0].card_id, CardId::Strike) << "the Attack stays put";
}

TEST(Colorless, SecretTechniqueOffersOnlySkills) {
  CombatState s = fight_holding(CardId::SecretTechnique);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Defend});
  s.draw_pile.push_back(Card{CardId::Armaments});  // another Skill
  s.draw_pile.push_back(Card{CardId::Strike});     // an Attack

  ASSERT_TRUE(apply_action(s, card_action(CardId::SecretTechnique)));

  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.kind, ChoiceKind::DrawPileSkillToHand);
  ASSERT_EQ(s.pending_choice.num_options, 2);
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    EXPECT_EQ(CARD_DATABASE.at(s.pending_choice.options[i].card_id).type,
              CardType::Skill);
  }
}

TEST(Colorless, SecretWeaponOffersOnlyAttacks) {
  CombatState s = fight_holding(CardId::SecretWeapon);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});
  s.draw_pile.push_back(Card{CardId::Bash});
  s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, card_action(CardId::SecretWeapon)));

  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.kind, ChoiceKind::DrawPileAttackToHand);
  ASSERT_EQ(s.pending_choice.num_options, 2);
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    EXPECT_EQ(CARD_DATABASE.at(s.pending_choice.options[i].card_id).type,
              CardType::Attack);
  }
}

TEST(Colorless, SecretTechniqueExhaustsAndThePlusDoesNot) {
  for (CardId id : {CardId::SecretTechnique, CardId::SecretTechniquePlus}) {
    CombatState s = fight_holding(id);
    s.draw_pile.clear();
    s.draw_pile.push_back(Card{CardId::Defend});
    ASSERT_TRUE(apply_action(s, card_action(id))) << card_name(id);

    const bool exhausted =
        !s.exhaust_pile.empty() && s.exhaust_pile.back().card_id == id;
    EXPECT_EQ(exhausted, id == CardId::SecretTechnique) << card_name(id);
  }
}

// A full hand sends the card to the discard instead, the same rule every other
// arrival follows.
TEST(Colorless, SecretWeaponOverflowsToTheDiscardWhenTheHandIsFull) {
  CombatState s = fight_holding(CardId::SecretWeapon);
  // A FULL hand once Secret Weapon itself has left it, which is what the
  // overflow needs: the played card is removed before its effect resolves, so
  // nine cards plus the card being played leaves room for the retrieved one.
  // StS reaches this state through effects that play a card without removing
  // it (its patch notes mention Burst); the engine's rule is generic.
  for (int i = 0; i < HAND_SIZE_LIMIT; ++i) {
    s.current_hand.push_back(Card{CardId::Defend});
  }
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Bash});
  s.discard_pile.clear();

  ASSERT_TRUE(apply_action(s, card_action(CardId::SecretWeapon)));

  EXPECT_TRUE(s.draw_pile.empty()) << "it still left the draw pile";
  bool in_discard = false;
  for (const Card& c : s.discard_pile) {
    if (c.card_id == CardId::Bash) in_discard = true;
  }
  EXPECT_TRUE(in_discard);
}

// ------------------------------------------------------------- Forethought

TEST(Colorless, ForethoughtPutsAHandCardOnTheBottomOfTheDrawPile) {
  CombatState s = fight_holding(CardId::Forethought);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});  // the bottom before the move
  s.current_hand.push_back(Card{CardId::Bash});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Forethought)));

  ASSERT_EQ(s.draw_pile.size(), 2u);
  EXPECT_EQ(s.draw_pile.front().card_id, CardId::Bash)
      << "front() is the BOTTOM — draw_one pops the back";
  EXPECT_EQ(s.draw_pile.front().cost_override, 0);
  EXPECT_EQ(s.draw_pile.front().cost_duration, CostDuration::UntilPlayed);
}

// "Until played" outlives the turn, unlike Transmutation's discount.
TEST(Colorless, ForethoughtsDiscountSurvivesTheTurn) {
  CombatState s = fight_holding(CardId::Forethought);
  s.draw_pile.clear();
  s.current_hand.push_back(Card{CardId::Bash});
  ASSERT_TRUE(apply_action(s, card_action(CardId::Forethought)));

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  bool found = false;
  for (const std::vector<Card>* pile :
       {&s.current_hand, &s.draw_pile, &s.discard_pile}) {
    for (const Card& c : *pile) {
      if (c.card_id == CardId::Bash && c.cost_override == 0) {
        found = true;
        EXPECT_EQ(c.cost_duration, CostDuration::UntilPlayed);
      }
    }
  }
  EXPECT_TRUE(found);
}

// ...and is SPENT by the play, rather than lasting the combat.
TEST(Colorless, ForethoughtsDiscountIsSpentByPlayingTheCard) {
  CombatState s = fight_holding(CardId::Forethought);
  s.draw_pile.clear();
  s.current_hand.push_back(Card{CardId::Bash});
  ASSERT_TRUE(apply_action(s, card_action(CardId::Forethought)));
  ASSERT_EQ(s.draw_pile.size(), 1u);

  // Draw it back by hand, then play it: Bash costs 2, and this play is free.
  s.current_hand.push_back(s.draw_pile.front());
  s.draw_pile.clear();
  s.character.energy = 1;  // not enough for Bash's printed cost

  ASSERT_TRUE(apply_action(s, card_action(CardId::Bash, 0)));

  EXPECT_EQ(s.character.energy, 1) << "the discounted play cost nothing";
  for (const Card& c : s.discard_pile) {
    if (c.card_id == CardId::Bash) {
      EXPECT_EQ(c.cost_override, kNoCostOverride)
          << "the discount is spent, not permanent";
    }
  }
}

// StS only marks a card whose PRINTED cost is above 0.
TEST(Colorless, ForethoughtDoesNotDiscountAFreeCard) {
  CombatState s = fight_holding(CardId::Forethought);
  s.draw_pile.clear();
  s.current_hand.push_back(Card{CardId::SwiftStrike});  // already costs 0

  ASSERT_TRUE(apply_action(s, card_action(CardId::Forethought)));

  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile.front().card_id, CardId::SwiftStrike);
  EXPECT_EQ(s.draw_pile.front().cost_override, kNoCostOverride);
}

// The + is "any number", a multi-select, which waits for batch 6.
TEST(Colorless, ForethoughtPlusIsStillMultiSelectAndUnplayable) {
  EXPECT_TRUE(CARD_DATABASE.at(CardId::ForethoughtPlus).unplayable);
  EXPECT_FALSE(CARD_DATABASE.at(CardId::Forethought).unplayable);
}

// ====================== batch 5: power triggers and the delayed effect

// The countdown is driven directly rather than by playing Panache, so the test
// pins the mechanism (every fifth card) without depending on whether Panache's
// own play counts toward the first cycle.
TEST(Colorless, PanacheFiresOnEveryFifthCardPlayed) {
  CombatState s = fight_holding(CardId::SwiftStrike);
  s.character.powers[Power::Panache] = 10;
  s.character.panache_counter = kPanacheCardsPerTrigger;
  s.current_hand.clear();
  for (int i = 0; i < 5; ++i) s.current_hand.push_back(Card{CardId::Defend});
  const int hp_before = s.enemies[0].hp;

  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  }
  EXPECT_EQ(s.enemies[0].hp, hp_before) << "four cards is not five";

  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  EXPECT_EQ(s.enemies[0].hp, hp_before - 10) << "the fifth card sets it off";
  EXPECT_EQ(s.character.panache_counter, kPanacheCardsPerTrigger)
      << "the countdown rolls back to 5";
}

TEST(Colorless, PanacheCountdownRestartsEachTurn) {
  CombatState s = fight_holding(CardId::SwiftStrike);
  s.character.powers[Power::Panache] = 10;
  s.character.panache_counter = kPanacheCardsPerTrigger;
  s.current_hand.clear();
  for (int i = 0; i < 4; ++i) s.current_hand.push_back(Card{CardId::Defend});
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  }

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));
  EXPECT_EQ(s.character.panache_counter, kPanacheCardsPerTrigger);

  const int hp_before = s.enemies[0].hp;
  s.current_hand.push_back(Card{CardId::Defend});
  s.character.energy = 99;
  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  EXPECT_EQ(s.enemies[0].hp, hp_before)
      << "four cards last turn plus one this turn must not trigger it";
}

// Stacks are the DAMAGE: a second Panache adds to it rather than starting a
// second countdown.
TEST(Colorless, PanacheStacksRaiseTheDamage) {
  CombatState s = fight_holding(CardId::Panache);
  s.current_hand.push_back(Card{CardId::PanachePlus});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Panache)));
  ASSERT_TRUE(apply_action(s, card_action(CardId::PanachePlus)));

  EXPECT_EQ(get_status(s.character.powers, Power::Panache), 24) << "10 + 14";
}

// ------------------------------------------------------- Sadistic Nature

TEST(Colorless, SadisticNatureDamagesAnEnemyWhenADebuffLands) {
  CombatState s = fight_holding(CardId::Blind);  // applies 2 Weak
  s.character.powers[Power::SadisticNature] = 5;
  const int hp_before = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, card_action(CardId::Blind, 0)));

  EXPECT_GT(get_status(s.enemies[0].debuffs, Debuff::Weak), 0);
  EXPECT_EQ(s.enemies[0].hp, hp_before - 5);
}

// "The damage will not trigger if the enemy prevents the debuff through
// Artifact" — the reason apply_debuff reports whether it landed.
TEST(Colorless, SadisticNatureIsSilentWhenArtifactEatsTheDebuff) {
  CombatState s = fight_holding(CardId::Blind);
  s.character.powers[Power::SadisticNature] = 5;
  s.enemies[0].powers[Power::Artifact] = 1;
  const int hp_before = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, card_action(CardId::Blind, 0)));

  EXPECT_EQ(get_status(s.enemies[0].debuffs, Debuff::Weak), 0);
  EXPECT_EQ(s.enemies[0].hp, hp_before) << "a negated debuff deals nothing";
}

// StS patched this specifically: Dark Shackles triggers Sadistic Nature ONCE.
// Its Strength loss counts; the Shackled give-back is excluded by name.
TEST(Colorless, SadisticNatureFiresOnceForDarkShackles) {
  CombatState s = fight_holding(CardId::DarkShackles);
  s.character.powers[Power::SadisticNature] = 5;
  const int hp_before = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, card_action(CardId::DarkShackles, 0)));

  EXPECT_EQ(s.enemies[0].hp, hp_before - 5) << "once, not twice";
  EXPECT_EQ(get_status(s.enemies[0].powers, Power::Shackled), 9);
}

// ---------------------------------------------------------------- Mayhem

TEST(Colorless, MayhemPlaysTheTopCardOfTheDrawPileEachTurn) {
  CombatState s = fight_holding(CardId::Mayhem);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Mayhem)));
  ASSERT_EQ(get_status(s.character.powers, Power::Mayhem), 1);
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});  // back() is the top
  const int hp_before = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  EXPECT_LT(s.enemies[0].hp, hp_before) << "the top card was played for free";
}

// The one difference from Havoc: Mayhem does NOT exhaust the card it plays.
TEST(Colorless, MayhemDoesNotExhaustTheCardItPlays) {
  CombatState s = fight_holding(CardId::Mayhem);
  ASSERT_TRUE(apply_action(s, card_action(CardId::Mayhem)));
  s.draw_pile.clear();
  s.draw_pile.push_back(Card{CardId::Strike});
  s.exhaust_pile.clear();

  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  for (const Card& c : s.exhaust_pile) {
    EXPECT_NE(c.card_id, CardId::Strike) << "Havoc exhausts; Mayhem does not";
  }
}

// -------------------------------------------------------------- The Bomb

TEST(Colorless, TheBombFiresAtTheEndOfTheThirdTurn) {
  CombatState s = fight_holding(CardId::TheBomb);
  s.enemies[0].hp = 200;  // survive the blast, so the delta stays readable
  ASSERT_TRUE(apply_action(s, card_action(CardId::TheBomb)));
  ASSERT_EQ(s.character.bombs[kBombFuseTurns - 1], 1) << "the fuse is lit";

  // Two turn ends walk the fuse down without firing.
  for (int turn = 0; turn < 2; ++turn) {
    const int before = s.enemies[0].hp;
    ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));
    ASSERT_EQ(s.outcome, Outcome::InProgress);
    EXPECT_EQ(s.enemies[0].hp, before) << "it must not go off early";
  }

  // The blast lands at the END of the player's turn, before the enemies act,
  // so block an enemy gained on its previous turn is still standing and would
  // absorb part of it. That is correct — the Bomb is unmodifiable, not
  // unblockable — and this test is measuring the blast, so clear it first.
  s.enemies[0].current_block = 0;
  const int before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));
  EXPECT_EQ(s.enemies[0].hp, before - 40) << "the third end of turn";
  EXPECT_EQ(s.character.bombs[0], 0) << "and the slot is spent";
}

TEST(Colorless, TheBombPlusDealsFifty) {
  CombatState s = fight_holding(CardId::TheBombPlus);
  s.enemies[0].hp = 200;
  ASSERT_TRUE(apply_action(s, card_action(CardId::TheBombPlus)));
  for (int turn = 0; turn < 2; ++turn) {
    ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));
  }

  s.enemies[0].current_block = 0;
  const int before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  EXPECT_EQ(s.enemies[0].hp, before - 50);
}

// Two Bombs played on the same turn share a fuse length, which is what lets
// three slots hold any number of them — and each still fires as its own hit.
TEST(Colorless, TwoBombsInOneTurnBothFire) {
  CombatState s = fight_holding(CardId::TheBomb);
  s.current_hand.push_back(Card{CardId::TheBombPlus});
  ASSERT_TRUE(apply_action(s, card_action(CardId::TheBomb)));
  ASSERT_TRUE(apply_action(s, card_action(CardId::TheBombPlus)));
  EXPECT_EQ(s.character.bombs[kBombFuseTurns - 1], 1);
  EXPECT_EQ(s.character.bombs_upgraded[kBombFuseTurns - 1], 1);

  s.enemies[0].hp = 200;
  for (int turn = 0; turn < 2; ++turn) {
    ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));
  }

  s.enemies[0].current_block = 0;
  const int before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, encode_action(ActionBlock::EndTurn)));

  EXPECT_EQ(s.enemies[0].hp, before - 90) << "40 and 50, as two separate hits";
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
TEST(Colorless, ExactlyThirtyFourOfThirtyFiveArePlayable) {
  const CardId playable[] = {
      CardId::BandageUp,     CardId::Blind,        CardId::DarkShackles,
      CardId::DeepBreath,    CardId::DramaticEntrance, CardId::Finesse,
      CardId::FlashOfSteel,  CardId::GoodInstincts, CardId::Impatience,
      CardId::MindBlast,     CardId::Panacea,      CardId::PanicButton,
      CardId::SwiftStrike,   CardId::Trip,         CardId::MasterOfStrategy,
      // Batch 1 (colorless-effects.md §5).
      CardId::Apotheosis,    CardId::HandOfGreed,  CardId::Violence,
      CardId::ThinkingAhead,
      // Batch 2: random in-combat generation.
      CardId::JackOfAllTrades, CardId::Transmutation, CardId::Magnetism,
      CardId::Discovery,
      // Batch 3: the "this combat" cost duration.
      CardId::Madness, CardId::Enlightenment, CardId::Chrysalis,
      CardId::Metamorphosis,
      // Batch 4: choices over the draw pile.
      CardId::SecretTechnique, CardId::SecretWeapon, CardId::Forethought,
      // Batch 5: power triggers and the delayed effect.
      CardId::Panache, CardId::SadisticNature, CardId::Mayhem,
      CardId::TheBomb};

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

// ===================================================================== curses

// Ten in the random pool, matching sts_lightspeed's curseCardPool. Curse of the
// Bell is excluded: it comes only from the Calling Bell relic, never a roll.
TEST(Curses, ThePoolIsTenAndExcludesCurseOfTheBell) {
  EXPECT_EQ(CURSE_POOL.size(), 10u);
  EXPECT_EQ(std::find(CURSE_POOL.begin(), CURSE_POOL.end(),
                      CardId::CurseOfTheBell),
            CURSE_POOL.end())
      << "Curse of the Bell is not a rollable curse";
}

// Every curse is Unplayable BY RULE — not as a "not implemented" marker. A
// curse that became playable would be a parity bug, not progress.
TEST(Curses, EveryCurseIsUnplayableAndTyped) {
  const CardId all[] = {CardId::Clumsy,    CardId::Decay,    CardId::Doubt,
                        CardId::Injury,    CardId::Normality, CardId::Pain,
                        CardId::Parasite,  CardId::Regret,   CardId::Shame,
                        CardId::Writhe,    CardId::CurseOfTheBell};
  EXPECT_EQ(std::size(all), 11u);

  for (CardId id : all) {
    const CardData& d = CARD_DATABASE.at(id);
    EXPECT_EQ(d.type, CardType::Curse) << card_name(id);
    EXPECT_TRUE(d.unplayable) << card_name(id) << " is playable";
    EXPECT_FALSE(card_description(id).empty()) << card_name(id);
  }
}

// Curses cannot be upgraded — which is why the block is 11 ids, not 22, and
// why CARDS lands on 270.
TEST(Curses, CursesAreNotUpgradable) {
  for (CardId id : CURSE_POOL) {
    EXPECT_FALSE(is_upgradable(id)) << card_name(id) << " gained an upgrade";
  }
  EXPECT_FALSE(is_upgradable(CardId::CurseOfTheBell));
}

// A curse in hand must never be offered to the agent.
TEST(Curses, ACurseInHandIsMaskedOut) {
  CombatState s = fight_holding(CardId::Injury);
  const std::vector<bool> mask = valid_actions(s);
  EXPECT_FALSE(mask[static_cast<size_t>(card_action(CardId::Injury, 0))])
      << "a curse was offered as a playable action";
}

// Decay shares Burn's field and therefore Burn's behaviour: damage at end of
// turn, and it leaves the hand as it fires.
TEST(Curses, DecayDealsTwoAtEndOfTurnAndLeavesTheHand) {
  CombatState s = fight_holding(CardId::Decay);
  s.character.current_block = 0;
  const int hp = s.character.hp;

  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(Action{ActionKind::DiscardHand});
  drain(s, q, ctx);

  EXPECT_EQ(s.character.hp, hp - 2) << "Decay dealt no end-of-turn damage";
  EXPECT_TRUE(s.current_hand.empty());
}

// Clumsy exhausts rather than discarding — Ethereal, so it is gone for the
// fight rather than cycling back.
TEST(Curses, ClumsyExhaustsAtEndOfTurn) {
  CombatState s = fight_holding(CardId::Clumsy);
  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(Action{ActionKind::DiscardHand});
  drain(s, q, ctx);

  EXPECT_EQ(s.exhaust_pile.size(), 1u) << "Clumsy did not exhaust";
  EXPECT_TRUE(s.discard_pile.empty());
}

// Writhe is Innate: it costs a card slot on turn 1 of every fight, which IS its
// effect.
TEST(Curses, WritheStartsInTheOpeningHand) {
  EXPECT_TRUE(CARD_DATABASE.at(CardId::Writhe).innate);

  CombatSetup setup;
  setup.seed = 1;
  setup.deck = starter_deck();
  setup.deck.push_back(Card{CardId::Writhe});
  const CombatState s = start_combat(std::move(setup));

  bool in_hand = false;
  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::Writhe) in_hand = true;
  }
  EXPECT_TRUE(in_hand);
}

// The data for the curses whose effects are not yet wired is still correct and
// distinguishable — these are the fields a later pass will read.
TEST(Curses, TheUnwiredCursesCarryTheirData) {
  EXPECT_EQ(CARD_DATABASE.at(CardId::Doubt).end_of_turn_self_debuff,
            Debuff::Weak);
  EXPECT_EQ(CARD_DATABASE.at(CardId::Shame).end_of_turn_self_debuff,
            Debuff::Frail);
  EXPECT_TRUE(CARD_DATABASE.at(CardId::Regret)
                  .end_of_turn_hp_loss_per_card_in_hand);
  EXPECT_EQ(CARD_DATABASE.at(CardId::Pain).hp_loss_in_hand_per_card_played, 1);
  EXPECT_EQ(CARD_DATABASE.at(CardId::Normality).cards_playable_cap_in_hand, 3);
  EXPECT_EQ(CARD_DATABASE.at(CardId::Parasite).max_hp_loss_on_removal, 3);
  EXPECT_TRUE(CARD_DATABASE.at(CardId::CurseOfTheBell).cannot_be_removed);
}

// CARDS = 270: 189 Ironclad + 70 colorless + 11 curses. The figure §5.1 commits
// to, and the width the v2 action space is sized against.
TEST(Curses, TheCardVocabularyIsNowTwoHundredAndSeventy) {
  EXPECT_EQ(kNumCardTypes, 270);
  EXPECT_EQ(CARD_DATABASE.size(), 270u);
}

}  // namespace minispire
