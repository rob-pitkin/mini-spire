// Gold and shops (docs/design/v2-spec.md §4.2, §4.3).

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "run_state.h"

namespace minispire {
namespace {

RunState at_a_shop(uint64_t seed = 1, int gold = 500) {
  RunState run = RunState::start(seed);
  run.floor = 5;
  run.gold = gold;
  run.phase = Phase::Shop;
  run.generate_shop();
  return run;
}

// ------------------------------------------------------------------ gold

TEST(Gold, ANormalFightPaysTenToTwenty) {
  for (uint64_t s = 0; s < 60; ++s) {
    RunState run = RunState::start(s);
    run.begin_combat(EncounterPool::Weak);
    run.end_combat();
    EXPECT_GE(run.gold, 10) << "seed " << s;
    EXPECT_LE(run.gold, 20) << "seed " << s;
  }
}

TEST(Gold, AnElitePaysMoreThanANormalFight) {
  long normal = 0, elite = 0;
  for (uint64_t s = 0; s < 60; ++s) {
    RunState a = RunState::start(s);
    a.begin_combat(EncounterPool::Weak);
    a.end_combat();
    normal += a.gold;

    RunState b = RunState::start(s);
    b.begin_combat(EncounterPool::Elite);
    b.end_combat();
    elite += b.gold;
  }
  EXPECT_GT(elite, normal);
}

TEST(Gold, AccumulatesAcrossFights) {
  RunState run = RunState::start(3);
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  const int after_one = run.gold;
  run.skip_card_reward();

  run.advance_to_next_floor(EncounterPool::Weak);
  run.end_combat();
  EXPECT_GT(run.gold, after_one);
}

// Dying pays nothing — the reward screen never opens.
TEST(Gold, ALostFightPaysNothing) {
  RunState run = RunState::start(3);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 0;
  run.end_combat();
  EXPECT_EQ(run.gold, 0);
}

// ------------------------------------------------------------- shop stock

// ------------------------------------------------- colorless slots (§4.3)

namespace {

bool in_pool(const std::vector<CardId>& pool, CardId id) {
  for (CardId candidate : pool) {
    if (candidate == id) return true;
  }
  return false;
}

}  // namespace

TEST(Shop, StocksAColorlessUncommonAndRareAfterTheClassCards) {
  RunState run = at_a_shop();

  ASSERT_EQ(run.shop_cards.size(), static_cast<size_t>(kShopCardSlots + 2));
  const ShopItem& uncommon = run.shop_cards[kShopCardSlots];
  const ShopItem& rare = run.shop_cards[kShopCardSlots + 1];
  EXPECT_EQ(uncommon.rarity, CardRarity::Uncommon);
  EXPECT_EQ(rare.rarity, CardRarity::Rare);
  EXPECT_TRUE(in_pool(COLORLESS_UNCOMMON_POOL, uncommon.card.card_id))
      << card_name(uncommon.card.card_id);
  EXPECT_TRUE(in_pool(COLORLESS_RARE_POOL, rare.card.card_id))
      << card_name(rare.card.card_id);
}

// StS discounts one of the FIRST FIVE card slots, so a colorless card is never
// the half-price one.
TEST(Shop, ColorlessSlotsAreNeverOnSale) {
  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = at_a_shop(s);
    for (size_t i = kShopCardSlots; i < run.shop_cards.size(); ++i) {
      EXPECT_FALSE(run.shop_cards[i].on_sale) << "seed " << s;
    }
  }
}

// 75 and 150 from the rarity table, times the 1.2 colorless markup, times the
// 0.9-1.1 jitter.
TEST(Shop, ColorlessPricesCarryTheMarkup) {
  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = at_a_shop(s);
    const int uncommon = run.shop_cards[kShopCardSlots].price;
    const int rare = run.shop_cards[kShopCardSlots + 1].price;
    EXPECT_GE(uncommon, 81) << "seed " << s;
    EXPECT_LE(uncommon, 99) << "seed " << s;
    EXPECT_GE(rare, 162) << "seed " << s;
    EXPECT_LE(rare, 198) << "seed " << s;
  }
}

