#include "query.h"

#include <cstring>

#include "action.h"  // get_status

namespace minispire {

namespace {

// Does this card's display name contain "Strike"? Perfected Strike's bonus is
// a NAME-substring rule in StS ("counts ALL cards with the word Strike in its
// title"), not a card-id list — so it automatically covers Strike, Strike+,
// Twin Strike, Pommel Strike, and Perfected Strike itself as the pool grows.
bool is_strike_named(CardId id) {
  return std::strstr(CARD_DATABASE.at(id).name, "Strike") != nullptr;
}

// Count "Strike"-named cards in the player's DECK: hand + draw + discard.
// The exhaust pile does NOT count — in StS1 exhausting a Strike reduces
// Perfected Strike's damage (verified; the StS2 rework added exhaust, we model
// StS1). The played Perfected Strike is in flight (already out of hand) when
// this is computed, so it counts itself via the +1 at the call site.
int count_strikes_in_deck(const CombatState& state) {
  int n = 0;
  for (const Card& c : state.current_hand) n += is_strike_named(c.card_id);
  for (const Card& c : state.draw_pile) n += is_strike_named(c.card_id);
  for (const Card& c : state.discard_pile) n += is_strike_named(c.card_id);
  return n;
}

}  // namespace

int effective_cost(const CombatState& state, CardId card) {
  const CardData& data = CARD_DATABASE.at(card);
  if (data.cost == kXCost) return kXCost;  // "spend all" — not a number

  // Infernal Blade's generated attack costs 0 for the rest of this turn.
  // Checked first: a free card is free regardless of the other modifiers.
  auto free_it = state.character.free_this_turn.find(card);
  if (free_it != state.character.free_this_turn.end() && free_it->second > 0) {
    return 0;
  }

  // Corruption: Skills cost 0.
  if (data.type == CardType::Skill &&
      get_status(state.character.powers, Power::Corruption) > 0) {
    return 0;
  }
  // Blood for Blood: 1 less per HP-loss EVENT this combat (any source —
  // including enemy attacks, unlike Rupture). Floored at 0.
  if (data.cost_drops_per_hp_loss) {
    const int reduced = data.cost - state.character.hp_loss_events;
    return reduced < 0 ? 0 : reduced;
  }
  return data.cost;
}

bool block_resets_at_turn_start(const CombatState& state) {
  // Barricade: block is NOT removed at the start of your turn.
  return get_status(state.character.powers, Power::Barricade) == 0;
}

bool can_draw(const CombatState& state) {
  // Battle Trance: no additional draws for the rest of this turn.
  return !state.character.no_draw_this_turn;
}

int base_card_damage(const CombatState& state, CardId card) {
  const CardData& data = CARD_DATABASE.at(card);
  switch (data.damage_rule) {
    case DamageRule::Normal:
      return data.damage;
    case DamageRule::EqualToBlock:
      // Body Slam: damage = the player's current block at resolution time.
      return state.character.current_block;
    case DamageRule::PerStrikeInDeck:
      // Perfected Strike: base + bonus per "Strike"-named card. +1 counts the
      // card being played, which is in flight (out of hand) at this point.
      return data.damage +
             data.damage_rule_amount * (count_strikes_in_deck(state) + 1);
    case DamageRule::SearingBlow:
      // Depends on the card INSTANCE's upgrade count, so it cannot be answered
      // from the type alone — instance_card_damage handles it. Reaching here
      // means a caller used the type-level query on a per-instance card;
      // return the id's own baseline rather than silently reporting 0.
      return card == CardId::SearingBlowPlus ? 16 : 12;
  }
  return data.damage;
}

int instance_card_damage(const CombatState& state, const Card& card) {
  const CardData& data = CARD_DATABASE.at(card.card_id);
  if (data.damage_rule == DamageRule::SearingBlow) {
    // n(n+7)/2 + 12 at n upgrades — matches the wiki's published progression
    // (12, 16, 21, 27, 34, 42, ...). Unbounded by design.
    //
    // The upgrade count normally lives entirely on the instance (Searing Blow
    // is deliberately absent from CARD_UPGRADES; upgrade_card_in_place bumps
    // the counter instead of swapping id). But CardId::SearingBlowPlus still
    // exists as a database row and an action slot, and a default-constructed
    // Card{SearingBlowPlus} has upgrades == 0 — which would have dealt 12, the
    // UNupgraded damage, from a card named "+" (ROB-85). Nothing constructs one
    // today; this makes the id behave correctly if anything ever does.
    const int n = card.upgrades + (card.card_id == CardId::SearingBlowPlus);
    return n * (n + 7) / 2 + 12;
  }
  // Rampage's accumulated bonus rides on the instance; every other rule is
  // type-level.
  return base_card_damage(state, card.card_id) + card.bonus_damage;
}

int strength_multiplier(CardId card) {
  // Heavy Blade: Strength affects it 3x (5x upgraded).
  const int m = CARD_DATABASE.at(card).strength_mult;
  return m > 0 ? m : 1;
}

bool is_playable(const CombatState& state, CardId card) {
  const CardData& data = CARD_DATABASE.at(card);
  if (data.unplayable) return false;  // Dazed etc. (ROB-65)
  // Entangle blocks all Attack-type cards for a turn (ROB-75).
  if (data.type == CardType::Attack &&
      get_status(state.character.debuffs, Debuff::Entangle) > 0) {
    return false;
  }
  // Clash: only playable if every OTHER card in hand is an Attack. The Clash
  // being played is still in hand when the mask is computed, and it's itself an
  // Attack, so a plain "all cards in hand are Attacks" check is equivalent.
  if (data.attacks_only_in_hand) {
    for (const Card& c : state.current_hand) {
      if (CARD_DATABASE.at(c.card_id).type != CardType::Attack) return false;
    }
  }
  return true;
}

}  // namespace minispire
