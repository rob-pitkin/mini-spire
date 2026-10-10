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

// Answer option i of the open choice through the action space, as the agent
// would: CardSelect at the option's (CardId, cost column).
bool pick(CombatState& s, int i) {
  const Card& o = s.pending_choice.options[i];
  return apply_action(s, encode_action(ActionBlock::CardSelect,
                                       static_cast<int>(o.card_id), 0,
                                       cost_column(s, o)));
}

// The option index offering `id`, or -1.
int option_of(const CombatState& s, CardId id) {
  for (int i = 0; i < s.pending_choice.num_options; ++i) {
    if (s.pending_choice.options[i].card_id == id) return i;
  }
  return -1;
}

bool decline(CombatState& s) {
  return apply_action(s, encode_action(ActionBlock::Decline));
}

int count_of(const std::vector<Card>& pile, CardId id) {
  return static_cast<int>(std::count_if(
      pile.begin(), pile.end(), [id](const Card& c) { return c.card_id == id; }));
}

std::vector<Card> cards(std::initializer_list<CardId> ids) {
  std::vector<Card> out;
  for (CardId id : ids) out.push_back(Card{id});
  return out;
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
  for (PotionId p : {PotionId::FairyPotion, PotionId::SmokeBomb,
                     PotionId::EntropicBrew}) {
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
// Step 2: the Discovery potions (§5.2)
//
// The three offered cards are rolled, so these tests assert only what holds for
// every roll — the type, distinctness, pool membership — or overwrite option 0
// with a known card before answering.
// ============================================================================

TEST(DiscoveryPotions, TypedPotionsOfferThreeDistinctCardsOfTheirType) {
  const struct {
    PotionId potion;
    CardType type;
    GenerationPool pool;
  } cases[] = {
      {PotionId::AttackPotion, CardType::Attack, GenerationPool::ClassAttack},
      {PotionId::SkillPotion, CardType::Skill, GenerationPool::ClassSkill},
      {PotionId::PowerPotion, CardType::Power, GenerationPool::ClassPower},
  };
  for (const auto& c : cases) {
    CombatState s = fight_holding({c.potion});
    ASSERT_TRUE(apply_action(s, drink(c.potion))) << potion_name(c.potion);
    const PendingChoice& pc = s.pending_choice;
    ASSERT_EQ(pc.kind, ChoiceKind::DiscoverCard) << potion_name(c.potion);
    EXPECT_EQ(pc.source_card, CardId::None) << "a potion opened it, not a card";
    EXPECT_EQ(pc.pool, c.pool);
    ASSERT_EQ(pc.num_options, 3);
    const auto& pool = generation_pool(c.pool);
    for (int i = 0; i < 3; ++i) {
      const CardId id = pc.options[i].card_id;
      EXPECT_EQ(CARD_DATABASE.at(id).type, c.type) << card_name(id);
      EXPECT_NE(std::find(pool.begin(), pool.end(), id), pool.end())
          << card_name(id) << " is not in the pool";
      for (int j = 0; j < i; ++j) EXPECT_NE(pc.options[j].card_id, id);
    }
  }
}

TEST(DiscoveryPotions, ThePowerPoolIsExactlyTheClassPowers) {
  // returnTrulyRandomCardInCombat(POWER): the obtainable class pools filtered
  // to Powers, healing cards excluded. Checked over the whole class pool.
  const auto& powers = generation_pool(GenerationPool::ClassPower);
  for (CardId id : generation_pool(GenerationPool::ClassAny)) {
    const bool is_power = CARD_DATABASE.at(id).type == CardType::Power;
    const bool in_pool =
        std::find(powers.begin(), powers.end(), id) != powers.end();
    EXPECT_EQ(is_power, in_pool) << card_name(id);
  }
  EXPECT_GE(powers.size(), 3u) << "too few Powers to offer three distinct";
}

TEST(DiscoveryPotions, TheTypedPotionsMayBeSkipped) {
  CombatState s = fight_holding({PotionId::SkillPotion});
  s.current_hand = cards({CardId::Strike});
  ASSERT_TRUE(apply_action(s, drink(PotionId::SkillPotion)));
  EXPECT_TRUE(s.pending_choice.is_optional);
  EXPECT_TRUE(valid_actions(s)[encode_action(ActionBlock::Decline)]);
  ASSERT_TRUE(decline(s));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.current_hand.size(), 1u) << "skipping adds nothing";
}

TEST(DiscoveryPotions, TheColorlessPotionCannotBeSkipped) {
  // DiscoveryAction passes skippable = (cardType != null), and the colorless
  // constructor sets no type.
  CombatState s = fight_holding({PotionId::ColorlessPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::ColorlessPotion)));
  const PendingChoice& pc = s.pending_choice;
  ASSERT_EQ(pc.kind, ChoiceKind::DiscoverColorlessCard);
  EXPECT_FALSE(pc.is_optional);
  EXPECT_FALSE(valid_actions(s)[encode_action(ActionBlock::Decline)]);
  EXPECT_FALSE(decline(s));
  const auto& pool = generation_pool(GenerationPool::Colorless);
  for (int i = 0; i < pc.num_options; ++i) {
    EXPECT_NE(std::find(pool.begin(), pool.end(), pc.options[i].card_id),
              pool.end());
  }
}

