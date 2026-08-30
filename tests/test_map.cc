// Tests for Act 1 map generation (docs/design/v2-spec.md §4.1).
//
// A 250-line algorithm port cannot be validated by inspection, so these assert
// the structural invariants the generator is supposed to guarantee, across many
// seeds. Where the source does something that looks wrong, there is a test
// pinning the odd behaviour so nobody "fixes" it.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "map.h"

namespace minispire {
namespace {

constexpr int kSeeds = 300;

std::vector<const MapNode*> path_nodes(const Map& map) {
  std::vector<const MapNode*> out;
  for (const auto& row : map) {
    for (const MapNode& n : row) {
      if (n.is_room()) out.push_back(&n);
    }
  }
  return out;
}

// ------------------------------------------------------------------ RNG port

TEST(StsRandom, IsDeterministic) {
  StsRandom a(12345), b(12345);
  for (int i = 0; i < 32; ++i) EXPECT_EQ(a.next_u64(), b.next_u64());
}

TEST(StsRandom, DifferentSeedsDiverge) {
  StsRandom a(1), b(2);
  bool differs = false;
  for (int i = 0; i < 32 && !differs; ++i) differs = a.next_u64() != b.next_u64();
  EXPECT_TRUE(differs);
}

TEST(StsRandom, RandRangeStaysInBounds) {
  StsRandom rng(7);
  for (int i = 0; i < 5000; ++i) {
    const int v = rng.rand_range(-1, 1);
    EXPECT_GE(v, -1);
    EXPECT_LE(v, 1);
  }
}

// A zero seed would collapse xorshift, so the source substitutes INT64_MIN.
TEST(StsRandom, ZeroSeedDoesNotCollapse) {
  StsRandom rng(0);
  std::set<uint64_t> seen;
  for (int i = 0; i < 16; ++i) seen.insert(rng.next_u64());
  EXPECT_GT(seen.size(), 1u);
}

// --------------------------------------------------------------- determinism

TEST(MapGeneration, SameSeedGivesTheSameMap) {
  EXPECT_EQ(format_map(generate_map(999)), format_map(generate_map(999)));
}

TEST(MapGeneration, DifferentSeedsGiveDifferentMaps) {
  std::set<std::string> maps;
  for (uint64_t s = 0; s < 50; ++s) maps.insert(format_map(generate_map(s)));
  EXPECT_GT(maps.size(), 40u) << "seeds are collapsing onto the same map";
}

// --------------------------------------------------------------- shape

TEST(MapGeneration, GridIsFifteenBySeven) {
  const Map map = generate_map(1);
  ASSERT_EQ(map.size(), static_cast<size_t>(kMapHeight));
  for (const auto& row : map) ASSERT_EQ(row.size(), static_cast<size_t>(kMapWidth));
}

// path_density is the number of paths carved, NOT the row width — a floor holds
// at most that many rooms, which is the distinction §4.1 calls out.
TEST(MapGeneration, NoRowHoldsMoreThanPathDensityRooms) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (int y = 0; y < kMapHeight; ++y) {
      int rooms = 0;
      for (int x = 0; x < kMapWidth; ++x) {
        if (map[y][x].is_room()) ++rooms;
      }
      EXPECT_LE(rooms, kPathDensity) << "seed " << s << " row " << y;
      EXPECT_GT(rooms, 0) << "seed " << s << " row " << y << " is empty";
    }
  }
}

TEST(MapGeneration, EveryPathNodeIsAssignedARoom) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (const MapNode* n : path_nodes(map)) {
      EXPECT_NE(n->room, RoomType::None)
          << "seed " << s << " left (" << n->x << "," << n->y << ") empty";
    }
  }
}

// Edges only ever reach the row above, and only columns c-1, c, c+1. This is
// what licenses the observation's 105x3 edge block instead of an adjacency
// matrix (§5.3).
TEST(MapGeneration, EdgesAreLocalAndGoUpwardOnly) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (const MapNode* n : path_nodes(map)) {
      for (const MapEdge& e : n->edges) {
        EXPECT_EQ(e.dst_y, n->y + 1) << "seed " << s;
        EXPECT_GE(e.dst_x, n->x - 1) << "seed " << s;
        EXPECT_LE(e.dst_x, n->x + 1) << "seed " << s;
        EXPECT_GE(e.dst_x, 0);
        EXPECT_LT(e.dst_x, kMapWidth);
      }
    }
  }
}

// Every node above the first row must be reachable: an edge into it exists.
TEST(MapGeneration, EveryPathNodeAboveTheFirstRowHasAParent) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (int y = 1; y < kMapHeight; ++y) {
      for (int x = 0; x < kMapWidth; ++x) {
        if (!map[y][x].on_a_path() && map[y][x].parents.empty()) continue;
        if (map[y][x].parents.empty()) {
          ADD_FAILURE() << "seed " << s << ": (" << x << "," << y
                        << ") is on a path but unreachable";
        }
      }
    }
  }
}

// Paths never cross. For any two nodes in a row, the left one's edges never
// reach further right than the right one's.
TEST(MapGeneration, PathsDoNotCross) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (int y = 0; y < kMapHeight - 1; ++y) {
      for (int x = 0; x < kMapWidth - 1; ++x) {
        const auto& left = map[y][x].edges;
        const auto& right = map[y][x + 1].edges;
        if (left.empty() || right.empty()) continue;
        EXPECT_LE(left.rbegin()->dst_x, right.begin()->dst_x)
            << "seed " << s << " crossing at row " << y;
      }
    }
  }
}

