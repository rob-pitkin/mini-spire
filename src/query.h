#pragma once

#include "card.h"
#include "combat_state.h"
#include "status_effect.h"

// The query/modifier layer (effects-architecture Stage 4b; §4.5).
//
// Modifiers are PULL, not push: unlike hooks (which fire and push actions),
// these are small pure functions consulted at computation sites. Corruption
// doesn't "fire" when you look at a Skill's cost — it's read as part of
// answering "what does this cost right now?".
//
// Contract: `valid_actions` and the executors call these instead of reading
// raw fields, so the mask and the resolution path can never disagree about
// cost, legality, or damage. Every function is const-on-state and free of RNG,
// so a query is safe to call from the mask (which must not mutate).

namespace minispire {

// Effective energy cost of a card right now. Corruption makes Skills cost 0;
// Blood for Blood costs 1 less per HP-loss event this combat (floored at 0).
// Returns kXCost unchanged for X-cost cards — "spend all" isn't a number.
int effective_cost(const CombatState& state, CardId card);

// Does the player's block clear at the start of their turn? Barricade keeps it.
bool block_resets_at_turn_start(const CombatState& state);

// May the player draw right now? Battle Trance forbids further draws this turn.
bool can_draw(const CombatState& state);

// Base damage for a card before the shared attack math (Strength/Weak/
// Vulnerable in compute_attack_damage). Body Slam deals damage equal to the
// player's current block; Perfected Strike adds per "Strike"-named card in the
// deck. Cards with no special rule return data.damage unchanged.
int base_card_damage(const CombatState& state, CardId card);

// As above, but for a specific card INSTANCE — the only correct entry point
// for cards whose damage depends on the copy (Rampage's accumulated bonus,
// Searing Blow's upgrade count). Falls back to base_card_damage for the rest.
int instance_card_damage(const CombatState& state, const Card& card);

// The Strength multiplier applied to a card's damage: Heavy Blade counts
// Strength 3x (5x upgraded), everything else 1x. Kept separate from
// base_card_damage because Strength is applied inside compute_attack_damage.
int strength_multiplier(CardId card);

// Is this card playable at all, ignoring energy and targeting? Covers the
// unplayable flag (Dazed), Entangle's attack lock, and Clash (only legal when
// every other card in hand is an Attack).
bool is_playable(const CombatState& state, CardId card);

}  // namespace minispire