TEST(DiscoveryPotions, TheChosenCardCostsZeroThisTurn) {
  for (PotionId p : {PotionId::AttackPotion, PotionId::ColorlessPotion}) {
    CombatState s = fight_holding({p});
    ASSERT_TRUE(apply_action(s, drink(p)));
    // A known 3-cost card in place of whatever was rolled.
    s.pending_choice.options[0] = Card{CardId::Bludgeon};
    ASSERT_TRUE(pick(s, 0)) << potion_name(p);
    ASSERT_EQ(s.current_hand.size(), 1u);
    EXPECT_EQ(s.current_hand[0].card_id, CardId::Bludgeon);
    EXPECT_EQ(instance_effective_cost(s, s.current_hand[0]), 0)
        << potion_name(p);
  }
}

TEST(DiscoveryPotions, SacredBarkAddsTwoCopies) {
  CombatState s = fight_holding({PotionId::PowerPotion}, {RelicId::SacredBark});
  ASSERT_TRUE(apply_action(s, drink(PotionId::PowerPotion)));
  EXPECT_EQ(s.pending_choice.copies, 2);
  s.pending_choice.options[0] = Card{CardId::DemonForm};
  ASSERT_TRUE(pick(s, 0));
  ASSERT_EQ(count_of(s.current_hand, CardId::DemonForm), 2);
  for (const Card& c : s.current_hand) {
    EXPECT_EQ(instance_effective_cost(s, c), 0) << "each copy is free";
  }
}

TEST(DiscoveryPotions, TwoCopiesFollowDiscoveryActionsHandLimitTable) {
  // DiscoveryAction with 2 copies: 8 or fewer in hand, both to hand; 9, one to
  // hand and one to the discard; 10, both to the discard.
  const struct {
    int hand;
    int to_hand;
    int to_discard;
  } cases[] = {{8, 2, 0}, {9, 1, 1}, {10, 0, 2}};
  for (const auto& c : cases) {
    CombatState s =
        fight_holding({PotionId::AttackPotion}, {RelicId::SacredBark});
    s.current_hand.assign(c.hand, Card{CardId::Defend});
    ASSERT_TRUE(apply_action(s, drink(PotionId::AttackPotion)));
    s.pending_choice.options[0] = Card{CardId::Bludgeon};
    ASSERT_TRUE(pick(s, 0));
    EXPECT_EQ(count_of(s.current_hand, CardId::Bludgeon), c.to_hand)
        << "hand of " << c.hand;
    EXPECT_EQ(count_of(s.discard_pile, CardId::Bludgeon), c.to_discard)
        << "hand of " << c.hand;
  }
}

// ============================================================================
// Step 2: Elixir, Gambler's Brew, Liquid Memories (§5.7)
// ============================================================================

TEST(Elixir, OpensAnyNumberChoiceThatCanStopAtZero) {
  CombatState s = fight_holding({PotionId::ElixirPotion});
  s.current_hand = cards({CardId::Strike, CardId::Defend, CardId::Bash});
  ASSERT_TRUE(apply_action(s, drink(PotionId::ElixirPotion)));
  const PendingChoice& pc = s.pending_choice;
  ASSERT_EQ(pc.kind, ChoiceKind::ExhaustCardInHand);
  EXPECT_EQ(pc.source_card, CardId::None);
  EXPECT_EQ(pc.max_picks, kMaxMultiSelectPicks);
  EXPECT_TRUE(pc.is_optional) << "zero cards is a legal answer";
  ASSERT_TRUE(decline(s));
  EXPECT_EQ(s.current_hand.size(), 3u);
  EXPECT_TRUE(s.exhaust_pile.empty());
}

TEST(Elixir, ExhaustsEveryPickWhenTheChoiceEnds) {
  CombatState s = fight_holding({PotionId::ElixirPotion});
  s.character.powers[Power::FeelNoPain] = 3;
  s.current_hand = cards({CardId::Strike, CardId::Defend, CardId::Bash});
  ASSERT_TRUE(apply_action(s, drink(PotionId::ElixirPotion)));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  EXPECT_TRUE(s.exhaust_pile.empty()) << "staged, not exhausted yet";
  ASSERT_TRUE(pick(s, option_of(s, CardId::Defend)));
  ASSERT_TRUE(decline(s));
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::Strike), 1);
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::Defend), 1);
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Bash);
  EXPECT_EQ(s.character.current_block, 6) << "Feel No Pain saw both exhausts";
}

