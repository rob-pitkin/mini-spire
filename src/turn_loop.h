#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "combat_state.h"
#include "encounter.h"
#include "status_effect.h"

namespace minispire {

constexpr int IRONCLAD_MAX_HP = 80;
constexpr int IRONCLAD_ENERGY_PER_TURN = 3;
constexpr int STARTING_HAND_SIZE = 5;
constexpr int HAND_SIZE_LIMIT = 10;

// Compute the actual damage dealt by an attack with the given base damage,
// given the attacker's status effects (Strength adds, Weak multiplies down)
// and defender's status effects (Vulnerable multiplies up). Float-internal,
// truncated once at the end. Returns max(result, 0).
//
// Exposed so the CLI / observation layer can display the *effective* enemy
// attack damage (Strength-modified, etc.) rather than the raw move.damage.
// `strength_mult` multiplies the attacker's Strength contribution (Heavy
// Blade's 3x/5x, Stage 4b); it defaults to the normal 1x.
int compute_attack_damage(
    int base, const std::unordered_map<Power, int>& attacker_powers,
    const std::unordered_map<Debuff, int>& attacker_debuffs,
    const std::unordered_map<Debuff, int>& defender_debuffs,
    int strength_mult = 1);

// The Ironclad starter deck: 5 Strike + 4 Defend + 1 Bash (unshuffled).
std::vector<Card> starter_deck();

// Everything a fight needs to exist. See docs/design/v2-spec.md §3.2.
//
// A parameter struct rather than a widening argument list, because the fight's
// inputs keep growing — hp, relics and potions now, curses and ascension later
// — and each addition would otherwise churn every call site and make the order
// of seven positional arguments load-bearing.
//
// More importantly, everything here must be present BEFORE the fight is built,
// not patched in afterwards. Setup reads this state: a start-of-combat heal
// works off `hp`, and a relic that changes the opening draw has to act before
// the hand is dealt. Assigning after construction means those read a fight that
// never existed.
struct CombatSetup {
  uint32_t seed = 0;
  EncounterPool pool = EncounterPool::Weak;
  std::vector<Card> deck;

  // The run's HP, not a fresh Ironclad's.
  int hp = IRONCLAD_MAX_HP;
  int max_hp = IRONCLAD_MAX_HP;

  // First-class combat state (§3.0.1), so the fight can show and use them.
  std::vector<HeldRelic> relics;
  std::vector<PotionId> potions;
};

// Constructs an initial CombatState (ROB-66): seeded RNG, the character at the
// given HP, an encounter sampled from `pool`, and `deck` shuffled into the draw
// pile. Draws the opening hand; enemy intents are primed by their factories.
//
// Takes the setup BY VALUE and moves out of it. Deck, relics and potions all go
// straight into the state, so a by-const-ref signature would copy the deck on
// every call — and CombatEnv::reset is on the reset-latency path this project
// benchmarks. Callers with a deck to spare should std::move into the setup.
CombatState start_combat(CombatSetup setup);

// v1.0.0's shape: a fresh 80/80 Ironclad with no relics or potions. Kept so a
// CombatEnv built the v1.0.0 way behaves exactly as it did (§3.0.1).
CombatState start_combat(uint32_t seed, EncounterPool pool,
                         std::vector<Card> deck);

// Backward-compatible v1 fixture: fixed single Jaw Worm + starter deck. Used by
// M1 / existing tests that want the deterministic Jaw Worm fight.
CombatState start_v1_combat(uint32_t seed);

// Action layout (ROB-60 + Stage 4c). Two mutually-exclusive blocks:
//
//   COMBAT (indices 0 .. kEndTurnAction) — legal only while no choice pends:
//     action   = card_idx * kMaxEnemies + target_idx
//     end_turn = kEndTurnAction                        (block's last index)
//   CHOICE (indices kFirstOptionSlot .. kDeclineAction) — legal only DURING a
//   pending choice (docs/design/decision-points.md §5.1):
//     slot k   = kFirstOptionSlot + k    the k-th offered option
//     decline  = kDeclineAction          optional choices only
//
// card_idx is the integer value of a CardId; target_idx is an enemy slot.
// Untargeted cards (Defend) use the canonical target_idx 0; their other slots
// are permanently masked (see valid_actions).
//
// The combat indices are byte-identical to pre-4c, so a policy's learned
// card-playing mapping survives the addition of the choice channel.
inline constexpr int kEndTurnAction = kNumCardTypes * kMaxEnemies;
inline constexpr int kFirstOptionSlot = kEndTurnAction + 1;
inline constexpr int kDeclineAction = kFirstOptionSlot + kNumOptionSlots;
inline constexpr int kTotalActions = kDeclineAction + 1;

struct DecodedAction {
  bool is_end_turn;
  CardId card;     // valid only if !is_end_turn
  int target;      // enemy slot index; valid only if !is_end_turn
};

// Pure arithmetic decode of an action index — no state, so the mask and the
// apply path share one source of truth (decode never disagrees with itself).
DecodedAction decode_action(int action);

// Validity mask over the full action space. An action is legal iff the card is
// in hand AND affordable, AND — if the card targets an enemy — that target slot
// holds a living enemy; if it does not target an enemy (Defend), only the
// canonical target slot 0 is legal. End-turn is always legal while in progress.
std::vector<bool> valid_actions(const CombatState& state);

// How a card came to be played. A normal play pays energy and leaves the hand;
// re-entrant plays (Double Tap's free replay, Havoc playing off the draw pile)
// do neither. Modelled as a mode rather than duplicated functions so a card
// behaves identically however it was played.
struct PlayContext {
  bool pay_energy = true;      // false for a free replay
  bool take_from_hand = true;  // false when the card is already in flight
  bool force_exhaust = false;  // Havoc: the played card always exhausts
  bool enters_pile = true;     // false for Double Tap's second copy
  Card instance{CardId::Strike};  // the copy being played, when not from hand
  int forced_x = -1;           // reuse the first play's X (Double Tap)
};

// Resolve one card. Public because the PlayCard ACTION re-enters it: a
// meta-card (Double Tap, Havoc) pushes a PlayCard action rather than calling
// this directly, so the nested play is a flat queue step like everything else.
void handle_play_card(CombatState& state, CardId card_id, int target,
                      const PlayContext& ctx_play = PlayContext{});

// Apply a player action. Returns false (silently, no exception) if the
// action is invalid; state is not mutated in that case. Combat must be
// in progress (Outcome::InProgress). Debug builds may log on rejection.
bool apply_action(CombatState& state, int action);

}  // namespace minispire
