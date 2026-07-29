#include <gtest/gtest.h>

#include <random>

#include "action.h"
#include "card.h"
#include "combat_env.h"
#include "enemy.h"
#include "combat_state.h"
#include "query.h"
#include "test_helpers.h"
#include "turn_loop.h"

using namespace minispire;
using minispire::testing::make_minimal_state;

// These tests drive the choice MECHANISM directly (build_choice /
// resolve_choice + a hand-built queue) rather than through a real card, so the
// source card is informational. Tests for the actual cards — Armaments,
// Warcry, Headbutt, Exhume, Dual Wield — live further down.
constexpr CardId kSourceStandIn = CardId::Strike;

// ============================================================================
// Stage 4c step 2: the pause/resume mechanism, driven directly.
// ============================================================================

namespace {

// Push a RequestChoice + a trailing marker action, then drain. The marker
// (gain 7 block) proves whether the REMAINDER of the queue was suspended
// rather than executed through the pause.
//
// NOTE: a choice with exactly ONE legal option auto-resolves (StS behaviour),
// so tests that want an actual pause must offer at least two options.
void request_choice_then_marker(CombatState& s, ChoiceKind kind,
                                CardId source) {
  ActionQueue q;
  ResolutionContext ctx;
  Action req;
  req.kind = ActionKind::RequestChoice;
  req.amount = static_cast<int>(kind);
  req.card = source;
  q.push_back(req);
  Action marker;
  marker.kind = ActionKind::GainBlock;
  marker.target = kPlayerSlot;
  marker.amount = 7;
  q.push_back(marker);
  drain(s, q, ctx);
}

}  // namespace

TEST(Choice, BuildChoiceDedupesAndSortsAscending) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Bash});    // higher CardId
  s.current_hand.push_back(Card{CardId::Strike});  // lower
  s.current_hand.push_back(Card{CardId::Strike});  // duplicate -> one option

  const PendingChoice pc =
      build_choice(s, ChoiceKind::HandToTopOfDraw, CardId::Strike);

  ASSERT_EQ(pc.num_options, 2);
  EXPECT_EQ(pc.options[0].card_id, CardId::Strike);  // ascending CardId order
  EXPECT_EQ(pc.options[1].card_id, CardId::Bash);
}

TEST(Choice, UpgradeFilterExcludesUpgradedAndStatusCards) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});      // upgradable
  s.current_hand.push_back(Card{CardId::StrikePlus});  // already upgraded
  s.current_hand.push_back(Card{CardId::Slimed});      // Status

  const PendingChoice pc =
      build_choice(s, ChoiceKind::UpgradeCardInHand, kSourceStandIn);

  ASSERT_EQ(pc.num_options, 1);
  EXPECT_EQ(pc.options[0].card_id, CardId::Strike);
}

TEST(Choice, DualWieldFilterKeepsOnlyAttacksAndPowers) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});   // Attack
  s.current_hand.push_back(Card{CardId::Inflame});  // Power
  s.current_hand.push_back(Card{CardId::Defend});   // Skill -> excluded

  const PendingChoice pc =
      build_choice(s, ChoiceKind::CopyAttackOrPowerInHand, CardId::Strike);

  ASSERT_EQ(pc.num_options, 2);
  for (int i = 0; i < pc.num_options; ++i) {
    const CardType t = CARD_DATABASE.at(pc.options[i].card_id).type;
    EXPECT_TRUE(t == CardType::Attack || t == CardType::Power);
  }
}

TEST(Choice, ChoicesReadTheCorrectSourcePile) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.discard_pile.push_back(Card{CardId::Bash});
  s.exhaust_pile.push_back(Card{CardId::Defend});

  const PendingChoice hand =
      build_choice(s, ChoiceKind::HandToTopOfDraw, CardId::Strike);
  const PendingChoice discard =
      build_choice(s, ChoiceKind::DiscardToTopOfDraw, CardId::Strike);
  const PendingChoice exhaust =
      build_choice(s, ChoiceKind::ExhaustToHand, CardId::Strike);

  ASSERT_EQ(hand.num_options, 1);
  EXPECT_EQ(hand.options[0].card_id, CardId::Strike);
  ASSERT_EQ(discard.num_options, 1);
  EXPECT_EQ(discard.options[0].card_id, CardId::Bash);
  ASSERT_EQ(exhaust.num_options, 1);
  EXPECT_EQ(exhaust.options[0].card_id, CardId::Defend);
}

TEST(Choice, RequestChoiceSuspendsTheRestOfTheQueue) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});  // 2nd option -> real pause

  request_choice_then_marker(s, ChoiceKind::UpgradeCardInHand,
                             kSourceStandIn);

  EXPECT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.kind, ChoiceKind::UpgradeCardInHand);
  // The marker did NOT run: the queue after the pause is suspended, not drained.
  EXPECT_EQ(s.character.current_block, 0);
}

TEST(Choice, ResolveChoiceAppliesTheChoiceAndResumesTheQueue) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});  // 2nd option -> real pause

  request_choice_then_marker(s, ChoiceKind::UpgradeCardInHand,
                             kSourceStandIn);
  ASSERT_TRUE(s.pending_choice.active());

  ASSERT_TRUE(resolve_choice(s, 0));

  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.current_hand[0].card_id, CardId::StrikePlus);  // upgraded
  EXPECT_EQ(s.character.current_block, 7);  // the suspended marker resumed
}

TEST(Choice, NoLegalOptionsSkipsThePauseEntirely) {
  // Exhume with an empty exhaust pile: the card still plays, the choice just
  // has no target. No pause, and the rest of the queue resolves normally.
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(s.exhaust_pile.empty());

  request_choice_then_marker(s, ChoiceKind::ExhaustToHand, CardId::Strike);

  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.character.current_block, 7);  // queue never suspended
}

TEST(Choice, ResolveChoiceRejectsInvalidIndicesWithoutCorruptingState) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});  // 2nd option -> real pause
  request_choice_then_marker(s, ChoiceKind::UpgradeCardInHand,
                             kSourceStandIn);
  ASSERT_TRUE(s.pending_choice.active());

  EXPECT_FALSE(resolve_choice(s, -5));
  EXPECT_FALSE(resolve_choice(s, 99));
  EXPECT_FALSE(resolve_choice(s, s.pending_choice.num_options));
  // Still paused, nothing applied, marker still suspended.
  EXPECT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(s.character.current_block, 0);

  // A valid index still works afterwards.
  EXPECT_TRUE(resolve_choice(s, 0));
  EXPECT_EQ(s.character.current_block, 7);
}

TEST(Choice, ResolveChoiceOnAnUnpausedStateIsRejected) {
  CombatState s = make_minimal_state(0);
  EXPECT_FALSE(resolve_choice(s, 0));
}

TEST(Choice, DecliningIsOnlyLegalForOptionalChoices) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});  // 2nd option -> real pause
  request_choice_then_marker(s, ChoiceKind::UpgradeCardInHand,
                             kSourceStandIn);

  // Not optional by default -> decline rejected, state untouched.
  EXPECT_FALSE(resolve_choice(s, kDeclineChoice));
  EXPECT_TRUE(s.pending_choice.active());

  // Mark optional -> decline accepted, choice NOT applied, queue resumes.
  s.pending_choice.is_optional = true;
  EXPECT_TRUE(resolve_choice(s, kDeclineChoice));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);  // not upgraded
  EXPECT_EQ(s.character.current_block, 7);               // but queue resumed
}

// --- The MCTS-critical property -------------------------------------------

TEST(Choice, PauseSurvivesCloneAndResumesIndependentlyOnTheClone) {
  // The round-trip the migration plan calls for: pause -> clone -> resume on
  // the clone. This is what makes a suspended resolution safe for MCTS.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  request_choice_then_marker(s, ChoiceKind::UpgradeCardInHand,
                             kSourceStandIn);
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_EQ(s.pending_choice.num_options, 2);  // Strike and Defend

  CombatState branch_a = s.clone();
  CombatState branch_b = s.clone();

  // The clones carry the full pause: kind, options, and the suspended queue.
  ASSERT_TRUE(branch_a.pending_choice.active());
  ASSERT_EQ(branch_a.pending_choice.num_options, 2);

  // Resolve the two branches DIFFERENTLY.
  ASSERT_TRUE(resolve_choice(branch_a, 0));  // upgrade Strike
  ASSERT_TRUE(resolve_choice(branch_b, 1));  // upgrade Defend

  // Each branch resumed its own suspended queue.
  EXPECT_EQ(branch_a.character.current_block, 7);
  EXPECT_EQ(branch_b.character.current_block, 7);

  // And each applied its own choice, without touching the other or the parent.
  EXPECT_EQ(branch_a.current_hand[0].card_id, CardId::StrikePlus);
  EXPECT_EQ(branch_a.current_hand[1].card_id, CardId::Defend);
  EXPECT_EQ(branch_b.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(branch_b.current_hand[1].card_id, CardId::DefendPlus);

  // The parent is still paused and untouched — cloning did not consume it.
  EXPECT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(s.character.current_block, 0);
}

// --- Per-kind application semantics ---------------------------------------

TEST(Choice, WarcryMovesAHandCardToTopOfDraw) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Bash});
  s.current_hand.push_back(Card{CardId::Strike});  // 2nd option -> real pause
  s.draw_pile.push_back(Card{CardId::Defend});

  request_choice_then_marker(s, ChoiceKind::HandToTopOfDraw, CardId::Strike);
  // Options are ascending by CardId: [Strike, Bash]. Pick Bash (index 1).
  ASSERT_EQ(s.pending_choice.options[1].card_id, CardId::Bash);
  ASSERT_TRUE(resolve_choice(s, 1));

  ASSERT_EQ(s.current_hand.size(), 1u);  // the filler Strike remains
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);
  ASSERT_EQ(s.draw_pile.size(), 2u);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Bash);  // back() == top
}

