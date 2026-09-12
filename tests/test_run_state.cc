// Tests for RunState and the handoff contract (docs/design/v2-spec.md §3.2).

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "run_state.h"
#include "turn_loop.h"

namespace minispire {
namespace {

// ------------------------------------------------------------- card identity

TEST(RunState, StarterDeckCardsGetDistinctUids) {
  RunState run = RunState::start(1);
  ASSERT_EQ(run.master_deck.size(), starter_deck().size());

  std::set<int> uids;
  for (const Card& c : run.master_deck) uids.insert(c.uid);
  EXPECT_EQ(uids.size(), run.master_deck.size()) << "uids collided";
  EXPECT_EQ(uids.count(kCombatScopedCardUid), 0u)
      << "a master-deck card kept the combat-scoped sentinel";
}

TEST(RunState, AddCardMintsAFreshUid) {
  RunState run = RunState::start(1);
  const int before = run.next_card_uid;
  run.add_card(Card{CardId::Anger});
  EXPECT_EQ(run.master_deck.back().uid, before);
  EXPECT_EQ(run.next_card_uid, before + 1);
}

// Two copies of the same card are different instances, and that is exactly what
// the uid is for — but they still PLAY the same, so same_as must not separate
// them (§3.2).
TEST(RunState, DuplicateCardsAreDistinctInstancesButStillCompareEqual) {
  RunState run = RunState::start(1);
  run.add_card(Card{CardId::Anger});
  run.add_card(Card{CardId::Anger});
  const Card& a = run.master_deck[run.master_deck.size() - 2];
  const Card& b = run.master_deck.back();

  EXPECT_NE(a.uid, b.uid);
  EXPECT_TRUE(a.same_as(b));
}

// ------------------------------------------------------ projection into combat

TEST(RunState, CombatSeesTheRunsHpNotAFreshIronclad) {
  RunState run = RunState::start(42);
  run.hp = 31;
  run.max_hp = 74;
  run.begin_combat(EncounterPool::Weak);

  EXPECT_EQ(run.combat.character.hp, 31);
  EXPECT_EQ(run.combat.character.max_hp, 74);
}

TEST(RunState, CombatDeckCarriesMasterDeckUids) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);

  std::vector<int> master;
  for (const Card& c : run.master_deck) master.push_back(c.uid);
  std::vector<int> in_combat;
  for (const Card& c : run.combat.draw_pile) in_combat.push_back(c.uid);
  for (const Card& c : run.combat.current_hand) in_combat.push_back(c.uid);

  std::sort(master.begin(), master.end());
  std::sort(in_combat.begin(), in_combat.end());
  EXPECT_EQ(master, in_combat)
      << "projection lost or invented card identities";
}

// The floor-indexed stream: the same floor of the same run always plays out the
// same, and a different floor does not.
TEST(RunState, CombatIsSeededPerFloor) {
  auto draw_order = [](uint64_t seed, int floor) {
    RunState run = RunState::start(seed);
    run.floor = floor;
    run.begin_combat(EncounterPool::Weak);
    std::vector<int> ids;
    for (const Card& c : run.combat.draw_pile) ids.push_back(static_cast<int>(c.card_id));
    return ids;
  };

  EXPECT_EQ(draw_order(7, 3), draw_order(7, 3));
  EXPECT_NE(draw_order(7, 3), draw_order(7, 4));
  EXPECT_NE(draw_order(7, 3), draw_order(8, 3));
}

// --------------------------------------------------------------- write-back

TEST(RunState, HpCarriesOutOfCombat) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 17;
  run.end_combat();

  // 17 plus Burning Blood's 6. Every Ironclad run holds the starter relic, so
  // a won fight always heals — this asserts the carry, not that HP is frozen.
  EXPECT_EQ(run.hp, 17 + 6);
  EXPECT_FALSE(run.in_combat);
}

// Feed and Neow can raise Max HP mid-fight, so it carries too.
TEST(RunState, MaxHpCarriesOutOfCombat) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.max_hp = 88;
  run.end_combat();

  EXPECT_EQ(run.max_hp, 88);
}

