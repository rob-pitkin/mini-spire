// Relic trigger infrastructure (docs/design/relic-effects.md §3.1).
//
// Batch 0: the hook vocabulary, fire_relic_hooks, and the combat-start
// sub-phases, proved with the relics whose primitives already exist.

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

#include "action.h"
#include "query.h"
#include "run_state.h"
#include "turn_loop.h"

namespace minispire {
namespace {

// A fight with exactly the relics named, at full HP, on the starter deck.
CombatState fight_with(std::vector<RelicId> ids, uint32_t seed = 1) {
  CombatSetup setup;
  setup.seed = seed;
  setup.deck = starter_deck();
  for (RelicId id : ids) setup.relics.push_back(HeldRelic{id, 0});
  return start_combat(setup);
}

// The same fight with no relics, as the control. Every assertion below is a
// DELTA against this rather than an absolute: a bare fight already has block,
// energy and a hand, and hardcoding those numbers would make these tests fail
// for reasons that have nothing to do with relics.
CombatState bare_fight(uint32_t seed = 1) { return fight_with({}, seed); }

// The same, as an ELITE fight. Several relics are conditional on the fight's
// kind, which CombatState now carries as is_elite.
// The combat action index for playing `id` at `target`. Derived the same way
// the action space defines it, never as an offset from kEndTurnAction — the
// option-slot channel sits after the combat block, so the last index is
// decline, not end-turn.
int card_action(CardId id, int target = 0) {
  return static_cast<int>(id) * kMaxEnemies + target;
}

// Deal `amount` fixed damage to the player through the real executor path, so
// block, Intangible, Plated Armor and Buffer all see it as they would in play.
void hit_player(CombatState& s, int amount) {
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::DealFixedDamage};
  a.target = kPlayerSlot;
  a.amount = amount;
  q.push_back(a);
  drain(s, q, ctx);
}

// The same, aimed at an enemy slot. Fixed damage is still DAMAGE, so it goes
// through the block subtraction that Hand Drill watches.
void hit_enemy(CombatState& s, int slot, int amount) {
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::DealFixedDamage};
  a.target = slot;
  a.amount = amount;
  q.push_back(a);
  drain(s, q, ctx);
}

// Force exactly one draw-pile reshuffle: empty the draw pile, leave one card in
// the discard, then draw. This is the event Sundial counts and The Abacus
// blocks on — the same path StS routes through EmptyDeckShuffleAction.
void force_one_reshuffle(CombatState& s) {
  s.current_hand.clear();
  s.draw_pile.clear();
  s.discard_pile.clear();
  s.discard_pile.push_back(Card{CardId::Strike});

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::DrawCards};
  a.amount = 1;
  q.push_back(a);
  drain(s, q, ctx);
}

CombatState elite_fight_with(std::vector<RelicId> ids, uint32_t seed = 1) {
  CombatSetup setup;
  setup.seed = seed;
  setup.pool = EncounterPool::Elite;
  setup.deck = starter_deck();
  for (RelicId id : ids) setup.relics.push_back(HeldRelic{id, 0});
  return start_combat(setup);
}

// ------------------------------------------------------------ combat start

TEST(RelicTriggers, VajraGrantsStrengthAtCombatStart) {
  const CombatState with = fight_with({RelicId::Vajra});
  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 1);
}

// --------------------------------------------- the three previously-dead hooks
//
// EnemyDeath, ShuffleDrawPile and BlockBroken existed in the Hook enum with
// nothing firing them, so four relics were unreachable. Each relic's numbers
// come from the decompiled class and were cross-checked against the wiki.

// Gremlin Horn: "Whenever an enemy dies, gain 1 Energy and draw 1 card."
// Asserted as a DELTA against the same fight without the relic, because a bare
// fight already has energy and a hand.
TEST(RelicTriggers, GremlinHornPaysOutWhenAnEnemyDies) {
  const auto kill_one_of_two = [](std::vector<RelicId> ids) {
    CombatState s = fight_with(std::move(ids));
    std::mt19937 rng(0);
    s.enemies.clear();
    Enemy dying = make_jaw_worm(rng);
    dying.hp = 1;
    dying.current_block = 0;
    Enemy survivor = make_jaw_worm(rng);
    s.enemies.push_back(std::move(dying));
    s.enemies.push_back(std::move(survivor));

    s.current_hand.clear();
    s.current_hand.push_back(Card{CardId::Strike});
    s.draw_pile.clear();
    s.draw_pile.push_back(Card{CardId::Defend});
    s.character.energy = 3;
    EXPECT_TRUE(apply_action(s, card_action(CardId::Strike, 0)));
    return s;
  };

  const CombatState with = kill_one_of_two({RelicId::GremlinHorn});
  const CombatState without = kill_one_of_two({});
  ASSERT_LE(with.enemies[0].hp, 0) << "the kill did not happen";

  EXPECT_EQ(with.character.energy, without.character.energy + 1);
  EXPECT_EQ(with.current_hand.size(), without.current_hand.size() + 1);
}

// ...but NOT on the kill that ends the fight. The decompiled relic guards on
// !areMonstersBasicallyDead(), and the energy and card would have nowhere to
// go. This is the condition the wiki text does not mention.
TEST(RelicTriggers, GremlinHornIsSilentOnTheKillThatEndsTheFight) {
  const auto kill_the_last = [](std::vector<RelicId> ids) {
    CombatState s = fight_with(std::move(ids));
    std::mt19937 rng(0);
    s.enemies.clear();
    Enemy dying = make_jaw_worm(rng);
    dying.hp = 1;
    dying.current_block = 0;
    s.enemies.push_back(std::move(dying));

    s.current_hand.clear();
    s.current_hand.push_back(Card{CardId::Strike});
    s.draw_pile.clear();
    s.draw_pile.push_back(Card{CardId::Defend});
    s.character.energy = 3;
    EXPECT_TRUE(apply_action(s, card_action(CardId::Strike, 0)));
    return s;
  };

  const CombatState with = kill_the_last({RelicId::GremlinHorn});
  const CombatState without = kill_the_last({});
  ASSERT_EQ(with.outcome, Outcome::Won);

  EXPECT_EQ(with.character.energy, without.character.energy)
      << "Gremlin Horn paid out on the last kill";
  EXPECT_EQ(with.current_hand.size(), without.current_hand.size());
}

// Sundial: "Every 3 times you shuffle your draw pile, gain 2 Energy." The
// counter is run-scoped in StS (set in onEquip, never cleared per combat).
TEST(RelicTriggers, SundialPaysOnEveryThirdShuffle) {
  CombatState s = fight_with({RelicId::Sundial});
  const int start = s.character.energy;

  force_one_reshuffle(s);
  EXPECT_EQ(s.character.energy, start) << "paid on the first shuffle";
  force_one_reshuffle(s);
  EXPECT_EQ(s.character.energy, start) << "paid on the second shuffle";
  force_one_reshuffle(s);
  EXPECT_EQ(s.character.energy, start + 2) << "the third shuffle paid nothing";

  // And the count restarts rather than paying on every shuffle thereafter.
  force_one_reshuffle(s);
  EXPECT_EQ(s.character.energy, start + 2);
}

// The Abacus: "Gain 6 Block whenever you shuffle your draw pile." Relic block
// is not card block, so Dexterity leaves it alone.
TEST(RelicTriggers, TheAbacusBlocksOnEveryShuffle) {
  CombatState s = fight_with({RelicId::TheAbacus});
  s.character.current_block = 0;

  force_one_reshuffle(s);
  EXPECT_EQ(s.character.current_block, 6);
  force_one_reshuffle(s);
  EXPECT_EQ(s.character.current_block, 12) << "every shuffle counts, not every third";
}

// The combat-start shuffle is NOT a shuffle for relic purposes: StS shuffles
// the opening draw pile inside CardGroup.initializeDeck, which never reaches
// the relics. If that ever changes, every Abacus fight starts with 6 block.
TEST(RelicTriggers, TheOpeningShuffleDoesNotCountAsAShuffle) {
  const CombatState abacus = fight_with({RelicId::TheAbacus});
  EXPECT_EQ(abacus.character.current_block, 0)
      << "the opening shuffle triggered The Abacus";

  const CombatState sundial = fight_with({RelicId::Sundial});
  const CombatState bare = bare_fight();
  EXPECT_EQ(sundial.character.energy, bare.character.energy);
  for (const HeldRelic& r : sundial.relics) {
    if (r.id == RelicId::Sundial) EXPECT_EQ(r.counter, 0);
  }
}

// Hand Drill: "Whenever you break an enemy's Block, apply 2 Vulnerable."
TEST(RelicTriggers, HandDrillMakesAnEnemyVulnerableWhenItsBlockBreaks) {
  CombatState s = fight_with({RelicId::HandDrill});
  s.enemies[0].current_block = 3;

  hit_enemy(s, 0, 5);

  EXPECT_EQ(get_status(s.enemies[0].debuffs, Debuff::Vulnerable), 2);
  EXPECT_EQ(s.enemies[0].current_block, 0);
}

// Equality counts as broken — decompiled decrementBlock takes the same branch
// for `damageAmount == currentBlock` as for `>`.
TEST(RelicTriggers, HandDrillFiresWhenDamageExactlyEqualsBlock) {
  CombatState s = fight_with({RelicId::HandDrill});
  s.enemies[0].current_block = 4;

  hit_enemy(s, 0, 4);

  EXPECT_EQ(get_status(s.enemies[0].debuffs, Debuff::Vulnerable), 2);
}

