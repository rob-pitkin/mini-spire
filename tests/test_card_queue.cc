// The card queue (docs/design/card-queue.md): cards Havoc, Mayhem and
// Distilled Chaos take, and Double Tap / Duplication replays, wait in
// CombatState::card_queue until every pending action has run, as in StS's
// GameActionManager.
//
// Every fight is built: make_minimal_state gives one Jaw Worm, the player at
// 80/80 with 3 energy and empty piles. Where a target is random, there is only
// one living enemy to roll, or the expected outcome holds for every roll.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "action.h"
#include "enemy.h"
#include "query.h"
#include "test_helpers.h"
#include "turn_loop.h"

namespace minispire {
namespace {

using testing::card_action;
using testing::make_minimal_state;

std::vector<Card> cards(std::initializer_list<CardId> ids) {
  std::vector<Card> out;
  for (CardId id : ids) out.push_back(Card{id});
  return out;
}

int count_of(const std::vector<Card>& pile, CardId id) {
  return static_cast<int>(std::count_if(
      pile.begin(), pile.end(), [id](const Card& c) { return c.card_id == id; }));
}

int drink(PotionId p) {
  return encode_action(ActionBlock::PotionUse, static_cast<int>(p), 0);
}

// One Jaw Worm at 100 HP, set to Chomp: an attack with no block, so fixed
// damage at the next turn start is not soaked.
CombatState one_enemy_fight() {
  CombatState s = make_minimal_state(0);
  s.enemies[0].hp = 100;
  s.enemies[0].last_move = MoveName::Chomp;
  s.enemies[0].consecutive_count = 1;
  return s;
}

// Survive an enemy phase without the damage mattering.
void end_turn_safely(CombatState& s) {
  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
}

// ---------------------------------------------------------------- Mayhem

TEST(CardQueue, MayhemTakesItsCardAfterThePostDrawDraws) {
  // The turn draws 5 and Pocketwatch draws 3 more (no card was played), all
  // Defends; only then does Mayhem take the top card — the Strike. With the
  // post-draw hook as a later action, its draw came after Mayhem's take.
  CombatState s = one_enemy_fight();
  s.relics.push_back(HeldRelic{RelicId::Pocketwatch, 0});
  s.character.powers[Power::Mayhem] = 1;
  s.draw_pile.push_back(Card{CardId::Strike});  // back() is the top
  for (int i = 0; i < STARTING_HAND_SIZE + kPocketwatchDraw; ++i) {
    s.draw_pile.push_back(Card{CardId::Defend});
  }

  end_turn_safely(s);

  EXPECT_EQ(s.enemies[0].hp, 100 - 6) << "Mayhem played the Strike";
  EXPECT_EQ(count_of(s.current_hand, CardId::Defend),
            STARTING_HAND_SIZE + kPocketwatchDraw);
  EXPECT_EQ(count_of(s.current_hand, CardId::Strike), 0);
}

TEST(CardQueue, MayhemRollsItsTargetAfterTheTurnStartRelics) {
  // Q2: relics' atTurnStart before powers' atStartOfTurn. Mercury Hourglass's
  // 3 damage kills the 3-HP enemy before Mayhem rolls, so the roll has one
  // living enemy to pick — for every seed. Powers first, Mayhem rolled while
  // both were alive and half the time picked one that was dead at play time.
  for (uint32_t seed = 0; seed < 16; ++seed) {
    CombatState s = one_enemy_fight();
    s.enemies.push_back(make_jaw_worm(s.rng));
    s.enemies[1].hp = 100;
    s.enemies[1].last_move = MoveName::Chomp;
    s.enemies[1].consecutive_count = 1;
    s.enemies[0].hp = 3;
    s.card_rng = std::mt19937(seed);
    s.relics.push_back(HeldRelic{RelicId::MercuryHourglass, 0});
    s.character.powers[Power::Mayhem] = 1;
    s.draw_pile.push_back(Card{CardId::Strike});
    for (int i = 0; i < STARTING_HAND_SIZE; ++i) {
      s.draw_pile.push_back(Card{CardId::Defend});
    }
    // Mercury Hourglass fires every turn start; the counter is irrelevant.

    end_turn_safely(s);

    EXPECT_LE(s.enemies[0].hp, 0) << "seed " << seed;
    EXPECT_EQ(s.enemies[1].hp, 100 - 3 - 6) << "seed " << seed;
  }
}

// -------------------------------------------------------- Distilled Chaos

TEST(CardQueue, DistilledChaosTakesEveryCardBeforePlayingAny) {
  // §2.2. The three takes run back to back, so the Pommel Strike played first
  // draws the card BELOW the third — the Defend — not a Strike meant for the
  // potion.
  CombatState s = one_enemy_fight();
  s.potions = {PotionId::DistilledChaos};
  s.draw_pile = cards({CardId::Defend, CardId::Strike, CardId::Strike,
                       CardId::PommelStrike});  // Pommel Strike on top

  ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 9 - 6 - 6);
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Defend);
  EXPECT_TRUE(s.draw_pile.empty());
  EXPECT_TRUE(s.card_queue.empty());
}