// The whole reason the colorless block was built: these are now buyable.
TEST(Shop, BuyingAColorlessCardAddsItToTheDeck) {
  RunState run = at_a_shop();
  const size_t before = run.master_deck.size();
  const CardId id = run.shop_cards[kShopCardSlots].card.card_id;
  const int price = run.shop_cards[kShopCardSlots].price;
  const int gold = run.gold;

  run.buy_card(kShopCardSlots);

  EXPECT_EQ(run.gold, gold - price);
  ASSERT_EQ(run.master_deck.size(), before + 1);
  EXPECT_EQ(run.master_deck.back().card_id, id);
  EXPECT_TRUE(run.shop_cards[kShopCardSlots].sold);
  EXPECT_NE(run.master_deck.back().uid, kCombatScopedCardUid)
      << "a bought card gets a run identity";
}

TEST(Shop, StocksFiveClassCardsAndTwoColorless) {
  RunState run = at_a_shop();
  EXPECT_EQ(run.shop_cards.size(), static_cast<size_t>(kShopCardSlots + 2));
}

// Two attacks, two skills, one power — the CLASS slots are typed. The two
// colorless slots that follow are typed by their pools instead, so they are
// deliberately outside this count.
TEST(Shop, SlotTypesAreTwoAttacksTwoSkillsOnePower) {
  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = at_a_shop(s);
    int attacks = 0, skills = 0, powers = 0;
    for (int i = 0; i < kShopCardSlots; ++i) {
      const ShopItem& item = run.shop_cards[i];
      switch (CARD_DATABASE.at(item.card.card_id).type) {
        case CardType::Attack: ++attacks; break;
        case CardType::Skill: ++skills; break;
        case CardType::Power: ++powers; break;
        default: break;
      }
    }
    EXPECT_EQ(attacks, 2) << "seed " << s;
    EXPECT_EQ(skills, 2) << "seed " << s;
    EXPECT_EQ(powers, 1) << "seed " << s;
  }
}

TEST(Shop, NeverStocksTheSameCardTwice) {
  for (uint64_t s = 0; s < 60; ++s) {
    RunState run = at_a_shop(s);
    std::set<CardId> ids;
    for (const ShopItem& item : run.shop_cards) ids.insert(item.card.card_id);
    EXPECT_EQ(ids.size(), run.shop_cards.size()) << "seed " << s;
  }
}

// The power slot promotes a COMMON roll to UNCOMMON, so a shop never sells a
// common power.
TEST(Shop, NeverSellsACommonPower) {
  std::set<CardId> commons;
  for (CardId id : IRONCLAD_COMMON_POOL) commons.insert(id);

  for (uint64_t s = 0; s < 100; ++s) {
    RunState run = at_a_shop(s);
    for (const ShopItem& item : run.shop_cards) {
      if (CARD_DATABASE.at(item.card.card_id).type != CardType::Power) continue;
      EXPECT_EQ(commons.count(item.card.card_id), 0u)
          << "seed " << s << " sold a common power";
    }
  }
}

TEST(Shop, StockComesFromTheIroncladPools) {
  std::set<CardId> pool;
  for (CardId id : IRONCLAD_COMMON_POOL) pool.insert(id);
  for (CardId id : IRONCLAD_UNCOMMON_POOL) pool.insert(id);
  for (CardId id : IRONCLAD_RARE_POOL) pool.insert(id);

  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = at_a_shop(s);
    // The CLASS slots only: the last two come from the colorless pools, which
    // StocksAColorlessUncommonAndRareAfterTheClassCards checks instead.
    for (int i = 0; i < kShopCardSlots; ++i) {
      EXPECT_EQ(pool.count(run.shop_cards[i].card.card_id), 1u)
          << "seed " << s;
    }
  }
}

// ------------------------------------------------------------------ prices

TEST(Shop, PricesArePositive) {
  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = at_a_shop(s);
    for (const ShopItem& item : run.shop_cards) EXPECT_GT(item.price, 0);
  }
}

// Exactly one of the five slots is half price. Asserted on the sale tag rather
// than on the price: a halved RARE still costs more than a full-price common,
// so price alone cannot identify the discounted card — which is also why the
// tag has to be observable state.
TEST(Shop, ExactlyOneCardIsOnSale) {
  for (uint64_t s = 0; s < 60; ++s) {
    RunState run = at_a_shop(s);
    int on_sale = 0;
    for (const ShopItem& item : run.shop_cards) {
      if (item.on_sale) ++on_sale;
    }
    EXPECT_EQ(on_sale, 1) << "seed " << s;
  }
}

