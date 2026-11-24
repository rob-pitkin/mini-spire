// Events, step 5 (docs/design/events.md §8): Match and Keep!, resolved by the
// engine (v2-spec §9, Rob E9 (a)).
//
// No test depends on which cards a draw put on the board or which pairs the
// policy found. The policy is tested on boards the test builds, where the
// answer is the same for every draw; the board by what holds for every seed;
// and the event by the same draws made on copies of its streams.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <vector>

#include "card.h"
#include "event.h"
#include "relic.h"
#include "run_rng.h"
#include "run_state.h"

using namespace minispire;

namespace {

RunState event_run(uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  run.floor = 7;
  run.gold = 100;
  run.phase = Phase::Map;
  return run;
}

bool in(const std::vector<CardId>& pool, CardId id) {
  return std::find(pool.begin(), pool.end(), id) != pool.end();
}

std::vector<CardId> deck_ids(const RunState& run) {
  std::vector<CardId> out;
  for (const Card& c : run.master_deck) out.push_back(c.card_id);
  return out;
}

int count_of(const RunState& run, CardId id) {
  const std::vector<CardId> ids = deck_ids(run);
  return static_cast<int>(std::count(ids.begin(), ids.end(), id));
}

}  // namespace

// ============================================================================
// The policy
// ============================================================================

// Every two cards on a board of one card are a pair: each attempt keeps one.
TEST(MatchAndKeepPolicy, EveryAttemptOnAMatchingBoardKeepsACard) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);
    const std::vector<CardId> board(kMatchAndKeepBoard, CardId::Anger);
    EXPECT_EQ(play_match_and_keep(board, rng, kMatchAndKeepAttempts),
              std::vector<CardId>(kMatchAndKeepAttempts, CardId::Anger))
        << seed;
  }
}

TEST(MatchAndKeepPolicy, ABoardWithNoPairKeepsNothing) {
  const std::vector<CardId> board = {
      CardId::Anger,     CardId::Bash,      CardId::Clash,
      CardId::Cleave,    CardId::Clothesline, CardId::Flex,
      CardId::Havoc,     CardId::Headbutt,  CardId::IronWave,
      CardId::PommelStrike, CardId::ShrugItOff, CardId::TwinStrike};
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);
    EXPECT_TRUE(play_match_and_keep(board, rng, kMatchAndKeepAttempts).empty())
        << seed;
  }
}

// The two cards turned over are different cards: one pair among singletons
// can be kept, a singleton never can, as it would be if a card were turned
// over twice in one attempt.
TEST(MatchAndKeepPolicy, AnAttemptTurnsOverTwoDifferentCards) {
  const std::vector<CardId> board = {
      CardId::Anger,     CardId::Anger,     CardId::Clash,
      CardId::Cleave,    CardId::Clothesline, CardId::Flex,
      CardId::Havoc,     CardId::Headbutt,  CardId::IronWave,
      CardId::PommelStrike, CardId::ShrugItOff, CardId::TwinStrike};
  for (uint32_t seed = 1; seed <= 50; ++seed) {
    std::mt19937 rng(seed);
    const std::vector<CardId> kept =
        play_match_and_keep(board, rng, kMatchAndKeepAttempts);
    EXPECT_LE(kept.size(), 1u) << seed;
    for (CardId id : kept) EXPECT_EQ(id, CardId::Anger) << seed;
  }
}

// A kept pair leaves the board: two cards of a kind are kept once.
TEST(MatchAndKeepPolicy, AKeptPairLeavesTheBoard) {
  std::mt19937 rng(1);
  const std::vector<CardId> board = {CardId::Anger, CardId::Anger};
  EXPECT_EQ(play_match_and_keep(board, rng, kMatchAndKeepAttempts),
            std::vector<CardId>{CardId::Anger});
}

TEST(MatchAndKeepPolicy, MakesOnlyTheAttemptsItIsGiven) {
  std::mt19937 rng(1);
  const std::vector<CardId> board(kMatchAndKeepBoard, CardId::Anger);
  EXPECT_EQ(play_match_and_keep(board, rng, 3).size(), 3u);
}

// ============================================================================
// The board
// ============================================================================

