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
#include "combat_env.h"
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

// ============================================================================
// Card damage is computed when the card is played (§3.8)
// ============================================================================

namespace {

// Drain a 40-point hit that takes 80 HP to 40, crossing Red Skull's half-HP
// line, followed by `next`. Red Skull's +3 is staged to resolve right after the
// hit, so it is in place before `next` runs.
void cross_red_skull_then(CombatState& s, const Action& next) {
  ActionQueue q;
  ResolutionContext ctx;
  Action hit{ActionKind::DealFixedDamage};
  hit.target = kPlayerSlot;
  hit.amount = 40;
  q.push_back(hit);
  q.push_back(next);
  drain(s, q, ctx);
  ASSERT_EQ(get_status(s.character.powers, Power::Strength), 3)
      << "Red Skull did not fire";
}

}  // namespace

// A targeted hit carries the damage computed when its card was played, so
// Strength gained after that — here Red Skull's +3, landing between the play
// and the hit — does not reach it. This is the shape handle_play_card emits.
TEST(CardDamageAtPlay, AQueuedHitKeepsTheDamageItWasPlayedWith) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::RedSkull, 0});
  Action strike{ActionKind::DealDamage};
  strike.actor = kPlayerSlot;
  strike.target = 0;
  strike.amount = 6;

  cross_red_skull_then(s, strike);

  EXPECT_EQ(s.enemies[0].hp, 200 - 6);
}

// Sword Boomerang is the exception: StS's AttackDamageRandomEnemyAction rolls
// its target when it runs and computes the damage then, so Strength gained
// before the hit does reach it.
TEST(CardDamageAtPlay, SwordBoomerangComputesEachHitWhenItLands) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::RedSkull, 0});
  Action hit{ActionKind::DamageRandomEnemyAttack};
  hit.actor = kPlayerSlot;
  hit.amount = 3;

  cross_red_skull_then(s, hit);

  EXPECT_EQ(s.enemies[0].hp, 200 - 6);
}

// Through a real play: Strength is applied once per hit at play time.
TEST(CardDamageAtPlay, TwinStrikeAppliesStrengthToBothHits) {
  CombatState s = fight();
  s.character.powers[Power::Strength] = 2;
  s.current_hand.push_back(Card{CardId::TwinStrike});

  ASSERT_TRUE(apply_action(s, card_action(CardId::TwinStrike, 0)));

  EXPECT_EQ(s.enemies[0].hp, 200 - 2 * (5 + 2));
}

// Fiend Fire deals one hit per card it exhausts, and each hit gets Strength
// (wiki: "Benefits once from Strength or Vigor per hit, so per card
// Exhausted"). One hit of 7 x 3 with Strength added once would deal 23.
TEST(CardDamageAtPlay, FiendFireAppliesStrengthToEveryHit) {
  CombatState s = fight();
  s.character.powers[Power::Strength] = 2;
  s.current_hand.push_back(Card{CardId::FiendFire});
  for (int i = 0; i < 3; ++i) s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(apply_action(s, card_action(CardId::FiendFire, 0)));

  EXPECT_EQ(s.enemies[0].hp, 200 - 3 * (7 + 2));
}

// An AoE card computes per target: Vulnerable on one enemy raises only the
// damage that enemy takes.
TEST(CardDamageAtPlay, AnAoeCardComputesEachTargetsDamageSeparately) {
  CombatState s = fight();
  std::mt19937 rng(0);
  s.enemies.push_back(make_jaw_worm(rng));
  s.enemies[1].hp = s.enemies[1].max_hp = 200;
  s.enemies[1].current_block = 0;
  s.enemies[1].debuffs[Debuff::Vulnerable] = 2;
  s.current_hand.push_back(Card{CardId::Cleave});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Cleave, 0)));

  EXPECT_EQ(s.enemies[0].hp, 200 - 8);
  EXPECT_EQ(s.enemies[1].hp, 200 - 12);
}

// ============================================================================
// Pain (§3.4)
// ============================================================================

