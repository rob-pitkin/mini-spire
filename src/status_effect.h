#pragma once

namespace minispire {

enum class StatusEffect {
  Vulnerable,
  Weak,
  Strength,
  Dexterity,
};

struct StatusApplication {
  enum class Target { Character, Enemy };

  StatusEffect effect;
  int amount;
  Target target;
};

}  // namespace minispire
