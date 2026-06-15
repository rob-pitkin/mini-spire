#pragma once

#include <cstdint>

#include "combat_state.h"

namespace minispire::testing {

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