// And the discount is real: the sale card falls below its own rarity's
// undiscounted floor, while every other card sits inside its band. This needs
// the rarity — which is why ShopItem records it.
TEST(Shop, TheSaleCardIsGenuinelyHalfPrice) {
  for (uint64_t s = 0; s < 60; ++s) {
    RunState run = at_a_shop(s);
    // The CLASS slots only. A colorless card carries the 1.2 markup, so it
    // sits ABOVE this band by design — ColorlessPricesCarryTheMarkup checks
    // that one — and it can never be the sale card anyway.
    for (int i = 0; i < kShopCardSlots; ++i) {
      const ShopItem& item = run.shop_cards[i];
      const int base = kCardRarityPrices[static_cast<int>(item.rarity)];
      const int floor_price = static_cast<int>(static_cast<float>(base) * 0.9f);
      const int ceil_price = static_cast<int>(static_cast<float>(base) * 1.1f);
      if (item.on_sale) {
        EXPECT_LE(item.price, ceil_price / 2 + 1) << "seed " << s;
      } else {
        EXPECT_GE(item.price, floor_price) << "seed " << s;
        EXPECT_LE(item.price, ceil_price) << "seed " << s;
      }
    }
  }
}

// ------------------------------------------------------------------ buying

TEST(Shop, BuyingSpendsGoldAndAddsTheCard) {
  RunState run = at_a_shop();
  const int price = run.shop_cards[0].price;
  const CardId id = run.shop_cards[0].card.card_id;
  const size_t deck = run.master_deck.size();

  run.buy_card(0);

  EXPECT_EQ(run.gold, 500 - price);
  ASSERT_EQ(run.master_deck.size(), deck + 1);
  EXPECT_EQ(run.master_deck.back().card_id, id);
  EXPECT_TRUE(run.shop_cards[0].sold);
}

// A bought card is a possession and gets an identity; on the shelf it had none.
TEST(Shop, ABoughtCardGetsAnIdentity) {
  RunState run = at_a_shop();
  EXPECT_EQ(run.shop_cards[0].card.uid, kCombatScopedCardUid);
  run.buy_card(0);
  EXPECT_NE(run.master_deck.back().uid, kCombatScopedCardUid);
}

TEST(Shop, CannotAffordMeansNoPurchase) {
  RunState run = at_a_shop(1, /*gold=*/0);
  const size_t deck = run.master_deck.size();
  run.buy_card(0);

  EXPECT_EQ(run.gold, 0);
  EXPECT_EQ(run.master_deck.size(), deck);
  EXPECT_FALSE(run.shop_cards[0].sold);
}

TEST(Shop, ACardCannotBeBoughtTwice) {
  RunState run = at_a_shop();
  run.buy_card(0);
  const int gold_after = run.gold;
  const size_t deck = run.master_deck.size();

  run.buy_card(0);
  EXPECT_EQ(run.gold, gold_after);
  EXPECT_EQ(run.master_deck.size(), deck);
}

// ---------------------------------------------------------------- removal

TEST(Shop, RemovalStartsAtSeventyFive) {
  RunState run = at_a_shop();
  EXPECT_EQ(run.shop_remove_price, kBaseRemovePrice);
}

TEST(Shop, RemovalTakesTheCardOutOfTheDeck) {
  RunState run = at_a_shop();
  const size_t deck = run.master_deck.size();
  const int uid = run.master_deck[0].uid;

  run.buy_card_removal(0);

  ASSERT_EQ(run.master_deck.size(), deck - 1);
  for (const Card& c : run.master_deck) EXPECT_NE(c.uid, uid);
  EXPECT_EQ(run.gold, 500 - kBaseRemovePrice);
}

TEST(Shop, OnlyOneRemovalPerShop) {
  RunState run = at_a_shop();
  run.buy_card_removal(0);
  const size_t deck = run.master_deck.size();

  run.buy_card_removal(0);
  EXPECT_EQ(run.master_deck.size(), deck) << "removed twice in one shop";
}

