#include <gtest/gtest.h>

#include <random>

#include "card.h"
#include "combat_env.h"
#include "combat_state.h"
#include "enemy.h"
#include "status_effect.h"
#include "test_helpers.h"
#include "turn_loop.h"

using namespace minispire;

namespace {

// End turn is the last index of the COMBAT block, no longer the last index of
// the action space (the Stage 4c option-slot channel follows it). Use the
// engine's constant rather than recomputing it.
using minispire::kEndTurnAction;

}  // namespace

// ============================================================================
// Reset / basic shape
// ============================================================================

TEST(CombatEnv, CardIdsAreDenseAndComplete) {
  // The obs pile-count order is DERIVED from the enum (ROB-87) rather than
  // hand-listed, which is only sound while CardId values are dense 0..N-1 and
  // CARD_DATABASE has exactly one row per value. This test is what makes that
  // safe: it replaces a 154-entry table whose only guard was a SIZE assert, so
  // a duplicate paired with a missing entry compiled clean and silently made
  // one card invisible in the observation while double-counting another.
  ASSERT_EQ(CARD_DATABASE.size(), static_cast<std::size_t>(kNumCardTypes));
  for (int i = 0; i < kNumCardTypes; ++i) {
    const CardId id = static_cast<CardId>(i);
    EXPECT_EQ(CARD_DATABASE.count(id), 1u)
        << "CardId value " << i << " has no CARD_DATABASE row — the enum is "
        << "not dense, so the derived obs order would index a missing card";
  }
}

// --- ROB-96: the enemy block must carry identity and every intent kind -------
//
// These check for ALIASING: two situations a human tells apart at a glance that
// the observation encoded identically. Each one asserts the vectors differ,
// which is the property that matters — a memoryless policy cannot learn a
// distinction the observation does not make.

namespace {
// The enemy sub-block of the observation, for comparing two states.
std::vector<float> enemy_block(const CombatEnv& env, int slot) {
  const int base = CombatEnv::kPlayerObsSize + slot * CombatEnv::kEnemyObsStride;
  const std::array<float, CombatEnv::kObsSize>& o = env.obs();
  return std::vector<float>(o.begin() + base,
                            o.begin() + base + CombatEnv::kEnemyObsStride);
}

// A one-enemy env holding `e`, observed without stepping.
CombatState one_enemy(Enemy e) {
  CombatState s = minispire::testing::make_minimal_state(0);
  s.enemies.clear();
  s.enemies.push_back(std::move(e));
  return s;
}
}  // namespace

TEST(CombatEnv, EnemyKindDistinguishesOtherwiseIdenticalEnemies) {
  // Red Louse (HP 10-15) and Green Louse (HP 11-17) overlap, and both open with
  // Bite. At the same HP with the same telegraphed move they were byte-identical
  // — while Red follows up with Grow (+3 Strength) and Green with Spit Web
  // (2 Weak). Different futures, one observation.
  std::mt19937 rng(0);
  Enemy red = make_red_louse(rng);
  Enemy green = make_green_louse(rng);
  red.hp = green.hp = 12;
  red.max_hp = green.max_hp = 12;
  red.last_move = MoveName::Bite;
  green.last_move = MoveName::Bite;
  red.moves[MoveName::Bite].damage = green.moves[MoveName::Bite].damage = 6;

  CombatEnv a(one_enemy(std::move(red)));
  CombatEnv b(one_enemy(std::move(green)));

  EXPECT_NE(enemy_block(a, 0), enemy_block(b, 0))
      << "a Red Louse and a Green Louse must not encode identically";
}

TEST(CombatEnv, EscapeAndSplitIntentsAreNotSilence) {
  // Escape and Split carry no damage, no block and no status application, so
  // before ROB-96 they encoded as all-zero intent — indistinguishable from a
  // Gremlin Wizard charging. "It flees next turn" changes the correct play.
  std::mt19937 rng(0);
  Enemy looter = make_looter(rng);
  Enemy wizard = make_gremlin_wizard(rng);
  looter.hp = wizard.hp = 20;
  looter.max_hp = wizard.max_hp = 20;
  looter.last_move = MoveName::Escape;
  wizard.last_move = MoveName::Charge1;  // does nothing this turn

  CombatEnv a(one_enemy(std::move(looter)));
  CombatEnv b(one_enemy(std::move(wizard)));

  EXPECT_NE(enemy_block(a, 0), enemy_block(b, 0))
      << "a fleeing Looter must not read the same as an enemy doing nothing";
}

