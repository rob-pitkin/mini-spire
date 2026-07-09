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
  FatGremlin,
  MadGremlin,
  SneakyGremlin,
  GremlinWizard,
  ShieldGremlin,
  Lagavulin,
  GremlinNob,
};

enum class MoveName {
  None,  // sentinel: "no move" (TriggeredEffect::move default when unused)
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
  // Gremlins (ROB-64)
  Smash,          // Fat Gremlin: 4 damage + 1 Weak
  Puncture,       // Sneaky Gremlin: 9 damage
  Scratch,        // Mad Gremlin: 4 damage
  Charge,         // Gremlin Wizard: no effect (charging)
  UltimateBlast,  // Gremlin Wizard: 25 damage
  // Wizard enriched pseudo-states (first cycle 2 charges, then 3 each cycle):
  Charge1,  // = Charge (first cycle, turn 1)
  Charge2,  // = Charge (first cycle, turn 2)
  Charge3a, Charge3b, Charge3c,  // = Charge (later cycles, 3 turns)
  // Shield Gremlin (ROB-77)
  Protect,       // give a living ally 7 block (self if none)
  ShieldBash,    // 6 damage
  ProtectAlone,  // = Protect; enriched state after becoming the last enemy
  // Lagavulin (ROB-65)
  LagavulinAttack,  // 18 damage
  SiphonSoul,       // -1 Strength and -1 Dexterity to the player
  Sleep,            // asleep: do nothing (holds Metallicize)
  Sleep1, Sleep2, Sleep3,  // = Sleep; the 3 self-wake countdown states
  Stunned,          // awake but does nothing this turn (damage-wake target)
  LagavulinAttack1, LagavulinAttack2,  // = LagavulinAttack; cycle states
  // Gremlin Nob (ROB-65). Bellow (reused) applies Enrage; then Rush/Skull Bash.
  Rush,       // 14 damage
  SkullBash,  // 6 damage + 2 Vulnerable
};

// FUTURE: multi-hit moves (Lagavulin's attacks, Hexaghost) need a `hits`
// field — Strength applies per-hit. v1 moves are single-hit.
struct Move {
  MoveName name;
  int damage;
  int block;
  std::vector<DebuffApplication> applies_debuffs;
  std::vector<PowerApplication> applies_powers;
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
  // The move's `block` goes to a uniform-random living ALLY (a different enemy
  // slot), not the acting enemy (ROB-77). The Shield Gremlin's Protect. If no
  // ally is alive, the block falls back to the acting enemy itself.
  bool blocks_ally = false;
  // The acting enemy wakes when this move resolves (ROB-65): fires the enemy's
  // OnWake triggered effects. Lagavulin's last sleep move (Sleep3) sets this, so
  // the self-wake path clears Metallicize at the END of the 3rd asleep turn
  // (that turn keeps its block; from the next turn on it gets none).
  bool wakes_on_resolve = false;
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

// Generalized triggered effects (ROB-65). One data-driven, clone-safe (POD)
// mechanism unifying every event->effect enemy behavior: the Large Slime's
// split interrupt, the Shield Gremlin's became-alone rewrite, the Mad Gremlin's
// Angry, the Louse's Curl Up, the Fungi Beast's Spore Cloud, plus the elites'
// Lagavulin wake-on-damage, Gremlin Nob Enrage, and Siphon Soul. A dispatcher
// fires the matching effects at each trigger site (damage / hp / alone / card
// play / death).
enum class Trigger {
  OnDamaged,        // the enemy took attack damage
  HpAtOrBelow,      // the enemy's hp <= param (checked after damage)
  BecameLastEnemy,  // this enemy is now the only living one
  OnPlayerSkill,    // the player played a Skill-type card
  OnDeath,          // the enemy died (hp -> 0 from the player, not escape)
  OnWake,           // the enemy woke (Lagavulin: self-wake or damage-wake)
};

enum class TriggeredAction {
  RewriteIntent,         // set last_move = move (interrupt the queued intent)
  GainStrength,          // powers[Strength] += amount
  GainStrengthFromPower, // powers[Strength] += powers[power] (Gremlin Nob Enrage)
  GainBlock,             // current_block += amount (once=true -> Curl Up)
  ApplyPlayerDebuff,     // apply `debuff` x amount to the player
  RemoveSelfPower,       // erase `power` from the acting enemy
  Wake,                  // set is_asleep = false (Lagavulin OnWake)
};

struct TriggeredEffect {
  Trigger trigger;
  TriggeredAction action;
  int param = 0;                   // HpAtOrBelow threshold (else unused)
  int amount = 0;                  // Gain*/ApplyPlayerDebuff magnitude (signed)
  MoveName move = MoveName::None;   // RewriteIntent target (else unused)
  Debuff debuff = Debuff::None;    // ApplyPlayerDebuff effect (else unused)
  Power power = Power::None;        // RemoveSelfPower effect (else unused)
  bool once = false;               // fire at most once, then latch off
  bool fired = false;              // runtime latch for `once`
  // Guard: fire only while the enemy is_asleep. Lagavulin's damage-wake uses
  // this so a first hit AFTER a self-wake can't re-stun it mid-cycle (the `once`
  // latch alone wouldn't distinguish the two).
  bool requires_asleep = false;
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
  std::unordered_map<Debuff, int> debuffs;
  std::unordered_map<Power, int> powers;

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

  // Generalized triggered effects (ROB-65): every event->effect behavior (Curl
  // Up, Angry, Spore Cloud, Split interrupt, became-alone rewrite, wake, Enrage,
  // Siphon Soul). Fired by the TurnLoop dispatcher at each trigger site. Empty
  // for enemies with no reactive behavior. POD + heap-backed vector -> stays
  // clone-safe.
  std::vector<TriggeredEffect> triggered_effects;

  // Split: the children spawned when this enemy's Split move resolves (ROB-64).
  // Data the Split *move* consumes — not itself a trigger. std::vector<Enemy> is
  // heap-backed so the recursive value type compiles and stays copyable.
  std::vector<Enemy> split_children;

  // Asleep (ROB-65): Lagavulin starts asleep and does nothing but hold
  // Metallicize until it wakes (self-wake after 3 turns, or damage-wake). The
  // TriggeredAction::Wake action clears this; the requires_asleep guard reads it.
  bool is_asleep = false;
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
Enemy make_fat_gremlin(std::mt19937& rng);
Enemy make_mad_gremlin(std::mt19937& rng);
Enemy make_sneaky_gremlin(std::mt19937& rng);
Enemy make_gremlin_wizard(std::mt19937& rng);
Enemy make_shield_gremlin(std::mt19937& rng);
Enemy make_lagavulin(std::mt19937& rng);
Enemy make_gremlin_nob(std::mt19937& rng);

}  // namespace minispire
