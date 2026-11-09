// Events, step 4 (docs/design/events.md §8): the two events that end in a
// fight, Dead Adventurer and Hypnotizing Colored Mushrooms; the fight an event
// starts and what its reward screen pays (§4.6); the awake Lagavulin, The
// Mushroom Lair, and Odd Mushroom.
//
// No test depends on a draw's result. Dead Adventurer's search is tested
// through search_dead_adventurer(roll) with chosen rolls, its reward order is
// set by the test, and the amounts an event rolls are checked by their range.

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

#include "action.h"
#include "combat_env.h"
#include "encounter.h"
#include "enemy.h"
#include "event.h"
#include "query.h"
#include "relic.h"
#include "run_state.h"
#include "turn_loop.h"

using namespace minispire;

namespace {

RunState event_run(uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  run.floor = 7;
  run.gold = 100;
  run.phase = Phase::Map;
  run.potions.clear();
  return run;
}

std::vector<EnemyKind> kinds(const std::vector<Enemy>& enemies) {
  std::vector<EnemyKind> out;
  for (const Enemy& e : enemies) out.push_back(e.kind);
  return out;
}

const std::vector<EnemyKind> kMushroomLair = {
    EnemyKind::FungiBeast, EnemyKind::FungiBeast, EnemyKind::FungiBeast};

// Ends the current fight as a win: what exit_current_room does in the run
// walk, without needing the enemies dead.
void win(RunState& run) {
  run.combat.character.hp = std::max(1, run.combat.character.hp);
  run.end_combat();
}

// An event fight with these rewards on the room, as an event leaves it.
RunState event_fight(int gold, std::vector<RelicId> relics,
                     std::vector<RelicId> held = {}) {
  RunState run = event_run();
  for (RelicId id : held) run.obtain_relic(id);
  run.event_fight_gold = gold;
  run.event_fight_relics = std::move(relics);
  run.begin_event_combat(EventEncounter::MushroomLair, /*elite_trigger=*/false);
  return run;
}

// Dead Adventurer entered with its rewards in a chosen order.
RunState dead_adventurer(std::array<DeadAdventurerReward, 3> order,
                         DeadAdventurerElite elite =
                             DeadAdventurerElite::GremlinNob) {
  RunState run = event_run();
  run.begin_event(EventId::DeadAdventurer);
  run.event.rewards = order;
  run.event.elite = elite;
  return run;
}

using R = DeadAdventurerReward;

}  // namespace

// ============================================================================
// The encounters
// ============================================================================

TEST(EventEncounters, AreTheFightsTheEventsName) {
  std::mt19937 rng(1);
  EXPECT_EQ(kinds(make_event_encounter(EventEncounter::ThreeSentries, rng)),
            (std::vector<EnemyKind>{EnemyKind::Sentry, EnemyKind::Sentry,
                                    EnemyKind::Sentry}));
  EXPECT_EQ(kinds(make_event_encounter(EventEncounter::GremlinNob, rng)),
            std::vector<EnemyKind>{EnemyKind::GremlinNob});
  EXPECT_EQ(kinds(make_event_encounter(EventEncounter::LagavulinAwake, rng)),
            std::vector<EnemyKind>{EnemyKind::Lagavulin});
  EXPECT_EQ(kinds(make_event_encounter(EventEncounter::MushroomLair, rng)),
            kMushroomLair);
}

// new Lagavulin(false): no block, no Metallicize, Siphon Soul first.
TEST(AwakeLagavulin, StartsAwakeWithSiphonSoul) {
  std::mt19937 rng(1);
  const Enemy e = make_lagavulin_awake(rng);
  EXPECT_FALSE(e.is_asleep);
  EXPECT_EQ(e.powers.count(Power::Metallicize), 0u);
  EXPECT_EQ(e.current_block, 0);
  EXPECT_EQ(e.last_move, MoveName::SiphonSoul);
  EXPECT_GE(e.max_hp, 109);
  EXPECT_LE(e.max_hp, 111);
}

// getMove: Attack while fewer than two attacks follow the last Siphon Soul.
TEST(AwakeLagavulin, CyclesAttackAttackSiphonSoul) {
  std::mt19937 rng(1);
  Enemy e = make_lagavulin_awake(rng);
  const std::vector<MoveName> expected = {
      MoveName::LagavulinAttack1, MoveName::LagavulinAttack2,
      MoveName::SiphonSoul, MoveName::LagavulinAttack1};
  for (MoveName move : expected) EXPECT_EQ(select_next_move(e, rng), move);
}

