#include "map.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

namespace minispire {
namespace {

uint64_t murmur_hash3(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

// Room-type quotas. Whatever is left over after these becomes a monster room.
constexpr float kShopChance = 0.05f;
constexpr float kRestChance = 0.12f;
constexpr float kEliteChance = 0.08f;
constexpr float kEventChance = 0.22f;

// The quota is consumed in THIS order. It is not alphabetical and not the
// enum's order; it is the order the source fills the list in, and it decides
// which type wins when several would fit a node.
constexpr RoomType kQuotaOrder[] = {RoomType::Shop, RoomType::Rest,
                                    RoomType::Elite, RoomType::Unknown};
constexpr float kQuotaChance[] = {kShopChance, kRestChance, kEliteChance,
                                  kEventChance};

}  // namespace

StsRandom::StsRandom(uint64_t seed) {
  // A zero seed would collapse the generator, so the source substitutes
  // INT64_MIN. Reproduced rather than "fixed" to a different sentinel.
  if (seed == 0) seed = static_cast<uint64_t>(INT64_MIN);
  seed0_ = murmur_hash3(seed);
  seed1_ = murmur_hash3(seed0_);
}

uint64_t StsRandom::next_u64() {
  uint64_t s1 = seed0_;
  const uint64_t s0 = seed1_;
  seed0_ = s0;
  s1 ^= s1 << 23;
  seed1_ = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);
  return s0 + seed1_;
}

uint64_t StsRandom::next_capped(uint64_t n) {
  // Rejection loop, kept verbatim. The condition is always true in practice for
  // the ranges this generator is asked for, but it is what the source does and
  // it consumes draws when it does reject.
  for (;;) {
    const uint64_t bits = next_u64() >> 1;
    const uint64_t value = bits % n;
    if (bits + n >= value + 1) return value;
  }
}

int StsRandom::next_int(uint64_t n) {
  return static_cast<int>(next_capped(n + 1));
}

int StsRandom::rand_range(int min, int max) {
  return min + next_int(static_cast<uint64_t>(max - min));
}

namespace {

Map create_nodes() {
  Map map(kMapHeight, std::vector<MapNode>(kMapWidth));
  for (int y = 0; y < kMapHeight; ++y) {
    for (int x = 0; x < kMapWidth; ++x) {
      map[y][x].x = x;
      map[y][x].y = y;
    }
  }
  return map;
}

const std::vector<Point>& parents_of(const Map& map, const Point& p) {
  return map[p.y][p.x].parents;
}

// Walks two same-row nodes up their parent chains looking for a shared
// ancestor within `max_depth` rows.
//
// ⚠️ Reproduces a bug from the game's own code, flagged as such in
// sts_map_oracle: the left/right assignment compares `node1.x < node2.y` —
// x against Y. It should obviously be node2.x. It is retained because the
// shipped game does this, and CLAUDE.md's parity rule is to the artifact, not
// the intent. "Fixing" it changes which maps a seed produces.
bool find_common_ancestor(const Map& map, const Point& node1, const Point& node2,
                          int max_depth, Point* out) {
  Point l_node = node1;
  Point r_node = node2;
  if (!(node1.x < node2.y)) {  // sic — see above
    l_node = node2;
    r_node = node1;
  }

  int current_y = node1.y;
  while (current_y >= 0 && current_y >= node1.y - max_depth) {
    const std::vector<Point>& l_parents = parents_of(map, l_node);
    const std::vector<Point>& r_parents = parents_of(map, r_node);
    if (l_parents.empty() || r_parents.empty()) return false;

    l_node = *std::max_element(
        l_parents.begin(), l_parents.end(),
        [](const Point& a, const Point& b) { return a.x < b.x; });
    r_node = *std::min_element(
        r_parents.begin(), r_parents.end(),
        [](const Point& a, const Point& b) { return a.x < b.x; });

    if (l_node == r_node) {
      *out = l_node;
      return true;
    }
    --current_y;
  }
  return false;
}

// Carves one path upward from `edge`'s destination, one row per call.
void create_path_from(Map& map, const MapEdge& edge, StsRandom& rng) {
  if (edge.dst_y + 1 >= kMapHeight) return;

  const int row_end = kMapWidth - 1;
  int min, max;
  if (edge.dst_x == 0) {
    min = 0;
    max = 1;
  } else if (edge.dst_x == row_end) {
    min = -1;
    max = 0;
  } else {
    min = -1;
    max = 1;
  }

  int new_edge_x = edge.dst_x + rng.rand_range(min, max);
  const int new_edge_y = edge.dst_y + 1;
  const Point current{edge.dst_x, edge.dst_y};

  // Push the path sideways if it would rejoin a sibling too soon, so branches
  // stay visibly separate rather than merging a row after they split.
  constexpr int kMinAncestorGap = 3;
  constexpr int kMaxAncestorGap = 5;
  Point candidate{new_edge_x, new_edge_y};
  const std::vector<Point> candidate_parents = map[new_edge_y][new_edge_x].parents;

  for (const Point& parent : candidate_parents) {
    if (current == parent) continue;
    Point ancestor;
    if (!find_common_ancestor(map, parent, current, kMaxAncestorGap, &ancestor)) {
      continue;
    }
    if (new_edge_y - ancestor.y >= kMinAncestorGap) continue;

    if (candidate.x > current.x) {
      new_edge_x = edge.dst_x + rng.rand_range(-1, 0);
      if (new_edge_x < 0) new_edge_x = edge.dst_x;
    } else if (candidate.x == current.x) {
      new_edge_x = edge.dst_x + rng.rand_range(-1, 1);
      if (new_edge_x > row_end) {
        new_edge_x = edge.dst_x - 1;
      } else if (new_edge_x < 0) {
        new_edge_x = edge.dst_x + 1;
      }
    } else {
      new_edge_x = edge.dst_x + rng.rand_range(0, 1);
      if (new_edge_x > row_end) new_edge_x = edge.dst_x;
    }
    candidate = Point{new_edge_x, new_edge_y};
  }

  // Paths must not cross. Clamp against the neighbours' outermost edges.
  if (edge.dst_x != 0) {
    const std::set<MapEdge>& left = map[edge.dst_y][edge.dst_x - 1].edges;
    if (!left.empty() && left.rbegin()->dst_x > new_edge_x) {
      new_edge_x = left.rbegin()->dst_x;
    }
  }
  if (edge.dst_x < row_end) {
    const std::set<MapEdge>& right = map[edge.dst_y][edge.dst_x + 1].edges;
    if (!right.empty() && right.begin()->dst_x < new_edge_x) {
      new_edge_x = right.begin()->dst_x;
    }
  }

  const MapEdge new_edge{edge.dst_x, edge.dst_y, new_edge_x, new_edge_y};
  map[edge.dst_y][edge.dst_x].edges.insert(new_edge);
  map[new_edge_y][new_edge_x].parents.push_back(Point{edge.dst_x, edge.dst_y});

  create_path_from(map, new_edge, rng);
}

void create_paths(Map& map, StsRandom& rng) {
  const int row_size = kMapWidth - 1;
  int first_starting_node = -1;
  for (int i = 0; i < kPathDensity; ++i) {
    int starting_node = rng.rand_range(0, row_size);
    if (i == 0) first_starting_node = starting_node;
    // The SECOND path is forced to start somewhere else, so a map never opens
    // with only one reachable first room. Later paths may reuse a start.
    while (i == 1 && starting_node == first_starting_node) {
      starting_node = rng.rand_range(0, row_size);
    }
    const MapEdge seed_edge{starting_node, -1, starting_node, 0};
    create_path_from(map, seed_edge, rng);
  }
}

// On the first row only, two paths that reached the same destination leave a
// duplicate edge. Trim the later one.
void filter_redundant_edges(Map& map) {
  std::set<std::pair<int, int>> seen;
  std::vector<std::pair<int, MapEdge>> to_delete;
  for (int x = 0; x < kMapWidth; ++x) {
    for (const MapEdge& edge : map[0][x].edges) {
      if (seen.count({edge.dst_x, edge.dst_y}) > 0) {
        to_delete.push_back({x, edge});
      }
      seen.insert({edge.dst_x, edge.dst_y});
    }
  }
  for (const auto& [x, edge] : to_delete) map[0][x].edges.erase(edge);
}

int count_unassigned_path_nodes(const Map& map) {
  int n = 0;
  for (const std::vector<MapNode>& row : map) {
    for (const MapNode& node : row) {
      if (node.on_a_path() && node.room == RoomType::None) ++n;
    }
  }
  return n;
}

// Fisher-Yates, descending, stopping at index 1 — and drawing from
// next_capped(i), i.e. [0, i-1], NOT next_int's inclusive range. Both details
// change how many draws are consumed and therefore every later roll, so this
// matches the source exactly rather than being "an equivalent shuffle".
void shuffle_rooms(std::vector<RoomType>& list, StsRandom& rng) {
  for (size_t i = list.size(); i >= 2; --i) {
    const size_t j = static_cast<size_t>(rng.next_shuffle(i));
    std::swap(list[j], list[i - 1]);
  }
}

bool rule_assignable_to_row(const MapNode& node, RoomType room) {
  // No rest or elite in the opening rows — the run needs a ramp.
  if (node.y <= 4 && (room == RoomType::Rest || room == RoomType::Elite)) {
    return false;
  }
  // No rest immediately before the boss: you get a guaranteed one on floor 15.
  if (node.y >= 13 && room == RoomType::Rest) return false;
  return true;
}

bool rule_parent_matches(const Map& map, const std::vector<Point>& parents,
                         RoomType room) {
  // Note this covers FOUR types where the sibling rule covers six. The
  // asymmetry is the game's, not a transcription slip.
  if (room != RoomType::Rest && room != RoomType::Treasure &&
      room != RoomType::Shop && room != RoomType::Elite) {
    return false;
  }
  for (const Point& p : parents) {
    if (map[p.y][p.x].room == room) return true;
  }
  return false;
}

std::vector<const MapNode*> siblings_of(const Map& map, const MapNode& node) {
  std::vector<const MapNode*> out;
  for (const Point& parent : node.parents) {
    for (const MapEdge& edge : map[parent.y][parent.x].edges) {
      const MapNode& sib = map[edge.dst_y][edge.dst_x];
      if (sib.x != node.x || sib.y != node.y) out.push_back(&sib);
    }
  }
  return out;
}

bool rule_sibling_matches(const std::vector<const MapNode*>& siblings,
                          RoomType room) {
  // SIX types here, against the parent rule's four.
  if (room == RoomType::None) return false;
  for (const MapNode* sib : siblings) {
    if (sib->room == room) return true;
  }
  return false;
}

bool next_room_type(const Map& map, const MapNode& node,
                    const std::vector<RoomType>& room_list, RoomType* out) {
  const std::vector<const MapNode*> siblings = siblings_of(map, node);
  for (RoomType room : room_list) {
    if (!rule_assignable_to_row(node, room)) continue;
    if (!rule_parent_matches(map, node.parents, room) &&
        !rule_sibling_matches(siblings, room)) {
      *out = room;
      return true;
    }
    // Row 0's documented escape hatch: if nothing fits, take it anyway rather
    // than leave the first floor empty.
    if (node.y == 0) {
      *out = room;
      return true;
    }
  }
  return false;
}

void assign_rooms(Map& map, std::vector<RoomType>& room_list) {
  for (int y = 0; y < kMapHeight; ++y) {
    for (int x = 0; x < kMapWidth; ++x) {
      MapNode& node = map[y][x];
      if (!node.on_a_path() || node.room != RoomType::None) continue;
      RoomType chosen;
      if (!next_room_type(map, node, room_list, &chosen)) continue;
      room_list.erase(std::find(room_list.begin(), room_list.end(), chosen));
      node.room = chosen;
    }
  }
}

// Anything on a path that the rules could not place becomes a monster room.
void fill_remaining_with_monsters(Map& map) {
  for (std::vector<MapNode>& row : map) {
    for (MapNode& node : row) {
      if (node.on_a_path() && node.room == RoomType::None) {
        node.room = RoomType::Monster;
      }
    }
  }
}

std::vector<RoomType> build_room_quota(int available) {
  std::vector<RoomType> list;
  for (int i = 0; i < 4; ++i) {
    const int n = static_cast<int>(
        std::round(kQuotaChance[i] * static_cast<float>(available)));
    for (int k = 0; k < n; ++k) list.push_back(kQuotaOrder[i]);
  }
  return list;
}

}  // namespace

Map generate_map(uint64_t seed) {
  // Act 1's map is generated from run_seed + 1 in the real game's scheme (the
  // three acts use offsets 1, 200, 600). Kept so a given base seed produces the
  // same Act 1 map here as in sts_map_oracle.
  StsRandom rng(seed + 1);

  Map map = create_nodes();
  create_paths(map, rng);
  filter_redundant_edges(map);

  // How many rooms the quota is sized against. Note two quirks, both retained:
  // the top row counts via its PARENTS (it has no outgoing edges), and row
  // height-2 is excluded outright.
  int available = 0;
  for (int y = 0; y < kMapHeight; ++y) {
    if (y == kMapHeight - 2) continue;
    for (int x = 0; x < kMapWidth; ++x) {
      if (map[y][x].is_room()) ++available;
    }
  }

  // Fixed floors, applied before the quota so those nodes are already spoken
  // for: floor 1 an easy fight, floor 9 the treasure chest, floor 15 the
  // guaranteed campfire before the boss. Set across the WHOLE row, including
  // grid positions no path reaches.
  for (int x = 0; x < kMapWidth; ++x) {
    map[0][x].room = RoomType::Monster;
    map[8][x].room = RoomType::Treasure;
    map[kMapHeight - 1][x].room = RoomType::Rest;
  }

  std::vector<RoomType> room_list = build_room_quota(available);

  // Pad with monsters so every remaining path node has something to take, THEN
  // shuffle. The shuffle is what makes two maps of the same shape differ;
  // without it the quota would be laid down in a fixed pattern.
  const int to_place = count_unassigned_path_nodes(map);
  if (static_cast<int>(room_list.size()) < to_place) {
    room_list.resize(to_place, RoomType::Monster);
  }
  shuffle_rooms(room_list, rng);

  assign_rooms(map, room_list);
  fill_remaining_with_monsters(map);

  return map;
}

std::string format_map(const Map& map) {
  std::string out;
  for (int y = kMapHeight - 1; y >= 0; --y) {
    for (int x = 0; x < kMapWidth; ++x) {
      const MapNode& node = map[y][x];
      char c = '*';
      if (node.is_room()) {
        switch (node.room) {
          case RoomType::Monster: c = 'M'; break;
          case RoomType::Elite: c = 'E'; break;
          case RoomType::Unknown: c = '?'; break;
          case RoomType::Rest: c = 'R'; break;
          case RoomType::Shop: c = '$'; break;
          case RoomType::Treasure: c = 'T'; break;
          case RoomType::None: c = '*'; break;
        }
      }
      out += ' ';
      out += c;
      out += ' ';
    }
    out += '\n';
  }
  return out;
}

}  // namespace minispire
