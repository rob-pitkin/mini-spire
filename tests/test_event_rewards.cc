// Events, step 3 (docs/design/events.md §8): the events that open a reward
// screen or take a gift. Lab, The Woman in Blue and We Meet Again, with We Meet
// Again's potion lock (Rob B1), the reward screen an event's potions wait on
// (Rob B2), and gold that stops at 0 (Rob B3).
//
// No test depends on which potion, gold amount, card or relic a draw produced.
// A draw is checked against the same draw made on a copy of its stream, or by
// what holds for every draw. Where a relic's pickup could change what a test
// measures (Old Coin's gold), that relic is held first, so it cannot be drawn.

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "event.h"
#include "potion.h"
#include "relic.h"
#include "run_state.h"

using namespace minispire;

namespace {

RunState event_run(std::vector<CardId> deck, uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  run.floor = 7;
  run.gold = 100;
  run.phase = Phase::Map;
  run.potions.clear();
  run.master_deck.clear();
  for (CardId id : deck) run.add_card(Card{id});
  return run;
}

// The potions getRandomPotion would draw next on the floor's potion stream.
std::vector<PotionId> next_potions(RunState& run, int count) {
  std::mt19937 rng = run.potion_rng_for_floor();
  std::vector<PotionId> out;
  for (int i = 0; i < count; ++i) out.push_back(any_potion(rng));
  return out;
}

void fill_belt(RunState& run) {
  run.potions.assign(static_cast<size_t>(run.potion_slots),
                     PotionId::FirePotion);
}

bool offered(const RunState& run, EventOption option) {
  const std::vector<EventOption> options = run.event_options();
  return std::find(options.begin(), options.end(), option) != options.end();
}

bool deck_has_uid(const RunState& run, int uid) {
  return std::any_of(run.master_deck.begin(), run.master_deck.end(),
                     [uid](const Card& c) { return c.uid == uid; });
}

}  // namespace

// ============================================================================
// Gold stops at 0 (Rob B3)
// ============================================================================

TEST(LoseGold, StopsAtZero) {
  RunState run = event_run({CardId::Strike});
  run.gold = 10;
  run.lose_gold(20);
  EXPECT_EQ(run.gold, 0);
}

TEST(SpendGold, StopsAtZero) {
  RunState run = event_run({CardId::Strike});
  run.gold = 10;
  run.spend_gold(20);
  EXPECT_EQ(run.gold, 0);
}

// ============================================================================
// Lab
// ============================================================================