// ------------------------------------------------------ placement rules (§4.1)

TEST(MapGeneration, NoRestOrEliteInTheOpeningRows) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (int y = 0; y <= 4; ++y) {
      for (int x = 0; x < kMapWidth; ++x) {
        EXPECT_NE(map[y][x].room, RoomType::Rest) << "seed " << s << " row " << y;
        EXPECT_NE(map[y][x].room, RoomType::Elite) << "seed " << s << " row " << y;
      }
    }
  }
}

// The quota may not place a rest site on row 13 — you should not get a campfire
// immediately before the guaranteed one.
TEST(MapGeneration, NoQuotaRestImmediatelyBelowTheFixedCampfire) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (int x = 0; x < kMapWidth; ++x) {
      EXPECT_NE(map[13][x].room, RoomType::Rest) << "seed " << s;
    }
  }
}

// ------------------------------------------------------------- fixed floors

// Floor 1 is always an easy fight, floor 9 the treasure chest, floor 15 the
// guaranteed campfire before the boss. These are set across the whole row and
// bypass the quota's placement rules entirely — which is why row 14 is rest
// despite `rule_assignable_to_row` forbidding rest above row 13.
TEST(MapGeneration, FixedFloorsAreAlwaysTheSame) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (int x = 0; x < kMapWidth; ++x) {
      if (map[0][x].is_room()) {
        EXPECT_EQ(map[0][x].room, RoomType::Monster) << "seed " << s;
      }
      if (map[8][x].is_room()) {
        EXPECT_EQ(map[8][x].room, RoomType::Treasure) << "seed " << s;
      }
      if (map[kMapHeight - 1][x].is_room()) {
        EXPECT_EQ(map[kMapHeight - 1][x].room, RoomType::Rest) << "seed " << s;
      }
    }
  }
}

// The top row is reachable but has nothing above it, so it is a room by virtue
// of its parents rather than its edges. A generator that only looked at edges
// would leave the pre-boss campfire empty.
TEST(MapGeneration, TheTopRowIsPopulated) {
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    int rooms = 0;
    for (int x = 0; x < kMapWidth; ++x) {
      if (map[kMapHeight - 1][x].is_room()) ++rooms;
    }
    EXPECT_GT(rooms, 0) << "seed " << s << ": no pre-boss campfire";
  }
}

// A node may not share Rest/Treasure/Shop/Elite with a parent. Monster and
// Unknown are deliberately absent from this rule — the parent rule covers four
// types where the sibling rule covers six, and that asymmetry is the game's.
TEST(MapGeneration, RestrictedTypesNeverRepeatFromParentToChild) {
  const RoomType restricted[] = {RoomType::Rest, RoomType::Treasure,
                                 RoomType::Shop, RoomType::Elite};
  for (uint64_t s = 0; s < kSeeds; ++s) {
    const Map map = generate_map(s);
    for (const MapNode* n : path_nodes(map)) {
      for (RoomType r : restricted) {
        if (n->room != r) continue;
        for (const Point& p : n->parents) {
          // Row 0 has a documented escape hatch and may violate this.
          if (n->y == 0) continue;
          EXPECT_NE(map[p.y][p.x].room, r)
              << "seed " << s << ": (" << n->x << "," << n->y
              << ") repeats its parent's room type";
        }
      }
    }
  }
}

// ------------------------------------------------------------- the ? node

// Generation produces `Unknown` nodes and never a "resolved event" type — a ?
// stays ? on the map, and what it became is reported by the phase (§5.4).
TEST(MapGeneration, UnknownRoomsExistAndStayUnknown) {
  int total_unknown = 0;
  for (uint64_t s = 0; s < 50; ++s) {
    const Map map = generate_map(s);
    for (const MapNode* n : path_nodes(map)) {
      if (n->room == RoomType::Unknown) ++total_unknown;
    }
  }
  EXPECT_GT(total_unknown, 0) << "no ? rooms were ever generated";
}

// The room-type enum IS the observation's one-hot (§5.4), so its numbering is
// an interface.
TEST(MapGeneration, RoomTypeNumberingMatchesTheObservation) {
  EXPECT_EQ(static_cast<int>(RoomType::None), 0);
  EXPECT_EQ(static_cast<int>(RoomType::Monster), 1);
  EXPECT_EQ(static_cast<int>(RoomType::Elite), 2);
  EXPECT_EQ(static_cast<int>(RoomType::Unknown), 3);
  EXPECT_EQ(static_cast<int>(RoomType::Rest), 4);
  EXPECT_EQ(static_cast<int>(RoomType::Shop), 5);
  EXPECT_EQ(static_cast<int>(RoomType::Treasure), 6);
  EXPECT_EQ(static_cast<int>(RoomType::Treasure) + 1, kNumRoomTypes);
}

// ------------------------------------------------------------- distribution

// The room list is shuffled before assignment. Without that the quota would be
// laid down in a fixed pattern and every map would look alike — so this checks
// that a given grid position is not always the same room type.
TEST(MapGeneration, RoomPlacementVariesAcrossSeeds) {
  std::set<RoomType> at_top_left;
  for (uint64_t s = 0; s < 100; ++s) {
    const Map map = generate_map(s);
    for (int x = 0; x < kMapWidth; ++x) {
      if (map[7][x].on_a_path()) at_top_left.insert(map[7][x].room);
    }
  }
  EXPECT_GT(at_top_left.size(), 2u)
      << "mid-map rooms barely vary — is the room list being shuffled?";
}

}  // namespace
}  // namespace minispire
