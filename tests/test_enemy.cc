#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

#include "enemy.h"
#include "status_effect.h"

using namespace minispire;

TEST(Enemy, JawWormHpInRange) {
  // Sample a handful of seeds; all should yield HP in [40, 44].
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_jaw_worm(rng);
    EXPECT_GE(e.hp, 40);
    EXPECT_LE(e.hp, 44);
    EXPECT_EQ(e.hp, e.max_hp);
  }
}

TEST(Enemy, JawWormMovesCatalog) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  EXPECT_EQ(e.moves.size(), 3u);
  EXPECT_NE(e.moves.find(MoveName::Chomp), e.moves.end());
  EXPECT_NE(e.moves.find(MoveName::Thrash), e.moves.end());
  EXPECT_NE(e.moves.find(MoveName::Bellow), e.moves.end());
}

TEST(Enemy, BellowAppliesStrengthToSelf) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  const Move& bellow = e.moves.at(MoveName::Bellow);
  ASSERT_EQ(bellow.applies.size(), 1u);
  EXPECT_EQ(bellow.applies[0].effect, StatusEffect::Strength);
  EXPECT_EQ(bellow.applies[0].amount, 3);
  // From the player's perspective, the buff lands on Target::Enemy.
  EXPECT_EQ(bellow.applies[0].target, StatusApplication::Target::Enemy);
}

TEST(Enemy, FirstTurnMoveIsChomp) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  ASSERT_TRUE(e.first_turn_move.has_value());
  EXPECT_EQ(*e.first_turn_move, MoveName::Chomp);
}

TEST(Enemy, TransitionTableHasFourKeys) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  EXPECT_EQ(e.transitions.size(), 4u);
  EXPECT_NE(e.transitions.find({MoveName::Chomp, 1}), e.transitions.end());
  EXPECT_NE(e.transitions.find({MoveName::Bellow, 1}), e.transitions.end());
  EXPECT_NE(e.transitions.find({MoveName::Thrash, 1}), e.transitions.end());
  EXPECT_NE(e.transitions.find({MoveName::Thrash, 2}), e.transitions.end());
}

TEST(Enemy, TransitionRowsSumToOne) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  constexpr float kEpsilon = 1e-4f;
  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    EXPECT_NEAR(sum, 1.0f, kEpsilon);
  }
}

TEST(Enemy, FirstSelectReturnsFirstTurnMove) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  ASSERT_FALSE(e.last_move.has_value());
  MoveName first = select_next_move(e, rng);
  EXPECT_EQ(first, MoveName::Chomp);
  ASSERT_TRUE(e.last_move.has_value());
  EXPECT_EQ(*e.last_move, MoveName::Chomp);
  EXPECT_EQ(e.consecutive_count, 1);
}

TEST(Enemy, NoChompTwiceInARow) {
  // The (Chomp, 1) row should not list Chomp as a next move — Chomp never
  // repeats.
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  const auto& dist = e.transitions.at({MoveName::Chomp, 1});
  for (const auto& t : dist) {
    EXPECT_NE(t.next_move, MoveName::Chomp);
  }
}

TEST(Enemy, NoBellowTwiceInARow) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  // The (Bellow, 1) row should not list Bellow as a next move — Bellow
  // never repeats.
  const auto& dist = e.transitions.at({MoveName::Bellow, 1});
  for (const auto& t : dist) {
    EXPECT_NE(t.next_move, MoveName::Bellow);
  }
}

TEST(Enemy, NoThrashAfterTwoInARow) {
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  // (Thrash, 2) row should not list Thrash as a next move.
  const auto& dist = e.transitions.at({MoveName::Thrash, 2});
  for (const auto& t : dist) {
    EXPECT_NE(t.next_move, MoveName::Thrash);
  }
}

TEST(Enemy, Determinism) {
  // Two enemies with same seed produce identical move sequences.
  std::mt19937 rng_a(42);
  std::mt19937 rng_b(42);
  Enemy a = make_jaw_worm(rng_a);
  Enemy b = make_jaw_worm(rng_b);
  ASSERT_EQ(a.hp, b.hp);
  std::vector<MoveName> seq_a, seq_b;
  for (int i = 0; i < 50; ++i) {
    seq_a.push_back(select_next_move(a, rng_a));
    seq_b.push_back(select_next_move(b, rng_b));
  }
  EXPECT_EQ(seq_a, seq_b);
}

TEST(Enemy, ConsecutiveCountIncrements) {
  // When the same move is sampled twice (only Thrash can do this for Jaw Worm
  // since Chomp and Bellow are banned after themselves), consecutive_count
  // should increment.
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
  e.last_move = MoveName::Thrash;
  e.consecutive_count = 1;

  // Try many seeds until we get a Thrash → Thrash transition.
  bool saw_repeat = false;
  for (uint32_t seed = 0; seed < 200 && !saw_repeat; ++seed) {
    std::mt19937 trng(seed);
    Enemy te = make_jaw_worm(trng);
    te.last_move = MoveName::Thrash;
    te.consecutive_count = 1;
    MoveName next = select_next_move(te, trng);
    if (next == MoveName::Thrash) {
      EXPECT_EQ(te.consecutive_count, 2);
      saw_repeat = true;
    } else {
      EXPECT_EQ(te.consecutive_count, 1);
      EXPECT_EQ(*te.last_move, next);
    }
  }
  EXPECT_TRUE(saw_repeat) << "Never saw Thrash -> Thrash in 200 seeds";
}
