#include "action.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "query.h"      // can_draw (Battle Trance)
#include "turn_loop.h"  // compute_attack_damage, HAND_SIZE_LIMIT

namespace minispire {

// ---------------------------------------------------------------------------
// Centralized mutators (Stage 1). EVERY gameplay stat mutation flows through
// these — they are the hook points of the action-queue architecture:
// Juggernaut fires inside gain_block, Rupture inside lose_player_hp, Feel No
// Pain / Dark Embrace inside move_to_exhaust, etc. Convention: no other code
// assigns to hp/block/energy or pushes to exhaust/discard for gameplay
// reasons.
// ---------------------------------------------------------------------------

void apply_damage_to_hp_block(int& hp, int& block, int amount) {
  int blocked = std::min(amount, block);
  block -= blocked;
  hp -= (amount - blocked);
  if (hp < 0) hp = 0;
}

void gain_block(CombatState& state, int slot, int amount) {
  if (amount <= 0) return;
  if (slot == kPlayerSlot) {
    state.character.current_block += amount;
  } else if (slot >= 0 && slot < static_cast<int>(state.enemies.size())) {
    state.enemies[slot].current_block += amount;
  }
}

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

int spend_all_energy(CombatState& state) {
  int x = state.character.energy;
  state.character.energy = 0;
  return x;
}

void move_to_exhaust(CombatState& state, Card card) {
  state.exhaust_pile.push_back(card);
}

void move_to_discard(CombatState& state, Card card) {
  state.discard_pile.push_back(card);
}

std::optional<CardId> draw_one(CombatState& state) {
  if (state.draw_pile.empty()) {
    if (state.discard_pile.empty()) return std::nullopt;
    state.draw_pile = std::move(state.discard_pile);
    state.discard_pile.clear();
    std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);
  }
  if (static_cast<int>(state.current_hand.size()) >= HAND_SIZE_LIMIT) {
    return std::nullopt;
  }
  state.current_hand.push_back(state.draw_pile.back());
  state.draw_pile.pop_back();
  return state.current_hand.back().card_id;
}

namespace {

// Resolve a Target to the map to write into. Returns nullptr if the target
// slot is out of range (defensive).
std::unordered_map<Debuff, int>* debuff_map(CombatState& state, Target target,
                                            int enemy_target) {
  if (target == Target::Character) return &state.character.debuffs;
  if (enemy_target >= 0 &&
      enemy_target < static_cast<int>(state.enemies.size())) {
    return &state.enemies[enemy_target].debuffs;
  }
  return nullptr;
}
std::unordered_map<Power, int>* power_map(CombatState& state, Target target,
                                          int enemy_target) {
  if (target == Target::Character) return &state.character.powers;
  if (enemy_target >= 0 &&
      enemy_target < static_cast<int>(state.enemies.size())) {
    return &state.enemies[enemy_target].powers;
  }
  return nullptr;
}

}  // namespace

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
  // Entangle is non-stacking: SET to the applied amount, not accumulated. It's
  // 1-turn and boolean (ROB-75); stacking to 2 would wrongly last two turns.
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

// ---------------------------------------------------------------------------
// Hook dispatch
// ---------------------------------------------------------------------------

namespace {

Action make_action(ActionKind kind) {
  Action a;
  a.kind = kind;
  return a;
}

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

// Translate one fired TriggeredEffect into its pushed response action.
// Magnitudes that read stacks (Enrage's GainStrengthFromPower) resolve at fire
// time; everything else resolves when the pushed action executes.
void push_trigger_response(CombatState& state, int slot,
                           const TriggeredEffect& fx, ActionQueue& q) {
  switch (fx.action) {
    case TriggeredAction::RewriteIntent: {
      Action a = make_action(ActionKind::RewriteIntent);
      a.target = slot;
      a.move = fx.move;
      q.push_back(a);
      break;
    }
    case TriggeredAction::GainStrength: {
      Action a = make_action(ActionKind::ApplyPower);
      a.target = slot;
      a.power = Power::Strength;
      a.amount = fx.amount;
      q.push_back(a);
      break;
    }
    case TriggeredAction::GainStrengthFromPower: {
      // Gain Strength = the enemy's stacks of fx.power (Gremlin Nob Enrage,
      // mirroring how start-of-turn Ritual grants Strength = Ritual stacks).
      Action a = make_action(ActionKind::ApplyPower);
      a.target = slot;
      a.power = Power::Strength;
      a.amount = get_status(state.enemies[slot].powers, fx.power);
      q.push_back(a);
      break;
    }
    case TriggeredAction::GainBlock: {
      Action a = make_action(ActionKind::GainBlock);
      a.target = slot;
      a.amount = fx.amount;
      q.push_back(a);
      break;
    }
    case TriggeredAction::ApplyPlayerDebuff: {
      Action a = make_action(ActionKind::ApplyDebuff);
      a.target = kPlayerSlot;
      a.debuff = fx.debuff;
      a.amount = fx.amount;
      q.push_back(a);
      break;
    }
    case TriggeredAction::RemoveSelfPower: {
      Action a = make_action(ActionKind::RemovePower);
      a.target = slot;
      a.power = fx.power;
      q.push_back(a);
      break;
    }
    case TriggeredAction::Wake: {
      Action a = make_action(ActionKind::Wake);
      a.target = slot;
      q.push_back(a);
      break;
    }
  }
}

}  // namespace

