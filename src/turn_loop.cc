#include "turn_loop.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "card.h"
#include "encounter.h"
#include "enemy.h"
#include "status_effect.h"

namespace minispire {

namespace {

// Helper: look up a stack count in a debuff/power map, returning 0 if absent.
template <typename Effect>
int get_status(const std::unordered_map<Effect, int>& m, Effect e) {
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

// ---------------------------------------------------------------------------
// Centralized mutators (effects-architecture Stage 1).
//
// EVERY gameplay stat mutation flows through these — they are the future hook
// points of the action-queue architecture (docs/design/effects-architecture.md):
// Juggernaut fires inside gain_block, Rupture inside lose_player_hp, Feel No
// Pain / Dark Embrace inside move_to_exhaust, etc. Construction-time writes
// (enemy factories, start_combat) and phase-boundary resets are upkeep, not
// gameplay events, and stay direct. Convention: no other code assigns to
// hp/block/energy or pushes to exhaust/discard for gameplay reasons.
// ---------------------------------------------------------------------------

// Entity addressing: enemy slot index, or kPlayerSlot for the player.
constexpr int kPlayerSlot = -1;

// Grant block to the player (slot == kPlayerSlot) or an enemy. `amount` is the
// final amount — card-block math (Dexterity, Frail) happens at the card site
// since it only applies to block gained FROM CARDS, not all block.
void gain_block(CombatState& state, int slot, int amount) {
  if (amount <= 0) return;
  if (slot == kPlayerSlot) {
    state.character.current_block += amount;
  } else if (slot >= 0 && slot < static_cast<int>(state.enemies.size())) {
    state.enemies[slot].current_block += amount;
  }
}

// Direct player HP loss (a lose-HP EFFECT — bypasses block, can kill; ROB-80).
void lose_player_hp(CombatState& state, int amount) {
  if (amount <= 0) return;
  state.character.hp -= amount;
  if (state.character.hp < 0) state.character.hp = 0;
}

void gain_energy(CombatState& state, int amount) {
  state.character.energy += amount;
}

void spend_energy(CombatState& state, int amount) {
  state.character.energy -= amount;
}

// Spend ALL energy (X-cost cards); returns the amount spent (= X).
int spend_all_energy(CombatState& state) {
  int x = state.character.energy;
  state.character.energy = 0;
  return x;
}

// Card pile routing. All gameplay-driven moves into exhaust/discard go through
// these (the future CardExhausted hook point).
void move_to_exhaust(CombatState& state, Card card) {
  state.exhaust_pile.push_back(card);
}

void move_to_discard(CombatState& state, Card card) {
  state.discard_pile.push_back(card);
}

// Resolve a Target to the map to write into. `enemy_target` is the decoded enemy
// slot (ROB-60); ignored for Target::Character. Returns nullptr if the target
// slot is out of range (defensive). AoE (apply to all enemies) is not yet
// modeled — revisit when AoE cards land.
std::unordered_map<Debuff, int>* debuff_map(CombatState& state, Target target,
                                            int enemy_target) {
  if (target == Target::Character) return &state.character.debuffs;
  if (enemy_target >= 0 && enemy_target < static_cast<int>(state.enemies.size())) {
    return &state.enemies[enemy_target].debuffs;
  }
  return nullptr;
}
std::unordered_map<Power, int>* power_map(CombatState& state, Target target,
                                          int enemy_target) {
  if (target == Target::Character) return &state.character.powers;
  if (enemy_target >= 0 && enemy_target < static_cast<int>(state.enemies.size())) {
    return &state.enemies[enemy_target].powers;
  }
  return nullptr;
}

// Entangle is non-stacking: SET to the applied amount, not accumulated. It's
// 1-turn and boolean (ROB-75); stacking to 2 would wrongly last two turns.
void apply_debuff(CombatState& state, const DebuffApplication& app,
                  int enemy_target) {
  auto* m = debuff_map(state, app.target, enemy_target);
  if (!m) return;
  // Artifact (ROB-65): negates the whole debuff APPLICATION regardless of
  // stacks, consuming one Artifact charge. Checked on the same target's powers.
  auto* pm = power_map(state, app.target, enemy_target);
  if (pm) {
    auto art = pm->find(Power::Artifact);
    if (art != pm->end() && art->second > 0) {
      if (--art->second <= 0) pm->erase(art);
      return;  // debuff negated
    }
  }
  if (app.effect == Debuff::Entangle) {
    (*m)[app.effect] = app.amount;
  } else {
    (*m)[app.effect] += app.amount;
  }
}

void apply_power(CombatState& state, const PowerApplication& app,
                 int enemy_target) {
  auto* m = power_map(state, app.target, enemy_target);
  if (m) (*m)[app.effect] += app.amount;
}

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

// Apply one triggered effect's action for the enemy at `slot` (ROB-65).
// Indexed by slot rather than holding an Enemy& — mutators may (in future
// stages) reallocate state.enemies, and slot-indexing is the anti-dangling
// convention (effects-architecture Stage 1).
void apply_triggered_action(CombatState& state, int slot,
                            const TriggeredEffect& fx) {
  Enemy& enemy = state.enemies[slot];
  switch (fx.action) {
    case TriggeredAction::RewriteIntent:
      // Only meaningful for a living enemy (a dead one takes no turn).
      if (enemy.hp > 0) enemy.last_move = fx.move;
      break;
    case TriggeredAction::GainStrength:
      enemy.powers[Power::Strength] += fx.amount;
      break;
    case TriggeredAction::GainStrengthFromPower:
      // Gain Strength = the enemy's stacks of fx.power (Gremlin Nob Enrage,
      // mirroring how start-of-turn Ritual grants Strength = Ritual stacks).
      enemy.powers[Power::Strength] += get_status(enemy.powers, fx.power);
      break;
    case TriggeredAction::GainBlock:
      gain_block(state, slot, fx.amount);
      break;
    case TriggeredAction::ApplyPlayerDebuff:
      state.character.debuffs[fx.debuff] += fx.amount;
      break;
    case TriggeredAction::RemoveSelfPower:
      enemy.powers.erase(fx.power);
      break;
    case TriggeredAction::Wake:
      enemy.is_asleep = false;
      break;
  }
}

// Generalized trigger dispatcher (ROB-65). Fires every triggered_effect on the
// enemy at `slot` whose trigger matches `which`. `which` is the event that just
// occurred; HpAtOrBelow is a special damage-time trigger whose param is the hp
// threshold (checked here). `once` effects latch off after firing.
void fire_triggers(CombatState& state, int slot, Trigger which) {
  // Iterate by index: an effect could mutate state.enemies (none currently do),
  // and we need a stable reference to the owner each iteration.
  auto& effects = state.enemies[slot].triggered_effects;
  for (std::size_t i = 0; i < effects.size(); ++i) {
    TriggeredEffect& fx = effects[i];
    if (fx.trigger != which) continue;
    if (fx.once && fx.fired) continue;
    // Guard: fire only while the enemy is asleep (Lagavulin's damage-wake; a
    // first hit AFTER a self-wake must not re-stun it mid-cycle).
    if (fx.requires_asleep && !state.enemies[slot].is_asleep) continue;
    // HpAtOrBelow: only fire while the enemy is alive and at/below the threshold.
    if (which == Trigger::HpAtOrBelow) {
      Enemy& e = state.enemies[slot];
      if (e.hp <= 0 || e.hp > fx.param) continue;
    }
    apply_triggered_action(state, slot, fx);
    fx.fired = true;
  }
}

// on_death hook: fires when an enemy reaches hp <= 0. Deferred to after the
// player's card fully resolves. `slot` is the dying enemy's slot. Spore Cloud
// and any other OnDeath triggered_effects fire here.
void fire_on_death(CombatState& state, int slot) {
  fire_triggers(state, slot, Trigger::OnDeath);
}

// on_damaged hook: fires when an enemy actually loses HP. Runs the OnDamaged
// triggers (Curl Up, Angry, Lagavulin's damage-wake -> Stunned), then OnWake if
// this hit woke a sleeping enemy (Metallicize present), then the HpAtOrBelow
// triggers (the Large Slime's split interrupt).
void fire_on_damaged(CombatState& state, int slot) {
  const bool was_asleep = state.enemies[slot].is_asleep;
  // OnDamaged first: the damage-wake RewriteIntent (guarded on is_asleep) sets
  // the Stunned intent while still asleep.
  fire_triggers(state, slot, Trigger::OnDamaged);
  // Damage-wake: a hit that lands while asleep wakes the enemy immediately
  // (OnWake clears Metallicize now, so the stun turn gains no block).
  if (was_asleep) fire_triggers(state, slot, Trigger::OnWake);
  fire_triggers(state, slot, Trigger::HpAtOrBelow);
}

// Damage one enemy slot once, firing on_damaged and recording a death into
// `died_slots`. Shared by single-target and AoE / multi-hit cards (ROB-80).
void hit_enemy_once(CombatState& state, int slot, int base_damage,
                    std::vector<int>& died_slots) {
  Enemy& enemy = state.enemies[slot];
  if (enemy.hp <= 0) return;
  int hp_before = enemy.hp;
  int dmg = compute_attack_damage(base_damage, state.character.powers,
                                  state.character.debuffs, enemy.debuffs);
  apply_damage_to_hp_block(enemy.hp, enemy.current_block, dmg);
  if (enemy.hp < hp_before) fire_on_damaged(state, slot);
  if (hp_before > 0 && enemy.hp <= 0) died_slots.push_back(slot);
}

void handle_play_card(CombatState& state, CardId card_id, int target) {
  const CardData& data = CARD_DATABASE.at(card_id);

  // 1. Pay energy. X-cost cards (Whirlwind) spend ALL energy; X = the amount
  // spent, used as the hit count.
  int x = 0;
  if (data.cost == kXCost) {
    x = spend_all_energy(state);
  } else {
    spend_energy(state, data.cost);
  }
  // hits: -1 means "X hits" (Whirlwind); otherwise the literal count.
  const int hits = (data.hits < 0) ? x : data.hits;

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

  // 2. Damage: `hits` times, to each target slot. Deaths deferred (on_death may
  // mutate state.enemies via a split).
  std::vector<int> died_slots;
  if (data.damage > 0) {
    for (int h = 0; h < hits; ++h) {
      for (int slot : target_slots) {
        hit_enemy_once(state, slot, data.damage, died_slots);
      }
    }
  }

  // 3. Apply block (Dexterity adds, then Frail reduces 25%, floored — StS order).
  if (data.block > 0) {
    int block_gained =
        data.block + get_status(state.character.powers, Power::Dexterity);
    if (get_status(state.character.debuffs, Debuff::Frail) > 0) {
      block_gained =
          static_cast<int>(std::floor(static_cast<float>(block_gained) * 0.75f));
    }
    gain_block(state, kPlayerSlot, block_gained);
  }

  // 4. Apply debuffs/powers. Self/None applications route to the player (their
  // Target::Character); enemy applications go to each target slot (AoE loops).
  for (const auto& app : data.applies_debuffs) {
    if (app.target == Target::Character) {
      apply_debuff(state, app, -1);
    } else {
      for (int slot : target_slots) apply_debuff(state, app, slot);
    }
  }
  for (const auto& app : data.applies_powers) {
    if (app.target == Target::Character) {
      apply_power(state, app, -1);
    } else {
      for (int slot : target_slots) apply_power(state, app, slot);
    }
  }

  // 4b. Card-flow effects (ROB-80 Tier B). Energy gain, then lose-HP (an EFFECT,
  // not a cost: direct HP loss bypassing block, and it CAN kill the player).
  if (data.energy > 0) gain_energy(state, data.energy);
  lose_player_hp(state, data.lose_hp);

  // 5. Move card from hand to discard or exhaust
  int idx = find_first_in_hand(state.current_hand, card_id);
  assert(idx >= 0 && "mask should have rejected this action");
  Card played = state.current_hand[idx];
  state.current_hand.erase(state.current_hand.begin() + idx);
  if (data.exhaust) {
    move_to_exhaust(state, played);
  } else {
    move_to_discard(state, played);
  }

  // 5b. OnPlayerSkill triggers (ROB-65). Playing a Skill fires every living
  // enemy's OnPlayerSkill effects (the Gremlin Nob's Enrage). Independent of
  // whether the card dealt damage or killed anything.
  if (data.type == CardType::Skill) {
    for (std::size_t i = 0; i < state.enemies.size(); ++i) {
      if (state.enemies[i].hp > 0) {
        fire_triggers(state, static_cast<int>(i), Trigger::OnPlayerSkill);
      }
    }
  }

  // 6. Deferred on_death hooks (ROB-62). Fire after the card fully resolves so a
  // Split can safely mutate state.enemies. Runs before the terminal check so a
  // split's spawned children prevent a premature "all enemies dead" win. An AoE
  // card can kill several enemies at once (ROB-80) — fire each.
  if (!died_slots.empty()) {
    for (int slot : died_slots) fire_on_death(state, slot);

    // 6b. BecameLastEnemy triggers (ROB-77 via ROB-65). Checked AFTER on_death
    // (a split could have added enemies). If the kills left exactly one living
    // enemy, fire its BecameLastEnemy effects — e.g. the Shield Gremlin rewrites
    // its queued Protect to attack once alone.
    if (count_living(state) == 1) {
      for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        if (state.enemies[i].hp > 0) {
          fire_triggers(state, static_cast<int>(i), Trigger::BecameLastEnemy);
        }
      }
    }
  }

  // 6c. Draw (ROB-80 Tier B) — resolves LAST, after the card's other effects.
  for (int i = 0; i < data.draw; ++i) draw_one(state);

  // 7. Terminal checks. DEATH takes precedence over victory: if the card's
  // self-damage (a lose-HP card — ROB-80) killed the player, it's a Loss even
  // if the same card also cleared the room (e.g. Hemokinesis at 2 HP killing the
  // last enemy while its 2 HP loss kills you). The player CAN now die on their
  // own turn, so the character check is no longer unreachable.
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
  if (move.wakes_on_resolve) {
    fire_triggers(state, actor_slot, Trigger::OnWake);
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