TEST(Choice, HeadbuttMovesADiscardCardToTopOfDraw) {
  CombatState s = make_minimal_state(0);
  s.discard_pile.push_back(Card{CardId::Bash});
  s.discard_pile.push_back(Card{CardId::Strike});  // 2nd option -> real pause

  request_choice_then_marker(s, ChoiceKind::DiscardToTopOfDraw,
                             CardId::Strike);
  ASSERT_EQ(s.pending_choice.options[1].card_id, CardId::Bash);
  ASSERT_TRUE(resolve_choice(s, 1));

  ASSERT_EQ(s.discard_pile.size(), 1u);  // the filler Strike remains
  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Bash);
}

TEST(Choice, ExhumeMovesAnExhaustedCardToHand) {
  CombatState s = make_minimal_state(0);
  s.exhaust_pile.push_back(Card{CardId::Bash});
  s.exhaust_pile.push_back(Card{CardId::Strike});  // 2nd option -> real pause

  request_choice_then_marker(s, ChoiceKind::ExhaustToHand, CardId::Strike);
  ASSERT_EQ(s.pending_choice.options[1].card_id, CardId::Bash);
  ASSERT_TRUE(resolve_choice(s, 1));

  // The one sanctioned removal from the exhaust pile (it only grows otherwise).
  ASSERT_EQ(s.exhaust_pile.size(), 1u);  // the filler Strike remains
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Bash);
}

TEST(Choice, DualWieldCopiesWithoutRemovingTheOriginal) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Bash});  // 2nd option -> real pause

  request_choice_then_marker(s, ChoiceKind::CopyAttackOrPowerInHand,
                             CardId::Strike);
  ASSERT_TRUE(resolve_choice(s, 0));  // copy the Strike

  ASSERT_EQ(s.current_hand.size(), 3u);  // Strike, Bash, + the copy
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(s.current_hand[2].card_id, CardId::Strike);  // the copy
}

TEST(Choice, UpgradeAffectsOnlyOneCopyOfADuplicate) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});  // 2nd distinct option

  request_choice_then_marker(s, ChoiceKind::UpgradeCardInHand,
                             kSourceStandIn);
  // Strike appears twice but dedupes to ONE option; Defend is the second.
  ASSERT_EQ(s.pending_choice.num_options, 2);
  ASSERT_EQ(s.pending_choice.options[0].card_id, CardId::Strike);
  ASSERT_TRUE(resolve_choice(s, 0));

  EXPECT_EQ(s.current_hand[0].card_id, CardId::StrikePlus);
  EXPECT_EQ(s.current_hand[1].card_id, CardId::Strike);  // only one upgraded
}

TEST(Choice, TerminalOutcomeDiscardsAPendingChoice) {
  // Death takes precedence (Rob's Offering rule): if the fight ends during a
  // resolution, any pending choice is dropped rather than left awaiting an
  // answer on a finished fight. Here a lose-HP action kills the player in the
  // same queue that would otherwise pause for a choice.
  CombatState s = make_minimal_state(0);
  s.character.hp = 3;
  s.current_hand.push_back(Card{CardId::Strike});

  ActionQueue q;
  ResolutionContext ctx;
  Action kill;  // lethal self-damage resolves FIRST
  kill.kind = ActionKind::LoseHp;
  kill.amount = 99;
  q.push_back(kill);
  Action req;  // ... so this choice must never arm
  req.kind = ActionKind::RequestChoice;
  req.amount = static_cast<int>(ChoiceKind::UpgradeCardInHand);
  req.card = kSourceStandIn;
  q.push_back(req);

  s.outcome = Outcome::Lost;  // as the turn loop's terminal check would set
  drain(s, q, ctx);

  EXPECT_FALSE(s.pending_choice.active());
}

// ============================================================================
// Stage 4c step 3: the obs / mask encoding of a pending choice.
// These pin the RL interface — the part that is expensive to change later.
// ============================================================================

TEST(ChoiceEncoding, CombatIndicesAreUnchangedByTheSlotChannel) {
  // The whole point of appending the channel: a policy's learned mapping for
  // playing cards must survive. Index arithmetic for the combat block is
  // exactly what it was pre-4c.
  EXPECT_EQ(kEndTurnAction, kNumCardTypes * kMaxEnemies);
  EXPECT_EQ(kFirstOptionSlot, kEndTurnAction + 1);
  EXPECT_EQ(kDeclineAction, kFirstOptionSlot + kNumOptionSlots);
  EXPECT_EQ(kTotalActions, kDeclineAction + 1);
  // A Strike at enemy slot 2 is still index (Strike * 5 + 2).
  EXPECT_EQ(static_cast<int>(CardId::Strike) * kMaxEnemies + 2,
            static_cast<int>(CardId::Strike) * kMaxEnemies + 2);
}

TEST(ChoiceEncoding, SlotsAreMaskedOffDuringNormalCombat) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  const auto mask = valid_actions(s);

  ASSERT_FALSE(s.pending_choice.active());
  for (int i = 0; i < kNumOptionSlots; ++i) {
    EXPECT_FALSE(mask[kFirstOptionSlot + i]) << "slot " << i;
  }
  EXPECT_FALSE(mask[kDeclineAction]);
  EXPECT_TRUE(mask[kEndTurnAction]);  // combat still legal
}

TEST(ChoiceEncoding, CombatIsMaskedOffDuringAPendingChoice) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  s.pending_choice = build_choice(s, ChoiceKind::UpgradeCardInHand,
                                  kSourceStandIn);
  ASSERT_TRUE(s.pending_choice.active());

  const auto mask = valid_actions(s);
  // Every combat index is illegal — including end-turn, which is otherwise
  // always legal. The two blocks are mutually exclusive.
  for (int i = 0; i <= kEndTurnAction; ++i) {
    EXPECT_FALSE(mask[i]) << "combat action " << i << " legal during a choice";
  }
  // Exactly the offered slots are legal.
  for (int i = 0; i < kNumOptionSlots; ++i) {
    EXPECT_EQ(mask[kFirstOptionSlot + i], i < s.pending_choice.num_options)
        << "slot " << i;
  }
}

TEST(ChoiceEncoding, DeclineIsMaskedUnlessTheChoiceIsOptional) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.pending_choice = build_choice(s, ChoiceKind::UpgradeCardInHand,
                                  kSourceStandIn);

  EXPECT_FALSE(valid_actions(s)[kDeclineAction]);
  s.pending_choice.is_optional = true;
  EXPECT_TRUE(valid_actions(s)[kDeclineAction]);
}

TEST(ChoiceEncoding, ApplyActionRoutesSlotIndicesToResolveChoice) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.pending_choice = build_choice(s, ChoiceKind::UpgradeCardInHand,
                                  kSourceStandIn);
  ASSERT_EQ(s.pending_choice.num_options, 1);

  EXPECT_TRUE(apply_action(s, kFirstOptionSlot + 0));

  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.current_hand[0].card_id, CardId::StrikePlus);
}

TEST(ChoiceEncoding, ApplyActionRejectsCombatActionsWhilePaused) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.pending_choice = build_choice(s, ChoiceKind::UpgradeCardInHand,
                                  kSourceStandIn);

  // Playing a card or ending the turn mid-choice must be rejected outright.
  EXPECT_FALSE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));
  EXPECT_FALSE(apply_action(s, kEndTurnAction));
  EXPECT_TRUE(s.pending_choice.active());  // still awaiting an answer
}

TEST(ChoiceEncoding, ApplyActionRejectsSlotActionsWhenNoChoicePends) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_FALSE(s.pending_choice.active());

  EXPECT_FALSE(apply_action(s, kFirstOptionSlot));
  EXPECT_FALSE(apply_action(s, kDeclineAction));
}

TEST(ChoiceEncoding, ApplyActionRejectsUnofferedSlots) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.pending_choice = build_choice(s, ChoiceKind::UpgradeCardInHand,
                                  kSourceStandIn);
  ASSERT_EQ(s.pending_choice.num_options, 1);

  // Slot 1 exists in the action space but is not offered.
  EXPECT_FALSE(apply_action(s, kFirstOptionSlot + 1));
  EXPECT_TRUE(s.pending_choice.active());
}

// --- Observation ------------------------------------------------------------

namespace {
// Obs offsets, derived from the engine's constants (never hardcoded).
constexpr int kChoiceBase = CombatEnv::kPlayerObsSize +
                            kMaxEnemies * CombatEnv::kEnemyObsStride +
                            CombatEnv::kPileObsSize + 1;
constexpr int kSlotBase = kChoiceBase + CombatEnv::kChoiceHeaderSize;
}  // namespace

TEST(ChoiceEncoding, ObsChoiceBlockIsZeroWhenNoChoicePends) {
  CombatEnv env;
  env.reset(0);
  const auto obs = env.obs();
  for (int i = kChoiceBase; i < CombatEnv::kObsSize; ++i) {
    EXPECT_FLOAT_EQ(obs[i], 0.0f) << "choice obs slot " << i;
  }
}

