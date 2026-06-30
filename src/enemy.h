#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "card.h"
#include "status_effect.h"

namespace minispire {

enum class EnemyKind {
  JawWorm,
  Cultist,
  RedLouse,
  GreenLouse,
  AcidSlimeS,
  AcidSlimeM,
  SpikeSlimeS,
  SpikeSlimeM,
  FungiBeast,
};

enum class MoveName {
  Chomp,
  Thrash,
  Bellow,
  // Cultist
  Incantation,  // gain Ritual 3 (turn 1 only)
  DarkStrike,   // deal 6 damage
  // Louse
  Bite,     // deal D damage (D rolled 5-7 per fight)
  Grow,     // Red Louse: gain 3 Strength
  SpitWeb,  // Green Louse: apply 2 Weak to the player
  // Slimes
  Tackle,         // deal damage (amount varies by slime)
  Lick,           // apply 1 Weak (Acid) or 1 Frail (Spike) to the player
  CorrosiveSpit,  // Acid M: deal 7, add 1 Slimed to discard
  FlameTackle,    // Spike M: deal 8, add 1 Slimed to discard
};

// FUTURE: multi-hit moves (Lagavulin's attacks, Hexaghost) need a `hits`
// field — Strength applies per-hit. v1 moves are single-hit.
struct Move {
  MoveName name;
  int damage;
  int block;
  std::vector<StatusApplication> applies;
  // Status cards this move adds to the player's discard pile (ROB-72), e.g. a
  // slime's spit adding Slimed. Applied at end of the acting enemy's turn.
  // General (not Slimed-specific) so Dazed/Wound/Burn reuse it.
  std::vector<CardId> adds_to_discard;
  // The acting enemy flees when this move resolves (ROB-74), e.g. the Looter's
  // Escape. Modeled as the enemy setting its own hp to 0 — it leaves the fight
  // (no longer targetable/acting; its slot frees). Escape counts as a win if it
  // was the last enemy; it does NOT trigger on_death hooks (escape != death).
  bool escapes = false;
};

struct MoveTransition {
  MoveName next_move;
  float probability;
};

struct TransitionKey {
  MoveName last;
  int consecutive;

  bool operator==(const TransitionKey& other) const {
    return last == other.last && consecutive == other.consecutive;
  }
};

// Enemy effect hooks (ROB-62). Plain enum tags + data fields on Enemy keep the
// enemy a trivially-copyable value type (required for CombatState::clone() /
// MCTS — no virtual methods, no std::function). TurnLoop dispatches on the tag.
enum class OnDeathEffect {
  None,
  Split,       // spawn split_children into free enemy slots (e.g. Large Slime)
  SporeCloud,  // apply spore_vulnerable Vulnerable to the player (Fungi Beast)
};

enum class OnDamagedEffect {
  None,
  CurlUp,  // on the first damage taken, gain curl_block block (Louse)
};

}  // namespace minispire

namespace std {
template <>
struct hash<minispire::TransitionKey> {
  std::size_t operator()(const minispire::TransitionKey& k) const noexcept {
    std::size_t h1 = std::hash<int>{}(static_cast<int>(k.last));
    std::size_t h2 = std::hash<int>{}(k.consecutive);
    return h1 ^ (h2 << 1);
  }
};
}  // namespace std

namespace minispire {

struct Enemy {
  EnemyKind kind;
  int hp;
  int max_hp;
  int current_block;
  std::unordered_map<StatusEffect, int> status_effects;

  std::unordered_map<MoveName, Move> moves;
  std::optional<MoveName> first_turn_move;
  std::unordered_map<TransitionKey, std::vector<MoveTransition>> transitions;

  // last_move stores the upcoming intent — the move that will fire on the
  // next enemy turn. It does double duty as Markov history (transitions are
  // keyed on the move that "was just sampled," which is also the move that
  // "will be played next"). Factory functions like make_jaw_worm leave this
  // primed so the agent's observation can read it immediately.
  std::optional<MoveName> last_move;
  int consecutive_count;

  // --- Effect hooks (ROB-62). All default to inert. ---
  OnDeathEffect on_death = OnDeathEffect::None;
  OnDamagedEffect on_damaged = OnDamagedEffect::None;

  // Split: the children spawned when this enemy dies. The dying enemy carries
  // its own children as data (M3 sets them); std::vector<Enemy> is heap-backed
  // so the recursive value type compiles and stays copyable for clone().
  std::vector<Enemy> split_children;

  // CurlUp: block-once latch. curl_available flips false after the first hit.
  bool curl_available = false;
  int curl_block = 0;

  // SporeCloud: Vulnerable stacks applied to the player on death.
  int spore_vulnerable = 0;
};

MoveName select_next_move(Enemy& enemy, std::mt19937& rng);

Enemy make_jaw_worm(std::mt19937& rng);
Enemy make_cultist(std::mt19937& rng);
Enemy make_red_louse(std::mt19937& rng);
Enemy make_green_louse(std::mt19937& rng);
Enemy make_acid_slime_s(std::mt19937& rng);
Enemy make_acid_slime_m(std::mt19937& rng);
Enemy make_spike_slime_s(std::mt19937& rng);
Enemy make_spike_slime_m(std::mt19937& rng);
Enemy make_fungi_beast(std::mt19937& rng);

}  // namespace minispire