TEST(RelicTriggers, HandDrillIsSilentWhenTheBlockHolds) {
  CombatState s = fight_with({RelicId::HandDrill});
  s.enemies[0].current_block = 6;

  hit_enemy(s, 0, 2);

  EXPECT_EQ(get_status(s.enemies[0].debuffs, Debuff::Vulnerable), 0);
  EXPECT_EQ(s.enemies[0].current_block, 4);
}

// The player's own block breaking is not an event: StS guards the relic loop
// with `this instanceof AbstractMonster`. A direct port of onBlockBroken would
// have applied the Vulnerable to the player.
TEST(RelicTriggers, HandDrillIgnoresThePlayersBlockBreaking) {
  CombatState s = fight_with({RelicId::HandDrill});
  s.character.current_block = 3;

  hit_player(s, 9);

  EXPECT_EQ(get_status(s.character.debuffs, Debuff::Vulnerable), 0);
}

// ------------------------------------------------- Toolbox: the blind choice
//
// Toolbox is the first relic that PAUSES combat setup, and the pause is the
// mechanic rather than an implementation detail. In StS the 1-of-3 appears
// BEFORE the opening hand is dealt, so a human cannot pick the card that suits
// the hand they were given. Resolving it after the draw would hand an agent
// strictly more information than a human gets — a parity defect of exactly the
// kind observation-space.md §1 rules out.

TEST(RelicTriggers, ToolboxPausesTheFightBeforeTheOpeningHandIsDealt) {
  const CombatState s = fight_with({RelicId::Toolbox});
  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.kind, ChoiceKind::DiscoverColorlessCard);
  EXPECT_EQ(s.pending_choice.num_options, 3);
  EXPECT_TRUE(s.current_hand.empty())
      << "the opening hand was dealt before the choice — Toolbox must be blind";
}

// The control, and the reason the assertion above is meaningful: every other
// fight opens unpaused with a full hand.
TEST(RelicTriggers, AFightWithNoPreDrawChoiceOpensWithItsHandDealt) {
  const CombatState s = bare_fight();
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(static_cast<int>(s.current_hand.size()), STARTING_HAND_SIZE);
}

TEST(RelicTriggers, ToolboxOffersThreeDistinctColorlessCards) {
  const CombatState s = fight_with({RelicId::Toolbox});
  ASSERT_TRUE(s.pending_choice.active());
  ASSERT_EQ(s.pending_choice.num_options, 3);

  const std::vector<CardId> pool = generatable_colorless_pool();
  for (int i = 0; i < 3; ++i) {
    const CardId id = s.pending_choice.options[i].card_id;
    EXPECT_NE(std::find(pool.begin(), pool.end(), id), pool.end())
        << card_name(id) << " is not a generatable colorless card";
    for (int j = i + 1; j < 3; ++j) {
      EXPECT_NE(id, s.pending_choice.options[j].card_id)
          << card_name(id) << " was offered twice";
    }
  }
}

TEST(RelicTriggers, ToolboxDealsTheOpeningHandOnceTheChoiceIsAnswered) {
  CombatState s = fight_with({RelicId::Toolbox});
  ASSERT_TRUE(s.pending_choice.active());
  const CardId chosen = s.pending_choice.options[0].card_id;
  ASSERT_TRUE(resolve_choice(s, 0));

  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(static_cast<int>(s.current_hand.size()), STARTING_HAND_SIZE + 1)
      << "the parked opening draw did not resume";

  int copies = 0;
  for (const Card& c : s.current_hand) {
    if (c.card_id != chosen) continue;
    ++copies;
    // Toolbox HANDS the card over; it does not discount it. Discovery's free
    // copy is the card's own text, not something the choice machinery does.
    EXPECT_EQ(c.cost_override, kNoCostOverride)
        << card_name(chosen) << " arrived discounted";
  }
  EXPECT_EQ(copies, 1) << card_name(chosen) << " never reached the hand";
}

// The opening draw and the two post-draw hooks sit BEHIND the pause in the
// suspended queue, so answering has to resume all of it. Vajra fires on
// CombatStart, which is post-draw: if the restructure dropped the parked
// actions, its Strength never lands.
TEST(RelicTriggers, ToolboxDoesNotSwallowThePostDrawRelicHooks) {
  CombatState s = fight_with({RelicId::Toolbox, RelicId::Vajra});
  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0)
      << "a post-draw hook fired before the pre-draw choice was answered";

  ASSERT_TRUE(resolve_choice(s, 0));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 1)
      << "the parked CombatStart hooks were lost";
}

// A fight can now BEGIN in a choice, which nothing else produces. The answer
// must therefore be reachable through the ACTION space on step 0, not only
// through resolve_choice() — the action space is all an agent has.
//
// CardSelect is indexed by CARD ID, not by option slot: the first draft of this
// test passed slot 0, which encodes Strike, and apply_action refused it because
// Strike was not on offer. The refusal was correct.
TEST(RelicTriggers, ToolboxsOpeningChoiceIsAnsweredThroughTheActionSpace) {
  CombatState s = fight_with({RelicId::Toolbox});
  ASSERT_TRUE(s.pending_choice.active());
  const CardId offered = s.pending_choice.options[0].card_id;

  ASSERT_TRUE(apply_action(
      s, encode_action(ActionBlock::CardSelect, static_cast<int>(offered))));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(static_cast<int>(s.current_hand.size()), STARTING_HAND_SIZE + 1);
}

// And a card that is NOT on offer stays illegal at fight start, so the opening
// pause cannot be used as a back door into the choice machinery.
TEST(RelicTriggers, ToolboxsOpeningChoiceRefusesACardItDidNotOffer) {
  CombatState s = fight_with({RelicId::Toolbox});
  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_FALSE(apply_action(
      s, encode_action(ActionBlock::CardSelect,
                       static_cast<int>(CardId::Strike))));
  EXPECT_TRUE(s.pending_choice.active()) << "the refused action still resolved";
}

// A relic's menu has NO source card. source_card used to default to Strike, so
// Toolbox's prompt reported Strike as the card that opened it — stated as fact
// in the observation and in the TUI header both.
TEST(RelicTriggers, ToolboxsChoiceReportsNoSourceCard) {
  const CombatState s = fight_with({RelicId::Toolbox});
  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.source_card, CardId::None);
  // And the name lookup is total. CARD_DATABASE has no row for the sentinel,
  // and the TUI calls card_name() on source_card unconditionally.
  EXPECT_STREQ(card_name(CardId::None), "(none)");
}

// The sentinel stays OUT of the card vocabulary: appended past the last real
// id, with no CARD_DATABASE row, so it can never encode a legal action and the
// action space stays 2,135. Inserting a card id ahead of it breaks this.
TEST(RelicTriggers, TheNoCardSentinelIsOutsideTheCardVocabulary) {
  EXPECT_EQ(static_cast<int>(CardId::None), kNumCardTypes);
  EXPECT_EQ(CARD_DATABASE.find(CardId::None), CARD_DATABASE.end());
  EXPECT_EQ(static_cast<int>(CARD_DATABASE.size()), kNumCardTypes);
}

// The control: a choice a CARD opened still names that card.
TEST(RelicTriggers, ACardOpenedChoiceStillNamesItsCard) {
  CombatState s = bare_fight();
  // Two distinct upgradable cards, so Armaments is a real prompt rather than a
  // one-option auto-resolve.
  s.current_hand.clear();
  s.current_hand.push_back(Card{CardId::Strike});
  s.current_hand.push_back(Card{CardId::Defend});
  s.current_hand.push_back(Card{CardId::Armaments});

  ASSERT_TRUE(apply_action(s, card_action(CardId::Armaments)));
  ASSERT_TRUE(s.pending_choice.active());
  EXPECT_EQ(s.pending_choice.source_card, CardId::Armaments);
}

TEST(RelicTriggers, OddlySmoothStoneGrantsDexterity) {
  const CombatState with = fight_with({RelicId::OddlySmoothStone});
  EXPECT_EQ(get_status(with.character.powers, Power::Dexterity), 1);
}

TEST(RelicTriggers, AnchorGrantsBlock) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::Anchor});
  EXPECT_EQ(with.character.current_block, bare.character.current_block + 10);
}

TEST(RelicTriggers, LanternGrantsEnergy) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::Lantern});
  EXPECT_EQ(with.character.energy, bare.character.energy + 1);
}

// Blood Vial heals, so it needs a fight that starts damaged to be visible.
TEST(RelicTriggers, BloodVialHealsAtCombatStart) {
  CombatSetup setup;
  setup.seed = 1;
  setup.deck = starter_deck();
  setup.hp = 50;
  setup.relics = {HeldRelic{RelicId::BloodVial, 0}};
  const CombatState with = start_combat(setup);
  EXPECT_EQ(with.character.hp, 52);
}

// A heal cannot exceed max HP, and combat start is exactly where that is easy
// to get wrong — the fight begins at full HP by default.
TEST(RelicTriggers, BloodVialCannotHealAboveMaxHp) {
  const CombatState with = fight_with({RelicId::BloodVial});
  EXPECT_EQ(with.character.hp, with.character.max_hp);
}

TEST(RelicTriggers, BagOfMarblesVulnerablesEveryEnemy) {
  const CombatState with = fight_with({RelicId::BagOfMarbles});
  ASSERT_FALSE(with.enemies.empty());
  for (const Enemy& e : with.enemies) {
    EXPECT_EQ(get_status(e.debuffs, Debuff::Vulnerable), 1)
        << "an enemy escaped Bag of Marbles";
  }
}