// Status cards the enemy added exist only for that fight. Nothing identifies
// them for removal — not carrying the piles forward is what removes them.
TEST(RunState, CombatCardsDoNotLeakIntoTheMasterDeck) {
  RunState run = RunState::start(42);
  const size_t before = run.master_deck.size();
  run.begin_combat(EncounterPool::Weak);

  run.combat.discard_pile.push_back(Card{CardId::Slimed});
  run.combat.current_hand.push_back(Card{CardId::Wound});
  run.end_combat();

  EXPECT_EQ(run.master_deck.size(), before);
  for (const Card& c : run.master_deck) {
    EXPECT_NE(c.card_id, CardId::Slimed);
    EXPECT_NE(c.card_id, CardId::Wound);
  }
}

// A mid-combat upgrade (Armaments) is combat-scoped and must not persist.
TEST(RunState, MidCombatCardChangesDoNotPersist) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);

  for (Card& c : run.combat.draw_pile) c.upgrades += 1;
  for (Card& c : run.combat.current_hand) c.upgrades += 1;
  run.end_combat();

  for (const Card& c : run.master_deck) EXPECT_EQ(c.upgrades, 0);
}

// ----------------------------------------------------------------- determinism

// The property §3.5 requires of the whole run: same seed, same actions, same
// trajectory.
TEST(RunState, SameSeedReplaysIdentically) {
  auto play = [](uint64_t seed) {
    RunState run = RunState::start(seed);
    std::vector<int> trace;
    for (int floor = 1; floor <= 3; ++floor) {
      run.floor = floor;
      run.begin_combat(EncounterPool::Weak);
      for (const Card& c : run.combat.draw_pile)
        trace.push_back(static_cast<int>(c.card_id));
      trace.push_back(static_cast<int>(run.combat.enemies.size()));
      run.end_combat();
      trace.push_back(run.hp);
    }
    return trace;
  };

  EXPECT_EQ(play(2024), play(2024));
  EXPECT_NE(play(2024), play(2025));
}

// ------------------------------------------------------------ episode boundary

TEST(RunState, StartsAtNeowOnFloorZero) {
  RunState run = RunState::start(1);
  EXPECT_EQ(run.phase, Phase::Neow);
  EXPECT_EQ(run.floor, 0);
  EXPECT_EQ(run.outcome, Outcome::InProgress);
  EXPECT_FALSE(run.is_terminal());
}

// The phase enum IS the observation's one-hot, so its numbering is an
// interface. This pins it against a silent renumbering.
TEST(RunState, PhaseNumberingMatchesTheSpec) {
  EXPECT_EQ(static_cast<int>(Phase::Neow), 0);
  EXPECT_EQ(static_cast<int>(Phase::Map), 1);
  EXPECT_EQ(static_cast<int>(Phase::Combat), 2);
  EXPECT_EQ(static_cast<int>(Phase::Reward), 3);
  EXPECT_EQ(static_cast<int>(Phase::Shop), 4);
  EXPECT_EQ(static_cast<int>(Phase::Rest), 5);
  EXPECT_EQ(static_cast<int>(Phase::Event), 6);
  EXPECT_EQ(static_cast<int>(Phase::Treasure), 7);
  EXPECT_EQ(static_cast<int>(Phase::Treasure) + 1, kNumPhases);
}

TEST(RunState, WinningAFightMovesToReward) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  EXPECT_EQ(run.phase, Phase::Combat);

  run.end_combat();
  EXPECT_EQ(run.phase, Phase::Reward);
  EXPECT_FALSE(run.is_terminal());
}

TEST(RunState, DyingEndsTheRun) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 0;
  run.end_combat();

  EXPECT_EQ(run.outcome, Outcome::Lost);
  EXPECT_TRUE(run.is_terminal());
}

TEST(RunState, AdvancingStepsTheFloorAndStartsTheNextFight) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();

  run.advance_to_next_floor(EncounterPool::Weak);
  EXPECT_EQ(run.floor, 1);
  EXPECT_EQ(run.phase, Phase::Combat);
  EXPECT_TRUE(run.in_combat);
}

