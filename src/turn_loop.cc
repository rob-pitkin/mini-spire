#include "turn_loop.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "card.h"
#include "enemy.h"
#include "status_effect.h"

namespace minispire {

namespace {

// Helper: look up a stack count in a status map, returning 0 if absent.
int get_status(const std::unordered_map<StatusEffect, int>& m, StatusEffect e) {
  auto it = m.find(e);
  return it == m.end() ? 0 : it->second;
}

// Apply damage to a HP/block pair: block absorbs first, then HP. HP is
// clamped at 0 so we don't end up with negative HP in observations.
//
// LIMITATION: STS technically tracks "overkill" damage for some end-of-fight
// effects (e.g. Centennial Puzzle). Clamping at 0 loses that information.
// Not used by any v1 mechanic.
void apply_damage_to_hp_block(int& hp, int& block, int amount) {
  int blocked = std::min(amount, block);
  block -= blocked;
  hp -= (amount - blocked);
  if (hp < 0) hp = 0;
}

// LIMITATION (multi-enemy): Target::Enemy currently means "enemies[0]". With
// multiple enemies this should know the source's index and which enemies are
// targeted (Target::AllEnemies, Target::EnemyIndex(n), etc.). See ROB-34
// design doc note on multi-enemy target semantics.
void apply_status(CombatState& state, const StatusApplication& app) {
  auto& target_status = (app.target == StatusApplication::Target::Character)
                            ? state.character.status_effects
                            : state.enemies[0].status_effects;
  target_status[app.effect] += app.amount;
}

// Decrement Vulnerable/Weak by 1; remove if at 0. Strength/Dexterity persist.
void tick_status_effects(std::unordered_map<StatusEffect, int>& effects) {
  for (auto it = effects.begin(); it != effects.end();) {
    bool decrements = (it->first == StatusEffect::Vulnerable ||
                       it->first == StatusEffect::Weak);
    if (decrements) {
      it->second -= 1;
      if (it->second <= 0) {
        it = effects.erase(it);
        continue;
      }
    }
    ++it;
  }
}

// Move all of discard_pile into draw_pile (if needed), shuffle, draw one
// to current_hand. Silent no-op if draw+discard empty or hand at limit.
void draw_one(CombatState& state) {
  if (state.draw_pile.empty()) {
    if (state.discard_pile.empty()) return;
    state.draw_pile = std::move(state.discard_pile);
    state.discard_pile.clear();
    std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);
  }
  if (static_cast<int>(state.current_hand.size()) >= HAND_SIZE_LIMIT) return;
  state.current_hand.push_back(state.draw_pile.back());
  state.draw_pile.pop_back();
}

void check_enemy_terminal(CombatState& state) {
  for (const auto& e : state.enemies) {
    if (e.hp > 0) return;
  }
  state.outcome = Outcome::Won;
}

void check_character_terminal(CombatState& state) {
  if (state.character.hp <= 0) state.outcome = Outcome::Lost;
}

// Returns hand index of first card with this id, or -1 if absent.
int find_first_in_hand(const std::vector<Card>& hand, CardId id) {
  for (std::size_t i = 0; i < hand.size(); ++i) {
    if (hand[i].card_id == id) return static_cast<int>(i);
  }
  return -1;
}

void handle_play_card(CombatState& state, CardId card_id) {
  const CardData& data = CARD_DATABASE.at(card_id);

  // 1. Pay energy
  state.character.energy -= data.cost;

  // 2. Apply damage to the (single) enemy.
  // LIMITATION (multi-enemy): v1 has one enemy; cards that target all enemies
  // (Cleave, Whirlwind) will need a target field on CardData and iteration here.
  Enemy& enemy = state.enemies[0];
  if (data.damage > 0) {
    int dmg = compute_attack_damage(data.damage, state.character.status_effects,
                                    enemy.status_effects);
    apply_damage_to_hp_block(enemy.hp, enemy.current_block, dmg);
  }

  // 3. Apply block (with Dexterity)
  if (data.block > 0) {
    int block_gained =
        data.block + get_status(state.character.status_effects,
                                StatusEffect::Dexterity);
    state.character.current_block += block_gained;
  }

  // 4. Apply status effects
  for (const auto& app : data.applies) {
    apply_status(state, app);
  }

  // 5. Move card from hand to discard or exhaust
  int idx = find_first_in_hand(state.current_hand, card_id);
  assert(idx >= 0 && "mask should have rejected this action");
  Card played = state.current_hand[idx];
  state.current_hand.erase(state.current_hand.begin() + idx);
  if (data.exhaust) {
    state.exhaust_pile.push_back(played);
  } else {
    state.discard_pile.push_back(played);
  }

  // 6. Terminal checks
  check_enemy_terminal(state);
  if (state.outcome != Outcome::InProgress) return;
  check_character_terminal(state);  // currently unreachable in v1
}

// LIMITATION (multi-enemy): this assumes a single attacking enemy at
// state.enemies[0]. With multiple enemies, each enemy's intent fires in turn
// order and references its own status_effects.
void apply_move_to_state(CombatState& state, const Move& move) {
  Enemy& enemy = state.enemies[0];

  if (move.damage > 0) {
    int dmg = compute_attack_damage(move.damage, enemy.status_effects,
                                    state.character.status_effects);
    apply_damage_to_hp_block(state.character.hp, state.character.current_block,
                             dmg);
  }
  if (move.block > 0) {
    enemy.current_block += move.block;
  }
  // STS limitation: STS cards/moves with multi-hit attacks (Twin Strike,
  // Pommel Strike) deal Strength bonus per-hit. Our Move model is one hit
  // per cast; multi-hit will need a `hits` field on Move.
  for (const auto& app : move.applies) {
    apply_status(state, app);
  }
}

// LIMITATION (multi-enemy): single enemy assumed. Multi-enemy needs to iterate
// state.enemies in turn order and let each act sequentially, with intermediate
// terminal checks.
void handle_end_turn(CombatState& state) {
  Enemy& enemy = state.enemies[0];

  // 1. End of player turn
  // 1a. Discard hand
  for (const Card& c : state.current_hand) {
    state.discard_pile.push_back(c);
  }
  state.current_hand.clear();
  // 1b. Tick character statuses
  tick_status_effects(state.character.status_effects);
  // 1c. Discard leftover energy
  state.character.energy = 0;

  // 2. Enemy turn
  state.character_turn = false;
  // 2a. Reset enemy block
  enemy.current_block = 0;
  // 2b. Apply the intent that was set during start_v1_combat (turn 1) or the
  // prior enemy turn (step 2e). enemy.last_move always stores the upcoming
  // intent, so the agent's observation sees the next move between turns.
  assert(enemy.last_move.has_value() &&
         "enemy.last_move must be primed by start_v1_combat or prior turn");
  MoveName current_intent = *enemy.last_move;
  apply_move_to_state(state, enemy.moves.at(current_intent));

  // 2c. Terminal check
  check_character_terminal(state);
  if (state.outcome != Outcome::InProgress) return;

  // 2d. Tick enemy statuses
  tick_status_effects(enemy.status_effects);

  // 2e. Advance Markov chain to set next intent (visible on next obs)
  select_next_move(enemy, state.rng);

  // 3. Start new player turn
  state.character.current_block = 0;
  state.character.energy = state.character.energy_per_turn;
  for (int i = 0; i < STARTING_HAND_SIZE; ++i) {
    draw_one(state);
  }
  state.turn_number += 1;
  state.character_turn = true;
}

}  // namespace