TEST(ChoiceEncoding, ObsPublishesThePendingChoiceAndItsOptions) {
  // R4 (the NLE lesson): the agent must always be able to tell that a menu is
  // open, and what is on it.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Strike});
  s.pending_choice = build_choice(s, ChoiceKind::UpgradeCardInHand,
                                  kSourceStandIn);
  ASSERT_EQ(s.pending_choice.num_options, 2);

  CombatEnv env(std::move(s), 0.0f);
  const auto obs = env.obs();

  EXPECT_FLOAT_EQ(obs[kChoiceBase + 0], 1.0f);  // pending
  EXPECT_FLOAT_EQ(obs[kChoiceBase + 1],
                  static_cast<float>(
                      static_cast<int>(ChoiceKind::UpgradeCardInHand)));
  EXPECT_FLOAT_EQ(obs[kChoiceBase + 2], 0.0f);  // source pile = hand
  EXPECT_FLOAT_EQ(obs[kChoiceBase + 4], 0.0f);  // not optional

  // Slot descriptors: occupied + the card id, in ascending-CardId order.
  const int stride = CombatEnv::kChoiceSlotStride;
  EXPECT_FLOAT_EQ(obs[kSlotBase + 0 * stride + 0], 1.0f);
  EXPECT_FLOAT_EQ(obs[kSlotBase + 0 * stride + 1],
                  static_cast<float>(static_cast<int>(CardId::Strike)));
  EXPECT_FLOAT_EQ(obs[kSlotBase + 1 * stride + 0], 1.0f);
  EXPECT_FLOAT_EQ(obs[kSlotBase + 1 * stride + 1],
                  static_cast<float>(static_cast<int>(CardId::Defend)));
  // The third slot is unoccupied.
  EXPECT_FLOAT_EQ(obs[kSlotBase + 2 * stride + 0], 0.0f);
}

TEST(ChoiceEncoding, ObsSourcePileDistinguishesTheChoiceKinds) {
  CombatState s = make_minimal_state(0);
  s.discard_pile.push_back(Card{CardId::Strike});
  s.pending_choice = build_choice(s, ChoiceKind::DiscardToTopOfDraw,
                                  kSourceStandIn);
  CombatEnv env(std::move(s), 0.0f);
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 2], 2.0f);  // discard

  CombatState s2 = make_minimal_state(0);
  s2.exhaust_pile.push_back(Card{CardId::Strike});
  s2.pending_choice = build_choice(s2, ChoiceKind::ExhaustToHand,
                                   kSourceStandIn);
  CombatEnv env2(std::move(s2), 0.0f);
  EXPECT_FLOAT_EQ(env2.obs()[kChoiceBase + 2], 3.0f);  // exhaust
}

TEST(ChoiceEncoding, ObsAndMaskAgreeOnWhichSlotsAreOffered) {
  // The invariant that matters: whatever the obs says is occupied is exactly
  // what the mask says is legal.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Bash});
  s.pending_choice = build_choice(s, ChoiceKind::HandToTopOfDraw,
                                  kSourceStandIn);

  CombatEnv env(std::move(s), 0.0f);
  const auto obs = env.obs();
  const auto mask = env.action_mask();
  const int stride = CombatEnv::kChoiceSlotStride;

  for (int i = 0; i < kNumOptionSlots; ++i) {
    const bool occupied = obs[kSlotBase + i * stride + 0] > 0.5f;
    const bool legal = mask[kFirstOptionSlot + i] != 0;
    EXPECT_EQ(occupied, legal) << "obs/mask disagree at slot " << i;
  }
}

// ============================================================================
// Stage 4c step 4: the five choice cards, driven end-to-end through
// apply_action (play the card -> pause -> answer with a slot action).
// ============================================================================

namespace {

// Play `card` from hand via the real action path, with enough energy.
bool play(CombatState& s, CardId card, int target = 0) {
  s.current_hand.push_back(Card{card});
  s.character.energy = 3;
  return apply_action(s, static_cast<int>(card) * kMaxEnemies + target);
}

}  // namespace

TEST(ChoiceCards, ArmamentsGainsBlockAndUpgradesTheChosenCard) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Armaments));

  EXPECT_EQ(s.character.current_block, 5);  // Armaments gains 5 Block
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_EQ(s.pending_choice.kind, ChoiceKind::UpgradeCardInHand);

  // Answer via the action space, as an agent would.
  ASSERT_EQ(s.pending_choice.options[0].card_id, CardId::Strike);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 0));

  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.current_hand[0].card_id, CardId::StrikePlus);
  EXPECT_EQ(s.current_hand[1].card_id, CardId::Defend);  // untouched
}

TEST(ChoiceCards, ArmamentsPlusUpgradesTheWholeHandWithNoChoice) {
  // The upgrade changes the choice's SHAPE, so there is no pause at all.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Slimed});  // Status: not upgradable

  ASSERT_TRUE(play(s, CardId::ArmamentsPlus));

  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.character.current_block, 5);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::StrikePlus);
  EXPECT_EQ(s.current_hand[1].card_id, CardId::DefendPlus);
  EXPECT_EQ(s.current_hand[2].card_id, CardId::Slimed);  // unchanged
}

TEST(ChoiceCards, WarcryDrawsFirstThenChoosesFromTheResultingHand) {
  // Ordering matters: Warcry draws, and only THEN do you pick a card — so a
  // just-drawn card must be a legal option.
  CombatState s = make_minimal_state(0);
  s.draw_pile.push_back(Card{CardId::Bash});  // back() is drawn first
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Warcry));

  ASSERT_TRUE(s.pending_choice.active());
  // The drawn Bash is on offer alongside the Defend already in hand.
  bool saw_bash = false;
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == CardId::Bash) saw_bash = true;
  }
  EXPECT_TRUE(saw_bash) << "Warcry must draw before offering the choice";
}

TEST(ChoiceCards, WarcryExhaustsItself) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Bash});

  ASSERT_TRUE(play(s, CardId::Warcry));
  if (s.pending_choice.active()) {
    ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 0));
  }

  ASSERT_EQ(s.exhaust_pile.size(), 1u);
  EXPECT_EQ(s.exhaust_pile[0].card_id, CardId::Warcry);
  EXPECT_TRUE(s.discard_pile.empty());  // exhausted, not discarded
}

TEST(ChoiceCards, HeadbuttDealsDamageThenMovesADiscardCardToTopOfDraw) {
  CombatState s = make_minimal_state(0);
  const int hp = s.enemies[0].hp;
  s.discard_pile.push_back(Card{CardId::Bash});
  s.discard_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Headbutt));

  EXPECT_EQ(s.enemies[0].hp, hp - 9);  // damage resolves before the choice
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_EQ(s.pending_choice.options[1].card_id, CardId::Bash);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 1));

  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Bash);
}

TEST(ChoiceCards, HeadbuttWithASingleDiscardCardAutoResolves) {
  // StS: "if there is only one card in your discard pile, it will
  // automatically be placed on top of your draw pile" — no prompt.
  CombatState s = make_minimal_state(0);
  s.discard_pile.push_back(Card{CardId::Bash});

  ASSERT_TRUE(play(s, CardId::Headbutt));

  EXPECT_FALSE(s.pending_choice.active());  // never paused
  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Bash);
}

TEST(ChoiceCards, HeadbuttWithAnEmptyDiscardStillDealsDamage) {
  CombatState s = make_minimal_state(0);
  const int hp = s.enemies[0].hp;
  ASSERT_TRUE(s.discard_pile.empty());

  ASSERT_TRUE(play(s, CardId::Headbutt));

  EXPECT_EQ(s.enemies[0].hp, hp - 9);
  EXPECT_FALSE(s.pending_choice.active());
}

TEST(ChoiceCards, ExhumeRetrievesFromExhaustAndCannotRetrieveItself) {
  CombatState s = make_minimal_state(0);
  s.exhaust_pile.push_back(Card{CardId::Bash});
  s.exhaust_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Exhume));

  ASSERT_TRUE(s.pending_choice.active());
  // Exhume is in flight (already out of hand) when the choice is built, so it
  // is not among its own options.
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    EXPECT_NE(s.pending_choice.options[i].card_id, CardId::Exhume);
  }
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 1));  // Bash

  EXPECT_EQ(s.current_hand.back().card_id, CardId::Bash);
  // Exhume itself exhausts.
  bool exhume_exhausted = false;
  for (const Card& c : s.exhaust_pile) {
    if (c.card_id == CardId::Exhume) exhume_exhausted = true;
  }
  EXPECT_TRUE(exhume_exhausted);
}

TEST(ChoiceCards, DualWieldAddsOneCopyAndThePlusAddsTwo) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Bash});

  ASSERT_TRUE(play(s, CardId::DualWield));
  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.copies, 1);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 0));  // copy the Strike

  int strikes = 0;
  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::Strike) strikes++;
  }
  EXPECT_EQ(strikes, 2);  // original + 1 copy

  // The upgraded version adds two.
  CombatState s2 = make_minimal_state(0);
  s2.current_hand.push_back(Card{CardId::Strike});
  s2.current_hand.push_back(Card{CardId::Bash});
  ASSERT_TRUE(play(s2, CardId::DualWieldPlus));
  ASSERT_TRUE(s2.pending_choice.active());
  EXPECT_EQ(s2.pending_choice.copies, 2);
  ASSERT_TRUE(apply_action(s2, kFirstOptionSlot + 0));

  int strikes2 = 0;
  for (const Card& c : s2.current_hand) {
    if (c.card_id == CardId::Strike) strikes2++;
  }
  EXPECT_EQ(strikes2, 3);  // original + 2 copies
}

TEST(ChoiceCards, DualWieldOnlyOffersAttacksAndPowers) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});   // Attack
  s.current_hand.push_back(Card{CardId::Inflame});  // Power
  s.current_hand.push_back(Card{CardId::Defend});   // Skill: excluded

  ASSERT_TRUE(play(s, CardId::DualWield));

  ASSERT_TRUE(s.pending_choice.active());
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    const CardType t = CARD_DATABASE.at(s.pending_choice.options[i].card_id).type;
    EXPECT_TRUE(t == CardType::Attack || t == CardType::Power);
    EXPECT_NE(s.pending_choice.options[i].card_id, CardId::Defend);
  }
}

