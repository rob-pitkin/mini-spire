// Potion use and discard in combat, and the step-1 potion effects
// (docs/design/potion-effects.md §2-§5, step 1 of §8).
//
// Every fight is built, not dealt: make_minimal_state gives one Jaw Worm, the
// player at 80/80 with 3 energy and empty piles, and each test adds exactly the
// potions, relics and cards it reads.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "action.h"
#include "combat_env.h"
#include "enemy.h"
#include "query.h"
#include "test_helpers.h"
#include "turn_loop.h"

namespace minispire {
namespace {

using testing::card_action;
using testing::make_minimal_state;

CombatState fight_holding(std::vector<PotionId> potions,
                          std::vector<RelicId> relics = {}) {
  CombatState s = make_minimal_state(0);
  s.potions = std::move(potions);
  for (RelicId r : relics) s.relics.push_back(HeldRelic{r});
  return s;
}

int drink(PotionId p, int target = 0) {
  return encode_action(ActionBlock::PotionUse, static_cast<int>(p), target);
}

int discard(PotionId p) {
  return encode_action(ActionBlock::PotionDiscard, static_cast<int>(p));
}

int held(const CombatState& s, PotionId p) {
  return static_cast<int>(std::count(s.potions.begin(), s.potions.end(), p));
}

// A second living enemy in slot 1, built rather than sampled.
void add_second_enemy(CombatState& s) {
  s.enemies.push_back(make_jaw_worm(s.rng));
}

// ============================================================================
// Use and discard (§2, §3.1)
// ============================================================================

TEST(PotionActions, AnUntargetedPotionIsDrunkAtSlotZeroOnly) {
  CombatState s = fight_holding({PotionId::BlockPotion});
  add_second_enemy(s);
  const auto mask = valid_actions(s);
  EXPECT_TRUE(mask[drink(PotionId::BlockPotion, 0)]);
  for (int t = 1; t < kMaxEnemies; ++t) {
    EXPECT_FALSE(mask[drink(PotionId::BlockPotion, t)]) << "target " << t;
  }
}

TEST(PotionActions, ATargetedPotionLightsLivingEnemiesOnly) {
  CombatState s = fight_holding({PotionId::FirePotion});
  add_second_enemy(s);
  s.enemies[0].hp = 0;  // dead: not a target
  const auto mask = valid_actions(s);
  EXPECT_FALSE(mask[drink(PotionId::FirePotion, 0)]);
  EXPECT_TRUE(mask[drink(PotionId::FirePotion, 1)]);
  for (int t = 2; t < kMaxEnemies; ++t) {
    EXPECT_FALSE(mask[drink(PotionId::FirePotion, t)]) << "empty slot " << t;
  }
}

TEST(PotionActions, OnlyHeldPotionsAreLit) {
  CombatState s = fight_holding({PotionId::BlockPotion});
  const auto mask = valid_actions(s);
  EXPECT_FALSE(mask[drink(PotionId::StrengthPotion)]);
  EXPECT_FALSE(mask[discard(PotionId::StrengthPotion)]);
  EXPECT_FALSE(apply_action(s, drink(PotionId::StrengthPotion)));
  EXPECT_EQ(held(s, PotionId::BlockPotion), 1);
}

TEST(PotionActions, APotionNotYetBuiltCanBeDiscardedButNotDrunk) {
  // Potions land in steps (§8); until its step, a potion is masked off rather
  // than doing nothing. Fairy in a Bottle is never drinkable (§5.4).
  for (PotionId p : {PotionId::AttackPotion, PotionId::FairyPotion,
                     PotionId::SmokeBomb, PotionId::EntropicBrew}) {
    CombatState s = fight_holding({p});
    const auto mask = valid_actions(s);
    for (int t = 0; t < kMaxEnemies; ++t) {
      EXPECT_FALSE(mask[drink(p, t)]) << potion_name(p);
    }
    EXPECT_TRUE(mask[discard(p)]) << potion_name(p);
  }
}

TEST(PotionActions, NoPotionActionDuringAPendingChoice) {
  // Rob, 2026-10-04 (Q8): neither drink nor discard while a card choice is
  // open.
  CombatState s = fight_holding({PotionId::BlockPotion});
  s.current_hand = {Card{CardId::Strike}, Card{CardId::Defend}};
  s.pending_choice =
      build_choice(s, ChoiceKind::UpgradeCardInHand, CardId::Armaments);
  ASSERT_TRUE(s.pending_choice.active());
  const auto mask = valid_actions(s);
  EXPECT_FALSE(mask[drink(PotionId::BlockPotion)]);
  EXPECT_FALSE(mask[discard(PotionId::BlockPotion)]);
  EXPECT_FALSE(apply_action(s, drink(PotionId::BlockPotion)));
  EXPECT_FALSE(apply_action(s, discard(PotionId::BlockPotion)));
  EXPECT_EQ(held(s, PotionId::BlockPotion), 1);
}

TEST(PotionActions, DrinkingTakesOneCopy) {
  CombatState s = fight_holding({PotionId::BlockPotion, PotionId::BlockPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::BlockPotion)));
  EXPECT_EQ(held(s, PotionId::BlockPotion), 1);
  EXPECT_EQ(s.character.current_block, 12);
}

TEST(PotionActions, DiscardingTakesOneCopyAndDoesNothingElse) {
  CombatState s = fight_holding({PotionId::BlockPotion, PotionId::BlockPotion});
  ASSERT_TRUE(apply_action(s, discard(PotionId::BlockPotion)));
  EXPECT_EQ(held(s, PotionId::BlockPotion), 1);
  EXPECT_EQ(s.character.current_block, 0);
}

TEST(PotionActions, DrinkingCostsNoEnergyAndKeepsTheTurn) {
  CombatState s = fight_holding({PotionId::StrengthPotion});
  s.character.energy = 0;
  ASSERT_TRUE(apply_action(s, drink(PotionId::StrengthPotion)));
  EXPECT_EQ(s.character.energy, 0);
  EXPECT_EQ(s.turn_number, 1);
  EXPECT_TRUE(s.character_turn);
}

TEST(PotionActions, APotionThatKillsTheLastEnemyWinsTheFight) {
  CombatState s = fight_holding({PotionId::ExplosivePotion});
  s.enemies[0].hp = 5;
  s.enemies[0].current_block = 0;
  ASSERT_TRUE(apply_action(s, drink(PotionId::ExplosivePotion)));
  EXPECT_EQ(s.outcome, Outcome::Won);
}

// ============================================================================
// Sacred Bark and Toy Ornithopter (§2)
// ============================================================================

TEST(PotionEffects, SacredBarkDoublesEveryPotency) {
  // The pure function, over every potion: no draw involved.
  const CombatState plain = fight_holding({});
  const CombatState bark = fight_holding({}, {RelicId::SacredBark});
  for (int i = 0; i < kNumPotions; ++i) {
    const PotionId p = static_cast<PotionId>(i);
    EXPECT_EQ(potion_potency(plain, p), potion_base_potency(p)) << potion_name(p);
    EXPECT_EQ(potion_potency(bark, p), 2 * potion_base_potency(p))
        << potion_name(p);
  }
}

TEST(PotionEffects, SacredBarkReachesTheEffect) {
  CombatState s = fight_holding({PotionId::BlockPotion}, {RelicId::SacredBark});
  ASSERT_TRUE(apply_action(s, drink(PotionId::BlockPotion)));
  EXPECT_EQ(s.character.current_block, 24);
}

TEST(PotionEffects, ToyOrnithopterHealsFiveOnEveryPotion) {
  CombatState s = fight_holding({PotionId::BlockPotion, PotionId::StrengthPotion},
                                {RelicId::ToyOrnithopter});
  s.character.hp = 50;
  ASSERT_TRUE(apply_action(s, drink(PotionId::BlockPotion)));
  EXPECT_EQ(s.character.hp, 55);
  ASSERT_TRUE(apply_action(s, drink(PotionId::StrengthPotion)));
  EXPECT_EQ(s.character.hp, 60);
}

TEST(PotionEffects, ToyOrnithopterDoesNotHealOnADiscard) {
  CombatState s = fight_holding({PotionId::BlockPotion},
                                {RelicId::ToyOrnithopter});
  s.character.hp = 50;
  ASSERT_TRUE(apply_action(s, discard(PotionId::BlockPotion)));
  EXPECT_EQ(s.character.hp, 50);
}

// ============================================================================
// The potions (§4)
// ============================================================================

TEST(PotionEffects, BloodPotionHealsTwentyPercentOfMaxHp) {
  CombatState s = fight_holding({PotionId::BloodPotion});
  s.character.hp = 50;
  s.character.max_hp = 83;  // 83 * 20 / 100 = 16.6, truncated to 16
  ASSERT_TRUE(apply_action(s, drink(PotionId::BloodPotion)));
  EXPECT_EQ(s.character.hp, 66);
}

TEST(PotionEffects, BlockPotionIgnoresDexterityAndFrail) {
  // GainBlockAction, not a card's block (§4).
  CombatState s = fight_holding({PotionId::BlockPotion});
  s.character.powers[Power::Dexterity] = 5;
  s.character.debuffs[Debuff::Frail] = 2;
  ASSERT_TRUE(apply_action(s, drink(PotionId::BlockPotion)));
  EXPECT_EQ(s.character.current_block, 12);
}

TEST(PotionEffects, StatPotionsGrantTheirStat) {
  CombatState s = fight_holding(
      {PotionId::StrengthPotion, PotionId::DexterityPotion,
       PotionId::AncientPotion, PotionId::LiquidBronze,
       PotionId::EssenceOfSteel, PotionId::HeartOfIron});
  for (PotionId p : {PotionId::StrengthPotion, PotionId::DexterityPotion,
                     PotionId::AncientPotion, PotionId::LiquidBronze,
                     PotionId::EssenceOfSteel, PotionId::HeartOfIron}) {
    ASSERT_TRUE(apply_action(s, drink(p))) << potion_name(p);
  }
  const auto& pw = s.character.powers;
  EXPECT_EQ(get_status(pw, Power::Strength), 2);
  EXPECT_EQ(get_status(pw, Power::Dexterity), 2);
  EXPECT_EQ(get_status(pw, Power::Artifact), 1);
  EXPECT_EQ(get_status(pw, Power::Thorns), 3);
  EXPECT_EQ(get_status(pw, Power::PlatedArmor), 4);
  EXPECT_EQ(get_status(pw, Power::Metallicize), 6);
}

TEST(PotionEffects, EnergyPotionGrantsTwoEnergy) {
  CombatState s = fight_holding({PotionId::EnergyPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::EnergyPotion)));
  EXPECT_EQ(s.character.energy, 5);
}