// ------------------------------------------------------ the sub-phase order

// The parity-critical one. Bag of Preparation resolves AFTER the opening hand,
// so it is a second draw of 2 rather than a 7-card opening draw. Firing it in
// the pre-draw sub-phase would still produce 7 cards -- and a different 7.
TEST(RelicTriggers, BagOfPreparationDrawsTwoMore) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::BagOfPreparation});
  EXPECT_EQ(with.current_hand.size(), bare.current_hand.size() + 2);
}

// The ordering proof: because the extra draw happens after the opening hand,
// the first cards drawn are the SAME as a bare fight's. A single 7-card draw
// would also produce 7 cards, but the opening 5 could differ.
TEST(RelicTriggers, BagOfPreparationDrawsAfterTheOpeningHandNotInsteadOfIt) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::BagOfPreparation});
  ASSERT_GE(with.current_hand.size(), bare.current_hand.size());

  for (size_t i = 0; i < bare.current_hand.size(); ++i) {
    EXPECT_TRUE(with.current_hand[i].same_as(bare.current_hand[i]))
        << "opening hand card " << i
        << " differs, so the extra draw resolved before the opening hand";
  }
}

// ------------------------------------------------------------- composition

// Relics fire in acquisition order, and several on one trigger must all land.
TEST(RelicTriggers, SeveralCombatStartRelicsAllFire) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with(
      {RelicId::Vajra, RelicId::Anchor, RelicId::Lantern,
       RelicId::OddlySmoothStone});

  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 1);
  EXPECT_EQ(get_status(with.character.powers, Power::Dexterity), 1);
  EXPECT_EQ(with.character.current_block, bare.character.current_block + 10);
  EXPECT_EQ(with.character.energy, bare.character.energy + 1);
}

// Holding a relic with no wired effect must be inert, not a crash or a stray
// mutation — most of the 140 are in exactly that state during batches 0-2.
TEST(RelicTriggers, AnUnwiredRelicChangesNothing) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::TheBoot});

  EXPECT_EQ(with.character.hp, bare.character.hp);
  EXPECT_EQ(with.character.current_block, bare.character.current_block);
  EXPECT_EQ(with.character.energy, bare.character.energy);
  EXPECT_EQ(with.current_hand.size(), bare.current_hand.size());
}

// The queue must be empty at every agent decision point (§4.2). Combat start
// drains, so a fight handed to an agent has nothing pending.
TEST(RelicTriggers, CombatStartLeavesNoPendingChoice) {
  const CombatState with = fight_with({RelicId::Vajra, RelicId::BagOfMarbles});
  EXPECT_FALSE(with.pending_choice.active());
  EXPECT_EQ(with.outcome, Outcome::InProgress);
}

// A relic held in the run reaches the fight through RunState, not just through
// a hand-built CombatSetup.
TEST(RelicTriggers, RelicsHeldInTheRunFireInTheRunsFights) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Vajra);
  run.begin_combat(EncounterPool::Weak);
  EXPECT_EQ(get_status(run.combat.character.powers, Power::Strength), 1);
}

// ===================================================================== batch A3
// Thorns and Plated Armor.

TEST(ThornsPower, RetaliatesWhenAttacked) {
  CombatState s = fight_with({RelicId::BronzeScales});
  ASSERT_EQ(get_status(s.character.powers, Power::Thorns), 3);
  ASSERT_FALSE(s.enemies.empty());

  const int enemy_hp = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_LT(s.enemies[0].hp, enemy_hp)
      << "the attacker took no Thorns damage";
}

// Thorns keys on BEING ATTACKED, not on being hurt — so it fires through full
// block, the same as Flame Barrier.
TEST(ThornsPower, FiresEvenWhenTheAttackIsFullyBlocked) {
  CombatState s = fight_with({RelicId::BronzeScales});
  s.character.current_block = 500;  // nothing will get through
  const int hp = s.character.hp;
  ASSERT_FALSE(s.enemies.empty());
  const int enemy_hp = s.enemies[0].hp;

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(s.character.hp, hp) << "the hit should have been fully blocked";
  EXPECT_LT(s.enemies[0].hp, enemy_hp)
      << "Thorns did not fire through block";
}

// Permanent, unlike Flame Barrier — it does not expire at the next turn start.
TEST(ThornsPower, DoesNotExpire) {
  CombatState s = fight_with({RelicId::BronzeScales});
  for (int turn = 0; turn < 2; ++turn) {
    ASSERT_TRUE(apply_action(s, kEndTurnAction));
    ASSERT_EQ(s.outcome, Outcome::InProgress);
  }
  EXPECT_EQ(get_status(s.character.powers, Power::Thorns), 3);
}

// ---------------------------------------------------------- Plated Armor

TEST(PlatedArmorPower, GrantsBlockAtEndOfTurn) {
  CombatState s = fight_with({RelicId::ThreadAndNeedle});
  ASSERT_EQ(get_status(s.character.powers, Power::PlatedArmor), 4);

  CombatState bare = bare_fight();
  const int bare_hp_before = bare.character.hp;
  const int s_hp_before = s.character.hp;
  ASSERT_TRUE(apply_action(bare, kEndTurnAction));
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);

  // The block lands before the enemy phase, so the armoured run takes less.
  EXPECT_LE(s_hp_before - s.character.hp, bare_hp_before - bare.character.hp)
      << "Plated Armor's block did not absorb anything";
}

// Dexterity and Frail do NOT modify it — it is not card block.
TEST(PlatedArmorPower, IsNotModifiedByDexterity) {
  CombatState plain = fight_with({RelicId::ThreadAndNeedle});
  CombatState dex = fight_with({RelicId::ThreadAndNeedle});
  dex.character.powers[Power::Dexterity] = 5;
  plain.character.current_block = 0;
  dex.character.current_block = 0;

  // Drive only the end-of-turn power hooks, so the enemy phase does not spend
  // the block before it can be compared.
  ActionQueue pq, dq;
  ResolutionContext pctx, dctx;
  fire_player_power_hooks(plain, Hook::TurnEndPlayer, pq);
  fire_player_power_hooks(dex, Hook::TurnEndPlayer, dq);
  drain(plain, pq, pctx);
  drain(dex, dq, dctx);

  EXPECT_EQ(plain.character.current_block, 4);
  EXPECT_EQ(dex.character.current_block, 4)
      << "Dexterity modified Plated Armor's block";
}

// Loses a stack to UNBLOCKED damage only.
TEST(PlatedArmorPower, LosesAStackToUnblockedDamage) {
  CombatState s = fight_with({RelicId::ThreadAndNeedle});
  s.character.current_block = 0;
  hit_player(s, 10);
  EXPECT_EQ(get_status(s.character.powers, Power::PlatedArmor), 3);
}

TEST(PlatedArmorPower, KeepsItsStackWhenFullyBlocked) {
  CombatState s = fight_with({RelicId::ThreadAndNeedle});
  s.character.current_block = 50;
  hit_player(s, 10);
  EXPECT_EQ(get_status(s.character.powers, Power::PlatedArmor), 4)
      << "a fully blocked hit cost a Plated Armor stack";
}

// ===================================================================== batch A2
// Buffer: prevent the next N times you would LOSE HP. A counter, not a
// duration — it does not tick, and only an actual HP loss spends a stack.

TEST(BufferPower, BufferPreventsOneHpLoss) {
  CombatState s = bare_fight();
  s.character.current_block = 0;
  s.character.powers[Power::Buffer] = 1;
  const int hp = s.character.hp;

  hit_player(s, 20);
  EXPECT_EQ(s.character.hp, hp) << "Buffer did not absorb the hit";
  EXPECT_EQ(get_status(s.character.powers, Power::Buffer), 0);

  hit_player(s, 20);
  EXPECT_EQ(s.character.hp, hp - 20) << "a spent Buffer still absorbed";
}

TEST(BufferPower, StacksAbsorbOneInstanceEach) {
  CombatState s = bare_fight();
  s.character.current_block = 0;
  s.character.powers[Power::Buffer] = 2;
  const int hp = s.character.hp;

  hit_player(s, 5);
  hit_player(s, 5);
  EXPECT_EQ(s.character.hp, hp);
  hit_player(s, 5);
  EXPECT_EQ(s.character.hp, hp - 5);
}

// A fully blocked hit must NOT spend a stack — the wiki calls this out as a
// deliberate fix ("no longer lost when hit by 0 damage attacks"). Block is
// still consumed.
TEST(BufferPower, AFullyBlockedHitDoesNotSpendAStack) {
  CombatState s = bare_fight();
  s.character.current_block = 20;
  s.character.powers[Power::Buffer] = 1;
  const int hp = s.character.hp;

  hit_player(s, 5);
  EXPECT_EQ(s.character.hp, hp);
  EXPECT_EQ(s.character.current_block, 15) << "block should still be spent";
  EXPECT_EQ(get_status(s.character.powers, Power::Buffer), 1)
      << "a blocked hit spent a Buffer stack";
}

// Block absorbs first, and only what would have reached HP is buffered.
TEST(BufferPower, BlockAppliesBeforeBuffer) {
  CombatState s = bare_fight();
  s.character.current_block = 5;
  s.character.powers[Power::Buffer] = 1;
  const int hp = s.character.hp;

  hit_player(s, 12);
  EXPECT_EQ(s.character.hp, hp) << "the 7 that got past block was not buffered";
  EXPECT_EQ(s.character.current_block, 0);
  EXPECT_EQ(get_status(s.character.powers, Power::Buffer), 0);
}