TEST(CardQueue, AFightThatEndsWithCardsWaitingClearsThem) {
  CombatState s = one_enemy_fight();
  s.enemies[0].hp = 6;
  s.potions = {PotionId::DistilledChaos};
  s.draw_pile = cards({CardId::Strike, CardId::Strike, CardId::Strike});

  ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));

  EXPECT_EQ(s.outcome, Outcome::Won);
  EXPECT_TRUE(s.card_queue.empty());
}

// ---------------------------------------------------------------- Replays

TEST(CardQueue, HavocsCopyTakesItsCardBeforeTheFirstIsPlayed) {
  // §2.3. Duplication's copy of Havoc goes to the FRONT of the queue and the
  // Pommel Strike Havoc took to the back, so the copy runs first and takes
  // the Bash. Then the Pommel Strike plays and draws the Defend under them.
  // With the copy at the back, the Pommel Strike would draw the Bash and the
  // copy would take the Defend.
  CombatState s = one_enemy_fight();
  s.character.powers[Power::Duplication] = 1;
  s.current_hand = cards({CardId::Havoc});
  s.draw_pile = cards({CardId::Defend, CardId::Bash, CardId::PommelStrike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Havoc)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 9 - 8);
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Defend);
  EXPECT_EQ(s.character.current_block, 0);
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::PommelStrike), 1);
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::Bash), 1);
}

TEST(CardQueue, AReplayCutsInFrontOfCardsAlreadyWaiting) {
  // addCardQueueItem(copy, true): the copy goes right behind the card being
  // played, ahead of cards already waiting. Distilled Chaos queues Strike,
  // Bash, Defend; the Strike's Duplication copy plays BEFORE the Bash, so it
  // lands before Vulnerable: 6 + 6 + 8. Behind the Bash it would be 6 + 8 + 9.
  CombatState s = one_enemy_fight();
  s.potions = {PotionId::DistilledChaos};
  s.character.powers[Power::Duplication] = 1;
  s.draw_pile = cards({CardId::Defend, CardId::Bash, CardId::Strike});

  ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 6 - 6 - 8);
}

TEST(CardQueue, AReplayWaitsForTheOriginalsActions) {
  // §2.4. Battle Trance queues its draw and then No Draw as actions; the
  // Duplication copy plays after both, so its draw is blocked: 3 cards, not 6.
  CombatState s = one_enemy_fight();
  s.character.powers[Power::Duplication] = 1;
  s.current_hand = cards({CardId::BattleTrance});
  s.draw_pile.assign(10, Card{CardId::Strike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::BattleTrance)));

  EXPECT_EQ(s.current_hand.size(), 3u);
}

// ------------------------------------------- Rampage's shared growth (#44)

// The damage of every Rampage in the discard pile, ascending.
std::vector<int> rampage_damages(const CombatState& s) {
  std::vector<int> out;
  for (const Card& c : s.discard_pile) {
    if (CARD_DATABASE.at(c.card_id).bonus_damage_per_play > 0) {
      out.push_back(instance_card_damage(s, c));
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

TEST(CardQueue, ADoubleTappedRampagePlaysGrownAndGrowsTheOriginalAgain) {
  // The copy shares the original's uuid, so the original's +5 reaches it in
  // limbo (it plays at 13), and its own +5 reaches the original in the
  // discard (which ends at 18).
  CombatState s = one_enemy_fight();
  s.character.powers[Power::DoubleTap] = 1;
  s.current_hand = cards({CardId::Rampage});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Rampage)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 8 - 13);
  EXPECT_EQ(rampage_damages(s), std::vector<int>{18});
}

TEST(CardQueue, TwoReplaysOfOneRampageGrowEachOther) {
  // Double Tap and Duplication: the second copy has taken the original's and
  // the first copy's growth by the time it plays — 8, 13, 18 — and the
  // original ends at 23.
  CombatState s = one_enemy_fight();
  s.character.powers[Power::DoubleTap] = 1;
  s.character.powers[Power::Duplication] = 1;
  s.current_hand = cards({CardId::Rampage});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Rampage)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 8 - 13 - 18);
  EXPECT_EQ(rampage_damages(s), std::vector<int>{23});
}

TEST(CardQueue, AnIdenticalRampageAlreadyDiscardedIsInterchangeable) {
  // A different Rampage, already at 13, sits in the discard. The replay's
  // growth may land on either 13 — they are identical — and the pile ends
  // {13, 18} either way.
  CombatState s = one_enemy_fight();
  s.character.powers[Power::DoubleTap] = 1;
  s.discard_pile = cards({CardId::Rampage5});
  s.current_hand = cards({CardId::Rampage});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Rampage)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 8 - 13);
  EXPECT_EQ(rampage_damages(s), (std::vector<int>{13, 18}));
}

// ------------------------------------------- X-cost cards taken (#42, §3.2)
//
// Whirlwind hits every enemy, so the target Havoc, Mayhem or Distilled Chaos
// rolls never changes the damage. 5 per hit against an unblocked Jaw Worm.

