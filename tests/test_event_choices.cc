// Events, step 2 (docs/design/events.md §8): the events that ask for a card
// from the master deck, and the deck-choice purposes they need (§3.3).
//
// Decks are built for each test, so which card a choice can take is fixed.
// The one draw a test cannot fix (a transform's new card, the Wing Statue's
// gold) is asserted by what holds for every draw.

#include <gtest/gtest.h>

#include <algorithm>

#include "event.h"
#include "relic.h"
#include "run_state.h"

using namespace minispire;

namespace {

RunState late_run(std::vector<CardId> deck) {
  RunState run = RunState::start(1);
  run.floor = 7;
  run.gold = 100;
  run.phase = Phase::Map;
  run.master_deck.clear();
  for (CardId id : deck) run.add_card(Card{id});
  return run;
}

int count_of(const RunState& run, CardId id) {
  return static_cast<int>(std::count_if(
      run.master_deck.begin(), run.master_deck.end(),
      [id](const Card& c) { return c.card_id == id; }));
}

// Enters `id`, takes `option`, and answers the deck choice it opens with
// `pick`.
void take_and_pick(RunState& run, EventId id, EventOption option, CardId pick) {
  run.begin_event(id);
  ASSERT_TRUE(run.choose_event_option(option));
  ASSERT_TRUE(run.has_pending_deck_choice());
  ASSERT_TRUE(run.resolve_deck_choice(pick));
}

}  // namespace

// ============================================================================
// Rarity (Bonfire Spirits pays by it)
// ============================================================================

TEST(DeckCardRarity, ReadsTheUnupgradedCard) {
  EXPECT_EQ(deck_card_rarity(CardId::Strike), DeckCardRarity::Basic);
  EXPECT_EQ(deck_card_rarity(CardId::BashPlus), DeckCardRarity::Basic);
  EXPECT_EQ(deck_card_rarity(CardId::IronWave), DeckCardRarity::Common);
  EXPECT_EQ(deck_card_rarity(CardId::IronWavePlus), DeckCardRarity::Common);
  EXPECT_EQ(deck_card_rarity(CardId::Inflame), DeckCardRarity::Uncommon);
  EXPECT_EQ(deck_card_rarity(CardId::SearingBlow3), DeckCardRarity::Uncommon);
  EXPECT_EQ(deck_card_rarity(CardId::Offering), DeckCardRarity::Rare);
  EXPECT_EQ(deck_card_rarity(CardId::BandageUp), DeckCardRarity::Uncommon);
  EXPECT_EQ(deck_card_rarity(CardId::Apotheosis), DeckCardRarity::Rare);
  EXPECT_EQ(deck_card_rarity(CardId::Regret), DeckCardRarity::Curse);
  EXPECT_EQ(deck_card_rarity(CardId::CurseOfTheBell), DeckCardRarity::Curse);
}

// No card that can sit in a deck is Special in this vocabulary: every one
// falls in a pool, the starters or the curses. Statuses never reach a deck.
TEST(DeckCardRarity, NoDeckCardIsSpecial) {
  for (const auto& [id, data] : CARD_DATABASE) {
    if (data.type == CardType::Status) continue;
    EXPECT_NE(deck_card_rarity(id), DeckCardRarity::Special) << data.name;
  }
}

// ============================================================================
// The event deck choice (events.md §3.3)
// ============================================================================

// An event's removal lists getGroupWithoutBottledCards: the bottled Bash is
// not offered.
TEST(EventDeckChoice, AnEventsRemovalSkipsBottledCards) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  run.master_deck[1].bottle = Bottle::Flame;
  run.begin_event(EventId::Purifier);
  run.choose_event_option(EventOption::PurifierPray);
  EXPECT_EQ(run.deck_choice_candidates(), std::vector<CardId>{CardId::Strike});
}

TEST(EventDeckChoice, AnUpgradeOffersOnlyUpgradableCards) {
  RunState run = late_run({CardId::StrikePlus, CardId::Bash});
  run.begin_event(EventId::UpgradeShrine);
  run.choose_event_option(EventOption::UpgradeShrinePray);
  EXPECT_EQ(run.deck_choice_candidates(), std::vector<CardId>{CardId::Bash});
}