TEST(ChoiceCards, CopiesOverflowToDiscardWhenTheHandIsFull) {
  // StS: "if a copy surpasses the hand size limit, it goes to the discard
  // pile" — verified for Dual Wield.
  CombatState s = make_minimal_state(0);
  for (int i = 0; i < HAND_SIZE_LIMIT - 1; ++i) {
    s.current_hand.push_back(Card{CardId::Strike});
  }
  s.current_hand.push_back(Card{CardId::Bash});  // hand now at the limit
  ASSERT_EQ(static_cast<int>(s.current_hand.size()), HAND_SIZE_LIMIT);

  // Playing Dual Wield frees one slot (it leaves the hand), so one copy fits
  // and the second (DualWield+) must overflow.
  ASSERT_TRUE(play(s, CardId::DualWieldPlus));
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 0));

  EXPECT_LE(static_cast<int>(s.current_hand.size()), HAND_SIZE_LIMIT);
  EXPECT_FALSE(s.discard_pile.empty()) << "overflow copy must go to discard";
}

TEST(ChoiceCards, PlayingAChoiceCardMasksOffCombatUntilAnswered) {
  // The agent cannot keep playing cards while a choice is open.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Armaments));
  ASSERT_TRUE(s.pending_choice.active());

  const auto mask = valid_actions(s);
  EXPECT_FALSE(mask[kEndTurnAction]);
  EXPECT_FALSE(mask[static_cast<int>(CardId::Strike) * kMaxEnemies]);
  EXPECT_TRUE(mask[kFirstOptionSlot + 0]);
}

TEST(ChoiceCards, ChoiceCardPauseSurvivesCloneEndToEnd) {
  // The MCTS property, but through the real card path rather than a synthetic
  // queue: clone a paused state and resolve the branches differently.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  ASSERT_TRUE(play(s, CardId::Armaments));
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_EQ(s.pending_choice.num_options, 2);

  CombatState a = s.clone();
  CombatState b = s.clone();
  ASSERT_TRUE(apply_action(a, kFirstOptionSlot + 0));  // upgrade Strike
  ASSERT_TRUE(apply_action(b, kFirstOptionSlot + 1));  // upgrade Defend

  EXPECT_EQ(a.current_hand[0].card_id, CardId::StrikePlus);
  EXPECT_EQ(a.current_hand[1].card_id, CardId::Defend);
  EXPECT_EQ(b.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(b.current_hand[1].card_id, CardId::DefendPlus);
  EXPECT_TRUE(s.pending_choice.active());  // parent untouched
}

// ============================================================================
// Per-instance card state: Rampage and Searing Blow.
// The point of these tests is that two copies of the SAME card can differ.
// ============================================================================

TEST(PerInstance, RampageGrowsTheCopyThatWasPlayed) {
  CombatState s = make_minimal_state(0);
  const int hp0 = s.enemies[0].hp;

  // First play: base 8.
  ASSERT_TRUE(play(s, CardId::Rampage));
  EXPECT_EQ(s.enemies[0].hp, hp0 - 8);

  // The copy went to the discard carrying its growth — as a RUNG ID now
  // (ROB-87), not a hidden counter, so it is visible in the observation and
  // playable as its own action.
  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Rampage5);
  EXPECT_EQ(s.discard_pile[0].bonus_damage, 0)
      << "below the cap the counter is unused";

  // Play that same copy again: 8 + 5.
  s.current_hand.push_back(s.discard_pile[0]);
  s.discard_pile.clear();
  s.character.energy = 3;
  const int hp1 = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Rampage5) * kMaxEnemies));
  EXPECT_EQ(s.enemies[0].hp, hp1 - 13);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Rampage10);  // grew again
}

TEST(PerInstance, RampagePlusGrowsByEight) {
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(play(s, CardId::RampagePlus));
  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::RampagePlus8);
}

TEST(PerInstance, RampageSaturatesAtTheTopRung) {
  // Past the cap the id stops moving but the engine keeps counting, so damage
  // stays exact where only the ENCODING saturates.
  CombatState s = make_minimal_state(0);
  s.character.energy = 5;
  s.current_hand.push_back(Card{CardId::Rampage30});  // top of the base ladder
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Rampage30) * kMaxEnemies));

  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Rampage30) << "id saturates";
  EXPECT_EQ(s.discard_pile[0].bonus_damage, 5) << "overflow keeps accumulating";
  EXPECT_EQ(instance_card_damage(s, s.discard_pile[0]), 43)
      << "38 (rung) + 5 (overflow) — damage is still exact above the cap";
}

TEST(PerInstance, TwoRampagesScaleSeparately) {
  // "Each copy scales separately" (wiki). Under ROB-87 the copies diverge in
  // IDENTITY, which is what makes the difference visible to the agent — the
  // aliasing this design was adopted to remove.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  s.current_hand.push_back(Card{CardId::Rampage});
  s.current_hand.push_back(Card{CardId::Rampage});

  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Rampage) * kMaxEnemies));

  // One copy grew; the one still in hand did not.
  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Rampage5);
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Rampage);
}

TEST(PerInstance, SearingBlowDamageFollowsTheWikiProgression) {
  // n(n+7)/2 + 12, verified against the published table.
  const int want[] = {12, 16, 21, 27, 34, 42, 51, 61, 72, 84, 97};
  CombatState s = make_minimal_state(0);
  for (int n = 0; n <= 10; ++n) {
    Card c{CardId::SearingBlow};
    c.upgrades = n;
    EXPECT_EQ(instance_card_damage(s, c), want[n]) << "at " << n << " upgrades";
  }
}

TEST(PerInstance, SearingBlowDealsItsInstanceDamage) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  Card blow{CardId::SearingBlow};
  blow.upgrades = 3;  // 27 damage
  s.current_hand.push_back(blow);
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(
      apply_action(s, static_cast<int>(CardId::SearingBlow) * kMaxEnemies));

  EXPECT_EQ(s.enemies[0].hp, hp - 27);
  // The upgrade count rode back into the discard pile with the copy.
  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].upgrades, 3);
}

TEST(PerInstance, ArmamentsClimbsTheSearingBlowLadder) {
  // Armaments moves the card one rung UP its ladder (ROB-87) — an ordinary id
  // swap now, not a counter bump on an unchanged id.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::SearingBlow2});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Armaments));
  ASSERT_TRUE(s.pending_choice.active());
  int slot = -1;
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == CardId::SearingBlow2) slot = i;
  }
  ASSERT_GE(slot, 0);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + slot));

  bool found = false;
  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::SearingBlow3) {
      EXPECT_EQ(c.upgrades, 0) << "below the cap the counter stays unused";
      found = true;
    }
  }
  EXPECT_TRUE(found) << "Searing Blow+2 should have become Searing Blow+3";
}

TEST(PerInstance, ArmamentsStillUpgradesAtTheSearingBlowCap) {
  // The parity clause end to end: Armaments must keep OFFERING a capped Searing
  // Blow ("can be upgraded any number of times"), and the upgrade must still
  // raise its damage even though the id can no longer move.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::SearingBlow5});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Armaments));
  ASSERT_TRUE(s.pending_choice.active());
  int slot = -1;
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == CardId::SearingBlow5) slot = i;
  }
  ASSERT_GE(slot, 0) << "a capped Searing Blow must still be offered";
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + slot));

  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::SearingBlow5) {
      EXPECT_EQ(c.upgrades, 1);
      EXPECT_EQ(instance_card_damage(s, c), 51) << "rung 6 on the progression";
    }
  }
}

TEST(PerInstance, ChoiceOffersDifferingCopiesSeparately) {
  // Two Rampages at different bonuses are NOT interchangeable, so they must be
  // two distinct options — the dedup keys on (id, instance state).
  CombatState s = make_minimal_state(0);
  Card weak{CardId::Rampage};
  Card strong{CardId::Rampage};
  strong.bonus_damage = 10;
  s.current_hand.push_back(weak);
  s.current_hand.push_back(strong);

  const PendingChoice pc =
      build_choice(s, ChoiceKind::HandToTopOfDraw, kSourceStandIn);

  ASSERT_EQ(pc.num_options, 2);
  EXPECT_EQ(pc.options[0].bonus_damage, 0);   // ascending instance state
  EXPECT_EQ(pc.options[1].bonus_damage, 10);
}

TEST(PerInstance, ChoiceStillCollapsesIdenticalCopies) {
  // Identical copies remain interchangeable and collapse to one option.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Rampage});
  s.current_hand.push_back(Card{CardId::Rampage});

  const PendingChoice pc =
      build_choice(s, ChoiceKind::HandToTopOfDraw, kSourceStandIn);

  EXPECT_EQ(pc.num_options, 1);
}

TEST(PerInstance, ChoiceTakesTheCopyThatWasChosen) {
  // Picking the buffed Rampage must move THAT copy, not an arbitrary one.
  CombatState s = make_minimal_state(0);
  Card weak{CardId::Rampage};
  Card strong{CardId::Rampage};
  strong.bonus_damage = 10;
  s.current_hand.push_back(weak);
  s.current_hand.push_back(strong);

  request_choice_then_marker(s, ChoiceKind::HandToTopOfDraw, kSourceStandIn);
  ASSERT_EQ(s.pending_choice.num_options, 2);
  ASSERT_TRUE(resolve_choice(s, 1));  // the +10 copy

  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile.back().bonus_damage, 10);  // the chosen one moved
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].bonus_damage, 0);    // the other stayed
}

TEST(PerInstance, DualWieldCopiesInheritInstanceState) {
  CombatState s = make_minimal_state(0);
  Card strong{CardId::Rampage};
  strong.bonus_damage = 10;
  s.current_hand.push_back(strong);
  s.current_hand.push_back(Card{CardId::Bash});

  ASSERT_TRUE(play(s, CardId::DualWield));
  ASSERT_TRUE(s.pending_choice.active());
  int slot = -1;
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == CardId::Rampage) slot = i;
  }
  ASSERT_GE(slot, 0);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + slot));

  int buffed = 0;
  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::Rampage && c.bonus_damage == 10) buffed++;
  }
  EXPECT_EQ(buffed, 2) << "the copy should inherit the +10";
}