// It covers direct HP loss too, not just damage — "the next time you would
// lose HP", from any source.
TEST(BufferPower, BufferCoversDirectHpLoss) {
  CombatState s = bare_fight();
  s.character.powers[Power::Buffer] = 1;
  const int hp = s.character.hp;

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::LoseHp};
  a.amount = 9;
  q.push_back(a);
  drain(s, q, ctx);

  EXPECT_EQ(s.character.hp, hp) << "Buffer did not cover direct HP loss";
}

// A counter, not a duration: it survives turn boundaries untouched.
TEST(BufferPower, BufferDoesNotTickDown) {
  CombatState s = bare_fight();
  s.character.powers[Power::Buffer] = 2;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_GE(get_status(s.character.powers, Power::Buffer), 1)
      << "Buffer ticked like a duration effect";
}

TEST(BufferPower, FossilizedHelixGrantsBufferAtCombatStart) {
  const CombatState s = fight_with({RelicId::FossilizedHelix});
  EXPECT_EQ(get_status(s.character.powers, Power::Buffer), 1);
}

// ------------------------------------------------------------- Intangible

TEST(IntangiblePower, AllDamageBecomesOne) {
  CombatState s = bare_fight();
  s.character.current_block = 0;
  s.character.powers[Power::Intangible] = 1;
  const int hp = s.character.hp;
  hit_player(s, 30);
  EXPECT_EQ(s.character.hp, hp - 1);
}

// The cap is on the INCOMING number, applied before block — so a 30-damage hit
// becomes 1 and the block absorbs it entirely, spending 1 block rather than 30.
TEST(IntangiblePower, TheCapAppliesBeforeBlock) {
  CombatState s = bare_fight();
  s.character.current_block = 5;
  s.character.powers[Power::Intangible] = 1;
  const int hp = s.character.hp;

  hit_player(s, 30);
  EXPECT_EQ(s.character.hp, hp) << "the capped 1 should have been blocked";
  EXPECT_EQ(s.character.current_block, 4)
      << "block was chewed through before the cap applied";
}

// It covers HP LOSS too, not just damage.
TEST(IntangiblePower, DirectHpLossIsAlsoCappedAtOne) {
  CombatState s = bare_fight();
  s.character.powers[Power::Intangible] = 1;
  const int hp = s.character.hp;

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::LoseHp};
  a.amount = 25;
  q.push_back(a);
  drain(s, q, ctx);

  EXPECT_EQ(s.character.hp, hp - 1);
}

// THE exception: it ticks at end of turn, and it is the only power that does.
TEST(IntangiblePower, TicksDownAtEndOfTurn) {
  CombatState s = bare_fight();
  s.character.powers[Power::Intangible] = 2;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.powers, Power::Intangible), 1);

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.powers, Power::Intangible), 0)
      << "Intangible did not expire";
}

// And no OTHER power ticks — the exception must stay an exception.
//
// Block is stacked high deliberately: the enemy attacks during the turn
// boundary, and an unblocked hit would SPEND a Buffer stack. That is correct
// behaviour and would look exactly like a tick here, so the two are separated
// rather than left to coincide.
TEST(IntangiblePower, OtherPowersStillDoNotTick) {
  CombatState s = bare_fight();
  s.character.powers[Power::Strength] = 3;
  s.character.powers[Power::Buffer] = 2;
  s.character.current_block = 500;

  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 3);
  EXPECT_EQ(get_status(s.character.powers, Power::Buffer), 2)
      << "Buffer ticked like a duration effect";
}

// One stack protects the enemy phase that follows, then expires — which is what
// makes Incense Burner worth holding.
TEST(IntangiblePower, IncenseBurnerFiresEverySixthTurn) {
  CombatState s = fight_with({RelicId::IncenseBurner});
  ASSERT_EQ(get_status(s.character.powers, Power::Intangible), 0)
      << "turn 1 should not fire";

  for (int turn = 2; turn <= 5; ++turn) {
    ASSERT_TRUE(apply_action(s, kEndTurnAction));
    ASSERT_EQ(s.outcome, Outcome::InProgress);
    EXPECT_EQ(get_status(s.character.powers, Power::Intangible), 0)
        << "turn " << turn << " should not fire";
  }

  ASSERT_TRUE(apply_action(s, kEndTurnAction));  // turn 6
  ASSERT_EQ(s.outcome, Outcome::InProgress);
  EXPECT_EQ(get_status(s.character.powers, Power::Intangible), 1)
      << "Incense Burner did not fire on turn 6";
}

// ===================================================================== batch A1
// Vigor and Pen Nib's charge: next-attack modifiers, consumed PER CARD.

namespace {

// Damage the first enemy takes from one Strike, with the fight's state as given.
int strike_damage(CombatState& s) {
  for (Enemy& e : s.enemies) {
    e.max_hp = 9999;
    e.hp = 9999;
    e.current_block = 0;
  }
  s.current_hand.clear();
  s.current_hand.push_back(Card{CardId::Strike});
  s.character.energy = 99;
  const int before = s.enemies[0].hp;
  EXPECT_TRUE(apply_action(s, card_action(CardId::Strike, 0)));
  return before - s.enemies[0].hp;
}

}  // namespace

TEST(NextAttackPowers, VigorAddsToTheNextAttack) {
  CombatState bare = bare_fight();
  const int base = strike_damage(bare);

  CombatState s = bare_fight();
  s.character.powers[Power::Vigor] = 8;
  EXPECT_EQ(strike_damage(s), base + 8);
}

// Consumed after ONE card, not carried to the next.
TEST(NextAttackPowers, VigorIsSpentByTheFirstAttack) {
  CombatState bare = bare_fight();
  const int base = strike_damage(bare);

  CombatState s = bare_fight();
  s.character.powers[Power::Vigor] = 8;
  ASSERT_EQ(strike_damage(s), base + 8);
  EXPECT_EQ(strike_damage(s), base) << "Vigor survived the attack that spent it";
  EXPECT_EQ(get_status(s.character.powers, Power::Vigor), 0);
}

// Vigor is ADDITIVE with Strength, so Weak and Vulnerable scale it. Applying it
// after the multipliers would make it immune to Weak.
TEST(NextAttackPowers, VigorIsScaledByWeak) {
  CombatState plain = bare_fight();
  plain.character.powers[Power::Vigor] = 8;
  const int unweakened = strike_damage(plain);

  CombatState weak = bare_fight();
  weak.character.powers[Power::Vigor] = 8;
  weak.character.debuffs[Debuff::Weak] = 1;
  EXPECT_LT(strike_damage(weak), unweakened)
      << "Weak did not scale the Vigor portion";
}

// A Skill must not spend it — "your next ATTACK".
TEST(NextAttackPowers, VigorSurvivesASkill) {
  CombatState s = bare_fight();
  s.character.powers[Power::Vigor] = 8;
  s.current_hand.clear();
  s.current_hand.push_back(Card{CardId::Defend});
  s.character.energy = 99;
  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend, 0)));
  EXPECT_EQ(get_status(s.character.powers, Power::Vigor), 8)
      << "a Skill spent a next-ATTACK power";
}

TEST(NextAttackPowers, PenNibChargeDoublesTheNextAttack) {
  CombatState bare = bare_fight();
  const int base = strike_damage(bare);

  CombatState s = bare_fight();
  s.character.powers[Power::PenNibCharge] = 1;
  EXPECT_EQ(strike_damage(s), base * 2);
}

TEST(NextAttackPowers, PenNibChargeIsSpentByTheFirstAttack) {
  CombatState bare = bare_fight();
  const int base = strike_damage(bare);

  CombatState s = bare_fight();
  s.character.powers[Power::PenNibCharge] = 1;
  ASSERT_EQ(strike_damage(s), base * 2);
  EXPECT_EQ(strike_damage(s), base);
}

// ---------------------------------------------------------- the two relics

TEST(NextAttackPowers, AkabekoGrantsEightVigorAtCombatStart) {
  const CombatState s = fight_with({RelicId::Akabeko});
  EXPECT_EQ(get_status(s.character.powers, Power::Vigor), 8);
}

TEST(NextAttackPowers, AkabekoBoostsOnlyTheFirstAttack) {
  CombatState bare = bare_fight();
  const int base = strike_damage(bare);

  CombatState s = fight_with({RelicId::Akabeko});
  ASSERT_EQ(strike_damage(s), base + 8);
  EXPECT_EQ(strike_damage(s), base) << "Akabeko boosted a second Attack";
}

// The TENTH Attack must be doubled itself, not the eleventh. The charge is
// granted on the ninth so the tenth can consume it — granting on the tenth
// would boost the one after.
TEST(NextAttackPowers, PenNibDoublesTheTenthAttackNotTheEleventh) {
  CombatState bare = bare_fight();
  const int base = strike_damage(bare);

  CombatState s = fight_with({RelicId::PenNib});
  for (int i = 1; i <= 9; ++i) {
    EXPECT_EQ(strike_damage(s), base) << "attack " << i << " was boosted";
  }
  EXPECT_EQ(strike_damage(s), base * 2) << "the tenth Attack was not doubled";
  EXPECT_EQ(strike_damage(s), base) << "the eleventh Attack was doubled";
}

