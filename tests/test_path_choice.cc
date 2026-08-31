// Path choice and `?` room resolution (docs/design/v2-spec.md §4.1, §5.4).

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "run_state.h"

namespace minispire {
namespace {

// Walks a run to the top of the map, resolving whatever each room needs.
// Returns the floors visited.
int walk_to_the_top(RunState& run) {
  int floors = 0;
  while (!run.is_terminal()) {
    const std::vector<int> options = run.available_paths();
    if (options.empty()) break;
    run.choose_path(options[0]);
    ++floors;

    if (run.phase == Phase::Combat) {
      run.combat.character.hp = std::max(1, run.combat.character.hp - 2);
      run.end_combat();
      if (run.is_terminal()) break;
      run.skip_card_reward();
    } else {
      // Non-combat rooms have no interaction yet; step straight back to the map.
      if (run.floor >= run.final_floor) {
        run.skip_card_reward();  // reuses the "floor cleared" transition
      } else {
        run.phase = Phase::Map;
      }
    }
  }
  return floors;
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
      if (run.phase == Phase::Combat) {
        run.combat.character.hp = std::max(1, run.combat.character.hp - 2);
        run.end_combat();
        if (run.is_terminal()) break;
        run.skip_card_reward();
      } else {
        run.phase = Phase::Map;
      }
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
      if (run.phase == Phase::Combat) {
        run.combat.character.hp = std::max(1, run.combat.character.hp - 2);
        run.end_combat();
        if (run.is_terminal()) break;
        run.skip_card_reward();
      } else {
        run.phase = Phase::Map;
      }
    }
  }
}

// Event is the FALLBACK, so it is the common outcome — not a rare roll that
// drifts upward. An implementation with the sign inverted would show a few
// percent here instead of most of the mass.
TEST(UnknownRooms, EventIsTheCommonOutcomeOfAFreshQuestionRoom) {
  std::map<RoomType, int> counts;
  const int kTrials = 400;
  for (uint64_t s = 0; s < kTrials; ++s) {
    RunState run = RunState::start(s);
    run.floor = 1;  // a fresh run's counters, one ? room
    counts[run.resolve_unknown_room()]++;
  }
  const double event_share = 100.0 * counts[RoomType::Unknown] / kTrials;
  EXPECT_GT(event_share, 70.0)
      << "events are not the fallback — is the distribution inverted?";
  EXPECT_LT(100.0 * counts[RoomType::Monster] / kTrials, 25.0);
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

    if (run.phase == Phase::Combat) {
      run.combat.character.hp = std::max(1, run.combat.character.hp - 2);
      run.end_combat();
      if (run.is_terminal()) break;
      run.skip_card_reward();
    } else if (run.floor >= run.final_floor) {
      run.skip_card_reward();
    } else {
      run.phase = Phase::Map;
    }
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
      if (run.phase == Phase::Combat) {
        run.combat.character.hp = std::max(1, run.combat.character.hp - 2);
        run.end_combat();
        if (run.is_terminal()) break;
        run.skip_card_reward();
      } else if (run.floor >= run.final_floor) {
        run.skip_card_reward();
      } else {
        run.phase = Phase::Map;
      }
    }
    return trace;
  };

  EXPECT_EQ(play(4242), play(4242));
  EXPECT_NE(play(4242), play(4243));
}

}  // namespace
}  // namespace minispire
