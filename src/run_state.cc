#include "run_state.h"

namespace minispire {

void RunState::add_card(Card card) {
  card.uid = next_card_uid++;
  master_deck.push_back(card);
}

RunState RunState::start(uint64_t run_seed) {
  RunState run;
  run.run_seed = run_seed;
  run.floor = 0;
  run.hp = IRONCLAD_MAX_HP;
  run.max_hp = IRONCLAD_MAX_HP;
  for (const Card& card : starter_deck()) run.add_card(card);
  return run;
}

void RunState::begin_combat(EncounterPool pool) {
  // Indexed by floor: the same fight on the same floor of the same run always
  // shuffles and rolls identically, whatever happened elsewhere in the run.
  const uint64_t combat_seed = derive_stream_seed(
      run_seed, RngStream::Combat, static_cast<uint32_t>(floor));

  combat = start_combat(static_cast<uint32_t>(combat_seed), pool, master_deck);

  // start_combat deals a fresh Ironclad. The run's HP is what actually carries,
  // so it overrides — this is the whole point of a run.
  combat.character.hp = hp;
  combat.character.max_hp = max_hp;

  in_combat = true;
}

void RunState::end_combat() {
  // Carried: HP and Max HP. Max HP because Feed and Neow can raise it mid-fight.
  hp = combat.character.hp;
  max_hp = combat.character.max_hp;

  // Discarded: every pile. The master deck is the truth, so combat's copies —
  // including any Wound/Dazed/Slimed/Burn the enemy added — simply go away.
  // Nothing needs to identify status cards for removal; not carrying the piles
  // forward is what removes them.
  //
  // Permanent card changes would be written back here by matching uid (§3.2).
  // Today that path has no callers: Rampage's growth and a mid-combat Armaments
  // upgrade are both combat-scoped by design, a campfire smith mutates the
  // master deck directly without going through a fight, and Feed raises max_hp
  // rather than changing a card. Ritual Dagger — the one Ironclad card whose
  // card state is genuinely run-scoped — is not implemented. The uid exists so
  // that when it lands, the write-back has something to match on.

  in_combat = false;
}

}  // namespace minispire
