#include "turn_loop.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

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

// Apply a status to either the character or a specific enemy slot. `enemy_target`
// is the decoded target slot (ROB-60) for Target::Enemy applications; ignored for
// Target::Character. AoE status (apply to all enemies) is not yet modeled —
// revisit when AoE cards land.
void apply_status(CombatState& state, const StatusApplication& app,
                  int enemy_target) {
  if (app.target == StatusApplication::Target::Character) {
    state.character.status_effects[app.effect] += app.amount;
    return;
  }
  if (enemy_target >= 0 && enemy_target < static_cast<int>(state.enemies.size())) {
    state.enemies[enemy_target].status_effects[app.effect] += app.amount;
  }
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

// Count living enemies (hp > 0).
int count_living(const CombatState& state) {
  int n = 0;
  for (const Enemy& e : state.enemies) {
    if (e.hp > 0) n++;
  }
  return n;
}

// First slot not holding a living enemy (dead corpse OR empty), or -1 if all
// slots are occupied by the living. A split overwrites a corpse (ROB-61 rule A).
int find_free_slot(const CombatState& state) {
  for (int i = 0; i < static_cast<int>(state.enemies.size()); ++i) {
    if (state.enemies[i].hp <= 0) return i;
  }
  // Slots beyond the current vector size are also "free" up to kMaxEnemies.
  if (static_cast<int>(state.enemies.size()) < kMaxEnemies) {
    return static_cast<int>(state.enemies.size());
  }
  return -1;
}

// Place one child into a free slot (overwriting a corpse or growing the vector
// up to kMaxEnemies). Throws if no slot is free (the living<=N invariant should
// make this impossible — a split is only legal with living <= N-1 beforehand).
void place_child(CombatState& state, const Enemy& child) {
  int slot = find_free_slot(state);
  if (slot < 0) {
    throw std::runtime_error(
        "split would exceed kMaxEnemies living enemies (mis-specified "
        "encounter: split-capable enemies must start with living <= N-1)");
  }
  if (slot < static_cast<int>(state.enemies.size())) {
    state.enemies[slot] = child;  // overwrite a corpse
  } else {
    state.enemies.push_back(child);
  }
}

// on_death hook: fires when an enemy reaches hp <= 0. Deferred to after the
// player's card fully resolves (Split mutates state.enemies). `slot` is the
// dying enemy's slot.
void fire_on_death(CombatState& state, int slot) {
  Enemy& dying = state.enemies[slot];
  switch (dying.on_death) {
    case OnDeathEffect::None:
      break;
    case OnDeathEffect::SporeCloud:
      state.character.status_effects[StatusEffect::Vulnerable] +=
          dying.spore_vulnerable;
      break;
    case OnDeathEffect::Split: {
      // Invariant guard: the children must fit. After this corpse is counted as
      // dead, living must end up <= kMaxEnemies. Copy children first (placing
      // may overwrite this corpse's slot and invalidate `dying`).
      std::vector<Enemy> children = dying.split_children;
      for (const Enemy& child : children) {
        place_child(state, child);
      }
      break;
    }
  }
}

// on_damaged hook: fires when an enemy actually loses HP. CurlUp grants block
// once (the latch flips off).
void fire_on_damaged(Enemy& enemy) {
  if (enemy.on_damaged == OnDamagedEffect::CurlUp && enemy.curl_available) {
    enemy.current_block += enemy.curl_block;
    enemy.curl_available = false;
  }
}

void handle_play_card(CombatState& state, CardId card_id, int target) {
  const CardData& data = CARD_DATABASE.at(card_id);

  // 1. Pay energy
  state.character.energy -= data.cost;

  // Track an enemy that died from this card's damage so its on_death hook can
  // fire *after* the card fully resolves (deferred — Split mutates the vector).
  int died_slot = -1;

  // 2. Apply damage to the targeted enemy (ROB-60: target is the decoded enemy
  // slot). The mask guarantees `target` is a living enemy for damage/enemy-
  // status cards; guard defensively anyway.
  // LIMITATION (multi-enemy): cards that hit *all* enemies (Cleave, Whirlwind)
  // will iterate all living enemies instead of a single target — revisit when
  // AoE cards land (ROB-60 FUTURE note in card.h).
  if (target >= 0 && target < static_cast<int>(state.enemies.size())) {
    Enemy& enemy = state.enemies[target];
    if (data.damage > 0) {
      int hp_before = enemy.hp;
      int dmg = compute_attack_damage(
          data.damage, state.character.status_effects, enemy.status_effects);
      apply_damage_to_hp_block(enemy.hp, enemy.current_block, dmg);
      if (enemy.hp < hp_before) {
        fire_on_damaged(enemy);  // e.g. Louse Curl Up
      }
      if (hp_before > 0 && enemy.hp <= 0) {
        died_slot = target;  // defer on_death until the card resolves
      }
    }
  }

  // 3. Apply block (with Dexterity)
  if (data.block > 0) {
    int block_gained =
        data.block + get_status(state.character.status_effects,
                                StatusEffect::Dexterity);
    state.character.current_block += block_gained;
  }

  // 4. Apply status effects to the chosen target (enemy-targeted) or self.
  for (const auto& app : data.applies) {
    apply_status(state, app, target);
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

  // 6. Deferred on_death hook (ROB-62). Fires after the card fully resolves so
  // a Split can safely mutate state.enemies. Runs before the terminal check so
  // a split's spawned children prevent a premature "all enemies dead" win.
  if (died_slot >= 0) {
    fire_on_death(state, died_slot);
  }

  // 7. Terminal checks
  check_enemy_terminal(state);
  if (state.outcome != Outcome::InProgress) return;
  check_character_terminal(state);  // currently unreachable in v1
}

// Resolve one enemy's move. `actor_slot` is the acting enemy's slot index — the
// move's damage uses that enemy's status, its block lands on that enemy, and a
// Target::Enemy status is that enemy's self-buff (e.g. Cultist Incantation ->
// own Strength).
// LIMITATION (cross-enemy buffs): a move that buffs a *different* enemy (none in
// the current roster) would need a target field on Move; revisit in M3.
void apply_move_to_state(CombatState& state, const Move& move, int actor_slot) {
  Enemy& enemy = state.enemies[actor_slot];

  if (move.damage > 0) {
    int dmg = compute_attack_damage(move.damage, enemy.status_effects,
                                    state.character.status_effects);
    apply_damage_to_hp_block(state.character.hp, state.character.current_block,
                             dmg);
  }
  if (move.block > 0) {
    enemy.current_block += move.block;
  }
  // STS limitation: multi-hit attacks (Twin Strike, Pommel Strike) deal Strength
  // bonus per-hit. Our Move model is one hit per cast; multi-hit needs a `hits`
  // field on Move.
  for (const auto& app : move.applies) {
    apply_status(state, app, /*enemy_target=*/actor_slot);
  }
  // Status cards the move adds to the player's discard (ROB-72), e.g. a slime
  // spit adding Slimed. Resolves with the move (end of this enemy's action).
  for (CardId card : move.adds_to_discard) {
    state.discard_pile.push_back(Card{card});
  }
}

void handle_end_turn(CombatState& state) {
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

  // 2. Enemy turn — each living enemy acts in slot order. Enemies never die or
  // spawn during their own turn (they attack the player or buff themselves), so
  // a straight index loop is safe (no mid-loop vector mutation). The only death
  // that can occur is the player's, which we check for after each enemy acts.
  state.character_turn = false;
  for (std::size_t slot = 0; slot < state.enemies.size(); ++slot) {
    Enemy& enemy = state.enemies[slot];
    if (enemy.hp <= 0) continue;  // dead slot: skip

    // 2a. Reset block
    enemy.current_block = 0;
    // 2b. Apply the primed intent (set at combat start or the prior enemy turn).
    // last_move always stores the upcoming intent so the obs shows it.
    assert(enemy.last_move.has_value() &&
           "enemy.last_move must be primed by start_v1_combat or prior turn");
    apply_move_to_state(state, enemy.moves.at(*enemy.last_move),
                        static_cast<int>(slot));

    // 2c. Terminal check — an enemy attack may have killed the player.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;

    // 2d. Tick this enemy's statuses.
    tick_status_effects(enemy.status_effects);

    // 2e. Advance this enemy's Markov chain to set its next intent.
    select_next_move(enemy, state.rng);
  }

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

DecodedAction decode_action(int action) {
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  const int end_turn_idx = num_card_ids * kMaxEnemies;
  if (action == end_turn_idx) {
    return DecodedAction{/*is_end_turn=*/true, CardId::Strike, 0};
  }
  // action = card_idx * kMaxEnemies + target_idx
  const int card_idx = action / kMaxEnemies;
  const int target = action % kMaxEnemies;
  return DecodedAction{/*is_end_turn=*/false, static_cast<CardId>(card_idx),
                       target};
}

std::vector<bool> valid_actions(const CombatState& state) {
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  const int num_actions = num_card_ids * kMaxEnemies + 1;
  std::vector<bool> mask(num_actions, false);

  if (state.outcome != Outcome::InProgress) {
    return mask;  // all false
  }

  for (int action = 0; action < num_actions - 1; ++action) {
    const DecodedAction d = decode_action(action);
    const int card_idx = static_cast<int>(d.card);
    if (card_idx < 0 || card_idx >= num_card_ids) continue;

    const CardData& data = CARD_DATABASE.at(d.card);
    const bool in_hand = find_first_in_hand(state.current_hand, d.card) >= 0;
    const bool affordable = state.character.energy >= data.cost;
    if (!in_hand || !affordable) continue;

    // Target legality fork (shares card_targets_enemy with apply_action, so
    // the mask and the apply path never disagree).
    if (card_targets_enemy(data)) {
      // Targeted: the chosen enemy slot must hold a living enemy.
      const bool alive = d.target < static_cast<int>(state.enemies.size()) &&
                         state.enemies[d.target].hp > 0;
      mask[action] = alive;
    } else {
      // Untargeted (Defend): only the canonical slot 0 is legal.
      mask[action] = (d.target == 0);
    }
  }

  // End turn is always legal while in progress.
  mask[num_actions - 1] = true;
  return mask;
}

bool apply_action(CombatState& state, int action) {
  if (state.outcome != Outcome::InProgress) return false;

  auto mask = valid_actions(state);
  if (action < 0 || action >= static_cast<int>(mask.size()) || !mask[action]) {
    return false;
  }

  const DecodedAction d = decode_action(action);
  if (d.is_end_turn) {
    handle_end_turn(state);
  } else {
    handle_play_card(state, d.card, d.target);
  }
  return true;
}

}  // namespace minispire
