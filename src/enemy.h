#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "status_effect.h"

namespace minispire {

enum class MoveName {
  Chomp,
  Thrash,
  Bellow,
};

struct Move {
  MoveName name;
  int damage;
  int block;
};

struct MoveOption {
  Move move;
  float probability;
  int max_consecutive;
};

struct Enemy {
  std::string name;
  int hp;
  int max_hp;
  int current_block;
  std::unordered_map<StatusEffect, int> status_effects;
  std::vector<MoveOption> moveset;
  std::optional<int> first_turn_move;
  int last_move_index;
  int consecutive_count;
};

}  // namespace minispire