TEST(Lab, SearchTakesThreeUniformPotionsWhenTheyFit) {
  RunState run = event_run({CardId::Strike});
  const std::vector<PotionId> expected = next_potions(run, kLabPotions);
  run.begin_event(EventId::Lab);
  EXPECT_EQ(run.potions, expected);
  EXPECT_TRUE(run.pending_potion_rewards.empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

TEST(Lab, AFullBeltLeavesThemOnARewardScreenWithNoCard) {
  RunState run = event_run({CardId::Strike});
  fill_belt(run);
  const std::vector<PotionId> held = run.potions;
  const std::vector<PotionId> expected = next_potions(run, kLabPotions);
  run.begin_event(EventId::Lab);
  EXPECT_EQ(run.phase, Phase::Reward);
  EXPECT_FALSE(run.event.active);
  EXPECT_TRUE(run.card_reward.empty());
  EXPECT_EQ(run.pending_potion_rewards, expected);
  EXPECT_EQ(run.potions, held);
}

TEST(Lab, ADiscardMakesRoomOnTheScreen) {
  RunState run = event_run({CardId::Strike});
  fill_belt(run);
  run.begin_event(EventId::Lab);
  const PotionId first = run.pending_potion_rewards[0];
  run.discard_potion(0);
  run.take_potion_reward(0);
  EXPECT_EQ(run.potions.back(), first);
  EXPECT_EQ(static_cast<int>(run.potions.size()), run.potion_slots);
  EXPECT_EQ(run.pending_potion_rewards.size(), 2u);
}

TEST(Lab, ProceedLeavesTheRestBehind) {
  RunState run = event_run({CardId::Strike});
  fill_belt(run);
  run.begin_event(EventId::Lab);
  run.skip_card_reward();
  EXPECT_EQ(run.phase, Phase::Map);
  EXPECT_TRUE(run.pending_potion_rewards.empty());
}

// The potions are still made, and the stream moves, but claiming one gets
// nothing: no screen waits.
TEST(Lab, WithSozuNothingIsTakenAndTheRoomExits) {
  RunState run = event_run({CardId::Strike});
  run.obtain_relic(RelicId::Sozu);
  std::mt19937 after = run.potion_rng_for_floor();
  for (int i = 0; i < kLabPotions; ++i) any_potion(after);
  run.begin_event(EventId::Lab);
  EXPECT_TRUE(run.potions.empty());
  EXPECT_EQ(run.phase, Phase::Map);
  EXPECT_TRUE(run.potion_rng_for_floor() == after);
}

// ============================================================================
// The Woman in Blue
// ============================================================================

TEST(TheWomanInBlue, EachPurchaseCostsItsPriceForThatManyPotions) {
  struct Case {
    EventOption option;
    int cost;
    int count;
  };
  for (const Case& c : {Case{EventOption::WomanInBlueBuyOne, 20, 1},
                        Case{EventOption::WomanInBlueBuyTwo, 30, 2},
                        Case{EventOption::WomanInBlueBuyThree, 40, 3}}) {
    RunState run = event_run({CardId::Strike});
    run.begin_event(EventId::WomanInBlue);
    const std::vector<PotionId> expected = next_potions(run, c.count);
    ASSERT_TRUE(run.choose_event_option(c.option));
    EXPECT_EQ(run.gold, 100 - c.cost) << c.count;
    EXPECT_EQ(run.potions, expected) << c.count;
    EXPECT_EQ(run.phase, Phase::Map) << c.count;
  }
}

TEST(TheWomanInBlue, LeaveCostsNothing) {
  RunState run = event_run({CardId::Strike});
  run.begin_event(EventId::WomanInBlue);
  ASSERT_TRUE(run.choose_event_option(EventOption::WomanInBlueLeave));
  EXPECT_EQ(run.gold, 100);
  EXPECT_TRUE(run.potions.empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

// The gate is 50 gold and the dearest purchase is 40, so StS disables none.
TEST(TheWomanInBlue, NoOptionIsMaskedAtTheGate) {
  RunState run = event_run({CardId::Strike});
  run.gold = kWomanInBlueMinGold;
  run.begin_event(EventId::WomanInBlue);
  EXPECT_EQ(run.event_options(),
            (std::vector<EventOption>{
                EventOption::WomanInBlueBuyOne, EventOption::WomanInBlueBuyTwo,
                EventOption::WomanInBlueBuyThree,
                EventOption::WomanInBlueLeave}));
}

TEST(TheWomanInBlue, AFullBeltWaitsOnTheRewardScreen) {
  RunState run = event_run({CardId::Strike});
  fill_belt(run);
  run.begin_event(EventId::WomanInBlue);
  ASSERT_TRUE(run.choose_event_option(EventOption::WomanInBlueBuyTwo));
  EXPECT_EQ(run.gold, 70);
  EXPECT_EQ(run.phase, Phase::Reward);
  EXPECT_EQ(run.pending_potion_rewards.size(), 2u);
}

TEST(TheWomanInBlue, ItsCostsDoNotBreakMawBank) {
  RunState run = event_run({CardId::Strike});
  run.obtain_relic(RelicId::MawBank);
  run.begin_event(EventId::WomanInBlue);
  run.choose_event_option(EventOption::WomanInBlueBuyOne);
  EXPECT_NE(run.held_relic(RelicId::MawBank)->counter, kMawBankUsedUp);
}

// ============================================================================
// We Meet Again: the offers
// ============================================================================

// Potion, then gold, then card, each on the floor's event stream. Each seed's
// offers are checked against the same draws on a copy of that seed's stream,
// which holds for every seed; several seeds make a wrong order unlikely to
// match by chance.
TEST(WeMeetAgain, EntryRollsThePotionThenTheGoldThenTheCard) {
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    RunState run =
        event_run({CardId::Strike, CardId::Anger, CardId::Inflame}, seed);
    run.gold = 120;
    run.potions = {PotionId::FirePotion, PotionId::BlockPotion};
    const std::vector<int> eligible = {run.master_deck[1].uid,
                                       run.master_deck[2].uid};
    std::mt19937 rng = run.event_rng_for_floor();
    const int potion = std::uniform_int_distribution<int>(0, 1)(rng);
    const int gold = std::uniform_int_distribution<int>(50, 120)(rng);
    const int card =
        eligible[std::uniform_int_distribution<size_t>(0, 1)(rng)];

    run.begin_event(EventId::WeMeetAgain);
    EXPECT_EQ(run.event.potion_index, potion) << seed;
    EXPECT_EQ(run.event.gold_amount, gold) << seed;
    EXPECT_EQ(run.event.card_uid, card) << seed;
  }
}

// getRandomPotion returns before drawing when no potion is held, so the gold
// takes the stream's first number.
TEST(WeMeetAgain, ARollWithNothingToOfferIsNotTaken) {
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    RunState run =
        event_run({CardId::Strike, CardId::Anger, CardId::Inflame}, seed);
    run.gold = 120;
    const std::vector<int> eligible = {run.master_deck[1].uid,
                                       run.master_deck[2].uid};
    std::mt19937 rng = run.event_rng_for_floor();
    const int gold = std::uniform_int_distribution<int>(50, 120)(rng);
    const int card =
        eligible[std::uniform_int_distribution<size_t>(0, 1)(rng)];

    run.begin_event(EventId::WeMeetAgain);
    EXPECT_EQ(run.event.potion_index, -1) << seed;
    EXPECT_EQ(run.event.gold_amount, gold) << seed;
    EXPECT_EQ(run.event.card_uid, card) << seed;
  }
}

TEST(WeMeetAgain, EachGiftIsMaskedWithoutItsOffer) {
  RunState run = event_run({CardId::Strike, CardId::Defend, CardId::Bash,
                            CardId::Regret});
  run.gold = kWeMeetAgainMinGold - 1;
  run.begin_event(EventId::WeMeetAgain);
  EXPECT_EQ(run.event.gold_amount, 0);
  EXPECT_EQ(run.event.card_uid, -1);
  EXPECT_EQ(run.event_options(),
            std::vector<EventOption>{EventOption::WeMeetAgainAttack});
}

TEST(WeMeetAgain, TheGoldOfferRunsFromFiftyToTheGoldHeldUpTo150) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    for (int held : {50, 80, 150, 500}) {
      RunState run = event_run({CardId::Strike}, seed);
      run.gold = held;
      run.begin_event(EventId::WeMeetAgain);
      EXPECT_GE(run.event.gold_amount, kWeMeetAgainMinGold) << held;
      EXPECT_LE(run.event.gold_amount, std::min(held, kWeMeetAgainMaxGold))
          << held;
      EXPECT_TRUE(offered(run, EventOption::WeMeetAgainGiveGold)) << held;
    }
  }
}