// isOutTriggered is set in the constructor, so a hit never stuns it.
TEST(AwakeLagavulin, AHitDoesNotStunIt) {
  RunState run = event_run();
  run.begin_event_combat(EventEncounter::LagavulinAwake, true);
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::DealFixedDamage};
  a.target = 0;
  a.amount = 5;
  q.push_back(a);
  drain(run.combat, q, ctx);
  ASSERT_LT(run.combat.enemies[0].hp, run.combat.enemies[0].max_hp);
  EXPECT_EQ(run.combat.enemies[0].last_move, MoveName::SiphonSoul);
}

// ============================================================================
// The fight an event starts
// ============================================================================

TEST(EventFight, DeadAdventurersFightCountsAsElite) {
  RunState run = event_run();
  run.begin_event_combat(EventEncounter::GremlinNob, /*elite_trigger=*/true);
  EXPECT_EQ(run.phase, Phase::Combat);
  EXPECT_EQ(run.combat_source, RewardSource::Event);
  EXPECT_TRUE(run.combat.is_elite);
}

TEST(EventFight, TheMushroomLairIsNotElite) {
  RunState run = event_run();
  run.begin_event_combat(EventEncounter::MushroomLair, /*elite_trigger=*/false);
  EXPECT_EQ(run.combat_source, RewardSource::Event);
  EXPECT_FALSE(run.combat.is_elite);
}

// The three relics that read eliteTrigger.
TEST(EventFight, EliteTriggerRelicsWorkInDeadAdventurersFight) {
  RunState run = event_run();
  run.obtain_relic(RelicId::SlingOfCourage);
  run.obtain_relic(RelicId::SlaversCollar);
  run.obtain_relic(RelicId::PreservedInsect);
  run.begin_event_combat(EventEncounter::GremlinNob, /*elite_trigger=*/true);
  EXPECT_EQ(run.combat.character.powers[Power::Strength], 2);
  EXPECT_EQ(run.combat.character.energy_per_turn, IRONCLAD_ENERGY_PER_TURN + 1);
  EXPECT_LT(run.combat.enemies[0].hp, run.combat.enemies[0].max_hp);
}

TEST(EventFight, EliteTriggerRelicsDoNothingInTheMushroomLair) {
  RunState run = event_run();
  run.obtain_relic(RelicId::SlingOfCourage);
  run.obtain_relic(RelicId::SlaversCollar);
  run.obtain_relic(RelicId::PreservedInsect);
  run.begin_event_combat(EventEncounter::MushroomLair, /*elite_trigger=*/false);
  EXPECT_EQ(run.combat.character.powers[Power::Strength], 0);
  EXPECT_EQ(run.combat.character.energy_per_turn, IRONCLAD_ENERGY_PER_TURN);
  for (const Enemy& e : run.combat.enemies) EXPECT_EQ(e.hp, e.max_hp);
}

// ============================================================================
// Its reward screen
// ============================================================================

// No room gold (an EventRoom is not a MonsterRoom): only the event's reward.
TEST(EventFightRewards, PayOnlyTheEventsGold) {
  RunState run = event_fight(40, {});
  win(run);
  EXPECT_EQ(run.gold, 140);
  EXPECT_EQ(run.phase, Phase::Reward);
}

// incrementGold re-applies the bonus to the merged total: 56 pays 14 more.
// Rounded per part, 26 and 30 would have paid 7 and 8.
TEST(EventFightRewards, GoldenIdolRoundsTheWholeReward) {
  RunState run = event_fight(56, {}, {RelicId::GoldenIdol});
  win(run);
  EXPECT_EQ(run.gold, 100 + 56 + 14);
}

TEST(EventFightRewards, TheEventsRelicsAreObtained) {
  RunState run = event_fight(10, {RelicId::OddMushroom});
  win(run);
  EXPECT_TRUE(run.has_relic(RelicId::OddMushroom));
}

TEST(EventFightRewards, ACardRewardAtTheNormalOdds) {
  RunState run = event_fight(10, {});
  win(run);
  EXPECT_EQ(static_cast<int>(run.card_reward.size()), kCardRewardSize);
  for (int roll = -45; roll <= 145; ++roll) {
    EXPECT_EQ(card_rarity_for_roll(roll, RewardSource::Event),
              card_rarity_for_roll(roll, RewardSource::Monster))
        << roll;
  }
}

