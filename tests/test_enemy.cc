#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

#include "enemy.h"
#include "status_effect.h"

using namespace minispire;

// Find the first triggered effect matching a trigger (ROB-65). Returns nullptr
// if the enemy has none — lets config tests assert on the generalized mechanism.
static const TriggeredEffect* find_trigger(const Enemy& e, Trigger t) {
  for (const TriggeredEffect& fx : e.triggered_effects) {
    if (fx.trigger == t) return &fx;
  }
  return nullptr;
}

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
  ASSERT_EQ(bellow.applies_powers.size(), 1u);
  EXPECT_EQ(bellow.applies_powers[0].effect, Power::Strength);
  EXPECT_EQ(bellow.applies_powers[0].amount, 3);
  // From the player's perspective, the buff lands on Target::Enemy.
  EXPECT_EQ(bellow.applies_powers[0].target, Target::Enemy);
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

TEST(Enemy, FactoryPrimesFirstTurnIntent) {
  // make_jaw_worm leaves the enemy in a ready-to-fight state: last_move
  // is set to first_turn_move (Chomp) and consecutive_count is 1.
  std::mt19937 rng(0);
  Enemy e = make_jaw_worm(rng);
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

// ============================================================================
// Cultist (ROB-63)
// ============================================================================

TEST(Enemy, CultistHpInRange) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_cultist(rng);
    EXPECT_GE(e.hp, 48);
    EXPECT_LE(e.hp, 54);
    EXPECT_EQ(e.hp, e.max_hp);
  }
}

TEST(Enemy, CultistFirstTurnIsIncantation) {
  std::mt19937 rng(0);
  Enemy e = make_cultist(rng);
  ASSERT_TRUE(e.first_turn_move.has_value());
  EXPECT_EQ(*e.first_turn_move, MoveName::Incantation);
}

TEST(Enemy, IncantationAppliesRitualThreeToSelf) {
  std::mt19937 rng(0);
  Enemy e = make_cultist(rng);
  const Move& inc = e.moves.at(MoveName::Incantation);
  EXPECT_EQ(inc.damage, 0);
  ASSERT_EQ(inc.applies_powers.size(), 1u);
  EXPECT_EQ(inc.applies_powers[0].effect, Power::Ritual);
  EXPECT_EQ(inc.applies_powers[0].amount, 3);
  EXPECT_EQ(inc.applies_powers[0].target, Target::Enemy);
}

TEST(Enemy, DarkStrikeDealsSixBase) {
  std::mt19937 rng(0);
  Enemy e = make_cultist(rng);
  EXPECT_EQ(e.moves.at(MoveName::DarkStrike).damage, 6);
}

TEST(Enemy, CultistAlwaysDarkStrikesAfterIncantation) {
  // Deterministic AI: Incantation (turn 1) then Dark Strike forever. Drive the
  // Markov chain by hand for many steps; it must never leave Dark Strike.
  std::mt19937 rng(0);
  Enemy e = make_cultist(rng);
  // After the factory primes, last_move == Incantation (the turn-1 intent).
  ASSERT_EQ(*e.last_move, MoveName::Incantation);

  MoveName next = select_next_move(e, rng);  // resolves the turn-1 intent fork
  EXPECT_EQ(next, MoveName::DarkStrike);
  for (int i = 0; i < 20; ++i) {
    next = select_next_move(e, rng);
    EXPECT_EQ(next, MoveName::DarkStrike);
  }
}

// ============================================================================
// Louse (Red + Green) (ROB-63)
// ============================================================================

TEST(Enemy, RedLouseHpInRange) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_red_louse(rng);
    EXPECT_GE(e.hp, 10);
    EXPECT_LE(e.hp, 15);
    EXPECT_EQ(e.hp, e.max_hp);
  }
}

TEST(Enemy, GreenLouseHpInRange) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_green_louse(rng);
    EXPECT_GE(e.hp, 11);
    EXPECT_LE(e.hp, 17);
    EXPECT_EQ(e.hp, e.max_hp);
  }
}

TEST(Enemy, LouseBiteDamageRolledInRange) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_red_louse(rng);
    int dmg = e.moves.at(MoveName::Bite).damage;
    EXPECT_GE(dmg, 5);
    EXPECT_LE(dmg, 7);
  }
}

