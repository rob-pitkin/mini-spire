#include "turn_loop.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "action.h"
#include "card.h"
#include "encounter.h"
#include "enemy.h"
#include "status_effect.h"

namespace minispire {

namespace {

// Debuffs decrement by 1 at end of the bearer's turn; remove at 0. Powers never
// tick — decrement-ness is now the TYPE, so no per-effect denylist.
void tick_debuffs(std::unordered_map<Debuff, int>& debuffs) {
  for (auto it = debuffs.begin(); it != debuffs.end();) {
    it->second -= 1;
    if (it->second <= 0) {
      it = debuffs.erase(it);
    } else {
      ++it;
    }
  }
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

// Play one card: translate it into its action sequence and drain the queue
// (effects-architecture Stage 2). The translation preserves the pre-queue
// resolution order: damage (hits x targets), block, debuffs/powers, energy,
// lose-HP, pile move, CardPlayed hook, deferred deaths (ROB-62), then draw
// LAST (ROB-80 Tier B). Hook responses are pushed to the BACK of the queue as
// they fire (§7 default ordering) — divergences from the pre-queue nested
// timing are recorded in docs/design/ordering-notes.md.
void handle_play_card(CombatState& state, CardId card_id, int target) {
  const CardData& data = CARD_DATABASE.at(card_id);

  // 1. Pay energy at translation time — an X-cost card's X (= its hit count)
  // must be known before its hits can be queued.
  int x = 0;
  if (data.cost == kXCost) {
    x = spend_all_energy(state);
  } else {
    spend_energy(state, data.cost);
  }
  // hits: -1 means "X hits" (Whirlwind); otherwise the literal count.
  const int hits = (data.hits < 0) ? x : data.hits;

  // 2. The played card leaves the hand now — it is "in flight" during
  // resolution (StS) and rejoins a pile via the queued ExhaustCard/DiscardCard.
  const int idx = find_first_in_hand(state.current_hand, card_id);
  assert(idx >= 0 && "mask should have rejected this action");
  state.current_hand.erase(state.current_hand.begin() + idx);

  // The set of enemy slots this card resolves against.
  std::vector<int> target_slots;
  if (data.target == CardTarget::AllEnemies) {
    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
      if (state.enemies[i].hp > 0) target_slots.push_back(static_cast<int>(i));
    }
  } else if (data.target == CardTarget::Enemy && target >= 0 &&
             target < static_cast<int>(state.enemies.size())) {
    target_slots.push_back(target);
  }

  // 3. Translate the CardData field-bag into its default action sequence.
  ActionQueue q;
  ResolutionContext ctx;
  if (data.damage > 0) {
    // One DealDamage per hit per target; a multi-hit AoE (Whirlwind) sweeps
    // all targets each swing. Damage math runs at execution (Strength per hit).
    for (int h = 0; h < hits; ++h) {
      for (int slot : target_slots) {
        Action a;
        a.kind = ActionKind::DealDamage;
        a.actor = kPlayerSlot;
        a.target = slot;
        a.amount = data.damage;
        q.push_back(a);
      }
    }
  }
  if (data.block > 0) {
    Action a;
    a.kind = ActionKind::GainBlock;
    a.target = kPlayerSlot;
    a.amount = data.block;
    a.card_block = true;  // Dex/Frail math applies in the executor
    q.push_back(a);
  }
  // Self/None applications route to the player; enemy applications go to each
  // target slot (AoE loops).
  for (const auto& app : data.applies_debuffs) {
    Action a;
    a.kind = ActionKind::ApplyDebuff;
    a.debuff = app.effect;
    a.amount = app.amount;
    if (app.target == Target::Character) {
      a.target = kPlayerSlot;
      q.push_back(a);
    } else {
      for (int slot : target_slots) {
        a.target = slot;
        q.push_back(a);
      }
    }
  }
  for (const auto& app : data.applies_powers) {
    Action a;
    a.kind = ActionKind::ApplyPower;
    a.power = app.effect;
    a.amount = app.amount;
    if (app.target == Target::Character) {
      a.target = kPlayerSlot;
      q.push_back(a);
    } else {
      for (int slot : target_slots) {
        a.target = slot;
        q.push_back(a);
      }
    }
  }
  // Card-flow effects (ROB-80 Tier B). Energy gain, then lose-HP (an EFFECT,
  // not a cost: direct HP loss bypassing block, and it CAN kill the player).
  if (data.energy > 0) {
    Action a;
    a.kind = ActionKind::GainEnergy;
    a.amount = data.energy;
    q.push_back(a);
  }
  if (data.lose_hp > 0) {
    Action a;
    a.kind = ActionKind::LoseHp;
    a.amount = data.lose_hp;
    q.push_back(a);
  }
  // The played card lands in its pile after the card's own effects resolve.
  {
    Action a;
    a.kind = data.exhaust ? ActionKind::ExhaustCard : ActionKind::DiscardCard;
    a.card = card_id;
    q.push_back(a);
  }
  // CardPlayed hook (Gremlin Nob Enrage), then deferred deaths, then draw.
  {
    Action a;
    a.kind = ActionKind::CardPlayedHook;
    a.card = card_id;
    q.push_back(a);
  }
  q.push_back(Action{ActionKind::CheckDeath});
  if (data.draw > 0) {
    Action a;
    a.kind = ActionKind::DrawCards;
    a.amount = data.draw;
    q.push_back(a);
  }

  // 4. Drain to completion — every mutation is a flat, sequential step; no
  // live reference or open loop spans a mutation.
  drain(state, q, ctx);

  // 5. Terminal checks. DEATH takes precedence over victory: if the card's
  // self-damage (a lose-HP card — ROB-80) killed the player, it's a Loss even
  // if the same card also cleared the room (e.g. Hemokinesis at 2 HP killing
  // the last enemy while its 2 HP loss kills you).
  check_character_terminal(state);
  if (state.outcome != Outcome::InProgress) return;
  check_enemy_terminal(state);
}

// Choose a uniform-random living ally (a slot != actor with hp > 0), or -1 if
// there are none. Used by ally-targeting moves (ROB-77 Protect).
int random_living_ally(CombatState& state, int actor_slot) {
  std::vector<int> allies;
  for (int i = 0; i < static_cast<int>(state.enemies.size()); ++i) {
    if (i != actor_slot && state.enemies[i].hp > 0) allies.push_back(i);
  }
  if (allies.empty()) return -1;
  std::uniform_int_distribution<int> pick(0, static_cast<int>(allies.size()) - 1);
  return allies[pick(state.rng)];
}

// Resolve one enemy's move. `actor_slot` is the acting enemy's slot index — the
// move's damage uses that enemy's status, its block lands on that enemy (or a
// random ally for a blocks_ally move), and a Target::Enemy status is that
// enemy's self-buff (e.g. Cultist Incantation -> own Strength).
//
// Still imperative (the Stage-2 two-regime period): the enemy phase migrates
// onto the queue at Stage 3. It calls the centralized mutators directly and
// fires its one trigger site through a local mini-drain.
void apply_move_to_state(CombatState& state, const Move& move, int actor_slot) {
  Enemy& enemy = state.enemies[actor_slot];

  if (move.damage > 0) {
    int dmg = compute_attack_damage(move.damage, enemy.powers, enemy.debuffs,
                                    state.character.debuffs);
    apply_damage_to_hp_block(state.character.hp, state.character.current_block,
                             dmg);
  }
  if (move.block > 0) {
    if (move.blocks_ally) {
      // Protect (ROB-77): block a random living ally; fall back to self if none.
      int ally = random_living_ally(state, actor_slot);
      int slot = (ally >= 0) ? ally : actor_slot;
      gain_block(state, slot, move.block);
    } else {
      gain_block(state, actor_slot, move.block);
    }
  }
  // STS limitation: multi-hit attacks (Twin Strike, Pommel Strike) deal Strength
  // bonus per-hit. Our Move model is one hit per cast; multi-hit needs a `hits`
  // field on Move.
  for (const auto& app : move.applies_debuffs) {
    apply_debuff(state, app, /*enemy_target=*/actor_slot);
  }
  for (const auto& app : move.applies_powers) {
    apply_power(state, app, /*enemy_target=*/actor_slot);
  }
  // Status cards the move adds to the player's discard (ROB-72), e.g. a slime
  // spit adding Slimed. Resolves with the move (end of this enemy's action).
  for (CardId card : move.adds_to_discard) {
    move_to_discard(state, Card{card});
  }
  // Wake-on-resolve (ROB-65): Lagavulin's last sleep move (Sleep3) fires the
  // enemy's OnWake effects at the END of the asleep turn (self-wake path), so
  // that turn keeps its Metallicize block and the next turn onward gets none.
  // Fired through a local mini-drain so trigger dispatch has exactly one
  // implementation (the two-regime bridge; Stage 3 removes it).
  if (move.wakes_on_resolve) {
    ActionQueue q;
    ResolutionContext ctx;
    fire_enemy_hooks(state, actor_slot, Hook::EnemyWake, q);
    drain(state, q, ctx);
  }
  // Escape (ROB-74): the acting enemy flees by setting its own hp to 0. It
  // leaves the fight — everything keys on hp>0, so it's no longer
  // targetable/acting and its slot frees. This is NOT a death: on_death hooks
  // are not fired (a fleeing enemy doesn't split/spore). check_enemy_terminal
  // (run after the enemy turn) treats it as gone -> Won if it was the last one.
  if (move.escapes) {
    enemy.hp = 0;
  }

  // Split (ROB-64): the acting enemy dies and spawns its children, each set to
  // the parent's CURRENT HP (inherited). Capture HP and kill the parent BEFORE
  // spawning — place_child may reallocate state.enemies (invalidating `enemy`)
  // and, by killing the parent first, its slot becomes a free slot a child can
  // reuse. on_death hooks are not fired (split is its own mechanic).
  if (move.splits) {
    const int inherited_hp = enemy.hp;
    std::vector<Enemy> children = enemy.split_children;  // copy before invalidation
    state.enemies[actor_slot].hp = 0;                    // parent dies
    for (Enemy child : children) {
      // Children take the parent's split-time HP as BOTH current and max — they
      // aren't "real" Mediums with rolled HP, just spawned at the inherited
      // value for this fight (ROB-64, verified faithful).
      child.hp = inherited_hp;
      child.max_hp = inherited_hp;
      place_child(state, child);
    }
  }
}

void handle_end_turn(CombatState& state) {
  // 1. End of player turn
  // 1a. Empty the hand: unplayed Ethereal cards exhaust (ROB-65 Dazed); the
  // rest discard.
  for (const Card& c : state.current_hand) {
    if (CARD_DATABASE.at(c.card_id).ethereal) {
      move_to_exhaust(state, c);
    } else {
      move_to_discard(state, c);
    }
  }
  state.current_hand.clear();
  // 1b. Tick character debuffs (powers never tick)
  tick_debuffs(state.character.debuffs);
  // 1c. Discard leftover energy
  state.character.energy = 0;

  // 2. Enemy turn — each enemy that is ALIVE AT THE START OF THE PHASE acts, in
  // slot order. An enemy can leave via escape (ROB-74: hp->0) or spawn children
  // mid-phase via a Split move (ROB-64). Split children must NOT act the phase
  // they spawn (StS), so we snapshot the acting slots up front: a child placed
  // into a freed/appended slot mid-phase is not in the snapshot and is skipped
  // until next phase. Terminal cases checked: the player dying (per enemy) and
  // all enemies gone (after the loop).
  state.character_turn = false;

  // 2a. Reset ALL enemies' block once, at the start of the enemy phase — not
  // per-individual-turn. This matches StS: block persists through the whole
  // enemy phase, so a Protect (ROB-77) granted to an ally that hasn't acted yet
  // survives into the player's turn. (Per-turn reset would wipe it.)
  for (Enemy& e : state.enemies) {
    if (e.hp > 0) e.current_block = 0;
  }

  // Snapshot the slots alive at phase start — the only enemies that act.
  std::vector<std::size_t> acting_slots;
  for (std::size_t i = 0; i < state.enemies.size(); ++i) {
    if (state.enemies[i].hp > 0) acting_slots.push_back(i);
  }

  for (std::size_t slot : acting_slots) {
    if (state.enemies[slot].hp <= 0) continue;  // died earlier this phase

    // 2a-pre. Start-of-turn powers. Ritual: gain Strength = Ritual stacks
    // (Cultist). It does NOT tick down. Because Ritual is applied mid-turn when
    // Incantation resolves (after this trigger point), it first fires the turn
    // *after* it's gained — matching StS (ROB-73).
    int ritual = get_status(state.enemies[slot].powers, Power::Ritual);
    if (ritual > 0) {
      state.enemies[slot].powers[Power::Strength] += ritual;
    }
    // Metallicize: gain block = stacks at the start of the turn (ROB-65,
    // Lagavulin asleep). Runs AFTER the phase-start block reset, so an asleep
    // enemy shows exactly its Metallicize amount each turn (no accumulation).
    int metallicize = get_status(state.enemies[slot].powers, Power::Metallicize);
    gain_block(state, static_cast<int>(slot), metallicize);

    // 2b. Apply the primed intent (set at combat start or the prior enemy turn).
    // last_move always stores the upcoming intent so the obs shows it. NOTE: a
    // Split move calls place_child -> push_back, which can REALLOCATE
    // state.enemies. Hold no Enemy& across this call; re-index by `slot` after.
    // Copy the move by value so it stays valid even if enemy.moves is freed.
    assert(state.enemies[slot].last_move.has_value() &&
           "enemy.last_move must be primed by start_v1_combat or prior turn");
    const Move move = state.enemies[slot].moves.at(*state.enemies[slot].last_move);
    apply_move_to_state(state, move, static_cast<int>(slot));

    // 2c. Terminal check — an enemy attack may have killed the player.
    check_character_terminal(state);
    if (state.outcome != Outcome::InProgress) return;

    // If this enemy left the fight via its move — escape (ROB-74) or Split
    // (ROB-64) — it takes no further action this phase. Both make the actor's
    // hp 0, but a Split child may immediately REOCCUPY this slot, so we can't
    // re-test state.enemies[slot].hp here (that would read the child). Key off
    // the move instead. Also skip if the actor died some other way (hp <= 0).
    if (move.escapes || move.splits || state.enemies[slot].hp <= 0) continue;

    // 2d. Tick this enemy's debuffs (powers never tick).
    tick_debuffs(state.enemies[slot].debuffs);

    // 2e. Advance this enemy's Markov chain to set its next intent.
    select_next_move(state.enemies[slot], state.rng);
  }

  // 2f. All enemies gone? An escape (ROB-74) can clear the last living enemy,
  // which ends the fight as a Win even though nothing was killed this turn.
  check_enemy_terminal(state);
  if (state.outcome != Outcome::InProgress) return;

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

int compute_attack_damage(
    int base, const std::unordered_map<Power, int>& attacker_powers,
    const std::unordered_map<Debuff, int>& attacker_debuffs,
    const std::unordered_map<Debuff, int>& defender_debuffs) {
  // Float-internal, truncated once at the end (per the STS wiki rounding rule).
  float d = static_cast<float>(base) +
            static_cast<float>(get_status(attacker_powers, Power::Strength));
  if (get_status(attacker_debuffs, Debuff::Weak) > 0) d *= 0.75f;
  if (get_status(defender_debuffs, Debuff::Vulnerable) > 0) d *= 1.5f;
  int result = static_cast<int>(std::floor(d));
  return result < 0 ? 0 : result;
}

std::vector<Card> starter_deck() {
  std::vector<Card> deck;
  for (int i = 0; i < 5; ++i) deck.push_back(Card{CardId::Strike});
  for (int i = 0; i < 4; ++i) deck.push_back(Card{CardId::Defend});
  deck.push_back(Card{CardId::Bash});
  return deck;
}

CombatState start_combat(uint32_t seed, EncounterPool pool,
                         std::vector<Card> deck) {
  CombatState state;
  state.seed = seed;
  state.rng = std::mt19937(seed);

  state.character.max_hp = IRONCLAD_MAX_HP;
  state.character.hp = IRONCLAD_MAX_HP;
  state.character.energy_per_turn = IRONCLAD_ENERGY_PER_TURN;
  state.character.energy = IRONCLAD_ENERGY_PER_TURN;
  state.character.current_block = 0;

  // Sample the encounter (each enemy primed with its turn-1 intent).
  state.enemies = sample_encounter(pool, state.rng);

  // Shuffle the given deck into the draw pile.
  state.draw_pile = std::move(deck);
  std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);

  state.turn_number = 1;
  state.character_turn = true;
  state.outcome = Outcome::InProgress;

  // Draw the opening hand.
  for (int i = 0; i < STARTING_HAND_SIZE; ++i) {
    draw_one(state);
  }

  return state;
}

CombatState start_v1_combat(uint32_t seed) {
  // Backward-compatible v1 fixture: a fixed single Jaw Worm + the starter deck.
  // Distinct from start_combat (which samples an encounter) so M1 / existing
  // tests keep their deterministic Jaw Worm fight.
  CombatState state;
  state.seed = seed;
  state.rng = std::mt19937(seed);

  state.character.max_hp = IRONCLAD_MAX_HP;
  state.character.hp = IRONCLAD_MAX_HP;
  state.character.energy_per_turn = IRONCLAD_ENERGY_PER_TURN;
  state.character.energy = IRONCLAD_ENERGY_PER_TURN;
  state.character.current_block = 0;

  state.enemies.push_back(make_jaw_worm(state.rng));

  state.draw_pile = starter_deck();
  std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);

  state.turn_number = 1;
  state.character_turn = true;
  state.outcome = Outcome::InProgress;

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

  // Entangle (ROB-75): while entangled, the player can't play attack cards.
  const bool entangled =
      get_status(state.character.debuffs, Debuff::Entangle) > 0;

  for (int action = 0; action < num_actions - 1; ++action) {
    const DecodedAction d = decode_action(action);
    const int card_idx = static_cast<int>(d.card);
    if (card_idx < 0 || card_idx >= num_card_ids) continue;

    const CardData& data = CARD_DATABASE.at(d.card);
    if (data.unplayable) continue;  // Dazed etc. — never a legal action (ROB-65)
    const bool in_hand = find_first_in_hand(state.current_hand, d.card) >= 0;
    // X-cost cards (ROB-80) are always affordable (X = current energy, may be 0);
    // fixed-cost cards need enough energy.
    const bool affordable =
        data.cost == kXCost || state.character.energy >= data.cost;
    if (!in_hand || !affordable) continue;
    // Entangle blocks all Attack-type cards for a turn (ROB-75).
    if (entangled && data.type == CardType::Attack) continue;

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