TEST(Elixir, TakingTheWholeHandEndsTheChoice) {
  CombatState s = fight_holding({PotionId::ElixirPotion});
  s.current_hand = cards({CardId::Strike, CardId::Defend});
  ASSERT_TRUE(apply_action(s, drink(PotionId::ElixirPotion)));
  ASSERT_TRUE(pick(s, 0));
  ASSERT_TRUE(pick(s, 0));
  EXPECT_FALSE(s.pending_choice.active()) << "nothing left to offer";
  EXPECT_TRUE(s.current_hand.empty());
  EXPECT_EQ(s.exhaust_pile.size(), 2u);
}

TEST(Elixir, AnEmptyHandOpensNothing) {
  CombatState s = fight_holding({PotionId::ElixirPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::ElixirPotion)));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(held(s, PotionId::ElixirPotion), 0) << "still drunk";
}

TEST(GamblersBrew, DiscardsThePicksThenDrawsThatMany) {
  CombatState s = fight_holding({PotionId::GamblersBrew});
  s.current_hand = cards({CardId::Strike, CardId::Defend, CardId::Bash});
  // back() is the top of the draw pile.
  s.draw_pile = cards({CardId::Bludgeon, CardId::Clothesline, CardId::Cleave});
  ASSERT_TRUE(apply_action(s, drink(PotionId::GamblersBrew)));
  ASSERT_EQ(s.pending_choice.kind, ChoiceKind::DiscardForDraw);
  EXPECT_TRUE(s.pending_choice.is_optional);
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Defend)));
  ASSERT_TRUE(decline(s));
  EXPECT_EQ(count_of(s.discard_pile, CardId::Strike), 1);
  EXPECT_EQ(count_of(s.discard_pile, CardId::Defend), 1);
  ASSERT_EQ(s.current_hand.size(), 3u);
  EXPECT_EQ(count_of(s.current_hand, CardId::Bash), 1);
  EXPECT_EQ(count_of(s.current_hand, CardId::Cleave), 1);
  EXPECT_EQ(count_of(s.current_hand, CardId::Clothesline), 1);
  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile[0].card_id, CardId::Bludgeon);
}

TEST(GamblersBrew, PickingNothingDrawsNothing) {
  CombatState s = fight_holding({PotionId::GamblersBrew});
  s.current_hand = cards({CardId::Strike});
  s.draw_pile = cards({CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::GamblersBrew)));
  ASSERT_TRUE(decline(s));
  EXPECT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.draw_pile.size(), 1u);
  EXPECT_TRUE(s.discard_pile.empty());
}

TEST(GamblersBrew, TheDiscardsLandBeforeTheDraw) {
  // GamblingChipAction discards synchronously and draws after. With an empty
  // draw pile the draw reshuffles the discard, so the cards just discarded
  // come straight back. Drawing first would find nothing to draw.
  CombatState s = fight_holding({PotionId::GamblersBrew});
  s.current_hand = cards({CardId::Strike, CardId::Defend});
  ASSERT_TRUE(apply_action(s, drink(PotionId::GamblersBrew)));
  ASSERT_TRUE(pick(s, 0));
  ASSERT_TRUE(pick(s, 0));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(count_of(s.current_hand, CardId::Strike), 1);
  EXPECT_EQ(count_of(s.current_hand, CardId::Defend), 1);
  EXPECT_TRUE(s.discard_pile.empty());
}

TEST(GamblersBrew, AnEmptyHandOpensNothing) {
  CombatState s = fight_holding({PotionId::GamblersBrew});
  s.draw_pile = cards({CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::GamblersBrew)));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(s.draw_pile.size(), 1u);
}

TEST(LiquidMemories, AnEmptyDiscardDoesNothing) {
  CombatState s = fight_holding({PotionId::LiquidMemories});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_TRUE(s.current_hand.empty());
}

TEST(LiquidMemories, ALoneCardReturnsWithoutAskingAndCostsZero) {
  CombatState s = fight_holding({PotionId::LiquidMemories});
  s.discard_pile = cards({CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  EXPECT_FALSE(s.pending_choice.active());
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(s.current_hand[0].card_id, CardId::Bludgeon);
  EXPECT_EQ(instance_effective_cost(s, s.current_hand[0]), 0);
  EXPECT_TRUE(s.discard_pile.empty());
}

TEST(LiquidMemories, OneMandatoryPickFromTheDiscard) {
  CombatState s = fight_holding({PotionId::LiquidMemories});
  s.discard_pile = cards({CardId::Strike, CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  const PendingChoice& pc = s.pending_choice;
  ASSERT_EQ(pc.kind, ChoiceKind::DiscardToHand);
  EXPECT_FALSE(pc.is_optional);
  EXPECT_FALSE(pc.is_multi());
  EXPECT_FALSE(decline(s));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Bludgeon)));
  ASSERT_EQ(s.current_hand.size(), 1u);
  EXPECT_EQ(instance_effective_cost(s, s.current_hand[0]), 0);
  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Strike);
}