TEST(PerInstance, ObsPublishesPerOptionInstanceDamage) {
  // Two Rampages at different bonuses must be distinguishable in the OBS, not
  // just in the mask — otherwise the agent sees "2 Rampage" and cannot choose.
  CombatState s = make_minimal_state(0);
  Card weak{CardId::Rampage};
  Card strong{CardId::Rampage};
  strong.bonus_damage = 10;
  s.current_hand.push_back(weak);
  s.current_hand.push_back(strong);
  s.pending_choice = build_choice(s, ChoiceKind::HandToTopOfDraw,
                                  kSourceStandIn);
  ASSERT_EQ(s.pending_choice.num_options, 2);

  CombatEnv env(std::move(s), 0.0f);
  const auto obs = env.obs();
  const int stride = CombatEnv::kChoiceSlotStride;

  // Same card id in both slots...
  EXPECT_FLOAT_EQ(obs[kSlotBase + 0 * stride + 1],
                  static_cast<float>(static_cast<int>(CardId::Rampage)));
  EXPECT_FLOAT_EQ(obs[kSlotBase + 1 * stride + 1],
                  static_cast<float>(static_cast<int>(CardId::Rampage)));
  // ...but different damage payloads.
  EXPECT_FLOAT_EQ(obs[kSlotBase + 0 * stride + 2], 8.0f);
  EXPECT_FLOAT_EQ(obs[kSlotBase + 1 * stride + 2], 18.0f);
}

// ============================================================================
// Card-generating cards. StS is specific about WHERE the generated card goes,
// and the difference is strategically real: a shuffled card can be drawn this
// combat, a discarded one cannot until reshuffle, one in hand clogs it now.
// ============================================================================

TEST(GeneratedCards, WildStrikeShufflesAWoundIntoTheDrawPile) {
  CombatState s = make_minimal_state(0);
  for (int i = 0; i < 5; ++i) s.draw_pile.push_back(Card{CardId::Strike});
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::WildStrike));

  EXPECT_EQ(s.enemies[0].hp, hp - 12);
  EXPECT_EQ(s.draw_pile.size(), 6u);  // the Wound went into the DRAW pile
  int wounds = 0;
  for (const Card& c : s.draw_pile) {
    if (c.card_id == CardId::Wound) wounds++;
  }
  EXPECT_EQ(wounds, 1);
  // Not in hand or discard.
  for (const Card& c : s.current_hand) EXPECT_NE(c.card_id, CardId::Wound);
}

TEST(GeneratedCards, PowerThroughAddsTwoWoundsToHand) {
  CombatState s = make_minimal_state(0);

  ASSERT_TRUE(play(s, CardId::PowerThrough));

  EXPECT_EQ(s.character.current_block, 15);
  int wounds = 0;
  for (const Card& c : s.current_hand) {
    if (c.card_id == CardId::Wound) wounds++;
  }
  EXPECT_EQ(wounds, 2) << "Power Through's Wounds go to HAND";
  EXPECT_TRUE(s.draw_pile.empty());
}

TEST(GeneratedCards, ImmolateAddsABurnToTheDiscardPile) {
  CombatState s = make_minimal_state(0);
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::Immolate));

  EXPECT_EQ(s.enemies[0].hp, hp - 21);
  int burns = 0;
  for (const Card& c : s.discard_pile) {
    if (c.card_id == CardId::Burn) burns++;
  }
  EXPECT_EQ(burns, 1) << "Immolate's Burn goes to the DISCARD pile";
}

TEST(GeneratedCards, RecklessChargeShufflesADazedIntoTheDrawPile) {
  CombatState s = make_minimal_state(0);
  for (int i = 0; i < 3; ++i) s.draw_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::RecklessCharge));

  int dazed = 0;
  for (const Card& c : s.draw_pile) {
    if (c.card_id == CardId::Dazed) dazed++;
  }
  EXPECT_EQ(dazed, 1);
}

TEST(GeneratedCards, AngerAddsACopyOfItselfToTheDiscard) {
  CombatState s = make_minimal_state(0);
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::Anger));

  EXPECT_EQ(s.enemies[0].hp, hp - 6);
  // Two Angers in the discard: the played one and its copy.
  int angers = 0;
  for (const Card& c : s.discard_pile) {
    if (c.card_id == CardId::Anger) angers++;
  }
  EXPECT_EQ(angers, 2);
}

TEST(GeneratedCards, WoundAndBurnAreUnplayable) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  s.current_hand.push_back(Card{CardId::Wound});
  s.current_hand.push_back(Card{CardId::Burn});

  const auto mask = valid_actions(s);
  EXPECT_FALSE(mask[static_cast<int>(CardId::Wound) * kMaxEnemies]);
  EXPECT_FALSE(mask[static_cast<int>(CardId::Burn) * kMaxEnemies]);
  // And the apply path agrees with the mask.
  EXPECT_FALSE(apply_action(s, static_cast<int>(CardId::Wound) * kMaxEnemies));
}

TEST(GeneratedCards, BurnDamagesThePlayerAtEndOfTurnAndThenDiscards) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Burn});
  s.enemies[0].hp = 0;  // no enemy attack, so Burn is the only damage source
  const int hp = s.character.hp;

  ASSERT_TRUE(apply_action(s, kEndTurnAction));

  // 2 damage from Burn (the fight ends as a win, so no enemy phase damage).
  EXPECT_EQ(s.character.hp, hp - 2);
}

TEST(GeneratedCards, BurnDamageIsAbsorbedByBlock) {
  // StS calls it damage, not HP loss ("unblocked damage from Burn"), so block
  // stops it.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Burn});
  s.character.current_block = 10;
  s.enemies[0].hp = 0;
  const int hp = s.character.hp;

  ASSERT_TRUE(apply_action(s, kEndTurnAction));

  EXPECT_EQ(s.character.hp, hp) << "block should absorb Burn";
}

TEST(GeneratedCards, BurnOnlyTicksWhileItIsInHand) {
  // A Burn sitting in the discard pile does nothing.
  CombatState s = make_minimal_state(0);
  s.discard_pile.push_back(Card{CardId::Burn});
  s.enemies[0].hp = 0;
  const int hp = s.character.hp;

  ASSERT_TRUE(apply_action(s, kEndTurnAction));

  EXPECT_EQ(s.character.hp, hp);
}

// ============================================================================
// Simple new mechanisms: random per-hit targeting, Strength multiplication,
// and an intent-conditional buff.
// ============================================================================

TEST(SimpleMechanisms, LimitBreakDoublesStrength) {
  CombatState s = make_minimal_state(0);
  s.character.powers[Power::Strength] = 4;

  ASSERT_TRUE(play(s, CardId::LimitBreak));

  EXPECT_EQ(s.character.powers[Power::Strength], 8);
  // The base version exhausts; the + does not.
  EXPECT_EQ(s.exhaust_pile.size(), 1u);
}

TEST(SimpleMechanisms, LimitBreakPlusDoesNotExhaust) {
  CombatState s = make_minimal_state(0);
  s.character.powers[Power::Strength] = 3;

  ASSERT_TRUE(play(s, CardId::LimitBreakPlus));

  EXPECT_EQ(s.character.powers[Power::Strength], 6);
  EXPECT_TRUE(s.exhaust_pile.empty());
  EXPECT_EQ(s.discard_pile.size(), 1u);
}

TEST(SimpleMechanisms, LimitBreakDoublesNegativeStrengthToo) {
  // Multiplying keeps the sign, so Limit Break after a Disarm makes it worse.
  CombatState s = make_minimal_state(0);
  s.character.powers[Power::Strength] = -3;

  ASSERT_TRUE(play(s, CardId::LimitBreak));

  EXPECT_EQ(s.character.powers[Power::Strength], -6);
}

TEST(SimpleMechanisms, LimitBreakOnZeroStrengthStaysZero) {
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(play(s, CardId::LimitBreak));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
}

TEST(SimpleMechanisms, SpotWeaknessGrantsStrengthWhenTheTargetAttacks) {
  CombatState s = make_minimal_state(0);
  // make_minimal_state's Jaw Worm is primed with an attacking intent.
  const auto& e = s.enemies[0];
  ASSERT_TRUE(e.last_move.has_value());
  ASSERT_GT(e.moves.at(*e.last_move).damage, 0) << "fixture must be attacking";

  ASSERT_TRUE(play(s, CardId::SpotWeakness));

  EXPECT_EQ(s.character.powers[Power::Strength], 3);
}

TEST(SimpleMechanisms, SpotWeaknessGrantsNothingWhenTheTargetIsNotAttacking) {
  CombatState s = make_minimal_state(0);
  // Point the enemy's intent at a non-damaging move.
  Move buff{MoveName::Bellow, 0, 0, {}};
  s.enemies[0].moves[MoveName::Bellow] = buff;
  s.enemies[0].last_move = MoveName::Bellow;

  ASSERT_TRUE(play(s, CardId::SpotWeakness));

  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
}

TEST(SimpleMechanisms, SwordBoomerangHitsThreeTimes) {
  // One enemy: all three hits land on it, so total damage is 3 x 3.
  CombatState s = make_minimal_state(0);
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::SwordBoomerang));

  EXPECT_EQ(s.enemies[0].hp, hp - 9);
}

TEST(SimpleMechanisms, SwordBoomerangPlusHitsFourTimes) {
  CombatState s = make_minimal_state(0);
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::SwordBoomerangPlus));

  EXPECT_EQ(s.enemies[0].hp, hp - 12);
}

TEST(SimpleMechanisms, SwordBoomerangAppliesStrengthPerHit) {
  // It is an ATTACK, so Strength applies to each of the three hits.
  CombatState s = make_minimal_state(0);
  s.character.powers[Power::Strength] = 2;
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::SwordBoomerang));

  EXPECT_EQ(s.enemies[0].hp, hp - 3 * (3 + 2));
}