TEST(Enemy, LouseCurlUpConfigured) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_green_louse(rng);
    // Curl Up: a once=true OnDamaged GainBlock effect (ROB-65).
    const TriggeredEffect* curl = find_trigger(e, Trigger::OnDamaged);
    ASSERT_NE(curl, nullptr);
    EXPECT_EQ(curl->action, TriggeredAction::GainBlock);
    EXPECT_TRUE(curl->once);
    EXPECT_GE(curl->amount, 3);
    EXPECT_LE(curl->amount, 7);
  }
}

TEST(Enemy, RedLouseGrowGivesThreeStrengthToSelf) {
  std::mt19937 rng(0);
  Enemy e = make_red_louse(rng);
  const Move& grow = e.moves.at(MoveName::Grow);
  ASSERT_EQ(grow.applies_powers.size(), 1u);
  EXPECT_EQ(grow.applies_powers[0].effect, Power::Strength);
  EXPECT_EQ(grow.applies_powers[0].amount, 3);
  EXPECT_EQ(grow.applies_powers[0].target, Target::Enemy);
}

TEST(Enemy, GreenLouseSpitWebAppliesTwoWeakToPlayer) {
  std::mt19937 rng(0);
  Enemy e = make_green_louse(rng);
  const Move& spit = e.moves.at(MoveName::SpitWeb);
  ASSERT_EQ(spit.applies_debuffs.size(), 1u);
  EXPECT_EQ(spit.applies_debuffs[0].effect, Debuff::Weak);
  EXPECT_EQ(spit.applies_debuffs[0].amount, 2);
  EXPECT_EQ(spit.applies_debuffs[0].target, Target::Character);
}

TEST(Enemy, LouseFirstMoveIsBiteOrOther) {
  // Turn 1 is the base 75/25 roll -> always one of the two moves, never unset.
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_red_louse(rng);
    ASSERT_TRUE(e.last_move.has_value());
    EXPECT_TRUE(*e.last_move == MoveName::Bite || *e.last_move == MoveName::Grow);
    EXPECT_FALSE(e.first_turn_move.has_value());  // no fixed opener
  }
}

TEST(Enemy, LouseNeverRepeatsMoveThreeTimes) {
  // Drive the chain over a long run; the same move must never appear 3x in a
  // row (consecutive_count caps at 2, then forces a switch).
  std::mt19937 rng(0);
  Enemy e = make_red_louse(rng);
  MoveName prev1 = *e.last_move;
  MoveName prev2 = MoveName::Chomp;  // sentinel, can't match a Louse move
  bool have_two = false;
  for (int i = 0; i < 500; ++i) {
    MoveName next = select_next_move(e, rng);
    if (have_two) {
      EXPECT_FALSE(next == prev1 && prev1 == prev2)
          << "saw the same move three times in a row";
    }
    prev2 = prev1;
    prev1 = next;
    have_two = true;
  }
}

// ============================================================================
// Slimes (Acid S/M, Spike S/M) (ROB-63)
// ============================================================================

TEST(Enemy, SlimeHpRanges) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 r1(seed), r2(seed), r3(seed), r4(seed);
    Enemy as = make_acid_slime_s(r1);
    EXPECT_GE(as.hp, 8);  EXPECT_LE(as.hp, 12);
    Enemy am = make_acid_slime_m(r2);
    EXPECT_GE(am.hp, 28); EXPECT_LE(am.hp, 32);
    Enemy ss = make_spike_slime_s(r3);
    EXPECT_GE(ss.hp, 10); EXPECT_LE(ss.hp, 14);
    Enemy sm = make_spike_slime_m(r4);
    EXPECT_GE(sm.hp, 28); EXPECT_LE(sm.hp, 32);
  }
}

TEST(Enemy, AcidSlimeMMoves) {
  std::mt19937 rng(0);
  Enemy e = make_acid_slime_m(rng);
  EXPECT_EQ(e.moves.at(MoveName::Tackle).damage, 10);
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).effect, Debuff::Weak);
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).amount, 1);
  const Move& spit = e.moves.at(MoveName::CorrosiveSpit);
  EXPECT_EQ(spit.damage, 7);
  ASSERT_EQ(spit.adds_to_discard.size(), 1u);
  EXPECT_EQ(spit.adds_to_discard[0], CardId::Slimed);
}