TEST(LiquidMemories, AFullHandLeavesThePickInTheDiscard) {
  CombatState s = fight_holding({PotionId::LiquidMemories});
  s.current_hand.assign(HAND_SIZE_LIMIT, Card{CardId::Defend});
  s.discard_pile = cards({CardId::Strike, CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  EXPECT_EQ(s.current_hand.size(), static_cast<std::size_t>(HAND_SIZE_LIMIT));
  // Not moved and put back: still where it was, under the Bludgeon.
  ASSERT_EQ(s.discard_pile.size(), 2u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Strike);
  EXPECT_EQ(s.discard_pile[1].card_id, CardId::Bludgeon);
}

TEST(LiquidMemories, SacredBarkRequiresExactlyTwoPicks) {
  CombatState s =
      fight_holding({PotionId::LiquidMemories}, {RelicId::SacredBark});
  s.discard_pile = cards({CardId::Strike, CardId::Defend, CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  ASSERT_TRUE(s.pending_choice.is_multi());
  EXPECT_EQ(s.pending_choice.min_picks, 2);
  EXPECT_FALSE(s.pending_choice.is_optional);
  EXPECT_FALSE(valid_actions(s)[encode_action(ActionBlock::Decline)]);
  ASSERT_TRUE(pick(s, option_of(s, CardId::Bludgeon)));
  EXPECT_FALSE(s.pending_choice.is_optional) << "one of two picked";
  EXPECT_FALSE(decline(s));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  EXPECT_FALSE(s.pending_choice.active()) << "two picked: finished";
  EXPECT_EQ(count_of(s.current_hand, CardId::Bludgeon), 1);
  EXPECT_EQ(count_of(s.current_hand, CardId::Strike), 1);
  for (const Card& c : s.current_hand) {
    EXPECT_EQ(instance_effective_cost(s, c), 0);
  }
  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Defend);
}

TEST(LiquidMemories, AStagedPickStaysInTheDiscardButLeavesTheOffer) {
  // The grid screen highlights a pick; it does not lift it out (§3.3).
  CombatState s =
      fight_holding({PotionId::LiquidMemories}, {RelicId::SacredBark});
  s.discard_pile = cards({CardId::Strike, CardId::Defend, CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  EXPECT_EQ(s.discard_pile.size(), 3u);
  EXPECT_EQ(option_of(s, CardId::Strike), -1);
  EXPECT_EQ(s.pending_choice.picks_made, 1);
  EXPECT_EQ(s.pending_choice.staged[0].card_id, CardId::Strike);
}

TEST(LiquidMemories, SacredBarkTakesASmallDiscardWholeInPileOrder) {
  // BetterDiscardPileToHandAction: a discard of no more than p cards is taken
  // without a screen, bottom card first. With one slot left, the bottom card
  // gets it — the agent is not offered a choice StS does not give.
  CombatState s =
      fight_holding({PotionId::LiquidMemories}, {RelicId::SacredBark});
  s.current_hand.assign(HAND_SIZE_LIMIT - 1, Card{CardId::Defend});
  s.discard_pile = cards({CardId::Strike, CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(count_of(s.current_hand, CardId::Strike), 1);
  ASSERT_EQ(s.discard_pile.size(), 1u);
  EXPECT_EQ(s.discard_pile[0].card_id, CardId::Bludgeon);
}

TEST(LiquidMemories, PicksWithNoAlternativeAreMadeWithoutPausing) {
  // Three identical Strikes and two required picks: every step would have one
  // legal action, so none is shown.
  CombatState s =
      fight_holding({PotionId::LiquidMemories}, {RelicId::SacredBark});
  s.discard_pile = cards({CardId::Strike, CardId::Strike, CardId::Strike});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  EXPECT_FALSE(s.pending_choice.active());
  EXPECT_EQ(count_of(s.current_hand, CardId::Strike), 2);
  EXPECT_EQ(count_of(s.discard_pile, CardId::Strike), 1);
}

// ============================================================================
// Step 3: Duplication (§1.2 T1, T2; Q3)
// ============================================================================

int counter_of(const CombatState& s, RelicId id) {
  for (const HeldRelic& r : s.relics) {
    if (r.id == id) return r.counter;
  }
  return -1;
}

TEST(DuplicationPotion, GrantsOneChargeAndTwoWithSacredBark) {
  CombatState s = fight_holding({PotionId::DuplicationPotion});
  ASSERT_TRUE(apply_action(s, drink(PotionId::DuplicationPotion)));
  EXPECT_EQ(get_status(s.character.powers, Power::Duplication), 1);

  CombatState b =
      fight_holding({PotionId::DuplicationPotion}, {RelicId::SacredBark});
  ASSERT_TRUE(apply_action(b, drink(PotionId::DuplicationPotion)));
  EXPECT_EQ(get_status(b.character.powers, Power::Duplication), 2);
}

TEST(DuplicationPotion, PlaysTheNextCardTwiceWhateverItsType) {
  // A Skill and a Power: Double Tap would replay neither.
  CombatState skill = fight_holding({});
  skill.character.powers[Power::Duplication] = 1;
  skill.current_hand = cards({CardId::Defend});
  ASSERT_TRUE(apply_action(skill, card_action(CardId::Defend)));
  EXPECT_EQ(skill.character.current_block, 10);
  EXPECT_EQ(get_status(skill.character.powers, Power::Duplication), 0);
  EXPECT_EQ(count_of(skill.discard_pile, CardId::Defend), 1)
      << "the replay enters no pile";

  CombatState power = fight_holding({});
  power.character.powers[Power::Duplication] = 1;
  power.current_hand = cards({CardId::Inflame});
  ASSERT_TRUE(apply_action(power, card_action(CardId::Inflame)));
  EXPECT_EQ(get_status(power.character.powers, Power::Strength), 4);
}

TEST(DuplicationPotion, StacksWithDoubleTapOnAnAttack) {
  CombatState s = fight_holding({});
  s.enemies[0].hp = 100;
  s.character.powers[Power::DoubleTap] = 1;
  s.character.powers[Power::Duplication] = 1;
  s.current_hand = cards({CardId::Strike});
  ASSERT_TRUE(apply_action(s, card_action(CardId::Strike)));
  EXPECT_EQ(s.enemies[0].hp, 100 - 3 * 6) << "played three times";
  EXPECT_EQ(get_status(s.character.powers, Power::DoubleTap), 0);
  EXPECT_EQ(get_status(s.character.powers, Power::Duplication), 0);
}

TEST(DuplicationPotion, TheChargeIsSpentWhenTheCardIsPlayed) {
  // T1: DuplicationPower.onUseCard decrements as Havoc is played. So the Defend
  // Havoc plays off the top finds no charge and is played once, and Havoc's
  // own replay plays the next card, the Bash. Spending at replay time instead
  // would replay the Defend (block 10) and leave Havoc's replay nothing.
  CombatState s = fight_holding({});
  s.enemies[0].hp = 100;
  s.character.powers[Power::Duplication] = 1;
  s.current_hand = cards({CardId::Havoc});
  s.draw_pile = cards({CardId::Bash, CardId::Defend});  // back() is the top
  ASSERT_TRUE(apply_action(s, card_action(CardId::Havoc)));
  EXPECT_EQ(s.character.current_block, 5) << "the Defend was not replayed";
  EXPECT_EQ(s.enemies[0].hp, 100 - 8) << "Havoc's replay played the Bash";
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::Defend), 1);
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::Bash), 1);
  EXPECT_EQ(get_status(s.character.powers, Power::Duplication), 0);
}

TEST(DuplicationPotion, AnUnusedChargeLosesOneAtEndOfRound) {
  // Q3: atEndOfRound reduces by 1, so Sacred Bark's second charge survives one
  // turn unused and the first expires.
  CombatState s = fight_holding({});
  s.character.powers[Power::Duplication] = 2;
  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Duplication), 1);
  s.character.current_block = 999;
  ASSERT_TRUE(apply_action(s, kEndTurnAction));
  EXPECT_EQ(get_status(s.character.powers, Power::Duplication), 0);
}

TEST(DuplicationPotion, AReplayWhoseTargetDiedIsNotPlayed) {
  // The replay is autoplayed: its target is dying, so canUse fails and the
  // purgeOnUse copy vanishes without triggers. Ink Bottle counts one card.
  CombatState s = fight_holding({}, {RelicId::InkBottle});
  add_second_enemy(s);
  s.enemies[0].hp = 3;
  const int hp1 = s.enemies[1].hp;
  s.character.powers[Power::Duplication] = 1;
  s.current_hand = cards({CardId::Strike});
  ASSERT_TRUE(apply_action(s, card_action(CardId::Strike, 0)));
  EXPECT_LE(s.enemies[0].hp, 0);
  EXPECT_EQ(s.enemies[1].hp, hp1) << "the replay did not retarget";
  EXPECT_EQ(counter_of(s, RelicId::InkBottle), 1) << "the replay was played";
}

// ============================================================================
// Step 3: Distilled Chaos, and Havoc/Mayhem's shared top-of-draw play (§1.2
// T3-T5)
// ============================================================================

TEST(DistilledChaos, PlaysTheTopThreeCardsWithoutExhausting) {
  CombatState s = fight_holding({PotionId::DistilledChaos});
  s.enemies[0].hp = 100;
  s.draw_pile = cards({CardId::Strike, CardId::Defend, CardId::Defend,
                       CardId::Bash});  // Bash is on top
  ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));
  EXPECT_EQ(s.enemies[0].hp, 100 - 8);
  EXPECT_EQ(s.character.current_block, 10);
  EXPECT_EQ(s.discard_pile.size(), 3u);
  EXPECT_TRUE(s.exhaust_pile.empty());
  ASSERT_EQ(s.draw_pile.size(), 1u);
  EXPECT_EQ(s.draw_pile[0].card_id, CardId::Strike);
}

