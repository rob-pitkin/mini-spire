// Events, step 1 (docs/design/events.md §8): selection, the run-layer damage
// path, Ssserpent Head, and the events with no card choice, fight or reward
// screen.
//
// No test depends on which event, relic or roll a draw produced. Events are
// entered with begin_event; the selection tests assert what holds for every
// draw (the pick's list follows the shrine check), or empty the lists so only
// one outcome is possible.

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "event.h"
#include "relic.h"
#include "run_state.h"

using namespace minispire;

namespace {

// A run standing on floor 7 with gold to spare, so every Act 1 gate passes.
RunState late_run(uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  run.floor = 7;
  run.gold = 100;
  run.phase = Phase::Map;
  return run;
}

bool contains(const std::vector<EventId>& list, EventId id) {
  return std::find(list.begin(), list.end(), id) != list.end();
}

bool is_shrine_group(EventId id) {
  return static_cast<int>(id) >= static_cast<int>(EventId::MatchAndKeep);
}

int count_of(const RunState& run, CardId id) {
  return static_cast<int>(std::count_if(
      run.master_deck.begin(), run.master_deck.end(),
      [id](const Card& c) { return c.card_id == id; }));
}

int upgraded_count(const RunState& run) {
  return static_cast<int>(std::count_if(
      run.master_deck.begin(), run.master_deck.end(),
      [](const Card& c) { return is_upgraded(c.card_id); }));
}

// The float the next draw from `rng` gives, without moving it.
float peek_roll(const std::mt19937& rng) {
  std::mt19937 copy = rng;
  return std::uniform_real_distribution<float>(0.0f, 1.0f)(copy);
}

// Walks one `?` node: the first map row is all `?`, standing before the map.
void stand_before_a_question_row(RunState& run) {
  run.floor = 0;
  run.phase = Phase::Map;
  for (MapNode& node : run.map[0]) node.room = RoomType::Unknown;
}

int first_room_column(const RunState& run) { return run.available_paths()[0]; }

}  // namespace

// ============================================================================
// The lists and their gates (events.md §2.1, §2.2)
// ============================================================================

TEST(EventSelection, TheListsStartAsStSBuildsThem) {
  const RunState run = RunState::start(1);
  EXPECT_EQ(run.event_list.size(), 11u);
  EXPECT_EQ(run.shrine_list.size(), 6u);
  EXPECT_EQ(run.one_time_list.size(), 8u);
  // Each list holds its own group, and together they are all 25.
  for (EventId id : run.event_list) EXPECT_FALSE(is_shrine_group(id));
  for (EventId id : run.shrine_list) {
    EXPECT_GE(static_cast<int>(id), static_cast<int>(EventId::MatchAndKeep));
    EXPECT_LE(static_cast<int>(id), static_cast<int>(EventId::WheelOfChange));
  }
  for (EventId id : run.one_time_list) {
    EXPECT_GE(static_cast<int>(id), static_cast<int>(EventId::OminousForge));
  }
}

TEST(EventSelection, DeadAdventurerAndMushroomsWaitForFloorSeven) {
  RunState run = late_run();
  run.floor = 6;
  EXPECT_FALSE(contains(run.event_candidates(), EventId::DeadAdventurer));
  EXPECT_FALSE(contains(run.event_candidates(), EventId::Mushrooms));
  run.floor = 7;
  EXPECT_TRUE(contains(run.event_candidates(), EventId::DeadAdventurer));
  EXPECT_TRUE(contains(run.event_candidates(), EventId::Mushrooms));
}

TEST(EventSelection, TheClericNeedsThirtyFiveGold) {
  RunState run = late_run();
  run.gold = kClericMinGold - 1;
  EXPECT_FALSE(contains(run.event_candidates(), EventId::TheCleric));
  run.gold = kClericMinGold;
  EXPECT_TRUE(contains(run.event_candidates(), EventId::TheCleric));
}

TEST(EventSelection, TheWomanInBlueNeedsFiftyGold) {
  RunState run = late_run();
  run.gold = kWomanInBlueMinGold - 1;
  EXPECT_FALSE(contains(run.shrine_candidates(), EventId::WomanInBlue));
  run.gold = kWomanInBlueMinGold;
  EXPECT_TRUE(contains(run.shrine_candidates(), EventId::WomanInBlue));
}

// isCursed skips Curse of the Bell, which nothing can remove.
TEST(EventSelection, TheDivineFountainNeedsACurseItCanRemove) {
  RunState run = late_run();
  EXPECT_FALSE(contains(run.shrine_candidates(), EventId::DivineFountain));
  run.add_card(Card{CardId::CurseOfTheBell});
  EXPECT_FALSE(contains(run.shrine_candidates(), EventId::DivineFountain));
  run.add_card(Card{CardId::Regret});
  EXPECT_TRUE(contains(run.shrine_candidates(), EventId::DivineFountain));
}

// getShrine adds every shrine with no gate.
TEST(EventSelection, ShrinesHaveNoGate) {
  RunState run = late_run();
  run.floor = 1;
  run.gold = 0;
  const std::vector<EventId> candidates = run.shrine_candidates();
  for (EventId shrine : run.shrine_list) {
    EXPECT_TRUE(contains(candidates, shrine)) << event_name(shrine);
  }
}