TEST(PotionEffects, SwiftPotionDrawsThree) {
  CombatState s = fight_holding({PotionId::SwiftPotion});
  s.draw_pile = {Card{CardId::Strike}, Card{CardId::Strike},
                 Card{CardId::Strike}, Card{CardId::Strike}};
  ASSERT_TRUE(apply_action(s, drink(PotionId::SwiftPotion)));
  EXPECT_EQ(s.current_hand.size(), 3u);
  EXPECT_EQ(s.draw_pile.size(), 1u);
}

TEST(PotionEffects, WeakAndFearPotionsDebuffTheTarget) {
  CombatState s = fight_holding({PotionId::WeakPotion, PotionId::FearPotion});
  add_second_enemy(s);
  ASSERT_TRUE(apply_action(s, drink(PotionId::WeakPotion, 1)));
  ASSERT_TRUE(apply_action(s, drink(PotionId::FearPotion, 1)));
  EXPECT_EQ(get_status(s.enemies[1].debuffs, Debuff::Weak), 3);
  EXPECT_EQ(get_status(s.enemies[1].debuffs, Debuff::Vulnerable), 3);
  EXPECT_EQ(get_status(s.enemies[0].debuffs, Debuff::Weak), 0)
      << "only the target";
}

TEST(PotionEffects, FirePotionIsFlatDamageVulnerableDoesNotApply) {
  // THORNS-type: Vulnerable multiplies only NORMAL damage, and the player's
  // Strength is not applied (§5.1). Blocked like any damage.
  CombatState s = fight_holding({PotionId::FirePotion});
  s.enemies[0].hp = 60;
  s.enemies[0].current_block = 5;
  s.enemies[0].debuffs[Debuff::Vulnerable] = 2;
  s.character.powers[Power::Strength] = 4;
  ASSERT_TRUE(apply_action(s, drink(PotionId::FirePotion, 0)));
  EXPECT_EQ(s.enemies[0].current_block, 0);
  EXPECT_EQ(s.enemies[0].hp, 60 - 15) << "20 damage, 5 of it blocked";
}

