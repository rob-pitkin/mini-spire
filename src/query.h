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

// As above, for a specific card INSTANCE. A copy can carry its own cost
// (Infernal Blade's generated attack, Discovery's pick, Madness' target), and
// that override wins over every other modifier — colorless-effects.md D2.
//
// The id-based form answers "what does playing this card cost?", which is the
// mask's question; since the engine plays the CHEAPEST copy, it reports the
// cheapest copy's cost. Use this one whenever a specific copy is in hand.
int instance_effective_cost(const CombatState& state, const Card& card);

// Does the player's block clear at the start of their turn? Barricade keeps it.
bool block_resets_at_turn_start(const CombatState& state);

// The player's block AFTER the start-of-turn reset. Three cases rather than the
// two the bool above covers: Barricade keeps all, Calipers loses 15, otherwise
// all of it goes. Prefer this at the turn-start call site.
int block_after_turn_start(const CombatState& state);

// Ginger (Weak) and Turnip (Frail). Consulted BEFORE Artifact, so an immune
// player does not spend an Artifact charge on a debuff that cannot land.
bool player_is_immune_to(const CombatState& state, Debuff d);

// False with Runic Pyramid: the hand is not discarded at end of turn.
bool hand_discards_at_turn_end(const CombatState& state);

// Extra energy per turn from relics. Summed once at combat setup, since
// "gain 1 Energy at the start of each turn" IS energy_per_turn and no relic is
// gained mid-fight. Takes `elite_or_boss` rather than the encounter itself so
// query.h stays independent of encounter.h — Slaver's Collar is the only member
// that cares.
int relic_bonus_energy(const CombatState& state, bool elite_or_boss);

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

// How much extra damage a Vulnerable enemy takes: 1.5 normally, 1.75 with Paper
// Phrog. Read by compute_attack_damage's caller rather than by the function
// itself, which takes status maps and cannot see relics.
float vulnerable_damage_multiplier(const CombatState& state);

// The Boot: raise 4-or-less UNBLOCKED attack damage to 5. Takes the damage
// remaining after block and returns what should actually reach HP. Zero is NOT
// raised — see the implementation for why, and for the two other edge cases.
int boot_adjusted_damage(const CombatState& state, int unblocked);

// Is this card playable at all, ignoring energy and targeting? Covers the
// unplayable flag (Dazed), Entangle's attack lock, and Clash (only legal when
// every other card in hand is an Attack).
bool is_playable(const CombatState& state, CardId card);

}  // namespace minispire