// Removal gets permanently dearer across the RUN, not per shop — which makes
// deck thinning something the whole run competes for.
TEST(Shop, RemovalGetsDearerForTheRestOfTheRun) {
  RunState run = at_a_shop();
  run.buy_card_removal(0);
  ASSERT_EQ(run.shop_remove_count, 1);

  run.leave_shop();
  run.floor = 9;
  run.phase = Phase::Shop;
  run.generate_shop();

  EXPECT_EQ(run.shop_remove_price, kBaseRemovePrice + kRemovePriceIncrease);
}

// -------------------------------------------------------------- determinism

TEST(Shop, StockIsDeterministicPerFloor) {
  auto stock = [](uint64_t seed, int floor) {
    RunState run = RunState::start(seed);
    run.floor = floor;
    run.phase = Phase::Shop;
    run.generate_shop();
    std::vector<int> out;
    for (const ShopItem& item : run.shop_cards) {
      out.push_back(static_cast<int>(item.card.card_id));
      out.push_back(item.price);
    }
    return out;
  };

  EXPECT_EQ(stock(11, 5), stock(11, 5));
  EXPECT_NE(stock(11, 5), stock(11, 6));
  EXPECT_NE(stock(11, 5), stock(12, 5));
}

TEST(Shop, LeavingClearsTheStock) {
  RunState run = at_a_shop();
  run.leave_shop();
  EXPECT_TRUE(run.shop_cards.empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

// ------------------------------------------------- the relic and potion shelves

TEST(Shop, StocksThreeRelicsAndThreePotions) {
  RunState run = at_a_shop();
  EXPECT_EQ(run.shop_relics.size(), 3u);
  EXPECT_EQ(run.shop_potions.size(), 3u);
}

// The third slot is always Shop tier, which is the only way shop-exclusive
// relics enter a run at all.
TEST(Shop, TheThirdRelicSlotIsAlwaysShopTier) {
  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = at_a_shop(s);
    ASSERT_EQ(run.shop_relics.size(), 3u) << "seed " << s;
    EXPECT_EQ(relic_tier(run.shop_relics[2].id), RelicTier::Shop) << "seed " << s;
  }
}

// A shelf must never offer the same relic twice. Two slots can roll the same
// tier, and a draw that excluded only HELD relics would let both land on the
// same id — the player then pays full price for the second and receives
// nothing, because obtain_relic refuses a duplicate.
TEST(Shop, NeverStocksTheSameRelicTwice) {
  for (uint64_t s = 0; s < 500; ++s) {
    RunState run = at_a_shop(s);
    std::set<RelicId> seen;
    for (const ShopRelic& offer : run.shop_relics) {
      EXPECT_TRUE(seen.insert(offer.id).second)
          << "seed " << s << ": the same relic was stocked in two slots";
    }
  }
}

TEST(Shop, AShelfNeverOffersARelicAlreadyHeld) {
  for (uint64_t s = 0; s < 200; ++s) {
    RunState run = RunState::start(s);
    run.floor = 5;
    run.gold = 500;
    run.phase = Phase::Shop;
    // Hold every common, so a common slot must draw from elsewhere.
    for (RelicId id : relic_pool(RelicTier::Common)) run.obtain_relic(id);
    run.generate_shop();
    for (const ShopRelic& offer : run.shop_relics) {
      EXPECT_FALSE(run.has_relic(offer.id))
          << "seed " << s << ": stocked a relic the run already holds";
    }
  }
}

// ------------------------------------------------------------- buying them

TEST(Shop, BuyingARelicTakesTheGoldAndGivesTheRelic) {
  RunState run = at_a_shop();
  const RelicId id = run.shop_relics[0].id;
  const int price = run.shop_relics[0].price;
  const int gold_before = run.gold;

  run.buy_relic(0);

  EXPECT_TRUE(run.has_relic(id));
  EXPECT_EQ(run.gold, gold_before - price);
  EXPECT_TRUE(run.shop_relics[0].sold);
}

TEST(Shop, BuyingARelicYouAlreadyHoldCostsNothing) {
  RunState run = at_a_shop();
  const RelicId id = run.shop_relics[0].id;
  run.obtain_relic(id);
  const int gold_before = run.gold;

  run.buy_relic(0);

  EXPECT_EQ(run.gold, gold_before) << "gold was taken for a relic already held";
  EXPECT_FALSE(run.shop_relics[0].sold);
}

TEST(Shop, CannotBuyARelicYouCannotAfford) {
  RunState run = at_a_shop(1, /*gold=*/0);
  const RelicId id = run.shop_relics[0].id;
  run.buy_relic(0);
  EXPECT_FALSE(run.has_relic(id));
  EXPECT_EQ(run.gold, 0);
}

TEST(Shop, BuyingAPotionTakesTheGoldAndGivesThePotion) {
  RunState run = at_a_shop();
  const PotionId id = run.shop_potions[0].id;
  const int price = run.shop_potions[0].price;
  const int gold_before = run.gold;

  run.buy_potion(0);

  ASSERT_EQ(run.potions.size(), 1u);
  EXPECT_EQ(run.potions[0], id);
  EXPECT_EQ(run.gold, gold_before - price);
  EXPECT_TRUE(run.shop_potions[0].sold);
}

// Sozu is the case an inline full-belt check misses: the belt has room, but the
// relic refuses potions outright. Paying before asking would take the gold and
// the slot in exchange for nothing.
TEST(Shop, SozuMeansAPotionPurchaseIsRefusedRatherThanWasted) {
  RunState run = at_a_shop();
  run.obtain_relic(RelicId::Sozu);
  const int gold_before = run.gold;

  run.buy_potion(0);

  EXPECT_EQ(run.gold, gold_before) << "gold was taken for a potion Sozu refuses";
  EXPECT_TRUE(run.potions.empty());
  EXPECT_FALSE(run.shop_potions[0].sold);
}

TEST(Shop, AFullBeltRefusesThePurchaseRatherThanWastingTheGold) {
  RunState run = at_a_shop();
  while (static_cast<int>(run.potions.size()) < run.potion_slots) {
    run.obtain_potion(PotionId::FirePotion);
  }
  const int gold_before = run.gold;
  const size_t held = run.potions.size();

  run.buy_potion(0);

  EXPECT_EQ(run.gold, gold_before);
  EXPECT_EQ(run.potions.size(), held);
  EXPECT_FALSE(run.shop_potions[0].sold);
}

// The header documents the whole shop block as empty outside Phase::Shop.
// Clearing only the cards left stale relic and potion offers visible to anything
// reading the block later — the v2 observation's shop slots, a serializer, the
// renderer.
TEST(Shop, LeavingClearsEveryShelfNotJustTheCards) {
  RunState run = at_a_shop();
  ASSERT_FALSE(run.shop_relics.empty());
  ASSERT_FALSE(run.shop_potions.empty());

  run.leave_shop();

  EXPECT_TRUE(run.shop_cards.empty());
  EXPECT_TRUE(run.shop_relics.empty()) << "stale relic offers survived the shop";
  EXPECT_TRUE(run.shop_potions.empty()) << "stale potion offers survived the shop";
}

// ============================================= shop discount relics (batch 2c)

namespace {

RunState at_a_shop_with(std::vector<RelicId> ids, uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  run.floor = 5;
  run.gold = 500;
  for (RelicId id : ids) run.obtain_relic(id);
  run.phase = Phase::Shop;
  run.generate_shop();
  return run;
}

}  // namespace

TEST(ShopDiscounts, NoRelicsMeansNoDiscount) {
  EXPECT_FLOAT_EQ(at_a_shop_with({}).shop_price_multiplier(), 1.0f);
}

TEST(ShopDiscounts, MembershipCardHalvesPrices) {
  EXPECT_FLOAT_EQ(
      at_a_shop_with({RelicId::MembershipCard}).shop_price_multiplier(), 0.5f);
}

TEST(ShopDiscounts, TheCourierTakesTwentyPercent) {
  EXPECT_FLOAT_EQ(at_a_shop_with({RelicId::TheCourier}).shop_price_multiplier(),
                  0.8f);
}

// The two MULTIPLY. Holding both is x0.40 — a 60% reduction, which is what the
// wiki states. Adding the discounts would give 70% off, a different and wrong
// number that a careless reading produces.
TEST(ShopDiscounts, MembershipCardAndCourierMultiplyToSixtyPercentOff) {
  const RunState run =
      at_a_shop_with({RelicId::MembershipCard, RelicId::TheCourier});
  EXPECT_FLOAT_EQ(run.shop_price_multiplier(), 0.4f);
  EXPECT_EQ(run.discounted_price(100), 40) << "additive stacking would give 30";
}

// Rounding is to nearest, halves UP.
TEST(ShopDiscounts, PricesRoundToNearestWithHalvesUp) {
  const RunState run = at_a_shop_with({RelicId::MembershipCard});
  EXPECT_EQ(run.discounted_price(75), 38);  // 37.5 -> 38
  EXPECT_EQ(run.discounted_price(51), 26);  // 25.5 -> 26
  EXPECT_EQ(run.discounted_price(50), 25);
}

// Every shelf is discounted, not just the cards.
TEST(ShopDiscounts, EveryShelfIsCheaperWithMembershipCard) {
  const RunState full = at_a_shop_with({});
  const RunState cheap = at_a_shop_with({RelicId::MembershipCard});

  ASSERT_EQ(full.shop_cards.size(), cheap.shop_cards.size());
  for (size_t i = 0; i < full.shop_cards.size(); ++i) {
    EXPECT_LT(cheap.shop_cards[i].price, full.shop_cards[i].price) << "card " << i;
  }
  ASSERT_EQ(full.shop_relics.size(), cheap.shop_relics.size());
  for (size_t i = 0; i < full.shop_relics.size(); ++i) {
    EXPECT_LT(cheap.shop_relics[i].price, full.shop_relics[i].price)
        << "relic " << i;
  }
  ASSERT_EQ(full.shop_potions.size(), cheap.shop_potions.size());
  for (size_t i = 0; i < full.shop_potions.size(); ++i) {
    EXPECT_LT(cheap.shop_potions[i].price, full.shop_potions[i].price)
        << "potion " << i;
  }
}

TEST(ShopDiscounts, CardRemovalIsDiscountedToo) {
  const RunState full = at_a_shop_with({});
  const RunState cheap = at_a_shop_with({RelicId::MembershipCard});
  EXPECT_LT(cheap.shop_remove_price, full.shop_remove_price);
}

// ------------------------------------------------------------ Smiling Mask

TEST(ShopDiscounts, SmilingMaskPinsRemovalAtFifty) {
  RunState run = at_a_shop_with({RelicId::SmilingMask});
  EXPECT_EQ(run.shop_remove_price, kSmilingMaskRemovalPrice);
}

// Smiling Mask holds at 50 even once the base price has climbed past it.
TEST(ShopDiscounts, SmilingMaskHoldsAsTheBasePriceClimbs) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::SmilingMask);
  run.shop_remove_count = 5;  // base would be 75 + 125 = 200
  EXPECT_EQ(run.removal_price(), kSmilingMaskRemovalPrice);
}