TEST(PotionEffects, ExplosivePotionHitsEveryEnemyForTenFlat) {
  CombatState s = fight_holding({PotionId::ExplosivePotion});
  add_second_enemy(s);
  s.enemies[0].hp = 40;
  s.enemies[1].hp = 40;
  s.enemies[0].current_block = 0;
  s.enemies[1].current_block = 0;
  s.enemies[1].debuffs[Debuff::Vulnerable] = 2;
  ASSERT_TRUE(apply_action(s, drink(PotionId::ExplosivePotion)));
  EXPECT_EQ(s.enemies[0].hp, 30);
  EXPECT_EQ(s.enemies[1].hp, 30) << "Vulnerable does not apply";
}

TEST(PotionEffects, BlessingOfTheForgeUpgradesTheHand) {
  CombatState s = fight_holding({PotionId::BlessingOfTheForge});
  s.current_hand = {Card{CardId::Strike}, Card{CardId::Bash}};
  ASSERT_TRUE(apply_action(s, drink(PotionId::BlessingOfTheForge)));
  EXPECT_EQ(s.current_hand[0].card_id, CardId::StrikePlus);
  EXPECT_EQ(s.current_hand[1].card_id, CardId::BashPlus);
}

TEST(PotionEffects, FruitJuiceRaisesMaxHpAndHealsTheSame) {
  CombatState s = fight_holding({PotionId::FruitJuice});
  s.character.hp = 50;
  ASSERT_TRUE(apply_action(s, drink(PotionId::FruitJuice)));
  EXPECT_EQ(s.character.max_hp, 85);
  EXPECT_EQ(s.character.hp, 55);
}

