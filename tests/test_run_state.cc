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

  EXPECT_EQ(run.hp, 17);
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
