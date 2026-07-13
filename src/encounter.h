#pragma once

#include <random>
#include <vector>

#include "enemy.h"

namespace minispire {

// Act 1 encounter pools (ROB-66). StS draws normal fights from a Weak pool (the
// first ~3 combats) then a Strong pool; elites are a separate pool. The future
// map (M8) picks the pool per node. Weights are Ascension-0 wiki values.
enum class EncounterPool {
  Weak,
  Strong,
  Elite,
};

// Sample one encounter from `pool`: a weighted pick of an encounter, whose
// generator then resolves its own internal randomness (e.g. a Louse's red/green
// coin flip, the gremlin-gang draw) using `rng`. Returns the enemy vector for a
// fresh fight (each enemy primed with its turn-1 intent).
//
// DEFERRED (ROB-66 / M4-M5): the StS "no immediate repeat" rule (an encounter
// won't recur consecutively) is a sequential-RUN concern needing cross-fight
// history. Single-fight now -> plain weighted sampling per reset.
std::vector<Enemy> sample_encounter(EncounterPool pool, std::mt19937& rng);

}  // namespace minispire