// ============================================================================
// The pick and the stream (events.md §2.2, §2.3)
// ============================================================================

TEST(EventSelection, ThePickLeavesTheEventStreamWhereItWas) {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    RunState run = late_run(seed);
    const std::mt19937 before = run.event_rng;
    ASSERT_TRUE(run.generate_event().has_value());
    EXPECT_TRUE(run.event_rng == before) << "seed " << seed;
  }
}

// The shrine check decides the list: below 0.25 a shrine or one-time event,
// otherwise a regular one. True for every draw while both groups have
// candidates.
TEST(EventSelection, TheShrineCheckDecidesWhichListIsDrawn) {
  for (uint64_t seed = 0; seed < 40; ++seed) {
    RunState run = late_run(seed);
    const bool shrine = peek_roll(run.event_rng) < kShrineChance;
    const std::optional<EventId> picked = run.generate_event();
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(is_shrine_group(*picked), shrine)
        << "seed " << seed << " picked " << event_name(*picked);
  }
}

// The shrine re-entry bug's mechanism: the next `?` roll reads the number the
// pick's shrine check read. With the monster band at 25 and the others empty,
// that `?` is a fight exactly when the roll is under 25, which is the shrine
// check passing. Predicted from the same number, so it holds for every draw.
TEST(EventSelection, TheNextQuestionRollReadsTheShrineChecksNumber) {
  for (uint64_t seed = 0; seed < 40; ++seed) {
    RunState run = late_run(seed);
    const float shared = peek_roll(run.event_rng);
    ASSERT_TRUE(run.generate_event().has_value());

    run.monster_chance = 0.25f;
    run.shop_chance = 0.0f;
    run.treasure_chance = 0.0f;
    const RoomType expected = static_cast<int>(shared * 100.0f) < 25
                                  ? RoomType::Monster
                                  : RoomType::Unknown;
    EXPECT_EQ(run.resolve_unknown_room(), expected) << "seed " << seed;
  }
}

// One stream for the run: each `?` roll is the next number on it, whatever
// the floor, where a per-floor stream would restart.
TEST(EventSelection, EachQuestionRollTakesTheStreamsNextNumber) {
  RunState run = late_run();
  std::mt19937 expected = run.event_rng;
  for (int floor : {3, 3, 8}) {
    run.floor = floor;
    std::uniform_real_distribution<float>(0.0f, 1.0f)(expected);
    run.resolve_unknown_room();
    EXPECT_TRUE(run.event_rng == expected) << "floor " << floor;
  }
}

TEST(EventSelection, APickedEventLeavesItsList) {
  RunState run = late_run();
  const size_t before = run.event_list.size() + run.shrine_list.size() +
                        run.one_time_list.size();
  const std::optional<EventId> picked = run.generate_event();
  ASSERT_TRUE(picked.has_value());
  EXPECT_FALSE(contains(run.event_list, *picked));
  EXPECT_FALSE(contains(run.shrine_list, *picked));
  EXPECT_FALSE(contains(run.one_time_list, *picked));
  EXPECT_EQ(run.event_list.size() + run.shrine_list.size() +
                run.one_time_list.size(),
            before - 1);
}

// The shrine side: with the event list empty, every pick comes from the shrine
// or one-time list, and leaves it.
TEST(EventSelection, APickedShrineLeavesItsList) {
  RunState run = late_run();
  run.event_list.clear();
  const size_t before = run.shrine_list.size() + run.one_time_list.size();
  const std::optional<EventId> picked = run.generate_event();
  ASSERT_TRUE(picked.has_value());
  EXPECT_FALSE(contains(run.shrine_list, *picked));
  EXPECT_FALSE(contains(run.one_time_list, *picked));
  EXPECT_EQ(run.shrine_list.size() + run.one_time_list.size(), before - 1);
}

// getEvent falls back to getShrine when no event passes its gate; the shrine
// check is irrelevant then, since both branches end in a shrine.
TEST(EventSelection, NoEventPassingItsGateFallsBackToAShrine) {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    RunState run = late_run(seed);
    run.floor = 3;
    run.event_list = {EventId::DeadAdventurer, EventId::Mushrooms};
    const std::optional<EventId> picked = run.generate_event();
    ASSERT_TRUE(picked.has_value());
    EXPECT_TRUE(is_shrine_group(*picked)) << "seed " << seed;
  }
}

TEST(EventSelection, NoShrineLeftFallsBackToAnEvent) {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    RunState run = late_run(seed);
    run.shrine_list.clear();
    run.one_time_list.clear();
    const std::optional<EventId> picked = run.generate_event();
    ASSERT_TRUE(picked.has_value());
    EXPECT_FALSE(is_shrine_group(*picked)) << "seed " << seed;
  }
}

TEST(EventSelection, EveryListEmptyLeavesTheRoomEmpty) {
  RunState run = late_run();
  run.event_list.clear();
  run.shrine_list.clear();
  run.one_time_list.clear();
  EXPECT_FALSE(run.generate_event().has_value());
  run.enter_room(RoomType::Unknown);
  EXPECT_EQ(run.phase, Phase::Map);
}