namespace {

bool play(CombatState& s, CardId id, int target = 0) {
  return apply_action(s, card_action(id, target));
}

bool legal(const CombatState& s, int action) {
  return valid_actions(s)[static_cast<std::size_t>(action)];
}

}  // namespace

// Each Pain in hand costs 1 HP per other card played.
TEST(Pain, EachOneLosesOneHpPerOtherCardPlayed) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Defend));

  EXPECT_EQ(s.character.hp, 78);
}

// A Pain played under Blue Candle has left the hand: the other Pain fires, it
// does not fire for itself. 1 from the other Pain, 1 from Blue Candle.
TEST(Pain, APainPlayedUnderBlueCandleDoesNotTriggerItself) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::BlueCandle, 0});
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Pain});

  ASSERT_TRUE(play(s, CardId::Pain));

  EXPECT_EQ(s.character.hp, 78);
}

// Second Wind exhausts the Pain, but only as an action that runs after Pain has
// fired, so the Pain still costs its 1.
TEST(Pain, ACardThatExhaustsItStillTriggersIt) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::SecondWind});

  ASSERT_TRUE(play(s, CardId::SecondWind));

  ASSERT_EQ(count_in(s.exhaust_pile, CardId::Pain), 1);
  EXPECT_EQ(s.character.hp, 79);
}

// Havoc is a card played, and so is the card it plays.
TEST(Pain, HavocTriggersItTwice) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Havoc});
  s.draw_pile.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Havoc));

  ASSERT_EQ(s.exhaust_pile.size(), 1u) << "Havoc did not play its card";
  EXPECT_EQ(s.character.hp, 78);
}

TEST(Pain, ADoubleTapReplayTriggersIt) {
  CombatState s = fight();
  s.character.powers[Power::DoubleTap] = 1;
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::Strike));

  ASSERT_EQ(s.enemies[0].hp, 200 - 12) << "the replay did not happen";
  EXPECT_EQ(s.character.hp, 78);
}

// Pain's loss comes before the card resolves (wiki: "before Thorns, Sharp Hide,
// Beat of Death, or any card-specific HP loss"). Buffer shows it: it stops the
// first loss to arrive, so with Pain first it is spent on Pain's 1 and
// Hemokinesis costs its 2. With Pain last it would stop the 2 instead.
TEST(Pain, ItsLossComesBeforeTheCardsOwnHpLoss) {
  CombatState s = fight();
  s.character.powers[Power::Buffer] = 1;
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Hemokinesis});

  ASSERT_TRUE(play(s, CardId::Hemokinesis));

  EXPECT_EQ(s.character.hp, 78);
}

// At 1 HP, Pain's loss kills the player, and the fight is lost even though the
// Strike kills the last enemy. (StS stops resolving at the death screen, so the
// Strike never lands; the engine resolves it, and the player's death takes
// precedence. Either way the fight is lost.)
TEST(Pain, KillingThePlayerLosesTheFightTheCardWouldHaveWon) {
  CombatState s = fight();
  s.character.hp = 1;
  s.enemies[0].hp = 6;
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::Strike));

  EXPECT_EQ(s.outcome, Outcome::Lost);
}

// Rupture's Strength from Pain's loss misses the card that triggered it and
// reaches the next one (wiki Rupture page).
TEST(Pain, RupturesStrengthReachesTheNextCardNotThisOne) {
  CombatState s = fight();
  s.character.powers[Power::Rupture] = 1;
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::Strike));
  EXPECT_EQ(s.enemies[0].hp, 200 - 6);
  ASSERT_TRUE(play(s, CardId::Strike));
  EXPECT_EQ(s.enemies[0].hp, 200 - 6 - 7);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 2);
}

// The same for Red Skull, whose +3 resolves right after the loss that crosses
// half HP: the triggering Strike was computed before it (§3.8).
TEST(Pain, RedSkullsStrengthDoesNotReachTheTriggeringCard) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::RedSkull, 0});
  s.character.hp = 41;
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::Strike));

  ASSERT_EQ(get_status(s.character.powers, Power::Strength), 3);
  EXPECT_EQ(s.enemies[0].hp, 200 - 6);
}

