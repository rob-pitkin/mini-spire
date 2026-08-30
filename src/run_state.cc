#include "run_state.h"

#include <algorithm>

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
  phase = Phase::Combat;

  // An elite pays a better reward (§4.2). Boss fights are not an EncounterPool
  // yet — bosses are Phase 7 — so nothing produces RewardSource::Boss today.
  combat_source = pool == EncounterPool::Elite ? RewardSource::Elite
                                               : RewardSource::Monster;
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

  if (hp <= 0) {
    // Losing is terminal for the run, and it is the only terminal condition
    // that exists today: winning needs an Act 1 boss to kill (Phase 7).
    outcome = Outcome::Lost;
  } else {
    phase = Phase::Reward;
    generate_card_reward(combat_source);
  }
}

void RunState::advance_to_next_floor(EncounterPool pool) {
  if (is_terminal()) return;
  ++floor;
  begin_combat(pool);
}

CardRarity RunState::roll_card_rarity(std::mt19937& rng, RewardSource source) {
  // A boss reward is always rare, and bypasses the roll entirely — but it still
  // resets the pity counter, which is easy to miss.
  if (source == RewardSource::Boss) {
    card_rarity_factor = 5;
    return CardRarity::Rare;
  }

  const bool elite = source == RewardSource::Elite;
  const int rare_chance = elite ? 10 : 3;
  const int uncommon_chance = elite ? 40 : 37;

  // random(99) is inclusive of 99 in the source we ported from.
  const int roll =
      static_cast<int>(std::uniform_int_distribution<int>(0, 99)(rng)) +
      card_rarity_factor;

  CardRarity rarity;
  if (roll < rare_chance) {
    rarity = CardRarity::Rare;
  } else if (roll < rare_chance + uncommon_chance) {
    rarity = CardRarity::Uncommon;
  } else {
    rarity = CardRarity::Common;
  }

  // Drift. Uncommon leaves the counter ALONE — only commons walk it down and
  // only a rare resets it. Treating uncommon as a decrement would make rares
  // far too frequent.
  if (rarity == CardRarity::Common) {
    card_rarity_factor = std::max(card_rarity_factor - 1, -40);
  } else if (rarity == CardRarity::Rare) {
    card_rarity_factor = 5;
  }
  return rarity;
}

void RunState::generate_card_reward(RewardSource source) {
  card_reward.clear();

  std::mt19937 rng = make_stream(run_seed, RngStream::CardReward,
                                 static_cast<uint32_t>(floor));

  for (int i = 0; i < kCardRewardSize; ++i) {
    const CardRarity rarity = roll_card_rarity(rng, source);
    const std::vector<CardId>& pool = rarity == CardRarity::Rare
                                          ? IRONCLAD_RARE_POOL
                                          : rarity == CardRarity::Uncommon
                                                ? IRONCLAD_UNCOMMON_POOL
                                                : IRONCLAD_COMMON_POOL;

    // One reward never offers the same card twice. Re-draw until distinct; the
    // pools are far larger than the reward, so this terminates quickly.
    CardId chosen;
    bool duplicate;
    do {
      chosen = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];
      duplicate = false;
      for (const Card& already : card_reward) {
        if (already.card_id == chosen) {
          duplicate = true;
          break;
        }
      }
    } while (duplicate);

    // Not in the master deck yet, so no uid: it is an offer, not a possession.
    card_reward.push_back(Card{chosen});
  }
}

void RunState::take_card_reward(int index) {
  if (index < 0 || index >= static_cast<int>(card_reward.size())) return;
  // add_card is what mints the uid — the card acquires identity at the moment
  // it becomes the player's, not when it was offered.
  add_card(card_reward[index]);
  skip_card_reward();
}

void RunState::skip_card_reward() {
  card_reward.clear();
  if (!is_terminal()) phase = Phase::Map;
}

}  // namespace minispire