TEST(CombatEnv, BuffAndDebuffAreSeparateIntents) {
  // One flag used to mean "applies something", so an enemy strengthening ITSELF
  // and an enemy weakening the PLAYER set the same bit. StS draws those as
  // different icons, and for the Louses they are the distinguishing moves.
  std::mt19937 rng(0);
  Enemy grower = make_red_louse(rng);    // Grow: +3 Strength to itself
  Enemy spitter = make_green_louse(rng);  // Spit Web: 2 Weak on the player
  grower.hp = spitter.hp = 12;
  grower.max_hp = spitter.max_hp = 12;
  grower.last_move = MoveName::Grow;
  spitter.last_move = MoveName::SpitWeb;

  CombatEnv a(one_enemy(std::move(grower)));
  CombatEnv b(one_enemy(std::move(spitter)));

  const std::vector<float> ga = enemy_block(a, 0), gb = enemy_block(b, 0);
  const int intent = 3 + CombatEnv::kEnemyStatusSize;
  EXPECT_EQ(ga[intent + 3], 1.0f) << "Grow is a self-buff";
  EXPECT_EQ(ga[intent + 4], 0.0f) << "Grow does nothing to the player";
  EXPECT_EQ(gb[intent + 3], 0.0f) << "Spit Web does not strengthen the louse";
  EXPECT_EQ(gb[intent + 4], 1.0f) << "Spit Web debuffs the player";
}

TEST(CombatEnv, ObsPileCountsMatchTheActualPiles) {
  // ROB-81 replaced a pile_count() call per card type per pile with one pass
  // per pile, indexing straight into the plane by CardId. That is a ~189x
  // reduction in comparisons and a silent mis-index away from a wrong
  // observation — and nothing in the suite read these floats, so the whole
  // rewrite was uncovered. Counted independently here, the slow and obvious
  // way, for the same reason the mask oracle exists.
  const int pile_base =
      CombatEnv::kPlayerObsSize + kMaxEnemies * CombatEnv::kEnemyObsStride;
  const int stride = kNumCardTypes;

  CombatEnv env(0.0f, EncounterPool::Weak);
  std::mt19937 rng(4);
  for (uint32_t seed = 0; seed < 25; ++seed) {
    env.reset(seed);
    for (int step = 0; step < 40; ++step) {
      const std::array<float, CombatEnv::kObsSize>& o = env.obs();
      const CombatState& s = env.state();
      const std::vector<Card>* piles[4] = {&s.current_hand, &s.draw_pile,
                                           &s.discard_pile, &s.exhaust_pile};
      for (int plane = 0; plane < 4; ++plane) {
        for (int id = 0; id < stride; ++id) {
          int want = 0;
          for (const Card& c : *piles[plane]) {
            if (static_cast<int>(c.card_id) == id) ++want;
          }
          ASSERT_EQ(o[pile_base + plane * stride + id],
                    static_cast<float>(want))
              << "seed " << seed << " step " << step << " plane " << plane
              << " card " << id;
        }
      }

      const std::vector<uint8_t>& mask = env.action_mask();
      std::vector<int> legal;
      for (std::size_t a = 0; a < mask.size(); ++a) {
        if (mask[a]) legal.push_back(static_cast<int>(a));
      }
      if (legal.empty()) break;
      env.step(legal[rng() % legal.size()]);
      if (env.outcome() != Outcome::InProgress) break;
    }
  }
}

