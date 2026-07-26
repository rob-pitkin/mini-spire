#include "action.h"

#include <algorithm>
#include <cmath>

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

// ---------------------------------------------------------------------------
// Executors + drain
// ---------------------------------------------------------------------------

namespace {

bool valid_enemy_slot(const CombatState& state, int slot) {
  return slot >= 0 && slot < static_cast<int>(state.enemies.size());
}

void execute(CombatState& state, const Action& a, ActionQueue& q,
             ResolutionContext& ctx) {
  switch (a.kind) {
    case ActionKind::DealDamage: {
      // One hit on one enemy (Stage 2: actor is always the player; enemy
      // attacks migrate here at Stage 3). Damage is computed at execution
      // time — Strength applies per hit (TwinStrike + Flex parity).
      if (!valid_enemy_slot(state, a.target)) break;
      if (state.enemies[a.target].hp <= 0) break;
      const int hp_before = state.enemies[a.target].hp;
      const int dmg =
          compute_attack_damage(a.amount, state.character.powers,
                                state.character.debuffs,
                                state.enemies[a.target].debuffs);
      apply_damage_to_hp_block(state.enemies[a.target].hp,
                               state.enemies[a.target].current_block, dmg);
      if (state.enemies[a.target].hp < hp_before) {
        // The on-damaged hook family. OnDamaged first: the damage-wake
        // RewriteIntent (guarded on is_asleep) sets the Stunned intent while
        // still asleep; then the wake itself; then HP-threshold interrupts.
        const bool was_asleep = state.enemies[a.target].is_asleep;
        fire_enemy_hooks(state, a.target, Hook::EnemyDamaged, q);
        if (was_asleep) fire_enemy_hooks(state, a.target, Hook::EnemyWake, q);
        fire_enemy_hooks(state, a.target, Hook::EnemyHpThreshold, q);
      }
      if (hp_before > 0 && state.enemies[a.target].hp <= 0) {
        ctx.record_death(a.target);
      }
      break;
    }
    case ActionKind::LoseHp:
      lose_player_hp(state, a.amount);
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
      break;
    }
    case ActionKind::GainEnergy:
      gain_energy(state, a.amount);
      break;
    case ActionKind::DrawCards:
      for (int i = 0; i < a.amount; ++i) draw_one(state);
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
      } else {
        apply_power(state, PowerApplication{a.power, a.amount, Target::Enemy},
                    a.target);
      }
      break;
    case ActionKind::RemovePower:
      if (valid_enemy_slot(state, a.target)) {
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
    case ActionKind::ExhaustCard:
      move_to_exhaust(state, Card{a.card});
      break;
    case ActionKind::DiscardCard:
      move_to_discard(state, Card{a.card});
      break;
    case ActionKind::CardPlayedHook:
      // Playing a Skill fires every living enemy's OnPlayerSkill effects (the
      // Gremlin Nob's Enrage), independent of whether the card dealt damage or
      // killed anything (ROB-65). Fires mid-drain, after the card's own
      // effects — the pre-queue 5b position.
      if (CARD_DATABASE.at(a.card).type == CardType::Skill) {
        for (std::size_t i = 0; i < state.enemies.size(); ++i) {
          if (state.enemies[i].hp > 0) {
            fire_enemy_hooks(state, static_cast<int>(i), Hook::CardPlayed, q);
          }
        }
      }
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

void drain(CombatState& state, ActionQueue& q, ResolutionContext& ctx) {
  while (!q.empty()) {
    Action a = q.pop_front();
    execute(state, a, q, ctx);
    if (state.outcome != Outcome::InProgress) return;  // terminal short-circuit
  }
}

}  // namespace minispire