// A `?` that stays a `?` starts the event the pick chose. One candidate, so
// there is one outcome.
TEST(EventSelection, AQuestionThatStaysAnEventStartsTheEvent) {
  RunState run = late_run();
  run.event_list = {EventId::BigFish};
  run.shrine_list.clear();
  run.one_time_list.clear();
  run.enter_room(RoomType::Unknown);
  EXPECT_EQ(run.phase, Phase::Event);
  EXPECT_TRUE(run.event.active);
  EXPECT_EQ(run.event.id, EventId::BigFish);
}

// ============================================================================
// Ssserpent Head (task #17)
// ============================================================================

// Every `?` NODE pays, whatever it becomes: here every roll is a fight.
TEST(SsserpentHead, PaysOnAQuestionNodeThatBecomesAFight) {
  RunState run = RunState::start(1);
  stand_before_a_question_row(run);
  run.obtain_relic(RelicId::SsserpentHead);
  run.monster_chance = 1.01f;  // every roll, 1.0 included, lands in the band
  run.shop_chance = 0.0f;
  run.treasure_chance = 0.0f;
  const int gold = run.gold;
  run.choose_path(first_room_column(run));
  ASSERT_EQ(run.phase, Phase::Combat);
  EXPECT_EQ(run.gold, gold + kSsserpentHeadGold);
}

TEST(SsserpentHead, PaysNothingOnARoomThatIsNotAQuestion) {
  RunState run = RunState::start(1);
  run.phase = Phase::Map;
  for (MapNode& node : run.map[0]) {
    if (node.is_room()) node.room = RoomType::Rest;
  }
  run.obtain_relic(RelicId::SsserpentHead);
  const int gold = run.gold;
  run.choose_path(first_room_column(run));
  EXPECT_EQ(run.gold, gold);
}

// The gold lands before the event gates read it. The Cleric is the only
// candidate and needs 35; the run has none until Ssserpent Head pays.
TEST(SsserpentHead, PaysBeforeTheEventGatesAreRead) {
  for (bool with_head : {false, true}) {
    RunState run = RunState::start(1);
    stand_before_a_question_row(run);
    if (with_head) run.obtain_relic(RelicId::SsserpentHead);
    run.gold = 0;
    run.monster_chance = 0.0f;  // every roll is an event
    run.shop_chance = 0.0f;
    run.treasure_chance = 0.0f;
    run.event_list = {EventId::TheCleric};
    run.shrine_list.clear();
    run.one_time_list.clear();
    run.choose_path(first_room_column(run));
    EXPECT_EQ(contains(run.event_list, EventId::TheCleric), !with_head)
        << (with_head ? "with" : "without") << " Ssserpent Head";
  }
}

// ============================================================================
// Damage outside combat (events.md §4.1)
// ============================================================================

TEST(EventDamage, TheBootRaisesOnlyThePlayersOwnHits) {
  EXPECT_EQ(event_damage_after_relics(3, EventDamage::Player, true, false, false), 5);
  EXPECT_EQ(event_damage_after_relics(3, EventDamage::Unowned, true, false, false), 3);
  EXPECT_EQ(event_damage_after_relics(3, EventDamage::HpLoss, true, false, false), 3);
  EXPECT_EQ(event_damage_after_relics(5, EventDamage::Player, true, false, false), 5);
  EXPECT_EQ(event_damage_after_relics(0, EventDamage::Player, true, false, false), 0);
}

TEST(EventDamage, ToriiLowersOnlyHitsWithAnOwner) {
  EXPECT_EQ(event_damage_after_relics(4, EventDamage::Player, false, true, false), 1);
  EXPECT_EQ(event_damage_after_relics(5, EventDamage::Player, false, true, false), 1);
  EXPECT_EQ(event_damage_after_relics(6, EventDamage::Player, false, true, false), 6);
  EXPECT_EQ(event_damage_after_relics(4, EventDamage::Unowned, false, true, false), 4);
  EXPECT_EQ(event_damage_after_relics(4, EventDamage::HpLoss, false, true, false), 4);
}

TEST(EventDamage, TungstenRodTakesOneOffEveryKind) {
  for (EventDamage kind :
       {EventDamage::Player, EventDamage::Unowned, EventDamage::HpLoss}) {
    EXPECT_EQ(event_damage_after_relics(7, kind, false, false, true), 6);
    EXPECT_EQ(event_damage_after_relics(1, kind, false, false, true), 0);
  }
}

// The Boot, then Torii, then Tungsten Rod: 3 becomes 5, then 1, then 0.
TEST(EventDamage, TheThreeApplyInStSsOrder) {
  EXPECT_EQ(event_damage_after_relics(3, EventDamage::Player, true, true, true), 0);
}

namespace {

RunState about_to_die() {
  RunState run = RunState::start(1);
  run.hp = 5;
  return run;
}

}  // namespace

TEST(EventDamage, FairyInABottleRevivesOutsideCombat) {
  RunState run = about_to_die();
  run.potions = {PotionId::FairyPotion};
  run.take_event_damage(10, EventDamage::Unowned);
  EXPECT_EQ(run.hp, run.max_hp * potion_base_potency(PotionId::FairyPotion) / 100);
  EXPECT_TRUE(run.potions.empty());
  EXPECT_FALSE(run.is_terminal());
}

TEST(EventDamage, SacredBarkDoublesTheFairy) {
  RunState run = about_to_die();
  run.potions = {PotionId::FairyPotion};
  run.obtain_relic(RelicId::SacredBark);
  run.take_event_damage(10, EventDamage::Unowned);
  EXPECT_EQ(run.hp,
            run.max_hp * 2 * potion_base_potency(PotionId::FairyPotion) / 100);
}