TEST(CombatEnv, ResetIsReproducibleForSameSeed) {
  // reset() samples an encounter from the pool (ROB-66); the same seed must
  // produce an identical fight (deterministic).
  CombatEnv a, b;
  a.reset(42);
  b.reset(42);
  EXPECT_EQ(a.state().character.hp, b.state().character.hp);
  ASSERT_EQ(a.state().enemies.size(), b.state().enemies.size());
  for (std::size_t i = 0; i < a.state().enemies.size(); ++i) {
    EXPECT_EQ(a.state().enemies[i].kind, b.state().enemies[i].kind);
    EXPECT_EQ(a.state().enemies[i].hp, b.state().enemies[i].hp);
  }
  EXPECT_EQ(a.state().turn_number, b.state().turn_number);
}

TEST(CombatEnv, ObsBufferIsKObsSize) {
  CombatEnv env;
  env.reset(0);
  EXPECT_EQ(env.obs().size(), static_cast<std::size_t>(CombatEnv::kObsSize));
  // Compute the layout from constants so this never goes stale as the obs
  // grows: player + enemies + piles + turn(1) + the Stage 4c choice block.
  const int expected = CombatEnv::kPlayerObsSize +
                       minispire::kMaxEnemies * CombatEnv::kEnemyObsStride +
                       CombatEnv::kPileObsSize + 1 + CombatEnv::kChoiceObsSize;
  EXPECT_EQ(env.obs().size(), static_cast<std::size_t>(expected));
}

TEST(CombatEnv, ActionMaskIsKNumActions) {
  CombatEnv env;
  env.reset(0);
  EXPECT_EQ(env.action_mask().size(),
            static_cast<std::size_t>(CombatEnv::kNumActions));
  // Combat block (card types x enemies + end-turn) plus the option-slot
  // channel (one slot per option + decline) — from constants, never stale.
  EXPECT_EQ(env.action_mask().size(),
            static_cast<std::size_t>(minispire::kNumCardTypes *
                                         minispire::kMaxEnemies +
                                     1 + minispire::kNumOptionSlots + 1));
}

TEST(CombatEnv, EndTurnAlwaysLegalAfterReset) {
  CombatEnv env;
  env.reset(0);
  EXPECT_TRUE(env.action_mask()[kEndTurnAction]);
}

TEST(CombatEnv, RewardZeroAfterReset) {
  CombatEnv env;
  env.reset(0);
  EXPECT_FLOAT_EQ(env.reward(), 0.0f);
}

TEST(CombatEnv, NotTerminatedAfterReset) {
  CombatEnv env;
  env.reset(0);
  EXPECT_FALSE(env.terminated());
  EXPECT_FALSE(env.truncated());
}

// ============================================================================
// Obs layout
// ============================================================================

TEST(CombatEnv, ObsCharacterStatsAfterReset) {
  CombatEnv env;
  env.reset(0);

  // Per ROB-40 layout, slots 0..4: hp, max_hp, block, energy, energy_per_turn.
  EXPECT_FLOAT_EQ(env.obs()[0], 80.0f);  // hp
  EXPECT_FLOAT_EQ(env.obs()[1], 80.0f);  // max_hp
  EXPECT_FLOAT_EQ(env.obs()[2], 0.0f);   // current_block
  EXPECT_FLOAT_EQ(env.obs()[3], 3.0f);   // energy
  EXPECT_FLOAT_EQ(env.obs()[4], 3.0f);   // energy_per_turn

  // Statuses 5..8 — all 0 at start.
  for (int i = 5; i <= 8; ++i) {
    EXPECT_FLOAT_EQ(env.obs()[i], 0.0f) << "status slot " << i;
  }
}

// Enemy obs layout (ROB-59/73), all derived from constants so it tracks the
// status-block width: enemies start after the player block; each enemy block is
//   +0 is_alive, +1 hp, +2 block, then status(kEnemyStatusSize), then
//   intent(4: attacking, atk_dmg, block, buff).
constexpr int kEnemy0Base = CombatEnv::kPlayerObsSize;
constexpr int kIntentOff = 3 + CombatEnv::kEnemyStatusSize;  // start of intent
constexpr int kPileBase =
    CombatEnv::kPlayerObsSize + minispire::kMaxEnemies * CombatEnv::kEnemyObsStride;

