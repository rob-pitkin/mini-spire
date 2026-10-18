// Curse effects and the end of the player's turn (docs/design/curse-effects.md).
//
// Every test builds the state it needs; none depends on which card or move a
// random draw produced. Where an enemy's attack would land in the middle of a
// measurement, the turn is ended with end_turn_then_win, or compared against a
// twin that differs only in the card under test.

#include <gtest/gtest.h>

#include <random>

#include "action.h"
#include "card.h"
#include "combat_state.h"
#include "enemy.h"
#include "query.h"
#include "relic.h"
#include "test_helpers.h"
#include "turn_loop.h"

using namespace minispire;
using namespace minispire::testing;

namespace {

// HP 80/80, no block, one Jaw Worm with plenty of HP whose primed move is
// Chomp, so the enemy phase can run. Piles empty.
CombatState fight() {
  CombatState s = make_minimal_state(0);
  s.enemies[0].hp = 200;
  s.enemies[0].max_hp = 200;
  s.enemies[0].last_move = MoveName::Chomp;
  return s;
}

// Ends the turn with no enemy phase. Combust kills the one 1-HP enemy in the
// LAST end-of-turn phase, after the end-of-turn cards have played; with no
// casts it costs no HP. An enemy already at 0 HP would not do: the fight would
// be over before the end-of-turn cards play.
void end_turn_then_win(CombatState& s) {
  s.enemies[0].hp = 1;
  s.enemies[0].current_block = 0;
  s.character.powers[Power::Combust] = 1;
  s.character.combust_casts = 0;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::Won) << "Combust should have ended the fight";
}

int count_in(const std::vector<Card>& pile, CardId id) {
  int n = 0;
  for (const Card& c : pile) {
    if (c.card_id == id) ++n;
  }
  return n;
}

void lose_hp(CombatState& s, int amount) {
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::LoseHp};
  a.amount = amount;
  q.push_back(a);
  drain(s, q, ctx);
}

}  // namespace

// ============================================================================
// The order of the end of the turn (§1.1, §3.1)
// ============================================================================

// Regen heals in endTurn's atEndOfTurn, after Burn has played from the card
// queue. At full HP, healing first would waste the heal. Compared with a twin
// that has no Regen, through the enemy phase, so the test does not lean on
// Combust's place in the order: the heal is worth 2 if it comes after Burn,
// nothing if before.
TEST(EndOfTurnOrder, RegenHealsAfterBurnsDamage) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Burn});
  CombatState bare = s;
  s.character.powers[Power::Regen] = 5;

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_TRUE(apply_action(bare, kEndTurnAction));

  EXPECT_EQ(s.character.hp, bare.character.hp + 2)
      << "Regen healed before Burn's damage";
}

// Runic Cube's draw off Burn's damage resolves before the hand is discarded,
// so the drawn card goes with the hand (wiki Burn page).
TEST(EndOfTurnOrder, RunicCubesDrawFromBurnIsDiscardedWithTheHand) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::RunicCube, 0});
  s.current_hand.push_back(Card{CardId::Burn});
  for (int i = 0; i < 5; ++i) s.draw_pile.push_back(Card{CardId::Defend});

  end_turn_then_win(s);

  ASSERT_EQ(s.draw_pile.size(), 4u) << "Runic Cube did not draw";
  EXPECT_TRUE(s.current_hand.empty()) << "the drawn card was kept";
  EXPECT_EQ(count_in(s.discard_pile, CardId::Defend), 1);
  EXPECT_EQ(count_in(s.discard_pile, CardId::Burn), 1);
}

// The pre-card powers and the end-of-turn relics put their block up before
// Burn plays, so it absorbs Burn's damage.
TEST(EndOfTurnOrder, MetallicizeBlockLandsBeforeBurn) {
  CombatState s = fight();
  s.character.powers[Power::Metallicize] = 3;
  s.current_hand.push_back(Card{CardId::Burn});

  end_turn_then_win(s);

  EXPECT_EQ(s.character.hp, 80);
  EXPECT_EQ(s.character.current_block, 1);
}

TEST(EndOfTurnOrder, PlatedArmorBlockLandsBeforeBurn) {
  CombatState s = fight();
  s.character.powers[Power::PlatedArmor] = 3;
  s.current_hand.push_back(Card{CardId::Burn});

  end_turn_then_win(s);

  EXPECT_EQ(s.character.hp, 80);
  EXPECT_EQ(s.character.current_block, 1);
  EXPECT_EQ(get_status(s.character.powers, Power::PlatedArmor), 3);
}

