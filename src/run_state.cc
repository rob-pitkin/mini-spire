#include "run_state.h"

#include <algorithm>

#include "action.h"  // Hook, ResolutionContext, fire_relic_hooks, drain

namespace minispire {

void RunState::add_card(Card card) {
  card.uid = next_card_uid++;
  master_deck.push_back(card);

  // Ceramic Fish: 9 gold for every card that joins the deck (decompiled
  // CeramicFish.onObtainCard). Through gain_gold, so Ectoplasm refuses it like
  // any other gain.
  //
  // This is the single deck-add path — the shop, card rewards and events all
  // arrive here — which is what lets the relic be written once. RunState::start
  // deals the ten starter cards through it as well, and that would pay out ten
  // times; it does not, because start obtains only Burning Blood before dealing
  // and nothing can grant Ceramic Fish on floor 0. Unreachable rather than
  // guarded, and worth re-checking if a Neow bonus ever hands out a relic
  // before the deck exists.
  if (has_relic(RelicId::CeramicFish)) gain_gold(kCeramicFishGold);
}

RunState RunState::start(uint64_t run_seed) {
  RunState run;
  run.run_seed = run_seed;
  run.floor = 0;
  run.hp = IRONCLAD_MAX_HP;
  run.max_hp = IRONCLAD_MAX_HP;

  // The starter relic. Every Ironclad run begins holding Burning Blood, and the
  // relic vocabulary's count depends on it: Black Blood is excluded from the 141
  // precisely because relicCanSpawn(BLACK_BLOOD) tests has(BURNING_BLOOD) and
  // that is true (relic.h). A run that did not hold it would make the exclusion
  // unjustified and quietly cost ~6 HP a fight once effects land.
  run.obtain_relic(RelicId::BurningBlood);

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

bool RunState::has_relic(RelicId id) const {
  for (const HeldRelic& r : relics) {
    if (r.id == id) return true;
  }
  return false;
}

HeldRelic* RunState::held_relic(RelicId id) {
  for (HeldRelic& r : relics) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

void RunState::gain_max_hp(int amount) {
  // Gaining Max HP HEALS by the same amount — the general rule in StS, not a
  // per-relic quirk, and the reason Strawberry is worth 7 HP now as well as 7
  // capacity. (Mark of the Bloom blocks the heal, but it is an Act 2+ event
  // relic and outside the vocabulary.)
  max_hp += amount;
  hp += amount;
  if (hp > max_hp) hp = max_hp;
}

void RunState::lose_max_hp(int amount) {
  // Deliberately not the inverse of gain_max_hp. Decompiled decreaseMaxHealth:
  // the maximum drops, floors at 1, and current HP follows ONLY if it is now
  // above the maximum. Gaining heals; losing does not wound.
  if (amount <= 0) return;
  max_hp -= amount;
  if (max_hp < 1) max_hp = 1;
  if (hp > max_hp) hp = max_hp;
}

bool RunState::remove_card_from_deck(int index) {
  if (index < 0 || index >= static_cast<int>(master_deck.size())) return false;
  const CardData& data = CARD_DATABASE.at(master_deck[index].card_id);
  // Curse of the Bell: "cannot be removed from your deck", and per the wiki it
  // cannot be transformed either — the same gate, since a transform removes.
  if (data.cannot_be_removed) return false;

  // Parasite: 3 Max HP when it leaves the MASTER DECK (decompiled
  // Parasite.onRemoveFromMasterDeck -> decreaseMaxHealth(3)). Not on exhaust,
  // which is why Blue Candle is the intended out.
  const int penalty = data.max_hp_loss_on_removal;
  master_deck.erase(master_deck.begin() + index);
  if (penalty > 0) lose_max_hp(penalty);
  return true;
}

int RunState::upgrade_random_cards(std::optional<CardType> type, int count,
                                   RelicId source) {
  // Indexed by the relic's own id (see RngStream::RelicEffect): two relics can
  // be picked up on the same floor, so a floor index would give both the same
  // upgrades.
  std::mt19937 rng =
      make_stream(run_seed, RngStream::RelicEffect,
                  static_cast<uint32_t>(source));

  // Eligible = right type AND actually upgradable. A deck of already-upgraded
  // Strikes leaves War Paint nothing to do, which is a real outcome rather than
  // an error.
  //
  // No type means every upgradable card is eligible — Tiny House, which shuffles
  // all of them together rather than filtering.
  std::vector<int> eligible;
  for (size_t i = 0; i < master_deck.size(); ++i) {
    if (type && CARD_DATABASE.at(master_deck[i].card_id).type != *type) continue;
    Card probe = master_deck[i];
    if (upgrade_card_in_place(probe)) eligible.push_back(static_cast<int>(i));
  }

  // Drawn WITHOUT REPLACEMENT, the same discipline as card rewards and shop
  // stock: a retry loop would consume a variable number of draws and shift
  // every later roll in the run.
  int upgraded = 0;
  for (int n = 0; n < count && !eligible.empty(); ++n) {
    const size_t pick = std::uniform_int_distribution<size_t>(
        0, eligible.size() - 1)(rng);
    if (upgrade_card_in_place(master_deck[eligible[pick]])) ++upgraded;
    eligible.erase(eligible.begin() + static_cast<long>(pick));
  }
  return upgraded;
}

bool RunState::obtain_relic(RelicId id) {
  if (has_relic(id)) return false;
  relics.push_back(HeldRelic{id, 0});

  // Pickup effects (§3.2). These fire once, the moment the relic is taken —
  // they are not combat triggers and do not go through the action queue,
  // because there is no fight to queue into.
  switch (id) {
    // Potion Belt widens the belt the moment it is picked up, so a potion that
    // would not have fit a second ago now does.
    case RelicId::PotionBelt:
      potion_slots += 2;
      break;

    case RelicId::Strawberry: gain_max_hp(7); break;
    case RelicId::Pear: gain_max_hp(10); break;
    case RelicId::Mango: gain_max_hp(14); break;

    case RelicId::LeesWaffle:
      // +7 Max HP *and* a full heal — not the same as the other three, whose
      // heal is only the incidental Max-HP one.
      gain_max_hp(7);
      hp = max_hp;
      break;

    case RelicId::WarPaint:
      upgrade_random_cards(CardType::Skill, 2, id);
      break;
    case RelicId::Whetstone:
      upgrade_random_cards(CardType::Attack, 2, id);
      break;

    // Straight through gain_gold, so Ectoplasm refuses it exactly as it refuses
    // every other gain. StS routes this through player.gainGold for the same
    // reason.
    case RelicId::OldCoin:
      gain_gold(kOldCoinGold);
      break;

    case RelicId::TinyHouse: {
      // Four payouts at once (decompiled TinyHouse.onEquip): upgrade one random
      // card, +5 Max HP, 50 gold, one potion.
      //
      // NOT a card reward. The community description often says "1 card", and
      // onEquip does not add one — it calls addGoldToRewards and
      // addPotionToRewards only. Writing the remembered version would have
      // handed out a card the game does not.
      upgrade_random_cards(std::nullopt, 1, id);
      gain_max_hp(kTinyHouseMaxHp);
      gain_gold(kTinyHouseGold);

      // A FLAT draw over every potion, not the rarity-weighted roll a combat
      // drop uses: getRandomPotion picks uniformly from the whole potion list.
      //
      // This stream is seeded identically to the one upgrade_random_cards just
      // built, since both index RelicEffect by this relic's id, so the potion
      // and the upgrade are drawn from the same first value. They land in
      // different ranges and so look independent; it is an artifact of each
      // helper owning its own generator, recorded rather than hidden.
      std::mt19937 rng = make_stream(run_seed, RngStream::RelicEffect,
                                     static_cast<uint32_t>(id));
      const PotionId rolled = static_cast<PotionId>(
          std::uniform_int_distribution<int>(0, kNumPotions - 1)(rng));
      // A full belt, or Sozu, means the potion is simply lost — obtain_potion
      // is the single authority on that, the same as every other award.
      obtain_potion(rolled);
      break;
    }

    default:
      break;
  }
  return true;
}

bool RunState::obtain_potion(PotionId id) {
  // Sozu means no potions at all; the belt being full is an ordinary and
  // deliberate part of play rather than a failure.
  if (has_relic(RelicId::Sozu)) return false;
  if (static_cast<int>(potions.size()) >= potion_slots) return false;
  potions.push_back(id);
  return true;
}

void RunState::discard_potion(int index) {
  if (index < 0 || index >= static_cast<int>(potions.size())) return;
  potions.erase(potions.begin() + index);
}

RelicTier relic_tier_standard(std::mt19937& rng) {
  const int roll = std::uniform_int_distribution<int>(0, 99)(rng);
  if (roll < kRelicTierChances[0]) return RelicTier::Common;
  if (roll < kRelicTierChances[0] + kRelicTierChances[1]) {
    return RelicTier::Uncommon;
  }
  return RelicTier::Rare;
}

std::optional<RelicId> RunState::random_relic(
    RelicTier tier, std::mt19937& rng,
    const std::vector<RelicId>& exclude) const {
  const std::vector<RelicId>& pool = relic_pool(tier);
  // Drawn WITHOUT REPLACEMENT against what is already held, rather than
  // re-rolling until new: a retry loop consumes a variable number of draws, so
  // an unrelated change would shift every later roll in the run.
  std::vector<RelicId> candidates;
  for (RelicId id : pool) {
    if (has_relic(id)) continue;
    if (std::find(exclude.begin(), exclude.end(), id) != exclude.end()) continue;
    candidates.push_back(id);
  }
  // A tier CAN be exhausted late in a long run. Signalling it is the point:
  // returning a held relic here would have obtain_relic reject it, and the
  // award would vanish with no caller able to tell.
  if (candidates.empty()) return std::nullopt;
  return candidates[std::uniform_int_distribution<size_t>(
      0, candidates.size() - 1)(rng)];
}

void RunState::roll_potion_drop(int rewards_already_on_screen) {
  std::mt19937 rng =
      make_stream(run_seed, RngStream::Potion, static_cast<uint32_t>(floor));

  // Elite and normal fights drop potions at the same rate — the fight's kind
  // does not enter into it, which is why this takes no RewardSource.
  int chance = 40 + potion_chance_bonus;
  if (has_relic(RelicId::WhiteBeastStatue)) chance = 100;

  // A reward screen already holding four things drops no potion (§4.2). This
  // couples potions to gold, relic and card rewards rather than leaving them
  // independent.
  //
  // Note this sets the chance to zero rather than returning: the roll still
  // happens and still MISSES, so the +10 drift applies. A suppressed screen
  // makes the next fight likelier to drop, exactly as the spec has it. Bailing
  // out early would quietly lower the cumulative drop rate of any run that hit
  // several full screens.
  if (rewards_already_on_screen >= 4) chance = 0;

  const int roll = std::uniform_int_distribution<int>(0, 99)(rng);
  if (roll >= chance) {
    // Missed: the next fight is likelier to drop one.
    potion_chance_bonus += 10;
    return;
  }

  // Hit: the chance goes DOWN. Symmetric drift, not a one-way pity counter.
  potion_chance_bonus -= 10;

  // Rarity roll, then draw. Common 65 / uncommon 25 / rare 10.
  const int rarity_roll = std::uniform_int_distribution<int>(0, 99)(rng);
  const PotionRarity rarity = rarity_roll < 65    ? PotionRarity::Common
                              : rarity_roll < 90  ? PotionRarity::Uncommon
                                                  : PotionRarity::Rare;
  const std::vector<PotionId>& pool = potion_pool(rarity);
  const PotionId drop =
      pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];

  // A full belt simply means the drop is lost, which is what happens in game.
  obtain_potion(drop);
}

void RunState::open_chest() {
  // The RELIC stream, not the treasure stream — §4 and RngStream's own comment
  // both assign a chest's relic here, and it is the stream the elite reward
  // already uses. The two relic sources agreeing matters more than the name:
  // drawing from Treasure needed a magic floor offset to dodge a collision with
  // award_combat_gold, which is a sign of using the wrong stream, not a fix.
  std::mt19937 rng =
      make_stream(run_seed, RngStream::Relic, static_cast<uint32_t>(floor));

  const int size_roll = std::uniform_int_distribution<int>(0, 99)(rng);
  const ChestSize size =
      size_roll < kChestSizeChances[0]
          ? ChestSize::Small
          : size_roll < kChestSizeChances[0] + kChestSizeChances[1]
                ? ChestSize::Medium
                : ChestSize::Large;
  const int s = static_cast<int>(size);

  // ONE roll decides both whether there is gold and which tier the relic is,
  // and the correlation that creates is the mechanic rather than a side effect.
  // The wiki states the rule directly: a chest gives gold only if it also rolled
  // the LOWEST rarity available to that chest size. A single roll reproduces
  // that exactly, because each size's gold band sits inside its lowest-tier
  // band — small 50 within common 75, medium 35 within common 35, large 50
  // within uncommon 75 (a large chest has no common).
  //
  // So for a large chest this is a hard exclusion, not a tendency: a rare from a
  // large chest can never come with gold. Two independent rolls would leave both
  // marginals looking correct and silently break that.
  const int roll = std::uniform_int_distribution<int>(0, 99)(rng);

  if (roll < kChestGoldChances[s]) gold += kChestGoldAmounts[s];

  const int common_chance = kChestTierChances[s][0];
  const int uncommon_chance = kChestTierChances[s][1];
  const RelicTier tier = roll < common_chance ? RelicTier::Common
                         : roll < common_chance + uncommon_chance
                             ? RelicTier::Uncommon
                             : RelicTier::Rare;

  if (const std::optional<RelicId> drawn = random_relic(tier, rng)) {
    obtain_relic(*drawn);
  }
}

void RunState::award_combat_gold(RewardSource source) {
  std::mt19937 rng =
      make_stream(run_seed, RngStream::Treasure, static_cast<uint32_t>(floor));

  int amount;
  switch (source) {
    case RewardSource::Elite:
      amount = std::uniform_int_distribution<int>(25, 35)(rng);
      break;
    case RewardSource::Boss:
      amount = 100 + std::uniform_int_distribution<int>(-5, 5)(rng);
      break;
    case RewardSource::Monster:
    default:
      amount = std::uniform_int_distribution<int>(10, 20)(rng);
      break;
  }
  // Golden Idol: +25%, rounding the BONUS to nearest and adding it — not
  // rounding the whole 1.25x product. The two agree on most values and are not
  // the same operation, so the reference's form is reproduced exactly.
  if (has_relic(RelicId::GoldenIdol)) {
    amount += static_cast<int>(
        std::lround(static_cast<float>(amount) * kGoldenIdolBonus));
  }

  // Ectoplasm is applied by gain_gold, LAST, so it overrides Golden Idol rather
  // than racing it — a run holding both gains nothing, which is the only
  // reading that makes sense of "you can no longer gain Gold".
  gain_gold(amount);
}

void RunState::gain_gold(int amount) {
  if (amount <= 0) return;
  // The single gold-gain path, so Ectoplasm cannot be forgotten by a new
  // caller. Golden Idol deliberately does NOT live here: in StS its +25% is
  // applied to a combat's REWARD pile, so Hand of Greed's kill gold — which
  // comes straight through here — is not boosted by it.
  if (has_relic(RelicId::Ectoplasm)) return;
  gold += amount;
}

void RunState::spend_gold(int amount) {
  // A zero-gold payment is not a spend, so it does not trip Maw Bank. Callers
  // have already checked they can afford the price; this debits and notifies
  // rather than authorising, so it does not re-check affordability either.
  if (amount <= 0) return;
  gold -= amount;

  // Maw Bank stops the first time gold is spent, on ANYTHING — decompiled
  // MawBank.onSpendGold has no room check. Reading it as "disabled by shop
  // purchases" would be right today and wrong the moment something else costs
  // gold, which is why this lives in the spend path rather than in the four
  // buy_* callers.
  if (HeldRelic* maw = held_relic(RelicId::MawBank)) {
    maw->counter = kMawBankUsedUp;
  }
}

std::vector<int> RunState::smithable_cards() const {
  std::vector<int> indices;
  for (size_t i = 0; i < master_deck.size(); ++i) {
    // is_upgradable covers both the ordinary "+" edge and a rung ladder's top,
    // where Searing Blow stays upgradable forever.
    Card probe = master_deck[i];
    if (upgrade_card_in_place(probe)) indices.push_back(static_cast<int>(i));
  }
  return indices;
}

std::vector<RestOption> RunState::rest_options() const {
  std::vector<RestOption> options;
  if (phase != Phase::Rest) return options;

  // Coffee Dripper removes Rest outright — the only thing that can, and the
  // reason this is not unconditional.
  if (!has_relic(RelicId::CoffeeDripper)) options.push_back(RestOption::Rest);

  // Smith drops out when there is nothing left to upgrade, or when Fusion
  // Hammer forbids it.
  if (!has_relic(RelicId::FusionHammer) && !smithable_cards().empty()) {
    options.push_back(RestOption::Smith);
  }

  // Lift / Toke / Dig are unlocked by Girya / Peace Pipe / Shovel. Girya is
  // additionally capped: three uses, tracked on the relic's own counter.
  if (has_relic(RelicId::Girya) && girya_uses() < kGiryaMaxUses) {
    options.push_back(RestOption::Lift);
  }
  if (has_relic(RelicId::PeacePipe) && !master_deck.empty()) {
    options.push_back(RestOption::Toke);
  }
  if (has_relic(RelicId::Shovel)) options.push_back(RestOption::Dig);

  return options;
}

int RunState::girya_uses() const {
  for (const HeldRelic& r : relics) {
    if (r.id == RelicId::Girya) return r.counter;
  }
  return 0;
}

void RunState::rest_heal() {
  if (phase != Phase::Rest) return;
  if (has_relic(RelicId::CoffeeDripper)) return;  // not an option at all

  int healed = static_cast<int>(static_cast<float>(max_hp) * kRestHealFraction);
  // Regal Pillow: a FLAT extra 15, added after the percentage rather than
  // folded into it. Scaling the fraction instead would make the bonus depend on
  // Max HP, which it does not.
  if (has_relic(RelicId::RegalPillow)) healed += kRegalPillowHeal;
  hp = std::min(max_hp, hp + healed);

  // Dream Catcher fires on REST specifically, not on any campfire option — the
  // wiki is explicit, and it is why this sits here rather than in leave_room.
  // The card reward is generated before leaving, so the room exits into the
  // reward screen the way a fight does.
  if (has_relic(RelicId::DreamCatcher)) {
    generate_card_reward(RewardSource::Monster);
    return;  // leave_room happens when the reward is taken or skipped
  }
  leave_room();
}

void RunState::rest_smith(int index) {
  if (phase != Phase::Rest) return;
  if (has_relic(RelicId::FusionHammer)) return;
  if (index < 0 || index >= static_cast<int>(master_deck.size())) return;
  // Mutates the master deck directly: a campfire smith is permanent and never
  // passes through a fight, which is why it needs no write-back machinery.
  if (!upgrade_card_in_place(master_deck[index])) return;
  leave_room();
}

void RunState::rest_lift() {
  if (phase != Phase::Rest) return;
  if (!has_relic(RelicId::Girya) || girya_uses() >= kGiryaMaxUses) return;

  // The counter IS the Strength. Girya grants "1 Strength at the start of every
  // combat" per Lift used, so the relic's run-scoped counter is read at combat
  // start rather than a separate permanent-Strength field being kept in sync.
  for (HeldRelic& r : relics) {
    if (r.id == RelicId::Girya) ++r.counter;
  }
  leave_room();
}

void RunState::rest_toke(int index) {
  if (phase != Phase::Rest) return;
  if (!has_relic(RelicId::PeacePipe)) return;
  // A refused removal does not consume the rest site: in StS the card simply
  // cannot be picked on the Toke screen, so the option is still there.
  if (!remove_card_from_deck(index)) return;
  leave_room();
}

void RunState::rest_dig() {
  if (phase != Phase::Rest) return;
  if (!has_relic(RelicId::Shovel)) return;

  // Dig gives a relic on the standard tier roll, the same distribution an elite
  // pays — NOT the chest's size-based one (§4.0.1), which is a different
  // mechanic that happens to share three numbers.
  std::mt19937 rng =
      make_stream(run_seed, RngStream::Relic, static_cast<uint32_t>(floor));
  if (const std::optional<RelicId> drawn =
          random_relic(relic_tier_standard(rng), rng)) {
    obtain_relic(*drawn);
  }
  leave_room();
}

namespace {

// The Ironclad pool of one rarity, filtered to one card type. A shop's slots
// are typed — two attacks, two skills, one power — so it draws from these
// rather than from a rarity pool wholesale.
std::vector<CardId> pool_of(CardRarity rarity, CardType type) {
  const std::vector<CardId>& pool = rarity == CardRarity::Rare
                                        ? IRONCLAD_RARE_POOL
                                        : rarity == CardRarity::Uncommon
                                              ? IRONCLAD_UNCOMMON_POOL
                                              : IRONCLAD_COMMON_POOL;
  std::vector<CardId> out;
  for (CardId id : pool) {
    if (CARD_DATABASE.at(id).type == type) out.push_back(id);
  }
  return out;
}

}  // namespace

void RunState::generate_shop() {
  shop_cards.clear();

  std::mt19937 rng =
      make_stream(run_seed, RngStream::Shop, static_cast<uint32_t>(floor));

  // Shop rarity uses a DIFFERENT base rare chance from a combat reward — 9
  // against 3 — but shares card_rarity_factor with it. So buying at a shop and
  // taking card rewards drift each other's odds; they are not independent.
  auto roll_shop_rarity = [&]() {
    constexpr int kBaseRare = 9;
    constexpr int kBaseUncommon = 37;
    const int roll =
        std::uniform_int_distribution<int>(0, 99)(rng) + card_rarity_factor;
    if (roll < kBaseRare) return CardRarity::Rare;
    if (roll >= kBaseRare + kBaseUncommon) return CardRarity::Common;
    return CardRarity::Uncommon;
  };

  // Draw without replacement: remove what is already on the shelf from the
  // candidate list, then take ONE draw. No retry loop — a retry loop consumes a
  // variable number of draws (so the same seed can diverge on an unrelated
  // change) and has to give up eventually, at which point it returns a
  // duplicate anyway.
  auto draw_distinct = [&](CardType type, CardRarity rarity) {
    std::vector<CardId> candidates;
    for (CardId id : pool_of(rarity, type)) {
      bool already_stocked = false;
      for (const ShopItem& item : shop_cards) {
        if (item.card.card_id == id) already_stocked = true;
      }
      if (!already_stocked) candidates.push_back(id);
    }
    // A rarity band can in principle be exhausted by earlier slots; widen to
    // the whole type rather than fail.
    if (candidates.empty()) {
      for (CardRarity r : {CardRarity::Common, CardRarity::Uncommon,
                           CardRarity::Rare}) {
        for (CardId id : pool_of(r, type)) {
          bool already_stocked = false;
          for (const ShopItem& item : shop_cards) {
            if (item.card.card_id == id) already_stocked = true;
          }
          if (!already_stocked) candidates.push_back(id);
        }
      }
    }
    return candidates[std::uniform_int_distribution<size_t>(
        0, candidates.size() - 1)(rng)];
  };

  const CardType slot_types[kShopCardSlots] = {
      CardType::Attack, CardType::Attack, CardType::Skill, CardType::Skill,
      CardType::Power};

  for (int i = 0; i < kShopCardSlots; ++i) {
    CardRarity rarity = roll_shop_rarity();
    // The power slot never sells a common. A COMMON roll is promoted rather
    // than re-rolled, so it does not consume another draw.
    if (slot_types[i] == CardType::Power && rarity == CardRarity::Common) {
      rarity = CardRarity::Uncommon;
    }
    ShopItem item;
    item.card = Card{draw_distinct(slot_types[i], rarity)};
    item.rarity = rarity;
    const float jitter = std::uniform_real_distribution<float>(0.9f, 1.1f)(rng);
    // Jitter first, then the relic discount — the discount applies to the price
    // the shop actually set, and rolling it in before the jitter would let the
    // random factor scale the discount too.
    item.price = discounted_price(static_cast<int>(
        static_cast<float>(kCardRarityPrices[static_cast<int>(rarity)]) *
        jitter));
    shop_cards.push_back(item);
  }

  // Exactly one of the five is half price.
  const int sale = std::uniform_int_distribution<int>(0, kShopCardSlots - 1)(rng);
  shop_cards[sale].price /= 2;
  shop_cards[sale].on_sale = true;

  // Relic-aware: Smiling Mask pins it, the discount relics scale it.
  shop_remove_price = removal_price();

  // Three relics. The first two roll a tier; the THIRD is always Shop tier,
  // which is what makes shop-exclusive relics obtainable at all.
  //
  // Drawn without replacement against the shelf as well as against held relics:
  // two slots can roll the same tier, and without `already_stocked` they could
  // offer the identical relic twice. Buying the second would charge full price
  // for a relic already owned.
  shop_relics.clear();
  std::vector<RelicId> already_stocked;
  for (int i = 0; i < 3; ++i) {
    const RelicTier tier =
        i == 2 ? RelicTier::Shop : relic_tier_standard(rng);
    const std::optional<RelicId> drawn = random_relic(tier, rng, already_stocked);
    // An exhausted tier leaves the slot empty rather than stocking a duplicate.
    if (!drawn) continue;
    already_stocked.push_back(*drawn);
    ShopRelic offer;
    offer.id = *drawn;
    const float jitter = std::uniform_real_distribution<float>(0.95f, 1.05f)(rng);
    offer.price = discounted_price(static_cast<int>(
        static_cast<float>(kRelicTierPrices[static_cast<int>(tier)]) * jitter));
    shop_relics.push_back(offer);
  }

  // Three potions.
  shop_potions.clear();
  for (int i = 0; i < 3; ++i) {
    const int rarity_roll = std::uniform_int_distribution<int>(0, 99)(rng);
    const PotionRarity rarity = rarity_roll < 65    ? PotionRarity::Common
                                : rarity_roll < 90  ? PotionRarity::Uncommon
                                                    : PotionRarity::Rare;
    const std::vector<PotionId>& pool = potion_pool(rarity);
    ShopPotion offer;
    offer.id = pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];
    const float jitter = std::uniform_real_distribution<float>(0.95f, 1.05f)(rng);
    offer.price = discounted_price(static_cast<int>(
        static_cast<float>(kPotionRarityPrices[static_cast<int>(rarity)]) *
        jitter));
    shop_potions.push_back(offer);
  }

  // The two colorless slots (§4.3): one uncommon, one rare, in that order.
  //
  // Stocked AFTER the sale roll deliberately — StS discounts one of the first
  // five card slots, so a colorless card is never the half-price one.
  //
  // The healing exclusion does NOT apply here: it is a rule about random
  // generation DURING combat (Bandage Up can be bought, just never conjured),
  // so these draw from the full pools.
  const std::pair<const std::vector<CardId>*, CardRarity> colorless_slots[] = {
      {&COLORLESS_UNCOMMON_POOL, CardRarity::Uncommon},
      {&COLORLESS_RARE_POOL, CardRarity::Rare},
  };
  for (const auto& [pool, rarity] : colorless_slots) {
    ShopItem item;
    item.card = Card{(*pool)[std::uniform_int_distribution<size_t>(
        0, pool->size() - 1)(rng)]};
    item.rarity = rarity;
    const float jitter = std::uniform_real_distribution<float>(0.9f, 1.1f)(rng);
    item.price = discounted_price(static_cast<int>(
        static_cast<float>(kCardRarityPrices[static_cast<int>(rarity)]) *
        jitter * kColorlessShopMarkup));
    shop_cards.push_back(item);
  }
}

float RunState::shop_price_multiplier() const {
  // MULTIPLICATIVE, not additive. Membership Card is x0.50 and The Courier
  // x0.80, so holding both is x0.40 — which the wiki states as "totalling a 60%
  // reduction". Adding the discounts (50 + 20 = 70% off) would be a different
  // and wrong number.
  float m = 1.0f;
  if (has_relic(RelicId::MembershipCard)) m *= kMembershipCardFactor;
  if (has_relic(RelicId::TheCourier)) m *= kCourierFactor;
  return m;
}

int RunState::discounted_price(int base) const {
  // Rounded to nearest, halves UP, per the wiki's stated rounding.
  const float p = static_cast<float>(base) * shop_price_multiplier();
  const int rounded = static_cast<int>(std::floor(p + 0.5f));
  return rounded < 0 ? 0 : rounded;
}

int RunState::removal_price() const {
  // Smiling Mask pins the removal service at a flat 50 and is IMMUNE to the
  // discount relics.
  //
  // sts_lightspeed DISAGREES: its Shop::getRemoveCost sets the cost to 50 and
  // then applies the Courier/Membership factors, giving 25 with Membership
  // Card. The wiki is explicit the other way — "Price-reduction effects will
  // not affect the price of card removals if the player possesses Smiling Mask.
  // It will always set the price of the card removal service to 50 Gold even if
  // its price would otherwise be lower." Per CLAUDE.md's rule 2 corollary the
  // wiki is the check, so the wiki's reading is what ships. Logged in
  // v2-spec.md §15 as a live disagreement rather than a settled fact.
  if (has_relic(RelicId::SmilingMask)) return kSmilingMaskRemovalPrice;

  // Deliberately does NOT read shop_remove_price, which carries the per-visit
  // "already used here" sentinel (-1). This answers "what does removal cost at
  // this point in the run", and generate_shop stores the answer.
  return discounted_price(kBaseRemovePrice +
                          kRemovePriceIncrease * shop_remove_count);
}

void RunState::buy_relic(int index) {
  if (phase != Phase::Shop) return;
  if (index < 0 || index >= static_cast<int>(shop_relics.size())) return;
  ShopRelic& offer = shop_relics[index];
  if (offer.sold || gold < offer.price) return;

  // Acquire FIRST, then charge. obtain_relic refuses a relic already held, and
  // paying before checking would take the gold and the slot in exchange for
  // nothing.
  if (!obtain_relic(offer.id)) return;
  spend_gold(offer.price);
  offer.sold = true;
}

void RunState::buy_potion(int index) {
  if (phase != Phase::Shop) return;
  if (index < 0 || index >= static_cast<int>(shop_potions.size())) return;
  ShopPotion& offer = shop_potions[index];
  if (offer.sold || gold < offer.price) return;

  // Acquire FIRST, then charge — and let obtain_potion be the single authority
  // on whether it can be. Re-checking only the full-belt case inline would miss
  // Sozu, and a Sozu run would pay for potions it can never receive.
  if (!obtain_potion(offer.id)) return;
  spend_gold(offer.price);
  offer.sold = true;
}

void RunState::buy_card(int index) {
  if (phase != Phase::Shop) return;
  if (index < 0 || index >= static_cast<int>(shop_cards.size())) return;
  ShopItem& item = shop_cards[index];
  if (item.sold || gold < item.price) return;

  spend_gold(item.price);
  item.sold = true;
  // add_card mints the uid: the card acquires identity when bought, not when
  // it was put on the shelf.
  add_card(item.card);
}

void RunState::buy_card_removal(int deck_index) {
  if (phase != Phase::Shop) return;
  if (shop_remove_price < 0 || gold < shop_remove_price) return;

  // The card leaves BEFORE the gold does. A refused removal — Curse of the
  // Bell, or an index off the end — must not charge for nothing, and must
  // leave the shop's one removal still unspent.
  if (!remove_card_from_deck(deck_index)) return;

  spend_gold(shop_remove_price);
  ++shop_remove_count;
  // One removal per shop; the next one costs more, for the rest of the run.
  shop_remove_price = -1;
}

void RunState::leave_shop() {
  if (phase != Phase::Shop) return;
  // All three shelves, not just the cards: the header documents the whole block
  // as empty outside Phase::Shop, and anything reading it later — the v2
  // observation's shop slots, a serializer, the renderer — would otherwise see
  // a shop that is not there.
  shop_cards.clear();
  shop_relics.clear();
  shop_potions.clear();
  leave_room();
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

void RunState::fire_room_entry_relics(RoomType room) {
  // Acquisition order, iterating `relics` as it stands — the same rule and the
  // same reason as fire_relic_hooks. Neither of these two interacts with the
  // other today, but sorting or grouping the loop would be a silent ordering
  // change the moment one does.
  for (HeldRelic& r : relics) {
    switch (r.id) {
      case RelicId::MawBank:
        // Every room, not only shops, and until the latch trips.
        if (r.counter != kMawBankUsedUp) gain_gold(kMawBankGold);
        break;

      case RelicId::MealTicket:
        // Shops only. Clamped, because a heal cannot carry HP above the
        // maximum.
        if (room == RoomType::Shop) {
          hp = std::min(max_hp, hp + kMealTicketHeal);
        }
        break;

      default:
        break;
    }
  }
}

void RunState::enter_room(RoomType room) {
  current_room = room;
  last_room_was_shop = room == RoomType::Shop;

  // BEFORE the room resolves. A Treasure room opens its chest and calls
  // leave_room below, so firing afterwards would miss those rooms entirely —
  // and Maw Bank's gold has to be in hand before a shop can take it.
  fire_room_entry_relics(room);

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
      // Stock is rolled on ENTERING the shop, not at map generation — a player
      // who reloads before walking in would otherwise see different goods
      // (§3.5 roll timing).
      generate_shop();
      break;
    case RoomType::Treasure:
      // A deterministic pass-through: one relic, no choice (§4). The chest is
      // opened on arrival rather than offering a decision — and the room then
      // EXITS, which is what makes it a pass-through. Setting the phase without
      // leaving would strand the run on floor 9, where every generated map puts
      // a chest.
      phase = Phase::Treasure;
      open_chest();
      leave_room();
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

  // The fight is CONSTRUCTED from the run's state, not built fresh and then
  // patched into shape. Setup reads this — HP, relics, potions — so anything
  // assigned afterwards would be read too late (§3.2).
  CombatSetup setup;
  setup.seed = static_cast<uint32_t>(combat_seed);
  // Card generation gets its own stream, indexed by floor like the fight's.
  // Nothing else may draw from it: the whole point is that a generation roll
  // cannot shift a shuffle, or vice versa (§3.5).
  setup.card_seed = static_cast<uint32_t>(
      derive_stream_seed(run_seed, RngStream::CardRandom,
                         static_cast<uint32_t>(floor)));
  setup.pool = pool;
  setup.deck = master_deck;
  setup.hp = hp;
  setup.max_hp = max_hp;
  setup.relics = relics;
  setup.potions = potions;
  combat = start_combat(std::move(setup));

  in_combat = true;
  phase = Phase::Combat;

  // An elite pays a better reward (§4.2). Boss fights are not an EncounterPool
  // yet — bosses are Phase 7 — so nothing produces RewardSource::Boss today.
  combat_source = pool == EncounterPool::Elite ? RewardSource::Elite
                                               : RewardSource::Monster;
}

void RunState::end_combat() {
  // End-of-combat relics fire on the FIGHT's state, before anything is written
  // back — so Burning Blood's heal lands in one place and is carried out by the
  // write-back below, rather than being applied twice or to the wrong copy.
  //
  // Only on a win. A dead Ironclad does not heal 6 and get back up; Lizard Tail
  // is the relic for that and is a different hook.
  //
  // SEQUENTIALLY, not batched. Burning Blood heals 6 and Meat on the Bone then
  // asks whether HP is at or below half — at 35 of 80 the answer is yes before
  // that heal and no after it. Batched, both read the pre-heal HP and both
  // fire, healing 18 where the real game heals 6.
  if (combat.character.hp > 0) {
    fire_relic_hooks_sequentially(combat, Hook::CombatEnd);
  }

  // Hand of Greed's kill gold, earned inside the fight and recorded there
  // because combat owns no gold (colorless-effects.md D5). Through gain_gold,
  // so Ectoplasm refuses it, and NOT through award_combat_gold, whose Golden
  // Idol bonus belongs to the reward pile rather than to a card's effect.
  gain_gold(combat.gold_gained);
  combat.gold_gained = 0;

  // Carried: HP and Max HP. Max HP because Feed and Neow can raise it mid-fight.
  hp = combat.character.hp;
  max_hp = combat.character.max_hp;

  // Relic counters carry — all of them, with nothing resetting at the boundary
  // (§3.3). Potions carry too, so any drunk during the fight are simply gone.
  relics = combat.relics;
  potions = combat.potions;

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

    // The screen's CONTENTS are decided before anything is granted.
    //
    // This ordering is the whole point. A reward screen in StS is rolled as a
    // unit and then collected, so a relic won from this fight cannot change what
    // else this fight offered. Granting as we went made it possible: an elite
    // dropping White Beast Statue would have had roll_potion_drop see the relic
    // already held and force the drop chance to 100 — that very fight
    // guaranteeing its own potion. Question Card and Prayer Wheel are the same
    // trap against the card reward. Rolling first removes the class, rather than
    // each relic separately.
    //
    // The count is every entry the screen will hold, card reward included (§4.2)
    // — gold, the elite's relic, and the card. Miscounting it moves the
    // four-item suppression threshold by a fight.
    award_combat_gold(combat_source);
    const bool elite = combat_source == RewardSource::Elite;

    // Black Star: an elite drops a SECOND relic. Both are drawn here, before
    // anything is granted, so neither can influence the other's tier roll or
    // the rest of the screen.
    std::vector<RelicId> elite_relics;
    if (elite) {
      std::mt19937 relic_rng =
          make_stream(run_seed, RngStream::Relic, static_cast<uint32_t>(floor));
      const int count = has_relic(RelicId::BlackStar) ? 2 : 1;
      for (int i = 0; i < count; ++i) {
        // Excluding what this same screen already drew, so an elite cannot pay
        // the same relic twice.
        if (const std::optional<RelicId> drawn = random_relic(
                relic_tier_standard(relic_rng), relic_rng, elite_relics)) {
          elite_relics.push_back(*drawn);
        }
      }
    }

    // Prayer Wheel: normal enemies drop an ADDITIONAL card reward — a second
    // screen, and explicitly not on elites or bosses.
    if (combat_source == RewardSource::Monster &&
        has_relic(RelicId::PrayerWheel)) {
      pending_extra_card_rewards += 1;
    }

    const int on_screen = 1 /* gold */ + 1 /* card */ +
                          static_cast<int>(elite_relics.size()) +
                          pending_extra_card_rewards;
    roll_potion_drop(on_screen);
    generate_card_reward(combat_source);

    // Collected last, so nothing above could read them.
    for (RelicId id : elite_relics) obtain_relic(id);
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

  // Question Card adds an option, Busted Crown removes two, and they STACK —
  // holding both gives 3 + 1 - 2 = 2, which the wiki confirms. Floored at zero
  // rather than one: nothing in Act 1 can reduce it that far today, but a
  // negative loop count would be a silent infinite-ish bug rather than an empty
  // screen.
  int size = kCardRewardSize;
  if (has_relic(RelicId::QuestionCard)) size += kQuestionCardExtraCards;
  if (has_relic(RelicId::BustedCrown)) size -= kBustedCrownFewerCards;
  if (size < 0) size = 0;

  for (int i = 0; i < size; ++i) {
    const CardRarity rarity = roll_card_rarity(rng, source);
    const std::vector<CardId>& pool = rarity == CardRarity::Rare
                                          ? IRONCLAD_RARE_POOL
                                          : rarity == CardRarity::Uncommon
                                                ? IRONCLAD_UNCOMMON_POOL
                                                : IRONCLAD_COMMON_POOL;

    // One reward never offers the same card twice. Drawn WITHOUT REPLACEMENT —
    // build the candidate list, take one draw — rather than re-rolling until
    // distinct. A retry loop consumes a variable number of draws, so an
    // unrelated change to the pools would shift every later roll in the run.
    std::vector<CardId> candidates;
    for (CardId id : pool) {
      bool already_offered = false;
      for (const Card& offered : card_reward) {
        if (offered.card_id == id) already_offered = true;
      }
      if (!already_offered) candidates.push_back(id);
    }
    const CardId chosen = candidates[std::uniform_int_distribution<size_t>(
        0, candidates.size() - 1)(rng)];

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

  // Prayer Wheel's extra reward is a SECOND SCREEN, not more cards on the first
  // — the wiki is explicit ("otherwise functionally identical to a normal card
  // reward"). So taking or skipping one reward rolls the next instead of
  // leaving the room, and only the last one exits.
  if (pending_extra_card_rewards > 0) {
    --pending_extra_card_rewards;
    generate_card_reward(combat_source);
    return;
  }
  leave_room();
}

void RunState::leave_room() {
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
