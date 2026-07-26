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
// space are fixed-size (ROB-59 / ROB-60). N = 5 covers the largest Act 1
// encounter: "Lots of Slimes" = 3 Spike-S + 2 Acid-S (ROB-66). The `enemies`
// vector is sized to this; dead enemies keep their slot (stable indices) and a
// slot is reusable only once its occupant is dead. Invariant: the count of
// *living* enemies never exceeds kMaxEnemies.
inline constexpr int kMaxEnemies = 5;

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
  // Combust's second counter (Stage 4a): HP lost at end of turn = casts, while
  // Power::Combust stacks = accumulated damage (5/7 per cast — one stack count
  // can't hold both once upgrades mix). Bumped by the ApplyPower executor.
  int combust_casts = 0;
  // --- Query-layer counters (Stage 4b). Hidden from the obs on purpose: they
  // are read through query.cc, not shown as power icons in StS. ---
  // Blood for Blood: cost drops 1 per HP-loss EVENT this combat, from ANY
  // source including enemy attacks (unlike Rupture, which is self-inflicted
  // only). Combat-scoped — never reset per turn.
  int hp_loss_events = 0;
  // Battle Trance: no further draws this turn. Cleared at turn start.
  bool no_draw_this_turn = false;
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
