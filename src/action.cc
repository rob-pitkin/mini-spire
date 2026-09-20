#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "action.h"
#include "action_types.h"
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

// Buffer: prevent the next `stacks` times you would LOSE HP. Spends one stack,
// and only when HP would actually be lost — a fully blocked hit or a 0-damage
// attack spends nothing, which the wiki calls out as a deliberate fix.
//
// Returns true if the loss was absorbed.
bool buffer_absorbs_hp_loss(CombatState& state) {
  auto it = state.character.powers.find(Power::Buffer);
  if (it == state.character.powers.end() || it->second <= 0) return false;
  if (--it->second <= 0) state.character.powers.erase(it);
  return true;
}

// Intangible caps ANY incoming damage or HP loss at 1. Applied BEFORE block, so
// 20 damage into 5 block becomes 1 damage that the block then absorbs entirely
// — the cap is on the incoming number, not on what reaches HP.
int intangible_capped(const CombatState& state, int amount) {
  if (amount <= 0) return amount;
  if (get_status(state.character.powers, Power::Intangible) <= 0) return amount;
  return 1;
}

void lose_player_hp(CombatState& state, int amount) {
  if (amount <= 0) return;
  amount = intangible_capped(state, amount);
  if (buffer_absorbs_hp_loss(state)) return;
  state.character.hp -= amount;
  if (state.character.hp < 0) state.character.hp = 0;
}