// The disagreement, pinned by a test so the decision cannot drift silently.
//
// sts_lightspeed sets the cost to 50 and THEN applies the discount factors,
// which gives 25 with Membership Card. The wiki says price-reduction effects do
// not affect removal at all when Smiling Mask is held — "always 50 Gold even if
// its price would otherwise be lower". CLAUDE.md's rule 2 corollary makes the
// wiki the check, so 50 is what ships.
TEST(ShopDiscounts, SmilingMaskOverridesTheDiscountRelics) {
  RunState run = at_a_shop_with(
      {RelicId::SmilingMask, RelicId::MembershipCard, RelicId::TheCourier});
  EXPECT_EQ(run.shop_remove_price, kSmilingMaskRemovalPrice)
      << "the discount relics reached a Smiling Mask removal price "
         "(sts_lightspeed's reading, which the wiki contradicts)";
}

// The discounts still apply to everything else while Smiling Mask is held —
// the override is scoped to removal alone.
TEST(ShopDiscounts, SmilingMaskDoesNotProtectTheOtherShelves) {
  const RunState full = at_a_shop_with({RelicId::SmilingMask});
  const RunState cheap =
      at_a_shop_with({RelicId::SmilingMask, RelicId::MembershipCard});
  ASSERT_FALSE(full.shop_cards.empty());
  EXPECT_LT(cheap.shop_cards[0].price, full.shop_cards[0].price);
}

// Buying still works against the discounted price, not the undiscounted one.
TEST(ShopDiscounts, APurchaseChargesTheDiscountedPrice) {
  RunState run = at_a_shop_with({RelicId::MembershipCard});
  const int price = run.shop_cards[0].price;
  const int gold_before = run.gold;
  run.buy_card(0);
  EXPECT_EQ(run.gold, gold_before - price);
}

}  // namespace
}  // namespace minispire