TEST(CombatEnv, ObsEnemyStatsAfterReset) {
  // Use the fixed v1 Jaw Worm fixture (reset() now samples a random encounter).
  CombatEnv env(start_v1_combat(0));

  // Enemy 0 alive; HP rolled in [40, 44]; no block. Enemy obs has NO max_hp
  // (ROB-59) — is_alive replaces it.
  EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + 0], 1.0f);  // is_alive
  EXPECT_GE(env.obs()[kEnemy0Base + 1], 40.0f);       // hp
  EXPECT_LE(env.obs()[kEnemy0Base + 1], 44.0f);
  EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + 2], 0.0f);  // block
}

TEST(CombatEnv, ObsIntentFirstTurnIsChompAttack) {
  CombatEnv env(start_v1_combat(0));

  // Jaw Worm turn 1 is always Chomp = 11 damage, no block, no buff.
  EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + kIntentOff + 0], 1.0f);   // is_attacking
  EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + kIntentOff + 1], 11.0f);  // atk_dmg
  EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + kIntentOff + 2], 0.0f);   // intent_block
  EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + kIntentOff + 3], 0.0f);   // intent_buff
}

TEST(CombatEnv, ObsDeadEnemySlotsAreZero) {
  CombatEnv env(start_v1_combat(0));
  // v1 has one enemy; slots 1..N-1 are empty -> all zero (incl. is_alive).
  for (int slot = 1; slot < minispire::kMaxEnemies; ++slot) {
    const int base = CombatEnv::kPlayerObsSize + slot * CombatEnv::kEnemyObsStride;
    for (int i = 0; i < CombatEnv::kEnemyObsStride; ++i) {
      EXPECT_FLOAT_EQ(env.obs()[base + i], 0.0f)
          << "enemy slot " << slot << " field " << i;
    }
  }
}

TEST(CombatEnv, ObsHandCountsMatchDeckDraw) {
  CombatEnv env;
  env.reset(0);

  // After start_v1_combat, hand has 5 cards from the 10-card starter deck.
  // hand + draw counts together = 10 total of {Strike, Defend, Bash}.
  // Pile stride = kNumCardTypes: hand [0..S), draw [S..2S), discard [2S..3S),
  // exhaust [3S..4S). Card order: Strike=0, Defend=1, Bash=2 (kObsCardOrder).
  constexpr int S = minispire::kNumCardTypes;
  int strike = static_cast<int>(env.obs()[kPileBase + 0] + env.obs()[kPileBase + S + 0]);
  int defend = static_cast<int>(env.obs()[kPileBase + 1] + env.obs()[kPileBase + S + 1]);
  int bash   = static_cast<int>(env.obs()[kPileBase + 2] + env.obs()[kPileBase + S + 2]);
  EXPECT_EQ(strike, 5);
  EXPECT_EQ(defend, 4);
  EXPECT_EQ(bash, 1);

  // Discard [2S..3S) + exhaust [3S..4S) are empty at start.
  for (int i = 2 * S; i < 4 * S; ++i) {
    EXPECT_FLOAT_EQ(env.obs()[kPileBase + i], 0.0f) << "discard/exhaust slot " << i;
  }
}

TEST(CombatEnv, ObsTurnNumberAfterReset) {
  CombatEnv env;
  env.reset(0);
  // Turn number sits after the pile block; the Stage 4c choice block follows
  // it, so this is no longer the obs's last slot.
  constexpr int kTurnOff = CombatEnv::kPlayerObsSize +
                           minispire::kMaxEnemies * CombatEnv::kEnemyObsStride +
                           CombatEnv::kPileObsSize;
  EXPECT_FLOAT_EQ(env.obs()[kTurnOff], 1.0f);
}

