#include <gtest/gtest.h>

#include <random>

#include "card.h"
#include "combat_state.h"
#include "test_helpers.h"

using namespace minispire;
using minispire::testing::make_minimal_state;

TEST(CombatState, CloneIsIndependentForHand) {
  CombatState original = make_minimal_state(0);
  original.current_hand.push_back(Card{CardId::Strike});

  CombatState copy = original.clone();
  copy.current_hand.push_back(Card{CardId::Bash});

  EXPECT_EQ(original.current_hand.size(), 1u);
  EXPECT_EQ(copy.current_hand.size(), 2u);
}

TEST(CombatState, CloneIsIndependentForEnemies) {
  CombatState original = make_minimal_state(0);
  int original_hp = original.enemies[0].hp;

  CombatState copy = original.clone();
  copy.enemies[0].hp = 1;

  EXPECT_EQ(original.enemies[0].hp, original_hp);
  EXPECT_EQ(copy.enemies[0].hp, 1);
}

TEST(CombatState, CloneIsIndependentForRng) {
  CombatState original = make_minimal_state(42);

  CombatState copy = original.clone();
  // Burn through the copy's RNG.
  for (int i = 0; i < 100; ++i) (void)copy.rng();

  // Original's RNG must be unaffected — first draw should match a fresh
  // mt19937 with seed 42 advanced past one number (the HP roll in
  // make_minimal_state via make_jaw_worm).
  std::mt19937 reference(42);
  std::uniform_int_distribution<int> burn(40, 44);
  burn(reference);  // mimic the HP roll
  EXPECT_EQ(original.rng(), reference());
}

TEST(CombatState, CloneRngStatePreserved) {
  CombatState original = make_minimal_state(0);
  CombatState copy = original.clone();
  // Same sequence of draws from RNGs in identical state.
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(original.rng(), copy.rng());
  }
}

TEST(CombatState, ClonePreservesAllFields) {
  CombatState original = make_minimal_state(7);
  original.turn_number = 9;
  original.character_turn = false;
  original.outcome = Outcome::Won;
  original.character.energy = 1;
  original.character.current_block = 5;

  CombatState copy = original.clone();

  EXPECT_EQ(copy.seed, original.seed);
  EXPECT_EQ(copy.turn_number, original.turn_number);
  EXPECT_EQ(copy.character_turn, original.character_turn);
  EXPECT_EQ(copy.outcome, original.outcome);
  EXPECT_EQ(copy.character.energy, original.character.energy);
  EXPECT_EQ(copy.character.current_block, original.character.current_block);
  EXPECT_EQ(copy.character.hp, original.character.hp);
  EXPECT_EQ(copy.character.max_hp, original.character.max_hp);
}