// The rule restated, independently of roll_card_target: a uniform pick among
// the living slots, from the card-random stream. Predicting with the function
// under test would follow it into any mistake — a mutation that moved it to
// the combat stream went unnoticed until this replaced that.
int predict_target(std::mt19937& card_rng, int living) {
  return static_cast<int>(std::uniform_int_distribution<std::size_t>(
      0, static_cast<std::size_t>(living) - 1)(card_rng));
}

TEST(DistilledChaos, EveryTargetIsRolledAtUseFromTheCardStream) {
  // T3. The targets are predicted from a COPY of card_rng taken before
  // drinking, so the expected outcome is computed from the roll rather than
  // assumed — the assertion holds for every seed. Both enemies are alive when
  // all three are rolled, so each pick is among slots {0, 1}. A target rolled
  // at use can be dead by the time its card plays; a Strike at a dead target
  // is not played.
  for (uint32_t seed = 0; seed < 24; ++seed) {
    CombatState s = fight_holding({PotionId::DistilledChaos});
    add_second_enemy(s);
    s.card_rng = std::mt19937(seed);
    s.enemies[0].hp = 1;
    s.enemies[1].hp = 100;
    s.draw_pile = cards({CardId::Defend, CardId::Strike, CardId::Strike});

    std::mt19937 probe = s.card_rng;
    const int t1 = predict_target(probe, 2);
    const int t2 = predict_target(probe, 2);
    bool alive0 = true;
    int hp1 = 100;
    for (int t : {t1, t2}) {
      if (t == 0) {
        alive0 = false;  // kills it, or finds it already dead and is not played
      } else {
        hp1 -= 6;
      }
    }

    ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));
    EXPECT_EQ(s.enemies[0].hp > 0, alive0) << "seed " << seed;
    EXPECT_EQ(s.enemies[1].hp, hp1) << "seed " << seed;
    EXPECT_EQ(s.character.current_block, 5) << "the Defend needs no target";
  }
}