// Damage TO the player: block absorbs first, then Buffer eats whatever would
// have reached HP. Block is still spent either way.
//
// The single path for damage aimed at the player, so a new damage source cannot
// quietly skip Buffer — there are three call sites (enemy attacks, fixed damage
// like Burn, and direct HP loss) and getting one of them wrong is invisible
// until the relic exists.
//
// Returns true if HP was actually lost.
bool damage_player(CombatState& state, int amount) {
  if (amount <= 0) return false;
  // Intangible first: it caps the incoming number, and block then absorbs the
  // capped 1. Capping AFTER block would let a 20-damage hit chew through 20
  // block before being reduced.
  amount = intangible_capped(state, amount);
  const int blocked = std::min(amount, state.character.current_block);
  state.character.current_block -= blocked;
  const int to_hp = amount - blocked;
  if (to_hp <= 0) return false;  // fully blocked: Buffer is not spent

  // Plated Armor loses a stack on receiving UNBLOCKED damage — which is what
  // getting past block means, so it is decided here rather than at the HP
  // write. Deliberately before Buffer: the damage was still unblocked, and
  // Buffer preventing the HP loss does not un-receive it.
  {
    auto it = state.character.powers.find(Power::PlatedArmor);
    if (it != state.character.powers.end() && it->second > 0) {
      if (--it->second <= 0) state.character.powers.erase(it);
    }
  }

  if (buffer_absorbs_hp_loss(state)) return false;
  state.character.hp -= to_hp;
  if (state.character.hp < 0) state.character.hp = 0;
  return true;
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

void heal_player(CombatState& state, int amount) {
  if (amount <= 0 || state.character.hp <= 0) return;
  state.character.hp += amount;
  if (state.character.hp > state.character.max_hp) {
    state.character.hp = state.character.max_hp;
  }
}

void gain_max_hp(CombatState& state, int amount) {
  if (amount <= 0) return;
  // StS raises current HP alongside max HP, so Feed on a hurt player is a
  // real heal as well as a cap increase.
  state.character.max_hp += amount;
  state.character.hp += amount;
}

void move_to_exhaust(CombatState& state, Card card) {
  state.exhaust_pile.push_back(card);
}

void move_to_discard(CombatState& state, Card card) {
  state.discard_pile.push_back(card);
}

void add_card_to_hand(CombatState& state, const Card& card) {
  if (static_cast<int>(state.current_hand.size()) >= HAND_SIZE_LIMIT) {
    move_to_discard(state, card);
  } else {
    state.current_hand.push_back(card);
  }
}

void reshuffle_discard_into_draw(CombatState& state, ActionQueue& q) {
  if (state.discard_pile.empty()) return;
  state.draw_pile.insert(state.draw_pile.end(), state.discard_pile.begin(),
                         state.discard_pile.end());
  state.discard_pile.clear();
  std::shuffle(state.draw_pile.begin(), state.draw_pile.end(), state.rng);
  // Fires the shuffle hook: Sundial counts shuffles, The Abacus gains block on
  // each one. All three reshuffle sites call this — the draw-dry reshuffle,
  // Deep Breath, and Havoc/Mayhem's — so none of them can skip the hook.
  //
  // The combat-start shuffle does not call this. StS shuffles the opening draw
  // pile in CardGroup.initializeDeck, which does not run the relic loop, so
  // Sundial and The Abacus do not fire on turn 1.
  fire_relic_hooks(state, Hook::ShuffleDrawPile, q);
}

std::optional<CardId> draw_one(CombatState& state, ActionQueue& q) {
  if (state.draw_pile.empty()) {
    if (state.discard_pile.empty()) return std::nullopt;
    reshuffle_discard_into_draw(state, q);
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

bool apply_debuff(CombatState& state, const DebuffApplication& app,
                  int enemy_target) {
  auto* m = debuff_map(state, app.target, enemy_target);
  if (!m) return false;

  // Relic immunity (Ginger / Turnip) is checked BEFORE Artifact, and the order
  // is the whole point: the wiki states that a player holding Ginger who would
  // receive Weak keeps their Artifact charge. Putting this after the Artifact
  // block below — the natural place, since Artifact reads first — would spend a
  // charge negating a debuff that could never have landed.
  if (app.target == Target::Character &&
      player_is_immune_to(state, app.effect)) {
    return false;
  }

  // Artifact (ROB-65): negates the whole debuff APPLICATION regardless of
  // stacks, consuming one Artifact charge. Checked on the same target's powers.
  auto* pm = power_map(state, app.target, enemy_target);
  if (pm) {
    auto art = pm->find(Power::Artifact);
    if (art != pm->end() && art->second > 0) {
      if (--art->second <= 0) pm->erase(art);
      return false;  // debuff negated
    }
  }
  // Entangle is non-stacking: SET to the applied amount, not accumulated. It's
  // 1-turn and boolean (ROB-75); stacking to 2 would wrongly last two turns.
  if (app.effect == Debuff::Entangle) {
    (*m)[app.effect] = app.amount;
  } else {
    (*m)[app.effect] += app.amount;
  }
  return true;
}

bool apply_power(CombatState& state, const PowerApplication& app,
                 int enemy_target) {
  auto* m = power_map(state, app.target, enemy_target);
  if (!m) return false;
  // Artifact also negates NEGATIVE power applications, not just Debuff ones:
  // in StS a negative buff *is* a debuff, so Disarm's "Enemy loses 2 Strength"
  // is eaten by a Sentry's Artifact charge (Rob's ruling from play experience;
  // the wiki states Artifact "negates any debuff" and confirms Strength Down is
  // negatable, but does not name the direct-negative case).
  //
  // Gated on amount < 0 deliberately: a POSITIVE application must never consume
  // a charge, or an enemy's own Ritual/Enrage Strength would burn its Artifact.
  if (app.amount < 0) {
    auto art = m->find(Power::Artifact);
    if (art != m->end() && art->second > 0) {
      if (--art->second <= 0) m->erase(art);
      return false;  // application negated
    }
  }
  (*m)[app.effect] += app.amount;
  return true;
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

// Sadistic Nature: an enemy the player just debuffed takes fixed damage.
// Pushed, never applied here, and only for a debuff that LANDED — StS deals
// nothing when Artifact eats it.
void push_sadistic_nature(const CombatState& state, int enemy_slot,
                          ActionQueue& q) {
  const int damage = get_status(state.character.powers, Power::SadisticNature);
  if (damage <= 0) return;
  Action a = make_action(ActionKind::DealFixedDamage);
  a.actor = kPlayerSlot;
  a.target = enemy_slot;
  a.amount = damage;
  q.push_back(a);
}

// First slot not holding a living enemy (dead corpse OR empty), or -1 if all
// slots are occupied by the living. A split overwrites a corpse (ROB-61 rule
// A).
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
    case Hook::CardPlayed:
      which = Trigger::OnPlayerSkill;
      break;
    case Hook::EnemyDamaged:
      which = Trigger::OnDamaged;
      break;
    case Hook::OnAnyDamage:
      which = Trigger::OnAnyDamage;
      break;
    case Hook::EnemyHpThreshold:
      which = Trigger::HpAtOrBelow;
      break;
    case Hook::EnemyDeath:
      which = Trigger::OnDeath;
      break;
    case Hook::EnemyWake:
      which = Trigger::OnWake;
      break;
    case Hook::BecameLastEnemy:
      which = Trigger::BecameLastEnemy;
      break;
    case Hook::TurnStartPlayer:
    case Hook::TurnEndPlayer:
    case Hook::TurnStartEnemy:
    case Hook::TurnEndEnemy:
    case Hook::CardExhausted:
    case Hook::BlockGainedPlayer:
    case Hook::HpLostPlayer:
    case Hook::CardDrawn:
    case Hook::PlayerAttacked:
    // Relic hooks have no enemy-Trigger analog. Listed explicitly rather than
    // caught by a default so that adding a hook keeps failing to compile here
    // until someone decides whether enemies care about it.
    case Hook::CombatStartPreDraw:
    case Hook::CombatStart:
    case Hook::TurnStartPostDraw:
    case Hook::BlockBroken:
    case Hook::ShuffleDrawPile:
    case Hook::PotionDrunk:
    case Hook::CombatEnd:
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
  if (hook == Hook::TurnEndEnemy) {
    // Ritual: gain Strength = Ritual stacks (Cultist). It does NOT tick down.
    // Fires at the END of the bearer's turn, per the wiki ("At the end of its
    // turn, gains X Strength").
    //
    // This placement is load-bearing for the OBSERVATION, not for the damage
    // (ROB-85). Either placement yields the same Dark Strike sequence
    // 9/12/15..., because Incantation resolves during turn 1 and the gain lands
    // before turn 2's attack either way. But `intent_attack_dmg` is computed
    // live from the enemy's CURRENT Strength, so firing at turn start left the
    // player's whole turn showing a stale, understated intent — the agent saw
    // 6 and then took 9. End-of-turn makes the intent correct when it is read.
    const int ritual = get_status(state.enemies[slot].powers, Power::Ritual);
    if (ritual > 0) {
      Action a = make_action(ActionKind::ApplyPower);
      a.target = slot;
      a.power = Power::Strength;
      a.amount = ritual;
      q.push_back(a);
    }
    // Shackled (Dark Shackles): hand the Strength back at the end of the
    // bearer's turn, then remove the marker — StS's GainStrengthPower. The loss
    // therefore covers the enemy's own attack this round, which is the point of
    // the card.
    const int shackled =
        get_status(state.enemies[slot].powers, Power::Shackled);
    if (shackled > 0) {
      Action give_back = make_action(ActionKind::ApplyPower);
      give_back.target = slot;
      give_back.power = Power::Strength;
      give_back.amount = shackled;
      q.push_back(give_back);
      Action clear = make_action(ActionKind::RemovePower);
      clear.target = slot;
      clear.power = Power::Shackled;
      q.push_back(clear);
    }
    return;
  }

  if (hook != Hook::TurnStartEnemy) return;  // Stage 4 extends this registry

  // Metallicize: gain block = stacks at the start of the turn (ROB-65,
  // Lagavulin asleep). Queued AFTER the phase-start block reset, so an asleep
  // enemy shows exactly its Metallicize amount each turn (no accumulation).
  //
  // StS's *printed* rule is end-of-turn ("At the end of your turn, gain N
  // Block"), and start-of-turn placement is a deliberate divergence — see
  // docs/design/ordering-notes.md §24. It reproduces StS's observable for both
  // of Lagavulin's wake paths, which end-of-turn placement does not: on a
  // self-wake the grant lands BEFORE OnWake strips the power (so turn 3 keeps
  // its 8 block), while a damage-wake strips the power during the player's turn
  // (so the next phase reads 0 and grants nothing).
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

void push_player_power(ActionQueue& q, Power p, int amount) {
  Action a = make_action(ActionKind::ApplyPower);
  a.target = kPlayerSlot;
  a.power = p;
  a.amount = amount;
  q.push_back(a);
}
void push_player_heal(ActionQueue& q, int amount) {
  Action a = make_action(ActionKind::Heal);
  a.amount = amount;
  q.push_back(a);
}
void push_player_energy(ActionQueue& q, int amount) {
  Action a = make_action(ActionKind::GainEnergy);
  a.amount = amount;
  q.push_back(a);
}
// Buff every LIVING enemy — Philosopher's Stone's drawback.
void push_power_all_enemies(CombatState& state, ActionQueue& q, Power p,
                            int amount) {
  for (int slot = 0; slot < static_cast<int>(state.enemies.size()); ++slot) {
    if (state.enemies[slot].hp <= 0) continue;
    Action a = make_action(ActionKind::ApplyPower);
    a.target = slot;
    a.power = p;
    a.amount = amount;
    q.push_back(a);
  }
}

// Debuff every LIVING enemy. Expanded here rather than at execution because the
// target set cannot change during a combat-start drain — nothing has acted yet.
void push_debuff_all_enemies(CombatState& state, ActionQueue& q, Debuff d,
                             int amount) {
  for (int slot = 0; slot < static_cast<int>(state.enemies.size()); ++slot) {
    if (state.enemies[slot].hp <= 0) continue;
    Action a = make_action(ActionKind::ApplyDebuff);
    a.target = slot;
    a.debuff = d;
    a.amount = amount;
    q.push_back(a);
  }
}

}  // namespace

namespace {

// The start of ONE player turn, for one relic.
//
// Shared by two call sites on purpose: the turn boundary fires it for turns 2+,
// and start_combat's CombatStart sub-phase fires it for turn 1 — because combat
// start IS turn 1's start. Counting turns in only one of those places is the
// bug this factoring exists to prevent, and it is exactly how Brimstone and Red
// Skull ended up misclassified (relic-effects.md §6.3): from the reference's
// single init call site, a per-turn effect and a once-per-fight effect are
// indistinguishable.
void fire_turn_start_relic(CombatState& state, HeldRelic& relic,
                           ActionQueue& q) {
  switch (relic.id) {
    case RelicId::Brimstone:
      // Every turn, not once per fight. The enemies' Strength is the drawback,
      // and it compounds for as long as the fight runs.
      push_player_power(q, Power::Strength, 2);
      push_power_all_enemies(state, q, Power::Strength, 1);
      break;

    case RelicId::IncenseBurner:
      // Same shape as Happy Flower: a run-scoped turn counter that fires on
      // reaching its threshold and resets. 1 Intangible, which then ticks away
      // at the end of that same turn — so it protects the enemy phase that
      // follows, which is the point of it.
      if (++relic.counter >= kIncenseBurnerTurns) {
        relic.counter = 0;
        push_player_power(q, Power::Intangible, 1);
      }
      break;

    case RelicId::HappyFlower:
      // The counter is incremented at the START of the player's turn and is NOT
      // reset between combats — so a counter left at 2 fires on the first turn
      // of the NEXT fight. That run-scoped behaviour is §3.3's whole purpose,
      // and it is why the counter lives on HeldRelic rather than being rebuilt
      // per fight.
      //
      // Mutating the counter here is trigger bookkeeping, not an effect: the
      // no-direct-mutation rule exists to stop a live reference spanning a
      // state change, and writing an int on an element already in hand cannot
      // reallocate or invalidate anything. The ENERGY still goes through the
      // queue.
      if (++relic.counter >= kHappyFlowerTurns) {
        relic.counter = 0;
        push_player_energy(q, 1);
      }
      break;

    default:
      break;
  }
}

}  // namespace

namespace {
// One relic's response to one hook. Extracted so the batched and sequential
// entry points cannot diverge — they differ only in when they drain.
void fire_one_relic(CombatState& state, HeldRelic& relic, Hook hook,
                    ActionQueue& q, int slot = kNoSlot);
void fire_card_played_relic(HeldRelic& relic, CardType type, ActionQueue& q);
}  // namespace

void fire_relic_card_played(CombatState& state, CardType type, ActionQueue& q) {
  for (HeldRelic& relic : state.relics) {
    fire_card_played_relic(relic, type, q);
  }
}

void fire_relic_hooks_sequentially(CombatState& state, Hook hook) {
  // Indexed rather than range-based: the drain can push actions that touch the
  // relic vector, and an iterator held across it would be the exact dangling
  // shape the action queue exists to prevent.
  for (size_t i = 0; i < state.relics.size(); ++i) {
    ActionQueue q;
    ResolutionContext ctx;
    fire_one_relic(state, state.relics[i], hook, q);
    drain(state, q, ctx);
  }
}

void fire_relic_hooks(CombatState& state, Hook hook, ActionQueue& q, int slot) {
  // ACQUISITION order — the order of state.relics, which is the order the
  // player picked them up and the order their relic bar shows. Unlike the
  // powers registry below (Power-enum order), this loop must not be sorted or
  // grouped by hook: doing so would silently change resolution order.
  for (HeldRelic& relic : state.relics) {
    fire_one_relic(state, relic, hook, q, slot);
  }
}

namespace {

// One relic's response to a card being played. Split out because the two
// counter kinds behave differently enough that inlining them into the main
// switch would obscure which is which.
void fire_card_played_relic(HeldRelic& relic, CardType type, ActionQueue& q) {
  // Counts a card of `wanted` type and reports whether the threshold was just
  // reached, resetting when it was.
  const auto counted = [&](CardType wanted, int threshold) {
    if (type != wanted) return false;
    if (++relic.counter < threshold) return false;
    relic.counter = 0;
    return true;
  };

  switch (relic.id) {
    // --- PER TURN: "3 Attacks in a SINGLE TURN". The counter is cleared at
    // every turn boundary, so three Attacks spread over three turns do nothing.
    case RelicId::Kunai:
      if (counted(CardType::Attack, kPerTurnCardRelicThreshold)) {
        push_player_power(q, Power::Dexterity, 1);
      }
      break;
    case RelicId::Shuriken:
      if (counted(CardType::Attack, kPerTurnCardRelicThreshold)) {
        push_player_power(q, Power::Strength, 1);
      }
      break;
    case RelicId::OrnamentalFan:
      if (counted(CardType::Attack, kPerTurnCardRelicThreshold)) {
        push_player_block(q, 4);
      }
      break;
    case RelicId::LetterOpener:
      if (counted(CardType::Skill, kPerTurnCardRelicThreshold)) {
        // NOT attack damage: the wiki states Strength, Vulnerable and The Boot
        // do not apply. DamageAllEnemies is the fixed-damage path, so that
        // holds by construction rather than by remembering to exclude them.
        Action a = make_action(ActionKind::DamageAllEnemies);
        a.amount = 5;
        q.push_back(a);
      }
      break;

    // --- PERSISTENT: the counter is NOT reset between turns or combats, so
    // progress accumulates across the whole run (§3.3).
    case RelicId::Nunchaku:
      if (counted(CardType::Attack, kPersistentCardRelicThreshold)) {
        push_player_energy(q, 1);
      }
      break;
    case RelicId::InkBottle:
      // Counts EVERY card played, not just Attacks — so it cannot use the
      // type-matching helper above.
      if (++relic.counter >= kPersistentCardRelicThreshold) {
        relic.counter = 0;
        push_draw(q, 1);
      }
      break;

    case RelicId::PenNib:
      // Every 10th Attack is doubled, and the counter persists across turns and
      // combats like Nunchaku's. The tenth Attack must be doubled ITSELF, not
      // the one after it — so the charge is granted on the ninth, ready for the
      // tenth to consume. Granting it on the tenth would boost the eleventh.
      if (type == CardType::Attack &&
          ++relic.counter >= kPersistentCardRelicThreshold - 1) {
        relic.counter = 0;
        push_player_power(q, Power::PenNibCharge, 1);
      }
      break;

    case RelicId::BirdFacedUrn:
      // No counter at all: every Power heals.
      if (type == CardType::Power) push_player_heal(q, 2);
      break;

    default:
      break;
  }
}

void fire_one_relic(CombatState& state, HeldRelic& relic, Hook hook,
                    ActionQueue& q, int slot) {
  {
    switch (hook) {
      case Hook::CombatStartPreDraw:
        // Toolbox: "choose 1 of 3 random Colorless cards and add the chosen
        // card into your hand". PRE-draw, and that is the point of the relic —
        // the choice is made before you know your opening hand, so it pauses
        // the whole combat-start sequence rather than resolving after it.
        if (relic.id == RelicId::Toolbox) {
          Action a = make_action(ActionKind::RequestChoice);
          a.amount = static_cast<int>(ChoiceKind::DiscoverColorlessCard);
          // No source CARD — the relic opened this menu. Set explicitly
          // because Action::card defaults to Strike, and the RequestChoice
          // executor copies it straight into PendingChoice::source_card.
          a.card = CardId::None;
          q.push_back(a);
        }
        break;

      case Hook::CombatStart:
        // Turn 1's start. Anything that fires every turn must fire here too and
        // exactly once — routed through the same function the turn boundary
        // uses, rather than duplicated, so the two can never drift.
        fire_turn_start_relic(state, relic, q);
        switch (relic.id) {
          case RelicId::Vajra:
            push_player_power(q, Power::Strength, 1);
            break;
          case RelicId::OddlySmoothStone:
            push_player_power(q, Power::Dexterity, 1);
            break;
          case RelicId::Anchor:
            push_player_block(q, 10);
            break;
          case RelicId::BloodVial:
            push_player_heal(q, 2);
            break;
          case RelicId::Lantern:
            push_player_energy(q, 1);
            break;
          case RelicId::BagOfMarbles:
            push_debuff_all_enemies(state, q, Debuff::Vulnerable, 1);
            break;

          // Philosopher's Stone's drawback, and the reason it is not free
          // energy: every enemy is permanently stronger.
          case RelicId::PhilosophersStone:
            push_power_all_enemies(state, q, Power::Strength, 1);
            break;

          // Gremlin Visage starts you Weakened. A drawback relic from the
          // Face Trader event, not a reward.
          case RelicId::GremlinVisage: {
            Action a = make_action(ActionKind::ApplyDebuff);
            a.target = kPlayerSlot;
            a.debuff = Debuff::Weak;
            a.amount = 1;
            q.push_back(a);
            break;
          }

          case RelicId::ClockworkSouvenir:
            push_player_power(q, Power::Artifact, 1);
            break;

          case RelicId::SlingOfCourage:
            // Elites only — the relic is worthless in a normal fight, which is
            // the whole trade.
            if (state.is_elite) push_player_power(q, Power::Strength, 2);
            break;

          case RelicId::DuVuDoll: {
            // +1 Strength per CURSE in the deck. Counted over the draw pile and
            // hand, which together are the whole deck at combat start — nothing
            // has been played or discarded yet.
            int curses = 0;
            for (const Card& c : state.draw_pile) {
              if (CARD_DATABASE.at(c.card_id).type == CardType::Curse) ++curses;
            }
            for (const Card& c : state.current_hand) {
              if (CARD_DATABASE.at(c.card_id).type == CardType::Curse) ++curses;
            }
            if (curses > 0) push_player_power(q, Power::Strength, curses);
            break;
          }

          case RelicId::MarkOfPain: {
            // Two Wounds SHUFFLED INTO THE DRAW PILE, and this hook fires after
            // the opening hand is dealt — which is what the wiki specifies, so
            // the Wounds can never appear in the opening five.
            for (int i = 0; i < 2; ++i) {
              Action a = make_action(ActionKind::AddCardToPile);
              a.card = CardId::Wound;
              a.amount = static_cast<int>(GeneratedPile::ShuffleDraw);
              q.push_back(a);
            }
            break;
          }

          case RelicId::PreservedInsect: {
            // Elite enemies start at 75% HP. MAX HP is untouched — the wiki is
            // explicit that current HP drops "as if they had taken damage", so
            // a healed elite can climb back to its full maximum.
            //
            // Applied directly rather than as damage: routing it through
            // DealDamage would fire the on-damaged hooks and wake a sleeping
            // Lagavulin before the fight began.
            if (!state.is_elite) break;
            for (Enemy& e : state.enemies) {
              if (e.hp <= 0) continue;
              e.hp = e.hp * 3 / 4;
              if (e.hp < 1) e.hp = 1;
            }
            break;
          }
          case RelicId::BronzeScales:
            push_player_power(q, Power::Thorns, 3);
            break;

          case RelicId::ThreadAndNeedle:
            push_player_power(q, Power::PlatedArmor, 4);
            break;

          case RelicId::FossilizedHelix:
            push_player_power(q, Power::Buffer, 1);
            break;

          case RelicId::Akabeko:
            // 8 Vigor at combat start, so the FIRST Attack of the fight hits
            // for +8 per hit. It is a one-shot: the charge is spent by that
            // card, not refreshed each turn.
            push_player_power(q, Power::Vigor, 8);
            break;

          case RelicId::Girya:
            // The counter IS the Strength: one per Lift spent at a campfire,
            // carried across fights by the run-scoped counter (§3.3). A relic
            // held but never lifted is worth nothing, so a zero counter must
            // push nothing rather than a zero-stack power.
            if (relic.counter > 0) {
              push_player_power(q, Power::Strength, relic.counter);
            }
            break;
          case RelicId::BagOfPreparation:
            // Resolves after the opening hand is already dealt, so this is a
            // SECOND draw of 2 rather than a 7-card opening draw. The
            // distinction is observable: the shuffle is unchanged, but any
            // draw-triggered effect sees two separate draws.
            push_draw(q, 2);
            break;
          default:
            break;
        }
        break;

      case Hook::TurnStartPlayer:
        fire_turn_start_relic(state, relic, q);
        break;

      case Hook::TurnEndPlayer:
        // Per-turn card counters reset here. This is what separates Kunai from
        // Nunchaku: both count Attacks on the same hook, and only the clearing
        // makes one "3 in a single turn" and the other "10, ever".
        if (relic_counter_is_per_turn(relic.id)) relic.counter = 0;

        switch (relic.id) {
          case RelicId::Orichalcum:
            // EXACTLY zero block, not "low" block — 1 point is enough to
            // suppress it. Read at end of turn, before the block is cleared at
            // the next turn's start.
            if (state.character.current_block == 0) push_player_block(q, 6);
            break;

          case RelicId::StoneCalendar:
            // Turn 7 ONLY, not every seventh turn. And it is FIXED damage, not
            // an attack: the wiki notes Strength and Vulnerable do not apply,
            // so DealFixedDamage rather than the attack path.
            if (state.turn_number == kStoneCalendarTurn) {
              Action a = make_action(ActionKind::DamageAllEnemies);
              a.amount = kStoneCalendarDamage;
              q.push_back(a);
            }
            break;

          default:
            break;
        }
        break;

      case Hook::CombatEnd:
        switch (relic.id) {
          case RelicId::BurningBlood:
            push_player_heal(q, 6);
            break;

          case RelicId::MeatOnTheBone:
            // "At or below 50%" is read at the moment this relic fires, and
            // that moment is AFTER earlier relics have already resolved —
            // which is why CombatEnd fires sequentially rather than batched
            // (fire_relic_hooks_sequentially). Burning Blood is always
            // acquired first, being the starter, so its 6 HP can lift you over
            // the threshold and suppress this. Batched, both would read the
            // same pre-heal HP and both fire.
            if (state.character.hp * 2 <= state.character.max_hp) {
              push_player_heal(q, 12);
            }
            break;

          default:
            break;
        }
        break;

      case Hook::TurnStartPostDraw:
        // Gambling Chip and Warped Tongs need a choice and an upgrade path
        // respectively; both land in later batches.
        break;

      case Hook::EnemyDeath:
        // Gremlin Horn: "whenever an enemy dies, gain 1 Energy and draw 1
        // card." Unconditional here — CheckDeath already withholds the fight's
        // last death, which is the relic's own !areMonstersBasicallyDead()
        // guard. The wiki notes the energy and card carry into the next turn
        // when the death happens during the enemies' turn; queueing them gives
        // that behaviour directly.
        if (relic.id == RelicId::GremlinHorn) {
          Action energy = make_action(ActionKind::GainEnergy);
          energy.amount = 1;
          q.push_back(energy);
          Action draw = make_action(ActionKind::DrawCards);
          draw.amount = 1;
          q.push_back(draw);
        }
        break;

      case Hook::ShuffleDrawPile:
        // Sundial: 2 Energy on every 3rd shuffle. The counter is RUN-scoped —
        // StS sets it in onEquip and never clears it per combat, so a fight can
        // end mid-count and the next one continues it.
        if (relic.id == RelicId::Sundial) {
          ++relic.counter;
          if (relic.counter >= 3) {
            relic.counter = 0;
            Action a = make_action(ActionKind::GainEnergy);
            a.amount = 2;
            q.push_back(a);
          }
        }
        // The Abacus: 6 Block on every shuffle. Relic block is never card
        // block, so Dexterity and Frail leave it alone.
        if (relic.id == RelicId::TheAbacus) {
          Action a = make_action(ActionKind::GainBlock);
          a.target = kPlayerSlot;
          a.amount = 6;
          q.push_back(a);
        }
        break;

      case Hook::BlockBroken:
        // Hand Drill: "whenever you break an enemy's Block, apply 2
        // Vulnerable." To THAT enemy — the slot the damage path handed us.
        // Never to the player: fire_block_broken is enemy-only, mirroring
        // StS's `this instanceof AbstractMonster` guard in brokeBlock().
        if (relic.id == RelicId::HandDrill && slot >= 0) {
          Action a = make_action(ActionKind::ApplyDebuff);
          a.target = slot;
          a.debuff = Debuff::Vulnerable;
          a.amount = 2;
          q.push_back(a);
        }
        break;

      default:
        // Every other hook is wired in a later batch. Listed explicitly rather
        // than silently ignored so an unhandled hook is a visible gap.
        break;
    }
  }
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
  const int strength_down = get_status(powers, Power::StrengthDown);
  const int thorns = get_status(powers, Power::Thorns);
  const int plated_armor = get_status(powers, Power::PlatedArmor);
  const int magnetism = get_status(powers, Power::Magnetism);
  const int mayhem = get_status(powers, Power::Mayhem);

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
      // Magnetism: `stacks` random colorless cards, at full price — the card
      // says "add", not "add for free", unlike Transmutation.
      if (magnetism > 0) {
        Action a = make_action(ActionKind::GenerateCards);
        a.amount = magnetism;
        a.gen_pool = GenerationPool::Colorless;
        a.gen_pile = GeneratedPile::Hand;
        q.push_back(a);
      }
      // Mayhem: play the top card of the draw pile, once per stack. These hooks
      // run BEFORE the turn's draw is queued, which is the correct order: the
      // card played is the one already on top, not one just drawn.
      for (int i = 0; i < mayhem; ++i) {
        Action a = make_action(ActionKind::PlayCard);
        a.amount = kPlayTopOfDrawKeeping;
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
      // Plated Armor's block is NOT card block: Dexterity and Frail do not
      // modify it. push_player_block leaves card_block false, so that holds by
      // construction rather than by remembering to exclude them.
      if (plated_armor > 0) push_player_block(q, plated_armor);
      // Rage lasts only the player's own turn.
      if (rage > 0) push_remove_player_power(q, Power::Rage);
      // Double Tap's charges are "this turn" too — unused ones are lost.
      if (get_status(state.character.powers, Power::DoubleTap) > 0) {
        push_remove_player_power(q, Power::DoubleTap);
      }
      // Flex's Strength Down: give back exactly what was gained, then clear the
      // marker. Queued LAST so anything this turn that read Strength (Combust
      // is fixed damage, but a future end-of-turn attack would not be) has
      // already resolved at the buffed value.
      if (strength_down > 0) {
        push_player_strength(q, -strength_down);
        push_remove_player_power(q, Power::StrengthDown);
      }
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
      // Thorns is Flame Barrier's shape with a different lifetime: it keys on
      // BEING ATTACKED, not on being hurt, so it fires through full block — and
      // unlike Flame Barrier it never expires. Fixed damage, so Strength and
      // Vulnerable do not scale it.
      if (thorns > 0 && attacker_slot >= 0) {
        Action a = make_action(ActionKind::DealFixedDamage);
        a.target = attacker_slot;
        a.amount = thorns;
        q.push_back(a);
      }
      break;
    case Hook::TurnStartEnemy:
    case Hook::TurnEndEnemy:
    case Hook::EnemyDamaged:
    case Hook::OnAnyDamage:
    case Hook::EnemyHpThreshold:
    case Hook::EnemyDeath:
    case Hook::EnemyWake:
    case Hook::BecameLastEnemy:
      break;  // enemy-side hooks
    // Relic hooks: no player POWER responds to these. Relics answer them in
    // fire_relic_hooks. Enumerated for the same reason as the enemy registry —
    // a new hook should not compile until every registry has considered it.
    case Hook::CombatStartPreDraw:
    case Hook::CombatStart:
    case Hook::TurnStartPostDraw:
    case Hook::BlockBroken:
    case Hook::ShuffleDrawPile:
    case Hook::PotionDrunk:
    case Hook::CombatEnd:
      break;
  }
}

// ---------------------------------------------------------------------------
// Executors + drain
// ---------------------------------------------------------------------------

namespace {

bool valid_enemy_slot(const CombatState& state, int slot) {
  return slot >= 0 && slot < static_cast<int>(state.enemies.size());
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
// An enemy's block BREAKS when damage meets or exceeds it — equality counts
// (decompiled decrementBlock: `damageAmount == currentBlock` takes the same
// branch as `>`), and there must have been block to break.
//
// Two things this deliberately does not do. It is never called for the player:
// StS guards the relic loop with `this instanceof AbstractMonster`, so a player
// whose own block breaks triggers nothing — a direct port of onBlockBroken's
// signature would have applied Hand Drill's Vulnerable to the player. And it is
// not called from the HP-loss path, which decrementBlock skips outright.
void fire_block_broken(CombatState& state, int slot, int block_before,
                       int damage, ActionQueue& q) {
  if (block_before > 0 && damage >= block_before) {
    fire_relic_hooks(state, Hook::BlockBroken, q, slot);
  }
}

void apply_fixed_damage(CombatState& state, int slot, int amount,
                        ActionQueue& q, ResolutionContext& ctx) {
  if (!valid_enemy_slot(state, slot) || amount <= 0) return;
  Enemy& e = state.enemies[slot];
  if (e.hp <= 0) return;
  const int hp_before = e.hp;
  // Thorns-type damage is still DAMAGE, not HP loss, so it breaks block and
  // Hand Drill answers it: decrementBlock skips only DamageType.HP_LOSS.
  const int block_before = e.current_block;
  apply_damage_to_hp_block(e.hp, e.current_block, amount);
  fire_block_broken(state, slot, block_before, amount, q);
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
  const int dmg = compute_attack_damage(
      base, state.character.powers, state.character.debuffs,
      state.enemies[slot].debuffs, strength_mult,
      vulnerable_damage_multiplier(state));

  // The Boot acts on what is left AFTER block, so the block subtraction is
  // done here rather than inside apply_damage_to_hp_block: the relic needs to
  // see the unblocked remainder to decide, and a fully-absorbed attack must
  // stay absorbed.
  const int block_before = state.enemies[slot].current_block;
  const int blocked = std::min(dmg, block_before);
  state.enemies[slot].current_block -= blocked;
  fire_block_broken(state, slot, block_before, dmg, q);
  const int to_hp = boot_adjusted_damage(state, dmg - blocked);
  state.enemies[slot].hp -= to_hp;
  if (state.enemies[slot].hp < 0) state.enemies[slot].hp = 0;
  // Track what actually reached HP — Reaper heals the unblocked total.
  ctx.unblocked_damage_dealt += hp_before - state.enemies[slot].hp;
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
  std::uniform_int_distribution<int> pick(0,
                                          static_cast<int>(living.size()) - 1);
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
        // Blood for Blood counts HP-loss events from ANY source, so unblocked
        // enemy damage counts too (Rupture, by contrast, does not fire here).
        if (damage_player(state, dmg)) state.character.hp_loss_events += 1;
        // Flame Barrier retaliates on being attacked, even if fully blocked.
        // a.card is None here — an enemy attack has no card — and the
        // PlayerAttacked arm keys on attacker_slot and never reads it.
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
        if (damage_player(state, a.amount)) {
          state.character.hp_loss_events += 1;  // Blood for Blood counts it
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
        // If Dexterity is negative, we don't subtract block (you can't gain
        // negative block), the min is 0.
        amount =
            amount + get_status(state.character.powers, Power::Dexterity) >= 0
                ? amount + get_status(state.character.powers, Power::Dexterity)
                : 0;
        if (get_status(state.character.debuffs, Debuff::Frail) > 0) {
          amount =
              static_cast<int>(std::floor(static_cast<float>(amount) * 0.75f));
        }
        // Panic Button's No Block (Debuff::NoBlock) belongs here: it zeroes
        // block gained FROM CARDS for 2 turns, and this branch is exactly the
        // card-block path. Everything else — Metallicize, Plated Armor,
        // Entrench, relics — reaches gain_block with card_block false and must
        // keep working.
        if (get_status(state.character.debuffs, Debuff::NoBlock) > 0) {
          amount = 0;
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
        const std::optional<CardId> drawn = draw_one(state, q);
        // Evolve / Fire Breathing key on the drawn card's type.
        if (drawn.has_value()) {
          fire_player_power_hooks(state, Hook::CardDrawn, q, *drawn);
        }
      }
      break;
    case ActionKind::ApplyDebuff:
      if (a.target == kPlayerSlot) {
        apply_debuff(state,
                     DebuffApplication{a.debuff, a.amount, Target::Character},
                     kNoSlot);
      } else if (apply_debuff(state,
                              DebuffApplication{a.debuff, a.amount,
                                                Target::Enemy},
                              a.target)) {
        push_sadistic_nature(state, a.target, q);
      }
      break;
    case ActionKind::ApplyPower:
      if (a.target == kPlayerSlot) {
        apply_power(state,
                    PowerApplication{a.power, a.amount, Target::Character},
                    kNoSlot);
        // Combust's second counter: stacks hold the accumulated damage, so the
        // per-cast 1 HP loss is counted here (Combust + Combust+ = 2 HP, 12
        // dmg).
        if (a.power == Power::Combust) state.character.combust_casts += 1;
      } else {
        const bool landed = apply_power(
            state, PowerApplication{a.power, a.amount, Target::Enemy},
            a.target);
        // A NEGATIVE power is a debuff in StS terms (Disarm's "lose Strength"),
        // so Sadistic Nature sees it — but never Shackled, which StS excludes
        // by name. That exclusion is what stops Dark Shackles triggering twice:
        // its Strength loss counts, the give-back does not.
        if (landed && a.amount < 0 && a.power != Power::Shackled) {
          push_sadistic_nature(state, a.target, q);
        }
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
      if (valid_enemy_slot(state, a.target) && state.enemies[a.target].hp > 0) {
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
    case ActionKind::UpgradeHand:
      // Armaments+: upgrade every card in hand, in place (Searing Blow bumps
      // its counter rather than swapping id).
      for (Card& c : state.current_hand) upgrade_card_in_place(c);
      break;
    case ActionKind::UpgradeAllPiles:
      // Apotheosis. upgrade_card_in_place already refuses what cannot be
      // upgraded — already-upgraded cards, Status and Curse — which is exactly
      // StS's canUpgrade filter, so no second guard is needed here.
      for (std::vector<Card>* pile :
           {&state.current_hand, &state.draw_pile, &state.discard_pile,
            &state.exhaust_pile}) {
        for (Card& c : *pile) upgrade_card_in_place(c);
      }
      break;
    case ActionKind::DrawPileToHand: {
      // Violence: N random cards of a type, taken from ANYWHERE in the draw
      // pile. Selection is without replacement, so a pile holding fewer
      // matches than asked for simply yields fewer — StS does the same.
      std::vector<int> matching;
      for (int i = 0; i < static_cast<int>(state.draw_pile.size()); ++i) {
        if (CARD_DATABASE.at(state.draw_pile[i].card_id).type == a.card_type) {
          matching.push_back(i);
        }
      }
      std::shuffle(matching.begin(), matching.end(), state.rng);
      const int take =
          std::min(static_cast<int>(matching.size()), std::max(a.amount, 0));
      std::vector<int> chosen(matching.begin(), matching.begin() + take);
      // Erase from the highest index down, so the earlier indices stay valid.
      std::sort(chosen.begin(), chosen.end(),
                [](int lhs, int rhs) { return lhs > rhs; });
      for (int idx : chosen) {
        const Card card = state.draw_pile[static_cast<std::size_t>(idx)];
        state.draw_pile.erase(state.draw_pile.begin() + idx);
        // Overflow past the hand limit goes to the discard pile, which is what
        // add_card_to_hand already does for every other generated card.
        add_card_to_hand(state, card);
      }
      break;
    }
    case ActionKind::DiscountRandomCardInHand: {
      // Madness. StS picks a random card from the WHOLE hand and re-rolls until
      // it lands on an eligible one; picking uniformly from the eligible set is
      // the same distribution with a fixed number of draws, which is what keeps
      // the stream stable (the same ruling as Discovery's three).
      //
      // Eligibility is two-tier, and the tiers are not interchangeable:
      // normally a card whose CURRENT cost is above 0, but if every card is
      // already discounted to 0, one whose PRINTED cost is above 0 — which is
      // how Madness still works on a card another effect made free this turn.
      // X-cost cards are never eligible (their cost is a sentinel, not a
      // number).
      std::vector<int> current_cost, printed_cost;
      for (int i = 0; i < static_cast<int>(state.current_hand.size()); ++i) {
        const Card& c = state.current_hand[i];
        const CardData& d = CARD_DATABASE.at(c.card_id);
        if (d.cost == kXCost) continue;
        if (instance_effective_cost(state, c) > 0) current_cost.push_back(i);
        if (d.cost > 0) printed_cost.push_back(i);
      }
      const std::vector<int>& eligible =
          !current_cost.empty() ? current_cost : printed_cost;
      if (eligible.empty()) break;
      std::uniform_int_distribution<std::size_t> pick(0, eligible.size() - 1);
      Card& victim = state.current_hand[eligible[pick(state.card_rng)]];
      victim.cost_override = 0;
      victim.cost_duration = CostDuration::ThisCombat;
      break;
    }
    case ActionKind::CapHandCost:
      // Enlightenment: every card in hand costing MORE than 1 drops to 1. A
      // cap, so a 0-cost card stays 0 and a Madness-discounted copy is not
      // raised. X-cost cards are untouched — their cost is a sentinel.
      for (Card& c : state.current_hand) {
        const CardData& d = CARD_DATABASE.at(c.card_id);
        if (d.cost == kXCost) continue;
        if (instance_effective_cost(state, c) <= 1) continue;
        c.cost_override = 1;
        c.cost_duration = a.cost_cap_for_combat ? CostDuration::ThisCombat
                                                : CostDuration::ThisTurn;
      }
      break;
    case ActionKind::DrawOpeningHand:
      draw_opening_hand(state, q);
      break;
    case ActionKind::CombatStartPostDraw:
      // The two relic hooks that follow the opening hand. Queued so that a
      // pre-draw pause parks them too — resolve_choice resumes exactly here.
      fire_relic_hooks(state, Hook::CombatStart, q);
      fire_relic_hooks(state, Hook::TurnStartPostDraw, q);
      break;
    case ActionKind::PlaceOnBottomOfDraw: {
      // front() is the BOTTOM: draw_one pops the back. Cards placed in
      // selection order therefore come back in that order, which is what StS
      // describes — the card chosen first is drawn first.
      //
      // The discount only marks a card whose PRINTED cost is above 0, exactly
      // as StS guards it: a card that already costs 0 gains nothing and must
      // not come back marked as discounted.
      Card moved = a.as_card();
      if (CARD_DATABASE.at(moved.card_id).cost > 0) {
        moved.cost_override = 0;
        moved.cost_duration = CostDuration::UntilPlayed;
      }
      state.draw_pile.insert(state.draw_pile.begin(), moved);
      break;
    }
    case ActionKind::ArmBomb:
      // A fuse starts at its full length and the end-of-turn tick walks it
      // down, so a Bomb played this turn goes off at the end of the third —
      // counting this one, as StS does.
      if (a.card == CardId::TheBombPlus) {
        ++state.character.bombs_upgraded[kBombFuseTurns - 1];
      } else {
        ++state.character.bombs[kBombFuseTurns - 1];
      }
      break;
    case ActionKind::TickBombs: {
      Character& c = state.character;
      // Each Bomb in the expiring slot fires as its OWN all-enemy hit, which is
      // how StS resolves several at once — they are separate powers there. The
      // damage is read from the card database so the numbers live in one place.
      for (int i = 0; i < c.bombs[0]; ++i) {
        Action bang = make_action(ActionKind::DamageAllEnemies);
        bang.amount = CARD_DATABASE.at(CardId::TheBomb).bomb_damage;
        q.push_back(bang);
      }
      for (int i = 0; i < c.bombs_upgraded[0]; ++i) {
        Action bang = make_action(ActionKind::DamageAllEnemies);
        bang.amount = CARD_DATABASE.at(CardId::TheBombPlus).bomb_damage;
        q.push_back(bang);
      }
      for (int i = 0; i + 1 < kBombFuseTurns; ++i) {
        c.bombs[i] = c.bombs[i + 1];
        c.bombs_upgraded[i] = c.bombs_upgraded[i + 1];
      }
      c.bombs[kBombFuseTurns - 1] = 0;
      c.bombs_upgraded[kBombFuseTurns - 1] = 0;
      break;
    }
    case ActionKind::GainGold:
      // Hand of Greed. Combat has no gold of its own; it records what it earned
      // and RunState writes it back (colorless-effects.md D5).
      state.gold_gained += a.amount;
      break;
    case ActionKind::GenerateCards: {
      // Random in-combat generation: Infernal Blade, Jack of All Trades,
      // Transmutation and Magnetism all land here.
      //
      // The pool is a published, ordered list (card.h) of obtainable cards
      // minus the HEALING-tagged ones, NOT a scan of CARD_DATABASE — which is
      // what the old Infernal Blade did, and which since v2 would have rolled
      // colorless cards, upgraded ids and rung ladders as if they were Ironclad
      // Attacks. Rolls come from card_rng, the dedicated generation stream.
      const std::vector<CardId>& pool = generation_pool(a.gen_pool);
      if (pool.empty()) break;
      std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
      for (int i = 0; i < a.amount; ++i) {
        // Each card is an INDEPENDENT roll — Jack of All Trades+ can hand you
        // the same card twice, and in StS it does.
        Card made{pool[pick(state.card_rng)]};
        if (a.gen_upgraded) made.card_id = upgraded_card(made.card_id);
        if (a.gen_free_this_turn) {
          made.cost_override = 0;
          made.cost_duration = CostDuration::ThisTurn;
        } else if (a.gen_free_this_combat) {
          // Chrysalis / Metamorphosis. StS only zeroes a card whose cost is
          // above 0, which matters for nothing today but keeps the override
          // off cards that were already free.
          if (CARD_DATABASE.at(made.card_id).cost > 0) {
            made.cost_override = 0;
            made.cost_duration = CostDuration::ThisCombat;
          }
        }
        switch (a.gen_pile) {
          case GeneratedPile::Hand:
            add_card_to_hand(state, made);
            break;
          case GeneratedPile::Discard:
            move_to_discard(state, made);
            break;
          case GeneratedPile::ShuffleDraw: {
            std::uniform_int_distribution<std::size_t> pos(
                0, state.draw_pile.size());
            state.draw_pile.insert(state.draw_pile.begin() + pos(state.rng),
                                   made);
            break;
          }
        }
      }
      break;
    }
    case ActionKind::PlayCard: {
      // Re-entrant card resolution — the case the action queue was built for
      // (effects-architecture §2.4). The nested play happens as a flat queue
      // step, so no resolution is open while it mutates state.
      PlayContext pc;
      pc.pay_energy = false;      // both cases are free plays
      pc.take_from_hand = false;  // the card is not in hand
      if (a.amount == kPlayFromDrawPile || a.amount == kPlayTopOfDrawKeeping) {
        // Havoc and Mayhem. Reshuffle first if the draw pile is empty ("it will
        // shuffle your discard pile into your draw pile and target the new top
        // card").
        if (state.draw_pile.empty()) reshuffle_discard_into_draw(state, q);
        if (state.draw_pile.empty()) break;
        const Card top = state.draw_pile.back();
        state.draw_pile.pop_back();
        // An unplayable card (Dazed, Wound) still exhausts but resolves
        // nothing. Routed through the ExhaustCard ACTION rather than a bare
        // move_to_exhaust: that executor is the only place Feel No Pain, Dark
        // Embrace and Sentinel's energy_when_exhausted fire, and "whenever a
        // card is Exhausted" is unconditional on cause (ROB-85). Every other
        // exhaust path in the engine already goes through it; this was the lone
        // exception, so Havoc-into-Dazed silently skipped the hooks.
        if (CARD_DATABASE.at(top.card_id).unplayable) {
          Action ex = make_action(ActionKind::ExhaustCard);
          ex.carry(top);
          q.push_back(ex);
          break;
        }
        // Havoc says "and Exhaust it"; Mayhem does not, so its card takes its
        // normal fate and can come round again. That is the only difference
        // between the two, and it is why they share this branch.
        pc.force_exhaust = a.amount == kPlayFromDrawPile;
        pc.instance = top;
        pc.forced_x = 0;  // an X-cost card played this way gets X = 0
        // The player chose no target, so auto-target at random.
        const int slot = pick_random_living_enemy(state);
        handle_play_card(state, top.card_id, slot < 0 ? 0 : slot, pc);
        break;
      }
      // Double Tap's replay. Consume a charge here, at execution.
      const int charges = get_status(state.character.powers, Power::DoubleTap);
      if (charges <= 0) break;
      if (charges - 1 <= 0) {
        state.character.powers.erase(Power::DoubleTap);
      } else {
        state.character.powers[Power::DoubleTap] = charges - 1;
      }
      pc.enters_pile = false;  // the first copy already went to its pile
      pc.instance = a.as_card();
      pc.forced_x = a.copies;  // "uses the same value of X as the first"
      handle_play_card(state, a.card, a.target, pc);
      break;
    }
    case ActionKind::MultiplyStrength: {
      // Limit Break. Multiplying keeps the sign, so a negative Strength
      // (Disarm, Siphon Soul) doubles into a worse debuff — faithful to StS.
      const int str = get_status(state.character.powers, Power::Strength);
      if (str != 0) {
        state.character.powers[Power::Strength] = str * a.amount;
      }
      break;
    }
    case ActionKind::Heal:
      // Reaper. Routed through an executor so "the player healed" has a single
      // write path and somewhere for a future listener to hang (ROB-91).
      heal_player(state, a.amount);
      break;
    case ActionKind::GainMaxHp:
      // Feed.
      gain_max_hp(state, a.amount);
      break;
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
      // Panache counts cards played THIS TURN, firing on every fifth. The
      // countdown is mutated here, in an executor, rather than in the power
      // registry — that registry pushes actions and never touches state.
      if (get_status(state.character.powers, Power::Panache) > 0) {
        if (--state.character.panache_counter <= 0) {
          state.character.panache_counter = kPanacheCardsPerTrigger;
          Action bang = make_action(ActionKind::DamageAllEnemies);
          bang.amount = get_status(state.character.powers, Power::Panache);
          q.push_back(bang);
        }
      }
      // Player powers first (Rage: block when an Attack is played), then the
      // enemy side: playing a Skill fires every living enemy's OnPlayerSkill
      // effects (the Gremlin Nob's Enrage), independent of whether the card
      // dealt damage or killed anything (ROB-65). Fires mid-drain, after the
      // card's own effects — the pre-queue 5b position.
      fire_player_power_hooks(state, Hook::CardPlayed, q, a.card);

      // Next-attack modifiers are spent HERE, once per card, rather than per
      // hit. Both say "your next attack", and the wiki is explicit that a
      // multi-hit card gets the benefit on every hit — so a per-hit removal
      // would give a 3-hit card one boosted hit and two plain ones.
      //
      // This runs after the card's damage has already resolved (the hits are
      // queued ahead of this hook), so every hit read the power before it is
      // removed.
      if (CARD_DATABASE.at(a.card).type == CardType::Attack) {
        if (get_status(state.character.powers, Power::Vigor) > 0) {
          push_remove_player_power(q, Power::Vigor);
        }
        if (get_status(state.character.powers, Power::PenNibCharge) > 0) {
          push_remove_player_power(q, Power::PenNibCharge);
        }
      }

      // Relics next. They take the card's TYPE, not its id — the id is
      // resolved to a type here, once, rather than in each relic arm.
      fire_relic_card_played(state, CARD_DATABASE.at(a.card).type, q);
      if (CARD_DATABASE.at(a.card).type == CardType::Skill) {
        for (std::size_t i = 0; i < state.enemies.size(); ++i) {
          if (state.enemies[i].hp > 0) {
            fire_enemy_hooks(state, static_cast<int>(i), Hook::CardPlayed, q);
          }
        }
      }
      break;
    case ActionKind::RequestChoice: {
      const ChoiceKind requested = static_cast<ChoiceKind>(a.amount);
      if (requested == ChoiceKind::DiscoverCard ||
          requested == ChoiceKind::DiscoverColorlessCard) {
        // Discovery's options are ROLLED, not taken from a pile, so it cannot
        // go through build_choice (which reads piles and takes a const state).
        //
        // Three DISTINCT cards, drawn WITHOUT REPLACEMENT. StS re-rolls until
        // it has three different ids, which has the same distribution but a
        // variable number of draws — and a variable draw count shifts every
        // later roll in the stream. A fixed three draws keeps replay stable
        // (Rob, 2026-09-19); the offer itself is identical.
        // Discovery offers the CLASS pool; Toolbox the colorless one.
        std::vector<CardId> candidates = generation_pool(
            requested == ChoiceKind::DiscoverCard ? GenerationPool::ClassAny
                                                  : GenerationPool::Colorless);
        PendingChoice pc;
        pc.kind = requested;
        pc.source_card = a.card;
        pc.is_optional = false;  // "Adding one is mandatory" (wiki)
        pc.copies = 1;
        const int wanted =
            std::min<int>(3, static_cast<int>(candidates.size()));
        for (int i = 0; i < wanted; ++i) {
          std::uniform_int_distribution<std::size_t> pick(
              0, candidates.size() - 1);
          const std::size_t idx = pick(state.card_rng);
          pc.options[pc.num_options++] = Card{candidates[idx]};
          candidates.erase(candidates.begin() + static_cast<long>(idx));
        }
        if (pc.num_options == 0) break;
        state.pending_choice = pc;
        break;
      }
      // Build the candidate list. If nothing qualifies, the choice is simply
      // skipped — StS plays the card, the choice just has no legal target
      // (e.g. Exhume with an empty exhaust pile). No pause, drain continues.
      PendingChoice pc = build_choice(state, requested, a.card);
      pc.copies = CARD_DATABASE.at(a.card).choice_copies;  // Dual Wield+ = 2
      // Multi-select (Purity, Forethought+): the choice stays open for several
      // picks, and Decline means "done" — so it is always optional, which also
      // keeps the single-option auto-resolve below from stealing a pick the
      // agent might not want to make.
      pc.max_picks = CARD_DATABASE.at(a.card).choice_max_picks;
      if (pc.max_picks > 1) pc.is_optional = true;
      if (pc.num_options == 0) break;
      if (pc.num_options == 1 && !pc.is_optional) {
        // Exactly one legal option: StS applies it without prompting ("if
        // there is only one card in your discard pile, it will automatically
        // be placed on top of your draw pile"). No pause — a choice with one
        // answer has no decision content, and pausing would cost the agent a
        // step whose mask has a single legal action.
        Action apply;
        apply.kind = ActionKind::ApplyChoice;
        apply.carry(pc.options[0]);
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
          // Warcry: hand -> top of draw. `back()` is the top (draw_one pops
          // it).
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
            ex.carry(chosen);
            q.push_front(ex);
          }
          break;
        case ChoiceKind::DrawPileSkillToHand:
        case ChoiceKind::DrawPileAttackToHand:
          // Secret Technique / Secret Weapon: draw pile -> hand. A hand at the
          // limit sends the card to the discard instead, which is what
          // add_card_to_hand already does for every other arrival.
          if (take_from_pile(state.draw_pile, chosen)) {
            add_card_to_hand(state, chosen);
          }
          break;
        case ChoiceKind::HandToBottomOfDraw:
          // Forethought: hand -> the BOTTOM of the draw pile. Queued rather
          // than moved here, so the single pick and Forethought+'s multi-select
          // take the same path.
          if (take_from_pile(state.current_hand, chosen)) {
            Action place = make_action(ActionKind::PlaceOnBottomOfDraw);
            place.carry(chosen);
            q.push_back(place);
          }
          break;
        case ChoiceKind::DiscoverCard: {
          // Discovery: the chosen card is a fresh copy that costs 0 this turn.
          // It is generated, so it comes from no pile and nothing is removed.
          Card made = chosen;
          made.cost_override = 0;
          made.cost_duration = CostDuration::ThisTurn;
          add_card_to_hand(state, made);
          break;
        }
        case ChoiceKind::DiscoverColorlessCard:
          // Toolbox: the card arrives at FULL price — the relic hands it over,
          // it does not discount it. A full hand sends it to the discard, which
          // is what add_card_to_hand already does.
          add_card_to_hand(state, chosen);
          break;
        case ChoiceKind::None:
          break;
      }
      break;
    }
    case ActionKind::ShuffleDiscardIntoDraw:
      // Deep Breath. Shuffles with the combat RNG, so the resulting draw order
      // is reproducible from the seed like every other shuffle.
      reshuffle_discard_into_draw(state, q);
      break;
    case ActionKind::DiscardHand: {
      // End of the player's turn: unplayed Ethereal cards exhaust (ROB-65
      // Dazed), the rest discard. Routed through the executors so an ethereal
      // exhaust is seen by Feel No Pain / Dark Embrace — StS handles the hand
      // before end-of-turn powers, so those responses queue ahead of Combust.
      //
      // Runic Pyramid keeps the hand, but it is NOT a blanket "nothing leaves":
      //   - Ethereal cards still exhaust. Exhausting is a different fate from
      //     discarding, and the relic only stops the discard.
      //   - Cards with an end-of-turn effect in hand still leave on their own
      //     (Burn, and the Decay/Doubt/Regret/Shame family). The wiki lists
      //     these as bypassing the relic.
      // Treating it as "skip the whole loop" would strand a Burn in hand
      // forever, dealing its damage every turn for the rest of the fight.
      const bool discards = hand_discards_at_turn_end(state);
      std::vector<Card> kept;
      for (const Card& c : state.current_hand) {
        const CardData& cd = CARD_DATABASE.at(c.card_id);
        // Burn: damage for sitting in hand at end of turn. Queued BEFORE the
        // card leaves, and as DealFixedDamage so block absorbs it (StS calls
        // it damage, not HP loss — "unblocked damage from Burn").
        const bool self_discards = cd.end_of_turn_damage_in_hand > 0;
        if (self_discards) {
          Action burn = make_action(ActionKind::DealFixedDamage);
          burn.target = kPlayerSlot;
          burn.amount = cd.end_of_turn_damage_in_hand;
          q.push_back(burn);
        }
        if (!discards && !cd.ethereal && !self_discards) {
          kept.push_back(c);
          continue;
        }
        Action move = make_action(cd.ethereal ? ActionKind::ExhaustCard
                                              : ActionKind::DiscardCard);
        // carry(), not the id alone: a card discounted for the COMBAT has to
        // still be discounted when it is drawn again next turn.
        move.carry(c);
        q.push_back(move);
      }
      state.current_hand = std::move(kept);
      break;
    }
    case ActionKind::CheckDeath:
      // Deferred death processing (ROB-62): on-death hooks fire after the
      // card's damage fully resolves. Then, if the kills left exactly one
      // living enemy, fire its BecameLastEnemy effects (ROB-77) — checked once
      // per resolution, not per death, so it can't double-fire.
      for (int i = 0; i < ctx.died_count; ++i) {
        fire_enemy_hooks(state, ctx.died_slots[i], Hook::EnemyDeath, q);
      }
      // Relics answer the same deaths — but NOT the killing blow that ends the
      // fight. Gremlin Horn's energy and card would have nowhere to go, and the
      // decompiled relic guards on !areMonstersBasicallyDead(), i.e. "some
      // monster is still neither dying nor escaping". Checked once here rather
      // than per relic, since the condition is about the fight, not the relic.
      if (ctx.died_count > 0 && count_living(state) > 0) {
        for (int i = 0; i < ctx.died_count; ++i) {
          fire_relic_hooks(state, Hook::EnemyDeath, q);
        }
      }
      if (ctx.died_count > 0 && count_living(state) == 1) {
        for (std::size_t i = 0; i < state.enemies.size(); ++i) {
          if (state.enemies[i].hp > 0) {
            fire_enemy_hooks(state, static_cast<int>(i), Hook::BecameLastEnemy,
                             q);
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
    case ChoiceKind::DrawPileSkillToHand:
      return d.type == CardType::Skill;
    case ChoiceKind::DrawPileAttackToHand:
      return d.type == CardType::Attack;
    case ChoiceKind::HandToTopOfDraw:
    case ChoiceKind::DiscardToTopOfDraw:
    case ChoiceKind::ExhaustToHand:
    case ChoiceKind::ExhaustCardInHand:
    case ChoiceKind::HandToBottomOfDraw:
      return true;  // any card in the source pile
    case ChoiceKind::DiscoverCard:
    case ChoiceKind::DiscoverColorlessCard:
      // Never reaches here: these options are ROLLED, so the RequestChoice
      // executor builds them and build_choice is not called. Listed rather
      // than defaulted so a new kind still has to be considered.
      return false;
    case ChoiceKind::None:
      return false;
  }
  return false;
}

// Which pile does this choice draw its options from?
const std::vector<Card>& source_pile(const CombatState& state,
                                     ChoiceKind kind) {
  switch (kind) {
    case ChoiceKind::DiscardToTopOfDraw:
      return state.discard_pile;
    case ChoiceKind::ExhaustToHand:
      return state.exhaust_pile;
    case ChoiceKind::DrawPileSkillToHand:
    case ChoiceKind::DrawPileAttackToHand:
      // The draw pile. Options are deduplicated by identity, so this reveals
      // WHAT is in the pile but never its ORDER — the parity rule v2-spec.md
      // §5.10 calls out, and the same reason StS shows an unordered grid.
      return state.draw_pile;
    case ChoiceKind::UpgradeCardInHand:
    case ChoiceKind::HandToTopOfDraw:
    case ChoiceKind::CopyAttackOrPowerInHand:
    case ChoiceKind::ExhaustCardInHand:
    case ChoiceKind::HandToBottomOfDraw:
    case ChoiceKind::DiscoverCard:  // no pile at all — the options are rolled
    case ChoiceKind::DiscoverColorlessCard:
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

namespace {

// Finish a multi-select: every staged card's effect resolves now, together
// (colorless-effects.md D1). Resolving per pick instead would let a Dark
// Embrace draw land between two of Purity's exhausts, so the agent could see —
// and pick — a card it had not drawn when the choice opened.
bool finish_multi_select(CombatState& state) {
  PendingChoice& pc = state.pending_choice;
  const ChoiceKind kind = pc.kind;
  const std::array<Card, kMaxMultiSelectPicks> staged = pc.staged;
  const int count = pc.picks_made;

  // Clear the pause BEFORE resuming, for the same reason the single-pick path
  // does: the resumed drain may request another choice.
  pc = PendingChoice{};
  ActionQueue q = state.suspended_queue;
  state.suspended_queue = ActionQueue{};

  // Walk backwards because each push_front puts its action ahead of the last,
  // so the first card picked ends up first in the queue. Forethought+ depends
  // on that: the card chosen first sits nearest the top of the draw pile and is
  // drawn first.
  for (int i = count - 1; i >= 0; --i) {
    Action a;
    switch (kind) {
      case ChoiceKind::ExhaustCardInHand:
        a = make_action(ActionKind::ExhaustCard);
        break;
      case ChoiceKind::HandToBottomOfDraw:
        a = make_action(ActionKind::PlaceOnBottomOfDraw);
        break;
      default:
        continue;  // no other kind is multi-select today
    }
    a.carry(staged[i]);
    q.push_front(a);
  }

  ResolutionContext ctx;
  drain(state, q, ctx);
  return true;
}

}  // namespace

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

  // Multi-select: a pick STAGES a card and the choice stays open. Decline means
  // "done", and reaching the limit — or running out of options — finishes it.
  if (pc.is_multi()) {
    if (!declining) {
      const Card chosen = pc.options[option_index];
      // Both multi-select cards pick from the HAND. Staging takes the copy out
      // now, which is what StS's selection screen shows, and is what keeps a
      // second copy of the same card selectable: the offer is rebuilt from what
      // remains, so three Strikes can be picked one at a time.
      if (!take_from_pile(state.current_hand, chosen)) return false;
      pc.staged[pc.picks_made++] = chosen;
      if (pc.picks_made < pc.max_picks) {
        PendingChoice rebuilt = build_choice(state, pc.kind, pc.source_card);
        if (rebuilt.num_options > 0) {
          rebuilt.copies = pc.copies;
          rebuilt.is_optional = true;
          rebuilt.max_picks = pc.max_picks;
          rebuilt.picks_made = pc.picks_made;
          rebuilt.staged = pc.staged;
          pc = rebuilt;
          return true;  // still open — the drain stays suspended
        }
      }
    }
    return finish_multi_select(state);
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
    a.carry(chosen);
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