// MagicFlower.onPlayerHeal checks RoomPhase.COMBAT.
TEST(EventDamage, MagicFlowerDoesNotBoostARevive) {
  RunState run = about_to_die();
  run.potions = {PotionId::FairyPotion};
  run.obtain_relic(RelicId::MagicFlower);
  run.take_event_damage(10, EventDamage::Unowned);
  EXPECT_EQ(run.hp, run.max_hp * potion_base_potency(PotionId::FairyPotion) / 100);
}

TEST(EventDamage, LizardTailRevivesWhenThereIsNoFairy) {
  RunState run = about_to_die();
  run.obtain_relic(RelicId::LizardTail);
  run.take_event_damage(10, EventDamage::Unowned);
  EXPECT_EQ(run.hp, run.max_hp / 2);
  EXPECT_EQ(run.held_relic(RelicId::LizardTail)->counter, kLizardTailUsedUp);
  EXPECT_FALSE(run.is_terminal());
}

TEST(EventDamage, TheFairyComesBeforeLizardTail) {
  RunState run = about_to_die();
  run.potions = {PotionId::FairyPotion};
  run.obtain_relic(RelicId::LizardTail);
  run.take_event_damage(10, EventDamage::Unowned);
  EXPECT_TRUE(run.potions.empty());
  EXPECT_NE(run.held_relic(RelicId::LizardTail)->counter, kLizardTailUsedUp);
}

TEST(EventDamage, WithNeitherTheRunIsLost) {
  RunState run = about_to_die();
  run.take_event_damage(10, EventDamage::Unowned);
  EXPECT_EQ(run.hp, 0);
  EXPECT_EQ(run.outcome, Outcome::Lost);
}

TEST(EventDamage, ASpentLizardTailDoesNotRevive) {
  RunState run = about_to_die();
  run.obtain_relic(RelicId::LizardTail);
  run.held_relic(RelicId::LizardTail)->counter = kLizardTailUsedUp;
  run.take_event_damage(10, EventDamage::Unowned);
  EXPECT_EQ(run.outcome, Outcome::Lost);
}

// ============================================================================
// A curse and a relic from one option (events.md §4.2)
// ============================================================================

// A Darkstone Periapt obtained with the curse hears of it.
TEST(CurseThenRelic, ADarkstoneFromTheSameOptionGivesMaxHp) {
  RunState run = RunState::start(1);
  const int max_hp = run.max_hp;
  run.obtain_curse_then_relic(CardId::Regret, RelicId::DarkstonePeriapt);
  EXPECT_EQ(count_of(run, CardId::Regret), 1);
  EXPECT_EQ(run.max_hp, max_hp + kDarkstoneMaxHp);
}

// An Omamori obtained with the curse is too late to block it.
TEST(CurseThenRelic, AnOmamoriFromTheSameOptionCannotBlockIt) {
  RunState run = RunState::start(1);
  run.obtain_curse_then_relic(CardId::Regret, RelicId::Omamori);
  EXPECT_EQ(count_of(run, CardId::Regret), 1);
  EXPECT_EQ(run.held_relic(RelicId::Omamori)->counter, kOmamoriCharges);
}

TEST(CurseThenRelic, AnOmamoriAlreadyHeldBlocksItAndTheRelicStillComes) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Omamori);
  run.obtain_curse_then_relic(CardId::Regret, RelicId::CeramicFish);
  EXPECT_EQ(count_of(run, CardId::Regret), 0);
  EXPECT_EQ(run.held_relic(RelicId::Omamori)->counter, kOmamoriCharges - 1);
  EXPECT_TRUE(run.has_relic(RelicId::CeramicFish));
}

TEST(CurseThenRelic, ACeramicFishFromTheSameOptionPaysForTheCurse) {
  RunState run = RunState::start(1);
  const int gold = run.gold;
  run.obtain_curse_then_relic(CardId::Regret, RelicId::CeramicFish);
  EXPECT_EQ(run.gold, gold + kCeramicFishGold);
}

// ============================================================================
// The events
// ============================================================================

TEST(Events, OutsideAnEventNothingIsOffered) {
  RunState run = late_run();
  EXPECT_TRUE(run.event_options().empty());
  EXPECT_FALSE(run.choose_event_option(EventOption::BigFishBanana));
}

TEST(Events, AnOptionNotOnOfferIsRefused) {
  RunState run = late_run();
  run.begin_event(EventId::BigFish);
  const int hp = run.hp;
  EXPECT_FALSE(run.choose_event_option(EventOption::GoldenShrinePray));
  EXPECT_EQ(run.hp, hp);
  EXPECT_EQ(run.phase, Phase::Event);
}

TEST(Events, AnOptionEndsTheEventAndTheRunReturnsToTheMap) {
  RunState run = late_run();
  run.begin_event(EventId::GoldenShrine);
  ASSERT_TRUE(run.choose_event_option(EventOption::GoldenShrineLeave));
  EXPECT_FALSE(run.event.active);
  EXPECT_EQ(run.phase, Phase::Map);
}

// --- Big Fish --------------------------------------------------------------

