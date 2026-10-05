#include "test_helpers.h"

#include "enemy.h"
#include "turn_loop.h"

namespace minispire::testing {

int card_action(CardId id, int target, int cost_column) {
  if (cost_column < 0) {
    // The printed cost's column, by cost_column's own rule: X-cost and
    // unplayable cards are column 0.
    const CardData& data = CARD_DATABASE.at(id);
    cost_column = (data.cost == kXCost || data.unplayable) ? 0 : data.cost;
  }
  return encode_action(ActionBlock::Combat, static_cast<int>(id), target,
                       cost_column);
}

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