TEST(Enemy, SpikeSlimeMMoves) {
  std::mt19937 rng(0);
  Enemy e = make_spike_slime_m(rng);
  const Move& flame = e.moves.at(MoveName::FlameTackle);
  EXPECT_EQ(flame.damage, 8);
  ASSERT_EQ(flame.adds_to_discard.size(), 1u);
  EXPECT_EQ(flame.adds_to_discard[0], CardId::Slimed);
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).effect, Debuff::Frail);
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).amount, 1);
}

TEST(Enemy, SpikeSlimeSAlwaysTackles) {
  std::mt19937 rng(0);
  Enemy e = make_spike_slime_s(rng);
  EXPECT_EQ(e.moves.at(MoveName::Tackle).damage, 5);
  ASSERT_EQ(*e.last_move, MoveName::Tackle);
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(select_next_move(e, rng), MoveName::Tackle);
  }
}

TEST(Enemy, AcidSlimeSStrictlyAlternates) {
  // After turn 1, Tackle and Lick must strictly alternate.
  std::mt19937 rng(0);
  Enemy e = make_acid_slime_s(rng);
  MoveName prev = *e.last_move;
  for (int i = 0; i < 50; ++i) {
    MoveName next = select_next_move(e, rng);
    EXPECT_NE(next, prev) << "Acid Slime (S) repeated a move";
    prev = next;
  }
}

TEST(Enemy, AcidSlimeMNeverRepeatsTackleOrLick) {
  // Tackle and Lick can't appear twice in a row; Corrosive Spit can (up to 2).
  std::mt19937 rng(0);
  Enemy e = make_acid_slime_m(rng);
  MoveName prev = *e.last_move;
  for (int i = 0; i < 1000; ++i) {
    MoveName next = select_next_move(e, rng);
    if (next == MoveName::Tackle || next == MoveName::Lick) {
      EXPECT_NE(next, prev) << "Tackle/Lick repeated";
    }
    prev = next;
  }
}

TEST(Enemy, AcidSlimeMNeverSpitsThreeInARow) {
  std::mt19937 rng(0);
  Enemy e = make_acid_slime_m(rng);
  MoveName p1 = *e.last_move, p2 = MoveName::Chomp;
  bool two = false;
  for (int i = 0; i < 1000; ++i) {
    MoveName next = select_next_move(e, rng);
    if (two && next == MoveName::CorrosiveSpit) {
      EXPECT_FALSE(p1 == MoveName::CorrosiveSpit && p2 == MoveName::CorrosiveSpit)
          << "Corrosive Spit three times in a row";
    }
    p2 = p1; p1 = next; two = true;
  }
}

// ============================================================================
// Fungi Beast (ROB-63)
// ============================================================================

TEST(Enemy, FungiBeastHpInRange) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_fungi_beast(rng);
    EXPECT_GE(e.hp, 22);
    EXPECT_LE(e.hp, 28);
    EXPECT_EQ(e.hp, e.max_hp);
  }
}

TEST(Enemy, FungiBeastMoves) {
  std::mt19937 rng(0);
  Enemy e = make_fungi_beast(rng);
  EXPECT_EQ(e.moves.at(MoveName::Bite).damage, 6);
  const Move& grow = e.moves.at(MoveName::Grow);
  ASSERT_EQ(grow.applies_powers.size(), 1u);
  EXPECT_EQ(grow.applies_powers[0].effect, Power::Strength);
  EXPECT_EQ(grow.applies_powers[0].amount, 3);
  EXPECT_EQ(grow.applies_powers[0].target, Target::Enemy);
}

TEST(Enemy, FungiBeastSporeCloudConfigured) {
  std::mt19937 rng(0);
  Enemy e = make_fungi_beast(rng);
  // Spore Cloud: OnDeath -> apply 2 Vulnerable to the player (ROB-65).
  const TriggeredEffect* spore = find_trigger(e, Trigger::OnDeath);
  ASSERT_NE(spore, nullptr);
  EXPECT_EQ(spore->action, TriggeredAction::ApplyPlayerDebuff);
  EXPECT_EQ(spore->debuff, Debuff::Vulnerable);
  EXPECT_EQ(spore->amount, 2);
}