// Three options and no way to decline (Rob W1).
TEST(BigFish, OffersThreeOptionsAndNoLeave) {
  RunState run = late_run();
  run.begin_event(EventId::BigFish);
  EXPECT_EQ(run.event_options(),
            (std::vector<EventOption>{EventOption::BigFishBanana,
                                      EventOption::BigFishDonut,
                                      EventOption::BigFishBox}));
}

TEST(BigFish, BananaHealsAThirdOfMaxHp) {
  RunState run = late_run();
  run.hp = 20;
  run.begin_event(EventId::BigFish);
  run.choose_event_option(EventOption::BigFishBanana);
  EXPECT_EQ(run.hp, 20 + run.max_hp / 3);
}

TEST(BigFish, DonutRaisesMaxHpAndHeals) {
  RunState run = late_run();
  run.hp = 20;
  const int max_hp = run.max_hp;
  run.begin_event(EventId::BigFish);
  run.choose_event_option(EventOption::BigFishDonut);
  EXPECT_EQ(run.max_hp, max_hp + kBigFishMaxHp);
  EXPECT_EQ(run.hp, 20 + kBigFishMaxHp);
}

TEST(BigFish, BoxGivesARegretAndARelic) {
  RunState run = late_run();
  const size_t relics = run.relics.size();
  run.begin_event(EventId::BigFish);
  run.choose_event_option(EventOption::BigFishBox);
  EXPECT_EQ(count_of(run, CardId::Regret), 1);
  EXPECT_EQ(run.relics.size(), relics + 1);
}

// An Omamori ALREADY held blocks the Box's Regret.
TEST(BigFish, AnOmamoriHeldBeforeTheBoxBlocksItsRegret) {
  RunState run = late_run();
  run.obtain_relic(RelicId::Omamori);
  run.begin_event(EventId::BigFish);
  run.choose_event_option(EventOption::BigFishBox);
  EXPECT_EQ(count_of(run, CardId::Regret), 0);
  EXPECT_EQ(run.held_relic(RelicId::Omamori)->counter, kOmamoriCharges - 1);
}

// --- Golden Idol -----------------------------------------------------------

TEST(GoldenIdol, ComputesItsAmountsOnEntry) {
  RunState run = late_run();
  run.max_hp = 80;
  run.begin_event(EventId::GoldenIdol);
  EXPECT_EQ(run.event.hp_amount, 20);   // (int)(80 * 0.25f)
  EXPECT_EQ(run.event.hp_amount2, 6);   // (int)(80 * 0.08f)
}

TEST(GoldenIdol, HideLosesAtLeastOneMaxHp) {
  RunState run = late_run();
  run.max_hp = 12;  // (int)(12 * 0.08f) is 0
  run.hp = 12;
  run.begin_event(EventId::GoldenIdol);
  EXPECT_EQ(run.event.hp_amount2, 1);
}

TEST(GoldenIdol, TakeGivesTheIdolAndOpensTheTrap) {
  RunState run = late_run();
  run.begin_event(EventId::GoldenIdol);
  ASSERT_TRUE(run.choose_event_option(EventOption::GoldenIdolTake));
  EXPECT_TRUE(run.has_relic(RelicId::GoldenIdol));
  EXPECT_EQ(run.phase, Phase::Event);
  EXPECT_EQ(run.event_options(),
            (std::vector<EventOption>{EventOption::GoldenIdolOutrun,
                                      EventOption::GoldenIdolSmash,
                                      EventOption::GoldenIdolHide}));
}

TEST(GoldenIdol, OutrunGivesAnInjury) {
  RunState run = late_run();
  run.begin_event(EventId::GoldenIdol);
  run.choose_event_option(EventOption::GoldenIdolTake);
  run.choose_event_option(EventOption::GoldenIdolOutrun);
  EXPECT_EQ(count_of(run, CardId::Injury), 1);
  EXPECT_EQ(run.phase, Phase::Map);
}

// No owner, so Torii does nothing to it; Tungsten Rod does. At 16 Max HP the
// hit is 4, inside Torii's range, so an owned hit would be 1 and then 0.
TEST(GoldenIdol, SmashIsDamageWithNoOwner) {
  RunState run = late_run();
  run.max_hp = 16;
  run.hp = 16;
  run.obtain_relic(RelicId::Torii);
  run.obtain_relic(RelicId::TungstenRod);
  run.begin_event(EventId::GoldenIdol);
  ASSERT_EQ(run.event.hp_amount, 4);
  run.choose_event_option(EventOption::GoldenIdolTake);
  run.choose_event_option(EventOption::GoldenIdolSmash);
  EXPECT_EQ(run.hp, 16 - 3);
}

TEST(GoldenIdol, HideLowersMaxHp) {
  RunState run = late_run();
  run.begin_event(EventId::GoldenIdol);
  const int max_hp = run.max_hp;
  const int loss = run.event.hp_amount2;
  run.choose_event_option(EventOption::GoldenIdolTake);
  run.choose_event_option(EventOption::GoldenIdolHide);
  EXPECT_EQ(run.max_hp, max_hp - loss);
}

TEST(GoldenIdol, LeaveTakesNothing) {
  RunState run = late_run();
  run.begin_event(EventId::GoldenIdol);
  run.choose_event_option(EventOption::GoldenIdolLeave);
  EXPECT_FALSE(run.has_relic(RelicId::GoldenIdol));
  EXPECT_EQ(run.phase, Phase::Map);
}

