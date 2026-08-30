#ifndef MINISPIRE_MAP_H
#define MINISPIRE_MAP_H

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace minispire {

// Act 1's map. See docs/design/v2-spec.md §4.1.
//
// Reimplemented in C++ from sts_map_oracle (MIT,
// https://github.com/Ru5ty0ne/sts_map_oracle), which is itself a
// reimplementation of Slay the Spire's generator. We transfer the algorithm,
// not the code, and no Rust enters the build.
//
// The port is deliberately faithful even where the source is odd — see
// StsRandom and get_common_ancestor, both of which reproduce behaviour that
// looks like a defect and is one.

// Room types, in the order the observation's one-hot uses (§5.4). `None` is
// index 0 so a zeroed buffer means "no room here", which is the right default
// for an unallocated map.
//
// `Unknown` is the `?` node — what generation calls an EventRoom. There is
// deliberately no separate "resolved event" value: a `?` stays `?` on the map
// forever, and what it became is reported by the phase, not here (§5.4).
//
// NOT the same 7 as the phase enum, which they superficially resemble.
enum class RoomType {
  None = 0,
  Monster,
  Elite,
  Unknown,
  Rest,
  Shop,
  Treasure,
};

inline constexpr int kNumRoomTypes = 7;

inline constexpr int kMapHeight = 15;
inline constexpr int kMapWidth = 7;
// Paths carved, NOT the width of a row — a floor holds at most this many rooms.
inline constexpr int kPathDensity = 6;

// Slay the Spire's own generator: xorshift128+ seeded through murmur_hash3,
// i.e. libgdx's RandomXS128.
//
// Ported rather than reusing §3.5's mt19937 for one reason: it makes our maps
// checkable against sts_map_oracle on a shared seed. For a 250-line algorithm
// port that is a far stronger validation than structural assertions alone, and
// it costs about thirty lines.
//
// The map stream still comes from §3.5's partition — this changes the generator
// inside the stream, not the partition itself.
class StsRandom {
 public:
  explicit StsRandom(uint64_t seed);

  uint64_t next_u64();
  // Uniform in [0, n], inclusive of n — matching the source's next_i32.
  int next_int(uint64_t n);
  // Uniform in [min, max], both inclusive.
  int rand_range(int min, int max);
  // Uniform in [0, n-1] — the shuffle's draw, EXCLUSIVE of n. Distinct from
  // next_int on purpose: they consume the same number of draws but cover
  // different ranges, and using the wrong one shifts every later roll.
  uint64_t next_shuffle(uint64_t n) { return next_capped(n); }

 private:
  uint64_t next_capped(uint64_t n);
  uint64_t seed0_;
  uint64_t seed1_;
};

struct Point {
  int x = 0;
  int y = 0;
  bool operator==(const Point& o) const { return x == o.x && y == o.y; }
  bool operator!=(const Point& o) const { return !(*this == o); }
};

struct MapEdge {
  int src_x = 0;
  int src_y = 0;
  int dst_x = 0;
  int dst_y = 0;

  // Ordered by destination, which is what makes "the left-most / right-most
  // edge of a neighbour" meaningful in the crossing-elimination step.
  bool operator<(const MapEdge& o) const {
    if (dst_x != o.dst_x) return dst_x < o.dst_x;
    return dst_y < o.dst_y;
  }
  bool operator==(const MapEdge& o) const {
    return src_x == o.src_x && src_y == o.src_y && dst_x == o.dst_x &&
           dst_y == o.dst_y;
  }
};

struct MapNode {
  int x = 0;
  int y = 0;
  RoomType room = RoomType::None;
  std::set<MapEdge> edges;
  std::vector<Point> parents;

  // Used by generation: a node the path-carver reached and can carve onward
  // from. The TOP row is never "on a path" by this definition because nothing
  // is carved out of it — which is exactly how the source uses it.
  bool on_a_path() const { return !edges.empty(); }

  // Whether this grid position is a real room on the finished map. The top row
  // has no outgoing edges but is a room whenever something below reaches it —
  // it is the guaranteed pre-boss campfire.
  bool is_room() const {
    return !edges.empty() || (y == kMapHeight - 1 && !parents.empty());
  }
};

// Row-major: map[y][x], y = 0 at the bottom (the floor you start on).
using Map = std::vector<std::vector<MapNode>>;

// Generates one act's map. Deterministic in `seed`.
Map generate_map(uint64_t seed);

// Renders the map the way sts_map_oracle does, for eyeballing and for tests:
// "M" monster, "E" elite, "?" unknown, "R" rest, "$" shop, "T" treasure,
// "*" no room.
std::string format_map(const Map& map);

}  // namespace minispire

#endif  // MINISPIRE_MAP_H
