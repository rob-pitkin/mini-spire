#include "test_helpers.h"

#include "enemy.h"

namespace minispire::testing {

CombatState make_minimal_state(uint32_t seed) {
  CombatState state;
  state.seed = seed;
  state.rng = std::mt19937(seed);

  state.character.hp = 80;
  state.character.max_hp = 80;
  state.character.energy = 3;
  state.character.energy_per_turn = 3;
  state.character.current_block = 0;

  state.enemies.push_back(make_jaw_worm(state.rng));
  // make_jaw_worm primes the intent — last_move is already set.

  state.turn_number = 1;
  state.character_turn = true;
  state.outcome = Outcome::InProgress;

  return state;
}

}  // namespace minispire::testing