TEST(Enemy, FungiBeastAsymmetricNoRepeat) {
  // Bite may appear at most twice in a row; Grow at most once. Drive the chain
  // a long time and assert both bounds hold.
  std::mt19937 rng(0);
  Enemy e = make_fungi_beast(rng);
  MoveName p1 = *e.last_move, p2 = MoveName::Chomp;  // sentinel
  bool two = false;
  for (int i = 0; i < 1000; ++i) {
    MoveName next = select_next_move(e, rng);
    if (next == MoveName::Grow) EXPECT_NE(p1, MoveName::Grow) << "Grow repeated";
    if (two && next == MoveName::Bite) {
      EXPECT_FALSE(p1 == MoveName::Bite && p2 == MoveName::Bite)
          << "Bite three times in a row";
    }
    p2 = p1; p1 = next; two = true;
  }
}

// ============================================================================
// Blue Slaver (ROB-63). Red Slaver deferred to ROB-76 (phased AI + Entangle).
// ============================================================================

TEST(Enemy, BlueSlaverHpInRange) {
  for (uint32_t seed = 0; seed < 50; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_blue_slaver(rng);
    EXPECT_GE(e.hp, 46);
    EXPECT_LE(e.hp, 50);
    EXPECT_EQ(e.hp, e.max_hp);
  }
}

TEST(Enemy, BlueSlaverMoves) {
  std::mt19937 rng(0);
  Enemy e = make_blue_slaver(rng);
  EXPECT_EQ(e.moves.at(MoveName::Stab).damage, 12);
  const Move& rake = e.moves.at(MoveName::Rake);
  EXPECT_EQ(rake.damage, 7);
  ASSERT_EQ(rake.applies_debuffs.size(), 1u);
  EXPECT_EQ(rake.applies_debuffs[0].effect, Debuff::Weak);
  EXPECT_EQ(rake.applies_debuffs[0].amount, 1);
  EXPECT_EQ(rake.applies_debuffs[0].target, Target::Character);
}

TEST(Enemy, BlueSlaverNeverRepeatsMoveThreeTimes) {
  std::mt19937 rng(0);
  Enemy e = make_blue_slaver(rng);
  MoveName p1 = *e.last_move, p2 = MoveName::Chomp;  // sentinel
  bool two = false;
  for (int i = 0; i < 500; ++i) {
    MoveName next = select_next_move(e, rng);
    if (two) {
      EXPECT_FALSE(next == p1 && p1 == p2) << "same move three times in a row";
    }
    p2 = p1; p1 = next; two = true;
  }
}

// ============================================================================
// Looter / Mugger (ROB-76, enriched-state AI). Thievery/gold deferred to M4/M5.
// ============================================================================

TEST(Enemy, ThiefHpAndMoveDamage) {
  std::mt19937 r1(0), r2(0);
  Enemy looter = make_looter(r1);
  EXPECT_GE(looter.hp, 44); EXPECT_LE(looter.hp, 48);
  EXPECT_EQ(looter.moves.at(MoveName::Mug).damage, 10);
  EXPECT_EQ(looter.moves.at(MoveName::Lunge).damage, 12);
  EXPECT_EQ(looter.moves.at(MoveName::SmokeBomb).block, 6);
  EXPECT_TRUE(looter.moves.at(MoveName::Escape).escapes);

  Enemy mugger = make_mugger(r2);
  EXPECT_GE(mugger.hp, 48); EXPECT_LE(mugger.hp, 52);
  EXPECT_EQ(mugger.moves.at(MoveName::Lunge).damage, 16);
  EXPECT_EQ(mugger.moves.at(MoveName::SmokeBomb).block, 11);
}

TEST(Enemy, ThiefPseudoStatesShareMugData) {
  std::mt19937 rng(0);
  Enemy e = make_looter(rng);
  // Mug1/Mug2 are enriched states that share Mug's damage.
  EXPECT_EQ(e.moves.at(MoveName::Mug1).damage, 10);
  EXPECT_EQ(e.moves.at(MoveName::Mug2).damage, 10);
}