void fire_enemy_hooks(CombatState& state, int slot, Hook hook, ActionQueue& q) {
  // Map the engine-wide Hook onto the per-enemy Trigger vocabulary. The two
  // enums unify when the two-regime period ends (Stage 3/4); until then
  // TriggeredEffect data tables keep their Trigger names. Hooks with no enemy
  // analog (player-power hooks) are no-ops here.
  Trigger which;
  switch (hook) {
    case Hook::CardPlayed:      which = Trigger::OnPlayerSkill; break;
    case Hook::EnemyDamaged:    which = Trigger::OnDamaged; break;
    case Hook::OnAnyDamage:     which = Trigger::OnAnyDamage; break;
    case Hook::EnemyHpThreshold: which = Trigger::HpAtOrBelow; break;
    case Hook::EnemyDeath:      which = Trigger::OnDeath; break;
    case Hook::EnemyWake:       which = Trigger::OnWake; break;
    case Hook::BecameLastEnemy: which = Trigger::BecameLastEnemy; break;
    case Hook::TurnStartPlayer:
    case Hook::TurnEndPlayer:
    case Hook::TurnStartEnemy:
    case Hook::CardExhausted:
    case Hook::BlockGainedPlayer:
    case Hook::HpLostPlayer:
    case Hook::CardDrawn:
    case Hook::PlayerAttacked:
      return;
  }

  // Iterate by index — responses only push (never mutate), but slot-indexing
  // per access is the anti-dangling convention.
  auto& effects = state.enemies[slot].triggered_effects;
  for (std::size_t i = 0; i < effects.size(); ++i) {
    TriggeredEffect& fx = effects[i];
    if (fx.trigger != which) continue;
    if (fx.once && fx.fired) continue;
    // Guard: fire only while the enemy is asleep (Lagavulin's damage-wake; a
    // first hit AFTER a self-wake must not re-stun it mid-cycle).
    if (fx.requires_asleep && !state.enemies[slot].is_asleep) continue;
    // HpAtOrBelow: only fire while the enemy is alive and at/below threshold.
    if (which == Trigger::HpAtOrBelow) {
      const Enemy& e = state.enemies[slot];
      if (e.hp <= 0 || e.hp > fx.param) continue;
    }
    push_trigger_response(state, slot, fx, q);
    fx.fired = true;
  }
}

void fire_enemy_power_hooks(CombatState& state, int slot, Hook hook,
                            ActionQueue& q) {
  if (hook != Hook::TurnStartEnemy) return;  // Stage 4 extends this registry

  // Ritual: gain Strength = Ritual stacks (Cultist). It does NOT tick down.
  // Because Ritual is applied mid-turn when Incantation resolves (after this
  // trigger point), it first fires the turn *after* it's gained — matching StS
  // (ROB-73).
  const int ritual = get_status(state.enemies[slot].powers, Power::Ritual);
  if (ritual > 0) {
    Action a = make_action(ActionKind::ApplyPower);
    a.target = slot;
    a.power = Power::Strength;
    a.amount = ritual;
    q.push_back(a);
  }
  // Metallicize: gain block = stacks at the start of the turn (ROB-65,
  // Lagavulin asleep). Queued AFTER the phase-start block reset, so an asleep
  // enemy shows exactly its Metallicize amount each turn (no accumulation).
  const int metallicize =
      get_status(state.enemies[slot].powers, Power::Metallicize);
  if (metallicize > 0) {
    Action a = make_action(ActionKind::GainBlock);
    a.target = slot;
    a.amount = metallicize;
    q.push_back(a);
  }
}

namespace {

// Push one player-power response. Small helpers keep the registry switch flat.
void push_player_block(ActionQueue& q, int amount) {
  Action a = make_action(ActionKind::GainBlock);
  a.target = kPlayerSlot;
  a.amount = amount;
  q.push_back(a);
}
void push_player_strength(ActionQueue& q, int amount) {
  Action a = make_action(ActionKind::ApplyPower);
  a.target = kPlayerSlot;
  a.power = Power::Strength;
  a.amount = amount;
  q.push_back(a);
}
void push_draw(ActionQueue& q, int amount) {
  Action a = make_action(ActionKind::DrawCards);
  a.amount = amount;
  q.push_back(a);
}
void push_remove_player_power(ActionQueue& q, Power p) {
  Action a = make_action(ActionKind::RemovePower);
  a.target = kPlayerSlot;
  a.power = p;
  q.push_back(a);
}

}  // namespace