TEST(SimpleMechanisms, SwordBoomerangSpreadsAcrossMultipleEnemies) {
  // With several enemies the hits are distributed randomly, so total damage
  // across all of them still equals hits x damage.
  CombatState s = make_minimal_state(0);
  std::mt19937 rng(7);
  s.enemies.push_back(make_jaw_worm(rng));
  s.enemies.push_back(make_jaw_worm(rng));
  int total_before = 0;
  for (const Enemy& e : s.enemies) total_before += e.hp;

  ASSERT_TRUE(play(s, CardId::SwordBoomerang));

  int total_after = 0;
  for (const Enemy& e : s.enemies) total_after += e.hp;
  EXPECT_EQ(total_before - total_after, 9) << "3 hits x 3 damage, spread";
}

TEST(SimpleMechanisms, SwordBoomerangIsDeterministicForASeed) {
  // Its random targeting draws from the seeded stream, so the same seed must
  // produce the same distribution.
  auto run = [](uint32_t seed) {
    CombatState s = make_minimal_state(seed);
    std::mt19937 rng(seed);
    s.enemies.push_back(make_jaw_worm(rng));
    s.enemies.push_back(make_jaw_worm(rng));
    EXPECT_TRUE(play(s, CardId::SwordBoomerang));
    std::vector<int> hps;
    for (const Enemy& e : s.enemies) hps.push_back(e.hp);
    return hps;
  };
  EXPECT_EQ(run(42), run(42));
}

// ============================================================================
// Exhaust-driven cards: True Grit, Burning Pact, Second Wind, Fiend Fire,
// Sentinel. What unites them is that exhausting is the EFFECT, not a cost.
// ============================================================================

TEST(ExhaustCards, TrueGritExhaustsARandomCardAndGainsBlock) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::TrueGrit));

  EXPECT_EQ(s.character.current_block, 7);
  EXPECT_EQ(s.exhaust_pile.size(), 1u);   // one random card left the hand
  EXPECT_EQ(s.current_hand.size(), 1u);
  EXPECT_FALSE(s.pending_choice.active());  // random, so no prompt
}

TEST(ExhaustCards, TrueGritPlusLetsYouChooseWhichCardToExhaust) {
  // The upgrade changes the choice's SHAPE (random -> chosen), not a number.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::TrueGritPlus));

  EXPECT_EQ(s.character.current_block, 9);
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_EQ(s.pending_choice.kind, ChoiceKind::ExhaustCardInHand);
  // Pick the Defend.
  int slot = -1;
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == CardId::Defend) slot = i;
  }
  ASSERT_GE(slot, 0);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + slot));

  ASSERT_EQ(s.exhaust_pile.size(), 1u);
  EXPECT_EQ(s.exhaust_pile[0].card_id, CardId::Defend);
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);  // the other stayed
}

TEST(ExhaustCards, TrueGritWithAnEmptyHandExhaustsNothing) {
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(play(s, CardId::TrueGrit));  // hand holds only True Grit itself
  EXPECT_EQ(s.character.current_block, 7);
  EXPECT_TRUE(s.exhaust_pile.empty());
}

TEST(ExhaustCards, BurningPactExhaustsAChosenCardThenDraws) {
  CombatState s = make_minimal_state(0);
  for (int i = 0; i < 5; ++i) s.draw_pile.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Bash});

  ASSERT_TRUE(play(s, CardId::BurningPact));
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_EQ(s.pending_choice.kind, ChoiceKind::ExhaustCardInHand);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 0));

  EXPECT_EQ(s.exhaust_pile.size(), 1u);
  // Started with 2, exhausted 1, drew 2 => 3.
  EXPECT_EQ(s.current_hand.size(), 3u);
}

TEST(ExhaustCards, BurningPactPlusDrawsThree) {
  CombatState s = make_minimal_state(0);
  for (int i = 0; i < 5; ++i) s.draw_pile.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Bash});

  ASSERT_TRUE(play(s, CardId::BurningPactPlus));
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + 0));

  EXPECT_EQ(s.current_hand.size(), 4u);  // 2 - 1 exhausted + 3 drawn
}

TEST(ExhaustCards, SecondWindExhaustsNonAttacksAndBlocksPerCard) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Defend});   // Skill -> exhausted
  s.current_hand.push_back(Card{CardId::Inflame});  // Power -> exhausted
  s.current_hand.push_back(Card{CardId::Strike});   // Attack -> kept

  ASSERT_TRUE(play(s, CardId::SecondWind));

  EXPECT_EQ(s.exhaust_pile.size(), 2u);
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(s.character.current_block, 10);  // 2 exhausted x 5
}

TEST(ExhaustCards, SecondWindPlusBlocksSevenPerCard) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::SecondWindPlus));

  EXPECT_EQ(s.character.current_block, 14);  // 2 x 7
}

TEST(ExhaustCards, SecondWindWithNoNonAttacksGivesNoBlock) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::SecondWind));

  EXPECT_EQ(s.character.current_block, 0);
  EXPECT_TRUE(s.exhaust_pile.empty());
}

TEST(ExhaustCards, FiendFireExhaustsTheWholeHandAndScalesDamage) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Inflame});
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::FiendFire));

  EXPECT_TRUE(s.current_hand.empty()) << "Fiend Fire exhausts the WHOLE hand";
  EXPECT_EQ(s.enemies[0].hp, hp - 3 * 7);
  // 3 hand cards plus Fiend Fire itself (it exhausts on play).
  EXPECT_EQ(s.exhaust_pile.size(), 4u);
}

TEST(ExhaustCards, FiendFirePlusDealsTenPerCard) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::FiendFirePlus));

  EXPECT_EQ(s.enemies[0].hp, hp - 2 * 10);
}

TEST(ExhaustCards, FiendFireWithAnEmptyHandDealsNothing) {
  CombatState s = make_minimal_state(0);
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::FiendFire));

  EXPECT_EQ(s.enemies[0].hp, hp);
}

TEST(ExhaustCards, SentinelGivesNoEnergyWhenSimplyPlayed) {
  // "If this card is Exhausted" — playing it normally discards it instead.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;

  ASSERT_TRUE(play(s, CardId::Sentinel));

  EXPECT_EQ(s.character.current_block, 5);
  EXPECT_EQ(s.character.energy, 2);  // paid 1, gained none back
  EXPECT_EQ(s.discard_pile.size(), 1u);
  EXPECT_TRUE(s.exhaust_pile.empty());
}

TEST(ExhaustCards, SentinelGivesEnergyWhenExhaustedByAnotherCard) {
  // Fiend Fire exhausts the hand, which triggers Sentinel's energy.
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Sentinel});
  s.character.energy = 3;

  ASSERT_TRUE(play(s, CardId::FiendFire));  // costs 2

  // 3 - 2 for Fiend Fire, + 2 from the exhausted Sentinel.
  EXPECT_EQ(s.character.energy, 3);
}

TEST(ExhaustCards, SentinelPlusGivesThreeEnergyWhenExhausted) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::SentinelPlus});
  s.character.energy = 3;

  ASSERT_TRUE(play(s, CardId::FiendFire));

  EXPECT_EQ(s.character.energy, 4);  // 3 - 2 + 3
}

TEST(ExhaustCards, SentinelTriggersUnderCorruption) {
  // Corruption exhausts every Skill played, so it turns Sentinel's own play
  // into an exhaust — the classic interaction.
  CombatState s = make_minimal_state(0);
  s.character.powers[Power::Corruption] = 1;
  s.character.energy = 3;

  ASSERT_TRUE(play(s, CardId::Sentinel));

  EXPECT_EQ(s.character.current_block, 5);
  EXPECT_EQ(s.exhaust_pile.size(), 1u);  // Corruption exhausted it
  // Corruption made it free, and the exhaust paid 2 energy.
  EXPECT_EQ(s.character.energy, 5);
}

TEST(ExhaustCards, ExhaustsFromTheseCardsFeedFeelNoPain) {
  // The exhausts are real actions, so the exhaust hooks see them.
  CombatState s = make_minimal_state(0);
  s.character.powers[Power::FeelNoPain] = 3;
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::SecondWind));

  // 2 x 5 from Second Wind itself, plus 2 x 3 from Feel No Pain.
  EXPECT_EQ(s.character.current_block, 10 + 6);
}

// ============================================================================
// Life-total cards: Feed and Reaper — the first cards that heal or change
// max HP, so they exercise paths the engine did not previously have.
// ============================================================================

TEST(LifeTotal, ReaperHealsTheUnblockedDamageItDealt) {
  CombatState s = make_minimal_state(0);
  s.character.hp = 40;  // room to heal
  const int hp = s.character.hp;

  ASSERT_TRUE(play(s, CardId::Reaper));

  // One enemy, no block: all 4 damage lands, so heal 4.
  EXPECT_EQ(s.character.hp, hp + 4);
}

TEST(LifeTotal, ReaperOnlyHealsUNBLOCKEDDamage) {
  CombatState s = make_minimal_state(0);
  s.character.hp = 40;
  s.enemies[0].current_block = 3;  // absorbs 3 of the 4
  const int hp = s.character.hp;

  ASSERT_TRUE(play(s, CardId::Reaper));

  EXPECT_EQ(s.character.hp, hp + 1) << "only the 1 unblocked point heals";
}

TEST(LifeTotal, ReaperHealsTheSumAcrossAllEnemies) {
  // It is AoE, so the heal is the total unblocked damage.
  CombatState s = make_minimal_state(0);
  s.character.hp = 40;
  std::mt19937 rng(3);
  s.enemies.push_back(make_jaw_worm(rng));
  s.enemies.push_back(make_jaw_worm(rng));
  const int hp = s.character.hp;

  ASSERT_TRUE(play(s, CardId::Reaper));

  EXPECT_EQ(s.character.hp, hp + 12) << "3 enemies x 4 damage";
}

