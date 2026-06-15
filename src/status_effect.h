#pragma once

namespace minispire {

enum class StatusEffect {
  Vulnerable,
  Weak,
  Strength,
  Dexterity,
};

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