void fire_player_power_hooks(CombatState& state, Hook hook, ActionQueue& q,
                             CardId card, int attacker_slot) {
  const auto& powers = state.character.powers;
  // Canonical firing order = Power enum order (§4.4 determinism rule). Each
  // arm reads its stacks and pushes; nothing mutates here.
  const int demon_form = get_status(powers, Power::DemonForm);
  const int combust = get_status(powers, Power::Combust);
  const int feel_no_pain = get_status(powers, Power::FeelNoPain);
  const int dark_embrace = get_status(powers, Power::DarkEmbrace);
  const int evolve = get_status(powers, Power::Evolve);
  const int fire_breathing = get_status(powers, Power::FireBreathing);
  const int rupture = get_status(powers, Power::Rupture);
  const int juggernaut = get_status(powers, Power::Juggernaut);
  const int rage = get_status(powers, Power::Rage);
  const int flame_barrier = get_status(powers, Power::FlameBarrier);
  const int brutality = get_status(powers, Power::Brutality);
  const int berserk = get_status(powers, Power::Berserk);
  const int metallicize = get_status(powers, Power::Metallicize);

  switch (hook) {
    case Hook::TurnStartPlayer:
      if (demon_form > 0) push_player_strength(q, demon_form);
      if (brutality > 0) {
        Action a = make_action(ActionKind::LoseHp);
        a.amount = brutality;
        q.push_back(a);
        push_draw(q, brutality);
      }
      if (berserk > 0) {
        Action a = make_action(ActionKind::GainEnergy);
        a.amount = berserk;
        q.push_back(a);
      }
      // Flame Barrier is "this turn" from the play until the START of the next
      // player turn — it must survive the enemy phase to retaliate (ROB wiki
      // ruling), so it expires here rather than at end of turn.
      if (flame_barrier > 0) push_remove_player_power(q, Power::FlameBarrier);
      break;
    case Hook::TurnEndPlayer:
      if (combust > 0) {
        // Lose 1 HP per cast, then fixed damage to all enemies. `stacks` is the
        // accumulated damage; casts are counted separately (mixed upgrades).
        if (state.character.combust_casts > 0) {
          Action a = make_action(ActionKind::LoseHp);
          a.amount = state.character.combust_casts;
          q.push_back(a);
        }
        Action a = make_action(ActionKind::DamageAllEnemies);
        a.amount = combust;
        q.push_back(a);
      }
      if (metallicize > 0) push_player_block(q, metallicize);
      // Rage lasts only the player's own turn.
      if (rage > 0) push_remove_player_power(q, Power::Rage);
      break;
    case Hook::CardPlayed:
      // Rage: block whenever an Attack is played this turn.
      if (rage > 0 && CARD_DATABASE.at(card).type == CardType::Attack) {
        push_player_block(q, rage);
      }
      break;
    case Hook::CardExhausted:
      if (feel_no_pain > 0) push_player_block(q, feel_no_pain);
      if (dark_embrace > 0) push_draw(q, dark_embrace);
      break;
    case Hook::BlockGainedPlayer:
      // Juggernaut: fixed damage to a random enemy, rolled per trigger at
      // execution time.
      if (juggernaut > 0) {
        Action a = make_action(ActionKind::DamageRandomEnemy);
        a.amount = juggernaut;
        q.push_back(a);
      }
      break;
    case Hook::HpLostPlayer:
      // Rupture: only self-inflicted HP loss reaches this hook (it fires from
      // the LoseHp executor; enemy attack damage goes through DealDamage).
      if (rupture > 0) push_player_strength(q, rupture);
      break;
    case Hook::CardDrawn: {
      const CardType type = CARD_DATABASE.at(card).type;
      // Evolve: Status only. Fire Breathing: Status AND Curse.
      if (evolve > 0 && type == CardType::Status) push_draw(q, evolve);
      if (fire_breathing > 0 &&
          (type == CardType::Status || type == CardType::Curse)) {
        Action a = make_action(ActionKind::DamageAllEnemies);
        a.amount = fire_breathing;
        q.push_back(a);
      }
      break;
    }
    case Hook::PlayerAttacked:
      // Flame Barrier retaliates against THE ATTACKER (`attacker_slot`) on
      // every attack, whether or not the damage got through block.
      if (flame_barrier > 0 && attacker_slot >= 0) {
        Action a = make_action(ActionKind::DealFixedDamage);
        a.target = attacker_slot;
        a.amount = flame_barrier;
        q.push_back(a);
      }
      break;
    case Hook::TurnStartEnemy:
    case Hook::EnemyDamaged:
    case Hook::OnAnyDamage:
    case Hook::EnemyHpThreshold:
    case Hook::EnemyDeath:
    case Hook::EnemyWake:
    case Hook::BecameLastEnemy:
      break;  // enemy-side hooks
  }
}

// ---------------------------------------------------------------------------
// Executors + drain
// ---------------------------------------------------------------------------