TEST(PotionEffects, FlexPotionsStrengthLastsTheTurn) {
  CombatState s = fight_holding({PotionId::FlexPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::FlexPotion)));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 5);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
}

TEST(PotionEffects, SpeedPotionsDexterityLastsTheTurn) {
  CombatState s = fight_holding({PotionId::SpeedPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::SpeedPotion)));
  EXPECT_EQ(get_status(s.character.powers, Power::Dexterity), 5);
  EXPECT_EQ(get_status(s.character.powers, Power::DexterityDown), 5);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Dexterity), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::DexterityDown), 0);
}

TEST(PotionEffects, RegenHealsThenLosesAStackEachTurn) {
  // The second power that ticks (Q2): heal by the stacks, then lose one.
  CombatState s = fight_holding({PotionId::RegenPotion});
  s.character.hp = 40;
  s.enemies[0].hp = 999;
  ASSERT_TRUE(apply_action(s, drink(PotionId::RegenPotion)));
  ASSERT_EQ(get_status(s.character.powers, Power::Regen), 5);

  // Block the Jaw Worm cannot get through, so the only HP change across the
  // turn boundary is the heal. Block resets at the start of the next turn, so
  // it is topped up before each end of turn.
  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(s.character.hp, 45) << "healed by 5";
  EXPECT_EQ(get_status(s.character.powers, Power::Regen), 4);

  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(s.character.hp, 49) << "healed by 4";
  EXPECT_EQ(get_status(s.character.powers, Power::Regen), 3);
}