// ====================================================================== batch 5
// Card-play relics. Two counter kinds on ONE hook: per-turn and persistent.

namespace {

// Play `n` Strikes at the first living enemy. Returns false if the fight ended.
bool play_strikes(CombatState& s, int n) {
  for (int i = 0; i < n; ++i) {
    // Refill hand and energy so the play always succeeds — this exercises the
    // relic counters, not the resource economy.
    s.current_hand.clear();
    s.current_hand.push_back(Card{CardId::Strike});
    s.character.energy = 99;
    for (Enemy& e : s.enemies) {
      e.max_hp = 9999;
      e.hp = 9999;
    }
    if (!apply_action(s, card_action(CardId::Strike, 0))) return false;
    if (s.outcome != Outcome::InProgress) return false;
  }
  return true;
}

}  // namespace

// ------------------------------------------------- per-turn: 3 in ONE turn

TEST(CardPlayRelics, ShurikenGivesStrengthEveryThreeAttacksInATurn) {
  CombatState s = fight_with({RelicId::Shuriken});
  ASSERT_TRUE(play_strikes(s, 2));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0) << "after 2";

  ASSERT_TRUE(play_strikes(s, 1));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 1) << "after 3";

  ASSERT_TRUE(play_strikes(s, 3));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 2) << "after 6";
}

TEST(CardPlayRelics, KunaiGivesDexterityEveryThreeAttacksInATurn) {
  CombatState s = fight_with({RelicId::Kunai});
  ASSERT_TRUE(play_strikes(s, 3));
  EXPECT_EQ(get_status(s.character.powers, Power::Dexterity), 1);
}

// The distinguishing test. Three Attacks spread across three turns must do
// NOTHING — "3 Attacks in a single turn". Without the turn-end reset this
// passes for Nunchaku's reason and fails the relic.
TEST(CardPlayRelics, ShurikenResetsBetweenTurns) {
  CombatState s = fight_with({RelicId::Shuriken});
  for (int turn = 0; turn < 3; ++turn) {
    ASSERT_TRUE(play_strikes(s, 2));
    ASSERT_TRUE(apply_action(s, kEndTurnAction));
    ASSERT_EQ(s.outcome, Outcome::InProgress);
  }
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0)
      << "two Attacks a turn for three turns triggered a per-turn relic";
}

TEST(CardPlayRelics, OrnamentalFanGivesBlockEveryThreeAttacks) {
  CombatState s = fight_with({RelicId::OrnamentalFan});
  const int before = s.character.current_block;
  ASSERT_TRUE(play_strikes(s, 3));
  EXPECT_EQ(s.character.current_block, before + 4);
}

// ------------------------------------------------- persistent: 10, ever

TEST(CardPlayRelics, NunchakuGivesEnergyEveryTenAttacks) {
  CombatState s = fight_with({RelicId::Nunchaku});
  const int energy_before = s.character.energy;
  ASSERT_TRUE(play_strikes(s, 9));
  // play_strikes sets energy to 99 before each play, so measure the counter.
  int counter = -1;
  for (const HeldRelic& r : s.relics) {
    if (r.id == RelicId::Nunchaku) counter = r.counter;
  }
  EXPECT_EQ(counter, 9) << "after 9 Attacks";

  ASSERT_TRUE(play_strikes(s, 1));
  for (const HeldRelic& r : s.relics) {
    if (r.id == RelicId::Nunchaku) counter = r.counter;
  }
  EXPECT_EQ(counter, 0) << "the tenth Attack did not reset the counter";
  (void)energy_before;
}

// The other half of the distinction: Nunchaku's counter must SURVIVE a turn
// boundary, where Shuriken's is cleared.
TEST(CardPlayRelics, NunchakusCounterSurvivesTheTurnBoundary) {
  CombatState s = fight_with({RelicId::Nunchaku});
  ASSERT_TRUE(play_strikes(s, 4));
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  ASSERT_EQ(s.outcome, Outcome::InProgress);

  int counter = -1;
  for (const HeldRelic& r : s.relics) {
    if (r.id == RelicId::Nunchaku) counter = r.counter;
  }
  EXPECT_EQ(counter, 4) << "a persistent counter was cleared at turn end";
}

// And across COMBATS — the wiki says "not reset between turns or combats".
TEST(CardPlayRelics, NunchakusCounterSurvivesTheWholeFight) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Nunchaku);
  run.begin_combat(EncounterPool::Weak);
  ASSERT_TRUE(play_strikes(run.combat, 4));
  run.combat.character.hp = 60;
  run.end_combat();
  run.skip_card_reward();

  run.floor = 8;
  run.begin_combat(EncounterPool::Weak);
  int counter = -1;
  for (const HeldRelic& r : run.combat.relics) {
    if (r.id == RelicId::Nunchaku) counter = r.counter;
  }
  EXPECT_EQ(counter, 4) << "the counter reset between combats";
}

// Ink Bottle counts EVERY card, not just Attacks.
TEST(CardPlayRelics, InkBottleCountsEveryCardType) {
  CombatState s = fight_with({RelicId::InkBottle});
  ASSERT_TRUE(play_strikes(s, 3));

  s.current_hand.clear();
  s.current_hand.push_back(Card{CardId::Defend});
  s.character.energy = 99;
  ASSERT_TRUE(apply_action(s, card_action(CardId::Defend, 0)));

  int counter = -1;
  for (const HeldRelic& r : s.relics) {
    if (r.id == RelicId::InkBottle) counter = r.counter;
  }
  EXPECT_EQ(counter, 4) << "Ink Bottle ignored a Skill";
}

// ---------------------------------------------------------- Bird-Faced Urn

TEST(CardPlayRelics, BirdFacedUrnDoesNotHealOnAnAttack) {
  CombatState s = fight_with({RelicId::BirdFacedUrn});
  s.character.hp = 40;
  ASSERT_TRUE(play_strikes(s, 1));
  EXPECT_EQ(s.character.hp, 40);
}

// ====================================================================== batch 4
// Turn-end and combat-end relics.

namespace {

// A run that fights, ends the fight at `end_hp`, and returns. The relics are
// obtained BEFORE the fight so they are projected into it.
RunState won_a_fight_at(int end_hp, std::vector<RelicId> ids,
                        uint64_t seed = 1) {
  RunState run = RunState::start(seed);
  for (RelicId id : ids) run.obtain_relic(id);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = end_hp;
  run.end_combat();
  return run;
}

}  // namespace

// --------------------------------------------------------- Burning Blood

// The starter relic, inert until now. Every Ironclad run holds it.
TEST(CombatEndRelics, BurningBloodHealsSixOnAWin) {
  const RunState run = won_a_fight_at(40, {});
  EXPECT_EQ(run.hp, 46);
}

TEST(CombatEndRelics, BurningBloodCannotHealAboveMaxHp) {
  RunState run = RunState::start(1);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = run.max_hp - 2;
  run.end_combat();
  EXPECT_EQ(run.hp, run.max_hp);
}

// A dead Ironclad does not heal 6 and get back up. Lizard Tail is the relic for
// that, and it is a different hook.
TEST(CombatEndRelics, BurningBloodDoesNotFireOnALoss) {
  RunState run = RunState::start(1);
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 0;
  run.end_combat();
  EXPECT_EQ(run.hp, 0);
  EXPECT_EQ(run.outcome, Outcome::Lost);
}

// -------------------------------------------------------- Meat on the Bone

TEST(CombatEndRelics, MeatOnTheBoneHealsTwelveWhenLow) {
  // 20 of 80 is well under half, and stays under after Burning Blood's 6.
  const RunState run = won_a_fight_at(20, {RelicId::MeatOnTheBone});
  EXPECT_EQ(run.hp, 20 + 6 + 12);
}

TEST(CombatEndRelics, MeatOnTheBoneDoesNothingWhenHealthy) {
  const RunState run = won_a_fight_at(70, {RelicId::MeatOnTheBone});
  EXPECT_EQ(run.hp, 70 + 6) << "Meat on the Bone fired above half HP";
}

// The ordering case, and the reason acquisition order matters. Burning Blood is
// always acquired first (it is the starter), so its 6 HP is applied BEFORE Meat
// on the Bone reads the threshold — and can lift the player over it.
//
// At 35 of 80: below half (40) on its own, but 41 after Burning Blood. So Meat
// on the Bone must NOT fire. Nothing special-cases this; iterating the relic
// vector in acquisition order is what makes it right.
TEST(CombatEndRelics, BurningBloodCanLiftYouOverMeatOnTheBonesThreshold) {
  const RunState run = won_a_fight_at(35, {RelicId::MeatOnTheBone});
  EXPECT_EQ(run.hp, 41)
      << "Meat on the Bone read the threshold before Burning Blood healed";
}

// And just below that boundary it does fire: 34 -> 40, which is exactly half.
TEST(CombatEndRelics, MeatOnTheBoneFiresAtExactlyHalf) {
  const RunState run = won_a_fight_at(34, {RelicId::MeatOnTheBone});
  EXPECT_EQ(run.hp, 34 + 6 + 12) << "'at or below 50%' excluded exactly 50%";
}

// ------------------------------------------------------------- Orichalcum

TEST(TurnEndRelics, OrichalcumGivesBlockWhenYouEndTurnWithNone) {
  CombatState s = fight_with({RelicId::Orichalcum});
  s.character.current_block = 0;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  // The block is granted at end of turn and then cleared at the next turn's
  // start, so what it actually does is absorb the enemy's attack in between.
  SUCCEED();
}

