// Path choice and `?` room resolution (docs/design/v2-spec.md §4.1, §5.4).

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "run_state.h"

namespace minispire {
namespace {

// Steps the run out of whatever room it just entered, using each room's REAL
// exit. Returns false if the run ended.
//
// The single place these walks are allowed to touch `phase` directly, and it is
// deliberately narrow. An earlier version forced Phase::Map for every non-combat
// room, which made a room that never exits indistinguishable from one that does
// — and that hid a real bug: treasure rooms stranded the run on floor 9, while
// the suite stayed green because the harness stepped over it every time.
//
// A test that needs to reach past the system under test is reporting a defect,
// not asking for a workaround. So each room here goes out the way the engine
// takes it out, and the one remaining patch is pinned to the one room that has
// no exit yet.
bool exit_current_room(RunState& run) {
  switch (run.phase) {
    case Phase::Combat:
      run.combat.character.hp = std::max(1, run.combat.character.hp - 2);
      run.end_combat();
      if (run.is_terminal()) return false;
      run.skip_card_reward();
      return true;

    case Phase::Rest:
      run.rest_heal();
      return true;

    case Phase::Shop:
      run.leave_shop();
      return true;

    case Phase::Treasure:
      // Unreachable: a chest opens and leaves in one step.
      ADD_FAILURE() << "a treasure room did not exit on its own (floor "
                    << run.floor << ")";
      run.phase = Phase::Map;
      return true;

    case Phase::Reward:
      // Lab searches on entry, so a full belt opens its reward screen before
      // any option is taken: Proceed.
      run.skip_card_reward();
      return true;

    case Phase::Map:
      // Already back on the map: the room exited on its own. A treasure chest
      // takes this path, which is precisely the behaviour the Treasure case
      // above asserts can no longer be needed.
      return true;

    case Phase::Event:
      // Take the last option on offer until the event ends: Leave where there
      // is one, and every event's last screen advances (events.md §3.2). An
      // event left with nothing to choose has stranded the run.
      while (run.phase == Phase::Event && !run.is_terminal()) {
        // An event's deck choice is answered with its first candidate.
        if (run.has_pending_deck_choice()) {
          run.resolve_deck_choice(run.deck_choice_candidates().front());
          continue;
        }
        const std::vector<EventOption> options = run.event_options();
        if (options.empty()) {
          ADD_FAILURE() << "an event offered nothing and did not end (floor "
                        << run.floor << ")";
          run.phase = Phase::Map;
          break;
        }
        run.choose_event_option(options.back());
      }
      // Lab's and The Woman in Blue's potions can wait on a reward screen:
      // Proceed.
      if (run.phase == Phase::Reward) run.skip_card_reward();
      return !run.is_terminal();

    default:
      if (run.floor >= run.final_floor) {
        run.skip_card_reward();  // reuses the "floor cleared" transition
        return true;
      }
      ADD_FAILURE() << "phase " << static_cast<int>(run.phase)
                    << " has no exit path (floor " << run.floor << ")";
      run.phase = Phase::Map;
      return true;
  }
}

// Walks a run to the top of the map, resolving whatever each room needs.
// Returns the floors visited.
int walk_to_the_top(RunState& run) {
  int floors = 0;
  while (!run.is_terminal()) {
    const std::vector<int> options = run.available_paths();
    if (options.empty()) break;
    run.choose_path(options[0]);
    ++floors;
    if (!exit_current_room(run)) break;
  }
  return floors;
}

// A hand-built map: rest sites in columns 1, 3 and 5 on every row, each with a
// single edge straight up. From any room the only CONNECTED move is straight
// ahead, so a sideways move can only be a Wing Boots flight. Built rather than
// generated: which nodes a generated map connects depends on the map RNG.
// Rest sites, so entering a room starts no fight.
void use_ladder_map(RunState& run) {
  for (auto& row : run.map) {
    for (MapNode& node : row) {
      node.room = RoomType::None;
      node.edges.clear();
      node.parents.clear();
    }
  }
  for (int y = 0; y < kMapHeight; ++y) {
    for (int x : {1, 3, 5}) {
      MapNode& node = run.map[y][x];
      node.room = RoomType::Rest;
      if (y + 1 < kMapHeight) {
        node.edges.insert(MapEdge{x, y, x, y + 1});
        run.map[y + 1][x].parents.push_back(Point{x, y});
      }
    }
  }
}

// ------------------------------------------------------------ available paths

TEST(PathChoice, FromNeowEveryFirstRowRoomIsAnOpening) {
  RunState run = RunState::start(1);
  const std::vector<int> options = run.available_paths();

  ASSERT_FALSE(options.empty());
  for (int x : options) EXPECT_TRUE(run.map[0][x].is_room());

  int rooms = 0;
  for (int x = 0; x < kMapWidth; ++x) {
    if (run.map[0][x].is_room()) ++rooms;
  }
  EXPECT_EQ(options.size(), static_cast<size_t>(rooms));
}

TEST(PathChoice, FromANodeTheOptionsAreItsEdges) {
  RunState run = RunState::start(1);
  const int first = run.available_paths()[0];
  run.choose_path(first);

  const std::vector<int> options = run.available_paths();
  std::set<int> expected;
  for (const MapEdge& e : run.map[run.floor - 1][run.column].edges) {
    expected.insert(e.dst_x);
  }
  EXPECT_EQ(options.size(), expected.size());
  for (int x : options) EXPECT_EQ(expected.count(x), 1u);
}

TEST(PathChoice, MovingSetsFloorAndColumn) {
  RunState run = RunState::start(1);
  const int first = run.available_paths()[0];
  run.choose_path(first);

  EXPECT_EQ(run.floor, 1);
  EXPECT_EQ(run.column, first);
}

// The mask should prevent this; the engine must not move if it doesn't.
TEST(PathChoice, AnUnreachableColumnIsIgnored) {
  RunState run = RunState::start(1);
  const std::vector<int> options = run.available_paths();

  int unreachable = -1;
  for (int x = 0; x < kMapWidth; ++x) {
    if (std::find(options.begin(), options.end(), x) == options.end()) {
      unreachable = x;
      break;
    }
  }
  ASSERT_NE(unreachable, -1);

  run.choose_path(unreachable);
  EXPECT_EQ(run.floor, 0) << "moved to a column with no path to it";
}

// ------------------------------------------------------- room dispatch

// Floor 1 is always an easy fight, so entering it must start combat.
TEST(PathChoice, FirstFloorAlwaysStartsAFight) {
  for (uint64_t s = 0; s < 40; ++s) {
    RunState run = RunState::start(s);
    run.choose_path(run.available_paths()[0]);
    EXPECT_EQ(run.phase, Phase::Combat) << "seed " << s;
    EXPECT_TRUE(run.in_combat);
  }
}

TEST(PathChoice, EliteRoomsUseTheElitePool) {
  // Find a seed and path that reaches an elite, then check the reward source.
  bool checked = false;
  for (uint64_t s = 0; s < 60 && !checked; ++s) {
    RunState run = RunState::start(s);
    while (!run.is_terminal() && !checked) {
      const std::vector<int> options = run.available_paths();
      if (options.empty()) break;
      int pick = options[0];
      for (int x : options) {
        if (run.map[run.floor][x].room == RoomType::Elite) pick = x;
      }
      const bool expecting_elite =
          run.map[run.floor][pick].room == RoomType::Elite;
      run.choose_path(pick);
      if (expecting_elite) {
        EXPECT_EQ(run.combat_source, RewardSource::Elite);
        checked = true;
        break;
      }
      if (!exit_current_room(run)) break;
    }
  }
  EXPECT_TRUE(checked) << "never reached an elite in 60 seeds";
}

// ----------------------------------------------------- `?` resolution (§4.1)

// The map is never rewritten. A `?` stays `?` on the map; what it became lives
// in current_room, which is what the phase is derived from (§5.4).
TEST(UnknownRooms, TheMapIsNeverRewrittenWhenAQuestionResolves) {
  for (uint64_t s = 0; s < 60; ++s) {
    RunState run = RunState::start(s);
    while (!run.is_terminal()) {
      const std::vector<int> options = run.available_paths();
      if (options.empty()) break;
      int pick = options[0];
      for (int x : options) {
        if (run.map[run.floor][x].room == RoomType::Unknown) pick = x;
      }
      const bool was_unknown =
          run.map[run.floor][pick].room == RoomType::Unknown;
      run.choose_path(pick);

      if (was_unknown) {
        EXPECT_EQ(run.map[run.floor - 1][run.column].room, RoomType::Unknown)
            << "the map recorded what a ? resolved into";
      }
      if (!exit_current_room(run)) break;
    }
  }
}

TEST(UnknownRooms, NotSeeingAMonsterMakesOneLikelierNextTime) {
  RunState run = RunState::start(1);
  const float before = run.monster_chance;
  // Force a non-monster outcome by making the monster band empty.
  run.monster_chance = 0.0f;
  run.resolve_unknown_room();
  EXPECT_GT(run.monster_chance, 0.0f) << "the pity counter did not rise";
  EXPECT_GT(run.monster_chance + before, before);
}

// A `?` cannot become a shop when the previous room was one.
TEST(UnknownRooms, AShopCannotFollowAShop) {
  RunState run = RunState::start(1);
  run.last_room_was_shop = true;
  // Make shop the overwhelmingly likely band if it were not suppressed.
  run.shop_chance = 0.95f;
  run.monster_chance = 0.0f;
  run.treasure_chance = 0.0f;

  for (int i = 0; i < 50; ++i) {
    run.floor = i;
    run.shop_chance = 0.95f;
    run.monster_chance = 0.0f;
    run.treasure_chance = 0.0f;
    EXPECT_NE(run.resolve_unknown_room(), RoomType::Shop)
        << "a shop followed a shop";
  }
}

// ------------------------------------------------------------- Wing Boots

int wing_boots_charges(const RunState& run) {
  for (const HeldRelic& r : run.relics) {
    if (r.id == RelicId::WingBoots) return r.counter;
  }
  return 0;
}

// Standing on row 0, column 1 of the ladder: only column 1 is connected.
RunState on_the_ladder(bool with_boots) {
  RunState run = RunState::start(1);
  use_ladder_map(run);
  if (with_boots) run.obtain_relic(RelicId::WingBoots);
  run.floor = 1;
  run.column = 1;
  run.phase = Phase::Map;
  return run;
}

TEST(WingBoots, StartsWithThreeCharges) {
  EXPECT_EQ(wing_boots_charges(on_the_ladder(true)), kWingBootsCharges);
}

TEST(WingBoots, OpensEveryRoomOnTheNextRow) {
  EXPECT_EQ(on_the_ladder(false).available_paths(), std::vector<int>{1});
  EXPECT_EQ(on_the_ladder(true).available_paths(), (std::vector<int>{1, 3, 5}));
}

// A connected move costs nothing, even with charges to spare.
TEST(WingBoots, AConnectedMoveSpendsNoCharge) {
  RunState run = on_the_ladder(true);
  run.choose_path(1);
  EXPECT_EQ(run.column, 1);
  EXPECT_EQ(wing_boots_charges(run), kWingBootsCharges);
}

TEST(WingBoots, AFlightSpendsOneCharge) {
  RunState run = on_the_ladder(true);
  run.choose_path(3);
  EXPECT_EQ(run.floor, 2);
  EXPECT_EQ(run.column, 3);
  EXPECT_EQ(wing_boots_charges(run), kWingBootsCharges - 1);
}

// After three flights the boots are spent: -2, and only edges remain.
TEST(WingBoots, ThreeFlightsUseThemUp) {
  RunState run = on_the_ladder(true);
  for (int target : {3, 5, 3}) run.choose_path(target);
  EXPECT_EQ(run.column, 3);
  EXPECT_EQ(wing_boots_charges(run), kWingBootsUsedUp);
  EXPECT_EQ(run.available_paths(), std::vector<int>{3});
}

// From floor 0 every first-row room is already open; nothing is spent there.
TEST(WingBoots, TheFirstRowSpendsNothing) {
  RunState run = on_the_ladder(true);
  run.floor = 0;
  run.choose_path(5);
  EXPECT_EQ(wing_boots_charges(run), kWingBootsCharges);
}

// Event is the FALLBACK: a roll that lands in no band is an event. Every band
// emptied, so no roll can land in one — no RNG dependence.
TEST(UnknownRooms, ARollOutsideEveryBandIsAnEvent) {
  RunState run = RunState::start(1);
  run.floor = 3;
  run.monster_chance = 0.0f;
  run.shop_chance = 0.0f;
  run.treasure_chance = 0.0f;

  EXPECT_EQ(run.resolve_unknown_room(), RoomType::Unknown);
}

// ------------------------------------------- relics that rewrite a `?` roll

// Makes every possible roll land in the monster band, so the result does not
// depend on what the Event stream draws. 1.01 rather than 1.0: the roll is
// `uniform_real_distribution<float> * 100`, and a float distribution is allowed
// to return exactly 1.0 on some standard libraries, which would read as 100 and
// fall through to Event. The band must cover 100 as well as 0..99.
void stack_the_monster_band(RunState& run) {
  run.monster_chance = 1.01f;
  run.shop_chance = 0.0f;
  run.treasure_chance = 0.0f;
}

// Juzu swaps the ROOM, not the roll: the monster chance still resets, exactly as
// if a fight had happened (EventHelper.roll resets MONSTER_CHANCE inside the
// same branch that swaps the choice). Resolving the swap before drift would
// raise it instead, and that is the ordering this pins.
TEST(UnknownRooms, JuzuBraceletTurnsAMonsterIntoAnEventAndStillResetsTheChance) {
  RunState run = RunState::start(1);
  run.relics.push_back(HeldRelic{RelicId::JuzuBracelet, 0});
  run.floor = 3;
  stack_the_monster_band(run);

  EXPECT_EQ(run.resolve_unknown_room(), RoomType::Unknown)
      << "a monster roll was not converted to an event";
  EXPECT_FLOAT_EQ(run.monster_chance, 0.10f)
      << "the monster chance drifted as if no monster had been rolled";
}

TEST(UnknownRooms, WithoutJuzuBraceletTheSameRollIsAFight) {
  // The control for the test above: the stacked band really does produce a
  // monster, so the Juzu test is not passing on a roll that was never one.
  RunState run = RunState::start(1);
  run.floor = 3;
  stack_the_monster_band(run);

  EXPECT_EQ(run.resolve_unknown_room(), RoomType::Monster);
}

TEST(UnknownRooms, TinyChestCountsEveryQuestionRoom) {
  RunState run = RunState::start(1);
  run.relics.push_back(HeldRelic{RelicId::TinyChest, 0});

  for (int expected = 1; expected < kTinyChestRooms; ++expected) {
    run.floor = expected;
    stack_the_monster_band(run);
    EXPECT_EQ(run.resolve_unknown_room(), RoomType::Monster)
        << "Tiny Chest forced a chest before its " << kTinyChestRooms
        << "th room";
    EXPECT_EQ(run.held_relic(RelicId::TinyChest)->counter, expected);
  }
}

// The fourth `?` is a chest whatever was rolled, the counter wraps, and the
// forced chest drifts as a Treasure result: treasure resets, monster rises.
TEST(UnknownRooms, TinyChestForcesTheFourthQuestionRoomToTreasure) {
  RunState run = RunState::start(1);
  run.relics.push_back(HeldRelic{RelicId::TinyChest, kTinyChestRooms - 1});
  run.floor = 5;
  stack_the_monster_band(run);
  run.treasure_chance = 0.30f;  // set after stacking: a reset must be visible

  EXPECT_EQ(run.resolve_unknown_room(), RoomType::Treasure);
  EXPECT_EQ(run.held_relic(RelicId::TinyChest)->counter, 0)
      << "the counter did not wrap";
  EXPECT_FLOAT_EQ(run.treasure_chance, 0.02f)
      << "the forced chest did not reset the treasure chance";
  EXPECT_GT(run.monster_chance, 1.01f)
      << "the overridden monster roll reset the monster chance";
}

// Juzu replaces MONSTERS only. A chest Tiny Chest forces is not one, so holding
// both still gives a chest on the fourth `?`.
TEST(UnknownRooms, JuzuBraceletLeavesTinyChestsChestAlone) {
  RunState run = RunState::start(1);
  run.relics.push_back(HeldRelic{RelicId::JuzuBracelet, 0});
  run.relics.push_back(HeldRelic{RelicId::TinyChest, kTinyChestRooms - 1});
  run.floor = 5;
  stack_the_monster_band(run);

  EXPECT_EQ(run.resolve_unknown_room(), RoomType::Treasure);
}

// ------------------------------------------------------------- a whole run

TEST(PathChoice, ARunCanBeWalkedFromNeowToTheTop) {
  RunState run = RunState::start(2024);
  const int floors = walk_to_the_top(run);

  EXPECT_EQ(run.outcome, Outcome::Won);
  EXPECT_EQ(floors, kMapHeight) << "did not walk every floor";
  EXPECT_EQ(run.floor, kMapHeight);
}

TEST(PathChoice, EveryStepOfAWalkedRunWasOnARealEdge) {
  RunState run = RunState::start(77);
  int previous_floor = 0;
  int previous_column = -1;

  while (!run.is_terminal()) {
    const std::vector<int> options = run.available_paths();
    if (options.empty()) break;
    const int pick = options[0];

    if (previous_column >= 0) {
      const auto& edges = run.map[previous_floor - 1][previous_column].edges;
      bool found = false;
      for (const MapEdge& e : edges) found = found || e.dst_x == pick;
      EXPECT_TRUE(found) << "stepped to a column with no edge to it";
    }

    run.choose_path(pick);
    previous_floor = run.floor;
    previous_column = run.column;

    if (!exit_current_room(run)) break;
  }
}

TEST(PathChoice, AWalkedRunIsReproducible) {
  auto play = [](uint64_t seed) {
    RunState run = RunState::start(seed);
    std::vector<int> trace;
    while (!run.is_terminal()) {
      const std::vector<int> options = run.available_paths();
      if (options.empty()) break;
      run.choose_path(options[0]);
      trace.push_back(run.column);
      trace.push_back(static_cast<int>(run.current_room));
      if (!exit_current_room(run)) break;
    }
    return trace;
  };

  EXPECT_EQ(play(4242), play(4242));
  EXPECT_NE(play(4242), play(4243));
}

}  // namespace
}  // namespace minispire
