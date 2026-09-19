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
  EnemyDeath,         // Spore Cloud; relics: Gremlin Horn
  EnemyWake,          // Lagavulin Metallicize removal
  BecameLastEnemy,    // Shield Gremlin attacks once alone
  // --- Relic hooks (docs/design/relic-effects.md). Appended, never reordered:
  // the enum is switched on in several registries and a renumber is silent. ---
  //
  // Combat start is THREE ordered sub-phases, not one. The real game queues
  // pre-draw relics, then the opening draw, then the rest — so Bag of
  // Preparation's "draw 2" resolves AFTER the opening hand rather than merging
  // into a single 7-card draw, and that changes which cards you get. Collapsing
  // these into one hook would be a parity bug with no local symptom.
  CombatStartPreDraw,   // resolves BEFORE the opening hand (Toolbox)
  CombatStart,          // resolves AFTER it (Bag of Marbles, Bag of Preparation)
  TurnStartPostDraw,    // last of the three (Gambling Chip, Warped Tongs)
  BlockBroken,          // Hand Drill — no site in sts_lightspeed (§2.2)
  ShuffleDrawPile,      // Sundial, The Abacus
  PotionDrunk,          // Toy Ornithopter, Sacred Bark
  CombatEnd,            // Burning Blood, Meat on the Bone — fired on the WON
                        // fight's state, before RunState writes HP back, so the
                        // heal lands in one place rather than two
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

// Fire the player's RELIC behaviors for `hook`, pushing response actions. The
// sibling of fire_player_power_hooks and built the same way: a switch over
// state.relics, no state beyond each HeldRelic's counter, so clone() stays a
// plain deep copy.
//
// The one difference is the iteration order. Powers fire in Power-enum order
// because that is their canonical order; relics fire in ACQUISITION order,
// which is the order of state.relics — the order the player picked them up and
// the order their bar displays. Sorting or grouping this loop would change
// resolution order, so it iterates the vector as it stands.
//
// Effects are PUSHED, never applied here, exactly as the powers registry does.
// See docs/design/relic-effects.md for which relic hangs off which hook.
void fire_relic_hooks(CombatState& state, Hook hook, ActionQueue& q);

// Hook::CardPlayed, which needs a payload the other hooks do not.
//
// Its own entry point rather than a defaulted parameter on the general one.
// A default would make "no card was played" read as "an Attack was played" at
// every non-CardPlayed call site, and a CardType::None sentinel would add a
// value to the enum whose only meaning is "ignore me" — which every switch over
// CardType would then have to handle. The payload belongs to one hook, so it
// belongs in that hook's signature.
//
// The payload is a CardType, not a CardId: no relic keys off a particular card,
// only off whether it was an Attack, Skill or Power. Kunai counts Attacks,
// Letter Opener counts Skills, Bird-Faced Urn watches Powers.
void fire_relic_card_played(CombatState& state, CardType type, ActionQueue& q);

// As above, but drains after EACH relic instead of batching them.
//
// StS applies relics sequentially: a relic's effect has already landed by the
// time the next one reads state. Batching them into one queue makes every relic
// on a hook read the SAME pre-effect state, which is only equivalent when their
// effects commute.
//
// Burning Blood and Meat on the Bone are the case where they do not. Burning
// Blood heals 6, and Meat on the Bone then asks whether HP is at or below half
// — at 35 of 80 the answer is yes before the heal and no after it. Batched, both
// read 35 and both fire, healing 18 instead of 6.
//
// Use this wherever one relic on a hook can read what another just changed.
void fire_relic_hooks_sequentially(CombatState& state, Hook hook);

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
//
// Returns whether the effect actually LANDED. Sadistic Nature needs that
// distinction — StS deals no damage when the target's Artifact eats the debuff
// — and "did it land" is knowable only here, where the charge is spent.
bool apply_debuff(CombatState& state, const DebuffApplication& app,
                  int enemy_target);
bool apply_power(CombatState& state, const PowerApplication& app,
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