TEST(LifeTotal, ReaperCannotHealAboveMaxHp) {
  CombatState s = make_minimal_state(0);
  s.character.hp = s.character.max_hp;  // already full

  ASSERT_TRUE(play(s, CardId::Reaper));

  EXPECT_EQ(s.character.hp, s.character.max_hp);
}

TEST(LifeTotal, ReaperPlusDealsFive) {
  CombatState s = make_minimal_state(0);
  s.character.hp = 40;
  const int enemy_hp = s.enemies[0].hp;
  const int hp = s.character.hp;

  ASSERT_TRUE(play(s, CardId::ReaperPlus));

  EXPECT_EQ(s.enemies[0].hp, enemy_hp - 5);
  EXPECT_EQ(s.character.hp, hp + 5);
}

TEST(LifeTotal, ReaperHealsNothingWhenFullyBlocked) {
  CombatState s = make_minimal_state(0);
  s.character.hp = 40;
  s.enemies[0].current_block = 50;
  const int hp = s.character.hp;

  ASSERT_TRUE(play(s, CardId::Reaper));

  EXPECT_EQ(s.character.hp, hp);
}

TEST(LifeTotal, FeedRaisesMaxHpWhenItKills) {
  CombatState s = make_minimal_state(0);
  s.enemies[0].hp = 5;  // dies to Feed's 10
  const int max_hp = s.character.max_hp;
  const int hp = s.character.hp;

  ASSERT_TRUE(play(s, CardId::Feed));

  EXPECT_EQ(s.character.max_hp, max_hp + 3);
  EXPECT_EQ(s.character.hp, hp + 3) << "current HP rises with max HP";
}

TEST(LifeTotal, FeedGivesNothingWhenItDoesNotKill) {
  CombatState s = make_minimal_state(0);
  s.enemies[0].hp = 50;  // survives Feed's 10
  const int max_hp = s.character.max_hp;

  ASSERT_TRUE(play(s, CardId::Feed));

  EXPECT_EQ(s.character.max_hp, max_hp);
}

TEST(LifeTotal, FeedPlusRaisesMaxHpByFour) {
  CombatState s = make_minimal_state(0);
  s.enemies[0].hp = 5;
  const int max_hp = s.character.max_hp;

  ASSERT_TRUE(play(s, CardId::FeedPlus));

  EXPECT_EQ(s.character.max_hp, max_hp + 4);
}

TEST(LifeTotal, FeedExhausts) {
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(play(s, CardId::Feed));
  ASSERT_EQ(s.exhaust_pile.size(), 1u);
  EXPECT_EQ(s.exhaust_pile[0].card_id, CardId::Feed);
  EXPECT_TRUE(s.discard_pile.empty());
}

TEST(LifeTotal, FeedMaxHpGainSurvivesWinningTheFight) {
  // The kill that grants the HP also ends the combat; the gain must still be
  // recorded rather than lost to the terminal short-circuit.
  CombatState s = make_minimal_state(0);
  s.enemies[0].hp = 5;
  const int max_hp = s.character.max_hp;

  ASSERT_TRUE(play(s, CardId::Feed));

  EXPECT_EQ(s.outcome, Outcome::Won);
  EXPECT_EQ(s.character.max_hp, max_hp + 3);
}

// ============================================================================
// Meta-cards: Double Tap, Havoc, Infernal Blade. These cause OTHER cards to be
// played or generated — the re-entrant case the action queue exists for. Each
// nested play goes through a PlayCard ACTION, not a nested call, so no
// resolution is ever open while state mutates.
// ============================================================================

TEST(MetaCards, DoubleTapReplaysTheNextAttack) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::DoubleTap));
  ASSERT_EQ(get_status(s.character.powers, Power::DoubleTap), 1);

  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));

  EXPECT_EQ(s.enemies[0].hp, hp - 12) << "Strike's 6 damage, twice";
  EXPECT_EQ(get_status(s.character.powers, Power::DoubleTap), 0)
      << "the charge is consumed";
}

TEST(MetaCards, DoubleTapReplayCostsNoEnergy) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  ASSERT_TRUE(play(s, CardId::DoubleTap));  // costs 1
  const int energy = s.character.energy;

  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));

  // Strike costs 1; the free replay costs nothing.
  EXPECT_EQ(s.character.energy, energy - 1);
}

TEST(MetaCards, DoubleTapDoesNotAffectSkills) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  ASSERT_TRUE(play(s, CardId::DoubleTap));

  s.current_hand.push_back(Card{CardId::Defend});
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Defend) * kMaxEnemies));

  EXPECT_EQ(s.character.current_block, 5) << "Defend is a Skill, played once";
  EXPECT_EQ(get_status(s.character.powers, Power::DoubleTap), 1)
      << "the charge is not spent on a non-Attack";
}

TEST(MetaCards, DoubleTapPlusReplaysTwoAttacks) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 5;
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::DoubleTapPlus));
  ASSERT_EQ(get_status(s.character.powers, Power::DoubleTap), 2);

  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));
  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));

  EXPECT_EQ(s.enemies[0].hp, hp - 24) << "two Strikes, each played twice";
  EXPECT_EQ(get_status(s.character.powers, Power::DoubleTap), 0);
}

TEST(MetaCards, DoubleTapSecondCopyEntersNoPile) {
  // "Not added to your draw or discard pile" — only the first copy goes.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  ASSERT_TRUE(play(s, CardId::DoubleTap));
  s.discard_pile.clear();

  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));

  int strikes = 0;
  for (const Card& c : s.discard_pile) {
    if (c.card_id == CardId::Strike) strikes++;
  }
  EXPECT_EQ(strikes, 1) << "one Strike discarded, not two";
}

TEST(MetaCards, DoubleTapChargesExpireAtEndOfTurn) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  ASSERT_TRUE(play(s, CardId::DoubleTap));
  ASSERT_EQ(get_status(s.character.powers, Power::DoubleTap), 1);

  ASSERT_TRUE(apply_action(s, kEndTurnAction));

  EXPECT_EQ(get_status(s.character.powers, Power::DoubleTap), 0)
      << "\"this turn\" — unused charges are lost";
}

TEST(MetaCards, HavocPlaysTheTopCardOfTheDrawPile) {
  CombatState s = make_minimal_state(0);
  s.draw_pile.push_back(Card{CardId::Strike});  // back() is the top
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::Havoc));

  EXPECT_EQ(s.enemies[0].hp, hp - 6) << "the Strike resolved";
  EXPECT_TRUE(s.draw_pile.empty());
}

TEST(MetaCards, HavocForceExhaustsTheCardItPlays) {
  // "and Exhaust it" — even a card that would normally discard.
  CombatState s = make_minimal_state(0);
  s.draw_pile.push_back(Card{CardId::Strike});  // normally discards

  ASSERT_TRUE(play(s, CardId::Havoc));

  bool strike_exhausted = false;
  for (const Card& c : s.exhaust_pile) {
    if (c.card_id == CardId::Strike) strike_exhausted = true;
  }
  EXPECT_TRUE(strike_exhausted);
  for (const Card& c : s.discard_pile) {
    EXPECT_NE(c.card_id, CardId::Strike) << "must not also discard";
  }
}

TEST(MetaCards, HavocPlaysTheCardForFree) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  s.draw_pile.push_back(Card{CardId::Bash});  // normally costs 2

  ASSERT_TRUE(play(s, CardId::Havoc));  // Havoc itself costs 1

  EXPECT_EQ(s.character.energy, 2) << "only Havoc's own cost was paid";
}

TEST(MetaCards, HavocReshufflesWhenTheDrawPileIsEmpty) {
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(s.draw_pile.empty());
  s.discard_pile.push_back(Card{CardId::Strike});
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::Havoc));

  EXPECT_EQ(s.enemies[0].hp, hp - 6) << "the discard was shuffled in and played";
}

TEST(MetaCards, HavocWithNothingToPlayIsANoOp) {
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(s.draw_pile.empty());
  ASSERT_TRUE(s.discard_pile.empty());

  ASSERT_TRUE(play(s, CardId::Havoc));  // must not crash

  EXPECT_EQ(s.outcome, Outcome::InProgress);
}

TEST(MetaCards, HavocExhaustsAnUnplayableCardWithoutResolvingIt) {
  CombatState s = make_minimal_state(0);
  s.draw_pile.push_back(Card{CardId::Dazed});  // unplayable
  const int hp = s.enemies[0].hp;

  ASSERT_TRUE(play(s, CardId::Havoc));

  EXPECT_EQ(s.enemies[0].hp, hp);  // nothing resolved
  bool dazed_exhausted = false;
  for (const Card& c : s.exhaust_pile) {
    if (c.card_id == CardId::Dazed) dazed_exhausted = true;
  }
  EXPECT_TRUE(dazed_exhausted);
}

TEST(MetaCards, InfernalBladeAddsARandomAttackThatCostsZero) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;

  ASSERT_TRUE(play(s, CardId::InfernalBlade));

  ASSERT_EQ(s.current_hand.size(), 1u);
  const CardId got = s.current_hand[0].card_id;
  EXPECT_EQ(CARD_DATABASE.at(got).type, CardType::Attack);
  EXPECT_EQ(effective_cost(s, got), 0) << "costs 0 this turn";
}

TEST(MetaCards, InfernalBladesDiscountExpiresNextTurn) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  for (int i = 0; i < 10; ++i) s.draw_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::InfernalBlade));
  const CardId got = s.current_hand[0].card_id;
  ASSERT_EQ(effective_cost(s, got), 0);
  ASSERT_GT(s.character.free_this_turn.count(got), 0u);

  ASSERT_TRUE(apply_action(s, kEndTurnAction));

  // The DISCOUNT is gone. Asserted on the free-list rather than on
  // effective_cost returning the printed cost: a randomly-picked card may
  // legitimately still be discounted by something else (Blood for Blood costs
  // less per HP-loss event, and the enemy attacks during the end-turn), which
  // is engine-correct but would fail a printed-cost comparison. This is a real
  // platform-dependent failure the macOS run missed and Linux CI caught.
  EXPECT_EQ(s.character.free_this_turn.count(got), 0u);
}

