#pragma once

namespace minispire {

// StS splits persistent effects into two mechanically-distinct categories
// (ROB-78). Debuffs decrement at end of turn; Powers persist and never
// self-decrement. Keeping them as separate types makes "does this tick" a
// property of the type, not a maintained denylist.

// Debuffs — decrement by 1 at end of the bearer's turn, expire at 0. `None` is a
// sentinel default (not tracked in the obs); keep the real values first so their
// enumerator order matches kObsDebuffOrder.
enum class Debuff {
  Vulnerable,  // takes 50% more attack damage; ticks down
  Weak,        // deals 25% less attack damage; ticks down
  Frail,       // block gained from cards reduced 25% (floored); ticks down
  Entangle,    // player: cannot play attack cards this turn; non-stacking, 1 turn
  None,        // sentinel: "no debuff" (default for unused fields)
};

// Powers — persistent; never self-decrement (removed only by specific effects).
// `None` is a sentinel default (not tracked in the obs).
enum class Power {
  Strength,     // +X attack damage per hit (may be negative)
  Dexterity,    // +X block from cards (may be negative)
  Ritual,       // enemy: gain Strength = stacks at the start of each of its turns
  Metallicize,  // enemy: gain block = stacks at the start of each of its turns
  Enrage,       // enemy: gain Strength = stacks whenever the player plays a Skill
  None,         // sentinel: "no power" (default for unused fields)
};

// Per-entity obs block widths (combat_env). The old single [status] block is now
// [debuffs then powers]. Keep kObsDebuffOrder / kObsPowerOrder in lockstep —
// static_asserts enforce the counts match. (The None sentinels are excluded.)
inline constexpr int kNumDebuffs = 4;
inline constexpr int kNumPowers = 5;
inline constexpr int kNumStatusEffects = kNumDebuffs + kNumPowers;

// FUTURE (multi-enemy): Target { Character, Enemy } collapses any "the enemy"
// to a single entity, which is unambiguous in v1 with one enemy. Multi-enemy
// fights need richer targeting (Target::AllEnemies for Cleave/Whirlwind,
// Target::EnemyIndex(n) for specific targeting). The current enum will need
// to grow or be replaced by a small variant. See ROB-34 design doc.
enum class Target { Character, Enemy };

// A debuff/power applied by a card or move, to the player or an enemy.
struct DebuffApplication {
  Debuff effect;
  int amount;
  Target target;
};

struct PowerApplication {
  Power effect;
  int amount;  // may be negative (e.g. Siphon Soul -1 Strength)
  Target target;
};

}  // namespace minispire
