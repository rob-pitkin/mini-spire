#include "enemy.h"

#include <cassert>
#include <cmath>
#include <random>

namespace minispire {

namespace {

// Largest consecutive count present in the table for a given last-move.
// Used to clamp consecutive_count during lookup so states beyond the table's
// resolution still resolve (defensive — should not be reached in practice
// when the table's banned-move structure is consistent).
int max_consecutive_for(
    const std::unordered_map<TransitionKey, std::vector<MoveTransition>>& transitions,
    MoveName last) {
  int max_c = 1;
  for (const auto& [key, _] : transitions) {
    if (key.last == last && key.consecutive > max_c) {
      max_c = key.consecutive;
    }
  }
  return max_c;
}

MoveName sample_from_distribution(const std::vector<MoveTransition>& dist,
                                  std::mt19937& rng) {
  std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
  float roll = uniform(rng);
  float cumulative = 0.0f;
  for (const auto& transition : dist) {
    cumulative += transition.probability;
    if (roll < cumulative) {
      return transition.next_move;
    }
  }
  // Float rounding may leave us past the cumulative — return the last entry.
  return dist.back().next_move;
}

}  // namespace

MoveName select_next_move(Enemy& enemy, std::mt19937& rng) {
  // Turn 1: fire first_turn_move if no prior move exists.
  if (!enemy.last_move.has_value()) {
    assert(enemy.first_turn_move.has_value() &&
           "Enemy must have either a prior move or a first_turn_move");
    MoveName chosen = *enemy.first_turn_move;
    enemy.last_move = chosen;
    enemy.consecutive_count = 1;
    return chosen;
  }

  // Subsequent turns: clamp consecutive_count to the table's resolution.
  int max_c = max_consecutive_for(enemy.transitions, *enemy.last_move);
  int clamped = enemy.consecutive_count < max_c ? enemy.consecutive_count : max_c;

  auto it = enemy.transitions.find({*enemy.last_move, clamped});
  assert(it != enemy.transitions.end() &&
         "Transition table missing entry for (last_move, clamped_consecutive)");

  MoveName chosen = sample_from_distribution(it->second, rng);

  if (chosen == *enemy.last_move) {
    enemy.consecutive_count += 1;
  } else {
    enemy.last_move = chosen;
    enemy.consecutive_count = 1;
  }
  return chosen;
}

Enemy make_jaw_worm(std::mt19937& rng) {
  Enemy e;
  e.kind = EnemyKind::JawWorm;

  std::uniform_int_distribution<int> hp_roll(40, 44);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      {MoveName::Chomp, {MoveName::Chomp, 11, 0, {}}},
      {MoveName::Thrash, {MoveName::Thrash, 7, 5, {}}},
      {MoveName::Bellow,
       {MoveName::Bellow, 0, 6,
        {{StatusEffect::Strength, 3, StatusApplication::Target::Enemy}}}},
  };

  e.first_turn_move = MoveName::Chomp;

  e.transitions = {
      {{MoveName::Chomp, 1},
       {{MoveName::Thrash, 0.41f}, {MoveName::Bellow, 0.59f}}},
      {{MoveName::Bellow, 1},
       {{MoveName::Chomp, 0.44f}, {MoveName::Thrash, 0.56f}}},
      {{MoveName::Thrash, 1},
       {{MoveName::Chomp, 0.25f},
        {MoveName::Thrash, 0.30f},
        {MoveName::Bellow, 0.45f}}},
      {{MoveName::Thrash, 2},
       {{MoveName::Chomp, 0.36f}, {MoveName::Bellow, 0.64f}}},
  };

  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  // Validate: every entry's probabilities sum to 1.0 within epsilon.
  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    (void)sum;
    assert(std::abs(sum - 1.0f) < 1e-4f &&
           "Transition probabilities must sum to 1.0");
  }

  // Prime the intent. The factory is responsible for leaving the enemy in a
  // "ready to fight" state where last_move stores the upcoming intent and
  // the agent's observation can read it. After this call, last_move is set
  // to first_turn_move and consecutive_count is 1.
  select_next_move(e, rng);

  return e;
}