TEST(EndOfTurnOrder, OrichalcumBlockLandsBeforeBurn) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::Orichalcum, 0});
  s.current_hand.push_back(Card{CardId::Burn});

  end_turn_then_win(s);

  EXPECT_EQ(s.character.hp, 80);
  EXPECT_EQ(s.character.current_block, 4);
}

// Stone Calendar ends the fight in the first phase, before the end-of-turn
// cards play: they are left unplayed, and the queue is not left holding them
// (card-queue.md, "Fight end").
TEST(EndOfTurnOrder, AFightWonBeforeTheEndOfTurnCardsPlayLeavesThemUnplayed) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::StoneCalendar, 0});
  s.turn_number = kStoneCalendarTurn;
  s.enemies[0].hp = 10;
  s.current_hand.push_back(Card{CardId::Burn});

  ASSERT_TRUE(apply_action(s, kEndTurnAction));

  EXPECT_EQ(s.outcome, Outcome::Won);
  EXPECT_EQ(s.character.hp, 80);
  EXPECT_TRUE(s.card_queue.empty());
}

// The end-of-turn cards are played with dontTriggerOnUseCard: they are not
// card plays. Ink Bottle counts every card played, and one more would draw.
TEST(EndOfTurnOrder, EndOfTurnCardsAreNotCardPlays) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::InkBottle, 9});
  s.current_hand.push_back(Card{CardId::Burn});
  s.current_hand.push_back(Card{CardId::Regret});

  end_turn_then_win(s);

  EXPECT_EQ(s.relics[0].counter, 9) << "an end-of-turn card counted as played";
}

// ============================================================================
// Regret
// ============================================================================

// Each Regret counts the hand as it was when the turn ended: itself, the other
// Regret, the Burn and the Strike. 4 + 4 from the Regrets, 2 from the Burn.
TEST(Regret, LosesHpEqualToTheWholeHandItselfIncluded) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Regret});
  s.current_hand.push_back(Card{CardId::Regret});
  s.current_hand.push_back(Card{CardId::Burn});
  s.current_hand.push_back(Card{CardId::Strike});

  end_turn_then_win(s);

  EXPECT_EQ(s.character.hp, 80 - 4 - 4 - 2);
  EXPECT_EQ(count_in(s.discard_pile, CardId::Regret), 2);
}

// HP loss, not damage: block does not absorb it.
TEST(Regret, IgnoresBlock) {
  CombatState s = fight();
  s.character.current_block = 10;
  s.current_hand.push_back(Card{CardId::Regret});
  s.current_hand.push_back(Card{CardId::Strike});

  end_turn_then_win(s);

  EXPECT_EQ(s.character.hp, 78);
  EXPECT_EQ(s.character.current_block, 10);
}

// Its HP loss is "from a card" (wiki Rupture page).
TEST(Regret, FiresRupture) {
  CombatState s = fight();
  s.character.powers[Power::Rupture] = 1;
  s.current_hand.push_back(Card{CardId::Regret});

  end_turn_then_win(s);

  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 1);
}

// It leaves the hand as it fires, so Runic Pyramid cannot hold it.
TEST(Regret, LeavesTheHandUnderRunicPyramid) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::RunicPyramid, 0});
  s.current_hand.push_back(Card{CardId::Regret});
  s.current_hand.push_back(Card{CardId::Strike});

  end_turn_then_win(s);

  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(count_in(s.discard_pile, CardId::Regret), 1);
}

// ============================================================================
// Doubt and Shame (§3.2)
// ============================================================================

// Doubt's Weak starts just applied (StS's isSourceMonster = true), so it skips
// the end-of-round tick of the turn it lands and is there for the next turn.
// The draw pile is full, so Doubt is not drawn again.
TEST(DoubtAndShame, DoubtsWeakLastsThroughTheNextTurn) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Doubt});
  for (int i = 0; i < 10; ++i) s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Weak), 1)
      << "Doubt's Weak was gone before the turn it is meant for";
  EXPECT_FALSE(
      s.character.debuff_just_applied[static_cast<int>(Debuff::Weak)])
      << "the just-applied flag survived to a decision point";

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Weak), 0);
}