// EXACTLY zero, not "low" — one point of block suppresses it entirely.
TEST(TurnEndRelics, OrichalcumIsSuppressedByASinglePointOfBlock) {
  CombatState with_none = fight_with({RelicId::Orichalcum});
  CombatState with_one = fight_with({RelicId::Orichalcum});
  with_none.character.current_block = 0;
  with_one.character.current_block = 1;

  const int hp_none_before = with_none.character.hp;
  const int hp_one_before = with_one.character.hp;
  ASSERT_TRUE(apply_action(with_none, kEndTurnAction));
  ASSERT_TRUE(apply_action(with_one, kEndTurnAction));

  // The 6 block absorbs more than the 1 block does, so the no-block run should
  // have taken no more damage than the one-block run.
  EXPECT_LE(hp_none_before - with_none.character.hp,
            hp_one_before - with_one.character.hp)
      << "one point of block did not suppress Orichalcum";
}

// ---------------------------------------------------------- Stone Calendar

// Turn 7 ONLY, and 52 fixed damage — not an attack, so Strength and Vulnerable
// do not scale it.
TEST(TurnEndRelics, StoneCalendarFiresAtTheEndOfTurnSeven) {
  CombatState s = fight_with({RelicId::StoneCalendar});
  ASSERT_FALSE(s.enemies.empty());

  // Turns 1-6 must not fire. Give the enemies enough HP to survive the hit so
  // the fight does not end before turn 7.
  for (Enemy& e : s.enemies) {
    e.max_hp = 500;
    e.hp = 500;
  }

  for (int turn = 1; turn <= 6; ++turn) {
    const int hp_before = s.enemies[0].hp;
    ASSERT_TRUE(apply_action(s, kEndTurnAction));
    ASSERT_EQ(s.outcome, Outcome::InProgress) << "turn " << turn;
    EXPECT_LT(hp_before - s.enemies[0].hp, kStoneCalendarDamage)
        << "Stone Calendar fired on turn " << turn;
  }

  ASSERT_EQ(s.turn_number, 7);
  // Clear enemy block: Stone Calendar's damage is absorbed by block like any
  // other fixed damage, and an enemy that gained block on turn 6 would eat part
  // of the hit and make this read low for the wrong reason.
  for (Enemy& e : s.enemies) e.current_block = 0;
  const int hp_before = s.enemies[0].hp;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_GE(hp_before - s.enemies[0].hp, kStoneCalendarDamage)
      << "Stone Calendar did not fire at the end of turn 7";
}

// ===================================================================== batch 3b
// Turn-start relics. Turn 1's start IS combat start, so both call sites route
// through one function — counting turns in only one of them is the bug.

namespace {

// Advance one full turn boundary, asserting the fight is still live.
void take_a_turn(CombatState& state) {
  ASSERT_TRUE(apply_action(state, kEndTurnAction));
  ASSERT_EQ(state.outcome, Outcome::InProgress);
}

int player_strength(const CombatState& s) {
  return get_status(s.character.powers, Power::Strength);
}

}  // namespace

// ------------------------------------------------------------- Brimstone

// EVERY turn, not once per fight. A combat-start reading caps it at +2 for the
// whole fight, which is how it was originally misclassified (§6.3).
TEST(TurnStartRelics, BrimstoneStrengthensEveryTurn) {
  CombatState s = fight_with({RelicId::Brimstone});
  EXPECT_EQ(player_strength(s), 2) << "turn 1";

  take_a_turn(s);
  EXPECT_EQ(player_strength(s), 4) << "turn 2 — Brimstone fired only once";

  take_a_turn(s);
  EXPECT_EQ(player_strength(s), 6) << "turn 3";
}

// The drawback compounds too: every enemy gains 1 Strength every turn.
TEST(TurnStartRelics, BrimstoneAlsoStrengthensEveryEnemyEveryTurn) {
  CombatState s = fight_with({RelicId::Brimstone});
  ASSERT_FALSE(s.enemies.empty());
  for (const Enemy& e : s.enemies) {
    if (e.hp > 0) EXPECT_EQ(get_status(e.powers, Power::Strength), 1);
  }

  take_a_turn(s);
  for (const Enemy& e : s.enemies) {
    if (e.hp > 0) EXPECT_EQ(get_status(e.powers, Power::Strength), 2);
  }
}

// ----------------------------------------------------------- Happy Flower

// Fires on the THIRD turn, not the first — the counter starts at 0 and turn 1
// takes it to 1.
TEST(TurnStartRelics, HappyFlowerFiresEveryThirdTurn) {
  CombatState s = fight_with({RelicId::HappyFlower});
  const int base = s.character.energy_per_turn;
  EXPECT_EQ(s.character.energy, base) << "turn 1 should not fire";

  take_a_turn(s);
  EXPECT_EQ(s.character.energy, base) << "turn 2 should not fire";

  take_a_turn(s);
  EXPECT_EQ(s.character.energy, base + 1) << "turn 3 should fire";

  take_a_turn(s);
  EXPECT_EQ(s.character.energy, base) << "the counter did not reset";
}

// Turn 1 must increment exactly once. If combat start and the turn boundary
// both counted it, this would read 2 after one turn rather than 1.
TEST(TurnStartRelics, TurnOneCountsOnceNotTwice) {
  CombatState s = fight_with({RelicId::HappyFlower});
  ASSERT_EQ(s.relics.size(), 1u);
  EXPECT_EQ(s.relics[0].counter, 1) << "turn 1 was double-counted";
}

// The counter is RUN-scoped: a fight that ends with it at 2 fires on the first
// turn of the next fight. This is §3.3's whole purpose, and the wiki states it
// as the relic's behaviour.
TEST(TurnStartRelics, HappyFlowersCounterCarriesIntoTheNextFight) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::HappyFlower);

  run.begin_combat(EncounterPool::Weak);
  take_a_turn(run.combat);  // counter: 1 -> 2
  ASSERT_EQ(run.combat.relics.size(), 2u);  // Burning Blood + Happy Flower
  int counter = -1;
  for (const HeldRelic& r : run.combat.relics) {
    if (r.id == RelicId::HappyFlower) counter = r.counter;
  }
  ASSERT_EQ(counter, 2) << "two turns should leave the counter at 2";

  run.combat.character.hp = 60;
  run.end_combat();
  run.skip_card_reward();

  // Next fight: turn 1 takes the counter from 2 to 3, so it fires immediately.
  run.floor = 8;
  run.begin_combat(EncounterPool::Weak);
  EXPECT_EQ(run.combat.character.energy,
            run.combat.character.energy_per_turn + 1)
      << "a carried counter did not fire on the next fight's first turn";
}

// ===================================================================== batch 3a
// Combat-start effects beyond the simple self-buffs from batch 0.

TEST(CombatStartRelics, PhilosophersStoneStrengthensEveryEnemy) {
  const CombatState with = fight_with({RelicId::PhilosophersStone});
  ASSERT_FALSE(with.enemies.empty());
  for (const Enemy& e : with.enemies) {
    EXPECT_EQ(get_status(e.powers, Power::Strength), 1)
        << "an enemy escaped Philosopher's Stone";
  }
}

TEST(CombatStartRelics, GremlinVisageStartsYouWeakened) {
  const CombatState with = fight_with({RelicId::GremlinVisage});
  EXPECT_EQ(get_status(with.character.debuffs, Debuff::Weak), 1);
}

TEST(CombatStartRelics, ClockworkSouvenirGrantsArtifact) {
  const CombatState with = fight_with({RelicId::ClockworkSouvenir});
  EXPECT_EQ(get_status(with.character.powers, Power::Artifact), 1);
}

// ------------------------------------------------- the elite-only relics

TEST(CombatStartRelics, SlingOfCourageGivesNothingInANormalFight) {
  const CombatState with = fight_with({RelicId::SlingOfCourage});
  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 0);
}

TEST(CombatStartRelics, SlingOfCourageGivesTwoStrengthInAnElite) {
  const CombatState with = elite_fight_with({RelicId::SlingOfCourage});
  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 2);
}

TEST(CombatStartRelics, PreservedInsectDoesNothingInANormalFight) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::PreservedInsect});
  ASSERT_EQ(bare.enemies.size(), with.enemies.size());
  for (size_t i = 0; i < bare.enemies.size(); ++i) {
    EXPECT_EQ(with.enemies[i].hp, bare.enemies[i].hp);
  }
}

// 25% off current HP, and MAX HP untouched — the wiki is explicit that the cut
// is to current HP "as if they had taken damage", so a healed elite can climb
// back to its full maximum.
TEST(CombatStartRelics, PreservedInsectCutsEliteHpButNotMaxHp) {
  const CombatState bare = elite_fight_with({});
  const CombatState with = elite_fight_with({RelicId::PreservedInsect});
  ASSERT_EQ(bare.enemies.size(), with.enemies.size());
  ASSERT_FALSE(bare.enemies.empty());

  for (size_t i = 0; i < bare.enemies.size(); ++i) {
    EXPECT_EQ(with.enemies[i].hp, bare.enemies[i].hp * 3 / 4)
        << "enemy " << i;
    EXPECT_EQ(with.enemies[i].max_hp, bare.enemies[i].max_hp)
        << "Preserved Insect lowered Max HP, which it must not";
  }
}