TEST(PotionEffects, CultistPotionGainsStrengthAtEachEndOfTurn) {
  // The player's Ritual (Q5): Strength at the end of every player turn,
  // starting with the turn it was drunk.
  CombatState s = fight_holding({PotionId::CultistPotion});
  s.enemies[0].hp = 999;
  ASSERT_TRUE(apply_action(s, drink(PotionId::CultistPotion)));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 0);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 1);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 2);
}

// ============================================================================
// Debuff-typed powers (§5.3, task #30)
// ============================================================================

TEST(PotionEffects, ArtifactNegatesStrengthDownSoFlexStrengthStays) {
  // Flex the card, with Clockwork Souvenir's kind of Artifact already up: the
  // Strength Down application is negated, the charge is spent, and the
  // Strength survives the end of turn.
  CombatState s = fight_holding({});
  s.character.powers[Power::Artifact] = 1;
  s.current_hand = {Card{CardId::Flex}};
  ASSERT_TRUE(apply_action(s, card_action(CardId::Flex)));
  EXPECT_EQ(get_status(s.character.powers, Power::Artifact), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::StrengthDown), 0);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 2);
}

TEST(PotionEffects, ArtifactCanBlockTheEndOfTurnLossInstead) {
  // Strength Down already landed, then Artifact arrives: the end-of-turn
  // Strength loss is itself a negative application, so the charge eats that
  // (wiki.gg Flex: "either the debuff application or the end-of-turn
  // removal").
  CombatState s = fight_holding({PotionId::FlexPotion, PotionId::AncientPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::FlexPotion)));
  ASSERT_TRUE(apply_action(s, drink(PotionId::AncientPotion)));
  ASSERT_EQ(get_status(s.character.powers, Power::StrengthDown), 5);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 5);
  EXPECT_EQ(get_status(s.character.powers, Power::Artifact), 0);
}

TEST(PotionEffects, ArtifactNegatesDexterityDown) {
  CombatState s = fight_holding({PotionId::SpeedPotion});
  s.character.powers[Power::Artifact] = 1;
  ASSERT_TRUE(apply_action(s, drink(PotionId::SpeedPotion)));
  EXPECT_EQ(get_status(s.character.powers, Power::DexterityDown), 0);
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Dexterity), 5);
}

TEST(PotionEffects, OrangePelletsRemovesStrengthAndDexterityDown) {
  CombatState s = fight_holding({PotionId::FlexPotion, PotionId::SpeedPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::FlexPotion)));
  ASSERT_TRUE(apply_action(s, drink(PotionId::SpeedPotion)));
  ActionQueue q;
  ResolutionContext ctx;
  q.push_back(Action{ActionKind::RemoveAllDebuffs});
  drain(s, q, ctx);
  EXPECT_EQ(get_status(s.character.powers, Power::StrengthDown), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::DexterityDown), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::Strength), 5)
      << "the gain itself is not a debuff";
}

// ============================================================================
// Observation (§3.2)
// ============================================================================

TEST(PotionObservation, CountsPerPotionAndSlotCapacity) {
  CombatState s = fight_holding({PotionId::FirePotion, PotionId::FirePotion,
                                 PotionId::BlockPotion});
  s.potion_slots = 5;
  CombatEnv env(s);
  const auto obs = env.obs();
  const int base = CombatEnv::kPotionObsIndex;
  for (int i = 0; i < kNumPotions; ++i) {
    const PotionId p = static_cast<PotionId>(i);
    const float want = p == PotionId::FirePotion    ? 2.0f
                       : p == PotionId::BlockPotion ? 1.0f
                                                    : 0.0f;
    EXPECT_FLOAT_EQ(obs[base + i], want) << potion_name(p);
  }
  EXPECT_FLOAT_EQ(obs[base + kNumPotions], 5.0f);
}

TEST(PotionObservation, AStandaloneFightHasThreeSlots) {
  CombatEnv env;
  env.reset(0);
  EXPECT_FLOAT_EQ(env.obs()[CombatEnv::kPotionObsIndex + kNumPotions], 3.0f);
}

}  // namespace
}  // namespace minispire