TEST(TopOfDrawPlay, ACardWhoseTargetDiedGoesToTheDiscardUnplayed) {
  // Constructed directly: a queued top-of-draw play whose rolled target is
  // dead. No damage anywhere, no on-play triggers, the card in the discard.
  CombatState s = fight_holding({}, {RelicId::InkBottle});
  add_second_enemy(s);
  s.enemies[0].hp = 0;
  const int hp1 = s.enemies[1].hp;
  s.draw_pile = cards({CardId::Strike});
  ActionQueue q;
  ResolutionContext ctx;
  Action a;
  a.kind = ActionKind::PlayCard;
  a.amount = kPlayTopOfDrawKeeping;
  a.target = 0;
  q.push_back(a);
  drain(s, q, ctx);
  EXPECT_EQ(s.enemies[1].hp, hp1);
  EXPECT_EQ(count_of(s.discard_pile, CardId::Strike), 1);
  EXPECT_EQ(counter_of(s, RelicId::InkBottle), 0) << "no on-play triggers";
}

TEST(TopOfDrawPlay, AnUnplayableCardIsDiscardedUnlessHavocPlaysIt) {
  // T5: UseCardAction exhausts only for Havoc's exhaustOnUseOnce or the card's
  // own Exhaust. Mayhem and Distilled Chaos used to exhaust a Wound.
  CombatState chaos = fight_holding({PotionId::DistilledChaos});
  chaos.draw_pile = cards({CardId::Wound, CardId::Wound, CardId::Dazed});
  ASSERT_TRUE(apply_action(chaos, drink(PotionId::DistilledChaos)));
  EXPECT_EQ(chaos.discard_pile.size(), 3u);
  EXPECT_TRUE(chaos.exhaust_pile.empty());

  CombatState havoc = fight_holding({});
  havoc.current_hand = cards({CardId::Havoc});
  havoc.draw_pile = cards({CardId::Wound});
  ASSERT_TRUE(apply_action(havoc, card_action(CardId::Havoc)));
  EXPECT_EQ(count_of(havoc.exhaust_pile, CardId::Wound), 1);
}