// It must not route through the damage path: that would fire the on-damaged
// hooks and wake a sleeping Lagavulin before the fight had begun.
TEST(CombatStartRelics, PreservedInsectDoesNotWakeASleepingEnemy) {
  CombatState with = elite_fight_with({RelicId::PreservedInsect});
  for (const Enemy& e : with.enemies) {
    // Only asserts about enemies that start asleep; in Act 1 elites none do,
    // so this is a guard against a future roster rather than a live case.
    if (e.is_asleep) {
      EXPECT_TRUE(e.is_asleep) << "the HP cut woke a sleeping enemy";
    }
  }
  SUCCEED();
}

// ------------------------------------------------------------ Mark of Pain

TEST(CombatStartRelics, MarkOfPainShufflesTwoWoundsIntoTheDrawPile) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::MarkOfPain});
  EXPECT_EQ(with.draw_pile.size(), bare.draw_pile.size() + 2);

  int wounds = 0;
  for (const Card& c : with.draw_pile) {
    if (c.card_id == CardId::Wound) ++wounds;
  }
  EXPECT_EQ(wounds, 2);
}

// They go to the DRAW PILE, not the hand — and because this hook fires after
// the opening hand is dealt, a Wound can never appear in the opening five.
TEST(CombatStartRelics, MarkOfPainsWoundsAreNeverInTheOpeningHand) {
  for (uint32_t s = 0; s < 40; ++s) {
    const CombatState with = fight_with({RelicId::MarkOfPain}, s);
    for (const Card& c : with.current_hand) {
      EXPECT_NE(c.card_id, CardId::Wound)
          << "seed " << s << ": a Wound reached the opening hand";
    }
  }
}

// ------------------------------------------------------------- Du-Vu Doll

TEST(CombatStartRelics, DuVuDollGivesNothingWithACleanDeck) {
  const CombatState with = fight_with({RelicId::DuVuDoll});
  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 0);
}

// The per-curse half cannot be tested yet: CardType::Curse exists as an enum
// value but NO curse card is implemented, so a deck's curse count is always
// zero. Du-Vu Doll is therefore correct-but-unreachable, the same state as
// Cursed Key — recorded in relic-effects.md §6.4 rather than left as a silent
// gap. When curses land, this is where the test goes.

// ===================================================================== batch 1a
// Query-layer damage modifiers. These have no event hook: they change a value
// that is computed, the way DamageRule does for cards.

// ------------------------------------------------------------ Strike Dummy

TEST(RelicQuery, StrikeDummyAddsThreeToStrikeNamedCards) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::StrikeDummy});
  const Card strike{CardId::Strike};
  EXPECT_EQ(instance_card_damage(with, strike),
            instance_card_damage(bare, strike) + 3);
}

// The test is on the NAME, not the card type or a hardcoded id list. Bash is an
// Attack and is not boosted; Perfected Strike is boosted despite not being a
// basic Strike.
TEST(RelicQuery, StrikeDummyIgnoresAttacksWithoutStrikeInTheName) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::StrikeDummy});
  const Card bash{CardId::Bash};
  EXPECT_EQ(instance_card_damage(with, bash), instance_card_damage(bare, bash));
}

TEST(RelicQuery, StrikeDummyBoostsPerfectedStrikeToo) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::StrikeDummy});
  const Card ps{CardId::PerfectedStrike};
  EXPECT_EQ(instance_card_damage(with, ps),
            instance_card_damage(bare, ps) + 3);
}

// ------------------------------------------------------------- Paper Phrog

TEST(RelicQuery, PaperPhrogRaisesTheVulnerableMultiplier) {
  EXPECT_FLOAT_EQ(vulnerable_damage_multiplier(bare_fight()), 1.5f);
  EXPECT_FLOAT_EQ(vulnerable_damage_multiplier(fight_with({RelicId::PaperPhrog})),
                  1.75f);
}

// 10 base damage into a Vulnerable enemy: 15 normally, 17 with Paper Phrog
// (10 * 1.75 = 17.5, truncated once at the end).
TEST(RelicQuery, PaperPhrogChangesActualAttackDamage) {
  std::unordered_map<Power, int> no_powers;
  std::unordered_map<Debuff, int> no_debuffs;
  std::unordered_map<Debuff, int> vulnerable{{Debuff::Vulnerable, 1}};

  EXPECT_EQ(compute_attack_damage(10, no_powers, no_debuffs, vulnerable, 1, 1.5f),
            15);
  EXPECT_EQ(compute_attack_damage(10, no_powers, no_debuffs, vulnerable, 1, 1.75f),
            17);
}

// Paper Phrog is the PLAYER's relic and must not make enemies hit harder. The
// enemy-intent path leaves the multiplier at its default, so this asserts the
// default is the unmodified one rather than something the relic can reach.
TEST(RelicQuery, PaperPhrogDoesNotApplyToEnemyAttacks) {
  std::unordered_map<Power, int> no_powers;
  std::unordered_map<Debuff, int> no_debuffs;
  std::unordered_map<Debuff, int> vulnerable{{Debuff::Vulnerable, 1}};
  // The default argument — what every enemy-side caller uses.
  EXPECT_EQ(compute_attack_damage(10, no_powers, no_debuffs, vulnerable), 15);
}

// ---------------------------------------------------------------- The Boot

TEST(RelicQuery, TheBootRaisesSmallUnblockedDamageToFive) {
  const CombatState with = fight_with({RelicId::TheBoot});
  for (int d = 1; d <= 4; ++d) {
    EXPECT_EQ(boot_adjusted_damage(with, d), 5) << "unblocked " << d;
  }
}

TEST(RelicQuery, TheBootLeavesFiveOrMoreAlone) {
  const CombatState with = fight_with({RelicId::TheBoot});
  EXPECT_EQ(boot_adjusted_damage(with, 5), 5);
  EXPECT_EQ(boot_adjusted_damage(with, 9), 9);
}

// Zero is excluded. An attack reduced to nothing — fully blocked, or zeroed by
// negative Strength — is not raised to 5, which is the edge the wiki calls out
// explicitly and the one a naive `d < 5` test gets wrong.
TEST(RelicQuery, TheBootDoesNotResurrectZeroDamage) {
  const CombatState with = fight_with({RelicId::TheBoot});
  EXPECT_EQ(boot_adjusted_damage(with, 0), 0);
}

TEST(RelicQuery, TheBootDoesNothingWithoutTheRelic) {
  const CombatState bare = bare_fight();
  for (int d = 0; d <= 6; ++d) {
    EXPECT_EQ(boot_adjusted_damage(bare, d), d);
  }
}

// The integration case: block absorbs first, and only what survives is judged.
// An enemy with 10 block hit for 3 takes nothing — The Boot must not turn a
// fully-absorbed attack into 5 damage to HP.
TEST(RelicQuery, TheBootRespectsBlockBeforeJudging) {
  CombatState with = fight_with({RelicId::TheBoot});
  ASSERT_FALSE(with.enemies.empty());
  with.enemies[0].current_block = 10;
  const int hp_before = with.enemies[0].hp;

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::DealDamage};
  a.actor = kPlayerSlot;
  a.target = 0;
  a.amount = 3;
  q.push_back(a);
  drain(with, q, ctx);

  EXPECT_EQ(with.enemies[0].hp, hp_before) << "a fully blocked attack got through";
  EXPECT_EQ(with.enemies[0].current_block, 7);
}

// And the other half: block that only partly absorbs leaves a remainder, and
// THAT remainder is what gets raised.
TEST(RelicQuery, TheBootRaisesTheRemainderAfterPartialBlock) {
  CombatState with = fight_with({RelicId::TheBoot});
  ASSERT_FALSE(with.enemies.empty());
  with.enemies[0].current_block = 4;
  const int hp_before = with.enemies[0].hp;

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::DealDamage};
  a.actor = kPlayerSlot;
  a.target = 0;
  a.amount = 6;  // 4 blocked, 2 through -> raised to 5
  q.push_back(a);
  drain(with, q, ctx);

  EXPECT_EQ(with.enemies[0].current_block, 0);
  EXPECT_EQ(with.enemies[0].hp, hp_before - 5);
}

// ===================================================================== batch 1b
// Query-layer rules about what happens to the player.

// ------------------------------------------------------- Ginger and Turnip

TEST(RelicQuery, GingerBlocksWeak) {
  CombatState with = fight_with({RelicId::Ginger});
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::ApplyDebuff};
  a.target = kPlayerSlot;
  a.debuff = Debuff::Weak;
  a.amount = 2;
  q.push_back(a);
  drain(with, q, ctx);
  EXPECT_EQ(get_status(with.character.debuffs, Debuff::Weak), 0);
}

TEST(RelicQuery, GingerDoesNotBlockOtherDebuffs) {
  CombatState with = fight_with({RelicId::Ginger});
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::ApplyDebuff};
  a.target = kPlayerSlot;
  a.debuff = Debuff::Vulnerable;
  a.amount = 2;
  q.push_back(a);
  drain(with, q, ctx);
  EXPECT_EQ(get_status(with.character.debuffs, Debuff::Vulnerable), 2);
}

TEST(RelicQuery, TurnipBlocksFrail) {
  CombatState with = fight_with({RelicId::Turnip});
  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::ApplyDebuff};
  a.target = kPlayerSlot;
  a.debuff = Debuff::Frail;
  a.amount = 3;
  q.push_back(a);
  drain(with, q, ctx);
  EXPECT_EQ(get_status(with.character.debuffs, Debuff::Frail), 0);
}

