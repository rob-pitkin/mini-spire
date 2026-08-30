// Tests for the named RNG streams of a run (docs/design/v2-spec.md §3.5) and
// for the Card uid invariants that ride alongside them (§3.2).

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "card.h"
#include "run_rng.h"

namespace minispire {
namespace {

std::vector<uint32_t> draw(std::mt19937 gen, int n) {
  std::vector<uint32_t> out;
  out.reserve(n);
  for (int i = 0; i < n; ++i) out.push_back(gen());
  return out;
}

// ---------------------------------------------------------------- RNG streams

TEST(RunRng, SameSeedAndStreamIsReproducible) {
  EXPECT_EQ(draw(make_stream(12345, RngStream::Map), 8),
            draw(make_stream(12345, RngStream::Map), 8));
}

TEST(RunRng, DifferentRunSeedsDiverge) {
  EXPECT_NE(draw(make_stream(1, RngStream::Map), 8),
            draw(make_stream(2, RngStream::Map), 8));
}

// The point of the partition: two systems drawing from the same run seed must
// not produce correlated sequences.
TEST(RunRng, StreamsAreIndependentOfEachOther) {
  const uint64_t seed = 0xABCDEF;
  std::set<std::vector<uint32_t>> sequences;
  for (auto s : {RngStream::Map, RngStream::Encounter, RngStream::Combat,
                 RngStream::CardReward, RngStream::Shop, RngStream::Event,
                 RngStream::Potion, RngStream::Relic, RngStream::AutoResolve,
                 RngStream::CardRandom}) {
    sequences.insert(draw(make_stream(seed, s), 8));
  }
  EXPECT_EQ(sequences.size(), 10u) << "two named streams produced identical draws";
}

// Combat is indexed by floor so a fight replays identically no matter what
// happened on other floors — v1.0.0's per-fight guarantee, preserved inside a
// run.
TEST(RunRng, CombatStreamIsIndexedByFloor) {
  const uint64_t seed = 777;
  EXPECT_EQ(draw(make_stream(seed, RngStream::Combat, 4), 8),
            draw(make_stream(seed, RngStream::Combat, 4), 8));
  EXPECT_NE(draw(make_stream(seed, RngStream::Combat, 4), 8),
            draw(make_stream(seed, RngStream::Combat, 5), 8));
}

// Stream isolation, stated as the property that actually matters: however many
// draws one stream takes, another stream's sequence is untouched. This is what
// stops an auto-resolved shrine shifting every downstream fight.
TEST(RunRng, DrawsInOneStreamDoNotPerturbAnother) {
  const uint64_t seed = 4242;
  const auto combat_before = draw(make_stream(seed, RngStream::Combat, 3), 8);

  std::mt19937 auto_resolve = make_stream(seed, RngStream::AutoResolve);
  for (int i = 0; i < 500; ++i) auto_resolve();

  EXPECT_EQ(draw(make_stream(seed, RngStream::Combat, 3), 8), combat_before);
}

TEST(RunRng, AdjacentIndicesAreNotCorrelated) {
  const uint64_t seed = 9;
  std::set<std::vector<uint32_t>> sequences;
  for (uint32_t floor = 0; floor < 16; ++floor) {
    sequences.insert(draw(make_stream(seed, RngStream::Combat, floor), 4));
  }
  EXPECT_EQ(sequences.size(), 16u) << "adjacent floor indices collided";
}

// ------------------------------------------------------------------- Card uid

TEST(CardUid, DefaultsToTheCombatScopedSentinel) {
  EXPECT_EQ(Card{CardId::Strike}.uid, kCombatScopedCardUid);
}

// The reason uid is declared last: every pre-existing aggregate initialisation
// must keep compiling and keep meaning what it meant.
TEST(CardUid, ExistingAggregateInitialisationIsUnaffected) {
  Card three{CardId::SearingBlow, 0, 2};
  EXPECT_EQ(three.card_id, CardId::SearingBlow);
  EXPECT_EQ(three.upgrades, 2);
  EXPECT_EQ(three.uid, kCombatScopedCardUid);
}

// uid must NOT participate in same_as. Two copies of a card play identically
// whichever instance they are; comparing uid would make every card unique and
// stop duplicate choice options collapsing.
TEST(CardUid, IsNotComparedBySameAs) {
  Card a{CardId::Strike};
  Card b{CardId::Strike};
  a.uid = 1;
  b.uid = 2;
  EXPECT_TRUE(a.same_as(b));
}

TEST(CardUid, IsNotInstanceStateForChoiceCollapsing) {
  Card c{CardId::Strike};
  c.uid = 17;
  EXPECT_FALSE(c.has_instance_state());
}

// same_as still separates cards that genuinely play differently.
TEST(CardUid, SameAsStillDistinguishesRealInstanceState) {
  Card plain{CardId::Rampage};
  Card buffed{CardId::Rampage, 8, 0};
  plain.uid = 1;
  buffed.uid = 1;
  EXPECT_FALSE(plain.same_as(buffed));
}

}  // namespace
}  // namespace minispire