TEST(Enemy, ThiefScriptMugsThenBranchesThenEscapes) {
  // Every run: Mug1, Mug2, then {Lunge->Smoke->Escape} or {Smoke->Escape}.
  for (uint32_t seed = 0; seed < 40; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_looter(rng);
    ASSERT_EQ(*e.first_turn_move, MoveName::Mug1);
    ASSERT_EQ(*e.last_move, MoveName::Mug1);          // turn 1
    EXPECT_EQ(select_next_move(e, rng), MoveName::Mug2);  // turn 2
    MoveName t3 = select_next_move(e, rng);              // turn 3 branch
    ASSERT_TRUE(t3 == MoveName::Lunge || t3 == MoveName::SmokeBomb);
    if (t3 == MoveName::Lunge) {
      EXPECT_EQ(select_next_move(e, rng), MoveName::SmokeBomb);
    }
    EXPECT_EQ(select_next_move(e, rng), MoveName::Escape);  // always ends here
  }
}

// ============================================================================
// Red Slaver (ROB-76). Needs Entangle (ROB-75).
// ============================================================================

TEST(Enemy, RedSlaverHpAndMoves) {
  std::mt19937 rng(0);
  Enemy e = make_red_slaver(rng);
  EXPECT_GE(e.hp, 46); EXPECT_LE(e.hp, 50);
  EXPECT_EQ(e.moves.at(MoveName::Stab).damage, 13);
  EXPECT_EQ(e.moves.at(MoveName::Scrape).damage, 8);
  EXPECT_EQ(e.moves.at(MoveName::Scrape).applies_debuffs.at(0).effect, Debuff::Weak);
  EXPECT_EQ(e.moves.at(MoveName::Entangle).applies_debuffs.at(0).effect, Debuff::Entangle);
  // Pseudo-states share data.
  EXPECT_EQ(e.moves.at(MoveName::OpenerStab).damage, 13);
  EXPECT_EQ(e.moves.at(MoveName::CycleStab).damage, 13);
  EXPECT_EQ(e.moves.at(MoveName::CycleScrape1).damage, 8);
}

TEST(Enemy, RedSlaverOpensWithStabThenEntanglesEventually) {
  for (uint32_t seed = 0; seed < 40; ++seed) {
    std::mt19937 rng(seed);
    Enemy e = make_red_slaver(rng);
    ASSERT_EQ(*e.last_move, MoveName::OpenerStab);  // turn 1 fixed
    // Within a bounded number of turns, Entangle must fire (25%/turn).
    bool entangled = false;
    for (int i = 0; i < 200 && !entangled; ++i) {
      if (select_next_move(e, rng) == MoveName::Entangle) entangled = true;
    }
    EXPECT_TRUE(entangled) << "Entangle never fired (seed " << seed << ")";
  }
}

TEST(Enemy, RedSlaverPostEntangleNoThreeInARow) {
  // Drive well past the first Entangle; post-phase Stab/Scrape must never repeat
  // three times in a row.
  std::mt19937 rng(0);
  Enemy e = make_red_slaver(rng);
  // Advance until Entangle has fired.
  while (select_next_move(e, rng) != MoveName::Entangle) { /* spin */ }
  MoveName p1 = MoveName::Entangle, p2 = MoveName::Chomp;
  bool two = false;
  for (int i = 0; i < 1000; ++i) {
    MoveName next = select_next_move(e, rng);
    if (two) EXPECT_FALSE(next == p1 && p1 == p2) << "three in a row post-Entangle";
    p2 = p1; p1 = next; two = true;
  }
}

// ============================================================================
// Large Slimes (Acid L, Spike L) — split mechanic (ROB-64)
// ============================================================================

TEST(Enemy, AcidSlimeLConfig) {
  std::mt19937 rng(0);
  Enemy e = make_acid_slime_l(rng);
  EXPECT_GE(e.hp, 65); EXPECT_LE(e.hp, 69);
  EXPECT_EQ(e.moves.at(MoveName::Tackle).damage, 16);
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).amount, 2);  // 2 Weak
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).effect, Debuff::Weak);
  const Move& spit = e.moves.at(MoveName::CorrosiveSpit);
  EXPECT_EQ(spit.damage, 11);
  EXPECT_EQ(spit.adds_to_discard.size(), 2u);  // 2 Slimed
  // Split config: HpAtOrBelow (max_hp/2) -> RewriteIntent to Split, a splits
  // move, 2 Medium children (ROB-65).
  const TriggeredEffect* split = find_trigger(e, Trigger::HpAtOrBelow);
  ASSERT_NE(split, nullptr);
  EXPECT_EQ(split->action, TriggeredAction::RewriteIntent);
  EXPECT_EQ(split->param, e.max_hp / 2);
  EXPECT_EQ(split->move, MoveName::Split);
  EXPECT_TRUE(e.moves.at(MoveName::Split).splits);
  ASSERT_EQ(e.split_children.size(), 2u);
  EXPECT_EQ(e.split_children[0].kind, EnemyKind::AcidSlimeM);
}

