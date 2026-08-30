#ifndef MINISPIRE_RUN_RNG_H
#define MINISPIRE_RUN_RNG_H

#include <cstdint>
#include <random>

namespace minispire {

// Named RNG streams for a run. See docs/design/v2-spec.md §3.5.
//
// A run has one seed. Every random system draws from its OWN stream derived
// from that seed, so a draw in one system can never perturb another. This is
// not a convenience — CLAUDE.md treats the RNG stream as an interface, and a
// single shared stream makes replay fragile under any content change: adding a
// Courier restock or resolving a shrine at random would shift every fight after
// it.
//
// The real game mints one run seed and replays a run exactly given the same
// actions. Named streams are HOW that is achieved, not a departure from it.
enum class RngStream : uint32_t {
  Map = 0,        // map generation for the act
  Encounter,      // which monster group a combat node holds
  Combat,         // shuffles and enemy AI within one fight — indexed by floor
  CardReward,     // reward rarity rolls and card choices
  Shop,           // stock, prices, the discount slot
  Event,          // which event a ? becomes, and its internal rolls
  Potion,         // drop rolls
  Relic,          // which relic a chest or elite grants
  AutoResolve,    // engine-side policies for shape-breakers (§9)
  CardRandom,     // in-combat card generation: Discovery potions, Infernal Blade
};

// splitmix64 — a strong finalising mix. Cheap, and good enough that adjacent
// (stream, index) pairs produce unrelated seeds.
inline uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

// Seed for one named stream of a run.
//
// `index` distinguishes repeated uses of the same stream. Combat is indexed by
// FLOOR, which is what makes a fight replay identically regardless of what
// happened on other floors — preserving v1.0.0's per-fight guarantee inside a
// run. Streams that occur once per run leave it 0.
inline uint64_t derive_stream_seed(uint64_t run_seed, RngStream stream,
                                   uint32_t index = 0) {
  // Mix the components separately so that neither a stream id nor an index can
  // alias into the run seed's contribution.
  uint64_t h = splitmix64(run_seed);
  h = splitmix64(h ^ (static_cast<uint64_t>(stream) * 0x100000001B3ULL));
  h = splitmix64(h ^ (static_cast<uint64_t>(index) + 0x9E3779B9ULL));
  return h;
}

// A generator for one named stream. Deterministic in (run_seed, stream, index).
inline std::mt19937 make_stream(uint64_t run_seed, RngStream stream,
                                uint32_t index = 0) {
  return std::mt19937(
      static_cast<std::mt19937::result_type>(derive_stream_seed(run_seed, stream, index)));
}

}  // namespace minispire

#endif  // MINISPIRE_RUN_RNG_H
