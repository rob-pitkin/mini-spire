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
  BlueSlaver,
  RedSlaver,
  Looter,
  Mugger,
  AcidSlimeL,
  SpikeSlimeL,
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
  // Slaver
  Stab,    // deal damage (12 Blue / 13 Red)
  Rake,    // Blue Slaver: deal 7, apply 1 Weak
  Scrape,  // Red Slaver: deal 8, apply 1 Weak (post-Entangle Markov state)
  Entangle,  // Red Slaver: apply Entangle to the player
  // --- Enriched pseudo-move-states (ROB-76): encode turn/phase/cycle position
  // as distinct names sharing another move's data. select_next_move keys on
  // these; apply_move_to_state uses the (identical) Move data mapped to them.
  // Red Slaver pre-Entangle cycle:
  OpenerStab,    // = Stab (turn 1)
  CycleScrape1,  // = Scrape (cycle pos 1)
  CycleScrape2,  // = Scrape (cycle pos 2)
  CycleStab,     // = Stab   (cycle pos 3)
  // Looter / Mugger script:
  Mug,        // deal 10
  Mug1,       // = Mug (turn 1)
  Mug2,       // = Mug (turn 2)
  Lunge,      // deal 12 (Looter) / 16 (Mugger)
  SmokeBomb,  // gain 6 (Looter) / 11 (Mugger) block
  Escape,     // leave the fight (ROB-74)
  Split,      // Large Slime: die and spawn 2 medium children at current HP (ROB-64)
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
  // The acting enemy splits when this move resolves (ROB-64): it dies and spawns
  // its split_children into free slots, each set to the parent's CURRENT HP
  // (inherited at split time). The Large Slime's Split move. Distinct from the
  // ROB-62 on_death Split hook — this is a chosen move, not a passive trigger.
  bool splits = false;
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

  // HP-threshold intent interrupt (ROB-64): when the enemy is damaged to
  // hp <= split_threshold_hp (and still alive), its queued intent is overwritten
  // to split_move immediately (obs-visible), regardless of what was planned.
  // The Large Slime's "split at <=50% HP". 0 = no threshold (inert). This is the
  // first, minimal instance of a general HP-threshold intent override; bosses
  // (Guardian, Slime Boss) will generalize it later.
  int split_threshold_hp = 0;
  MoveName split_move = MoveName::Split;  // the move forced at the threshold
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
Enemy make_blue_slaver(std::mt19937& rng);
Enemy make_red_slaver(std::mt19937& rng);
Enemy make_looter(std::mt19937& rng);
Enemy make_mugger(std::mt19937& rng);
Enemy make_acid_slime_l(std::mt19937& rng);
Enemy make_spike_slime_l(std::mt19937& rng);

}  // namespace minispire