TEST(Enemy, SpikeSlimeLConfig) {
  std::mt19937 rng(0);
  Enemy e = make_spike_slime_l(rng);
  EXPECT_GE(e.hp, 64); EXPECT_LE(e.hp, 70);
  const Move& flame = e.moves.at(MoveName::FlameTackle);
  EXPECT_EQ(flame.damage, 16);
  EXPECT_EQ(flame.adds_to_discard.size(), 2u);  // 2 Slimed
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).effect, Debuff::Frail);
  EXPECT_EQ(e.moves.at(MoveName::Lick).applies_debuffs.at(0).amount, 2);  // 2 Frail
  const TriggeredEffect* split = find_trigger(e, Trigger::HpAtOrBelow);
  ASSERT_NE(split, nullptr);
  EXPECT_EQ(split->param, e.max_hp / 2);
  EXPECT_EQ(split->move, MoveName::Split);
  EXPECT_TRUE(e.moves.at(MoveName::Split).splits);
  ASSERT_EQ(e.split_children.size(), 2u);
  EXPECT_EQ(e.split_children[0].kind, EnemyKind::SpikeSlimeM);
}

// ============================================================================
// Gremlins (Fat, Sneaky, Mad, Wizard) (ROB-64). Shield deferred to ROB-77.
// ============================================================================

TEST(Enemy, FatGremlinSmashes) {
  std::mt19937 rng(0);
  Enemy e = make_fat_gremlin(rng);
  EXPECT_GE(e.hp, 13); EXPECT_LE(e.hp, 17);
  EXPECT_EQ(e.moves.at(MoveName::Smash).damage, 4);
  EXPECT_EQ(e.moves.at(MoveName::Smash).applies_debuffs.at(0).effect, Debuff::Weak);
  EXPECT_EQ(*e.last_move, MoveName::Smash);
  for (int i = 0; i < 10; ++i) EXPECT_EQ(select_next_move(e, rng), MoveName::Smash);
}

TEST(Enemy, SneakyGremlinPunctures) {
  std::mt19937 rng(0);
  Enemy e = make_sneaky_gremlin(rng);
  EXPECT_GE(e.hp, 10); EXPECT_LE(e.hp, 14);
  EXPECT_EQ(e.moves.at(MoveName::Puncture).damage, 9);
  for (int i = 0; i < 10; ++i) EXPECT_EQ(select_next_move(e, rng), MoveName::Puncture);
}

TEST(Enemy, MadGremlinScratchesAndIsAngry) {
  std::mt19937 rng(0);
  Enemy e = make_mad_gremlin(rng);
  EXPECT_GE(e.hp, 20); EXPECT_LE(e.hp, 24);
  EXPECT_EQ(e.moves.at(MoveName::Scratch).damage, 4);
  // Angry: OnDamaged -> +1 Strength, every hit (no `once`) — ROB-65.
  const TriggeredEffect* angry = find_trigger(e, Trigger::OnDamaged);
  ASSERT_NE(angry, nullptr);
  EXPECT_EQ(angry->action, TriggeredAction::GainStrength);
  EXPECT_EQ(angry->amount, 1);
  EXPECT_FALSE(angry->once);
  for (int i = 0; i < 10; ++i) EXPECT_EQ(select_next_move(e, rng), MoveName::Scratch);
}

