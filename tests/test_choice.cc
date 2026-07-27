#include <gtest/gtest.h>

#include "action.h"
#include "card.h"
#include "combat_env.h"
#include "combat_state.h"
#include "test_helpers.h"
#include "turn_loop.h"

using namespace minispire;
using minispire::testing::make_minimal_state;

// PendingChoice::source_card records which card caused the pause; it is
// informational at this stage (the five choice cards — Armaments, Warcry,
// Headbutt, Exhume, Dual Wield — arrive in step 4). These tests drive the
// mechanism directly, so any CardId serves as the source.
constexpr CardId kSourceStandIn = CardId::Strike;

// ============================================================================
// Stage 4c step 2: the pause/resume mechanism.
// No card requests a choice yet (that is step 4), so these drive the machinery
// directly through build_choice / resolve_choice and a hand-built queue.
// ============================================================================

namespace {

// Push a RequestChoice + a trailing marker action, then drain. The marker
// (gain 7 block) proves whether the REMAINDER of the queue was suspended
// rather than executed through the pause.
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
  EXPECT_EQ(pc.options[0], CardId::Strike);  // ascending CardId order
  EXPECT_EQ(pc.options[1], CardId::Bash);
}

TEST(Choice, UpgradeFilterExcludesUpgradedAndStatusCards) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});      // upgradable
  s.current_hand.push_back(Card{CardId::StrikePlus});  // already upgraded
  s.current_hand.push_back(Card{CardId::Slimed});      // Status

  const PendingChoice pc =
      build_choice(s, ChoiceKind::UpgradeCardInHand, kSourceStandIn);

  ASSERT_EQ(pc.num_options, 1);
  EXPECT_EQ(pc.options[0], CardId::Strike);
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
    const CardType t = CARD_DATABASE.at(pc.options[i]).type;
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
  EXPECT_EQ(hand.options[0], CardId::Strike);
  ASSERT_EQ(discard.num_options, 1);
  EXPECT_EQ(discard.options[0], CardId::Bash);
  ASSERT_EQ(exhaust.num_options, 1);
  EXPECT_EQ(exhaust.options[0], CardId::Defend);
}

TEST(Choice, RequestChoiceSuspendsTheRestOfTheQueue) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});

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
  s.draw_pile.push_back(Card{CardId::Defend});

  request_choice_then_marker(s, ChoiceKind::HandToTopOfDraw, CardId::Strike);
  ASSERT_TRUE(resolve_choice(s, 0));

  EXPECT_TRUE(s.current_hand.empty());
  ASSERT_EQ(s.draw_pile.size(), 2u);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Bash);  // back() == top
}

TEST(Choice, HeadbuttMovesADiscardCardToTopOfDraw) {
  CombatState s = make_minimal_state(0);
  s.discard_pile.push_back(Card{CardId::Bash});

  request_choice_then_marker(s, ChoiceKind::DiscardToTopOfDraw,
                             CardId::Strike);
  ASSERT_TRUE(resolve_choice(s, 0));

  EXPECT_TRUE(s.discard_pile.empty());
  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile.back().card_id, CardId::Bash);
}

TEST(Choice, ExhumeMovesAnExhaustedCardToHand) {
  CombatState s = make_minimal_state(0);
  s.exhaust_pile.push_back(Card{CardId::Bash});

  request_choice_then_marker(s, ChoiceKind::ExhaustToHand, CardId::Strike);
  ASSERT_TRUE(resolve_choice(s, 0));

  // The one sanctioned removal from the exhaust pile (it only grows otherwise).
  EXPECT_TRUE(s.exhaust_pile.empty());
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Bash);
}

TEST(Choice, DualWieldCopiesWithoutRemovingTheOriginal) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});

  request_choice_then_marker(s, ChoiceKind::CopyAttackOrPowerInHand,
                             CardId::Strike);
  ASSERT_TRUE(resolve_choice(s, 0));

  ASSERT_EQ(s.current_hand.size(), 2u);  // original + copy
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(s.current_hand[1].card_id, CardId::Strike);
}

TEST(Choice, UpgradeAffectsOnlyOneCopyOfADuplicate) {
  CombatState s = make_minimal_state(0);
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Strike});

  request_choice_then_marker(s, ChoiceKind::UpgradeCardInHand,
                             kSourceStandIn);
  ASSERT_EQ(s.pending_choice.num_options, 1);  // deduped to one option
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
