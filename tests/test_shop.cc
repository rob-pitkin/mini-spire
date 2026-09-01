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

TEST(Shop, StocksFiveClassCards) {
  RunState run = at_a_shop();
  EXPECT_EQ(run.shop_cards.size(), static_cast<size_t>(kShopCardSlots));
}

// Two attacks, two skills, one power — the slots are typed.
TEST(Shop, SlotTypesAreTwoAttacksTwoSkillsOnePower) {
  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = at_a_shop(s);
    int attacks = 0, skills = 0, powers = 0;
    for (const ShopItem& item : run.shop_cards) {
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
    for (const ShopItem& item : run.shop_cards) {
      EXPECT_EQ(pool.count(item.card.card_id), 1u) << "seed " << s;
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
    for (const ShopItem& item : run.shop_cards) {
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

}  // namespace
}  // namespace minispire
