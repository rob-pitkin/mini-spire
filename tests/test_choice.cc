#include <gtest/gtest.h>

#include "action.h"
#include "card.h"
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