// rarity == BASIC || type == CURSE is skipped, upgraded basics included. Anger
// is the one card left, whatever the seed.
TEST(WeMeetAgain, TheCardOfferIsNeitherBasicNorACurse) {
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    RunState run =
        event_run({CardId::Strike, CardId::StrikePlus, CardId::Defend,
                   CardId::BashPlus, CardId::Regret, CardId::Anger},
                  seed);
    run.begin_event(EventId::WeMeetAgain);
    EXPECT_EQ(run.event.card_uid, run.master_deck[5].uid) << seed;
  }
}

// ============================================================================
// We Meet Again: the gifts
// ============================================================================

// Over seeds, so both slots are offered; the check holds for either.
TEST(WeMeetAgain, GivePotionGivesUpTheOfferedOneForARelic) {
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    RunState run = event_run({CardId::Strike}, seed);
    run.potions = {PotionId::FruitJuice, PotionId::BloodPotion};
    run.begin_event(EventId::WeMeetAgain);
    const std::vector<PotionId> before = run.potions;
    const size_t relics = run.relics.size();
    ASSERT_TRUE(run.choose_event_option(EventOption::WeMeetAgainGivePotion));
    ASSERT_EQ(run.potions.size(), 1u);
    EXPECT_EQ(run.potions[0], before[1 - run.event.potion_index]) << seed;
    EXPECT_EQ(run.relics.size(), relics + 1) << seed;
    EXPECT_EQ(run.phase, Phase::Map) << seed;
  }
}

// Old Coin is held so that the relic drawn cannot pay gold of its own.
TEST(WeMeetAgain, GiveGoldPaysTheOfferForARelicAndKeepsMawBank) {
  RunState run = event_run({CardId::Strike});
  run.obtain_relic(RelicId::OldCoin);
  run.obtain_relic(RelicId::MawBank);
  run.gold = 120;
  run.begin_event(EventId::WeMeetAgain);
  const int offer = run.event.gold_amount;
  const size_t relics = run.relics.size();
  ASSERT_TRUE(run.choose_event_option(EventOption::WeMeetAgainGiveGold));
  EXPECT_EQ(run.gold, 120 - offer);
  EXPECT_EQ(run.relics.size(), relics + 1);
  EXPECT_NE(run.held_relic(RelicId::MawBank)->counter, kMawBankUsedUp);
}