// getUpgradableCards keeps bottled cards, unlike the removal lists.
TEST(EventDeckChoice, AnUpgradeCanTakeABottledCard) {
  RunState run = late_run({CardId::StrikePlus, CardId::Bash});
  run.master_deck[1].bottle = Bottle::Flame;
  run.begin_event(EventId::UpgradeShrine);
  run.choose_event_option(EventOption::UpgradeShrinePray);
  EXPECT_EQ(run.deck_choice_candidates(), std::vector<CardId>{CardId::Bash});
}

TEST(EventDeckChoice, TheRoomExitsOnceTheChoiceIsAnswered) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  run.begin_event(EventId::Purifier);
  run.choose_event_option(EventOption::PurifierPray);
  EXPECT_EQ(run.phase, Phase::Event);
  run.resolve_deck_choice(CardId::Strike);
  EXPECT_EQ(run.phase, Phase::Map);
}

// The events guard their grids on the list being non-empty.
TEST(EventDeckChoice, NothingEligibleOpensNoChoice) {
  RunState run = late_run({CardId::CurseOfTheBell});
  run.begin_event(EventId::Purifier);
  run.choose_event_option(EventOption::PurifierPray);
  EXPECT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(run.phase, Phase::Map);
}

// ============================================================================
// The Cleric
// ============================================================================

TEST(TheCleric, HealCostsThirtyFiveAndHealsAQuarter) {
  RunState run = late_run({CardId::Strike});
  run.max_hp = 80;
  run.hp = 30;
  run.begin_event(EventId::TheCleric);
  run.choose_event_option(EventOption::ClericHeal);
  EXPECT_EQ(run.gold, 100 - kClericHealCost);
  EXPECT_EQ(run.hp, 30 + 20);
}

TEST(TheCleric, PurifyCostsFiftyAndRemovesACard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::TheCleric, EventOption::ClericPurify,
                CardId::Strike);
  EXPECT_EQ(run.gold, 100 - kClericPurifyCost);
  EXPECT_EQ(count_of(run, CardId::Strike), 0);
}

// With nothing to remove there is no grid, and no gold is taken.
TEST(TheCleric, PurifyTakesNoGoldWithNothingToRemove) {
  RunState run = late_run({CardId::CurseOfTheBell});
  run.begin_event(EventId::TheCleric);
  run.choose_event_option(EventOption::ClericPurify);
  EXPECT_EQ(run.gold, 100);
  EXPECT_FALSE(run.has_pending_deck_choice());
}

TEST(TheCleric, EachOptionIsMaskedBelowItsPrice) {
  RunState run = late_run({CardId::Strike});
  run.gold = kClericPurifyCost - 1;
  run.begin_event(EventId::TheCleric);
  EXPECT_EQ(run.event_options(),
            (std::vector<EventOption>{EventOption::ClericHeal,
                                      EventOption::ClericLeave}));
  run.gold = kClericHealCost - 1;
  EXPECT_EQ(run.event_options(),
            std::vector<EventOption>{EventOption::ClericLeave});
}

TEST(TheCleric, ItsCostsDoNotBreakMawBank) {
  RunState run = late_run({CardId::Strike});
  run.obtain_relic(RelicId::MawBank);
  run.begin_event(EventId::TheCleric);
  run.choose_event_option(EventOption::ClericHeal);
  EXPECT_NE(run.held_relic(RelicId::MawBank)->counter, kMawBankUsedUp);
}

// ============================================================================
// Wing Statue
// ============================================================================

TEST(WingStatue, PrayCostsSevenThenRemovesACard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  const int hp = run.hp;
  take_and_pick(run, EventId::WingStatue, EventOption::WingStatuePray,
                CardId::Bash);
  EXPECT_EQ(run.hp, hp - kWingStatueDamage);
  EXPECT_EQ(count_of(run, CardId::Bash), 0);
}

