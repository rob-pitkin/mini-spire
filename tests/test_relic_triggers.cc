// Relic trigger infrastructure (docs/design/relic-effects.md §3.1).
//
// Batch 0: the hook vocabulary, fire_relic_hooks, and the combat-start
// sub-phases, proved with the relics whose primitives already exist.

#include <gtest/gtest.h>

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

// ------------------------------------------------------------ combat start

TEST(RelicTriggers, VajraGrantsStrengthAtCombatStart) {
  const CombatState with = fight_with({RelicId::Vajra});
  EXPECT_EQ(get_status(with.character.powers, Power::Strength), 1);
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

CombatState elite_fight_with(std::vector<RelicId> ids, uint32_t seed = 1) {
  CombatSetup setup;
  setup.seed = seed;
  setup.pool = EncounterPool::Elite;
  setup.deck = starter_deck();
  for (RelicId id : ids) setup.relics.push_back(HeldRelic{id, 0});
  return start_combat(setup);
}

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
