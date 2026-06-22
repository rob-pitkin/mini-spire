#pragma once

namespace minispire {

enum class StatusEffect {
  Vulnerable,
  Weak,
  Strength,
  Dexterity,
  Frail,   // player: block gained from cards reduced 25% (floored); ticks down
  Ritual,  // enemy: gain Strength = stacks at the start of each of its turns
};

// Number of status effects tracked in the observation. Drives the per-entity
// status block width in the obs layout (combat_env). Keep kObsStatusOrder in
// lockstep — a static_assert enforces the count matches.
inline constexpr int kNumStatusEffects = 6;

// FUTURE (multi-enemy): Target { Character, Enemy } collapses any "the enemy"
// to a single entity, which is unambiguous in v1 with one enemy. Multi-enemy
// fights need richer targeting (Target::AllEnemies for Cleave/Whirlwind,
// Target::EnemyIndex(n) for specific targeting). The current enum will need
// to grow or be replaced by a small variant. See ROB-34 design doc.
struct StatusApplication {
  enum class Target { Character, Enemy };

  StatusEffect effect;
  int amount;
  Target target;
};

}  // namespace minispire
