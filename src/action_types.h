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
// Mayhem. Havoc's sibling, and the difference is the whole distinction between
// the two cards: Havoc reads "and Exhaust it", Mayhem does not, so a card
// Mayhem plays goes to the discard and can come round again.
inline constexpr int kPlayTopOfDrawKeeping = 2;

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
  Heal,        // player regains `amount` HP (Reaper)
  GainMaxHp,   // player's max HP rises by `amount`, and current HP with it
               // (Feed). Both of these existed only as direct calls to the
               // Stage-1 mutators until ROB-91 — an undocumented hole in the
               // single-write-path rule, which is how a fourth one appears.
  // Structure
  PlayCard,       // resolve a card from inside a resolution (Double Tap's free
                  // replay, Havoc playing off the draw pile). `amount` selects
                  // which, via the kPlay* constants below.
  UpgradeHand,    // Armaments+: upgrade every card in hand
  UpgradeAllPiles,  // Apotheosis: upgrade every upgradable card in hand, draw,
                    // discard AND exhaust. Queued BEFORE the card's own pile
                    // move, because StS never upgrades Apotheosis itself.
  DrawPileToHand,   // Violence: move `amount` random cards of `card_type` from
                    // the draw pile to the hand; overflow past the hand limit
                    // goes to the discard pile
  GainGold,         // Hand of Greed: record `amount` gold earned in this fight.
                    // Combat has no gold — RunState writes it back (§3.2)
  DiscountRandomCardInHand,  // Madness: one random eligible card in hand costs
                             // 0 for the rest of the combat
  CapHandCost,    // Enlightenment: every card in hand costing more than 1 drops
                  // to 1, for this turn or (upgraded) the whole combat
  RemoveAllDebuffs,     // Orange Pellets: clear every debuff on the player at
                        // once. An action rather than a direct map clear so it
                        // goes through the executors like every other mutation.
  DrawOpeningHand,      // the fight's first hand, Innate-aware. An ACTION so a
                        // pre-draw relic that pauses for a choice (Toolbox)
                        // parks the draw behind it instead of being overtaken
                        // by it — the decision must be made without seeing the
                        // opening hand.
  CombatStartPostDraw,  // the two relic hooks that follow the opening draw,
                        // queued so they ride behind a parked draw
  PlaceOnBottomOfDraw,  // Forethought: the carried card goes UNDER the draw
                        // pile, costing 0 until played. An action rather than
                        // an inline move so the multi-select path and the
                        // single-pick path share one implementation.
  ArmBomb,        // The Bomb: start a fuse at its full length (`card` says
                  // which Bomb, since the two differ only in damage)
  TickBombs,      // The Bomb: end of turn — the slot that has run out fires at
                  // every enemy, then the remaining fuses shift down one
  GenerateCards,  // roll `amount` cards from `gen_pool` into `gen_pile`
                  // (Infernal Blade, Jack of All Trades, Transmutation,
                  // Magnetism). Rolled from the CardRandom stream at execution.
                  // Replaces MakeCardFree, whose pool was every Attack id in
                  // CARD_DATABASE and whose rolls came from the combat stream.
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
  ShuffleDiscardIntoDraw,  // Deep Breath. Distinct from draw_one's automatic
                           // reshuffle, which fires only when the draw pile has
                           // run dry; this happens regardless.
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
  // For card-carrying kinds. None means "this action carries no card", which
  // is the truth for most kinds: an enemy's DealDamage, a Mayhem PlayCard that
  // takes the top of the draw pile, a relic's RequestChoice. It defaulted to
  // Strike, and that silently became a REAL answer wherever an unset field was
  // read — Toolbox's choice reported Strike as the card that opened it.
  //
  // Every executor that reads this field was audited when the default changed:
  // each either sets it at every push site (ExhaustCard, DiscardCard,
  // CardPlayedHook, AddCardToPile, ArmBomb, PlaceOnBottomOfDraw, and the
  // Double Tap replay) or never reads it on the cardless path.
  CardId card = CardId::None;
  // Strength multiplier for this hit (Heavy Blade's 3x/5x, Stage 4b). Set from
  // the card at translation; 1 for enemy attacks and fixed damage, which have
  // no card. Explicit rather than re-derived from `card`, whose default would
  // silently stand in for "no card".
  int strength_mult = 1;
  Debuff debuff = Debuff::None;    // ApplyDebuff payload
  Power power = Power::None;       // ApplyPower / RemovePower payload
  // DrawPileToHand payload: which card type to filter the draw pile by. A
  // separate field rather than reusing `card`, which names a specific card —
  // Violence wants "any Attack", not "this Attack".
  CardType card_type = CardType::Attack;
  // GenerateCards payload: which pool, where the cards land, and what happens
  // to them on arrival.
  GenerationPool gen_pool = GenerationPool::None;
  GeneratedPile gen_pile = GeneratedPile::Hand;
  bool gen_upgraded = false;        // Transmutation+
  bool gen_free_this_turn = false;  // Transmutation, Infernal Blade
  bool gen_free_this_combat = false;  // Chrysalis, Metamorphosis
  // CapHandCost: does the cap last the combat (Enlightenment+) or the turn?
  bool cost_cap_for_combat = false;
  MoveName move = MoveName::None;  // RewriteIntent payload
  bool card_block = false;  // GainBlock from a played card: apply Dex/Frail
  int copies = 1;  // ApplyChoice: how many copies to add (Dual Wield+ = 2)
  // Per-instance card state for card-moving kinds (ExhaustCard, DiscardCard,
  // AddCardToPile). Carried alongside `card` so a Rampage returning to the
  // discard pile keeps its accumulated bonus, and an upgraded Searing Blow
  // keeps its counter. Zero for cards with no instance state.
  //
  // The cost override belongs to the same family and for the same reason: a
  // Madness-discounted card discarded at end of turn must still be free when it
  // is drawn again. Leaving it out silently reset every this-combat discount at
  // the turn boundary — two tests caught it.
  int card_bonus_damage = 0;
  int card_upgrades = 0;
  int card_cost_override = kNoCostOverride;
  CostDuration card_cost_duration = CostDuration::None;

  // Rebuild the card instance this action carries.
  Card as_card() const {
    Card c{card, card_bonus_damage, card_upgrades};
    c.cost_override = card_cost_override;
    c.cost_duration = card_cost_duration;
    return c;
  }

  // Carry one card's full instance state onto this action. Preferred over
  // setting the fields one at a time, which is how the cost override came to be
  // dropped at three of the four call sites.
  void carry(const Card& c) {
    card = c.card_id;
    card_bonus_damage = c.bonus_damage;
    card_upgrades = c.upgrades;
    card_cost_override = c.cost_override;
    card_cost_duration = c.cost_duration;
  }
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