TEST(Enemy, GremlinWizardChargeCycle) {
  // First cycle: 2 charges then Blast. Every cycle after: 3 charges then Blast.
  std::mt19937 rng(0);
  Enemy e = make_gremlin_wizard(rng);
  EXPECT_GE(e.hp, 23); EXPECT_LE(e.hp, 25);
  EXPECT_EQ(e.moves.at(MoveName::UltimateBlast).damage, 25);

  // Collect the resolved-move sequence. Charge* pseudo-states all mean "charge";
  // map them to whether the intent deals damage (Blast) or not (Charge).
  auto is_blast = [](MoveName m) { return m == MoveName::UltimateBlast; };
  // Turn 1 intent is the primed OpenerCharge (Charge1).
  std::vector<bool> blasts;  // per turn: was it a Blast?
  blasts.push_back(is_blast(*e.last_move));
  for (int i = 0; i < 12; ++i) blasts.push_back(is_blast(select_next_move(e, rng)));

  // Expected: [C C B]  then [C C C B] repeating -> blast at indices 2, 6, 10.
  for (int i = 0; i < static_cast<int>(blasts.size()); ++i) {
    bool expect_blast = (i == 2 || i == 6 || i == 10);
    EXPECT_EQ(blasts[i], expect_blast) << "turn " << i;
  }
}

// ============================================================================
// Shield Gremlin (ROB-77)
// ============================================================================

TEST(Enemy, ShieldGremlinConfig) {
  std::mt19937 rng(0);
  Enemy e = make_shield_gremlin(rng);
  EXPECT_GE(e.hp, 12); EXPECT_LE(e.hp, 15);
  const Move& protect = e.moves.at(MoveName::Protect);
  EXPECT_EQ(protect.block, 7);
  EXPECT_TRUE(protect.blocks_ally);
  EXPECT_EQ(e.moves.at(MoveName::ShieldBash).damage, 6);
  // Primes Protect; supports forever while allies live.
  EXPECT_EQ(*e.last_move, MoveName::Protect);
  for (int i = 0; i < 10; ++i) EXPECT_EQ(select_next_move(e, rng), MoveName::Protect);
  // Alone-rewrite config: BecameLastEnemy -> RewriteIntent to ProtectAlone.
  const TriggeredEffect* alone = find_trigger(e, Trigger::BecameLastEnemy);
  ASSERT_NE(alone, nullptr);
  EXPECT_EQ(alone->action, TriggeredAction::RewriteIntent);
  EXPECT_EQ(alone->move, MoveName::ProtectAlone);
}

TEST(Enemy, ShieldGremlinProtectAloneThenBashes) {
  // From ProtectAlone: -> Shield Bash -> Shield Bash forever.
  std::mt19937 rng(0);
  Enemy e = make_shield_gremlin(rng);
  e.last_move = MoveName::ProtectAlone;
  e.consecutive_count = 1;
  EXPECT_EQ(select_next_move(e, rng), MoveName::ShieldBash);
  for (int i = 0; i < 10; ++i) EXPECT_EQ(select_next_move(e, rng), MoveName::ShieldBash);
}

// ============================================================================
// Lagavulin (ROB-65) — elite
// ============================================================================

TEST(Enemy, LagavulinConfig) {
  std::mt19937 rng(0);
  Enemy e = make_lagavulin(rng);
  EXPECT_GE(e.hp, 109); EXPECT_LE(e.hp, 111);
  EXPECT_TRUE(e.is_asleep);
  EXPECT_EQ(e.powers[Power::Metallicize], 8);
  EXPECT_EQ(*e.last_move, MoveName::Sleep1);  // starts asleep
  EXPECT_EQ(e.moves.at(MoveName::LagavulinAttack).damage, 18);
  // Siphon Soul: -1 Str AND -1 Dex to the player.
  const Move& siphon = e.moves.at(MoveName::SiphonSoul);
  ASSERT_EQ(siphon.applies_powers.size(), 2u);
  EXPECT_EQ(siphon.applies_powers[0].amount, -1);
  EXPECT_EQ(siphon.applies_powers[1].amount, -1);
  // Sleep3 wakes on resolve (self-wake).
  EXPECT_TRUE(e.moves.at(MoveName::Sleep3).wakes_on_resolve);
}