// End-of-turn autoplays are dontTriggerOnUseCard: Burn costs its 2, Pain
// nothing.
TEST(Pain, EndOfTurnCardsDoNotTriggerIt) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Burn});

  end_turn_then_win(s);

  EXPECT_EQ(s.character.hp, 78);
}

// Tungsten Rod takes each trigger to nothing, and nothing then fires.
TEST(Pain, TungstenRodCancelsEachTrigger) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::TungstenRod, 0});
  s.character.powers[Power::Rupture] = 1;
  s.current_hand.push_back(Card{CardId::Pain});
  s.current_hand.push_back(Card{CardId::Defend});

  ASSERT_TRUE(play(s, CardId::Defend));

  EXPECT_EQ(s.character.hp, 80);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
}

// ============================================================================
// Normality and Velvet Choker (§3.3)
// ============================================================================

TEST(Normality, AfterThreePlaysNoCardIsPlayable) {
  CombatState s = fight();
  s.character.energy = 99;
  s.current_hand.push_back(Card{CardId::Normality});
  for (int i = 0; i < 4; ++i) s.current_hand.push_back(Card{CardId::Defend});
  s.potions = {PotionId::BlockPotion};

  for (int i = 0; i < 3; ++i) ASSERT_TRUE(play(s, CardId::Defend));

  EXPECT_FALSE(legal(s, card_action(CardId::Defend)));
  EXPECT_FALSE(play(s, CardId::Defend)) << "the apply path disagrees with the mask";
  EXPECT_TRUE(legal(s, encode_action(ActionBlock::PotionUse,
                                     static_cast<int>(PotionId::BlockPotion), 0)));
  EXPECT_TRUE(legal(s, kEndTurnAction));
}

// The count is the turn's, not Normality's: plays made before it reached the
// hand count.
TEST(Normality, PlaysBeforeItWasDrawnCount) {
  CombatState s = fight();
  s.character.energy = 99;
  for (int i = 0; i < 4; ++i) s.current_hand.push_back(Card{CardId::Defend});
  for (int i = 0; i < 3; ++i) ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_TRUE(legal(s, card_action(CardId::Defend)));

  s.current_hand.push_back(Card{CardId::Normality});

  EXPECT_FALSE(legal(s, card_action(CardId::Defend)));
}

// Only while it is in hand: Second Wind as the third play exhausts it, and the
// fourth card can be played.
TEST(Normality, ExhaustingItLiftsTheCap) {
  CombatState s = fight();
  s.character.energy = 99;
  s.current_hand.push_back(Card{CardId::Normality});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::SecondWind});
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_TRUE(play(s, CardId::SecondWind));
  ASSERT_EQ(count_in(s.exhaust_pile, CardId::Normality), 1);

  EXPECT_TRUE(play(s, CardId::Strike));
}

// The cap binds cards played from the card queue too (canUse runs
// hasEnoughEnergy for them): Havoc as the third play takes its card and cannot
// play it, so the card only exhausts.
TEST(Normality, HavocAsTheThirdPlayCannotPlayItsCard) {
  CombatState s = fight();
  s.character.energy = 99;
  s.current_hand.push_back(Card{CardId::Normality});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Havoc});
  s.draw_pile.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_TRUE(play(s, CardId::Havoc));

  EXPECT_EQ(s.enemies[0].hp, 200) << "the taken Strike was played";
  EXPECT_EQ(count_in(s.exhaust_pile, CardId::Strike), 1);
  EXPECT_EQ(s.character.cards_played_this_turn, 3);
}

// A Double Tap replay of the third play is a fourth play, which the cap stops;
// a blocked replay simply vanishes.
TEST(Normality, ADoubleTapReplayOfTheThirdPlayVanishes) {
  CombatState s = fight();
  s.character.energy = 99;
  s.character.powers[Power::DoubleTap] = 1;
  s.current_hand.push_back(Card{CardId::Normality});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Strike});

  ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_TRUE(play(s, CardId::Strike));

  EXPECT_EQ(s.enemies[0].hp, 200 - 6) << "the replay was played";
  EXPECT_EQ(count_in(s.discard_pile, CardId::Strike), 1);
}

