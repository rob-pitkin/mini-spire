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

// Effective energy cost of a card TYPE right now: the cheapest copy in hand.
// With no copy in hand, what a fresh copy would cost (Corruption; Blood for
// Blood reduced by this combat's HP-loss events). Returns kXCost unchanged for
// X-cost cards — "spend all" isn't a number.
//
// Not what a play costs: a play names its copy by cost column (v2-spec.md §6.5)
// and pays THAT copy's instance_effective_cost. This answers a per-type
// question, for display and for cards not in hand.
int effective_cost(const CombatState& state, CardId card);

// As above, for a specific card INSTANCE: X-cost, then Corruption, then the
// copy's own layers — free_once, turn_cost, combat_cost — then the printed
// cost (relic-effects.md §6.27). Use this one whenever a specific copy is in
// hand.
int instance_effective_cost(const CombatState& state, const Card& card);

// The copy's COST COLUMN: the cost index of the two card-indexed action blocks
// (v2-spec.md §6.5). Its effective cost, 0..kMaxCardCost. X-cost and unplayable
// cards are column 0: their cost never differs between copies, so the column
// would say nothing, and a fixed one keeps a single action per card.
int cost_column(const CombatState& state, const Card& card);

// Of two copies sharing one (card, cost column) — they differ only in ways the
// card face does not show — is `a` the one an action takes first? The least
// durable discount goes first (v2-spec.md §6.5): a turn-layer discount (it
// expires at end of turn anyway), then printed or combat-layer cost (it
// survives being played), then free_once last (playing spends it). Ties fall to
// Card::sorts_before, so the pick never depends on pile order.
bool taken_before(const Card& a, const Card& b);

// A potion's potency right now: its base potency, doubled by Sacred Bark (StS's
// getPotency). Potions that read no potency stay 0 (potion-effects.md §2).
int potion_potency(const CombatState& state, PotionId id);

// Can the player drink this potion now? In combat, at a decision point with no
// pending choice (Q8), the fight in progress, and the potion's effect built
// (potion_effect_implemented). Says nothing about whether one is HELD — the
// mask checks that. Fairy in a Bottle is never drinkable.
bool potion_can_be_drunk(const CombatState& state, PotionId id);

// Corruption: a Skill costs 0 while it is up. Above every cost layer.
bool corruption_frees_skill(const CombatState& state, CardId card);

// How many cards a turn's draw deals, the opening hand included: StS's
// masterHandSize. 5, or 7 with Snecko Eye.
int hand_size(const CombatState& state);

// Does the player's block clear at the start of their turn? Barricade keeps it.
bool block_resets_at_turn_start(const CombatState& state);

// The player's block AFTER the start-of-turn reset. Three cases rather than the
// two the bool above covers: Barricade keeps all, Calipers loses 15, otherwise
// all of it goes. Prefer this at the turn-start call site.
int block_after_turn_start(const CombatState& state);

// Energy at the start of the player's turn. Normally the per-turn allowance,
// but Ice Cream ADDS it to whatever was left instead of replacing it, so
// unspent energy carries forward. A query rather than a relic hook because
// that is where StS puts it: IceCream.java is an empty class, and
// EnergyManager::recharge() branches on holding it.
int energy_after_turn_start(const CombatState& state);

// How much HP the player actually loses from `amount`, after the relics that
// reduce incoming loss. `from_attack` is true only for enemy ATTACK damage:
// Torii's hook in StS excludes HP-loss and thorns-type damage and requires an
// attacker, so Burn's tick and a retaliation are untouched.
//
// EVERY path that removes player HP calls this — attack damage after block,
// fixed damage after block, and direct HP loss — so a new damage source cannot
// quietly skip a reduction. Call it with the post-block number: Torii reads
// what is actually unblocked.
int reduce_player_hp_loss(const CombatState& state, int amount,
                          bool from_attack);

// How much the player actually heals from `amount`, after the relics that
// modify healing. Magic Flower is the only one, and it is COMBAT ONLY — which
// is why this takes a CombatState and why the run layer's heals (rest sites,
// Meal Ticket, Eternal Feather) neither call it nor need to.
//
// Every in-combat heal goes through heal_player, and heal_player calls this, so
// a new healing source cannot quietly skip the modifier. Applied BEFORE the add
// and before the max-HP clamp, matching AbstractCreature::heal — relics, then
// powers, then add, then clamp.
int boost_player_heal(const CombatState& state, int amount);

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

// How much extra a Vulnerable PLAYER takes from an enemy attack: 1.5
// normally, 1.25 with Odd Mushroom.
float player_vulnerable_multiplier(const CombatState& state);

// The Boot: raise 4-or-less UNBLOCKED attack damage to 5. Takes the damage
// remaining after block and returns what should actually reach HP. Zero is NOT
// raised — see the implementation for why, and for the two other edge cases.
int boot_adjusted_damage(const CombatState& state, int unblocked);

// Is this card playable at all, ignoring energy and targeting? Covers the
// unplayable flag (Dazed), Entangle's attack lock, and Clash (only legal when
// every other card in hand is an Attack).
bool is_playable(const CombatState& state, CardId card);

}  // namespace minispire
