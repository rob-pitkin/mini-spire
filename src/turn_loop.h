#pragma once

#include <cstdint>
#include <vector>

#include "combat_state.h"

namespace minispire {

constexpr int IRONCLAD_MAX_HP = 80;
constexpr int IRONCLAD_ENERGY_PER_TURN = 3;
constexpr int STARTING_HAND_SIZE = 5;
constexpr int HAND_SIZE_LIMIT = 10;

// Constructs the v1 initial CombatState: seeded RNG, Ironclad starter
// character (80 HP / 3 energy), one Jaw Worm with rolled HP, starter deck
// (5 Strike + 4 Defend + 1 Bash) shuffled into draw pile, opening hand of
// 5 cards drawn, first enemy intent selected.
CombatState start_v1_combat(uint32_t seed);

// Action layout: indices [0, num_card_ids) play a card by CardId
// (first-occurrence rule for duplicates); index num_card_ids ends the turn.
// Mask size is num_card_ids + 1.
std::vector<bool> valid_actions(const CombatState& state);

// Apply a player action. Returns false (silently, no exception) if the
// action is invalid; state is not mutated in that case. Combat must be
// in progress (Outcome::InProgress). Debug builds may log on rejection.
bool apply_action(CombatState& state, int action);

}  // namespace minispire
