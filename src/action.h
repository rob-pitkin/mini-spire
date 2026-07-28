#pragma once

#include <array>
#include <cassert>
#include <optional>
#include <unordered_map>

#include "action_types.h"  // Action, ActionQueue, kPlayerSlot, get_status
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


// Per-resolution scratch state (local to one drain, like the queue itself —
// never stored in CombatState). Deaths are recorded by the DealDamage executor
// and processed by the CheckDeath action.
struct ResolutionContext {
  std::array<int, kMaxEnemies> died_slots{};
  int died_count = 0;

  // Damage that actually reached enemy HP this resolution (Reaper heals the
  // UNBLOCKED total, summed across its AoE targets).
  int unblocked_damage_dealt = 0;

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
  TurnStartEnemy,     // enemy Metallicize (see ordering-notes §24)
  TurnEndEnemy,       // enemy Ritual — StS grants it at the END of the bearer's
                      // turn, so the intent the player sees already reflects it
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

// Heal the player, capped at max HP (Reaper). A heal on a dead player does
// nothing — the terminal check has already fired.
void heal_player(CombatState& state, int amount);

// Raise max HP, and current HP with it (Feed). "Permanent" in StS means
// run-scoped; within a single combat it simply persists on the state.
void gain_max_hp(CombatState& state, int amount);

// Spend ALL energy (X-cost cards); returns the amount spent (= X).
int spend_all_energy(CombatState& state);

// Card pile routing. All gameplay-driven moves into exhaust/discard go through
// these (the future CardExhausted hook point).
void move_to_exhaust(CombatState& state, Card card);
void move_to_discard(CombatState& state, Card card);

// Add a card to the hand, overflowing to the DISCARD pile if the hand is full
// (StS: "if a copy surpasses the hand size limit, it goes to the discard
// pile"). Used by Dual Wield, Exhume, and Infernal Blade.
void add_card_to_hand(CombatState& state, const Card& card);

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

// ---------------------------------------------------------------------------
// Mid-card choices (Stage 4c; docs/design/decision-points.md)
// ---------------------------------------------------------------------------

// Build the candidate list for `kind`: applies the per-card filter, dedupes to
// distinct card types, and sorts ascending by CardId (the canonical ordering,
// which is public interface because slot indices are actions). Returns the
// populated PendingChoice; `num_options == 0` means no legal option exists.
PendingChoice build_choice(const CombatState& state, ChoiceKind kind,
                           CardId source_card);

// Answer a pending choice and resume the suspended drain. `option_index` is an
// index into pending_choice.options; pass kDeclineChoice to decline an optional
// choice. Returns false (leaving the state untouched) if there is no active
// choice or the index is illegal — so callers can't corrupt a paused state.
//
// Public because all three consumers use it: the RL step path, the TUI's choice
// screen, and tests.
bool resolve_choice(CombatState& state, int option_index);

// Sentinel for "decline" (Warcry-style optional choices, v2's skip).
inline constexpr int kDeclineChoice = -1;

}  // namespace minispire
