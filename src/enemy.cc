#include "enemy.h"

#include <cassert>
#include <cmath>

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

}  // namespace minispire