// Over seeds, so both copies are offered; the check holds for either.
TEST(WeMeetAgain, GiveCardRemovesThatCopyForARelic) {
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    RunState run =
        event_run({CardId::Anger, CardId::Anger, CardId::Strike}, seed);
    run.begin_event(EventId::WeMeetAgain);
    const int given = run.event.card_uid;
    const size_t relics = run.relics.size();
    ASSERT_TRUE(run.choose_event_option(EventOption::WeMeetAgainGiveCard));
    EXPECT_FALSE(deck_has_uid(run, given)) << seed;
    EXPECT_EQ(run.master_deck.size(), 2u) << seed;
    EXPECT_EQ(run.relics.size(), relics + 1) << seed;
  }
}

// getRandomNonBasicCard walks the whole deck, and removeCard does not check
// bottles.
TEST(WeMeetAgain, ABottledCardCanBeOfferedAndGiven) {
  RunState run = event_run({CardId::Strike, CardId::Anger});
  run.master_deck[1].bottle = Bottle::Flame;
  const int anger = run.master_deck[1].uid;
  run.begin_event(EventId::WeMeetAgain);
  ASSERT_EQ(run.event.card_uid, anger);
  ASSERT_TRUE(run.choose_event_option(EventOption::WeMeetAgainGiveCard));
  EXPECT_FALSE(deck_has_uid(run, anger));
}

TEST(WeMeetAgain, AttackGivesAndTakesNothing) {
  RunState run = event_run({CardId::Strike, CardId::Anger});
  run.potions = {PotionId::FirePotion};
  run.begin_event(EventId::WeMeetAgain);
  const size_t relics = run.relics.size();
  ASSERT_TRUE(run.choose_event_option(EventOption::WeMeetAgainAttack));
  EXPECT_EQ(run.gold, 100);
  EXPECT_EQ(run.potions.size(), 1u);
  EXPECT_EQ(run.master_deck.size(), 2u);
  EXPECT_EQ(run.relics.size(), relics);
  EXPECT_EQ(run.phase, Phase::Map);
}

// ============================================================================
// We Meet Again: the potion lock (Rob B1)
// ============================================================================

TEST(WeMeetAgainLock, PotionsCannotBeDrunkOrDiscardedDuringIt) {
  RunState run = event_run({CardId::Strike});
  run.potions = {PotionId::FruitJuice};
  run.begin_event(EventId::WeMeetAgain);
  const int max_hp = run.max_hp;
  run.drink_potion(0);
  run.discard_potion(0);
  EXPECT_EQ(run.potions, std::vector<PotionId>{PotionId::FruitJuice});
  EXPECT_EQ(run.max_hp, max_hp);
}

// The room's event is still We Meet Again on the map after it.
TEST(WeMeetAgainLock, HoldsOnTheMapAfterTheEvent) {
  RunState run = event_run({CardId::Strike});
  run.potions = {PotionId::FruitJuice};
  run.begin_event(EventId::WeMeetAgain);
  ASSERT_TRUE(run.choose_event_option(EventOption::WeMeetAgainAttack));
  ASSERT_EQ(run.phase, Phase::Map);
  run.drink_potion(0);
  run.discard_potion(0);
  EXPECT_EQ(run.potions, std::vector<PotionId>{PotionId::FruitJuice});
}

TEST(WeMeetAgainLock, LiftsInTheNextRoom) {
  RunState run = event_run({CardId::Strike});
  run.potions = {PotionId::FruitJuice, PotionId::FirePotion};
  run.begin_event(EventId::WeMeetAgain);
  ASSERT_TRUE(run.choose_event_option(EventOption::WeMeetAgainAttack));
  run.enter_room(RoomType::Rest);
  const int max_hp = run.max_hp;
  run.drink_potion(0);
  EXPECT_GT(run.max_hp, max_hp);
  run.discard_potion(0);
  EXPECT_TRUE(run.potions.empty());
}

TEST(WeMeetAgainLock, NoOtherEventLocksPotions) {
  RunState run = event_run({CardId::Strike});
  run.potions = {PotionId::FirePotion};
  run.begin_event(EventId::GoldenShrine);
  run.discard_potion(0);
  EXPECT_TRUE(run.potions.empty());
}
