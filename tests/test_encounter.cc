#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>

#include "combat_state.h"
#include "encounter.h"

using namespace minispire;

namespace {

// Count enemies of a kind across a vector.
int count_kind(const std::vector<Enemy>& enemies, EnemyKind kind) {
  int n = 0;
  for (const Enemy& e : enemies)
    if (e.kind == kind) ++n;
  return n;
}

}  // namespace

// --- Determinism ---

TEST(Encounter, SameSeedSameEncounter) {
  std::mt19937 a(7), b(7);
  auto ea = sample_encounter(EncounterPool::Strong, a);
  auto eb = sample_encounter(EncounterPool::Strong, b);
  ASSERT_EQ(ea.size(), eb.size());
  for (std::size_t i = 0; i < ea.size(); ++i) {
    EXPECT_EQ(ea[i].kind, eb[i].kind);
    EXPECT_EQ(ea[i].hp, eb[i].hp);
  }
}

// --- Every draw is a valid, non-empty, in-bounds encounter ---

TEST(Encounter, AllPoolsProduceValidEncounters) {
  for (EncounterPool pool :
       {EncounterPool::Weak, EncounterPool::Strong, EncounterPool::Elite}) {
    for (uint32_t seed = 0; seed < 500; ++seed) {
      std::mt19937 rng(seed);
      auto enc = sample_encounter(pool, rng);
      EXPECT_FALSE(enc.empty());
      EXPECT_LE(static_cast<int>(enc.size()), kMaxEnemies)
          << "encounter exceeds kMaxEnemies";
      for (const Enemy& e : enc) EXPECT_GT(e.hp, 0);
    }
  }
}

// --- Weak pool composition ---

TEST(Encounter, WeakPoolOnlyProducesWeakEncounters) {
  std::map<EnemyKind, int> seen;
  for (uint32_t seed = 0; seed < 2000; ++seed) {
    std::mt19937 rng(seed);
    auto enc = sample_encounter(EncounterPool::Weak, rng);
    // Weak encounters are 1 or 2 enemies.
    EXPECT_LE(static_cast<int>(enc.size()), 2u);
    for (const Enemy& e : enc) seen[e.kind]++;
  }
  // All four weak encounters should have appeared: Cultist, Jaw Worm, Louses,
  // and slimes (medium + small).
  EXPECT_GT(seen[EnemyKind::Cultist], 0);
  EXPECT_GT(seen[EnemyKind::JawWorm], 0);
  EXPECT_GT(seen[EnemyKind::RedLouse] + seen[EnemyKind::GreenLouse], 0);
}

// --- "Lots of Slimes" = 5 enemies (the reason for kMaxEnemies=5) ---

TEST(Encounter, LotsOfSlimesHasFiveEnemies) {
  bool found_five = false;
  for (uint32_t seed = 0; seed < 3000 && !found_five; ++seed) {
    std::mt19937 rng(seed);
    auto enc = sample_encounter(EncounterPool::Strong, rng);
    if (enc.size() == 5) {
      found_five = true;
      // 3 Spike-S + 2 Acid-S.
      EXPECT_EQ(count_kind(enc, EnemyKind::SpikeSlimeS), 3);
      EXPECT_EQ(count_kind(enc, EnemyKind::AcidSlimeS), 2);
    }
  }
  EXPECT_TRUE(found_five) << "Lots of Slimes (5 enemies) never sampled";
}

// --- Gremlin gang: 4 drawn without replacement from the 8-multiset ---

TEST(Encounter, GremlinGangDrawsFourWithoutReplacement) {
  bool found_gang = false;
  for (uint32_t seed = 0; seed < 3000; ++seed) {
    std::mt19937 rng(seed);
    auto enc = sample_encounter(EncounterPool::Strong, rng);
    // A gremlin gang is 4 enemies, all gremlin kinds.
    bool all_gremlins =
        enc.size() == 4 &&
        std::all_of(enc.begin(), enc.end(), [](const Enemy& e) {
          return e.kind == EnemyKind::MadGremlin ||
                 e.kind == EnemyKind::SneakyGremlin ||
                 e.kind == EnemyKind::FatGremlin ||
                 e.kind == EnemyKind::GremlinWizard ||
                 e.kind == EnemyKind::ShieldGremlin;
        });
    if (!all_gremlins) continue;
    found_gang = true;
    // Without-replacement caps: at most 2 Mad/Sneaky/Fat, at most 1 Wizard/Shield.
    EXPECT_LE(count_kind(enc, EnemyKind::MadGremlin), 2);
    EXPECT_LE(count_kind(enc, EnemyKind::SneakyGremlin), 2);
    EXPECT_LE(count_kind(enc, EnemyKind::FatGremlin), 2);
    EXPECT_LE(count_kind(enc, EnemyKind::GremlinWizard), 1);
    EXPECT_LE(count_kind(enc, EnemyKind::ShieldGremlin), 1);
  }
  EXPECT_TRUE(found_gang) << "gremlin gang never sampled";
}

// --- Elite pool: exactly the three elites, roughly uniform ---

TEST(Encounter, ElitePoolIsTheThreeElites) {
  int nob = 0, laga = 0, sentries = 0;
  for (uint32_t seed = 0; seed < 3000; ++seed) {
    std::mt19937 rng(seed);
    auto enc = sample_encounter(EncounterPool::Elite, rng);
    if (enc.size() == 1 && enc[0].kind == EnemyKind::GremlinNob) ++nob;
    else if (enc.size() == 1 && enc[0].kind == EnemyKind::Lagavulin) ++laga;
    else if (enc.size() == 3 &&
             std::all_of(enc.begin(), enc.end(),
                         [](const Enemy& e) { return e.kind == EnemyKind::Sentry; }))
      ++sentries;
    else
      ADD_FAILURE() << "unexpected elite encounter (size " << enc.size() << ")";
  }
  // Uniform-ish: each ~1000 of 3000. Loose bounds for RNG noise.
  EXPECT_GT(nob, 700);
  EXPECT_GT(laga, 700);
  EXPECT_GT(sentries, 700);
}

// --- Sentry stagger: Bolt / Beam / Bolt openers ---

TEST(Encounter, SentryEncounterStaggersOpeners) {
  for (uint32_t seed = 0; seed < 3000; ++seed) {
    std::mt19937 rng(seed);
    auto enc = sample_encounter(EncounterPool::Elite, rng);
    if (enc.size() == 3 && enc[0].kind == EnemyKind::Sentry) {
      EXPECT_EQ(*enc[0].last_move, MoveName::Bolt);
      EXPECT_EQ(*enc[1].last_move, MoveName::Beam);
      EXPECT_EQ(*enc[2].last_move, MoveName::Bolt);
      return;
    }
  }
  FAIL() << "no sentry encounter sampled";
}
