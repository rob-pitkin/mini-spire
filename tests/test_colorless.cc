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
TEST(Colorless, TheNotYetPlayableUncommonsAreExactlyTheseSix) {
  const CardId expected[] = {CardId::Discovery,       CardId::Enlightenment,
                             CardId::Forethought,     CardId::JackOfAllTrades,
                             CardId::Madness,         CardId::Purity};

  for (CardId id : expected) {
    EXPECT_TRUE(CARD_DATABASE.at(id).unplayable)
        << card_name(id) << " became playable — update this list";
  }
  for (CardId id : {CardId::BandageUp, CardId::Blind, CardId::DarkShackles,
                    CardId::DeepBreath, CardId::DramaticEntrance,
                    CardId::Finesse, CardId::FlashOfSteel,
                    CardId::GoodInstincts, CardId::Impatience,
                    CardId::MindBlast, CardId::Panacea, CardId::PanicButton,
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
TEST(Colorless, ExactlyNineteenOfThirtyFiveArePlayable) {
  const CardId playable[] = {
      CardId::BandageUp,     CardId::Blind,        CardId::DarkShackles,
      CardId::DeepBreath,    CardId::DramaticEntrance, CardId::Finesse,
      CardId::FlashOfSteel,  CardId::GoodInstincts, CardId::Impatience,
      CardId::MindBlast,     CardId::Panacea,      CardId::PanicButton,
      CardId::SwiftStrike,   CardId::Trip,         CardId::MasterOfStrategy,
      // Batch 1 (colorless-effects.md §5).
      CardId::Apotheosis,    CardId::HandOfGreed,  CardId::Violence,
      CardId::ThinkingAhead};

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
