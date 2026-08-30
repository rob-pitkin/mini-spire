#ifndef MINISPIRE_RUN_STATE_H
#define MINISPIRE_RUN_STATE_H

#include <cstdint>
#include <vector>

#include "card.h"
#include "combat_state.h"
#include "encounter.h"
#include "run_rng.h"
#include "turn_loop.h"  // IRONCLAD_MAX_HP, start_combat, starter_deck

namespace minispire {

// The run above the fight. See docs/design/v2-spec.md §3.
//
// RunState is the owner of record ACROSS fights. A fight is a projection of run
// state into a CombatState (begin_combat) and a write-back of the results
// (end_combat). Within a fight, CombatState holds what it needs outright —
// including relics and potions once those land (§3.0.1), so the combat
// environment stays usable standalone rather than being a fragment that only
// works inside a run.
//
// No `ascension` and no `act` field: v2.0.0 is Act 1 at Ascension 0, and a
// constant occupies no state (§3.0).
struct RunState {
  // One seed per run. Every random system draws from its own stream derived
  // from this (§3.5) — never from a single shared generator.
  uint64_t run_seed = 0;

  // 0 is Neow. A linear counter until the map lands (§11 step 1).
  int floor = 0;

  int hp = IRONCLAD_MAX_HP;
  int max_hp = IRONCLAD_MAX_HP;
  int gold = 0;

  // The truth about what the player owns. Combat gets a copy.
  std::vector<Card> master_deck;

  // Monotonic source of card identity. Reproducible from the run seed because
  // it depends only on the order cards are acquired, which is itself seeded —
  // deliberately not a hash or a global (§3.2).
  int next_card_uid = 0;

  // Owned, not inherited (§3). Populated by begin_combat.
  CombatState combat;
  bool in_combat = false;

  // relics / potions live here once RelicId and PotionId exist. They are part
  // of CombatState too (§3.0.1); RunState owns them across fights.

  // Puts a card into the master deck, giving it a fresh identity. This is the
  // ONLY place a run-scoped uid is minted.
  void add_card(Card card);

  // Starts a run: seeds the RNG and deals the Ironclad the starter deck, each
  // card with its own uid.
  static RunState start(uint64_t run_seed);

  // Projects run state into a fight on the current floor (§3.2 "Entering a
  // fight"). Draws from stream Combat indexed by floor, so this fight replays
  // identically regardless of what happened on any other floor.
  void begin_combat(EncounterPool pool);

  // Writes the fight's results back (§3.2 "On fight end"). HP and Max HP carry;
  // the piles are discarded because the master deck is the truth.
  void end_combat();
};

}  // namespace minispire

#endif  // MINISPIRE_RUN_STATE_H