TEST(RunState, ADeadRunDoesNotAdvance) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 0;
  run.end_combat();

  run.advance_to_next_floor(EncounterPool::Weak);
  EXPECT_EQ(run.floor, 0) << "a terminal run stepped onto another floor";
  EXPECT_FALSE(run.in_combat);
}

// Sequential fights with HP carrying across them — §11 step 2, and the thing a
// run actually is.
TEST(RunState, HpCarriesAcrossSequentialFights) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 55;
  run.end_combat();
  // Burning Blood heals 6 on the way out, so the next fight starts at 61.
  ASSERT_EQ(run.hp, 55 + 6);

  run.advance_to_next_floor(EncounterPool::Weak);
  EXPECT_EQ(run.combat.character.hp, 61)
      << "the next fight did not start from the HP the last one ended on";
}

// --------------------------------------------------------------- card rewards

TEST(CardReward, WinningAFightOffersThreeDistinctCards) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();

  ASSERT_EQ(run.card_reward.size(), static_cast<size_t>(kCardRewardSize));
  std::set<CardId> ids;
  for (const Card& c : run.card_reward) ids.insert(c.card_id);
  EXPECT_EQ(ids.size(), run.card_reward.size()) << "a reward offered a duplicate";
}

// An offer is not a possession: uid is minted on acquisition, not on display.
TEST(CardReward, OfferedCardsHaveNoIdentityUntilTaken) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();

  for (const Card& c : run.card_reward) {
    EXPECT_EQ(c.uid, kCombatScopedCardUid);
  }

  const CardId taking = run.card_reward[1].card_id;
  const size_t before = run.master_deck.size();
  run.take_card_reward(1);

  ASSERT_EQ(run.master_deck.size(), before + 1);
  EXPECT_EQ(run.master_deck.back().card_id, taking);
  EXPECT_NE(run.master_deck.back().uid, kCombatScopedCardUid);
}

