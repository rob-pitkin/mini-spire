#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "combat_state.h"
#include "status_effect.h"

namespace minispire {

constexpr int IRONCLAD_MAX_HP = 80;
constexpr int IRONCLAD_ENERGY_PER_TURN = 3;
constexpr int STARTING_HAND_SIZE = 5;
constexpr int HAND_SIZE_LIMIT = 10;

// Compute the actual damage dealt by an attack with the given base damage,
// given the attacker's status effects (Strength adds, Weak multiplies down)
// and defender's status effects (Vulnerable multiplies up). Float-internal,
// truncated once at the end. Returns max(result, 0).
//
// Exposed so the CLI / observation layer can display the *effective* enemy
// attack damage (Strength-modified, etc.) rather than the raw move.damage.
int compute_attack_damage(
    int base, const std::unordered_map<Power, int>& attacker_powers,
    const std::unordered_map<Debuff, int>& attacker_debuffs,
    const std::unordered_map<Debuff, int>& defender_debuffs);

// Constructs the v1 initial CombatState: seeded RNG, Ironclad starter
// character (80 HP / 3 energy), one Jaw Worm with rolled HP, starter deck
// (5 Strike + 4 Defend + 1 Bash) shuffled into draw pile, opening hand of
// 5 cards drawn, first enemy intent selected.
CombatState start_v1_combat(uint32_t seed);

// Action layout (ROB-60): (card x target) cross-product plus end-turn.
//   action = card_idx * kMaxEnemies + target_idx   for card_idx in [0, num_cards)
//   end_turn = num_card_ids * kMaxEnemies            (last index)
// card_idx is the integer value of a CardId; target_idx is an enemy slot.
// Untargeted cards (Defend) use the canonical target_idx 0; their other slots
// are permanently masked (see valid_actions). Mask size is
// num_card_ids * kMaxEnemies + 1.
struct DecodedAction {
  bool is_end_turn;
  CardId card;     // valid only if !is_end_turn
  int target;      // enemy slot index; valid only if !is_end_turn
};

// Pure arithmetic decode of an action index — no state, so the mask and the
// apply path share one source of truth (decode never disagrees with itself).
DecodedAction decode_action(int action);

// Validity mask over the full action space. An action is legal iff the card is
// in hand AND affordable, AND — if the card targets an enemy — that target slot
// holds a living enemy; if it does not target an enemy (Defend), only the
// canonical target slot 0 is legal. End-turn is always legal while in progress.
std::vector<bool> valid_actions(const CombatState& state);

// Apply a player action. Returns false (silently, no exception) if the
// action is invalid; state is not mutated in that case. Combat must be
// in progress (Outcome::InProgress). Debug builds may log on rejection.
bool apply_action(CombatState& state, int action);

}  // namespace minispire