TEST(TopOfDrawPlay, AnAttackIsNotPlayedWhileEntangled) {
  // T5: Entangle fails canUse for an autoplayed Attack as for one from hand.
  // Feed carries its own Exhaust, so it exhausts even unplayed; the Strike is
  // discarded.
  CombatState s = fight_holding({PotionId::DistilledChaos});
  s.enemies[0].hp = 100;
  s.character.debuffs[Debuff::Entangle] = 1;
  s.draw_pile = cards({CardId::Defend, CardId::Feed, CardId::Strike});
  ASSERT_TRUE(apply_action(s, drink(PotionId::DistilledChaos)));
  EXPECT_EQ(s.enemies[0].hp, 100) << "no Attack was played";
  EXPECT_EQ(count_of(s.discard_pile, CardId::Strike), 1);
  EXPECT_EQ(count_of(s.exhaust_pile, CardId::Feed), 1);
  EXPECT_EQ(s.character.current_block, 5) << "the Skill still played";
}

TEST(TopOfDrawPlay, HavocsTargetIsRolledFromTheCardStream) {
  // T4: Havoc rolls its target as it is played, from card_rng — predicted
  // from a copy, as above, so the assertion holds for every seed.
  for (uint32_t seed = 0; seed < 16; ++seed) {
    CombatState s = fight_holding({});
    add_second_enemy(s);
    s.card_rng = std::mt19937(seed);
    s.enemies[0].hp = 100;
    s.enemies[1].hp = 100;
    s.current_hand = cards({CardId::Havoc});
    s.draw_pile = cards({CardId::Strike});

    std::mt19937 probe = s.card_rng;
    const int t = predict_target(probe, 2);

    ASSERT_TRUE(apply_action(s, card_action(CardId::Havoc)));
    EXPECT_EQ(s.enemies[t].hp, 94) << "seed " << seed;
    EXPECT_EQ(s.enemies[1 - t].hp, 100) << "seed " << seed;
  }
}

// ============================================================================
// Step 3: Snecko Oil
// ============================================================================

TEST(SneckoOil, DrawsFiveThenRollsEachEligibleCostOnce) {
  // RandomizeHandCostAction, predicted from a copy of card_rng: one roll per
  // card with a cost, in hand order; none for an X-cost or unplayable card. An
  // equal roll leaves the card alone, so Bludgeon's turn discount survives a
  // roll of 3 and a Strike is unchanged by a roll of 1. freeToPlayOnce stays.
  for (uint32_t seed = 0; seed < 12; ++seed) {
    CombatState s = fight_holding({PotionId::SneckoOil});
    s.card_rng = std::mt19937(seed);
    Card bludgeon{CardId::Bludgeon};
    set_turn_cost(bludgeon, 0);
    bludgeon.free_once = true;
    s.current_hand = {bludgeon, Card{CardId::Whirlwind}, Card{CardId::Wound}};
    s.draw_pile.assign(5, Card{CardId::Strike});

    std::mt19937 probe = s.card_rng;
    std::vector<int> rolls;
    for (int i = 0; i < 6; ++i) {  // Bludgeon, then the five drawn Strikes
      rolls.push_back(std::uniform_int_distribution<int>(0, 3)(probe));
    }

    ASSERT_TRUE(apply_action(s, drink(PotionId::SneckoOil)));
    ASSERT_EQ(s.current_hand.size(), 8u);
    // Read through the layers, not instance_effective_cost: free_once makes
    // the effective cost 0 whatever the roll.
    const Card& b = s.current_hand[0];
    EXPECT_EQ(turn_layer_cost(b), rolls[0] == 3 ? 0 : rolls[0])
        << "seed " << seed;
    EXPECT_TRUE(b.free_once) << "Snecko Oil does not clear it";
    EXPECT_EQ(instance_effective_cost(s, s.current_hand[1]), kXCost);
    EXPECT_EQ(s.current_hand[2].combat_cost, kNoCostOverride) << "Wound";
    for (int i = 0; i < 5; ++i) {
      EXPECT_EQ(instance_effective_cost(s, s.current_hand[3 + i]), rolls[1 + i])
          << "seed " << seed << " Strike " << i;
    }
    EXPECT_EQ(s.card_rng, probe) << "exactly one roll per eligible card";
  }
}

TEST(SneckoOil, SacredBarkDrawsTen) {
  CombatState s = fight_holding({PotionId::SneckoOil}, {RelicId::SacredBark});
  s.draw_pile.assign(12, Card{CardId::Strike});
  ASSERT_TRUE(apply_action(s, drink(PotionId::SneckoOil)));
  EXPECT_EQ(s.current_hand.size(), 10u);
  EXPECT_EQ(s.draw_pile.size(), 2u);
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

// The choice block's offsets, from the published constants.
constexpr int kChoiceBase = CombatEnv::kTurnObsIndex + 1;
constexpr int kStagedBase = kChoiceBase + CombatEnv::kChoiceHeaderSize +
                            kNumOptionSlots * CombatEnv::kChoiceSlotStride;
constexpr int kPileBase =
    CombatEnv::kPlayerObsSize + kMaxEnemies * CombatEnv::kEnemyObsStride;

float staged_obs(const CombatEnv& env, CardId id) {
  return env.obs()[kStagedBase + static_cast<int>(id)];
}

// Plane 0 is the hand, plane 2 the discard (combat_env.h).
float pile_obs(const CombatEnv& env, int plane, CardId id) {
  return env.obs()[kPileBase + plane * kNumCardTypes + static_cast<int>(id)];
}

TEST(ChoiceObservation, TheHeaderSaysCopiesCostZeroAndPicksLeft) {
  // S7: what the menu tells a human. A Power Potion under Sacred Bark adds 2
  // copies that cost 0, and it is one pick.
  CombatState s = fight_holding({PotionId::PowerPotion}, {RelicId::SacredBark});
  ASSERT_TRUE(apply_action(s, drink(PotionId::PowerPotion)));
  CombatEnv env(s);
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 5], 2.0f) << "copies";
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 6], 1.0f) << "arrives free";
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 7], 1.0f) << "picks left";
}