// --- World of Goop ---------------------------------------------------------

// The loss is rolled on entry and capped at the gold held. With less gold than
// the smallest roll, every roll is capped to all of it.
TEST(WorldOfGoop, TheLossIsCappedAtTheGoldHeld) {
  RunState run = late_run();
  run.gold = kGoopLossMin - 1;
  run.begin_event(EventId::WorldOfGoop);
  EXPECT_EQ(run.event.gold_amount, kGoopLossMin - 1);
}

TEST(WorldOfGoop, TheLossIsInItsRange) {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    RunState run = late_run(seed);
    run.begin_event(EventId::WorldOfGoop);
    EXPECT_GE(run.event.gold_amount, kGoopLossMin);
    EXPECT_LE(run.event.gold_amount, kGoopLossMax);
  }
}

TEST(WorldOfGoop, GatherCostsElevenHpAndPaysSeventyFive) {
  RunState run = late_run();
  const int hp = run.hp;
  const int gold = run.gold;
  run.begin_event(EventId::WorldOfGoop);
  run.choose_event_option(EventOption::GoopGather);
  EXPECT_EQ(run.hp, hp - kGoopDamage);
  EXPECT_EQ(run.gold, gold + kGoopGold);
}

TEST(WorldOfGoop, LeaveLosesTheRolledGold) {
  RunState run = late_run();
  run.begin_event(EventId::WorldOfGoop);
  const int gold = run.gold;
  const int loss = run.event.gold_amount;
  run.choose_event_option(EventOption::GoopLeave);
  EXPECT_EQ(run.gold, gold - loss);
}

// loseGold tells relics only in a ShopRoom, so Maw Bank keeps paying (wiki.gg:
// "will not break when spending Gold at an Event").
TEST(WorldOfGoop, LosingGoldHereDoesNotBreakMawBank) {
  RunState run = late_run();
  run.obtain_relic(RelicId::MawBank);
  run.begin_event(EventId::WorldOfGoop);
  ASSERT_GT(run.event.gold_amount, 0);
  run.choose_event_option(EventOption::GoopLeave);
  EXPECT_NE(run.held_relic(RelicId::MawBank)->counter, kMawBankUsedUp);
}

// --- The Ssssserpent -------------------------------------------------------

TEST(TheSsssserpent, AgreeGivesGoldAndADoubt) {
  RunState run = late_run();
  const int gold = run.gold;
  run.begin_event(EventId::TheSsssserpent);
  run.choose_event_option(EventOption::SsssserpentAgree);
  EXPECT_EQ(run.gold, gold + kSsssserpentGold);
  EXPECT_EQ(count_of(run, CardId::Doubt), 1);
}

TEST(TheSsssserpent, OmamoriBlocksTheDoubtButNotTheGold) {
  RunState run = late_run();
  run.obtain_relic(RelicId::Omamori);
  const int gold = run.gold;
  run.begin_event(EventId::TheSsssserpent);
  run.choose_event_option(EventOption::SsssserpentAgree);
  EXPECT_EQ(run.gold, gold + kSsssserpentGold);
  EXPECT_EQ(count_of(run, CardId::Doubt), 0);
}

// --- Scrap Ooze ------------------------------------------------------------

// `roll >= 99 - chance`: 26 of the 100 rolls pass at 25, not 25.
TEST(ScrapOoze, TheOddsAreOneMoreThanTheChanceShown) {
  for (int chance : {25, 35, 45}) {
    int passing = 0;
    for (int roll = 0; roll <= 99; ++roll) {
      if (scrap_ooze_finds_relic(roll, chance)) ++passing;
    }
    EXPECT_EQ(passing, chance + 1);
  }
}

// Whatever the roll: the damage is taken, and then either a relic ends the
// event or the next attempt costs 1 more.
TEST(ScrapOoze, ReachingInDamagesThenEndsOrRaisesTheStakes) {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    RunState run = late_run(seed);
    const int hp = run.hp;
    const size_t relics = run.relics.size();
    run.begin_event(EventId::ScrapOoze);
    run.choose_event_option(EventOption::ScrapOozeReachIn);
    EXPECT_EQ(run.hp, hp - kScrapOozeDamage) << "seed " << seed;
    if (run.relics.size() == relics + 1) {
      EXPECT_EQ(run.phase, Phase::Map) << "seed " << seed;
    } else {
      EXPECT_EQ(run.event.attempts, 1) << "seed " << seed;
      EXPECT_EQ(run.phase, Phase::Event) << "seed " << seed;
    }
  }
}

TEST(ScrapOoze, EachFailureMakesTheNextAttemptCostOneMore) {
  RunState run = late_run();
  run.begin_event(EventId::ScrapOoze);
  run.event.attempts = 2;
  const int hp = run.hp;
  run.choose_event_option(EventOption::ScrapOozeReachIn);
  EXPECT_EQ(run.hp, hp - (kScrapOozeDamage + 2 * kScrapOozeDamageStep));
}

// --- Shining Light ---------------------------------------------------------

TEST(ShiningLight, TheDamageIsAFifthOfMaxHpRounded) {
  RunState run = late_run();
  run.max_hp = 77;  // 15.4
  run.begin_event(EventId::ShiningLight);
  EXPECT_EQ(run.event.hp_amount, 15);
  run.max_hp = 78;  // 15.6
  run.begin_event(EventId::ShiningLight);
  EXPECT_EQ(run.event.hp_amount, 16);
}

