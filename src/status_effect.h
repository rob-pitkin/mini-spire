#pragma once

namespace minispire {

enum class StatusEffect {
  Vulnerable,
  Weak,
  Strength,
  Dexterity,
};

struct StatusApplication {
  StatusEffect effect;
  int amount;
};

}  // namespace minispire