TEST(ChoiceObservation, ToolboxsCardDoesNotArriveFree) {
  // The colorless menu is shared with the Colorless Potion; only this float
  // tells the two apart.
  CombatSetup setup;
  setup.seed = 1;
  setup.deck = starter_deck();
  setup.relics.push_back(HeldRelic{RelicId::Toolbox, 0});
  CombatEnv env(start_combat(setup));
  ASSERT_FLOAT_EQ(env.obs()[kChoiceBase + 0], 1.0f) << "Toolbox's menu is open";
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 6], 0.0f);
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 5], 1.0f);
}

TEST(ChoiceObservation, PicksLeftCountsDownAsCardsAreStaged) {
  CombatState s = fight_holding({PotionId::ElixirPotion});
  s.current_hand = cards({CardId::Strike, CardId::Strike, CardId::Defend});
  ASSERT_TRUE(apply_action(s, drink(PotionId::ElixirPotion)));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  CombatEnv env(s);
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 7],
                  static_cast<float>(kMaxMultiSelectPicks - 1));
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 6], 0.0f) << "Elixir adds no card";
}

TEST(ChoiceObservation, AStagedHandPickMovesFromTheHandPlaneToTheStagedPlane) {
  // #33: the agent used to see the card vanish without knowing it had picked
  // it. HandCardSelectScreen lifts a pick out of the hand.
  CombatState s = fight_holding({PotionId::ElixirPotion});
  s.current_hand = cards({CardId::Strike, CardId::Strike, CardId::Defend});
  ASSERT_TRUE(apply_action(s, drink(PotionId::ElixirPotion)));
  {
    CombatEnv env(s);
    EXPECT_FLOAT_EQ(staged_obs(env, CardId::Strike), 0.0f);
    EXPECT_FLOAT_EQ(pile_obs(env, 0, CardId::Strike), 2.0f);
  }
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Strike)));
  CombatEnv env(s);
  EXPECT_FLOAT_EQ(staged_obs(env, CardId::Strike), 2.0f);
  EXPECT_FLOAT_EQ(staged_obs(env, CardId::Defend), 0.0f);
  EXPECT_FLOAT_EQ(pile_obs(env, 0, CardId::Strike), 0.0f);
  EXPECT_FLOAT_EQ(pile_obs(env, 0, CardId::Defend), 1.0f);
}

TEST(ChoiceObservation, AStagedDiscardPickStaysInTheDiscardPlane) {
  // The grid screen leaves a pick in the pile, so the agent sees it in both.
  CombatState s =
      fight_holding({PotionId::LiquidMemories}, {RelicId::SacredBark});
  s.discard_pile = cards({CardId::Strike, CardId::Defend, CardId::Bludgeon});
  ASSERT_TRUE(apply_action(s, drink(PotionId::LiquidMemories)));
  ASSERT_TRUE(pick(s, option_of(s, CardId::Bludgeon)));
  CombatEnv env(s);
  EXPECT_FLOAT_EQ(staged_obs(env, CardId::Bludgeon), 1.0f);
  EXPECT_FLOAT_EQ(pile_obs(env, 2, CardId::Bludgeon), 1.0f);
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 4], 0.0f) << "not optional yet";
  EXPECT_FLOAT_EQ(env.obs()[kChoiceBase + 7], 1.0f) << "one pick left";
}

TEST(ChoiceObservation, TheChoiceViewCarriesTheSameFacts) {
  CombatState s = fight_holding({PotionId::AttackPotion}, {RelicId::SacredBark});
  ASSERT_TRUE(apply_action(s, drink(PotionId::AttackPotion)));
  const ChoiceView view = CombatEnv(s).choice_view();
  EXPECT_EQ(view.copies, 2);
  EXPECT_TRUE(view.arrives_free);
  EXPECT_EQ(view.picks_left, 1);
  EXPECT_EQ(view.pool, GenerationPool::ClassAttack);
  EXPECT_TRUE(view.staged.empty());
}

}  // namespace
}  // namespace minispire