TEST(ShiningLight, EnterUpgradesTwoCardsAfterTheDamage) {
  RunState run = late_run();
  const int hp = run.hp;
  run.begin_event(EventId::ShiningLight);
  const int damage = run.event.hp_amount;
  run.choose_event_option(EventOption::ShiningLightEnter);
  EXPECT_EQ(run.hp, hp - damage);
  EXPECT_EQ(upgraded_count(run), kShiningLightUpgrades);
}

// The hit has the player as owner, so Torii sees it: at 20 Max HP it is 4,
// which Torii makes 1. Unowned, it would stay 4.
TEST(ShiningLight, ItsHitIsOwnedByThePlayer) {
  RunState run = late_run();
  run.max_hp = 20;
  run.hp = 20;
  run.obtain_relic(RelicId::Torii);
  run.begin_event(EventId::ShiningLight);
  ASSERT_EQ(run.event.hp_amount, 4);
  run.choose_event_option(EventOption::ShiningLightEnter);
  EXPECT_EQ(run.hp, 19);
}

TEST(ShiningLight, WithOneUpgradableCardItUpgradesOne) {
  RunState run = late_run();
  run.master_deck = {Card{CardId::StrikePlus}, Card{CardId::Bash}};
  run.begin_event(EventId::ShiningLight);
  run.choose_event_option(EventOption::ShiningLightEnter);
  EXPECT_EQ(count_of(run, CardId::BashPlus), 1);
}

TEST(ShiningLight, EnterIsMaskedWithNothingToUpgrade) {
  RunState run = late_run();
  run.master_deck = {Card{CardId::StrikePlus}, Card{CardId::DefendPlus}};
  run.begin_event(EventId::ShiningLight);
  EXPECT_EQ(run.event_options(),
            std::vector<EventOption>{EventOption::ShiningLightLeave});
}

// --- Golden Shrine ---------------------------------------------------------

TEST(GoldenShrine, PrayGivesAHundredGold) {
  RunState run = late_run();
  const int gold = run.gold;
  run.begin_event(EventId::GoldenShrine);
  run.choose_event_option(EventOption::GoldenShrinePray);
  EXPECT_EQ(run.gold, gold + kGoldenShrineGold);
  EXPECT_EQ(count_of(run, CardId::Regret), 0);
}

TEST(GoldenShrine, DesecrateGivesMoreGoldAndARegret) {
  RunState run = late_run();
  const int gold = run.gold;
  run.begin_event(EventId::GoldenShrine);
  run.choose_event_option(EventOption::GoldenShrineDesecrate);
  EXPECT_EQ(run.gold, gold + kGoldenShrineDesecrateGold);
  EXPECT_EQ(count_of(run, CardId::Regret), 1);
}

// --- The Divine Fountain ---------------------------------------------------

TEST(DivineFountain, DrinkRemovesEveryCurseThatCanLeave) {
  RunState run = late_run();
  run.add_card(Card{CardId::Regret});
  run.add_card(Card{CardId::Injury});
  run.add_card(Card{CardId::Parasite});
  run.add_card(Card{CardId::CurseOfTheBell});
  const int max_hp = run.max_hp;
  const size_t deck = run.master_deck.size();
  run.begin_event(EventId::DivineFountain);
  run.choose_event_option(EventOption::DivineFountainDrink);
  EXPECT_EQ(count_of(run, CardId::Regret), 0);
  EXPECT_EQ(count_of(run, CardId::Injury), 0);
  EXPECT_EQ(count_of(run, CardId::Parasite), 0);
  EXPECT_EQ(count_of(run, CardId::CurseOfTheBell), 1);
  EXPECT_EQ(run.master_deck.size(), deck - 3);
  EXPECT_EQ(run.max_hp, max_hp - 3) << "Parasite's removal costs Max HP";
}

// --- Face Trader -----------------------------------------------------------

TEST(FaceTrader, TouchPaysThenHurts) {
  RunState run = late_run();
  run.max_hp = 80;
  run.hp = 50;
  const int gold = run.gold;
  run.begin_event(EventId::FaceTrader);
  EXPECT_EQ(run.event.hp_amount, 8);
  run.choose_event_option(EventOption::FaceTraderTouch);
  EXPECT_EQ(run.gold, gold + kFaceTraderGold);
  EXPECT_EQ(run.hp, 42);
}

// No owner: at 50 Max HP the hit is 5, which Torii would make 1 if it had one.
TEST(FaceTrader, TouchHasNoOwner) {
  RunState run = late_run();
  run.max_hp = 50;
  run.hp = 50;
  run.obtain_relic(RelicId::Torii);
  run.begin_event(EventId::FaceTrader);
  run.choose_event_option(EventOption::FaceTraderTouch);
  EXPECT_EQ(run.hp, 45);
}

TEST(FaceTrader, TouchHurtsAtLeastOne) {
  RunState run = late_run();
  run.max_hp = 9;
  run.hp = 9;
  run.begin_event(EventId::FaceTrader);
  EXPECT_EQ(run.event.hp_amount, 1);
}

