#ifndef MINISPIRE_RUN_STATE_H
#define MINISPIRE_RUN_STATE_H

#include <cstdint>
#include <vector>

#include "card.h"
#include "combat_state.h"
#include "encounter.h"
#include "map.h"
#include "run_rng.h"
#include "turn_loop.h"  // IRONCLAD_MAX_HP, start_combat, starter_deck

namespace minispire {

// A mode that persists across steps, with a varying legal-action set — exactly
// how combat already works. Not one decision. See docs/design/v2-spec.md §4.
//
// The declaration order IS the observation's phase one-hot (block 5), so these
// values are an interface: renumbering them silently changes the observation.
//
// NOTE: these are NOT the map's room types, which are a different 7 and look
// interchangeable (§5.4). Do not share an enum between them.
enum class Phase {
  Neow = 0,   // run start; exits when a blessing is chosen
  Map,        // after any room resolves; exits when a path node is chosen
  Combat,     // monster/elite/boss node; exits when all enemies die or you do
  Reward,     // combat won; exits when rewards are taken or skipped
  Shop,       // merchant node; exits when the player leaves
  Rest,       // rest node; exits when an option is taken
  Event,      // event node; exits when the event resolves
  Treasure,   // chest — a deterministic pass-through, no agent decision (§4)
};

inline constexpr int kNumPhases = 8;

// Rarity of an obtainable card. Only the three a reward can roll — Basic and
// Special are not drawn from (§4.2).
enum class CardRarity { Common, Uncommon, Rare };

// How many cards a normal reward offers. Question Card makes it 4 (§8); that
// relic does not exist yet.
inline constexpr int kCardRewardSize = 3;

// The room a reward came from. Elites roll better, and a boss reward is always
// rare (§4.2).
enum class RewardSource { Monster, Elite, Boss };

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

  // 0 is Neow, before the map. Floor N is map row N-1, so floor 15 is the top
  // row and floor 16 would be the boss.
  int floor = 0;

  // Where on the current row the player is standing. Meaningless at floor 0.
  int column = 0;

  // Terminates after clearing this many floors. Stands in for the Act 1 boss,
  // which is Phase 7 — a real run ends when the boss dies, not on a count.
  int final_floor = kMapHeight;

  // The act's map, generated at run start and fixed thereafter.
  Map map;

  int hp = IRONCLAD_MAX_HP;
  int max_hp = IRONCLAD_MAX_HP;
  int gold = 0;

  // The truth about what the player owns. Combat gets a copy.
  std::vector<Card> master_deck;

  // Monotonic source of card identity. Reproducible from the run seed because
  // it depends only on the order cards are acquired, which is itself seeded —
  // deliberately not a hash or a global (§3.2).
  int next_card_uid = 0;

  // The pity counter behind card-reward rarity (§4.2). Starts at +5 and is
  // ADDED to the roll, so a LOWER value makes rares more likely; commons walk
  // it down to a floor of -40 and a rare resets it.
  //
  // The wiki describes the same system with the opposite sign (an offset from
  // -5 rising to +40, applied to the rare chance). Both are the same mechanic:
  // offset_wiki == -card_rarity_factor. Do not "fix" one into the other — see
  // §15's standing traps.
  int card_rarity_factor = 5;

  // What the current reward screen is offering. Empty outside Phase::Reward.
  std::vector<Card> card_reward;

  // Which kind of fight is in progress, so the reward it pays out can be rolled
  // correctly. Set by begin_combat from the encounter pool.
  RewardSource combat_source = RewardSource::Monster;

  // The `?` room pity counters (§4.1). Each rises when its outcome does NOT
  // happen and resets when it does; Event is the fallback taken when none hit,
  // which is why it has no counter and is the common result.
  float monster_chance = 0.10f;
  float shop_chance = 0.03f;
  float treasure_chance = 0.02f;

  // A `?` cannot become a shop if the previous room was one — and this is set
  // whether that shop was a Shop room or a `?` that resolved into one.
  bool last_room_was_shop = false;

  // What the `?` on the current floor turned into. Equals the map's room type
  // for every other room. The map itself never records this: a `?` stays `?`
  // (§5.4), and this is what the phase is derived from.
  RoomType current_room = RoomType::None;

  // Owned, not inherited (§3). Populated by begin_combat.
  CombatState combat;
  bool in_combat = false;

  Phase phase = Phase::Neow;

  // Run-level result, reusing combat's Outcome rather than defining a parallel
  // enum. InProgress until the player dies; Won needs a boss to kill, which is
  // Phase 7 work (§1, roadmap).
  Outcome outcome = Outcome::InProgress;

  bool is_terminal() const { return outcome != Outcome::InProgress; }

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
  //
  // Sets outcome to Lost if the player died, otherwise moves to Reward.
  void end_combat();

  // Columns the player may move to from where they stand. From floor 0 this is
  // every room on the map's first row; otherwise it is the current node's
  // out-edges. Empty once there is nowhere left to go.
  std::vector<int> available_paths() const;

  // Moves to `column` on the next floor and enters whatever is there. A `?` is
  // resolved here, on entry (§4.1) — which is the only moment its contents are
  // decided.
  //
  // Ignores a column that is not in available_paths(); the action mask is what
  // should have prevented it.
  void choose_path(int column);

  // Directly starts a fight on the current floor. Used by choose_path, and by
  // tests that want a fight without walking a map.
  void advance_to_next_floor(EncounterPool pool);

  // Rolls one card's rarity and advances the pity counter (§4.2).
  CardRarity roll_card_rarity(std::mt19937& rng, RewardSource source);

  // Fills `card_reward` with kCardRewardSize distinct cards for the current
  // floor. Called on entering Phase::Reward.
  void generate_card_reward(RewardSource source);

  // Takes the card at `index` from the reward into the master deck, then closes
  // the screen. Out-of-range is ignored rather than fatal — the action mask is
  // what should have prevented it.
  void take_card_reward(int index);

  // Closes the reward screen without taking anything.
  void skip_card_reward();

  // Rolls what a `?` becomes, and advances the pity counters (§4.1). Public
  // because the distribution is worth testing directly — it is the piece most
  // likely to be implemented with the sign inverted.
  RoomType resolve_unknown_room();

  // Enters `room` on the current floor, setting the phase and doing whatever
  // the room does on arrival.
  void enter_room(RoomType room);
};

}  // namespace minispire

#endif  // MINISPIRE_RUN_STATE_H
