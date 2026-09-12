#ifndef MINISPIRE_RUN_STATE_H
#define MINISPIRE_RUN_STATE_H

#include <cstdint>
#include <optional>
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

// What you can do at a campfire (§8).
//
// Five, not six: Recall obtains the Ruby Key and is Act 4 content, so it can
// never occur in v2.0.0 and reserving a slot for it would be a dead index
// (§5.1 rule 5).
//
// Lift, Toke and Dig each need a relic — Girya, Peace Pipe, Shovel — all of
// which ARE reachable in Act 1, so they keep their slots even though relics do
// not exist yet.
enum class RestOption {
  Rest = 0,   // heal 30% of max HP
  Smith,      // upgrade a card in the master deck
  Lift,       // Girya
  Toke,       // Peace Pipe
  Dig,        // Shovel
};

inline constexpr int kNumRestOptions = 5;

// Fraction of max HP a campfire restores. Truncated, as the game does.
inline constexpr float kRestHealFraction = 0.30f;

// Which tier a relic reward comes from for MOST sources, elites included:
// 50% common, 33% uncommon, 17% rare (wiki-confirmed).
//
// Deliberately its own constant even though it currently matches
// kChestSizeChances digit for digit. They are unrelated mechanics that happen to
// share three numbers, and sharing the array would mean a correction to chest
// sizes silently reassigned every elite relic.
inline constexpr int kRelicTierChances[] = {50, 33, 17};
RelicTier relic_tier_standard(std::mt19937& rng);

// Treasure chests do NOT use the standard distribution. A chest first rolls a
// size — 50% small, 33% medium, 17% large — and the size sets the tier odds:
//
//   small   75 / 25 /  0
//   medium  35 / 50 / 15
//   large    0 / 75 / 25
//
// So a large chest can never give a common, and a small can never give a rare.
// Cross-checked against the wiki's published aggregate of 49 / 42 / 9, which
// these reconstruct once weighted by size: .50(75)+.33(35)+.17(0) = 49.05,
// .50(25)+.33(50)+.17(75) = 41.75, .50(0)+.33(15)+.17(25) = 9.2.
enum class ChestSize { Small, Medium, Large };

inline constexpr int kChestSizeChances[] = {50, 33, 17};
// {common, uncommon} per size; rare is the remainder.
inline constexpr int kChestTierChances[3][2] = {{75, 25}, {35, 50}, {0, 75}};
inline constexpr int kChestGoldChances[] = {50, 35, 50};
inline constexpr int kChestGoldAmounts[] = {25, 50, 75};

// --- shops (§4.3) ---

// Base prices by card rarity: common, uncommon, rare.
inline constexpr int kCardRarityPrices[] = {50, 75, 150};

// Card removal starts here and gets permanently dearer each time it is used —
// the counter is per RUN, not per shop, which makes removal a resource the
// whole run competes for rather than a per-shop choice.
inline constexpr int kBaseRemovePrice = 75;
inline constexpr int kRemovePriceIncrease = 25;

// A shop's five class-card slots. Two attacks, two skills, one power, and the
// power slot promotes a COMMON roll to UNCOMMON.
inline constexpr int kShopCardSlots = 5;

// One item on offer.
struct ShopItem {
  Card card;
  // Visible to a player as the card's border colour, and what sets the price.
  CardRarity rarity = CardRarity::Common;
  int price = 0;
  bool sold = false;
  // The shop marks one card as discounted, and a player sees that tag. It is
  // not inferable from the price — a halved rare still costs more than a
  // full-price common — so under §1's parity rule it has to be state.
  bool on_sale = false;
};

struct ShopRelic {
  RelicId id = RelicId::BurningBlood;
  int price = 0;
  bool sold = false;
};

