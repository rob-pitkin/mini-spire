#pragma once

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "card.h"
#include "enemy.h"
#include "status_effect.h"

namespace minispire {

// Maximum number of enemy slots in a fight. Fixed so the observation and action
// space are fixed-size (ROB-59 / ROB-60). N = 4 covers the largest Act 1
// encounter including post-split (e.g. two Large Slimes each splitting into two,
// or three Sentries). The `enemies` vector is sized to this; dead enemies keep
// their slot (stable indices) and a slot is reusable only once its occupant is
// dead. Invariant: the count of *living* enemies never exceeds kMaxEnemies.
inline constexpr int kMaxEnemies = 4;

enum class Outcome {
  InProgress,
  Won,
  Lost,
};

struct Character {
  int hp;
  int max_hp;
  int energy;
  int energy_per_turn;
  int current_block;
  std::unordered_map<Debuff, int> debuffs;
  std::unordered_map<Power, int> powers;
};

struct CombatState {
  Character character;
  std::vector<Enemy> enemies;
  std::vector<Card> current_hand;
  std::vector<Card> discard_pile;
  std::vector<Card> draw_pile;
  std::vector<Card> exhaust_pile;
  int turn_number;
  bool character_turn;
  Outcome outcome;
  std::mt19937 rng;
  uint32_t seed;

  CombatState clone() const;
};

}  // namespace minispire