Enemy make_cultist(std::mt19937& rng) {
  Enemy e;
  e.kind = EnemyKind::Cultist;

  std::uniform_int_distribution<int> hp_roll(48, 54);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  e.moves = {
      // Incantation: gain Ritual 3 (self-buff via Target::Enemy). Ritual grants
      // Strength at the start of each subsequent turn (ROB-73), so the first
      // Dark Strike (turn 2) already hits for 6 + 3 = 9.
      {MoveName::Incantation,
       {MoveName::Incantation, 0, 0,
        {{StatusEffect::Ritual, 3, StatusApplication::Target::Enemy}}}},
      {MoveName::DarkStrike, {MoveName::DarkStrike, 6, 0, {}}},
  };

  e.first_turn_move = MoveName::Incantation;

  // Deterministic AI: Incantation once (turn 1), then Dark Strike forever.
  // The (DarkStrike, 1) entry covers all repeat counts — consecutive_count is
  // clamped to the table's max (1) in select_next_move.
  e.transitions = {
      {{MoveName::Incantation, 1}, {{MoveName::DarkStrike, 1.0f}}},
      {{MoveName::DarkStrike, 1}, {{MoveName::DarkStrike, 1.0f}}},
  };

  e.last_move = std::nullopt;
  e.consecutive_count = 0;

  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    (void)sum;
    assert(std::abs(sum - 1.0f) < 1e-4f &&
           "Transition probabilities must sum to 1.0");
  }

  select_next_move(e, rng);

  return e;
}

namespace {

// Shared Louse factory (ROB-63). Red and Green Louse are identical except for
// HP range and their non-Bite move (Grow vs SpitWeb). Bite damage is rolled
// once per fight (5-7); Curl Up block is rolled once (3-7). AI: 75% Bite /
// 25% other, no move three times in a row (forced switch at consecutive 2).
Enemy make_louse(std::mt19937& rng, EnemyKind kind, int hp_lo, int hp_hi,
                 MoveName other_move, const Move& other_move_data) {
  Enemy e;
  e.kind = kind;

  std::uniform_int_distribution<int> hp_roll(hp_lo, hp_hi);
  e.max_hp = hp_roll(rng);
  e.hp = e.max_hp;
  e.current_block = 0;

  // Bite damage fixed for the whole fight.
  std::uniform_int_distribution<int> bite_roll(5, 7);
  int bite_damage = bite_roll(rng);

  e.moves = {
      {MoveName::Bite, {MoveName::Bite, bite_damage, 0, {}}},
      {other_move, other_move_data},
  };

  // Curl Up: gain X block (rolled 3-7) on the first damage taken (ROB-62).
  std::uniform_int_distribution<int> curl_roll(3, 7);
  e.on_damaged = OnDamagedEffect::CurlUp;
  e.curl_available = true;
  e.curl_block = curl_roll(rng);

  // Transition table: 75% Bite / 25% other when both legal; forced switch when
  // a move has been used twice in a row (no three-in-a-row).
  const std::vector<MoveTransition> base = {{MoveName::Bite, 0.75f},
                                            {other_move, 0.25f}};
  e.transitions = {
      {{MoveName::Bite, 1}, base},
      {{MoveName::Bite, 2}, {{other_move, 1.0f}}},
      {{other_move, 1}, base},
      {{other_move, 2}, {{MoveName::Bite, 1.0f}}},
  };

  // Turn 1 is the same base 75/25 roll (no fixed opener). We roll it here and
  // prime the queued intent directly: `last_move` holds the *upcoming* move
  // (see its doc on Enemy), so this sets the turn-1 intent the obs displays.
  // first_turn_move stays unset — select_next_move is never reached with an
  // unprimed Louse.
  e.first_turn_move = std::nullopt;
  e.last_move = sample_from_distribution(base, rng);
  e.consecutive_count = 1;

  for (const auto& [key, dist] : e.transitions) {
    float sum = 0.0f;
    for (const auto& t : dist) sum += t.probability;
    (void)sum;
    assert(std::abs(sum - 1.0f) < 1e-4f &&
           "Transition probabilities must sum to 1.0");
  }

  return e;
}

}  // namespace

Enemy make_red_louse(std::mt19937& rng) {
  // Grow: gain 3 Strength (self-buff via Target::Enemy).
  Move grow{MoveName::Grow, 0, 0,
            {{StatusEffect::Strength, 3, StatusApplication::Target::Enemy}}};
  return make_louse(rng, EnemyKind::RedLouse, 10, 15, MoveName::Grow, grow);
}

Enemy make_green_louse(std::mt19937& rng) {
  // SpitWeb: apply 2 Weak to the player.
  Move spit{MoveName::SpitWeb, 0, 0,
            {{StatusEffect::Weak, 2, StatusApplication::Target::Character}}};
  return make_louse(rng, EnemyKind::GreenLouse, 11, 17, MoveName::SpitWeb, spit);
}

}  // namespace minispire