namespace {

bool valid_enemy_slot(const CombatState& state, int slot) {
  return slot >= 0 && slot < static_cast<int>(state.enemies.size());
}

// Add a card to the hand, overflowing to the DISCARD pile if the hand is full
// (StS: "if a copy surpasses the hand size limit, it goes to the discard pile"
// — verified for Dual Wield; the same limit applies to Exhume).
void add_card_to_hand(CombatState& state, const Card& card) {
  if (static_cast<int>(state.current_hand.size()) >= HAND_SIZE_LIMIT) {
    move_to_discard(state, card);
  } else {
    state.current_hand.push_back(card);
  }
}

// Remove one copy MATCHING `card` (id and instance state) from a pile.
// Returns false if absent. Matching the instance matters once two copies of a
// card can differ — taking an arbitrary one would be the wrong card.
bool take_from_pile(std::vector<Card>& pile, const Card& card) {
  for (auto it = pile.begin(); it != pile.end(); ++it) {
    if (it->same_as(card)) {
      pile.erase(it);
      return true;
    }
  }
  return false;
}

// Apply fixed (thorns-type) damage to one enemy: no Strength/Weak/Vulnerable
// modifiers, but block still absorbs it (verified: the wiki speaks of
// "unblocked damage" from such sources). Fires the ANY-damage hook family —
// the HP threshold interrupt, Lagavulin's wake — but NOT Hook::EnemyDamaged,
// whose listeners (Curl Up, Angry) are attack-only in StS.
void apply_fixed_damage(CombatState& state, int slot, int amount,
                        ActionQueue& q, ResolutionContext& ctx) {
  if (!valid_enemy_slot(state, slot) || amount <= 0) return;
  Enemy& e = state.enemies[slot];
  if (e.hp <= 0) return;
  const int hp_before = e.hp;
  apply_damage_to_hp_block(e.hp, e.current_block, amount);
  if (e.hp < hp_before) {
    const bool was_asleep = e.is_asleep;
    fire_enemy_hooks(state, slot, Hook::OnAnyDamage, q);
    if (was_asleep) fire_enemy_hooks(state, slot, Hook::EnemyWake, q);
    fire_enemy_hooks(state, slot, Hook::EnemyHpThreshold, q);
  }
  if (hp_before > 0 && state.enemies[slot].hp <= 0) ctx.record_death(slot);
}

// One player attack landing on one enemy slot: the shared damage path for
// targeted attacks and for Sword Boomerang's random hits.
void player_attack_enemy(CombatState& state, int slot, int base,
                         int strength_mult, ActionQueue& q,
                         ResolutionContext& ctx) {
  if (!valid_enemy_slot(state, slot)) return;
  if (state.enemies[slot].hp <= 0) return;
  const int hp_before = state.enemies[slot].hp;
  const int dmg = compute_attack_damage(base, state.character.powers,
                                        state.character.debuffs,
                                        state.enemies[slot].debuffs,
                                        strength_mult);
  apply_damage_to_hp_block(state.enemies[slot].hp,
                           state.enemies[slot].current_block, dmg);
  if (state.enemies[slot].hp < hp_before) {
    // The on-damaged hook family. OnDamaged first: the damage-wake
    // RewriteIntent (guarded on is_asleep) sets the Stunned intent while still
    // asleep; then the wake itself; then HP-threshold interrupts.
    const bool was_asleep = state.enemies[slot].is_asleep;
    fire_enemy_hooks(state, slot, Hook::EnemyDamaged, q);
    fire_enemy_hooks(state, slot, Hook::OnAnyDamage, q);
    if (was_asleep) fire_enemy_hooks(state, slot, Hook::EnemyWake, q);
    fire_enemy_hooks(state, slot, Hook::EnemyHpThreshold, q);
  }
  if (hp_before > 0 && state.enemies[slot].hp <= 0) ctx.record_death(slot);
}

// A uniformly-random living enemy slot, or -1 if none. Consumes RNG only when
// there is a choice to make.
int pick_random_living_enemy(CombatState& state) {
  std::vector<int> living;
  for (std::size_t i = 0; i < state.enemies.size(); ++i) {
    if (state.enemies[i].hp > 0) living.push_back(static_cast<int>(i));
  }
  if (living.empty()) return -1;
  std::uniform_int_distribution<int> pick(
      0, static_cast<int>(living.size()) - 1);
  return living[pick(state.rng)];
}

void execute(CombatState& state, const Action& a, ActionQueue& q,
             ResolutionContext& ctx) {
  switch (a.kind) {
    case ActionKind::DealDamage: {
      // One hit, either direction. Damage is computed at execution time —
      // Strength applies per hit (TwinStrike + Flex parity), and an enemy's
      // start-of-turn Ritual Strength (queued ahead of its attack) is visible.
      if (a.target == kPlayerSlot) {
        // Enemy -> player. Note this does NOT fire HpLostPlayer: Rupture keys
        // on self-inflicted HP loss only, never on enemy damage.
        if (!valid_enemy_slot(state, a.actor)) break;
        const int dmg = compute_attack_damage(
            a.amount, state.enemies[a.actor].powers,
            state.enemies[a.actor].debuffs, state.character.debuffs);
        const int hp_before = state.character.hp;
        apply_damage_to_hp_block(state.character.hp,
                                 state.character.current_block, dmg);
        // Blood for Blood counts HP-loss events from ANY source, so unblocked
        // enemy damage counts too (Rupture, by contrast, does not fire here).
        if (state.character.hp < hp_before) state.character.hp_loss_events += 1;
        // Flame Barrier retaliates on being attacked, even if fully blocked.
        fire_player_power_hooks(state, Hook::PlayerAttacked, q, a.card,
                                a.actor);
        break;
      }
      // Player -> enemy.
      player_attack_enemy(state, a.target, a.amount, a.strength_mult, q, ctx);
      break;
    }
    case ActionKind::DamageRandomEnemyAttack: {
      // Sword Boomerang: each hit rolls its own target at EXECUTION time, so a
      // hit that kills an enemy changes the pool for the next hit.
      const int slot = pick_random_living_enemy(state);
      if (slot >= 0) {
        player_attack_enemy(state, slot, a.amount, a.strength_mult, q, ctx);
      }
      break;
    }
    case ActionKind::DealFixedDamage:
      if (a.target == kPlayerSlot) {
        // Fixed damage TO the player (Burn's end-of-turn tick). Blockable,
        // and it is damage rather than HP loss, so Rupture does not fire.
        if (a.amount > 0) {
          const int hp_before = state.character.hp;
          apply_damage_to_hp_block(state.character.hp,
                                   state.character.current_block, a.amount);
          if (state.character.hp < hp_before) {
            state.character.hp_loss_events += 1;  // Blood for Blood counts it
          }
        }
        break;
      }
      apply_fixed_damage(state, a.target, a.amount, q, ctx);
      break;
    case ActionKind::DamageAllEnemies: {
      // Expand at execution so the target set is current (an earlier action in
      // this same drain may have killed or spawned an enemy).
      for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        if (state.enemies[i].hp > 0) {
          apply_fixed_damage(state, static_cast<int>(i), a.amount, q, ctx);
        }
      }
      break;
    }
    case ActionKind::DamageRandomEnemy: {
      // Juggernaut: a fresh uniform roll per trigger, at execution time.
      std::vector<int> living;
      for (std::size_t i = 0; i < state.enemies.size(); ++i) {
        if (state.enemies[i].hp > 0) living.push_back(static_cast<int>(i));
      }
      if (living.empty()) break;
      std::uniform_int_distribution<int> pick(
          0, static_cast<int>(living.size()) - 1);
      apply_fixed_damage(state, living[pick(state.rng)], a.amount, q, ctx);
      break;
    }
    case ActionKind::LoseHp:
      if (a.amount > 0) {
        lose_player_hp(state, a.amount);
        // Blood for Blood counts HP-loss EVENTS from any source (Stage 4b).
        state.character.hp_loss_events += 1;
        // Rupture: HP lost from a card or power (never from enemy damage).
        fire_player_power_hooks(state, Hook::HpLostPlayer, q);
      }
      break;
    case ActionKind::GainBlock: {
      int amount = a.amount;
      if (a.card_block) {
        // Card block math (Dexterity adds, then Frail reduces 25%, floored —
        // StS order). Applies ONLY to block gained from cards.
        amount += get_status(state.character.powers, Power::Dexterity);
        if (get_status(state.character.debuffs, Debuff::Frail) > 0) {
          amount = static_cast<int>(
              std::floor(static_cast<float>(amount) * 0.75f));
        }
      }
      gain_block(state, a.target, amount);
      // Juggernaut: whenever the PLAYER gains block, from any source.
      if (a.target == kPlayerSlot && amount > 0) {
        fire_player_power_hooks(state, Hook::BlockGainedPlayer, q);
      }
      break;
    }
    case ActionKind::GainEnergy:
      gain_energy(state, a.amount);
      break;
    case ActionKind::DrawCards:
      // Battle Trance forbids further draws this turn (query, not a hook).
      if (!can_draw(state)) break;
      for (int i = 0; i < a.amount; ++i) {
        const std::optional<CardId> drawn = draw_one(state);
        // Evolve / Fire Breathing key on the drawn card's type.
        if (drawn.has_value()) {
          fire_player_power_hooks(state, Hook::CardDrawn, q, *drawn);
        }
      }
      break;
    case ActionKind::ApplyDebuff:
      if (a.target == kPlayerSlot) {
        apply_debuff(state, DebuffApplication{a.debuff, a.amount,
                                              Target::Character}, kNoSlot);
      } else {
        apply_debuff(state, DebuffApplication{a.debuff, a.amount,
                                              Target::Enemy}, a.target);
      }
      break;
    case ActionKind::ApplyPower:
      if (a.target == kPlayerSlot) {
        apply_power(state, PowerApplication{a.power, a.amount,
                                            Target::Character}, kNoSlot);
        // Combust's second counter: stacks hold the accumulated damage, so the
        // per-cast 1 HP loss is counted here (Combust + Combust+ = 2 HP, 12 dmg).
        if (a.power == Power::Combust) state.character.combust_casts += 1;
      } else {
        apply_power(state, PowerApplication{a.power, a.amount, Target::Enemy},
                    a.target);
      }
      break;
    case ActionKind::RemovePower:
      // Either side: enemy (Lagavulin dropping Metallicize on wake) or player
      // (Rage / Flame Barrier expiring at their turn boundary).
      if (a.target == kPlayerSlot) {
        state.character.powers.erase(a.power);
      } else if (valid_enemy_slot(state, a.target)) {
        state.enemies[a.target].powers.erase(a.power);
      }
      break;
    case ActionKind::RewriteIntent:
      // Only meaningful for a living enemy (a dead one takes no turn).
      if (valid_enemy_slot(state, a.target) &&
          state.enemies[a.target].hp > 0) {
        state.enemies[a.target].last_move = a.move;
      }
      break;
    case ActionKind::Wake:
      if (valid_enemy_slot(state, a.target)) {
        state.enemies[a.target].is_asleep = false;
      }
      break;
    case ActionKind::ExhaustCard: {
      move_to_exhaust(state, a.as_card());
      // Sentinel: energy when THIS card is exhausted — by any means, and
      // notably not by being played (playing it discards instead). Corruption,
      // True Grit and Fiend Fire are the usual triggers.
      const int energy = CARD_DATABASE.at(a.card).energy_when_exhausted;
      if (energy > 0) gain_energy(state, energy);
      // Feel No Pain / Dark Embrace: whenever a card is exhausted.
      fire_player_power_hooks(state, Hook::CardExhausted, q, a.card);
      break;
    }
    case ActionKind::DiscardCard:
      move_to_discard(state, a.as_card());
      break;
    case ActionKind::MultiplyStrength: {
      // Limit Break. Multiplying keeps the sign, so a negative Strength
      // (Disarm, Siphon Soul) doubles into a worse debuff — faithful to StS.
      const int str = get_status(state.character.powers, Power::Strength);
      if (str != 0) {
        state.character.powers[Power::Strength] = str * a.amount;
      }
      break;
    }
    case ActionKind::AddCardToPile: {
      // A generated card is always fresh (no inherited instance state), except
      // Anger's self-copy, which carries the played copy's state.
      const Card made = a.as_card();
      switch (static_cast<GeneratedPile>(a.amount)) {
        case GeneratedPile::Discard:
          move_to_discard(state, made);
          break;
        case GeneratedPile::Hand:
          add_card_to_hand(state, made);
          break;
        case GeneratedPile::ShuffleDraw: {
          // SHUFFLE into the draw pile: insert at a uniformly random position
          // so it isn't deterministically the next draw. Consumes RNG only
          // when such a card is actually generated.
          std::uniform_int_distribution<std::size_t> pos(
              0, state.draw_pile.size());
          state.draw_pile.insert(state.draw_pile.begin() + pos(state.rng),
                                 made);
          break;
        }
      }
      break;
    }
    case ActionKind::EnemyEscape:
      // Escape (ROB-74): the enemy flees by setting its own hp to 0. It leaves
      // the fight — everything keys on hp>0, so it's no longer targetable or
      // acting and its slot frees. NOT a death: on-death hooks don't fire and
      // no death is recorded (a fleeing enemy doesn't split/spore).
      // check_enemy_terminal treats it as gone -> Won if it was the last one.
      if (valid_enemy_slot(state, a.target)) {
        state.enemies[a.target].hp = 0;
      }
      break;
    case ActionKind::EnemySplit: {
      // Split (ROB-64): the enemy dies and spawns its children, each set to
      // the parent's CURRENT HP as both current and max — they aren't "real"
      // Mediums with rolled HP (verified faithful). Children are copied out
      // and the parent killed BEFORE placement: place_child may reallocate
      // state.enemies, and a dead parent's slot is a free slot a child can
      // reuse. As a flat executor step, no reference spans the reallocation.
      // on-death hooks are not fired (split is its own mechanic).
      if (!valid_enemy_slot(state, a.target)) break;
      const int inherited_hp = state.enemies[a.target].hp;
      std::vector<Enemy> children = state.enemies[a.target].split_children;
      state.enemies[a.target].hp = 0;  // parent dies
      for (Enemy child : children) {
        child.hp = inherited_hp;
        child.max_hp = inherited_hp;
        place_child(state, child);
      }
      break;
    }
    case ActionKind::CardPlayedHook:
      // Player powers first (Rage: block when an Attack is played), then the
      // enemy side: playing a Skill fires every living enemy's OnPlayerSkill
      // effects (the Gremlin Nob's Enrage), independent of whether the card
      // dealt damage or killed anything (ROB-65). Fires mid-drain, after the
      // card's own effects — the pre-queue 5b position.
      fire_player_power_hooks(state, Hook::CardPlayed, q, a.card);
      if (CARD_DATABASE.at(a.card).type == CardType::Skill) {
        for (std::size_t i = 0; i < state.enemies.size(); ++i) {
          if (state.enemies[i].hp > 0) {
            fire_enemy_hooks(state, static_cast<int>(i), Hook::CardPlayed, q);
          }
        }
      }
      break;
    case ActionKind::RequestChoice: {
      // Build the candidate list. If nothing qualifies, the choice is simply
      // skipped — StS plays the card, the choice just has no legal target
      // (e.g. Exhume with an empty exhaust pile). No pause, drain continues.
      PendingChoice pc = build_choice(state, static_cast<ChoiceKind>(a.amount),
                                      a.card);
      pc.copies = CARD_DATABASE.at(a.card).choice_copies;  // Dual Wield+ = 2
      if (pc.num_options == 0) break;
      if (pc.num_options == 1 && !pc.is_optional) {
        // Exactly one legal option: StS applies it without prompting ("if
        // there is only one card in your discard pile, it will automatically
        // be placed on top of your draw pile"). No pause — a choice with one
        // answer has no decision content, and pausing would cost the agent a
        // step whose mask has a single legal action.
        Action apply;
        apply.kind = ActionKind::ApplyChoice;
        apply.card = pc.options[0].card_id;
        apply.card_bonus_damage = pc.options[0].bonus_damage;
        apply.card_upgrades = pc.options[0].upgrades;
        apply.amount = a.amount;
        apply.copies = pc.copies;
        q.push_front(apply);
        break;
      }
      state.pending_choice = pc;
      // The drain loop sees the active choice and suspends the remainder.
      break;
    }
    case ActionKind::ApplyChoice: {
      const ChoiceKind kind = static_cast<ChoiceKind>(a.amount);
      // The chosen INSTANCE: matching on the id alone would grab an arbitrary
      // copy, which is wrong once two Rampages differ.
      const Card chosen = a.as_card();
      switch (kind) {
        case ChoiceKind::UpgradeCardInHand:
          // Armaments: upgrade one matching copy in hand, in place (Searing
          // Blow bumps its counter rather than swapping id).
          for (Card& c : state.current_hand) {
            if (c.same_as(chosen)) {
              upgrade_card_in_place(c);
              break;
            }
          }
          break;
        case ChoiceKind::HandToTopOfDraw:
          // Warcry: hand -> top of draw. `back()` is the top (draw_one pops it).
          if (take_from_pile(state.current_hand, chosen)) {
            state.draw_pile.push_back(chosen);
          }
          break;
        case ChoiceKind::DiscardToTopOfDraw:
          // Headbutt: discard -> top of draw.
          if (take_from_pile(state.discard_pile, chosen)) {
            state.draw_pile.push_back(chosen);
          }
          break;
        case ChoiceKind::ExhaustToHand:
          // Exhume: exhaust -> hand. The exhaust pile only grows otherwise
          // (Rob's invariant), and this is the one sanctioned removal.
          if (take_from_pile(state.exhaust_pile, chosen)) {
            add_card_to_hand(state, chosen);
          }
          break;
        case ChoiceKind::CopyAttackOrPowerInHand:
          // Dual Wield: ADD copies; the original stays in hand. The + adds 2.
          // Copies inherit the instance state (a copied +10 Rampage is +10).
          for (int i = 0; i < a.copies; ++i) add_card_to_hand(state, chosen);
          break;
        case ChoiceKind::ExhaustCardInHand:
          // Burning Pact / True Grit+: exhaust the chosen card. Queued as an
          // action so Feel No Pain, Dark Embrace and Sentinel all see it.
          if (take_from_pile(state.current_hand, chosen)) {
            Action ex = make_action(ActionKind::ExhaustCard);
            ex.card = chosen.card_id;
            ex.card_bonus_damage = chosen.bonus_damage;
            ex.card_upgrades = chosen.upgrades;
            q.push_front(ex);
          }
          break;
        case ChoiceKind::None:
          break;
      }
      break;
    }
    case ActionKind::DiscardHand:
      // End of the player's turn: unplayed Ethereal cards exhaust (ROB-65
      // Dazed), the rest discard. Routed through the executors so an ethereal
      // exhaust is seen by Feel No Pain / Dark Embrace — StS handles the hand
      // before end-of-turn powers, so those responses queue ahead of Combust.
      for (const Card& c : state.current_hand) {
        const CardData& cd = CARD_DATABASE.at(c.card_id);
        // Burn: damage for sitting in hand at end of turn. Queued BEFORE the
        // card leaves, and as DealFixedDamage so block absorbs it (StS calls
        // it damage, not HP loss — "unblocked damage from Burn").
        if (cd.end_of_turn_damage_in_hand > 0) {
          Action burn = make_action(ActionKind::DealFixedDamage);
          burn.target = kPlayerSlot;
          burn.amount = cd.end_of_turn_damage_in_hand;
          q.push_back(burn);
        }
        Action move = make_action(cd.ethereal ? ActionKind::ExhaustCard
                                              : ActionKind::DiscardCard);
        move.card = c.card_id;
        move.card_bonus_damage = c.bonus_damage;
        move.card_upgrades = c.upgrades;
        q.push_back(move);
      }
      state.current_hand.clear();
      break;
    case ActionKind::CheckDeath:
      // Deferred death processing (ROB-62): on-death hooks fire after the
      // card's damage fully resolves. Then, if the kills left exactly one
      // living enemy, fire its BecameLastEnemy effects (ROB-77) — checked once
      // per resolution, not per death, so it can't double-fire.
      for (int i = 0; i < ctx.died_count; ++i) {
        fire_enemy_hooks(state, ctx.died_slots[i], Hook::EnemyDeath, q);
      }
      if (ctx.died_count > 0 && count_living(state) == 1) {
        for (std::size_t i = 0; i < state.enemies.size(); ++i) {
          if (state.enemies[i].hp > 0) {
            fire_enemy_hooks(state, static_cast<int>(i),
                             Hook::BecameLastEnemy, q);
          }
        }
      }
      break;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Mid-card choices (Stage 4c)
// ---------------------------------------------------------------------------

namespace {

// Does this card qualify as an option for `kind`?
bool card_qualifies(ChoiceKind kind, CardId id) {
  const CardData& d = CARD_DATABASE.at(id);
  switch (kind) {
    case ChoiceKind::UpgradeCardInHand:
      return is_upgradable(id);  // already-upgraded and Status cards excluded
    case ChoiceKind::CopyAttackOrPowerInHand:
      return d.type == CardType::Attack || d.type == CardType::Power;
    case ChoiceKind::HandToTopOfDraw:
    case ChoiceKind::DiscardToTopOfDraw:
    case ChoiceKind::ExhaustToHand:
    case ChoiceKind::ExhaustCardInHand:
      return true;  // any card in the source pile
    case ChoiceKind::None:
      return false;
  }
  return false;
}

// Which pile does this choice draw its options from?
const std::vector<Card>& source_pile(const CombatState& state,
                                     ChoiceKind kind) {
  switch (kind) {
    case ChoiceKind::DiscardToTopOfDraw: return state.discard_pile;
    case ChoiceKind::ExhaustToHand:      return state.exhaust_pile;
    case ChoiceKind::UpgradeCardInHand:
    case ChoiceKind::HandToTopOfDraw:
    case ChoiceKind::CopyAttackOrPowerInHand:
    case ChoiceKind::ExhaustCardInHand:
    case ChoiceKind::None:
      break;
  }
  return state.current_hand;
}

}  // namespace

PendingChoice build_choice(const CombatState& state, ChoiceKind kind,
                           CardId source_card) {
  PendingChoice pc;
  pc.kind = kind;
  pc.source_card = source_card;
  if (kind == ChoiceKind::None) return pc;

  // Dedupe INTERCHANGEABLE copies. Two cards collapse into one option only if
  // they play identically — same id AND same instance state — so a Rampage at
  // +10 and one at +0 are two distinct, separately selectable options.
  for (const Card& c : source_pile(state, kind)) {
    if (!card_qualifies(kind, c.card_id)) continue;
    bool already = false;
    for (int i = 0; i < pc.num_options; ++i) {
      if (pc.options[i].same_as(c)) {
        already = true;
        break;
      }
    }
    if (already) continue;
    assert(pc.num_options < kNumOptionSlots);
    pc.options[pc.num_options++] = c;
  }
  // Canonical ordering (public interface: slot indices are actions, so it must
  // be deterministic and documented): ascending CardId, then ascending
  // instance state so two copies of one card have a stable relative order.
  std::sort(pc.options.begin(), pc.options.begin() + pc.num_options,
            [](const Card& a, const Card& b) {
              if (a.card_id != b.card_id) return a.card_id < b.card_id;
              if (a.upgrades != b.upgrades) return a.upgrades < b.upgrades;
              return a.bonus_damage < b.bonus_damage;
            });
  return pc;
}

bool resolve_choice(CombatState& state, int option_index) {
  PendingChoice& pc = state.pending_choice;
  if (!pc.active()) return false;

  // Validate before mutating, so a bad index can't corrupt a paused state.
  const bool declining = option_index == kDeclineChoice;
  if (declining) {
    if (!pc.is_optional) return false;
  } else if (option_index < 0 || option_index >= pc.num_options) {
    return false;
  }

  const ChoiceKind kind = pc.kind;
  const int copies = pc.copies;
  const Card chosen =
      declining ? Card{CardId::Strike} : pc.options[option_index];

  // Clear the pause BEFORE resuming: the resumed drain may itself request
  // another choice (v2's nested rest site), which needs a clean slot.
  pc = PendingChoice{};

  ActionQueue q = state.suspended_queue;
  state.suspended_queue = ActionQueue{};

  if (!declining) {
    Action a;
    a.kind = ActionKind::ApplyChoice;
    a.card = chosen.card_id;
    a.card_bonus_damage = chosen.bonus_damage;
    a.card_upgrades = chosen.upgrades;
    a.amount = static_cast<int>(kind);
    a.copies = copies;
    q.push_front(a);  // the choice applies before the card's remaining actions
  }

  ResolutionContext ctx;
  drain(state, q, ctx);
  return true;
}

void drain(CombatState& state, ActionQueue& q, ResolutionContext& ctx) {
  while (!q.empty()) {
    Action a = q.pop_front();
    execute(state, a, q, ctx);
    if (state.outcome != Outcome::InProgress) {
      // Terminal short-circuit. A fight that ended mid-resolution discards any
      // pending choice: death takes precedence, and answering a choice on a
      // finished fight is meaningless.
      state.pending_choice = PendingChoice{};
      state.suspended_queue = ActionQueue{};
      return;
    }
    if (state.pending_choice.active()) {
      // A RequestChoice executor armed a pause. Park the not-yet-executed
      // remainder; resolve_choice() resumes exactly here (Stage 4c).
      state.suspended_queue = q;
      return;
    }
  }
}

}  // namespace minispire
