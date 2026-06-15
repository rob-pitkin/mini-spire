#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "status_effect.h"

namespace minispire {

enum class EnemyKind {
  JawWorm,
};

enum class MoveName {
  Chomp,
  Thrash,
  Bellow,
};

// FUTURE: multi-hit moves (Lagavulin's attacks, Hexaghost) need a `hits`
// field — Strength applies per-hit. v1 moves are single-hit.
struct Move {
  MoveName name;
  int damage;
  int block;
  std::vector<StatusApplication> applies;
};

struct MoveTransition {
  MoveName next_move;
  float probability;
};

struct TransitionKey {
  MoveName last;
  int consecutive;

  bool operator==(const TransitionKey& other) const {
    return last == other.last && consecutive == other.consecutive;
  }
};

}  // namespace minispire

namespace std {
template <>
struct hash<minispire::TransitionKey> {
  std::size_t operator()(const minispire::TransitionKey& k) const noexcept {
    std::size_t h1 = std::hash<int>{}(static_cast<int>(k.last));
    std::size_t h2 = std::hash<int>{}(k.consecutive);
    return h1 ^ (h2 << 1);
  }
};
}  // namespace std

namespace minispire {

struct Enemy {
  EnemyKind kind;
  int hp;
  int max_hp;
  int current_block;
  std::unordered_map<StatusEffect, int> status_effects;

  std::unordered_map<MoveName, Move> moves;
  std::optional<MoveName> first_turn_move;
  std::unordered_map<TransitionKey, std::vector<MoveTransition>> transitions;

  // last_move stores the upcoming intent — the move that will fire on the
  // next enemy turn. It does double duty as Markov history (transitions are
  // keyed on the move that "was just sampled," which is also the move that
  // "will be played next"). Factory functions like make_jaw_worm leave this
  // primed so the agent's observation can read it immediately.
  std::optional<MoveName> last_move;
  int consecutive_count;
};

MoveName select_next_move(Enemy& enemy, std::mt19937& rng);

Enemy make_jaw_worm(std::mt19937& rng);

}  // namespace minispire