// The starter deck's biggest hit is Bash's 8: Destroy is masked.
TEST(WingStatue, DestroyNeedsAnAttackOfTenBaseDamage) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  run.begin_event(EventId::WingStatue);
  EXPECT_EQ(run.event_options(),
            (std::vector<EventOption>{EventOption::WingStatuePray,
                                      EventOption::WingStatueLeave}));
}

// baseDamage is the card as it stands: Bash+ is 10.
TEST(WingStatue, AnUpgradeCanMeetTheRequirement) {
  RunState run = late_run({CardId::Strike, CardId::BashPlus});
  run.begin_event(EventId::WingStatue);
  const std::vector<EventOption> options = run.event_options();
  EXPECT_NE(std::find(options.begin(), options.end(),
                      EventOption::WingStatueDestroy),
            options.end());
}

TEST(WingStatue, DestroyPaysFiftyToEighty) {
  for (uint64_t floor = 1; floor <= 15; ++floor) {
    RunState run = late_run({CardId::Bludgeon});
    run.floor = static_cast<int>(floor);
    run.begin_event(EventId::WingStatue);
    run.choose_event_option(EventOption::WingStatueDestroy);
    EXPECT_GE(run.gold, 100 + kWingStatueGoldMin);
    EXPECT_LE(run.gold, 100 + kWingStatueGoldMax);
  }
}

// ============================================================================
// Living Wall, Transmogrifier, Purifier, Upgrade Shrine
// ============================================================================

TEST(LivingWall, ForgetRemovesACard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::LivingWall, EventOption::LivingWallForget,
                CardId::Strike);
  EXPECT_EQ(run.master_deck.size(), 1u);
}

// transformCard(c, false, …): a different card, not upgraded.
TEST(LivingWall, ChangeTransformsWithoutUpgrading) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::LivingWall, EventOption::LivingWallChange,
                CardId::Strike);
  ASSERT_EQ(run.master_deck.size(), 2u);
  EXPECT_EQ(count_of(run, CardId::Strike), 0);
  EXPECT_FALSE(is_upgraded(run.master_deck.back().card_id));
}

TEST(LivingWall, GrowUpgradesTheCard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::LivingWall, EventOption::LivingWallGrow,
                CardId::Bash);
  EXPECT_EQ(count_of(run, CardId::BashPlus), 1);
}

TEST(LivingWall, GrowIsMaskedWithNothingToUpgrade) {
  RunState run = late_run({CardId::StrikePlus});
  run.begin_event(EventId::LivingWall);
  EXPECT_EQ(run.event_options(),
            (std::vector<EventOption>{EventOption::LivingWallForget,
                                      EventOption::LivingWallChange}));
}

TEST(Transmogrifier, PrayTransformsACard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::Transmogrifier, EventOption::TransmogrifierPray,
                CardId::Bash);
  EXPECT_EQ(run.master_deck.size(), 2u);
  EXPECT_EQ(count_of(run, CardId::Bash), 0);
}

TEST(Purifier, PrayRemovesACard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::Purifier, EventOption::PurifierPray,
                CardId::Strike);
  EXPECT_EQ(run.master_deck.size(), 1u);
  EXPECT_EQ(count_of(run, CardId::Bash), 1);
}

TEST(UpgradeShrine, PrayIsMaskedWithNothingToUpgrade) {
  RunState run = late_run({CardId::StrikePlus});
  run.begin_event(EventId::UpgradeShrine);
  EXPECT_EQ(run.event_options(),
            std::vector<EventOption>{EventOption::UpgradeShrineLeave});
}

// ============================================================================
// Ominous Forge
// ============================================================================

TEST(OminousForge, ForgeUpgradesACard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::OminousForge, EventOption::OminousForgeForge,
                CardId::Strike);
  EXPECT_EQ(count_of(run, CardId::StrikePlus), 1);
}