// Prayer Wheel tests instanceof MonsterRoom.
TEST(EventFightRewards, PrayerWheelAddsNoSecondScreen) {
  RunState run = event_fight(10, {}, {RelicId::PrayerWheel});
  win(run);
  EXPECT_EQ(run.pending_extra_card_rewards, 0);
}

// The potion roll counts the event's gold and relics, which were on the list
// first: four rewards stop even White Beast Statue's certain drop.
TEST(EventFightRewards, TheEventsRewardsCountTowardTheFourRewardCap) {
  RunState three = event_fight(
      10, {RelicId::Anchor, RelicId::Vajra}, {RelicId::WhiteBeastStatue});
  win(three);
  EXPECT_EQ(three.potions.size(), 1u);

  RunState four = event_fight(
      10, {RelicId::Anchor, RelicId::Vajra, RelicId::BagOfMarbles},
      {RelicId::WhiteBeastStatue});
  win(four);
  EXPECT_TRUE(four.potions.empty());
}

// openCombat(smoked) never sets up the room's rewards.
TEST(EventFightRewards, ASmokeBombEscapeLosesThem) {
  RunState run = event_fight(40, {RelicId::OddMushroom});
  run.combat.outcome = Outcome::Escaped;
  run.end_combat();
  EXPECT_EQ(run.gold, 100);
  EXPECT_FALSE(run.has_relic(RelicId::OddMushroom));
  EXPECT_EQ(run.phase, Phase::Map);
}

TEST(EventFightRewards, LeaveTheRunStateWhateverTheResult) {
  RunState won = event_fight(40, {RelicId::OddMushroom});
  win(won);
  EXPECT_EQ(won.event_fight_gold, 0);
  EXPECT_TRUE(won.event_fight_relics.empty());

  RunState lost = event_fight(40, {RelicId::OddMushroom});
  lost.combat.character.hp = 0;
  lost.end_combat();
  ASSERT_EQ(lost.outcome, Outcome::Lost);
  EXPECT_EQ(lost.event_fight_gold, 0);
  EXPECT_TRUE(lost.event_fight_relics.empty());
}

TEST(EventFightRewards, ProceedLeavesTheRoom) {
  RunState run = event_fight(10, {});
  win(run);
  run.skip_card_reward();
  EXPECT_EQ(run.phase, Phase::Map);
}

// ============================================================================
// Dead Adventurer
// ============================================================================

TEST(DeadAdventurer, TheChanceRisesByTwentyFivePerReward) {
  EXPECT_EQ(dead_adventurer_chance(0), 25);
  EXPECT_EQ(dead_adventurer_chance(1), 50);
  EXPECT_EQ(dead_adventurer_chance(2), 75);
}

// Whatever the shuffle, the three rewards are each there once.
TEST(DeadAdventurer, EntryShufflesTheThreeRewards) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    RunState run = event_run(seed);
    run.begin_event(EventId::DeadAdventurer);
    std::array<R, 3> sorted = run.event.rewards;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted, (std::array<R, 3>{R::Gold, R::Nothing, R::Relic}))
        << seed;
    EXPECT_EQ(run.event_options(),
              (std::vector<EventOption>{EventOption::DeadAdventurerSearch,
                                        EventOption::DeadAdventurerLeave}));
  }
}

// The shuffle, then the elite, on the floor's event stream.
TEST(DeadAdventurer, EntryRollsTheOrderThenTheElite) {
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    RunState run = event_run(seed);
    std::mt19937 rng = run.event_rng_for_floor();
    std::array<R, 3> order = {R::Gold, R::Nothing, R::Relic};
    std::shuffle(order.begin(), order.end(), rng);
    const int elite = std::uniform_int_distribution<int>(0, 2)(rng);
    run.begin_event(EventId::DeadAdventurer);
    EXPECT_EQ(run.event.rewards, order) << seed;
    EXPECT_EQ(static_cast<int>(run.event.elite), elite) << seed;
  }
}

// `random(0, 99) < chance`: at the first chance of 25, 24 fights and 25
// searches. Literal rolls, so the test does not read the function it checks.
TEST(DeadAdventurer, ARollBelowTheChanceFights) {
  RunState fight = dead_adventurer({R::Gold, R::Nothing, R::Relic});
  fight.search_dead_adventurer(24);
  EXPECT_EQ(fight.phase, Phase::Combat);

  RunState search = dead_adventurer({R::Gold, R::Nothing, R::Relic});
  search.search_dead_adventurer(25);
  EXPECT_EQ(search.phase, Phase::Event);
  EXPECT_EQ(search.event.attempts, 1);
}

