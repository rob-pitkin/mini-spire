#include "encounter.h"

#include <cassert>
#include <functional>
#include <numeric>

namespace minispire {

namespace {

using Generator = std::function<std::vector<Enemy>(std::mt19937&)>;

// A weighted entry in an encounter pool. Weights need not be normalized;
// sample_encounter picks proportionally.
struct WeightedEncounter {
  float weight;
  Generator generate;
};

bool coin(std::mt19937& rng) {
  return std::uniform_int_distribution<int>(0, 1)(rng) == 0;
}

// --- Sub-choice pickers (each internal coin flip is 50/50, verified) ---

Enemy random_louse(std::mt19937& rng) {
  return coin(rng) ? make_red_louse(rng) : make_green_louse(rng);
}
Enemy random_medium_slime(std::mt19937& rng) {
  return coin(rng) ? make_acid_slime_m(rng) : make_spike_slime_m(rng);
}
Enemy random_small_slime(std::mt19937& rng) {
  return coin(rng) ? make_acid_slime_s(rng) : make_spike_slime_s(rng);
}
Enemy random_slaver(std::mt19937& rng) {
  return coin(rng) ? make_blue_slaver(rng) : make_red_slaver(rng);
}
// A single Louse OR a Medium slime (both encounter sub-slots use this).
Enemy random_louse_or_medium_slime(std::mt19937& rng) {
  return coin(rng) ? random_louse(rng) : random_medium_slime(rng);
}

// --- Gremlin gang: draw 4 WITHOUT REPLACEMENT from {2 Mad, 2 Sneaky, 2 Fat,
// 1 Wizard, 1 Shield} (8 gremlins across 5 types). ---
std::vector<Enemy> generate_gremlin_gang(std::mt19937& rng) {
  // Build the 8-gremlin multiset as factory pointers, draw 4 without replacement.
  using Factory = Enemy (*)(std::mt19937&);
  std::vector<Factory> pool = {
      make_mad_gremlin,    make_mad_gremlin,    make_sneaky_gremlin,
      make_sneaky_gremlin, make_fat_gremlin,    make_fat_gremlin,
      make_gremlin_wizard, make_shield_gremlin,
  };
  std::vector<Enemy> gang;
  for (int i = 0; i < 4; ++i) {
    std::uniform_int_distribution<std::size_t> pick(0, pool.size() - 1);
    std::size_t idx = pick(rng);
    gang.push_back(pool[idx](rng));
    pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(idx));
  }
  return gang;
}

// --- Pool tables. Weights are Ascension-0 wiki values (verified with Rob). ---

const std::vector<WeightedEncounter>& weak_pool() {
  static const std::vector<WeightedEncounter> pool = {
      {0.25f, [](std::mt19937& r) { return std::vector<Enemy>{make_cultist(r)}; }},
      {0.25f, [](std::mt19937& r) { return std::vector<Enemy>{make_jaw_worm(r)}; }},
      // 2 Louses (each 50/50 red/green).
      {0.25f, [](std::mt19937& r) {
         return std::vector<Enemy>{random_louse(r), random_louse(r)};
       }},
      // Small Slimes: 1 Medium + 1 Small (each a coin flip).
      {0.25f, [](std::mt19937& r) {
         return std::vector<Enemy>{random_medium_slime(r), random_small_slime(r)};
       }},
  };
  return pool;
}

const std::vector<WeightedEncounter>& strong_pool() {
  // Weights as /32 fractions so they sum to exactly 1.0.
  static const std::vector<WeightedEncounter> pool = {
      {2.0f / 32, generate_gremlin_gang},
      // Large Slime: Acid-L or Spike-L.
      {4.0f / 32, [](std::mt19937& r) {
         return std::vector<Enemy>{coin(r) ? make_acid_slime_l(r)
                                           : make_spike_slime_l(r)};
       }},
      // Lots of Slimes: 3 Spike-S + 2 Acid-S (5 enemies — needs kMaxEnemies>=5).
      {2.0f / 32, [](std::mt19937& r) {
         return std::vector<Enemy>{make_spike_slime_s(r), make_spike_slime_s(r),
                                   make_spike_slime_s(r), make_acid_slime_s(r),
                                   make_acid_slime_s(r)};
       }},
      {4.0f / 32, [](std::mt19937& r) {
         return std::vector<Enemy>{make_blue_slaver(r)};
       }},
      {2.0f / 32, [](std::mt19937& r) {
         return std::vector<Enemy>{make_red_slaver(r)};
       }},
      // 3 Louses (each 50/50).
      {4.0f / 32, [](std::mt19937& r) {
         return std::vector<Enemy>{random_louse(r), random_louse(r),
                                   random_louse(r)};
       }},
      {4.0f / 32, [](std::mt19937& r) {
         return std::vector<Enemy>{make_fungi_beast(r), make_fungi_beast(r)};
       }},
      // Exordium Thugs: (Louse or Medium slime) + (Looter or Cultist or Slaver).
      {3.0f / 32, [](std::mt19937& r) {
         Enemy first = random_louse_or_medium_slime(r);
         int pick = std::uniform_int_distribution<int>(0, 2)(r);
         Enemy second = pick == 0   ? make_looter(r)
                        : pick == 1 ? make_cultist(r)
                                    : random_slaver(r);
         return std::vector<Enemy>{std::move(first), std::move(second)};
       }},
      // Exordium Wildlife: (Fungi or Jaw Worm) + (Louse or Medium slime).
      {3.0f / 32, [](std::mt19937& r) {
         Enemy first = coin(r) ? make_fungi_beast(r) : make_jaw_worm(r);
         Enemy second = random_louse_or_medium_slime(r);
         return std::vector<Enemy>{std::move(first), std::move(second)};
       }},
      {4.0f / 32, [](std::mt19937& r) {
         return std::vector<Enemy>{make_looter(r)};
       }},
  };
  return pool;
}

const std::vector<WeightedEncounter>& elite_pool() {
  // Uniform 1/3 — StS elite-node probabilities aren't well-sourced (assumption).
  static const std::vector<WeightedEncounter> pool = {
      {1.0f / 3, [](std::mt19937& r) {
         return std::vector<Enemy>{make_gremlin_nob(r)};
       }},
      {1.0f / 3, [](std::mt19937& r) {
         return std::vector<Enemy>{make_lagavulin(r)};
       }},
      // 3 Sentries: Bolt/Beam/Bolt stagger (sentries 1 & 3 open Bolt, 2 opens Beam).
      {1.0f / 3, [](std::mt19937& r) {
         return std::vector<Enemy>{make_bolt_sentry(r), make_beam_sentry(r),
                                   make_bolt_sentry(r)};
       }},
  };
  return pool;
}

const std::vector<WeightedEncounter>& pool_for(EncounterPool pool) {
  switch (pool) {
    case EncounterPool::Weak:
      return weak_pool();
    case EncounterPool::Strong:
      return strong_pool();
    case EncounterPool::Elite:
      return elite_pool();
  }
  assert(false && "unknown EncounterPool");
  return weak_pool();
}

}  // namespace

std::vector<Enemy> sample_encounter(EncounterPool pool, std::mt19937& rng) {
  const std::vector<WeightedEncounter>& table = pool_for(pool);
  float total = std::accumulate(
      table.begin(), table.end(), 0.0f,
      [](float acc, const WeightedEncounter& e) { return acc + e.weight; });
  float roll = std::uniform_real_distribution<float>(0.0f, total)(rng);
  float cumulative = 0.0f;
  for (const WeightedEncounter& e : table) {
    cumulative += e.weight;
    if (roll < cumulative) return e.generate(rng);
  }
  return table.back().generate(rng);  // float-edge fallback
}

}  // namespace minispire