TEST(OminousForge, RummageGivesPainAndWarpedTongs) {
  RunState run = late_run({CardId::Strike});
  run.begin_event(EventId::OminousForge);
  run.choose_event_option(EventOption::OminousForgeRummage);
  EXPECT_EQ(count_of(run, CardId::Pain), 1);
  EXPECT_TRUE(run.has_relic(RelicId::WarpedTongs));
}

TEST(OminousForge, ForgeIsMaskedWithNothingToUpgrade) {
  RunState run = late_run({CardId::StrikePlus});
  run.begin_event(EventId::OminousForge);
  EXPECT_EQ(run.event_options(),
            (std::vector<EventOption>{EventOption::OminousForgeRummage,
                                      EventOption::OminousForgeLeave}));
}

// ============================================================================
// Bonfire Spirits
// ============================================================================

namespace {

// Enters Bonfire Spirits, whose offer opens at once, and offers `card`.
void offer(RunState& run, CardId card) {
  run.begin_event(EventId::BonfireSpirits);
  ASSERT_TRUE(run.has_pending_deck_choice());
  ASSERT_TRUE(run.resolve_deck_choice(card));
}

}  // namespace

TEST(BonfireSpirits, TheOfferOpensOnEntry) {
  RunState run = late_run({CardId::Strike});
  run.begin_event(EventId::BonfireSpirits);
  EXPECT_TRUE(run.has_pending_deck_choice());
  EXPECT_EQ(run.phase, Phase::Event);
}

TEST(BonfireSpirits, ACurseGivesSpiritPoop) {
  RunState run = late_run({CardId::Strike, CardId::Regret});
  offer(run, CardId::Regret);
  EXPECT_TRUE(run.has_relic(RelicId::SpiritPoop));
  EXPECT_EQ(count_of(run, CardId::Regret), 0);
}

TEST(BonfireSpirits, ABasicCardGivesNothing) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  run.hp = 30;
  offer(run, CardId::Strike);
  EXPECT_EQ(run.hp, 30);
  EXPECT_EQ(count_of(run, CardId::Strike), 0);
}

TEST(BonfireSpirits, ACommonHealsFive) {
  RunState run = late_run({CardId::Strike, CardId::IronWave});
  run.hp = 30;
  offer(run, CardId::IronWave);
  EXPECT_EQ(run.hp, 30 + kBonfireCommonHeal);
}

TEST(BonfireSpirits, AnUncommonHealsToFull) {
  RunState run = late_run({CardId::Strike, CardId::Inflame});
  run.hp = 30;
  offer(run, CardId::Inflame);
  EXPECT_EQ(run.hp, run.max_hp);
}

TEST(BonfireSpirits, ARareRaisesMaxHpAndHealsToFull) {
  RunState run = late_run({CardId::Strike, CardId::Offering});
  run.hp = 30;
  const int max_hp = run.max_hp;
  offer(run, CardId::Offering);
  EXPECT_EQ(run.max_hp, max_hp + kBonfireRareMaxHp);
  EXPECT_EQ(run.hp, run.max_hp);
}

// setReward, then removeCard: the Parasite pays Spirit Poop, then its 3 Max HP.
TEST(BonfireSpirits, AParasiteGivesSpiritPoopAndCostsMaxHp) {
  RunState run = late_run({CardId::Strike, CardId::Parasite});
  const int max_hp = run.max_hp;
  offer(run, CardId::Parasite);
  EXPECT_TRUE(run.has_relic(RelicId::SpiritPoop));
  EXPECT_EQ(run.max_hp, max_hp - 3);
}

// Curse of the Bell cannot be offered; with nothing else, nothing happens.
TEST(BonfireSpirits, NothingToOfferEndsTheEvent) {
  RunState run = late_run({CardId::CurseOfTheBell});
  run.begin_event(EventId::BonfireSpirits);
  EXPECT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(run.phase, Phase::Map);
}

// ============================================================================
// A Note For Yourself
// ============================================================================

TEST(NoteForYourself, TakeAndGiveObtainsIronWaveThenStoresACard) {
  RunState run = late_run({CardId::Strike, CardId::Bash});
  take_and_pick(run, EventId::NoteForYourself,
                EventOption::NoteForYourselfTakeAndGive, CardId::Bash);
  EXPECT_EQ(count_of(run, CardId::IronWave), 1);
  EXPECT_EQ(count_of(run, CardId::Bash), 0);
}

