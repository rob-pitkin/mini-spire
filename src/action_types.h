#pragma once

#include <array>
#include <cassert>
#include <unordered_map>

#include "card.h"
#include "enemy.h"
#include "status_effect.h"

// POD action value types, split out of action.h (Stage 4c) so that
// CombatState can hold a suspended ActionQueue without a circular include:
// action.h needs CombatState (executors mutate it), while CombatState now
// needs ActionQueue (the paused mid-card queue persists on the state).
// Nothing here depends on CombatState.

namespace minispire {

// Entity addressing: enemy slot index, or kPlayerSlot for the player.
// kNoSlot marks an unused actor/target field.
inline constexpr int kPlayerSlot = -1;
inline constexpr int kNoSlot = -2;

// PlayCard modes (Action::amount). Which re-entrant play this is.
inline constexpr int kPlayFromDrawPile = 0;     // Havoc
inline constexpr int kPlayDoubleTapReplay = 1;  // Double Tap

// Helper: look up a stack count in a debuff/power map, returning 0 if absent.
template <typename Effect>
int get_status(const std::unordered_map<Effect, int>& m, Effect e) {
  auto it = m.find(e);
  return it == m.end() ? 0 : it->second;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

enum class ActionKind {
  // Mutations
  DealDamage,   // actor attacks target for `amount` base damage (one hit)
  DealFixedDamage,  // thorns-type damage (Juggernaut, Combust, Fire Breathing,
                    // Flame Barrier): ignores Strength/Weak/Vulnerable, but IS
                    // absorbed by block. Fires no attack-only enemy triggers.
  DamageAllEnemies,   // fan out fixed damage to every living enemy (Combust,
                      // Fire Breathing) — expands at execution so the target
                      // set is current
  DamageRandomEnemy,  // fixed damage to one uniformly-random living enemy
                      // (Juggernaut) — rolled at execution, per trigger
  DamageRandomEnemyAttack,  // as above but an ATTACK (Strength/Weak/Vulnerable
                            // apply, and it fires the attack-damage hooks) —
                            // Sword Boomerang. Rolled per hit at execution.
  LoseHp,       // direct player HP loss — bypasses block, can kill (ROB-80)
  GainBlock,    // target gains `amount` block; card_block applies Dex/Frail
  GainEnergy,   // player gains `amount` energy
  DrawCards,    // player draws `amount` cards
  ApplyDebuff,  // apply debuff x amount to target (Artifact-checked)
  ApplyPower,   // apply power x amount to target
  RemovePower,  // erase `power` from target (Lagavulin's Metallicize on wake)
  RewriteIntent,  // set target's last_move = move (interrupt the queued intent)
  Wake,           // set target's is_asleep = false
  ExhaustCard,    // put `card` in the exhaust pile (played or generated)
  DiscardCard,    // put `card` in the discard pile
  MultiplyStrength,  // Limit Break: player Strength *= amount
  // Structure
  PlayCard,       // resolve a card from inside a resolution (Double Tap's free
                  // replay, Havoc playing off the draw pile). `amount` selects
                  // which, via the kPlay* constants below.
  UpgradeHand,    // Armaments+: upgrade every card in hand
  MakeCardFree,   // Infernal Blade: `card` costs 0 for the rest of this turn
  AddCardToPile,  // generate a card into a pile (Wild Strike's Wound, Power
                  // Through's Wounds, Immolate's Burn, Anger's self-copy).
                  // `amount` is the GeneratedPile.
  EnemyEscape,    // target flees: hp -> 0, NOT a death (no on-death; ROB-74)
  EnemySplit,     // target dies and spawns its split_children at its current
                  // HP (ROB-64) — the reallocation is one flat executor step
  // Bookkeeping
  CardPlayedHook,  // fire Hook::CardPlayed listeners for `card` (Enrage, Rage)
  CheckDeath,      // process deaths recorded this resolution (on-death,
                   // became-last) — replaces the hand-rolled died_slots deferral
  DiscardHand,     // end of turn: ethereal cards exhaust, the rest discard
                   // (routed through the executors so Feel No Pain / Dark
                   // Embrace see the exhausts)
  RequestChoice,   // PAUSES the drain: build the candidate list for `choice`
                   // and suspend until resolve_choice() supplies an answer
                   // (Stage 4c; docs/design/decision-points.md)
  ApplyChoice,     // apply the answered choice — pushed by resolve_choice(),
                   // carries the chosen CardId in `card`
};

// A small, clone-safe tagged value. No closures, no pointers into state —
// actors and targets are slot indices / enums. Extend fields as kinds demand
// (POD only).
struct Action {
  ActionKind kind;
  int actor = kNoSlot;   // damage source: kPlayerSlot or an enemy slot
  int target = kNoSlot;  // recipient: kPlayerSlot or an enemy slot
  int amount = 0;
  CardId card = CardId::Strike;    // for card-carrying kinds
  // Strength multiplier for this hit (Heavy Blade's 3x/5x, Stage 4b). Set from
  // the card at translation; 1 for enemy attacks and fixed damage, which have
  // no card. Explicit rather than re-derived from `card`, whose default would
  // silently stand in for "no card".
  int strength_mult = 1;
  Debuff debuff = Debuff::None;    // ApplyDebuff payload
  Power power = Power::None;       // ApplyPower / RemovePower payload
  MoveName move = MoveName::None;  // RewriteIntent payload
  bool card_block = false;  // GainBlock from a played card: apply Dex/Frail
  int copies = 1;  // ApplyChoice: how many copies to add (Dual Wield+ = 2)
  // Per-instance card state for card-moving kinds (ExhaustCard, DiscardCard,
  // AddCardToPile). Carried alongside `card` so a Rampage returning to the
  // discard pile keeps its accumulated bonus, and an upgraded Searing Blow
  // keeps its counter. Zero for cards with no instance state.
  int card_bonus_damage = 0;
  int card_upgrades = 0;

  // Rebuild the card instance this action carries.
  Card as_card() const { return Card{card, card_bonus_damage, card_upgrades}; }
};

// Fixed-capacity ring buffer (no steady-state allocation — constraint §3.3).
// push_back ≈ StS addToBottom (the default); push_front ≈ addToTop
// ("immediately next"). Capacity covers the worst realistic card (X-cost
// multi-hit AoE at high energy) with a wide margin; overflow is a bug.
class ActionQueue {
 public:
  bool empty() const { return count_ == 0; }

  void push_back(const Action& a) {
    assert(count_ < kCapacity && "ActionQueue overflow");
    buf_[(head_ + count_) % kCapacity] = a;
    ++count_;
  }

  void push_front(const Action& a) {
    assert(count_ < kCapacity && "ActionQueue overflow");
    head_ = (head_ + kCapacity - 1) % kCapacity;
    buf_[head_] = a;
    ++count_;
  }

  Action pop_front() {
    assert(count_ > 0 && "pop from empty ActionQueue");
    Action a = buf_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return a;
  }

 private:
  static constexpr int kCapacity = 128;
  std::array<Action, kCapacity> buf_;
  int head_ = 0;
  int count_ = 0;
};

}  // namespace minispire
