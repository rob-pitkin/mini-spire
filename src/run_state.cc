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
  run.map = generate_map(derive_stream_seed(run_seed, RngStream::Map));
  return run;
}

std::vector<int> RunState::available_paths() const {
  std::vector<int> columns;
  if (is_terminal()) return columns;

  if (floor == 0) {
    // Standing before the map: any room on the first row is an opening.
    for (int x = 0; x < kMapWidth; ++x) {
      if (map[0][x].is_room()) columns.push_back(x);
    }
    return columns;
  }

  const int row = floor - 1;
  if (row < 0 || row >= kMapHeight) return columns;
  for (const MapEdge& edge : map[row][column].edges) {
    columns.push_back(edge.dst_x);
  }
  return columns;
}

RoomType RunState::resolve_unknown_room() {
  std::mt19937 rng =
      make_stream(run_seed, RngStream::Event, static_cast<uint32_t>(floor));
  const int roll =
      static_cast<int>(std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) *
                       100.0f);

  // Bands are cumulative, and the shop band collapses to nothing when the last
  // room was a shop — its probability falls through to treasure and event
  // rather than being redistributed.
  const int monster_size = static_cast<int>(monster_chance * 100);
  const int shop_size =
      (last_room_was_shop ? 0 : static_cast<int>(shop_chance * 100)) +
      monster_size;
  const int treasure_size =
      static_cast<int>(treasure_chance * 100) + shop_size;

  RoomType choice;
  if (roll < monster_size) {
    choice = RoomType::Monster;
  } else if (roll < shop_size) {
    choice = RoomType::Shop;
  } else if (roll < treasure_size) {
    choice = RoomType::Treasure;
  } else {
    // Event is the FALLBACK, and therefore the common outcome — roughly 85% of
    // a first `?`. It is not a low-probability roll that drifts upward.
    choice = RoomType::Unknown;
  }

  // Drift applies to the final choice. Note the counter still increments when
  // an outcome did not happen even though its band was suppressed — a shop
  // skipped because the last room was a shop still makes shops likelier next
  // time.
  if (choice == RoomType::Monster) {
    monster_chance = 0.10f;
  } else {
    monster_chance += 0.10f;
  }
  if (choice == RoomType::Shop) {
    shop_chance = 0.03f;
  } else {
    shop_chance += 0.03f;
  }
  if (choice == RoomType::Treasure) {
    treasure_chance = 0.02f;
  } else {
    treasure_chance += 0.02f;
  }
  return choice;
}

void RunState::enter_room(RoomType room) {
  current_room = room;
  last_room_was_shop = room == RoomType::Shop;

  switch (room) {
    case RoomType::Monster:
      begin_combat(EncounterPool::Weak);
      break;
    case RoomType::Elite:
      begin_combat(EncounterPool::Elite);
      break;
    case RoomType::Rest:
      phase = Phase::Rest;
      break;
    case RoomType::Shop:
      phase = Phase::Shop;
      break;
    case RoomType::Treasure:
      // A deterministic pass-through: one relic, no choice (§4). Relics do not
      // exist yet, so entering is all that happens.
      phase = Phase::Treasure;
      break;
    case RoomType::Unknown:
      // A `?` that stayed a `?` — an actual event. Events are §11 step 7.
      phase = Phase::Event;
      break;
    case RoomType::None:
      phase = Phase::Map;
      break;
  }
}

void RunState::choose_path(int chosen_column) {
  if (is_terminal()) return;

  const std::vector<int> options = available_paths();
  if (std::find(options.begin(), options.end(), chosen_column) == options.end()) {
    return;
  }

  ++floor;
  column = chosen_column;

  RoomType room = map[floor - 1][column].room;
  // The map records a `?`; what it becomes is decided now, on entry, and is
  // never written back into the map (§5.4).
  if (room == RoomType::Unknown) room = resolve_unknown_room();

  enter_room(room);
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
  if (is_terminal()) return;

  if (floor >= final_floor) {
    // Stands in for killing the Act 1 boss. Replaced in Phase 7 by the boss
    // actually dying; a floor count is not a win condition in Slay the Spire.
    outcome = Outcome::Won;
    return;
  }
  phase = Phase::Map;
}

}  // namespace minispire