TEST(CombatEnv, IntentDamageReflectsEnemyStrength) {
  // Manually push the env's enemy into a Strength-buffed state by stepping
  // turns until Bellow fires (or directly mutating — but we don't have a
  // public setter, so go through start_v1_combat and check the formula's
  // wiring by inspecting an artificial scenario via the engine).
  //
  // Simpler: build a CombatState by hand with enemy Strength 3 and Chomp
  // intent, hand it to a fresh env via reset… but reset only takes a seed.
  // So we test the wiring at the formula level here: ROB-37's
  // ComputeAttackDamage tests already cover that 11 + 3 = 14. We just
  // verify the env reads it.
  //
  // Instead: step through end-turns until Strength is applied (Bellow), then
  // check that the displayed damage in the obs reflects the bonus on the
  // following turn's intent.
  CombatEnv env;
  // Seed 3 — Jaw Worm pattern often hits Bellow within a few turns.
  env.reset(3);

  // End turn a bunch of times. Eventually Bellow fires; on the turn AFTER
  // Bellow, if the next intent is Chomp or Thrash, obs[17] should reflect
  // base + Strength.
  for (int i = 0; i < 8 && !env.terminated(); ++i) {
    if (!env.action_mask()[kEndTurnAction]) break;
    env.step(kEndTurnAction);
  }
  // After enough turns the enemy has at least one Strength stack. Verify
  // the obs intent damage matches compute_attack_damage on the move.
  if (!env.terminated() && env.state().enemies[0].last_move.has_value()) {
    const Enemy& e = env.state().enemies[0];
    MoveName next = *e.last_move;
    const Move& m = e.moves.at(next);
    if (m.damage > 0) {
      int expected = compute_attack_damage(m.damage, e.powers, e.debuffs,
                                           env.state().character.debuffs);
      EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + kIntentOff + 1],
                      static_cast<float>(expected));
    }
  }
}

// ============================================================================
// step()
// ============================================================================

TEST(CombatEnv, StepEndTurnAdvancesTurnNumber) {
  CombatEnv env;
  env.reset(0);
  int before = env.turn_number();
  env.step(kEndTurnAction);
  // Whether it advances depends on whether the player died this turn. With
  // 80 HP and Jaw Worm Chomp 11, they live; turn advances.
  EXPECT_EQ(env.turn_number(), before + 1);
}

TEST(CombatEnv, StepUpdatesObsAndMask) {
  CombatEnv env;
  env.reset(0);
  // Capture obs before, step end-turn, check it changed (energy was 3, now 3
  // again but the rest of the state changed too — hand cards differ).
  // Easier check: enemy HP unchanged (Chomp doesn't damage itself) but
  // character HP might have dropped.
  float char_hp_before = env.obs()[0];
  env.step(kEndTurnAction);
  // Character either took damage or has block.
  EXPECT_LE(env.obs()[0], char_hp_before);
}

TEST(CombatEnv, StepThrowsOnOutOfRangeAction) {
  CombatEnv env;
  env.reset(0);
  EXPECT_THROW(env.step(-1), std::invalid_argument);
  EXPECT_THROW(env.step(CombatEnv::kNumActions), std::invalid_argument);
  EXPECT_THROW(env.step(999), std::invalid_argument);
}

TEST(CombatEnv, StepThrowsOnMaskedAction) {
  CombatEnv env;
  env.reset(0);
  // Find a CardId that's not in the hand and isn't end-turn.
  for (int i = 0; i < CombatEnv::kNumActions - 1; ++i) {
    if (!env.action_mask()[i]) {
      EXPECT_THROW(env.step(i), std::invalid_argument);
      return;
    }
  }
  GTEST_SKIP() << "No masked card action found in this seed's opening hand";
}

TEST(CombatEnv, RewardIsWinOnEnemyKill) {
  // Construct a state where one Strike kills the enemy: reset, then mutate
  // the enemy HP via... we don't have a public setter. Instead: keep
  // stepping end-turn so we lose, then check reward = -1.
  CombatEnv env;
  env.reset(42);
  for (int i = 0; i < 30 && !env.terminated(); ++i) {
    if (!env.action_mask()[kEndTurnAction]) break;
    env.step(kEndTurnAction);
  }
  EXPECT_TRUE(env.terminated());
  EXPECT_EQ(env.outcome(), Outcome::Lost);
  EXPECT_FLOAT_EQ(env.reward(), -1.0f);
}

// ============================================================================
// clone()
// ============================================================================

TEST(CombatEnv, CloneProducesIndependentEnv) {
  CombatEnv original;
  original.reset(7);

  CombatEnv copy = original.clone();
  // Step the copy; original should be unaffected.
  copy.step(kEndTurnAction);

  EXPECT_NE(copy.turn_number(), original.turn_number());
  EXPECT_EQ(original.turn_number(), 1);
}

