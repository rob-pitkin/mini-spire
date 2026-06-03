#pragma once

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "card.h"
#include "enemy.h"
#include "status_effect.h"

namespace minispire {

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
  std::unordered_map<StatusEffect, int> status_effects;
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