TEST(DeadAdventurer, EachEliteIsItsOwnFight) {
  struct Case {
    DeadAdventurerElite elite;
    EnemyKind kind;
    size_t count;
  };
  for (const Case& c :
       {Case{DeadAdventurerElite::ThreeSentries, EnemyKind::Sentry, 3},
        Case{DeadAdventurerElite::GremlinNob, EnemyKind::GremlinNob, 1},
        Case{DeadAdventurerElite::Lagavulin, EnemyKind::Lagavulin, 1}}) {
    RunState run = dead_adventurer({R::Gold, R::Nothing, R::Relic}, c.elite);
    run.search_dead_adventurer(0);
    ASSERT_EQ(run.combat.enemies.size(), c.count);
    for (const Enemy& e : run.combat.enemies) EXPECT_EQ(e.kind, c.kind);
    EXPECT_TRUE(run.combat.is_elite);
  }
}

TEST(DeadAdventurer, ItsLagavulinIsAwake) {
  RunState run = dead_adventurer({R::Gold, R::Nothing, R::Relic},
                                 DeadAdventurerElite::Lagavulin);
  run.search_dead_adventurer(0);
  EXPECT_FALSE(run.combat.enemies[0].is_asleep);
  EXPECT_EQ(run.combat.enemies[0].last_move, MoveName::SiphonSoul);
}

// Found in order: 30 gold through gainGold (no Golden Idol), nothing, a relic;
// then the event ends.
TEST(DeadAdventurer, EachSearchFindsTheNextReward) {
  RunState run = dead_adventurer({R::Gold, R::Nothing, R::Relic});
  run.obtain_relic(RelicId::GoldenIdol);
  const size_t relics = run.relics.size();
  run.search_dead_adventurer(99);
  EXPECT_EQ(run.gold, 100 + kDeadAdventurerGold);
  run.search_dead_adventurer(99);
  EXPECT_EQ(run.gold, 100 + kDeadAdventurerGold);
  EXPECT_EQ(run.relics.size(), relics);
  ASSERT_EQ(run.phase, Phase::Event);
  run.search_dead_adventurer(99);
  EXPECT_EQ(run.relics.size(), relics + 1);
  EXPECT_EQ(run.phase, Phase::Map);
}

// The chance after each reward found: 49 searches at 25 and fights at 50.
// Literal rolls, so a chance that never rose could not move them with it.
TEST(DeadAdventurer, EachFindRaisesTheChance) {
  RunState run = dead_adventurer({R::Nothing, R::Gold, R::Relic});
  run.search_dead_adventurer(49);
  ASSERT_EQ(run.phase, Phase::Event);
  run.search_dead_adventurer(49);
  EXPECT_EQ(run.phase, Phase::Combat);
}

// The fight's screen: misc 25-35 gold, plus 30 if the gold was not found, plus
// a relic if the relic was not.
TEST(DeadAdventurer, TheFightPaysWhatWasNotFound) {
  RunState none_found = dead_adventurer({R::Gold, R::Nothing, R::Relic});
  none_found.search_dead_adventurer(0);
  EXPECT_GE(none_found.event_fight_gold,
            kDeadAdventurerFightGoldMin + kDeadAdventurerGold);
  EXPECT_LE(none_found.event_fight_gold,
            kDeadAdventurerFightGoldMax + kDeadAdventurerGold);
  EXPECT_EQ(none_found.event_fight_relics.size(), 1u);

  RunState gold_found = dead_adventurer({R::Gold, R::Nothing, R::Relic});
  gold_found.search_dead_adventurer(99);
  gold_found.search_dead_adventurer(0);
  EXPECT_GE(gold_found.event_fight_gold, kDeadAdventurerFightGoldMin);
  EXPECT_LE(gold_found.event_fight_gold, kDeadAdventurerFightGoldMax);
  EXPECT_EQ(gold_found.event_fight_relics.size(), 1u);

  RunState relic_found = dead_adventurer({R::Relic, R::Nothing, R::Gold});
  relic_found.search_dead_adventurer(99);
  relic_found.search_dead_adventurer(0);
  EXPECT_TRUE(relic_found.event_fight_relics.empty());
  EXPECT_GE(relic_found.event_fight_gold,
            kDeadAdventurerFightGoldMin + kDeadAdventurerGold);
}