int compute_attack_damage(int base,
                          const std::unordered_map<StatusEffect, int>& attacker,
                          const std::unordered_map<StatusEffect, int>& defender) {
  // Float-internal, truncated once at the end (per the STS wiki rounding rule).
  float d = static_cast<float>(base) +
            static_cast<float>(get_status(attacker, StatusEffect::Strength));
  if (get_status(attacker, StatusEffect::Weak) > 0) d *= 0.75f;
  if (get_status(defender, StatusEffect::Vulnerable) > 0) d *= 1.5f;
  int result = static_cast<int>(std::floor(d));
  return result < 0 ? 0 : result;
}

CombatState start_v1_combat(uint32_t seed) {
  CombatState state;
  state.seed = seed;
  state.rng = std::mt19937(seed);

  state.character.max_hp = IRONCLAD_MAX_HP;
  state.character.hp = IRONCLAD_MAX_HP;
  state.character.energy_per_turn = IRONCLAD_ENERGY_PER_TURN;
  state.character.energy = IRONCLAD_ENERGY_PER_TURN;
  state.character.current_block = 0;

  state.enemies.push_back(make_jaw_worm(state.rng));

  // Build the starter deck and shuffle into draw pile.
  std::vector<Card> deck;
  for (int i = 0; i < 5; ++i) deck.push_back(Card{CardId::Strike});
  for (int i = 0; i < 4; ++i) deck.push_back(Card{CardId::Defend});
  deck.push_back(Card{CardId::Bash});
  state.draw_pile = std::move(deck);
  std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);

  state.turn_number = 1;
  state.character_turn = true;
  state.outcome = Outcome::InProgress;

  // (Enemy intent is already primed by make_jaw_worm.)

  // Draw the opening hand.
  for (int i = 0; i < STARTING_HAND_SIZE; ++i) {
    draw_one(state);
  }

  return state;
}

std::vector<bool> valid_actions(const CombatState& state) {
  // Action layout: [0, num_card_ids) play card by id; last index = end turn.
  // Use the size of CARD_DATABASE — every CardId has an entry.
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  std::vector<bool> mask(num_card_ids + 1, false);

  if (state.outcome != Outcome::InProgress) {
    return mask;  // all false
  }

  // Per-card-id validity. Iterates CARD_DATABASE — invariant: every CardId
  // has an entry, so any CardId without one would leave mask[idx] = false
  // and the action would be rejected.
  for (const auto& [card_id, data] : CARD_DATABASE) {
    int idx = static_cast<int>(card_id);
    if (idx < 0 || idx >= num_card_ids) continue;
    bool in_hand = find_first_in_hand(state.current_hand, card_id) >= 0;
    bool affordable = state.character.energy >= data.cost;
    mask[idx] = in_hand && affordable;
  }

  // End turn is always legal while in progress.
  mask[num_card_ids] = true;
  return mask;
}

bool apply_action(CombatState& state, int action) {
  if (state.outcome != Outcome::InProgress) return false;

  auto mask = valid_actions(state);
  if (action < 0 || action >= static_cast<int>(mask.size()) || !mask[action]) {
    return false;
  }

  const int end_turn_idx = static_cast<int>(mask.size()) - 1;
  if (action == end_turn_idx) {
    handle_end_turn(state);
  } else {
    handle_play_card(state, static_cast<CardId>(action));
  }
  return true;
}

}  // namespace minispire