// Shame's Frail reduces the next turn's card block: Defend gives 3, not 5.
TEST(DoubtAndShame, ShamesFrailReducesTheNextTurnsBlock) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Shame});
  for (int i = 0; i < 10; ++i) s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  ASSERT_EQ(get_status(s.character.debuffs, Debuff::Frail), 1);
  s.character.current_block = 0;
  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend)));
  EXPECT_EQ(s.character.current_block, 3);

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Frail), 0);
}

// Onto a Weak already held, Doubt stacks and the held one's tick applies:
// 1 + 1, less one at the end of the round.
TEST(DoubtAndShame, DoubtStacksOntoAHeldWeakWithoutSkippingItsTick) {
  CombatState s = fight();
  s.character.debuffs[Debuff::Weak] = 1;
  s.current_hand.push_back(Card{CardId::Doubt});
  for (int i = 0; i < 10; ++i) s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Weak), 1);
}

TEST(DoubtAndShame, ArtifactBlocksDoubt) {
  CombatState s = fight();
  s.character.powers[Power::Artifact] = 1;
  s.current_hand.push_back(Card{CardId::Doubt});

  end_turn_then_win(s);

  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Weak), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::Artifact), 0);
}

// Ginger and Turnip make the player immune, and are checked before Artifact,
// so the charge is kept.
TEST(DoubtAndShame, GingerBlocksDoubtAndKeepsArtifact) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::Ginger, 0});
  s.character.powers[Power::Artifact] = 1;
  s.current_hand.push_back(Card{CardId::Doubt});

  end_turn_then_win(s);

  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Weak), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::Artifact), 1);
}

TEST(DoubtAndShame, TurnipBlocksShame) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::Turnip, 0});
  s.current_hand.push_back(Card{CardId::Shame});

  end_turn_then_win(s);

  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Frail), 0);
}

// ============================================================================
// HP-loss reactions need HP actually lost (§1.4, §3.5)
// ============================================================================

// Tungsten Rod takes a 1-HP loss to nothing: no Rupture, no Blood for Blood.
TEST(HpLossReactions, TungstenRodCancellingALossFiresNothing) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::TungstenRod, 0});
  s.character.powers[Power::Rupture] = 1;

  lose_hp(s, 1);

  EXPECT_EQ(s.character.hp, 80);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
  EXPECT_EQ(s.character.hp_loss_events, 0);
}

TEST(HpLossReactions, BufferStoppingALossFiresNothing) {
  CombatState s = fight();
  s.character.powers[Power::Buffer] = 1;
  s.character.powers[Power::Rupture] = 1;

  lose_hp(s, 4);

  EXPECT_EQ(s.character.hp, 80);
  EXPECT_EQ(get_status(s.character.powers, Power::Buffer), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
  EXPECT_EQ(s.character.hp_loss_events, 0);
}

// The control for the two above: a loss that lands fires both.
TEST(HpLossReactions, ALossThatLandsFiresRuptureAndBloodForBlood) {
  CombatState s = fight();
  s.character.powers[Power::Rupture] = 1;

  lose_hp(s, 1);

  EXPECT_EQ(s.character.hp, 79);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 1);
  EXPECT_EQ(s.character.hp_loss_events, 1);
}

// Burn's damage has the player as its owner, so HP it takes fires Rupture.
TEST(HpLossReactions, BurnGettingThroughBlockFiresRupture) {
  CombatState s = fight();
  s.character.powers[Power::Rupture] = 1;
  s.current_hand.push_back(Card{CardId::Burn});

  end_turn_then_win(s);

  EXPECT_EQ(s.character.hp, 78);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 1);
}

TEST(HpLossReactions, BurnFullyBlockedDoesNotFireRupture) {
  CombatState s = fight();
  s.character.powers[Power::Rupture] = 1;
  s.character.current_block = 10;
  s.current_hand.push_back(Card{CardId::Burn});

  end_turn_then_win(s);

  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
}

// Fixed damage with no player source is not the player's own and does not fire
// Rupture. Today nothing deals it to the player except Burn and Decay; this
// pins the source check for the enemy thorns-type damage that will.
TEST(HpLossReactions, SourcelessFixedDamageDoesNotFireRupture) {
  CombatState s = fight();
  s.character.powers[Power::Rupture] = 1;
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::DealFixedDamage};
  a.target = kPlayerSlot;
  a.amount = 3;
  q.push_back(a);
  drain(s, q, ctx);

  ASSERT_EQ(s.character.hp, 77);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
}