// The ordering case. Ginger is consulted BEFORE Artifact, so the charge
// survives — a debuff that could never land must not cost one. Checking
// immunity after Artifact would pass every other test in this file and fail
// only here.
TEST(RelicQuery, GingerDoesNotConsumeAnArtifactCharge) {
  CombatState with = fight_with({RelicId::Ginger});
  with.character.powers[Power::Artifact] = 1;

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::ApplyDebuff};
  a.target = kPlayerSlot;
  a.debuff = Debuff::Weak;
  a.amount = 1;
  q.push_back(a);
  drain(with, q, ctx);

  EXPECT_EQ(get_status(with.character.debuffs, Debuff::Weak), 0);
  EXPECT_EQ(get_status(with.character.powers, Power::Artifact), 1)
      << "Ginger spent an Artifact charge on a debuff it already prevented";
}

TEST(RelicQuery, TurnipDoesNotConsumeAnArtifactCharge) {
  CombatState with = fight_with({RelicId::Turnip});
  with.character.powers[Power::Artifact] = 1;

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::ApplyDebuff};
  a.target = kPlayerSlot;
  a.debuff = Debuff::Frail;
  a.amount = 1;
  q.push_back(a);
  drain(with, q, ctx);

  EXPECT_EQ(get_status(with.character.powers, Power::Artifact), 1);
}

// Without the relic, Artifact still does its job — the new early-out must not
// have shadowed it.
TEST(RelicQuery, ArtifactStillAbsorbsWeakWithoutGinger) {
  CombatState bare = bare_fight();
  bare.character.powers[Power::Artifact] = 1;

  ActionQueue q;
  ResolutionContext ctx;
  Action a{ActionKind::ApplyDebuff};
  a.target = kPlayerSlot;
  a.debuff = Debuff::Weak;
  a.amount = 1;
  q.push_back(a);
  drain(bare, q, ctx);

  EXPECT_EQ(get_status(bare.character.debuffs, Debuff::Weak), 0);
  EXPECT_EQ(get_status(bare.character.powers, Power::Artifact), 0)
      << "Artifact should have been spent";
}

// ----------------------------------------------------------------- Calipers

TEST(RelicQuery, CalipersLosesFifteenBlockNotAllOfIt) {
  CombatState with = fight_with({RelicId::Calipers});
  with.character.current_block = 40;
  EXPECT_EQ(block_after_turn_start(with), 25);
}

TEST(RelicQuery, CalipersCannotGoNegative) {
  CombatState with = fight_with({RelicId::Calipers});
  with.character.current_block = 8;
  EXPECT_EQ(block_after_turn_start(with), 0);
}

TEST(RelicQuery, WithoutCalipersAllBlockIsLost) {
  CombatState bare = bare_fight();
  bare.character.current_block = 40;
  EXPECT_EQ(block_after_turn_start(bare), 0);
}

// Barricade is strictly stronger than Calipers: "block is not removed" beats
// "lose 15 rather than all", so holding both keeps everything.
TEST(RelicQuery, BarricadeBeatsCalipers) {
  CombatState with = fight_with({RelicId::Calipers});
  with.character.current_block = 40;
  with.character.powers[Power::Barricade] = 1;
  EXPECT_EQ(block_after_turn_start(with), 40);
}

// ------------------------------------------------------------ Runic Pyramid

TEST(RelicQuery, RunicPyramidKeepsTheHandAtEndOfTurn) {
  CombatState with = fight_with({RelicId::RunicPyramid});
  const size_t hand_before = with.current_hand.size();
  ASSERT_GT(hand_before, 0u);

  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(Action{ActionKind::DiscardHand});
  drain(with, q, ctx);

  EXPECT_EQ(with.current_hand.size(), hand_before);
  EXPECT_TRUE(with.discard_pile.empty());
}

TEST(RelicQuery, WithoutRunicPyramidTheHandDiscards) {
  CombatState bare = bare_fight();
  const size_t hand_before = bare.current_hand.size();
  ASSERT_GT(hand_before, 0u);

  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(Action{ActionKind::DiscardHand});
  drain(bare, q, ctx);

  EXPECT_TRUE(bare.current_hand.empty());
  EXPECT_EQ(bare.discard_pile.size(), hand_before);
}

// Runic Pyramid is not a blanket "nothing leaves". A Burn deals its damage and
// STILL leaves the hand — the wiki lists the end-of-turn-effect family as
// bypassing the relic. Keeping it would strand the Burn in hand, dealing its
// damage every turn for the rest of the fight.
TEST(RelicQuery, RunicPyramidDoesNotStrandABurnInHand) {
  CombatState with = fight_with({RelicId::RunicPyramid});
  with.current_hand.clear();
  with.current_hand.push_back(Card{CardId::Burn});
  with.current_hand.push_back(Card{CardId::Strike});
  const int hp_before = with.character.hp;

  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(Action{ActionKind::DiscardHand});
  drain(with, q, ctx);

  ASSERT_EQ(with.current_hand.size(), 1u) << "the Burn should have left";
  EXPECT_EQ(with.current_hand[0].card_id, CardId::Strike);
  EXPECT_LT(with.character.hp, hp_before) << "the Burn dealt no damage";
}

// Ethereal cards exhaust rather than discard, and exhausting is a different
// fate the relic does not prevent.
TEST(RelicQuery, RunicPyramidStillLetsEtherealCardsExhaust) {
  CombatState with = fight_with({RelicId::RunicPyramid});
  with.current_hand.clear();
  with.current_hand.push_back(Card{CardId::Dazed});
  with.current_hand.push_back(Card{CardId::Strike});

  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(Action{ActionKind::DiscardHand});
  drain(with, q, ctx);

  ASSERT_EQ(with.current_hand.size(), 1u) << "the ethereal card should have left";
  EXPECT_EQ(with.current_hand[0].card_id, CardId::Strike);
  EXPECT_EQ(with.exhaust_pile.size(), 1u);
}

// ===================================================================== batch 1c
// The boss energy relics. Each reads "gain 1 Energy at the start of each turn",
// which is what energy_per_turn means, so they are summed once at setup.

TEST(RelicEnergy, EachFlatEnergyRelicGivesOne) {
  const CombatState bare = bare_fight();
  const RelicId flat[] = {
      RelicId::CoffeeDripper, RelicId::FusionHammer, RelicId::Ectoplasm,
      RelicId::PhilosophersStone, RelicId::MarkOfPain, RelicId::RunicDome,
      RelicId::CursedKey, RelicId::BustedCrown, RelicId::Sozu,
      RelicId::VelvetChoker};
  for (RelicId id : flat) {
    const CombatState with = fight_with({id});
    EXPECT_EQ(with.character.energy_per_turn,
              bare.character.energy_per_turn + 1)
        << relic_name(id) << " gave no energy";
    EXPECT_EQ(with.character.energy, with.character.energy_per_turn)
        << relic_name(id) << " granted per-turn energy but not turn 1's";
  }
}

// They stack — nothing in the game stops a run holding several boss relics.
TEST(RelicEnergy, EnergyRelicsStack) {
  const CombatState bare = bare_fight();
  const CombatState with =
      fight_with({RelicId::Sozu, RelicId::RunicDome, RelicId::CursedKey});
  EXPECT_EQ(with.character.energy_per_turn,
            bare.character.energy_per_turn + 3);
}

// Slaver's Collar is the conditional one: "During Boss and Elite combats."
TEST(RelicEnergy, SlaversCollarGivesNothingInANormalFight) {
  const CombatState bare = bare_fight();
  const CombatState with = fight_with({RelicId::SlaversCollar});
  EXPECT_EQ(with.character.energy_per_turn, bare.character.energy_per_turn);
}

TEST(RelicEnergy, SlaversCollarGivesEnergyInAnEliteFight) {
  const CombatState bare_elite = elite_fight_with({});
  const CombatState with = elite_fight_with({RelicId::SlaversCollar});
  EXPECT_EQ(with.character.energy_per_turn,
            bare_elite.character.energy_per_turn + 1);
}

// An unconditional relic is unaffected by the fight's kind — the conditional
// path must not have made everything conditional.
TEST(RelicEnergy, AFlatEnergyRelicWorksInBothFightKinds) {
  const CombatState normal = fight_with({RelicId::Sozu});
  const CombatState elite = elite_fight_with({RelicId::Sozu});
  EXPECT_EQ(normal.character.energy_per_turn, IRONCLAD_ENERGY_PER_TURN + 1);
  EXPECT_EQ(elite.character.energy_per_turn, IRONCLAD_ENERGY_PER_TURN + 1);
}

// The energy survives into later turns, not just the first — it is per-turn
// energy, not a one-off grant. Driven through the real turn boundary rather
// than by calling the refill directly, so it exercises the path the agent does.
TEST(RelicEnergy, TheEnergyPersistsAcrossTurns) {
  CombatState with = fight_with({RelicId::Sozu});
  const int expected = with.character.energy_per_turn;
  ASSERT_EQ(with.character.energy, expected);

  with.character.energy = 0;  // spend it all
  // kEndTurnAction from turn_loop.h, never re-derived — the option-slot channel
  // sits after the combat block, so the last index is decline, not end-turn.
  ASSERT_TRUE(apply_action(with, kEndTurnAction));

  // A Weak-pool fight cannot end on turn 1, but assert rather than assume.
  ASSERT_EQ(with.outcome, Outcome::InProgress);
  EXPECT_EQ(with.character.energy, expected)
      << "turn 2 refilled to the base amount, not the relic-boosted one";
}

}  // namespace
}  // namespace minispire