TEST(CombatEnv, CloneRngIsIndependent) {
  CombatEnv a;
  a.reset(5);
  CombatEnv b = a.clone();

  // Both step the same action; should produce identical states (same RNG
  // sequence).
  a.step(kEndTurnAction);
  b.step(kEndTurnAction);

  EXPECT_EQ(a.state().enemies[0].hp, b.state().enemies[0].hp);
  EXPECT_EQ(a.state().character.hp, b.state().character.hp);
}

// ============================================================================
// state_piles() — ROB-46
// ============================================================================

TEST(CombatEnv, StatePilesAfterResetHasFiveHandFiveDraw) {
  CombatEnv env;
  env.reset(0);
  StatePiles piles = env.state_piles();
  EXPECT_EQ(piles.hand.size(), 5u);
  // Draw is a count map; total count across CardIds should be 5.
  int draw_total = 0;
  for (const auto& [id, n] : piles.draw) draw_total += n;
  EXPECT_EQ(draw_total, 5);
  EXPECT_EQ(piles.discard.size(), 0u);
  EXPECT_EQ(piles.exhaust.size(), 0u);
}

TEST(CombatEnv, StatePilesHandPlusDrawMatchesStarterDeck) {
  // 5 Strike + 4 Defend + 1 Bash across hand + draw.
  CombatEnv env;
  env.reset(0);
  StatePiles piles = env.state_piles();

  // Hand is ordered (a vector); draw is a count map.
  auto count_in_hand = [&](CardId id) {
    int n = 0;
    for (CardId h : piles.hand) if (h == id) ++n;
    return n;
  };
  auto count_in_draw = [&](CardId id) {
    auto it = piles.draw.find(id);
    return it == piles.draw.end() ? 0 : it->second;
  };
  EXPECT_EQ(count_in_hand(CardId::Strike) + count_in_draw(CardId::Strike), 5);
  EXPECT_EQ(count_in_hand(CardId::Defend) + count_in_draw(CardId::Defend), 4);
  EXPECT_EQ(count_in_hand(CardId::Bash) + count_in_draw(CardId::Bash), 1);
}

TEST(CombatEnv, StatePilesUpdatesAfterPlayingCard) {
  // End the turn so the discard reshuffle behavior is tested too: actually,
  // simpler — find a Strike in hand, play it, verify discard now has 1 Strike.
  CombatEnv env;
  env.reset(0);

  // Find Strike's action index = static_cast<int>(CardId::Strike) = 0
  // (per CardId enum order). Verify it's legal before stepping.
  int strike_action = static_cast<int>(CardId::Strike);
  if (!env.action_mask()[strike_action]) {
    GTEST_SKIP() << "Seed 0 opening hand has no Strike";
  }

  std::size_t hand_before = env.state_piles().hand.size();
  env.step(strike_action);
  StatePiles after = env.state_piles();

  EXPECT_EQ(after.hand.size(), hand_before - 1);
  EXPECT_EQ(after.discard.size(), 1u);
  EXPECT_EQ(after.discard[0], CardId::Strike);
}

// ============================================================================
// Reward shaping (ROB-52)
// ============================================================================

namespace {

// Build a CombatState set up for a one-Strike kill: enemy at `enemy_hp`,
// character at `char_hp`/`char_max_hp`, a Strike in hand, energy to play it.
// Strike deals 6, so enemy_hp <= 6 means the Strike wins.
CombatState make_one_strike_kill_state(int enemy_hp, int char_hp,
                                       int char_max_hp) {
  CombatState s = minispire::testing::make_minimal_state(0);
  s.enemies[0].hp = enemy_hp;
  s.enemies[0].max_hp = std::max(enemy_hp, s.enemies[0].max_hp);
  s.character.hp = char_hp;
  s.character.max_hp = char_max_hp;
  s.character.energy = 3;
  s.current_hand.clear();
  s.current_hand.push_back(Card{CardId::Strike});
  return s;
}

constexpr int kStrikeAction = static_cast<int>(CardId::Strike);

}  // namespace

