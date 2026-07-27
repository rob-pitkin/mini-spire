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
//
// Layout contract: the first kNumEnemyPowers values are the enemy-relevant
// powers (the enemy obs block encodes exactly these, in this order); the
// player obs block encodes ALL real values in enum order (ROB Stage 4a
// decision: per-entity order lists). Append new player powers before None.
enum class Power {
  Strength,     // +X attack damage per hit (may be negative)
  Dexterity,    // +X block from cards (may be negative)
  Ritual,       // enemy: gain Strength = stacks at the start of each of its turns
  Metallicize,  // gain block = stacks (enemy: start of its turn; player: end of turn)
  Enrage,       // enemy: gain Strength = stacks whenever the player plays a Skill
  Artifact,     // enemy: negates the next `stacks` debuff applications, then decrements
  // --- Player powers (Tier C, effects-architecture Stage 4a). Behavior lives
  // in the static registry fire_player_power_hooks (action.cc). ---
  DemonForm,     // turn start: gain `stacks` Strength
  Combust,       // turn end: lose 1 HP per cast (Character::combust_casts) and
                 // deal `stacks` fixed damage to ALL enemies
  FeelNoPain,    // whenever a card is exhausted: gain `stacks` block
  DarkEmbrace,   // whenever a card is exhausted: draw `stacks` cards
  Evolve,        // whenever a Status card is drawn: draw `stacks` cards
  FireBreathing, // whenever a Status/Curse is drawn: `stacks` fixed dmg to all
  Rupture,       // whenever HP is lost from a card/power: gain `stacks` Strength
  Juggernaut,    // whenever block is gained: `stacks` fixed dmg to a random enemy
  Rage,          // this turn, whenever an Attack is played: gain `stacks` block;
                 // removed at end of turn
  FlameBarrier,  // this turn, whenever attacked: `stacks` fixed dmg back;
                 // removed at the start of the next turn
  Brutality,     // turn start: lose `stacks` HP, draw `stacks` cards
  Berserk,       // turn start: gain `stacks` energy
  // --- Query-layer powers (Stage 4b): consulted, never fired. Behavior lives
  // in query.cc, not in a hook registry. ---
  Corruption,    // Skills cost 0 and exhaust when played
  Barricade,     // block is not removed at the start of your turn
  DoubleTap,     // this turn, the next `stacks` Attacks are played twice
  None,          // sentinel: "no power" (default for unused fields)
};

// Per-entity obs block widths (combat_env). Each block is [debuffs then
// powers]; the player block lists every power, the enemy block only the
// enemy-relevant prefix (Stage 4a). Keep kObsDebuffOrder /
// kObsPlayerPowerOrder / kObsEnemyPowerOrder in lockstep — static_asserts
// enforce the counts match. (The None sentinels are excluded.)
inline constexpr int kNumDebuffs = 4;
inline constexpr int kNumEnemyPowers = 6;
inline constexpr int kNumPlayerPowers = 21;

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