TEST(Normality, TheCountResetsNextTurn) {
  CombatState s = fight();
  s.character.energy = 99;
  s.current_hand.push_back(Card{CardId::Normality});
  for (int i = 0; i < 3; ++i) s.current_hand.push_back(Card{CardId::Defend});
  for (int i = 0; i < 10; ++i) s.draw_pile.push_back(Card{CardId::Defend});
  for (int i = 0; i < 3; ++i) ASSERT_TRUE(play(s, CardId::Defend));
  ASSERT_EQ(s.character.cards_played_this_turn, 3);

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);

  EXPECT_EQ(s.character.cards_played_this_turn, 0);
}

// Velvet Choker: a sixth play is legal, a seventh is not.
TEST(VelvetChoker, StopsTheSeventhPlay) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::VelvetChoker, 0});
  s.character.energy = 99;
  for (int i = 0; i < 7; ++i) s.current_hand.push_back(Card{CardId::Defend});

  for (int i = 0; i < 6; ++i) ASSERT_TRUE(play(s, CardId::Defend)) << i;

  EXPECT_FALSE(legal(s, card_action(CardId::Defend)));
}

// ============================================================================
// Blue Candle (§3.6)
// ============================================================================

TEST(BlueCandle, ACurseIsPlayableAtNoEnergyExhaustsAndCostsOneHp) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::BlueCandle, 0});
  s.character.energy = 0;
  s.current_hand.push_back(Card{CardId::Injury});

  ASSERT_TRUE(play(s, CardId::Injury));

  EXPECT_EQ(s.character.hp, 79);
  EXPECT_EQ(s.character.energy, 0);
  EXPECT_EQ(count_in(s.exhaust_pile, CardId::Injury), 1);
  EXPECT_TRUE(s.current_hand.empty());
}

// It is a card played (wiki: it triggers Rupture, and counts for Normality).
TEST(BlueCandle, ItFiresRuptureAndCountsAsAPlay) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::BlueCandle, 0});
  s.character.powers[Power::Rupture] = 1;
  s.current_hand.push_back(Card{CardId::Injury});

  ASSERT_TRUE(play(s, CardId::Injury));

  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 1);
  EXPECT_EQ(s.character.cards_played_this_turn, 1);
}

// Normality can be played to exhaust it, but only below its own cap.
TEST(BlueCandle, NormalityCanBePlayedOnlyBelowItsCap) {
  CombatState s = fight();
  s.relics.push_back(HeldRelic{RelicId::BlueCandle, 0});
  s.current_hand.push_back(Card{CardId::Normality});

  s.character.cards_played_this_turn = 3;
  EXPECT_FALSE(legal(s, card_action(CardId::Normality)));
  s.character.cards_played_this_turn = 2;
  EXPECT_TRUE(legal(s, card_action(CardId::Normality)));
}

TEST(BlueCandle, WithoutItACurseIsUnplayable) {
  CombatState s = fight();
  s.current_hand.push_back(Card{CardId::Injury});

  EXPECT_FALSE(legal(s, card_action(CardId::Injury)));
  EXPECT_FALSE(play(s, CardId::Injury));
}

// ============================================================================
// The observation (§3.7)
// ============================================================================

// The last player-base slot carries the cards played this turn.
TEST(CardsPlayedObservation, IsWrittenToThePlayerBlock) {
  CombatState s = fight();
  s.character.energy = 99;
  for (int i = 0; i < 3; ++i) s.current_hand.push_back(Card{CardId::Defend});
  CombatEnv env(s);
  constexpr int kSlot = CombatEnv::kPlayerBaseSize - 1;
  ASSERT_EQ(env.obs()[kSlot], 0.0f);

  env.step(card_action(CardId::Defend));
  env.step(card_action(CardId::Defend));

  EXPECT_EQ(env.obs()[kSlot], 2.0f);
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