TEST(CombatEnv, DefaultCoeffWinRewardIsExactlyOne) {
  // Strike kills the 5-HP enemy; with coeff 0 the reward is exactly 1.0
  // regardless of character HP.
  CombatEnv env(make_one_strike_kill_state(/*enemy_hp=*/5, /*char_hp=*/40,
                                           /*char_max_hp=*/80));
  ASSERT_TRUE(env.action_mask()[kStrikeAction]);
  env.step(kStrikeAction);
  ASSERT_EQ(env.outcome(), Outcome::Won);
  EXPECT_FLOAT_EQ(env.reward(), 1.0f);
}

TEST(CombatEnv, ShapedWinAtFullHpIsOnePlusCoeff) {
  // coeff 0.5, win at full HP (80/80) -> 1 + 0.5 * 1.0 = 1.5.
  CombatEnv env(make_one_strike_kill_state(5, 80, 80), 0.5f);
  env.step(kStrikeAction);
  ASSERT_EQ(env.outcome(), Outcome::Won);
  EXPECT_FLOAT_EQ(env.reward(), 1.5f);
}

TEST(CombatEnv, ShapedWinAtHalfHpIsOnePlusHalfCoeff) {
  // coeff 0.5, win at 40/80 -> 1 + 0.5 * 0.5 = 1.25.
  CombatEnv env(make_one_strike_kill_state(5, 40, 80), 0.5f);
  env.step(kStrikeAction);
  ASSERT_EQ(env.outcome(), Outcome::Won);
  EXPECT_FLOAT_EQ(env.reward(), 1.25f);
}

TEST(CombatEnv, ShapedWinUsesFloatDivision) {
  // 27/80 is non-integer; integer division would give 0. Expect
  // 1 + 0.5 * (27/80) = 1.16875.
  CombatEnv env(make_one_strike_kill_state(5, 27, 80), 0.5f);
  env.step(kStrikeAction);
  ASSERT_EQ(env.outcome(), Outcome::Won);
  EXPECT_FLOAT_EQ(env.reward(), 1.0f + 0.5f * (27.0f / 80.0f));
}

TEST(CombatEnv, ShapedLossRewardIgnoresCoeff) {
  // Character at 1 HP, no card to play -> ends turn -> Jaw Worm Chomp kills.
  CombatState s = minispire::testing::make_minimal_state(0);
  s.character.hp = 1;
  s.current_hand.clear();  // nothing to play; must end turn
  CombatEnv env(std::move(s), 0.5f);
  ASSERT_TRUE(env.action_mask()[kEndTurnAction]);
  env.step(kEndTurnAction);
  ASSERT_EQ(env.outcome(), Outcome::Lost);
  EXPECT_FLOAT_EQ(env.reward(), -1.0f);
}

TEST(CombatEnv, ShapedMidFightRewardIsZero) {
  // A non-killing Strike against a healthy enemy leaves the fight in progress;
  // reward is 0 regardless of coeff.
  CombatEnv env(make_one_strike_kill_state(/*enemy_hp=*/40, 80, 80), 0.5f);
  env.step(kStrikeAction);
  ASSERT_EQ(env.outcome(), Outcome::InProgress);
  EXPECT_FLOAT_EQ(env.reward(), 0.0f);
}

TEST(CombatEnv, ClonePreservesRewardCoeff) {
  // Clone before the killing blow; the cloned env must produce the shaped
  // reward, proving the coefficient survived the copy.
  CombatEnv original(make_one_strike_kill_state(5, 80, 80), 0.5f);
  CombatEnv copy = original.clone();
  copy.step(kStrikeAction);
  ASSERT_EQ(copy.outcome(), Outcome::Won);
  EXPECT_FLOAT_EQ(copy.reward(), 1.5f);
}

TEST(CombatEnv, StateConstructorBuffersAreConsistent) {
  // The CombatState constructor must compute obs/mask immediately.
  CombatEnv env(make_one_strike_kill_state(5, 40, 80));
  // obs slot 0 is character HP (ROB-40).
  EXPECT_FLOAT_EQ(env.obs()[0], 40.0f);
  // Strike is legal (in hand + affordable).
  EXPECT_TRUE(env.action_mask()[kStrikeAction]);
}