TEST(Enemy, LagavulinSelfWakeCountdownThenAttackCycle) {
  // Undamaged: Sleep1, Sleep2, Sleep3, then Attack, Attack, Siphon, repeating.
  std::mt19937 rng(0);
  Enemy e = make_lagavulin(rng);
  EXPECT_EQ(*e.last_move, MoveName::Sleep1);
  EXPECT_EQ(select_next_move(e, rng), MoveName::Sleep2);
  EXPECT_EQ(select_next_move(e, rng), MoveName::Sleep3);
  EXPECT_EQ(select_next_move(e, rng), MoveName::LagavulinAttack1);
  EXPECT_EQ(select_next_move(e, rng), MoveName::LagavulinAttack2);
  EXPECT_EQ(select_next_move(e, rng), MoveName::SiphonSoul);
  EXPECT_EQ(select_next_move(e, rng), MoveName::LagavulinAttack1);  // loops
}

// ============================================================================
// Gremlin Nob (ROB-65) — elite
// ============================================================================

TEST(Enemy, GremlinNobConfig) {
  std::mt19937 rng(0);
  Enemy e = make_gremlin_nob(rng);
  EXPECT_GE(e.hp, 82); EXPECT_LE(e.hp, 86);
  EXPECT_EQ(*e.last_move, MoveName::Bellow);  // fixed opener
  // Bellow grants Enrage 2 (a Power, self-buff).
  const Move& bellow = e.moves.at(MoveName::Bellow);
  ASSERT_EQ(bellow.applies_powers.size(), 1u);
  EXPECT_EQ(bellow.applies_powers[0].effect, Power::Enrage);
  EXPECT_EQ(bellow.applies_powers[0].amount, 2);
  EXPECT_EQ(e.moves.at(MoveName::Rush).damage, 14);
  const Move& bash = e.moves.at(MoveName::SkullBash);
  EXPECT_EQ(bash.damage, 6);
  EXPECT_EQ(bash.applies_debuffs.at(0).effect, Debuff::Vulnerable);
  EXPECT_EQ(bash.applies_debuffs.at(0).amount, 2);
  // Enrage: OnPlayerSkill -> GainStrengthFromPower(Enrage).
  const TriggeredEffect* enrage = find_trigger(e, Trigger::OnPlayerSkill);
  ASSERT_NE(enrage, nullptr);
  EXPECT_EQ(enrage->action, TriggeredAction::GainStrengthFromPower);
  EXPECT_EQ(enrage->power, Power::Enrage);
}

TEST(Enemy, GremlinNobNoRushThreeInARow) {
  // After Bellow, Rush/Skull Bash with no Rush 3x in a row.
  std::mt19937 rng(0);
  Enemy e = make_gremlin_nob(rng);
  MoveName p1 = *e.last_move, p2 = MoveName::None;
  bool two = false;
  for (int i = 0; i < 1000; ++i) {
    MoveName next = select_next_move(e, rng);
    if (two && next == MoveName::Rush) {
      EXPECT_FALSE(p1 == MoveName::Rush && p2 == MoveName::Rush)
          << "Rush three times in a row";
    }
    p2 = p1; p1 = next; two = true;
  }
}

// ============================================================================
// Sentries (ROB-65) — elite trio
// ============================================================================

TEST(Enemy, SentryConfigAndStagger) {
  std::mt19937 rng(0);
  Enemy bolt = make_bolt_sentry(rng);
  Enemy beam = make_beam_sentry(rng);
  EXPECT_GE(bolt.hp, 38); EXPECT_LE(bolt.hp, 42);
  EXPECT_EQ(bolt.powers[Power::Artifact], 1);
  EXPECT_EQ(beam.powers[Power::Artifact], 1);
  EXPECT_EQ(bolt.moves.at(MoveName::Beam).damage, 9);
  EXPECT_EQ(bolt.moves.at(MoveName::Bolt).damage, 0);
  EXPECT_EQ(bolt.moves.at(MoveName::Bolt).adds_to_discard.size(), 2u);  // 2 Dazed
  // Stagger: bolt-sentry opens Bolt, beam-sentry opens Beam.
  EXPECT_EQ(*bolt.last_move, MoveName::Bolt);
  EXPECT_EQ(*beam.last_move, MoveName::Beam);
}

TEST(Enemy, SentriesAlternate) {
  std::mt19937 rng(0);
  Enemy e = make_bolt_sentry(rng);
  MoveName expected[] = {MoveName::Beam, MoveName::Bolt, MoveName::Beam,
                         MoveName::Bolt, MoveName::Beam};
  for (MoveName exp : expected) EXPECT_EQ(select_next_move(e, rng), exp);
}