TEST(CardReward, TakingClosesTheScreen) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  run.take_card_reward(0);

  EXPECT_TRUE(run.card_reward.empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

TEST(CardReward, SkippingTakesNothing) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  const size_t before = run.master_deck.size();
  run.skip_card_reward();

  EXPECT_EQ(run.master_deck.size(), before);
  EXPECT_TRUE(run.card_reward.empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

// The mask should stop this; the engine should not corrupt state if it doesn't.
TEST(CardReward, OutOfRangeIndexTakesNothing) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  const size_t before = run.master_deck.size();

  run.take_card_reward(99);
  run.take_card_reward(-1);
  EXPECT_EQ(run.master_deck.size(), before);
  EXPECT_EQ(run.card_reward.size(), static_cast<size_t>(kCardRewardSize));
}

TEST(CardReward, RewardsComeFromTheIroncladPools) {
  std::set<CardId> pool;
  for (CardId id : IRONCLAD_COMMON_POOL) pool.insert(id);
  for (CardId id : IRONCLAD_UNCOMMON_POOL) pool.insert(id);
  for (CardId id : IRONCLAD_RARE_POOL) pool.insert(id);

  for (uint64_t seed = 0; seed < 25; ++seed) {
    RunState run = RunState::start(seed);
    run.begin_combat(EncounterPool::Weak);
    run.end_combat();
    for (const Card& c : run.card_reward) {
      EXPECT_EQ(pool.count(c.card_id), 1u)
          << "reward offered a card outside the obtainable pools";
    }
  }
}

TEST(CardReward, PoolsAreTheCountedSizes) {
  EXPECT_EQ(IRONCLAD_COMMON_POOL.size(), 20u);
  EXPECT_EQ(IRONCLAD_UNCOMMON_POOL.size(), 36u);
  EXPECT_EQ(IRONCLAD_RARE_POOL.size(), 16u);
}

// ------------------------------------------------------------- rarity drift

// Uncommon must leave the pity counter ALONE. Decrementing on uncommon would
// make rares far too frequent, and nothing else in the system would notice.
TEST(CardRarity, OnlyCommonsWalkThePityCounterDown) {
  RunState run = RunState::start(1);
  std::mt19937 rng(7);

  const int before = run.card_rarity_factor;
  int commons = 0;
  for (int i = 0; i < 200; ++i) {
    const int prior = run.card_rarity_factor;
    const CardRarity r = run.roll_card_rarity(rng, RewardSource::Monster);
    if (r == CardRarity::Common) {
      EXPECT_LE(run.card_rarity_factor, prior);
      ++commons;
    } else if (r == CardRarity::Uncommon) {
      EXPECT_EQ(run.card_rarity_factor, prior) << "uncommon moved the counter";
    } else {
      EXPECT_EQ(run.card_rarity_factor, 5) << "rare did not reset the counter";
    }
  }
  EXPECT_GT(commons, 0);
  EXPECT_LE(run.card_rarity_factor, before);
}

// Falls out of the arithmetic and is worth pinning because it looks like a bug:
// at the starting factor of +5, `roll = random(0,99) + 5` is at least 5, and the
// rare threshold is 3 — so a rare is UNREACHABLE until commons have walked the
// counter down. The wiki states the same thing with the opposite sign: the
// offset starts at -5, which takes the 3% rare chance negative.
TEST(CardRarity, AFreshRunCannotRollARareYet) {
  RunState run = RunState::start(1);
  std::mt19937 rng(123);
  run.card_rarity_factor = 5;
  EXPECT_NE(run.roll_card_rarity(rng, RewardSource::Monster), CardRarity::Rare);
}

TEST(CardRarity, RaresBecomeReachableOnceTheCounterHasDrifted) {
  RunState run = RunState::start(1);
  std::mt19937 rng(5);
  run.card_rarity_factor = -40;

  bool saw_rare = false;
  for (int i = 0; i < 200 && !saw_rare; ++i) {
    run.card_rarity_factor = -40;  // hold it at the floor
    saw_rare = run.roll_card_rarity(rng, RewardSource::Monster) == CardRarity::Rare;
  }
  EXPECT_TRUE(saw_rare) << "rares never became reachable at the pity floor";
}

TEST(CardRarity, PityCounterFloorsAtMinus40) {
  RunState run = RunState::start(1);
  std::mt19937 rng(3);
  for (int i = 0; i < 5000; ++i) run.roll_card_rarity(rng, RewardSource::Monster);
  EXPECT_GE(run.card_rarity_factor, -40);
}

// A boss reward bypasses the roll but still resets the counter — easy to miss,
// and both halves matter.
TEST(CardRarity, BossRewardsAreAlwaysRareAndResetTheCounter) {
  RunState run = RunState::start(1);
  std::mt19937 rng(11);
  run.card_rarity_factor = -30;

  EXPECT_EQ(run.roll_card_rarity(rng, RewardSource::Boss), CardRarity::Rare);
  EXPECT_EQ(run.card_rarity_factor, 5);
}

// Elites roll better. Compared over many samples from the same stream so the
// difference is the chances, not the seed.
TEST(CardRarity, ElitesRollBetterThanNormalFights) {
  auto count_rares = [](RewardSource source) {
    RunState run = RunState::start(1);
    std::mt19937 rng(99);
    int rares = 0;
    for (int i = 0; i < 3000; ++i) {
      if (run.roll_card_rarity(rng, source) == CardRarity::Rare) ++rares;
    }
    return rares;
  };
  EXPECT_GT(count_rares(RewardSource::Elite), count_rares(RewardSource::Monster));
}

TEST(CardReward, IsDeterministicPerFloor) {
  auto offered = [](uint64_t seed, int floor) {
    RunState run = RunState::start(seed);
    run.floor = floor;
    run.begin_combat(EncounterPool::Weak);
    run.end_combat();
    std::vector<int> ids;
    for (const Card& c : run.card_reward) ids.push_back(static_cast<int>(c.card_id));
    return ids;
  };

  EXPECT_EQ(offered(5, 2), offered(5, 2));
  EXPECT_NE(offered(5, 2), offered(5, 3));
}

// ----------------------------------------------------------- walking skeleton

// §11 step 4: three fights, a card reward between each, terminating after N
// floors. The whole loop end to end — which is the point of the slice, since it
// proves the architecture before four more features are built on it.
TEST(WalkingSkeleton, ThreeFightsWithRewardsCompletesARun) {
  RunState run = RunState::start(2024);
  run.final_floor = 3;

  int fights = 0;
  int rewards_taken = 0;
  const size_t deck_at_start = run.master_deck.size();

  while (!run.is_terminal()) {
    run.advance_to_next_floor(EncounterPool::Weak);
    ASSERT_EQ(run.phase, Phase::Combat);
    ++fights;

    // Stand in for winning the fight.
    run.combat.character.hp = std::max(1, run.combat.character.hp - 5);
    run.end_combat();

    if (run.is_terminal()) break;
    ASSERT_EQ(run.phase, Phase::Reward);
    ASSERT_EQ(run.card_reward.size(), static_cast<size_t>(kCardRewardSize));
    run.take_card_reward(0);
    ++rewards_taken;
  }

  EXPECT_EQ(run.outcome, Outcome::Won);
  EXPECT_EQ(fights, 3);
  EXPECT_EQ(rewards_taken, 3) << "the last floor's reward is still offered";
  EXPECT_EQ(run.master_deck.size(), deck_at_start + 3);
  EXPECT_EQ(run.floor, 3);
}

TEST(WalkingSkeleton, DyingMidRunEndsItWithoutAReward) {
  RunState run = RunState::start(2024);
  run.final_floor = 3;

  run.advance_to_next_floor(EncounterPool::Weak);
  run.combat.character.hp = 0;
  run.end_combat();

  EXPECT_EQ(run.outcome, Outcome::Lost);
  EXPECT_TRUE(run.card_reward.empty()) << "a dead run was offered a reward";
  EXPECT_NE(run.phase, Phase::Reward);
}

// Every card taken is a distinct instance, even when the same card is offered
// on different floors — the uid is what makes that true.
TEST(WalkingSkeleton, EveryDraftedCardGetsItsOwnIdentity) {
  RunState run = RunState::start(7);
  run.final_floor = 3;

  while (!run.is_terminal()) {
    run.advance_to_next_floor(EncounterPool::Weak);
    run.combat.character.hp = std::max(1, run.combat.character.hp - 5);
    run.end_combat();
    if (run.is_terminal()) break;
    run.take_card_reward(0);
  }

  std::set<int> uids;
  for (const Card& c : run.master_deck) uids.insert(c.uid);
  EXPECT_EQ(uids.size(), run.master_deck.size());
}

// The property the whole run layer exists to provide, asserted over a full run
// rather than a single fight.
TEST(WalkingSkeleton, AWholeRunIsReproducibleFromItsSeed) {
  auto play = [](uint64_t seed) {
    RunState run = RunState::start(seed);
    run.final_floor = 3;
    std::vector<int> trace;
    while (!run.is_terminal()) {
      run.advance_to_next_floor(EncounterPool::Weak);
      for (const Card& c : run.combat.draw_pile)
        trace.push_back(static_cast<int>(c.card_id));
      run.combat.character.hp = std::max(1, run.combat.character.hp - 5);
      run.end_combat();
      if (run.is_terminal()) break;
      for (const Card& c : run.card_reward)
        trace.push_back(static_cast<int>(c.card_id));
      run.take_card_reward(0);
      trace.push_back(run.hp);
    }
    trace.push_back(static_cast<int>(run.outcome));
    return trace;
  };

  EXPECT_EQ(play(31337), play(31337));
  EXPECT_NE(play(31337), play(31338));
}

// clone() must preserve uids: an MCTS rollout that minted fresh ones would
// write back to the wrong cards.
TEST(RunState, ClonePreservesCardIdentity) {
  RunState run = RunState::start(42);
  run.begin_combat(EncounterPool::Weak);

  CombatState copy = run.combat.clone();
  ASSERT_EQ(copy.draw_pile.size(), run.combat.draw_pile.size());
  for (size_t i = 0; i < copy.draw_pile.size(); ++i) {
    EXPECT_EQ(copy.draw_pile[i].uid, run.combat.draw_pile[i].uid);
  }
}

}  // namespace
}  // namespace minispire