// The Iron Wave joins the deck before the grid opens, so it can be stored.
TEST(NoteForYourself, TheNotedCardCanBeTheOneStored) {
  RunState run = late_run({CardId::Strike});
  run.begin_event(EventId::NoteForYourself);
  run.choose_event_option(EventOption::NoteForYourselfTakeAndGive);
  const std::vector<CardId> candidates = run.deck_choice_candidates();
  EXPECT_NE(std::find(candidates.begin(), candidates.end(), CardId::IronWave),
            candidates.end());
}

// onObtainCard runs for it: Molten Egg upgrades the Attack.
TEST(NoteForYourself, TheEggsSeeTheNotedCard) {
  RunState run = late_run({CardId::Strike});
  run.obtain_relic(RelicId::MoltenEgg);
  run.begin_event(EventId::NoteForYourself);
  run.choose_event_option(EventOption::NoteForYourselfTakeAndGive);
  EXPECT_EQ(count_of(run, CardId::IronWavePlus), 1);
}

TEST(NoteForYourself, IgnoreChangesNothing) {
  RunState run = late_run({CardId::Strike});
  run.begin_event(EventId::NoteForYourself);
  run.choose_event_option(EventOption::NoteForYourselfIgnore);
  EXPECT_EQ(run.master_deck.size(), 1u);
  EXPECT_EQ(run.phase, Phase::Map);
}

// ============================================================================
// Wheel of Change (each result applied directly)
// ============================================================================

TEST(WheelOfChange, ZeroPaysAHundredGold) {
  RunState run = late_run({CardId::Strike});
  run.apply_wheel_result(0);
  EXPECT_EQ(run.gold, 100 + kWheelGold);
}

TEST(WheelOfChange, OneGivesARelic) {
  RunState run = late_run({CardId::Strike});
  const size_t relics = run.relics.size();
  run.apply_wheel_result(1);
  EXPECT_EQ(run.relics.size(), relics + 1);
}

TEST(WheelOfChange, TwoHealsToFull) {
  RunState run = late_run({CardId::Strike});
  run.hp = 10;
  run.apply_wheel_result(2);
  EXPECT_EQ(run.hp, run.max_hp);
}

TEST(WheelOfChange, ThreeGivesDecay) {
  RunState run = late_run({CardId::Strike});
  run.apply_wheel_result(3);
  EXPECT_EQ(count_of(run, CardId::Decay), 1);
}

TEST(WheelOfChange, FourRemovesACard) {
  RunState run = late_run({CardId::Strike});
  run.event.id = EventId::WheelOfChange;  // the event the choice belongs to
  run.apply_wheel_result(4);
  EXPECT_TRUE(run.has_pending_deck_choice());
}

// HP_LOSS with no owner: Torii does not touch it; Tungsten Rod does. 10% of
// 50 is 5, inside Torii's range.
TEST(WheelOfChange, FiveIsHpLossThatOnlyTungstenRodChanges) {
  RunState run = late_run({CardId::Strike});
  run.max_hp = 50;
  run.hp = 50;
  run.obtain_relic(RelicId::Torii);
  run.obtain_relic(RelicId::TungstenRod);
  run.apply_wheel_result(5);
  EXPECT_EQ(run.hp, 50 - 4);
}

// The spin happens on entry; whatever it lands, the event ends (once a
// removal, if that is the result, is answered).
TEST(WheelOfChange, EnteringSpinsAndEnds) {
  for (int floor = 1; floor <= 12; ++floor) {
    RunState run = late_run({CardId::Strike, CardId::Bash});
    run.floor = floor;
    run.begin_event(EventId::WheelOfChange);
    if (run.has_pending_deck_choice()) {
      run.resolve_deck_choice(run.deck_choice_candidates().front());
    }
    EXPECT_EQ(run.phase, Phase::Map) << "floor " << floor;
  }
}