TEST(MetaCards, InfernalBladesDiscountAppliesOnlyToTheGeneratedCard) {
  // The free-cost flag is keyed per card type, so an unrelated card in hand
  // keeps its printed cost.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  s.current_hand.push_back(Card{CardId::Bash});  // cost 2, not generated

  ASSERT_TRUE(play(s, CardId::InfernalBlade));

  if (s.character.free_this_turn.count(CardId::Bash) == 0) {
    EXPECT_EQ(effective_cost(s, CardId::Bash), 2);
  }
}

TEST(MetaCards, InfernalBladeExhausts) {
  CombatState s = make_minimal_state(0);
  ASSERT_TRUE(play(s, CardId::InfernalBlade));
  bool found = false;
  for (const Card& c : s.exhaust_pile) {
    if (c.card_id == CardId::InfernalBlade) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(MetaCards, MetaCardsAreDeterministicForASeed) {
  // Havoc's targeting and Infernal Blade's card pick both draw from the seeded
  // stream, so a seed must reproduce exactly.
  auto run = [](uint32_t seed) {
    CombatState s = make_minimal_state(seed);
    s.character.energy = 5;
    for (int i = 0; i < 6; ++i) s.draw_pile.push_back(Card{CardId::Strike});
    EXPECT_TRUE(play(s, CardId::InfernalBlade));
    EXPECT_TRUE(play(s, CardId::Havoc));
    std::vector<int> out{s.enemies[0].hp, s.character.energy,
                         static_cast<int>(s.current_hand.size())};
    for (const Card& c : s.current_hand) out.push_back(static_cast<int>(c.card_id));
    return out;
  };
  EXPECT_EQ(run(11), run(11));
}

TEST(MetaCards, DoubleTapReplaysThroughTheQueueNotRecursion) {
  // Regression guard for the architecture: the replay must leave the state
  // consistent even when the replayed attack kills the last enemy — which,
  // under nested resolution, is exactly where mutation-during-resolution bugs
  // used to appear.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  ASSERT_TRUE(play(s, CardId::DoubleTap));
  s.enemies[0].hp = 8;  // survives the first Strike, dies to the replay

  s.current_hand.push_back(Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));

  EXPECT_LE(s.enemies[0].hp, 0);
  EXPECT_EQ(s.outcome, Outcome::Won);
}

// ============================================================================
// ROB-85 wiki-audit regressions. Each of these pins a defect that the existing
// suite passed straight through — the fixes broke NO existing test, which is
// exactly why they needed new ones.
// ============================================================================

TEST(WikiAudit, InfernalBladeDiscountIsConsumedByOnePlay) {
  // The discount was keyed by CardId and never decremented, so "It costs 0 this
  // turn" applied to the whole card TYPE. With 5 Strikes in the starter deck,
  // rolling a Strike made every Strike free for the rest of the turn.
  //
  // Set the flag directly rather than playing Infernal Blade: its generated
  // attack is a random draw, and the bug only shows when the roll COLLIDES with
  // a card already in hand. The old test skipped its assertion on exactly that
  // case, which is how this survived.
  CombatState s = make_minimal_state(0);
  s.character.energy = 5;
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Strike});
  s.character.free_this_turn[CardId::Strike] = 1;

  const int start = s.character.energy;
  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));
  EXPECT_EQ(s.character.energy, start) << "the free copy costs nothing";

  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::Strike) * kMaxEnemies));
  EXPECT_EQ(s.character.energy, start - 1)
      << "only ONE copy was free; the second pays its printed cost";
}

TEST(WikiAudit, InfernalBladeDiscountStillCoversItsOwnCard) {
  // Guard the other direction: consuming the charge must not make the generated
  // card cost energy on its first play.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  ASSERT_TRUE(play(s, CardId::InfernalBlade));  // costs 1
  const int after_blade = s.character.energy;

  ASSERT_EQ(s.current_hand.size(), 1u);
  const CardId got = s.current_hand[0].card_id;
  ASSERT_EQ(effective_cost(s, got), 0);

  ASSERT_TRUE(apply_action(s, static_cast<int>(got) * kMaxEnemies));
  EXPECT_EQ(s.character.energy, after_blade) << "the generated attack was free";
}

TEST(WikiAudit, BurningPactChoosesFromThePreDrawHand) {
  // "Exhaust 1 card. Draw 2 cards." — the exhaust resolves FIRST, so a card the
  // same play drew must not be offerable. The choice used to queue after the
  // draw, letting the agent exhaust what it had just drawn.
  // Two candidates must be in hand up front: a lone legal option is auto-applied
  // without pausing (StS: "if there is only one card ... it will automatically
  // be placed"), which would hide the option set entirely.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  s.current_hand.push_back(Card{CardId::BurningPact});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Strike});
  // Distinct from each other and from the hand, so a leaked drawn card is
  // unambiguous rather than deduped into an existing option.
  s.draw_pile.push_back(Card{CardId::Cleave});
  s.draw_pile.push_back(Card{CardId::Bash});

  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::BurningPact) * kMaxEnemies));

  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.num_options, 2)
      << "only the pre-draw hand (Defend, Strike) is on offer";
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    const CardId id = s.pending_choice.options[i].card_id;
    EXPECT_NE(id, CardId::Bash) << "a just-drawn card leaked into the choice";
    EXPECT_NE(id, CardId::Cleave) << "a just-drawn card leaked into the choice";
  }
}

TEST(WikiAudit, BurningPactStillDrawsAfterTheExhaustResolves) {
  // Reordering must not cost the card its draw.
  CombatState s = make_minimal_state(0);
  s.character.energy = 3;
  s.current_hand.push_back(Card{CardId::BurningPact});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Strike});  // 2nd candidate -> real pause
  for (int i = 0; i < 4; ++i) s.draw_pile.push_back(Card{CardId::Bash});

  ASSERT_TRUE(apply_action(s, static_cast<int>(CardId::BurningPact) * kMaxEnemies));
  ASSERT_TRUE(s.pending_choice.active());
  // Answer with whichever slot offers the Defend.
  int defend_slot = -1;
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == CardId::Defend) defend_slot = i;
  }
  ASSERT_GE(defend_slot, 0);
  ASSERT_TRUE(apply_action(s, kFirstOptionSlot + defend_slot));

  EXPECT_FALSE(s.pending_choice.active());
  int bashes = 0;
  for (const Card& c : s.current_hand) bashes += (c.card_id == CardId::Bash);
  EXPECT_EQ(bashes, 2) << "the draw still happens, after the exhaust";
  bool defend_exhausted = false;
  for (const Card& c : s.exhaust_pile) {
    if (c.card_id == CardId::Defend) defend_exhausted = true;
  }
  EXPECT_TRUE(defend_exhausted);
}

TEST(WikiAudit, HavocExhaustingAnUnplayableCardFiresFeelNoPain) {
  // Havoc's unplayable branch called move_to_exhaust directly instead of
  // queueing an ExhaustCard action, so it silently skipped every
  // "whenever a card is Exhausted" trigger.
  CombatState s = make_minimal_state(0);
  s.character.energy = 5;
  ASSERT_TRUE(play(s, CardId::FeelNoPain));  // 3 block per exhaust
  ASSERT_EQ(s.character.current_block, 0);

  s.draw_pile.push_back(Card{CardId::Dazed});  // unplayable
  ASSERT_TRUE(play(s, CardId::Havoc));

  EXPECT_EQ(s.character.current_block, 3)
      << "\"Whenever a card is Exhausted, gain 3 Block\" is unconditional on "
         "cause";
  bool dazed_exhausted = false;
  for (const Card& c : s.exhaust_pile) {
    if (c.card_id == CardId::Dazed) dazed_exhausted = true;
  }
  EXPECT_TRUE(dazed_exhausted) << "and it still reaches the exhaust pile";
}

TEST(WikiAudit, HavocExhaustingAnUnplayableCardFiresDarkEmbrace) {
  CombatState s = make_minimal_state(0);
  s.character.energy = 5;
  ASSERT_TRUE(play(s, CardId::DarkEmbrace));  // draw 1 per exhaust
  s.draw_pile.push_back(Card{CardId::Strike});  // the card Dark Embrace draws
  s.draw_pile.push_back(Card{CardId::Dazed});   // top: unplayable, exhausts

  const std::size_t hand_before = s.current_hand.size();
  ASSERT_TRUE(play(s, CardId::Havoc));

  EXPECT_EQ(s.current_hand.size(), hand_before + 1)
      << "the exhaust triggered Dark Embrace's draw";
}

TEST(WikiAudit, SearingBlowPlusDealsItsUpgradedDamage) {
  // CardId::SearingBlowPlus is a real database row and action slot, but the
  // damage formula reads only Card::upgrades — so a default-constructed
  // instance dealt 12, the UNupgraded number, from a card named "+".
  CombatState s = make_minimal_state(0);
  EXPECT_EQ(instance_card_damage(s, Card{CardId::SearingBlowPlus}), 16)
      << "n=1 on the wiki progression 12/16/21/27/34/42";
  EXPECT_EQ(instance_card_damage(s, Card{CardId::SearingBlow}), 12);
}

TEST(WikiAudit, SearingBlowInstanceUpgradesStillDriveTheProgression) {
  // The instance path must be unaffected by the id-baseline fix.
  CombatState s = make_minimal_state(0);
  const int want[] = {12, 16, 21, 27, 34, 42};
  for (int n = 0; n < 6; ++n) {
    Card c{CardId::SearingBlow};
    c.upgrades = n;
    EXPECT_EQ(instance_card_damage(s, c), want[n]) << "at " << n << " upgrades";
  }
}