struct ShopPotion {
  PotionId id = PotionId::BloodPotion;
  int price = 0;
  bool sold = false;
};

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

  // Owned ACROSS fights; projected into CombatState for the duration of one
  // (§3.0.1). Relic order is acquisition order, which is a stated parity defect
  // rather than a limitation (§5.8) — the real game's ordering affects trigger
  // resolution.
  std::vector<HeldRelic> relics;
  std::vector<PotionId> potions;

  // Potion Belt raises this by 2; Sozu takes potions away entirely.
  int potion_slots = kBasePotionSlots;

  // Potion drop chance drifts by ±10 around a 40% base (§4.2). Note it goes
  // DOWN after a drop — it is not a one-way pity counter, and modelling it as
  // one would make potions far too common.
  int potion_chance_bonus = 0;

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

  // Pays out a fight's gold (§4.2): 10–20 normal, 25–35 elite, 100±5 boss.
  void award_combat_gold(RewardSource source);

  // --- relics and potions ---

  bool has_relic(RelicId id) const;

  // Takes a relic. Duplicates are ignored: you cannot hold two of the same.
  // False when nothing was gained — the relic is already held. Callers that
  // charge for it MUST check, or the payment buys nothing.
  //
  // Also fires the relic's PICKUP effect, if it has one (Strawberry's Max HP,
  // War Paint's upgrades). Those run here rather than at a trigger site because
  // there is no fight to queue them into.
  bool obtain_relic(RelicId id);

  // Raise Max HP by `amount` and heal by the same — gaining Max HP heals in
  // StS, which is a general rule rather than a per-relic quirk.
  void gain_max_hp(int amount);

  // Upgrade up to `count` random upgradable cards of `type` in the master deck,
  // drawn without replacement. Returns how many were actually upgraded, which
  // can be fewer than asked when the deck has run out of eligible cards.
  // `source` indexes the RNG stream, so each relic's draw is its own.
  int upgrade_random_cards(CardType type, int count, RelicId source);

  // Takes a potion if a slot is free. Returns false when the belt is full,
  // which is a real decision point in the game rather than an error.
  bool obtain_potion(PotionId id);

  // Discards the potion at `index`.
  void discard_potion(int index);

  // Rolls a post-combat potion drop and its drift (§4.2). Takes how many
  // rewards are already on the screen, because four suppresses the drop —
  // potions are coupled to the other rewards, not independent of them.
  void roll_potion_drop(int rewards_already_on_screen);

  // Draws a relic of `tier`, excluding any already held so a run never sees a
  // duplicate offered.
  // Empty when the tier is exhausted — every relic in it is already held. That
  // is reachable late in a long run, and returning a held relic instead would
  // make the award silently evaporate inside obtain_relic.
  //
  // `exclude` keeps a single shop's shelf from stocking one relic twice: the
  // draw is without replacement against held relics AND against this list.
  std::optional<RelicId> random_relic(
      RelicTier tier, std::mt19937& rng,
      const std::vector<RelicId>& exclude = {}) const;

  // Opens a treasure chest: rolls its size, then gold and the relic tier from
  // a SINGLE shared roll (§4).
  void open_chest();

  // --- shops (§4.3) ---

  // What the current shop is selling. Empty outside Phase::Shop.
  //
  // Still missing the 2 colorless card slots, which need colorless cards.
  std::vector<ShopItem> shop_cards;
  std::vector<ShopRelic> shop_relics;
  std::vector<ShopPotion> shop_potions;

  // Price of removing a card here, or -1 once removal has been used.
  int shop_remove_price = 0;

  // How many times removal has been bought THIS RUN. Removal gets permanently
  // dearer, so this is run state rather than shop state.
  int shop_remove_count = 0;

  // Stocks the shop on arrival.
  void generate_shop();

  // Buys `shop_cards[index]` if it is affordable and unsold.
  void buy_card(int index);

  void buy_relic(int index);

  // Refuses when the belt is full, rather than taking the gold for nothing.
  void buy_potion(int index);

  // Pays for a removal and takes `master_deck[deck_index]` out of the deck.
  void buy_card_removal(int deck_index);

  // Leaves the shop.
  void leave_shop();

  // --- campfire (§8) ---

  // Which campfire options are currently legal. Smith drops out when nothing
  // in the deck can be upgraded.
  std::vector<RestOption> rest_options() const;

  // Heals 30% of max HP, truncated, and leaves the campfire.
  void rest_heal();

  // Upgrades `master_deck[index]` and leaves the campfire. A campfire smith is
  // permanent — it mutates the master deck DIRECTLY rather than going through
  // a fight's write-back, which is why a mid-combat Armaments upgrade and this
  // do not need telling apart at the handoff (§3.2).
  void rest_smith(int index);

  // Which master-deck indices Smith may target.
  std::vector<int> smithable_cards() const;

  // Rolls what a `?` becomes, and advances the pity counters (§4.1). Public
  // because the distribution is worth testing directly — it is the piece most
  // likely to be implemented with the sign inverted.
  RoomType resolve_unknown_room();

  // Enters `room` on the current floor, setting the phase and doing whatever
  // the room does on arrival.
  void enter_room(RoomType room);

  // Finishes with the current room: back to the map, or the run is won if this
  // was the last floor. Every room's exit goes through here so the win check
  // lives in exactly one place.
  void leave_room();
};

}  // namespace minispire

#endif  // MINISPIRE_RUN_STATE_H