// Six cards, each twice: a rare, an uncommon and a common class card, an
// uncommon colorless card, a curse, and Bash.
TEST(MatchAndKeepBoard, IsSixPairsOfTheSourcesCards) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    RunState run = event_run(seed);
    const std::vector<CardId> board = run.match_and_keep_board();
    ASSERT_EQ(static_cast<int>(board.size()), kMatchAndKeepBoard);
    std::map<CardId, int> counts;
    for (CardId id : board) ++counts[id];
    EXPECT_EQ(counts.size(), 6u) << seed;
    for (const auto& [id, n] : counts) EXPECT_EQ(n, 2) << seed;
    EXPECT_TRUE(in(IRONCLAD_RARE_POOL, board[0])) << seed;
    EXPECT_TRUE(in(IRONCLAD_UNCOMMON_POOL, board[1])) << seed;
    EXPECT_TRUE(in(IRONCLAD_COMMON_POOL, board[2])) << seed;
    EXPECT_TRUE(in(COLORLESS_UNCOMMON_POOL, board[3])) << seed;
    EXPECT_TRUE(in(CURSE_POOL, board[4])) << seed;
    EXPECT_EQ(board[5], CardId::Bash) << seed;
  }
}

// In initializeCards' order, on the floor's card stream.
TEST(MatchAndKeepBoard, DrawsInTheSourcesOrderOnTheCardStream) {
  for (uint64_t seed = 1; seed <= 10; ++seed) {
    RunState run = event_run(seed);
    std::mt19937 rng = make_stream(seed, RngStream::CardReward, 7);
    const auto draw = [&rng](const std::vector<CardId>& pool) {
      return pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(
          rng)];
    };
    std::vector<CardId> expected;
    expected.push_back(draw(IRONCLAD_RARE_POOL));
    expected.push_back(draw(IRONCLAD_UNCOMMON_POOL));
    expected.push_back(draw(IRONCLAD_COMMON_POOL));
    expected.push_back(draw(COLORLESS_UNCOMMON_POOL));
    expected.push_back(draw(CURSE_POOL));
    expected.push_back(CardId::Bash);
    const std::vector<CardId> board = run.match_and_keep_board();
    EXPECT_EQ(std::vector<CardId>(board.begin(), board.begin() + 6), expected)
        << seed;
  }
}

// getCard draws the class pools: Prismatic Shard does not widen them.
TEST(MatchAndKeepBoard, PrismaticShardDoesNotWidenTheClassCards) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    RunState run = event_run(seed);
    run.obtain_relic(RelicId::PrismaticShard);
    const std::vector<CardId> board = run.match_and_keep_board();
    EXPECT_TRUE(in(IRONCLAD_RARE_POOL, board[0])) << seed;
    EXPECT_TRUE(in(IRONCLAD_UNCOMMON_POOL, board[1])) << seed;
    EXPECT_TRUE(in(IRONCLAD_COMMON_POOL, board[2])) << seed;
  }
}

// ============================================================================
// Keeping the cards
// ============================================================================

// makeCopy(), then onObtainCard: an Egg upgrades what is kept.
TEST(MatchAndKeepObtain, KeptCardsAreObtainedAsAnyCardIs) {
  RunState run = event_run();
  run.obtain_relic(RelicId::MoltenEgg);
  run.resolve_match_and_keep(
      std::vector<CardId>(kMatchAndKeepBoard, CardId::Anger));
  EXPECT_EQ(count_of(run, CardId::AngerPlus), kMatchAndKeepAttempts);
}

// ShowCardAndObtainEffect checks Omamori: two charges stop two curses.
TEST(MatchAndKeepObtain, OmamoriStopsAKeptCurse) {
  RunState run = event_run();
  run.obtain_relic(RelicId::Omamori);
  run.resolve_match_and_keep(
      std::vector<CardId>(kMatchAndKeepBoard, CardId::Regret));
  EXPECT_EQ(count_of(run, CardId::Regret), kMatchAndKeepAttempts - 2);
}

// ============================================================================
// The event
// ============================================================================

// Entered, played and left in one step: there is nothing to decide.
TEST(MatchAndKeep, ResolvesOnEntryAndLeaves) {
  RunState run = event_run();
  run.begin_event(EventId::MatchAndKeep);
  EXPECT_TRUE(run.event_options().empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

// The board shuffled on the event stream, then the policy on AutoResolve:
// the deck gains what those same draws keep.
TEST(MatchAndKeep, KeepsWhatThePolicyFindsOnTheShuffledBoard) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    RunState run = event_run(seed);
    RunState probe = run;
    std::vector<CardId> board = probe.match_and_keep_board();
    std::shuffle(board.begin(), board.end(), probe.event_rng_for_floor());
    std::mt19937 rng = make_stream(seed, RngStream::AutoResolve, 7);
    const std::vector<CardId> kept =
        play_match_and_keep(board, rng, kMatchAndKeepAttempts);

    const size_t before = run.master_deck.size();
    run.begin_event(EventId::MatchAndKeep);
    const std::vector<CardId> after = deck_ids(run);
    EXPECT_EQ(std::vector<CardId>(after.begin() + before, after.end()), kept)
        << seed;
  }
}
