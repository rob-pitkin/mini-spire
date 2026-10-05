#pragma once

#include <cstdint>

#include "card.h"
#include "combat_state.h"

namespace minispire::testing {

// The action that plays a copy of `id` at enemy slot `target`. Actions name a
// copy by (CardId, cost column) (v2-spec.md §6.5); by default this names the
// PRINTED cost's column — a copy nothing has discounted, which is what almost
// every test plays. A test about a discounted copy (Madness, Corruption, a
// Snecko roll) passes the column it means. One definition, through
// encode_action, so no test re-derives the layout.
int card_action(CardId id, int target = 0, int cost_column = -1);

// Returns a deterministically-constructed CombatState for unit tests.
// Character: HP 80/80, energy 3/3, no statuses, no block.
// Enemies: one Jaw Worm constructed via make_jaw_worm(rng).
// Piles: hand/draw/discard/exhaust all empty.
// Turn 1, character_turn=true, outcome=InProgress.
// RNG seeded with `seed`.
//
// Tests mutate fields from this minimal baseline rather than calling
// start_v1_combat (which would make them circular).
CombatState make_minimal_state(uint32_t seed = 0);

}  // namespace minispire::testing