TEST(FaceTrader, TradeGivesAFaceNotAlreadyHeld) {
  for (uint64_t seed = 0; seed < 10; ++seed) {
    RunState run = late_run(seed);
    const std::vector<RelicId> faces = face_trader_faces();
    // Four faces held: the fifth is the only one left.
    for (size_t i = 0; i + 1 < faces.size(); ++i) run.obtain_relic(faces[i]);
    run.begin_event(EventId::FaceTrader);
    run.choose_event_option(EventOption::FaceTraderTrade);
    EXPECT_TRUE(run.has_relic(faces.back())) << "seed " << seed;
  }
}

// --- The faces it gives ----------------------------------------------------

namespace {

// A fight won at `end_hp` with `ids` held, as the combat-end relic tests do.
RunState won_a_fight_at(int end_hp, std::vector<RelicId> ids) {
  RunState run = RunState::start(1);
  for (RelicId id : ids) run.obtain_relic(id);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = end_hp;
  run.end_combat();
  return run;
}

// A chest on floor 9 with `ids` held.
RunState opened_a_chest(std::vector<RelicId> ids) {
  RunState run = RunState::start(1);
  for (RelicId id : ids) run.obtain_relic(id);
  run.floor = 9;
  run.open_chest();
  return run;
}

}  // namespace

// onVictory: +1 Max HP, which heals 1, on top of Burning Blood's 6.
TEST(FaceOfCleric, EachWinRaisesMaxHpByOne) {
  const RunState control = won_a_fight_at(40, {});
  const RunState run = won_a_fight_at(40, {RelicId::FaceOfCleric});
  EXPECT_EQ(run.max_hp, control.max_hp + kFaceOfClericMaxHp);
  EXPECT_EQ(run.hp, control.hp + kFaceOfClericMaxHp);
}

TEST(FaceOfCleric, ALostFightRaisesNothing) {
  const RunState run = won_a_fight_at(0, {RelicId::FaceOfCleric});
  EXPECT_EQ(run.max_hp, IRONCLAD_MAX_HP);
}

// A Smoke Bomb escape reaches endBattle, and so onVictory.
TEST(FaceOfCleric, ASmokeBombEscapeCountsToo) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::FaceOfCleric);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 40;
  run.combat.outcome = Outcome::Escaped;
  run.end_combat();
  EXPECT_EQ(run.max_hp, IRONCLAD_MAX_HP + kFaceOfClericMaxHp);
}

// increaseMaxHp heals through heal(), and the room is still in combat, so Magic
// Flower makes the 1 a 2.
TEST(FaceOfCleric, MagicFlowerBoostsItsHeal) {
  const RunState control = won_a_fight_at(40, {RelicId::MagicFlower});
  const RunState run =
      won_a_fight_at(40, {RelicId::MagicFlower, RelicId::FaceOfCleric});
  EXPECT_EQ(run.max_hp, control.max_hp + 1);
  EXPECT_EQ(run.hp, control.hp + 2);
}

TEST(NlothsHungryFace, StartsWithOneCharge) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::NlothsHungryFace);
  EXPECT_EQ(run.held_relic(RelicId::NlothsHungryFace)->counter,
            kNlothsHungryFaceCharges);
}

// The next chest drops no relic; its gold is untouched, and the charge is
// spent. The chest rolls the same with the face held, so the gold is compared
// with a run that does not hold it.
TEST(NlothsHungryFace, TheNextChestDropsItsRelic) {
  const RunState control = opened_a_chest({});
  RunState run = opened_a_chest({RelicId::NlothsHungryFace});
  EXPECT_EQ(run.relics.size(), control.relics.size());  // the face, not the relic
  EXPECT_EQ(run.gold, control.gold);
  EXPECT_EQ(run.held_relic(RelicId::NlothsHungryFace)->counter,
            kNlothsHungryFaceUsedUp);
}

TEST(NlothsHungryFace, OnlyTheNextChest) {
  RunState run = opened_a_chest({RelicId::NlothsHungryFace});
  const size_t before = run.relics.size();
  run.floor = 10;
  run.open_chest();
  EXPECT_EQ(run.relics.size(), before + 1);
}

// removeOneRelicFromRewards takes the first relic on the list, and
// Matryoshka's is added before the chest's own: the chest's relic is what
// remains. The control holds Matryoshka alone, so its last relic is the
// chest's.
TEST(NlothsHungryFace, WithMatryoshkaItTakesMatryoshkasRelic) {
  const RunState control = opened_a_chest({RelicId::Matryoshka});
  const RunState run =
      opened_a_chest({RelicId::Matryoshka, RelicId::NlothsHungryFace});
  // Starter + Matryoshka + its relic + the chest's, against starter +
  // Matryoshka + the face + the chest's.
  ASSERT_EQ(run.relics.size(), control.relics.size());
  EXPECT_EQ(run.relics.back().id, control.relics.back().id);
  EXPECT_FALSE(run.has_relic(control.relics[control.relics.size() - 2].id))
      << "Matryoshka's relic should be the one removed";
}

TEST(FaceTrader, TradeGivesOneFace) {
  RunState run = late_run();
  run.begin_event(EventId::FaceTrader);
  run.choose_event_option(EventOption::FaceTraderTrade);
  int held = 0;
  for (RelicId face : face_trader_faces()) held += run.has_relic(face) ? 1 : 0;
  EXPECT_EQ(held, 1);
}