TEST(DeadAdventurer, LeaveEndsIt) {
  RunState run = dead_adventurer({R::Gold, R::Nothing, R::Relic});
  ASSERT_TRUE(run.choose_event_option(EventOption::DeadAdventurerLeave));
  EXPECT_EQ(run.phase, Phase::Map);
  EXPECT_EQ(run.gold, 100);
}

// ============================================================================
// Hypnotizing Colored Mushrooms
// ============================================================================

TEST(Mushrooms, StompFightsTheLairForGoldAndOddMushroom) {
  RunState run = event_run();
  run.begin_event(EventId::Mushrooms);
  ASSERT_TRUE(run.choose_event_option(EventOption::MushroomsStomp));
  EXPECT_EQ(run.phase, Phase::Combat);
  EXPECT_EQ(kinds(run.combat.enemies), kMushroomLair);
  EXPECT_FALSE(run.combat.is_elite);
  EXPECT_GE(run.event_fight_gold, kMushroomsFightGoldMin);
  EXPECT_LE(run.event_fight_gold, kMushroomsFightGoldMax);
  EXPECT_EQ(run.event_fight_relics,
            std::vector<RelicId>{RelicId::OddMushroom});

  win(run);
  EXPECT_TRUE(run.has_relic(RelicId::OddMushroom));
}

TEST(Mushrooms, EatHealsAQuarterAndGivesParasite) {
  RunState run = event_run();
  run.hp = 30;
  run.begin_event(EventId::Mushrooms);
  ASSERT_TRUE(run.choose_event_option(EventOption::MushroomsEat));
  EXPECT_EQ(run.hp, 30 + IRONCLAD_MAX_HP / 4);
  EXPECT_TRUE(std::any_of(run.master_deck.begin(), run.master_deck.end(),
                          [](const Card& c) {
                            return c.card_id == CardId::Parasite;
                          }));
  EXPECT_EQ(run.phase, Phase::Map);
}

TEST(Mushrooms, OmamoriStopsTheParasite) {
  RunState run = event_run();
  run.obtain_relic(RelicId::Omamori);
  run.begin_event(EventId::Mushrooms);
  ASSERT_TRUE(run.choose_event_option(EventOption::MushroomsEat));
  EXPECT_FALSE(std::any_of(run.master_deck.begin(), run.master_deck.end(),
                           [](const Card& c) {
                             return c.card_id == CardId::Parasite;
                           }));
}

// ============================================================================
// Odd Mushroom
// ============================================================================

TEST(OddMushroom, LowersTheVulnerableMultiplierOnThePlayer) {
  CombatState bare = start_v1_combat(0);
  EXPECT_FLOAT_EQ(player_vulnerable_multiplier(bare), 1.5f);
  bare.relics.push_back(HeldRelic{RelicId::OddMushroom, 0});
  EXPECT_FLOAT_EQ(player_vulnerable_multiplier(bare), 1.25f);
}

// 10 into a Vulnerable player: 15 normally, 12 with Odd Mushroom (12.5).
TEST(OddMushroom, AnEnemyAttackOnAVulnerablePlayerHitsForAQuarterMore) {
  for (bool held : {false, true}) {
    CombatState s = start_v1_combat(0);
    if (held) s.relics.push_back(HeldRelic{RelicId::OddMushroom, 0});
    s.character.debuffs[Debuff::Vulnerable] = 1;
    s.character.current_block = 0;
    const int before = s.character.hp;
    ActionQueue q;
    ResolutionContext ctx;
    Action a{ActionKind::DealDamage};
    a.actor = 0;
    a.target = kPlayerSlot;
    a.amount = 10;
    q.push_back(a);
    drain(s, q, ctx);
    EXPECT_EQ(before - s.character.hp, held ? 12 : 15) << held;
  }
}

// The intent a human reads already has the multiplier in it: Chomp's 11 shows
// as 13 (13.75), not 16.
TEST(OddMushroom, TheIntentShowsTheLowerDamage) {
  constexpr int kEnemy0Base = CombatEnv::kPlayerObsSize;
  constexpr int kIntentOff = 3 + CombatEnv::kEnemyStatusSize;
  for (bool held : {false, true}) {
    CombatState s = start_v1_combat(0);
    if (held) s.relics.push_back(HeldRelic{RelicId::OddMushroom, 0});
    s.character.debuffs[Debuff::Vulnerable] = 1;
    CombatEnv env(s);
    EXPECT_FLOAT_EQ(env.obs()[kEnemy0Base + kIntentOff + 1],
                    held ? 13.0f : 16.0f)
        << held;
  }
}
