#pragma once

#include <array>
#include <cassert>
#include <optional>
#include <unordered_map>

#include "card.h"
#include "combat_state.h"
#include "enemy.h"
#include "status_effect.h"

// The action queue (effects-architecture Stages 2–3; docs/design/
// effects-architecture.md §4). Card resolution AND enemy turns are translated
// into a flat sequence of POD Actions and drained to completion — hooks
// respond by PUSHING actions, never by mutating state directly, so no live
// reference or open loop ever spans a mutation (the Split-UAF bug class is
// impossible by construction). The queue is drained at every agent decision
// point and is never stored in CombatState — clone() is untouched.
//
// Phase orchestration (block resets, acting-slot snapshots, debuff ticks,
// terminal checks, turn-start draws) is upkeep and stays imperative in
// turn_loop.cc; everything that IS a game effect flows through executors.

namespace minispire {

// Entity addressing: enemy slot index, or kPlayerSlot for the player.
// kNoSlot marks an unused actor/target field.
inline constexpr int kPlayerSlot = -1;
inline constexpr int kNoSlot = -2;

// Helper: look up a stack count in a debuff/power map, returning 0 if absent.
template <typename Effect>
int get_status(const std::unordered_map<Effect, int>& m, Effect e) {
  auto it = m.find(e);
  return it == m.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

enum class ActionKind {
  // Mutations
  DealDamage,   // actor attacks target for `amount` base damage (one hit)
  DealFixedDamage,  // thorns-type damage (Juggernaut, Combust, Fire Breathing,
                    // Flame Barrier): ignores Strength/Weak/Vulnerable, but IS
                    // absorbed by block. Fires no attack-only enemy triggers.
  DamageAllEnemies,   // fan out fixed damage to every living enemy (Combust,
                      // Fire Breathing) — expands at execution so the target
                      // set is current
  DamageRandomEnemy,  // fixed damage to one uniformly-random living enemy
                      // (Juggernaut) — rolled at execution, per trigger
  LoseHp,       // direct player HP loss — bypasses block, can kill (ROB-80)
  GainBlock,    // target gains `amount` block; card_block applies Dex/Frail
  GainEnergy,   // player gains `amount` energy
  DrawCards,    // player draws `amount` cards
  ApplyDebuff,  // apply debuff x amount to target (Artifact-checked)
  ApplyPower,   // apply power x amount to target
  RemovePower,  // erase `power` from target (Lagavulin's Metallicize on wake)
  RewriteIntent,  // set target's last_move = move (interrupt the queued intent)
  Wake,           // set target's is_asleep = false
  ExhaustCard,    // put `card` in the exhaust pile (played or generated)
  DiscardCard,    // put `card` in the discard pile
  EnemyEscape,    // target flees: hp -> 0, NOT a death (no on-death; ROB-74)
  EnemySplit,     // target dies and spawns its split_children at its current
                  // HP (ROB-64) — the reallocation is one flat executor step
  // Bookkeeping
  CardPlayedHook,  // fire Hook::CardPlayed listeners for `card` (Enrage, Rage)
  CheckDeath,      // process deaths recorded this resolution (on-death,
                   // became-last) — replaces the hand-rolled died_slots deferral
  DiscardHand,     // end of turn: ethereal cards exhaust, the rest discard
                   // (routed through the executors so Feel No Pain / Dark
                   // Embrace see the exhausts)
};

// A small, clone-safe tagged value. No closures, no pointers into state —
// actors and targets are slot indices / enums. Extend fields as kinds demand
// (POD only).
struct Action {
  ActionKind kind;
  int actor = kNoSlot;   // damage source: kPlayerSlot or an enemy slot
  int target = kNoSlot;  // recipient: kPlayerSlot or an enemy slot
  int amount = 0;
  CardId card = CardId::Strike;    // for card-carrying kinds
  Debuff debuff = Debuff::None;    // ApplyDebuff payload
  Power power = Power::None;       // ApplyPower / RemovePower payload
  MoveName move = MoveName::None;  // RewriteIntent payload
  bool card_block = false;  // GainBlock from a played card: apply Dex/Frail
};

// Fixed-capacity ring buffer (no steady-state allocation — constraint §3.3).
// push_back ≈ StS addToBottom (the default); push_front ≈ addToTop
// ("immediately next"). Capacity covers the worst realistic card (X-cost
// multi-hit AoE at high energy) with a wide margin; overflow is a bug.
class ActionQueue {
 public:
  bool empty() const { return count_ == 0; }

  void push_back(const Action& a) {
    assert(count_ < kCapacity && "ActionQueue overflow");
    buf_[(head_ + count_) % kCapacity] = a;
    ++count_;
  }

  void push_front(const Action& a) {
    assert(count_ < kCapacity && "ActionQueue overflow");
    head_ = (head_ + kCapacity - 1) % kCapacity;
    buf_[head_] = a;
    ++count_;
  }

  Action pop_front() {
    assert(count_ > 0 && "pop from empty ActionQueue");
    Action a = buf_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return a;
  }

 private:
  static constexpr int kCapacity = 128;
  std::array<Action, kCapacity> buf_;
  int head_ = 0;
  int count_ = 0;
};

// Per-resolution scratch state (local to one drain, like the queue itself —
// never stored in CombatState). Deaths are recorded by the DealDamage executor
// and processed by the CheckDeath action.
struct ResolutionContext {
  std::array<int, kMaxEnemies> died_slots{};
  int died_count = 0;

  void record_death(int slot) {
    assert(died_count < kMaxEnemies);
    died_slots[died_count++] = slot;
  }
};

// ---------------------------------------------------------------------------
// Hooks — the engine-wide event vocabulary (§4.4). One vocabulary consulted
// from inside executors and at phase boundaries. Stage 2 consults the enemy
// events + CardPlayed; the player-power registry (Demon Form, Juggernaut, ...)
// arrives at Stage 4 and consults the rest.
// ---------------------------------------------------------------------------

enum class Hook {
  TurnStartPlayer,    // Demon Form, Brutality, Berserk, Flame Barrier expiry
  TurnEndPlayer,      // Combust, player Metallicize, Rage expiry
  TurnStartEnemy,     // enemy Ritual / Metallicize
  CardPlayed,         // enemy OnPlayerSkill; Rage (Attack played)
  CardExhausted,      // Feel No Pain, Dark Embrace
  BlockGainedPlayer,  // Juggernaut
  HpLostPlayer,       // Rupture — self-inflicted HP loss only, never enemy damage
  CardDrawn,          // Evolve (Status), Fire Breathing (Status/Curse)
  PlayerAttacked,     // Flame Barrier retaliation
  EnemyDamaged,       // Curl Up, Angry — ATTACK damage only
  OnAnyDamage,        // Lagavulin damage-wake — any damage, incl. fixed/thorns
  EnemyHpThreshold,   // Large Slime split interrupt
  EnemyDeath,         // Spore Cloud
  EnemyWake,          // Lagavulin Metallicize removal
  BecameLastEnemy,    // Shield Gremlin attacks once alone
};

// Fire the enemy-at-`slot`'s TriggeredEffects matching `hook`, PUSHING the
// response actions onto `q` (never mutating directly). Firing conditions
// (once/fired latch, requires_asleep, HpAtOrBelow threshold) are evaluated at
// fire time; response magnitudes that read stacks (Enrage) are also resolved
// at fire time. Hooks with no enemy-Trigger analog are no-ops.
void fire_enemy_hooks(CombatState& state, int slot, Hook hook, ActionQueue& q);

// Fire the enemy-at-`slot`'s POWER behaviors for `hook`, pushing response
// actions. The enemy-side static registry (§4.4): a compiler-checked switch
// over the powers map, no state beyond the stacks themselves. Stage 3 handles
// Hook::TurnStartEnemy (Ritual -> Strength, then Metallicize -> block); the
// player-power registry arrives at Stage 4 as its sibling.
void fire_enemy_power_hooks(CombatState& state, int slot, Hook hook,
                            ActionQueue& q);

// Fire the PLAYER's power behaviors for `hook`, pushing response actions
// (Stage 4a). The static registry (§4.4): a switch over character.powers in a
// fixed canonical order (Power enum order), no state beyond the stacks — so
// clone() stays a plain deep copy. `card` carries the CardPlayed / CardDrawn
// payload; `attacker_slot` the PlayerAttacked attacker (Flame Barrier's
// retaliation target). Both are ignored by hooks that don't use them.
void fire_player_power_hooks(CombatState& state, Hook hook, ActionQueue& q,
                             CardId card = CardId::Strike,
                             int attacker_slot = kNoSlot);

// Drain the queue to completion: pop-execute until empty, short-circuiting on
// a terminal outcome. Executors may push more actions. The queue must be empty
// at every agent decision point (§4.2 invariant).
void drain(CombatState& state, ActionQueue& q, ResolutionContext& ctx);

// ---------------------------------------------------------------------------
// Centralized mutators (Stage 1) — the single write-path for every gameplay
// stat mutation, wrapped by the executors above and still called directly by
// the (Stage-3-pending) imperative enemy phase. Construction-time writes and
// phase-boundary resets are upkeep, not gameplay events, and stay direct.
// ---------------------------------------------------------------------------

// Apply damage to a HP/block pair: block absorbs first, then HP (clamped 0).
// LIMITATION: StS tracks "overkill" damage for some effects (Centennial
// Puzzle); clamping loses it. Not used by any current mechanic.
void apply_damage_to_hp_block(int& hp, int& block, int amount);

// Grant block to the player (slot == kPlayerSlot) or an enemy. `amount` is the
// final amount — card-block math (Dex/Frail) applies only to block gained from
// cards and happens in the GainBlock executor.
void gain_block(CombatState& state, int slot, int amount);

// Direct player HP loss (a lose-HP EFFECT — bypasses block, can kill; ROB-80).
void lose_player_hp(CombatState& state, int amount);

void gain_energy(CombatState& state, int amount);
void spend_energy(CombatState& state, int amount);

// Spend ALL energy (X-cost cards); returns the amount spent (= X).
int spend_all_energy(CombatState& state);

// Card pile routing. All gameplay-driven moves into exhaust/discard go through
// these (the future CardExhausted hook point).
void move_to_exhaust(CombatState& state, Card card);
void move_to_discard(CombatState& state, Card card);

// Move all of discard_pile into draw_pile (if needed), shuffle, draw one card
// to the hand. Returns the drawn card, or nullopt if nothing was drawn
// (draw+discard empty, or hand at limit) — the CardDrawn hook needs the id.
std::optional<CardId> draw_one(CombatState& state);

// Apply one debuff/power application to its target ('enemy_target' = decoded
// enemy slot; ignored for Target::Character). Artifact negates a whole debuff
// application; Entangle is SET, not accumulated (non-stacking, ROB-75).
void apply_debuff(CombatState& state, const DebuffApplication& app,
                  int enemy_target);
void apply_power(CombatState& state, const PowerApplication& app,
                 int enemy_target);

}  // namespace minispire