TEST(CardQueue, HavocPlaysAnXCostCardForTheEnergyLeftWithoutSpendingIt) {
  // Havoc costs 1 of the 3, and the Whirlwind it takes reads the 2 left.
  CombatState s = one_enemy_fight();
  s.current_hand = cards({CardId::Havoc});
  s.draw_pile = cards({CardId::Whirlwind});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Havoc)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 5 * 2);
  EXPECT_EQ(s.character.energy, 2) << "the Whirlwind spent nothing";
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::Whirlwind), 1);
}

TEST(CardQueue, AnAutoplayedXIsReadWhenTheCardIsTaken) {
  // Distilled Chaos takes all three cards at 3 energy. Bloodletting plays
  // first and raises energy to 5, but the Whirlwind's X was read when it was
  // taken: 3 hits, not 5.
  CombatState s = one_enemy_fight();
  s.potions = {PotionId::DistilledChaos};
  s.draw_pile = cards({CardId::Defend, CardId::Whirlwind,
                       CardId::Bloodletting});  // Bloodletting on top

  ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 5 * 3);
  EXPECT_EQ(s.character.energy, 3 + 2) << "only Bloodletting changed it";
  EXPECT_TRUE(s.card_queue.empty());
}

TEST(CardQueue, ADoubleTappedTakenXCostCardReplaysWithTheSameX) {
  // Havoc is a Skill, so Double Tap answers the Whirlwind, and its replay
  // reuses the taken card's X: 2 hits twice.
  CombatState s = one_enemy_fight();
  s.character.powers[Power::DoubleTap] = 1;
  s.current_hand = cards({CardId::Havoc});
  s.draw_pile = cards({CardId::Whirlwind});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Havoc)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 5 * 2 * 2);
  EXPECT_EQ(s.character.energy, 2);
}

TEST(CardQueue, ChemicalXAddsTwoToATakenXCostCard) {
  CombatState s = one_enemy_fight();
  s.relics.push_back(HeldRelic{RelicId::ChemicalX, 0});
  s.current_hand = cards({CardId::Havoc});
  s.draw_pile = cards({CardId::Whirlwind});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Havoc)));

  EXPECT_EQ(s.enemies[0].hp, 100 - 5 * (2 + kChemicalXBonus));
  EXPECT_EQ(s.character.energy, 2);
}

TEST(CardQueue, MayhemPlaysAnXCostCardForTheTurnsEnergy) {
  // The turn ends with 0 energy left. Mayhem takes the Whirlwind after the
  // turn's draw, by when energy has been refilled to 3.
  CombatState s = one_enemy_fight();
  s.character.powers[Power::Mayhem] = 1;
  s.draw_pile.push_back(Card{CardId::Whirlwind});  // under the turn's draw
  for (int i = 0; i < STARTING_HAND_SIZE; ++i) {
    s.draw_pile.push_back(Card{CardId::Defend});
  }
  s.character.energy = 0;

  end_turn_safely(s);

  EXPECT_EQ(s.enemies[0].hp, 100 - 5 * 3);
  EXPECT_EQ(s.character.energy, 3);
}

// ---------------------------------------------------------------- Choices

TEST(CardQueue, AQueuedCardsChoicePausesTheRestUntilAnswered) {
  // Distilled Chaos takes Armaments and two Strikes. Armaments opens its
  // upgrade choice; the Strikes wait in the queue — in no pile — and play once
  // it is answered. A clone taken during the pause resumes identically.
  CombatState s = one_enemy_fight();
  s.potions = {PotionId::DistilledChaos};
  s.current_hand = cards({CardId::Bash, CardId::Defend});
  s.draw_pile = cards({CardId::Strike, CardId::Strike, CardId::Armaments});

  ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));
  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.card_queue.size(), 2);
  EXPECT_TRUE(s.draw_pile.empty()) << "all three were taken first";
  EXPECT_EQ(s.enemies[0].hp, 100) << "the Strikes have not played";

  CombatState clone = s;
  for (CombatState* st : {&s, &clone}) {
    int bash = -1;
    for (int i = 0; i < st->pending_choice.num_options; ++i) {
      if (st->pending_choice.options[i].card_id == CardId::Bash) bash = i;
    }
    ASSERT_GE(bash, 0);
    const Card& o = st->pending_choice.options[bash];
    ASSERT_TRUE(apply_action(*st, encode_action(ActionBlock::CardSelect,
                                                static_cast<int>(o.card_id),
                                                0, cost_column(*st, o))));
    EXPECT_FALSE(st->pending_choice.active());
    EXPECT_TRUE(st->card_queue.empty());
    EXPECT_EQ(st->enemies[0].hp, 100 - 12) << "both Strikes played";
    EXPECT_EQ(count_of(st->current_hand, CardId::BashPlus), 1);
  }
  EXPECT_EQ(clone.enemies[0].hp, s.enemies[0].hp);
  EXPECT_EQ(clone.character.current_block, s.character.current_block);
}

}  // namespace
}  // namespace minispire
