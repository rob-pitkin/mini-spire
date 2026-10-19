// Holding relics and potions across a run (docs/design/v2-spec.md §3.0.1,
// §3.3, §4.2).

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>
#include <vector>

#include "run_state.h"

namespace minispire {
namespace {

namespace {

// Find a held relic by id rather than by position. Every run starts holding
// Burning Blood, so relics[0] is the STARTER relic and not whatever the test
// just granted — indexing positionally reads the wrong relic and, because
// Burning Blood's counter is also 0, can do it silently.
const HeldRelic* find_relic(const RunState& run, RelicId id) {
  for (const HeldRelic& r : run.relics) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

}  // namespace

// ------------------------------------------------------------- the starter

// Every Ironclad run begins holding Burning Blood. The relic vocabulary's count
// depends on this: Black Blood is excluded from the 141 precisely because
// relicCanSpawn(BLACK_BLOOD) tests has(BURNING_BLOOD) and finds it true.
TEST(RelicState, ARunStartsHoldingBurningBlood) {
  RunState run = RunState::start(1);
  ASSERT_EQ(run.relics.size(), 1u) << "a run should start with exactly the starter relic";
  EXPECT_EQ(run.relics[0].id, RelicId::BurningBlood);
  EXPECT_EQ(run.relics[0].counter, 0);
}

// -------------------------------------------------------------- acquisition

TEST(RelicState, ObtainingAddsTheRelic) {
  RunState run = RunState::start(1);
  const size_t before = run.relics.size();
  EXPECT_FALSE(run.has_relic(RelicId::Vajra));
  EXPECT_TRUE(run.obtain_relic(RelicId::Vajra));
  EXPECT_TRUE(run.has_relic(RelicId::Vajra));
  EXPECT_EQ(run.relics.size(), before + 1);
}

TEST(RelicState, YouCannotHoldTwoOfTheSameRelic) {
  RunState run = RunState::start(1);
  const size_t before = run.relics.size();
  EXPECT_TRUE(run.obtain_relic(RelicId::Vajra));
  // The second attempt reports failure rather than silently doing nothing —
  // buy_relic depends on that signal to avoid charging for it.
  EXPECT_FALSE(run.obtain_relic(RelicId::Vajra));
  EXPECT_EQ(run.relics.size(), before + 1);
}

TEST(RelicState, RelicsStartWithAZeroCounter) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Nunchaku);
  const HeldRelic* nunchaku = find_relic(run, RelicId::Nunchaku);
  ASSERT_NE(nunchaku, nullptr);
  EXPECT_EQ(nunchaku->counter, 0);
}

// A random draw never offers something already held, so a run cannot be shown
// a relic it cannot take.
TEST(RelicState, RandomDrawsExcludeHeldRelics) {
  RunState run = RunState::start(1);
  std::mt19937 rng(4);
  // Hold every common but one.
  const std::vector<RelicId>& common = relic_pool(RelicTier::Common);
  for (size_t i = 1; i < common.size(); ++i) run.obtain_relic(common[i]);

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(run.random_relic(RelicTier::Common, rng), common[0]);
  }
}

// ------------------------------------------------------------------ potions

TEST(PotionState, ObtainingFillsABeltSlot) {
  RunState run = RunState::start(1);
  EXPECT_TRUE(run.obtain_potion(PotionId::FirePotion));
  EXPECT_EQ(run.potions.size(), 1u);
}

TEST(PotionState, TheBeltHasThreeSlotsByDefault) {
  RunState run = RunState::start(1);
  EXPECT_EQ(run.potion_slots, kBasePotionSlots);
  for (int i = 0; i < kBasePotionSlots; ++i) {
    EXPECT_TRUE(run.obtain_potion(PotionId::FirePotion));
  }
  EXPECT_FALSE(run.obtain_potion(PotionId::FirePotion))
      << "a fourth potion fit in a three-slot belt";
  EXPECT_EQ(run.potions.size(), static_cast<size_t>(kBasePotionSlots));
}

// Unlike relics, duplicate potions are fine — you can carry two Fire Potions.
TEST(PotionState, DuplicatePotionsAreAllowed) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.obtain_potion(PotionId::FirePotion);
  EXPECT_EQ(run.potions.size(), 2u);
}

TEST(PotionState, PotionBeltWidensTheBeltImmediately) {
  RunState run = RunState::start(1);
  for (int i = 0; i < kBasePotionSlots; ++i) run.obtain_potion(PotionId::FirePotion);
  ASSERT_FALSE(run.obtain_potion(PotionId::BlockPotion));

  run.obtain_relic(RelicId::PotionBelt);
  EXPECT_EQ(run.potion_slots, kBasePotionSlots + 2);
  EXPECT_TRUE(run.obtain_potion(PotionId::BlockPotion))
      << "Potion Belt did not open a slot";
}

// Sozu means no potions at all.
TEST(PotionState, SozuBlocksAllPotions) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Sozu);
  EXPECT_FALSE(run.obtain_potion(PotionId::FirePotion));
  EXPECT_TRUE(run.potions.empty());
}

TEST(PotionState, DiscardingFreesTheSlot) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.obtain_potion(PotionId::BlockPotion);
  run.discard_potion(0);

  ASSERT_EQ(run.potions.size(), 1u);
  EXPECT_EQ(run.potions[0], PotionId::BlockPotion);
}

// ------------------------------------------------- projection and write-back

// The fight is CONSTRUCTED with the run's state rather than built fresh and
// patched. Only the construction path is observable from here — the ordering it
// buys (a start-of-combat heal reading real HP, a relic changing the opening
// draw) cannot be asserted until effects exist, which is why the ordering slot
// is commented in start_combat rather than left implicit.
TEST(CombatSetup, TheFightIsBuiltAtTheRunsHpNotAFreshIroncladsHp) {
  CombatSetup setup;
  setup.seed = 1;
  setup.deck = starter_deck();
  setup.hp = 23;
  setup.max_hp = 71;
  setup.relics = {HeldRelic{RelicId::Vajra, 3}};
  setup.potions = {PotionId::FirePotion};

  const CombatState state = start_combat(setup);
  EXPECT_EQ(state.character.hp, 23);
  EXPECT_EQ(state.character.max_hp, 71);
  ASSERT_EQ(state.relics.size(), 1u);
  EXPECT_EQ(state.relics[0].counter, 3) << "the counter did not come along";
  EXPECT_EQ(state.potions.size(), 1u);
}

// v1.0.0's three-argument form still deals a fresh 80/80 Ironclad with nothing
// held, so a CombatEnv built the old way is unchanged (§3.0.1).
TEST(CombatSetup, TheV1FormIsUnchanged) {
  const CombatState state = start_combat(1, EncounterPool::Weak, starter_deck());
  EXPECT_EQ(state.character.hp, IRONCLAD_MAX_HP);
  EXPECT_EQ(state.character.max_hp, IRONCLAD_MAX_HP);
  EXPECT_TRUE(state.relics.empty());
  EXPECT_TRUE(state.potions.empty());
}

// §3.0.1: relics and potions are first-class combat state, so the fight can
// see and use them.
TEST(RelicState, CombatSeesTheRunsRelicsAndPotions) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Vajra);
  run.obtain_potion(PotionId::FirePotion);
  run.begin_combat(EncounterPool::Weak);

  EXPECT_TRUE(run.combat.has_relic(RelicId::Vajra));
  ASSERT_EQ(run.combat.potions.size(), 1u);
  EXPECT_EQ(run.combat.potions[0], PotionId::FirePotion);
}

// §3.3: every relic counter is run-scoped and carries out of the fight.
TEST(RelicState, RelicCountersCarryOutOfCombat) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Nunchaku);
  run.begin_combat(EncounterPool::Weak);

  // By id, not by index — relics[0] is Burning Blood, so bumping it positionally
  // would set the STARTER's counter and then assert on it, passing while testing
  // nothing about Nunchaku.
  for (HeldRelic& r : run.combat.relics) {
    if (r.id == RelicId::Nunchaku) r.counter = 7;
  }
  run.end_combat();

  const HeldRelic* nunchaku = find_relic(run, RelicId::Nunchaku);
  ASSERT_NE(nunchaku, nullptr);
  EXPECT_EQ(nunchaku->counter, 7) << "a relic counter reset at a fight boundary";
}

TEST(RelicState, CountersKeepAccumulatingAcrossFights) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::InkBottle);

  run.begin_combat(EncounterPool::Weak);
  run.combat.relics[0].counter = 5;
  run.end_combat();
  run.skip_card_reward();

  run.advance_to_next_floor(EncounterPool::Weak);
  ASSERT_EQ(run.combat.relics[0].counter, 5) << "the counter did not project in";
  run.combat.relics[0].counter = 9;
  run.end_combat();

  EXPECT_EQ(run.relics[0].counter, 9);
}

// A potion drunk during a fight is gone afterwards.
//
// The drop is suppressed deliberately: end_combat writes potions back and THEN
// rolls a post-fight drop, so a naive count assertion here reads 2 and looks
// like the write-back failed. It is the drop, and the two are easy to confuse.
TEST(PotionState, PotionsDrunkInCombatDoNotComeBack) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.obtain_potion(PotionId::BlockPotion);
  run.potion_chance_bonus = -40;  // 40 - 40 = 0%, so no drop can land
  run.begin_combat(EncounterPool::Weak);

  run.combat.potions.erase(run.combat.potions.begin());
  run.end_combat();

  ASSERT_EQ(run.potions.size(), 1u);
  EXPECT_EQ(run.potions[0], PotionId::BlockPotion);
}

// The other half of that interaction, asserted rather than left implicit: a
// fight can both consume a potion and drop a new one.
TEST(PotionState, AFightCanConsumeAPotionAndStillDropOne) {
  RunState run = RunState::start(1);
  run.obtain_potion(PotionId::FirePotion);
  run.potion_chance_bonus = 60;  // 40 + 60 = 100%, guaranteed
  run.begin_combat(EncounterPool::Weak);

  run.combat.potions.clear();  // drank it
  run.end_combat();

  EXPECT_EQ(run.potions.size(), 1u) << "the drop did not land after the drink";
}

// ------------------------------------------------------------ elite rewards

TEST(RelicState, ElitesGrantARelic) {
  RunState run = RunState::start(3);
  const size_t before = run.relics.size();
  run.begin_combat(EncounterPool::Elite);
  run.end_combat();
  EXPECT_EQ(run.relics.size(), before + 1) << "an elite paid no relic";
}

TEST(RelicState, NormalFightsGrantNoRelic) {
  RunState run = RunState::start(3);
  const size_t before = run.relics.size();
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  EXPECT_EQ(run.relics.size(), before);
}

// The reward screen is rolled as a unit and only then collected, so a relic won
// from THIS fight cannot change what else this fight offered.
//
// White Beast Statue is the sharp case: it forces the potion drop chance to 100.
// Granting the elite's relic before the potion roll let a fight that dropped the
// statue guarantee its own potion — an ordering bug with no local symptom.
TEST(RelicState, AnElitesOwnRelicCannotAlterThatFightsPotionRoll) {
  // Drive the drop chance to zero so any potion that appears can only have come
  // from a relic granted too early.
  for (uint64_t s = 0; s < 200; ++s) {
    RunState run = RunState::start(s);
    run.potion_chance_bonus = -40;
    run.begin_combat(EncounterPool::Elite);
    run.end_combat();
    if (run.has_relic(RelicId::WhiteBeastStatue)) {
      EXPECT_TRUE(run.potions.empty())
          << "seed " << s
          << ": the elite's own White Beast Statue forced its own potion drop";
    }
  }
}

// The potion roll counts gold and relics, not the card reward, which StS
// creates only afterwards (v2-spec.md §4.2, correction #26). A Black Star
// elite therefore counts 3 and still drops: counting the card made it 4 and
// suppressed every one.
TEST(PotionState, ABlackStarElitesTwoRelicsDoNotSuppressItsPotion) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::BlackStar);
  run.potion_chance_bonus = 60;  // 40 + 60 = 100%: certain unless suppressed
  run.begin_combat(EncounterPool::Elite);
  run.end_combat();
  EXPECT_EQ(run.potions.size(), 1u) << "the drop was suppressed";
  EXPECT_EQ(run.potion_chance_bonus, 50) << "the roll missed";
}

// A suppressed reward screen still MISSES, and a miss drifts the chance up. The
// spec sets the chance to zero and rolls; returning early instead would skip the
// drift and quietly lower the drop rate of any run that hit full screens.
TEST(PotionState, AFullRewardScreenStillDriftsTheChanceUp) {
  RunState run = RunState::start(1);
  const int before = run.potion_chance_bonus;
  run.roll_potion_drop(/*rewards_already_on_screen=*/4);
  EXPECT_TRUE(run.potions.empty()) << "a full screen dropped a potion anyway";
  EXPECT_EQ(run.potion_chance_bonus, before + 10)
      << "a suppressed screen skipped the miss drift";
}

// ------------------------------------------------------ Smoke Bomb escapes
//
// potion-effects.md §5.5, Rob V2. StS runs the same reward block as for a win
// and then opens a screen that grants nothing.

TEST(SmokeBombEscape, GrantsNothingAndLeavesTheRoom) {
  // An elite, with the potion drop forced to 100%: there is a relic, a potion,
  // gold and a card reward to withhold.
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 60;
  run.begin_combat(EncounterPool::Elite);
  const int gold = run.gold;
  const size_t relics = run.relics.size();
  const int rarity = run.card_rarity_factor;

  run.combat.outcome = Outcome::Escaped;
  run.end_combat();

  EXPECT_EQ(run.gold, gold);
  EXPECT_EQ(run.relics.size(), relics) << "the elite's relic was granted";
  EXPECT_TRUE(run.potions.empty()) << "the potion drop was granted";
  EXPECT_TRUE(run.card_reward.empty()) << "a card reward was made";
  EXPECT_EQ(run.card_rarity_factor, rarity) << "the card was rolled";
  EXPECT_EQ(run.phase, Phase::Map) << "a smoked screen has nothing to decide";
}

TEST(SmokeBombEscape, ThePotionRollStillMovesTheDrift) {
  // 100% chance: the roll hits, so the drift falls by 10 although nothing is
  // shown (wiki.gg: "rolls for a potion, but does not display" it).
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 60;
  run.begin_combat(EncounterPool::Weak);
  run.combat.outcome = Outcome::Escaped;
  run.end_combat();
  EXPECT_EQ(run.potion_chance_bonus, 50);
}

TEST(SmokeBombEscape, ThePotionRollCountsTheScreenAsAWinWould) {
  // StS rolls the potion before it knows the screen is smoked, so the count
  // of rewards on screen is the unsmoked one. Black Star's two relics make
  // the count matter: the escape's drift must match the same fight won,
  // whatever task #47 rules the count to be.
  auto drift_after = [](bool escape) {
    RunState run = RunState::start(1);
    run.obtain_relic(RelicId::BlackStar);
    run.potion_chance_bonus = 60;
    run.begin_combat(EncounterPool::Elite);
    if (escape) run.combat.outcome = Outcome::Escaped;
    run.end_combat();
    return run.potion_chance_bonus;
  };
  EXPECT_EQ(drift_after(true), drift_after(false));
}

TEST(SmokeBombEscape, VictoryRelicsFireAndInFightGoldIsKept) {
  // endBattle runs onVictory, so Burning Blood heals 6. Hand of Greed's gold
  // was gained during the fight and stays.
  RunState run = RunState::start(1);
  run.begin_combat(EncounterPool::Weak);
  const int gold = run.gold;
  run.combat.character.hp = 50;
  run.combat.gold_gained = 20;
  run.combat.outcome = Outcome::Escaped;
  run.end_combat();
  EXPECT_EQ(run.hp, 56);
  EXPECT_EQ(run.gold, gold + 20);
}

TEST(SmokeBombEscape, OwesNoPrayerWheelScreen) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::PrayerWheel);
  run.begin_combat(EncounterPool::Weak);
  run.combat.outcome = Outcome::Escaped;
  run.end_combat();
  EXPECT_EQ(run.pending_extra_card_rewards, 0);
  EXPECT_EQ(run.phase, Phase::Map);
}

// ------------------------------------------------ potions out of combat
//
// potion-effects.md §6.1: Blood Potion, Fruit Juice and Entropic Brew override
// AbstractPotion.canUse, which is otherwise combat-only.

RunState on_the_map(std::vector<PotionId> potions,
                    std::vector<RelicId> relics = {}) {
  RunState run = RunState::start(1);
  for (RelicId r : relics) run.obtain_relic(r);
  run.potions = std::move(potions);
  run.phase = Phase::Map;
  return run;
}

TEST(PotionsOutOfCombat, BloodPotionHealsTwentyPercentFortyWithSacredBark) {
  RunState run = on_the_map({PotionId::BloodPotion});
  run.hp = 40;
  run.drink_potion(0);
  EXPECT_EQ(run.hp, 40 + 80 * 20 / 100);
  EXPECT_TRUE(run.potions.empty());

  RunState bark = on_the_map({PotionId::BloodPotion}, {RelicId::SacredBark});
  bark.hp = 40;
  bark.drink_potion(0);
  EXPECT_EQ(bark.hp, 40 + 80 * 40 / 100);
}

TEST(PotionsOutOfCombat, FruitJuiceRaisesMaxHpAndHeals) {
  RunState run = on_the_map({PotionId::FruitJuice});
  run.hp = 40;
  run.drink_potion(0);
  EXPECT_EQ(run.max_hp, 85);
  EXPECT_EQ(run.hp, 45);

  RunState bark = on_the_map({PotionId::FruitJuice}, {RelicId::SacredBark});
  bark.drink_potion(0);
  EXPECT_EQ(bark.max_hp, 90);
}

TEST(PotionsOutOfCombat, ToyOrnithopterHealsFiveAndMagicFlowerDoesNotApply) {
  RunState run = on_the_map({PotionId::FruitJuice},
                            {RelicId::ToyOrnithopter, RelicId::MagicFlower});
  run.hp = 40;
  run.drink_potion(0);
  EXPECT_EQ(run.hp, 40 + 5 + 5) << "Fruit Juice's 5, then Toy Ornithopter's 5";
}

TEST(PotionsOutOfCombat, OtherPotionsCannotBeDrunk) {
  RunState run = on_the_map({PotionId::FirePotion, PotionId::BlockPotion});
  run.drink_potion(0);
  run.drink_potion(1);
  EXPECT_EQ(run.potions.size(), 2u);
}

TEST(PotionsOutOfCombat, NotInCombatPhaseNorDuringADeckChoice) {
  RunState fighting = on_the_map({PotionId::BloodPotion});
  fighting.phase = Phase::Combat;
  fighting.hp = 40;
  fighting.drink_potion(0);
  EXPECT_EQ(fighting.hp, 40) << "combat has its own action";

  // Bottled Flame opens a deck choice over the starter's Attacks.
  RunState choosing = on_the_map({PotionId::BloodPotion});
  choosing.obtain_relic(RelicId::BottledFlame);
  ASSERT_TRUE(choosing.has_pending_deck_choice());
  choosing.hp = 40;
  choosing.drink_potion(0);
  EXPECT_EQ(choosing.hp, 40);
}

TEST(PotionsOutOfCombat, EntropicBrewFillsTheBeltFromTheFloorsStream) {
  // Its own slot included, one unlimited roll per belt slot.
  RunState run = on_the_map({PotionId::EntropicBrew, PotionId::FirePotion});
  std::mt19937 expected = run.potion_rng_for_floor();
  for (int i = 0; i < kBasePotionSlots; ++i) random_potion(expected, false);
  run.drink_potion(0);
  ASSERT_EQ(run.potions.size(), 3u);
  EXPECT_EQ(run.potions[0], PotionId::FirePotion);
  EXPECT_TRUE(run.floor_potion_rng == expected);
}

TEST(PotionsOutOfCombat, EntropicBrewBrewsTheStreamsUnlimitedDraws) {
  // The potions are the floor stream's next unlimited draws, in order — an
  // invariant for every seed. A limited draw differs whenever a rare comes up
  // (its pool is one shorter), which is how a wrong flag here would show.
  for (uint64_t seed = 1; seed <= 32; ++seed) {
    RunState run = RunState::start(seed);
    run.potions = {PotionId::EntropicBrew};
    run.phase = Phase::Map;
    std::mt19937 stream = run.potion_rng_for_floor();
    std::vector<PotionId> predicted;
    for (int i = 0; i < kBasePotionSlots; ++i) {
      predicted.push_back(random_potion(stream, /*limited=*/false));
    }
    run.drink_potion(0);
    EXPECT_EQ(run.potions, predicted) << "seed " << seed;
  }
}

TEST(PotionsOutOfCombat, EntropicBrewWithSozuRollsNothing) {
  RunState run = on_the_map({PotionId::EntropicBrew}, {RelicId::Sozu});
  const std::mt19937 before = run.potion_rng_for_floor();
  run.drink_potion(0);
  EXPECT_TRUE(run.potions.empty());
  EXPECT_TRUE(run.floor_potion_rng == before);
}

// ------------------------------------------------ the floor's potion stream

TEST(FloorPotionStream, CombatBorrowsTheFloorsGeneratorAsItIs) {
  // Advanced before the fight (as a potion drunk on this floor would), and
  // the fight gets it in that state, not a fresh one.
  RunState run = RunState::start(1);
  random_potion(run.potion_rng_for_floor(), false);
  run.begin_combat(EncounterPool::Weak);
  EXPECT_TRUE(run.combat.potion_rng == run.floor_potion_rng);
}

TEST(FloorPotionStream, TheDropContinuesWhereTheFightLeftIt) {
  // An Entropic Brew in the fight advances the borrowed generator; the drop
  // after the fight must continue from there, not replay the same numbers.
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 60;
  run.begin_combat(EncounterPool::Weak);
  for (int i = 0; i < kBasePotionSlots; ++i) {
    random_potion(run.combat.potion_rng, true);
  }
  const std::mt19937 after_fight = run.combat.potion_rng;
  run.end_combat();

  RunState twin = RunState::start(1);
  twin.potion_chance_bonus = 60;
  twin.floor_potion_rng = after_fight;
  twin.floor_potion_rng_floor = twin.floor;
  twin.roll_potion_drop(/*rewards_already_on_screen=*/2);
  EXPECT_TRUE(run.floor_potion_rng == twin.floor_potion_rng);
}

// ------------------------------------------------ the potion reward screen
//
// potion-effects.md §6.3, Rob W1.

TEST(PotionRewards, ADropOnAFullBeltWaitsForRoom) {
  RunState run = RunState::start(1);
  run.potions = {PotionId::FirePotion, PotionId::FirePotion,
                 PotionId::FirePotion};
  run.potion_chance_bonus = 60;  // a certain drop
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  ASSERT_EQ(run.pending_potion_rewards.size(), 1u) << "the drop was lost";
  EXPECT_EQ(run.potions.size(), 3u);

  run.take_potion_reward(0);
  EXPECT_EQ(run.pending_potion_rewards.size(), 1u) << "claimed on a full belt";

  run.discard_potion(0);
  run.take_potion_reward(0);
  EXPECT_TRUE(run.pending_potion_rewards.empty());
  EXPECT_EQ(run.potions.size(), 3u);
}

TEST(PotionRewards, LeavingTheRoomLosesWhatWasNotClaimed) {
  RunState run = RunState::start(1);
  run.potions = {PotionId::FirePotion, PotionId::FirePotion,
                 PotionId::FirePotion};
  run.potion_chance_bonus = 60;
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  ASSERT_EQ(run.pending_potion_rewards.size(), 1u);
  run.skip_card_reward();
  EXPECT_TRUE(run.pending_potion_rewards.empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

TEST(PotionRewards, ADropWithRoomIsTakenAtOnce) {
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 60;
  run.begin_combat(EncounterPool::Weak);
  run.end_combat();
  EXPECT_EQ(run.potions.size(), 1u);
  EXPECT_TRUE(run.pending_potion_rewards.empty());
}

// ------------------------------------------------ Cauldron (§6.2, Rob W2)

TEST(CauldronRelic, BrewsFiveUniformDrawsFromTheFloorsStream) {
  // PotionHelper.getRandomPotion: one uniform draw over every potion, no
  // rarity roll. Predicted with a plain uniform draw, so a rarity-rolling
  // Cauldron (two draws per potion) cannot match. An invariant for every seed.
  for (uint64_t seed = 1; seed <= 16; ++seed) {
    RunState run = RunState::start(seed);
    run.phase = Phase::Shop;
    std::mt19937 stream = run.potion_rng_for_floor();
    std::vector<PotionId> predicted;
    for (int i = 0; i < kCauldronPotions; ++i) {
      predicted.push_back(static_cast<PotionId>(
          std::uniform_int_distribution<int>(0, kNumPotions - 1)(stream)));
    }
    run.obtain_relic(RelicId::Cauldron);
    EXPECT_EQ(run.pending_potion_rewards, predicted) << "seed " << seed;
  }
}

TEST(CauldronRelic, FivePotionsWaitWhenTheyDoNotAllFit) {
  RunState run = RunState::start(1);
  run.phase = Phase::Shop;
  run.obtain_relic(RelicId::Cauldron);
  ASSERT_EQ(run.pending_potion_rewards.size(), 5u);
  EXPECT_TRUE(run.potions.empty()) << "taken without the player choosing";

  // Claim three of the five; the fourth does not fit until a slot is freed.
  run.take_potion_reward(4);
  run.take_potion_reward(0);
  run.take_potion_reward(0);
  EXPECT_EQ(run.potions.size(), 3u);
  EXPECT_EQ(run.pending_potion_rewards.size(), 2u);
  run.take_potion_reward(0);
  EXPECT_EQ(run.pending_potion_rewards.size(), 2u);
  run.discard_potion(0);
  run.take_potion_reward(0);
  EXPECT_EQ(run.pending_potion_rewards.size(), 1u);

  run.close_potion_rewards();
  EXPECT_TRUE(run.pending_potion_rewards.empty());
  EXPECT_EQ(run.potions.size(), 3u);
}

TEST(CauldronRelic, WithRoomForAllFiveTheyAreTakenAtOnce) {
  RunState run = RunState::start(1);
  run.phase = Phase::Shop;
  run.obtain_relic(RelicId::PotionBelt);
  run.obtain_relic(RelicId::Cauldron);
  EXPECT_EQ(run.potions.size(), 5u);
  EXPECT_TRUE(run.pending_potion_rewards.empty());
}

TEST(CauldronRelic, WithSozuNothingIsObtained) {
  RunState run = RunState::start(1);
  run.phase = Phase::Shop;
  run.obtain_relic(RelicId::Sozu);
  run.obtain_relic(RelicId::Cauldron);
  EXPECT_TRUE(run.potions.empty());
  EXPECT_TRUE(run.pending_potion_rewards.empty());
}

TEST(CauldronRelic, ItsScreenClosesTheShopUntilClosed) {
  RunState run = RunState::start(1);
  run.phase = Phase::Shop;
  run.generate_shop();
  run.gold = 999;
  run.obtain_relic(RelicId::Cauldron);
  ASSERT_TRUE(run.shop_is_covered());

  run.buy_card(0);
  EXPECT_EQ(run.gold, 999) << "bought through Cauldron's screen";
  run.leave_shop();
  EXPECT_EQ(run.phase, Phase::Shop) << "left through Cauldron's screen";

  run.close_potion_rewards();
  run.leave_shop();
  EXPECT_EQ(run.phase, Phase::Map);
}

TEST(CauldronRelic, RollsAndDiscardsACardRewardAtShopOdds) {
  // The rarity counter must end where a shop-odds card reward leaves it, and
  // the card offer on screen is untouched. An invariant for every seed; the
  // shop's 9 differs from a fight's 3 only on a low roll, hence 64 seeds.
  for (uint64_t seed = 1; seed <= 64; ++seed) {
    RunState run = RunState::start(seed);
    run.phase = Phase::Shop;
    run.card_reward = {Card{CardId::Strike}};
    run.obtain_relic(RelicId::Cauldron);

    RunState twin = RunState::start(seed);
    twin.generate_card_reward(RewardSource::Shop);

    EXPECT_EQ(run.card_rarity_factor, twin.card_rarity_factor) << "seed " << seed;
    ASSERT_EQ(run.card_reward.size(), 1u);
    EXPECT_EQ(run.card_reward[0].card_id, CardId::Strike);
  }
}

TEST(CardRarityOdds, ShopMonsterEliteAndBossThresholds) {
  // AbstractRoom.getCardRarity with each room's base chances; the roll
  // already includes the pity counter.
  EXPECT_EQ(card_rarity_for_roll(8, RewardSource::Shop), CardRarity::Rare);
  EXPECT_EQ(card_rarity_for_roll(9, RewardSource::Shop), CardRarity::Uncommon);
  EXPECT_EQ(card_rarity_for_roll(45, RewardSource::Shop), CardRarity::Uncommon);
  EXPECT_EQ(card_rarity_for_roll(46, RewardSource::Shop), CardRarity::Common);

  EXPECT_EQ(card_rarity_for_roll(2, RewardSource::Monster), CardRarity::Rare);
  EXPECT_EQ(card_rarity_for_roll(3, RewardSource::Monster),
            CardRarity::Uncommon);
  EXPECT_EQ(card_rarity_for_roll(39, RewardSource::Monster),
            CardRarity::Uncommon);
  EXPECT_EQ(card_rarity_for_roll(40, RewardSource::Monster), CardRarity::Common);

  EXPECT_EQ(card_rarity_for_roll(9, RewardSource::Elite), CardRarity::Rare);
  EXPECT_EQ(card_rarity_for_roll(10, RewardSource::Elite), CardRarity::Uncommon);
  EXPECT_EQ(card_rarity_for_roll(49, RewardSource::Elite), CardRarity::Uncommon);
  EXPECT_EQ(card_rarity_for_roll(50, RewardSource::Elite), CardRarity::Common);

  EXPECT_EQ(card_rarity_for_roll(99, RewardSource::Boss), CardRarity::Rare);
}

// White Beast Statue forces the chance to 100, but a full screen still wins.
TEST(PotionState, AFullScreenSuppressesEvenWhiteBeastStatue) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::WhiteBeastStatue);
  run.roll_potion_drop(/*rewards_already_on_screen=*/4);
  EXPECT_TRUE(run.potions.empty());
}

// ------------------------------------------------------- pickup effects (2a)

// Gaining Max HP HEALS by the same amount. That is a general StS rule, not a
// per-relic quirk, and it is why a Strawberry taken at low HP is worth 7 HP now
// as well as 7 capacity.
TEST(RelicPickup, FoodRelicsRaiseMaxHpAndHealTheSameAmount) {
  struct Case { RelicId id; int amount; };
  const Case cases[] = {{RelicId::Strawberry, 7},
                        {RelicId::Pear, 10},
                        {RelicId::Mango, 14}};
  for (const Case& c : cases) {
    RunState run = RunState::start(1);
    run.hp = 40;
    const int max_before = run.max_hp;
    run.obtain_relic(c.id);
    EXPECT_EQ(run.max_hp, max_before + c.amount) << relic_name(c.id);
    EXPECT_EQ(run.hp, 40 + c.amount) << relic_name(c.id) << " did not heal";
  }
}

// At full HP the heal has nowhere to go, but Max HP still rises — and HP must
// track it rather than being left behind at the old maximum.
TEST(RelicPickup, AFoodRelicTakenAtFullHpKeepsYouAtFull) {
  RunState run = RunState::start(1);
  ASSERT_EQ(run.hp, run.max_hp);
  run.obtain_relic(RelicId::Pear);
  EXPECT_EQ(run.hp, run.max_hp);
  EXPECT_EQ(run.max_hp, IRONCLAD_MAX_HP + 10);
}

// Lee's Waffle is NOT just a bigger Strawberry: +7 Max HP and a FULL heal.
TEST(RelicPickup, LeesWaffleFullyHeals) {
  RunState run = RunState::start(1);
  run.hp = 12;
  run.obtain_relic(RelicId::LeesWaffle);
  EXPECT_EQ(run.max_hp, IRONCLAD_MAX_HP + 7);
  EXPECT_EQ(run.hp, run.max_hp) << "Lee's Waffle healed only the Max HP gain";
}

TEST(RelicPickup, WarPaintUpgradesTwoSkills) {
  RunState run = RunState::start(1);
  // The starter deck's Skills are 4 Defends. Two should come back upgraded.
  const std::vector<Card> before = run.master_deck;
  run.obtain_relic(RelicId::WarPaint);

  int changed = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    if (!before[i].same_as(run.master_deck[i])) ++changed;
  }
  EXPECT_EQ(changed, 2) << "War Paint should upgrade exactly 2 cards";

  // And they must be Skills, not Attacks.
  for (size_t i = 0; i < before.size(); ++i) {
    if (before[i].same_as(run.master_deck[i])) continue;
    EXPECT_EQ(CARD_DATABASE.at(before[i].card_id).type, CardType::Skill)
        << "War Paint upgraded a non-Skill";
  }
}

TEST(RelicPickup, WhetstoneUpgradesTwoAttacks) {
  RunState run = RunState::start(1);
  const std::vector<Card> before = run.master_deck;
  run.obtain_relic(RelicId::Whetstone);

  int changed = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    if (before[i].same_as(run.master_deck[i])) continue;
    ++changed;
    EXPECT_EQ(CARD_DATABASE.at(before[i].card_id).type, CardType::Attack)
        << "Whetstone upgraded a non-Attack";
  }
  EXPECT_EQ(changed, 2);
}

// A deck with nothing eligible is a real outcome, not an error. The relic is
// still obtained; it just has nothing to do.
TEST(RelicPickup, WarPaintOnADeckWithNoSkillsUpgradesNothing) {
  RunState run = RunState::start(1);
  run.master_deck.clear();
  run.add_card(Card{CardId::Strike});
  const std::vector<Card> before = run.master_deck;

  EXPECT_TRUE(run.obtain_relic(RelicId::WarPaint));

  ASSERT_EQ(run.master_deck.size(), before.size());
  EXPECT_TRUE(before[0].same_as(run.master_deck[0]));
}

// Fewer eligible cards than asked: upgrade what there is, and leave the rest
// alone rather than reaching into another card type to make up the count.
TEST(RelicPickup, WarPaintWithOneSkillUpgradesJustThatOne) {
  RunState run = RunState::start(1);
  run.master_deck.clear();
  run.add_card(Card{CardId::Defend});
  run.add_card(Card{CardId::Strike});
  const std::vector<Card> before = run.master_deck;

  run.obtain_relic(RelicId::WarPaint);

  EXPECT_FALSE(before[0].same_as(run.master_deck[0]))
      << "the only Skill was not upgraded";
  EXPECT_TRUE(before[1].same_as(run.master_deck[1]))
      << "War Paint reached into the Attacks to make up its count";
}

// Two relics picked up on the SAME floor must not share a draw. The stream is
// indexed by RelicId for exactly this reason — a floor index would hand War
// Paint and Whetstone identical rolls.
TEST(RelicPickup, TwoPickupsOnOneFloorDrawIndependently) {
  RunState a = RunState::start(7);
  a.floor = 3;
  a.obtain_relic(RelicId::WarPaint);

  RunState b = RunState::start(7);
  b.floor = 3;
  b.obtain_relic(RelicId::Whetstone);

  // Different card TYPES, so the picks cannot coincide by construction — what
  // this asserts is that each fired against its own pool rather than one
  // stream's draw being reused.
  int a_changed = 0, b_changed = 0;
  const RunState fresh = RunState::start(7);
  for (size_t i = 0; i < fresh.master_deck.size(); ++i) {
    if (!fresh.master_deck[i].same_as(a.master_deck[i])) ++a_changed;
    if (!fresh.master_deck[i].same_as(b.master_deck[i])) ++b_changed;
  }
  EXPECT_EQ(a_changed, 2);
  EXPECT_EQ(b_changed, 2);
}

// Pickup effects fire once. Re-obtaining is refused, so the HP cannot be
// farmed by repeated calls.
TEST(RelicPickup, APickupEffectCannotFireTwice) {
  RunState run = RunState::start(1);
  run.hp = 40;
  EXPECT_TRUE(run.obtain_relic(RelicId::Pear));
  const int hp_after = run.hp;
  const int max_after = run.max_hp;

  EXPECT_FALSE(run.obtain_relic(RelicId::Pear));
  EXPECT_EQ(run.hp, hp_after);
  EXPECT_EQ(run.max_hp, max_after);
}

// --------------------------------------------- reward generation (batch 2d)

namespace {

RunState fight_and_win(std::vector<RelicId> ids, EncounterPool pool,
                       uint64_t seed = 3) {
  RunState run = RunState::start(seed);
  for (RelicId id : ids) run.obtain_relic(id);
  run.begin_combat(pool);
  run.combat.character.hp = 50;
  run.end_combat();
  return run;
}

}  // namespace

// ------------------------------------------------------------- Golden Idol

TEST(RewardRelics, GoldenIdolPaysTwentyFivePercentMore) {
  for (uint64_t s = 0; s < 40; ++s) {
    const RunState bare = fight_and_win({}, EncounterPool::Weak, s);
    const RunState rich =
        fight_and_win({RelicId::GoldenIdol}, EncounterPool::Weak, s);
    // The bonus is rounded and added, so the result is the base plus
    // lround(base * 0.25).
    const int expected =
        bare.gold + static_cast<int>(std::lround(bare.gold * 0.25f));
    EXPECT_EQ(rich.gold, expected) << "seed " << s << ", base " << bare.gold;
  }
}

// ------------------------------------------------------------- Ectoplasm

TEST(RewardRelics, EctoplasmMeansNoGoldAtAll) {
  const RunState run =
      fight_and_win({RelicId::Ectoplasm}, EncounterPool::Weak);
  EXPECT_EQ(run.gold, 0) << "Ectoplasm gained gold";
}

// Ectoplasm is applied last, so it overrides Golden Idol rather than racing it.
// "You can no longer gain Gold" has no reading in which +25% of nothing is
// something.
TEST(RewardRelics, EctoplasmBeatsGoldenIdol) {
  const RunState run = fight_and_win(
      {RelicId::GoldenIdol, RelicId::Ectoplasm}, EncounterPool::Weak);
  EXPECT_EQ(run.gold, 0);
}

// --------------------------------------------- card reward screen size

TEST(RewardRelics, QuestionCardAddsAnOption) {
  const RunState bare = fight_and_win({}, EncounterPool::Weak);
  const RunState with =
      fight_and_win({RelicId::QuestionCard}, EncounterPool::Weak);
  EXPECT_EQ(with.card_reward.size(), bare.card_reward.size() + 1);
}

TEST(RewardRelics, BustedCrownRemovesTwoOptions) {
  const RunState bare = fight_and_win({}, EncounterPool::Weak);
  const RunState with =
      fight_and_win({RelicId::BustedCrown}, EncounterPool::Weak);
  EXPECT_EQ(with.card_reward.size(), bare.card_reward.size() - 2);
}

// They stack: 3 + 1 - 2 = 2, which the wiki states directly.
TEST(RewardRelics, QuestionCardAndBustedCrownStack) {
  const RunState bare = fight_and_win({}, EncounterPool::Weak);
  const RunState with = fight_and_win(
      {RelicId::QuestionCard, RelicId::BustedCrown}, EncounterPool::Weak);
  EXPECT_EQ(with.card_reward.size(), bare.card_reward.size() - 1);
}

// Busted Crown does not touch shop offerings — the wiki calls this out as the
// way to play around it.
TEST(RewardRelics, BustedCrownDoesNotShrinkTheShop) {
  RunState bare = RunState::start(1);
  bare.floor = 5;
  bare.phase = Phase::Shop;
  bare.generate_shop();

  RunState with = RunState::start(1);
  with.obtain_relic(RelicId::BustedCrown);
  with.floor = 5;
  with.phase = Phase::Shop;
  with.generate_shop();

  EXPECT_EQ(with.shop_cards.size(), bare.shop_cards.size());
}

// ------------------------------------------------------------ Prayer Wheel

// A SECOND screen, not more cards on the first. Taking or skipping the first
// rolls another instead of leaving the room.
TEST(RewardRelics, PrayerWheelGivesASecondCardRewardScreen) {
  RunState run = fight_and_win({RelicId::PrayerWheel}, EncounterPool::Weak);
  const size_t first_size = run.card_reward.size();
  ASSERT_GT(first_size, 0u);

  run.skip_card_reward();

  EXPECT_EQ(run.card_reward.size(), first_size)
      << "Prayer Wheel did not roll a second screen";
  EXPECT_EQ(run.phase, Phase::Reward) << "the run left before the second screen";

  run.skip_card_reward();
  EXPECT_TRUE(run.card_reward.empty());
  EXPECT_NE(run.phase, Phase::Reward) << "the run never left after both screens";
}

TEST(RewardRelics, WithoutPrayerWheelThereIsOneScreen) {
  RunState run = fight_and_win({}, EncounterPool::Weak);
  ASSERT_FALSE(run.card_reward.empty());
  run.skip_card_reward();
  EXPECT_TRUE(run.card_reward.empty());
}

// Explicitly not on elites — the wiki says so, and this is the kind of
// condition that is easy to implement as "always".
// ------------------------------------------------------------ Singing Bowl

// A reward screen open on floor 0, so leaving it goes back to the map rather
// than ending the run.
RunState at_a_card_reward(bool with_bowl) {
  RunState run = RunState::start(1);
  if (with_bowl) run.obtain_relic(RelicId::SingingBowl);
  run.phase = Phase::Reward;
  run.card_reward = {Card{CardId::Anger}, Card{CardId::Inflame}};
  run.hp = run.max_hp - 10;
  return run;
}

// +2 Max HP, and +2 HP with it; no card; the screen closes as a skip does.
TEST(RewardRelics, SingingBowlTradesTheCardForTwoMaxHp) {
  RunState run = at_a_card_reward(true);
  const int max_before = run.max_hp;
  const int hp_before = run.hp;
  const size_t deck_before = run.master_deck.size();

  run.take_max_hp_instead();

  EXPECT_EQ(run.max_hp, max_before + kSingingBowlMaxHp);
  EXPECT_EQ(run.hp, hp_before + kSingingBowlMaxHp) << "gaining Max HP heals";
  EXPECT_EQ(run.master_deck.size(), deck_before);
  EXPECT_TRUE(run.card_reward.empty());
  EXPECT_EQ(run.phase, Phase::Map);
}

// It closes ONE screen. A Prayer Wheel's second reward still follows.
TEST(RewardRelics, SingingBowlLeavesPrayerWheelsSecondScreen) {
  RunState run = at_a_card_reward(true);
  run.combat_source = RewardSource::Monster;
  run.pending_extra_card_rewards = 1;

  run.take_max_hp_instead();

  EXPECT_FALSE(run.card_reward.empty()) << "the second screen did not open";
  EXPECT_EQ(run.pending_extra_card_rewards, 0);
}

TEST(RewardRelics, WithoutSingingBowlThereIsNoMaxHpOption) {
  RunState run = at_a_card_reward(false);
  const int max_before = run.max_hp;

  run.take_max_hp_instead();

  EXPECT_EQ(run.max_hp, max_before);
  EXPECT_EQ(run.card_reward.size(), 2u) << "the screen closed anyway";
}

TEST(RewardRelics, PrayerWheelDoesNotFireOnElites) {
  RunState run = fight_and_win({RelicId::PrayerWheel}, EncounterPool::Elite);
  EXPECT_EQ(run.pending_extra_card_rewards, 0);
}

// -------------------------------------------------------------- Black Star

TEST(RewardRelics, BlackStarGivesElitesASecondRelic) {
  const RunState bare = fight_and_win({}, EncounterPool::Elite);
  RunState with = RunState::start(3);
  with.obtain_relic(RelicId::BlackStar);
  const size_t before = with.relics.size();
  with.begin_combat(EncounterPool::Elite);
  with.combat.character.hp = 50;
  with.end_combat();

  EXPECT_EQ(with.relics.size(), before + 2) << "Black Star paid only one relic";
  (void)bare;
}

TEST(RewardRelics, BlackStarDoesNotAffectNormalFights) {
  RunState run = RunState::start(3);
  run.obtain_relic(RelicId::BlackStar);
  const size_t before = run.relics.size();
  run.begin_combat(EncounterPool::Weak);
  run.combat.character.hp = 50;
  run.end_combat();
  EXPECT_EQ(run.relics.size(), before);
}

// The two relics must be different — an elite cannot pay the same relic twice.
TEST(RewardRelics, BlackStarsTwoRelicsAreDistinct) {
  for (uint64_t s = 0; s < 60; ++s) {
    RunState run = RunState::start(s);
    run.obtain_relic(RelicId::BlackStar);
    const size_t before = run.relics.size();
    run.begin_combat(EncounterPool::Elite);
    run.combat.character.hp = 50;
    run.end_combat();
    ASSERT_EQ(run.relics.size(), before + 2) << "seed " << s;
    EXPECT_NE(run.relics[before].id, run.relics[before + 1].id) << "seed " << s;
  }
}

// returnRandomNonCampfireRelic: Black Star's extra relic is never Girya, Peace
// Pipe or Shovel. Every other Rare is already held, so a Rare second draw has
// only campfire relics to pick from — without the exclusion it takes one; with
// it, it draws nothing. The assertion holds for every seed; the held Rares only
// make a Rare second draw decisive rather than rare.
TEST(RewardRelics, BlackStarsExtraRelicIsNeverACampfireRelic) {
  const auto is_campfire = [](RelicId id) {
    return id == RelicId::Girya || id == RelicId::PeacePipe ||
           id == RelicId::Shovel;
  };
  for (uint64_t s = 0; s < 60; ++s) {
    RunState run = RunState::start(s);
    run.obtain_relic(RelicId::BlackStar);
    for (RelicId id : relic_pool(RelicTier::Rare)) {
      if (!is_campfire(id)) run.relics.push_back(HeldRelic{id, 0});
    }
    const size_t before = run.relics.size();
    run.begin_combat(EncounterPool::Elite);
    run.combat.character.hp = 50;
    run.end_combat();

    if (run.relics.size() == before + 2) {
      EXPECT_FALSE(is_campfire(run.relics[before + 1].id)) << "seed " << s;
    }
  }
}

// ------------------------------------------------- run-state spawn gates

// Never stocked in a shop, anywhere else as normal.
TEST(RelicSpawn, ShopExcludedRelicsCannotSpawnInAShop) {
  RunState run = RunState::start(1);
  for (RelicId id : {RelicId::TheCourier, RelicId::MawBank, RelicId::OldCoin,
                     RelicId::SmilingMask}) {
    run.phase = Phase::Shop;
    EXPECT_FALSE(run.relic_can_spawn(id)) << static_cast<int>(id);
    run.phase = Phase::Reward;
    EXPECT_TRUE(run.relic_can_spawn(id)) << static_cast<int>(id);
  }
}

// The same rule through generate_shop, as an invariant over every stocking.
TEST(RelicSpawn, AShopNeverStocksAShopExcludedRelic) {
  for (int floor = 1; floor <= 15; ++floor) {
    RunState run = RunState::start(static_cast<uint64_t>(floor));
    run.floor = floor;
    run.enter_room(RoomType::Shop);
    for (const ShopRelic& offer : run.shop_relics) {
      EXPECT_NE(offer.id, RelicId::TheCourier) << "floor " << floor;
      EXPECT_NE(offer.id, RelicId::MawBank) << "floor " << floor;
      EXPECT_NE(offer.id, RelicId::OldCoin) << "floor " << floor;
      EXPECT_NE(offer.id, RelicId::SmilingMask) << "floor " << floor;
    }
  }
}

// Fewer than two of the three HELD. One held leaves the other two open.
TEST(RelicSpawn, CampfireRelicsStopAtTwoHeld) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Girya);
  EXPECT_TRUE(run.relic_can_spawn(RelicId::PeacePipe));
  EXPECT_TRUE(run.relic_can_spawn(RelicId::Shovel));

  run.obtain_relic(RelicId::PeacePipe);
  EXPECT_FALSE(run.relic_can_spawn(RelicId::Shovel));
}

// --------------------------------------------------------------- the chest

// A treasure room is a pass-through: it opens and EXITS. Every generated map
// hard-sets row 8 to Treasure, so a room that set its phase without leaving
// would strand every run on floor 9 — and because leave_room is also where the
// win check lives, a chest on the final floor would never record a win.
TEST(Chest, ATreasureRoomOpensAndThenLeaves) {
  RunState run = RunState::start(1);
  run.floor = 5;
  const size_t before = run.relics.size();

  run.enter_room(RoomType::Treasure);

  EXPECT_EQ(run.relics.size(), before + 1) << "the chest paid nothing";
  EXPECT_NE(run.phase, Phase::Treasure)
      << "the run is stranded in the treasure phase with no way out";
  EXPECT_EQ(run.phase, Phase::Map);
}

// The chest's rules, tested over EVERY roll through the pure functions
// open_chest calls — no seeds, so nothing depends on what a stream produces.
// (These replaced 300- and 200-seed sweeps that asserted "at least one common",
// "at least one rare", "some but not all paid gold": probability arguments over
// draws that are not the same on every standard library.)

// 50 / 33 / 17, at exactly these boundaries.
TEST(Chest, SizeRollMapsToSmallMediumLarge) {
  for (int roll = 0; roll < 100; ++roll) {
    const ChestSize expected = roll < 50   ? ChestSize::Small
                               : roll < 83 ? ChestSize::Medium
                                           : ChestSize::Large;
    EXPECT_EQ(chest_size_for_roll(roll), expected) << "roll " << roll;
  }
}

// The size decides the tier odds: a large chest can never give a common and a
// small can never give a rare — a different distribution from an elite's.
// Every tier each size CAN give is reached by some roll.
TEST(Chest, TierRespectsChestSizeBounds) {
  struct Expect {
    ChestSize size;
    bool common, uncommon, rare;
  };
  for (const Expect& e : {Expect{ChestSize::Small, true, true, false},
                          Expect{ChestSize::Medium, true, true, true},
                          Expect{ChestSize::Large, false, true, true}}) {
    bool saw_common = false, saw_uncommon = false, saw_rare = false;
    for (int roll = 0; roll < 100; ++roll) {
      switch (chest_tier_for_roll(e.size, roll)) {
        case RelicTier::Common: saw_common = true; break;
        case RelicTier::Uncommon: saw_uncommon = true; break;
        case RelicTier::Rare: saw_rare = true; break;
        default: ADD_FAILURE() << "a chest rolled a non-chest tier"; break;
      }
    }
    EXPECT_EQ(saw_common, e.common) << static_cast<int>(e.size);
    EXPECT_EQ(saw_uncommon, e.uncommon) << static_cast<int>(e.size);
    EXPECT_EQ(saw_rare, e.rare) << static_cast<int>(e.size);
  }
}

// Gold only with the size's LOWEST tier (the wiki's rule, and the reason one
// roll decides both), always the size's own amount, and on some rolls but not
// all.
TEST(Chest, GoldComesOnlyWithTheLowestTier) {
  for (ChestSize size : {ChestSize::Small, ChestSize::Medium, ChestSize::Large}) {
    const RelicTier lowest =
        size == ChestSize::Large ? RelicTier::Uncommon : RelicTier::Common;
    int paid = 0;
    for (int roll = 0; roll < 100; ++roll) {
      const int gold = chest_gold_for_roll(size, roll);
      if (gold == 0) continue;
      ++paid;
      EXPECT_EQ(gold, kChestGoldAmounts[static_cast<int>(size)]);
      EXPECT_EQ(chest_tier_for_roll(size, roll), lowest)
          << "gold came with a higher tier on roll " << roll;
    }
    EXPECT_EQ(paid, kChestGoldChances[static_cast<int>(size)])
        << "the gold band is the wrong width";
  }
}

// Chest gold is a reward StS claims through player.gainGold, so Ectoplasm
// refuses it; it used to be added directly. Compared against the same chest
// without Ectoplasm — a boss relic, absent from the chest pools, so both open
// the identical chest. The invariant holds on every seed; a seed whose chest
// rolls no gold simply cannot show the bug, which is why the comparison is
// spelled out rather than asserting "some seed paid gold".
TEST(Chest, EctoplasmRefusesChestGold) {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    RunState control = RunState::start(seed);
    RunState held = RunState::start(seed);
    held.obtain_relic(RelicId::Ectoplasm);
    control.floor = held.floor = 9;

    control.open_chest();
    held.open_chest();

    EXPECT_EQ(held.relics.back().id, control.relics.back().id) << "seed " << seed;
    EXPECT_EQ(held.gold, 0) << "seed " << seed << ": the chest paid "
                            << control.gold << " through Ectoplasm";
  }
}

// The same seed and floor open the same chest. Compares the chest's OWN relic
// — relics.back(). This compared relics[0], which is always the starter Burning
// Blood, so only the gold half ever checked anything.
TEST(Chest, IsDeterministicPerFloor) {
  auto opened = [](uint64_t seed, int floor) {
    RunState run = RunState::start(seed);
    run.floor = floor;
    run.open_chest();
    return std::pair<int, int>{static_cast<int>(run.relics.back().id), run.gold};
  };
  EXPECT_EQ(opened(5, 9), opened(5, 9));
  EXPECT_EQ(opened(6, 10), opened(6, 10));
}

// --------------------------------------------------- Bottled relics (§6.23)

int count_bottled(const RunState& run, Bottle b) {
  int n = 0;
  for (const Card& c : run.master_deck) {
    if (c.bottle == b) ++n;
  }
  return n;
}

// canSpawn: a NON-basic Attack or Skill, or any Power. The starter deck has
// none of the three.
TEST(Bottles, NoBottleCanSpawnOnAStarterDeck) {
  const RunState run = RunState::start(1);
  EXPECT_FALSE(run.relic_can_spawn(RelicId::BottledFlame));
  EXPECT_FALSE(run.relic_can_spawn(RelicId::BottledLightning));
  EXPECT_FALSE(run.relic_can_spawn(RelicId::BottledTornado));
}

TEST(Bottles, EachBottleSpawnsOnceItsCardTypeIsInTheDeck) {
  RunState run = RunState::start(1);
  run.add_card(Card{CardId::Anger});
  EXPECT_TRUE(run.relic_can_spawn(RelicId::BottledFlame));
  EXPECT_FALSE(run.relic_can_spawn(RelicId::BottledLightning));

  run.add_card(Card{CardId::ShrugItOff});
  EXPECT_TRUE(run.relic_can_spawn(RelicId::BottledLightning));

  EXPECT_FALSE(run.relic_can_spawn(RelicId::BottledTornado));
  run.add_card(Card{CardId::Inflame});
  EXPECT_TRUE(run.relic_can_spawn(RelicId::BottledTornado));
}

// random_relic honours the gate. Every other Uncommon is excluded, so the only
// candidates left are the Bottles, which a starter deck cannot spawn — the
// draw must come back empty whatever the RNG does.
TEST(Bottles, RandomRelicSkipsABottleThatCannotSpawn) {
  const RunState run = RunState::start(1);
  std::vector<RelicId> exclude;
  for (RelicId id : relic_pool(RelicTier::Uncommon)) {
    if (id != RelicId::BottledFlame && id != RelicId::BottledLightning &&
        id != RelicId::BottledTornado) {
      exclude.push_back(id);
    }
  }
  std::mt19937 rng(1);
  EXPECT_FALSE(run.random_relic(RelicTier::Uncommon, rng, exclude).has_value());
}

// Pickup opens the choice over EVERY card of the type, starters included.
TEST(Bottles, PickingUpBottledFlameOpensAChoiceOverTheDecksAttacks) {
  RunState run = RunState::start(1);
  run.add_card(Card{CardId::Anger});

  run.obtain_relic(RelicId::BottledFlame);

  ASSERT_TRUE(run.has_pending_deck_choice());
  std::vector<CardId> expected = {CardId::Strike, CardId::Bash, CardId::Anger};
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(run.deck_choice_candidates(), expected);
}

TEST(Bottles, ResolvingBottlesExactlyOneCopy) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::BottledFlame);

  EXPECT_FALSE(run.resolve_deck_choice(CardId::Defend)) << "not an Attack";
  EXPECT_TRUE(run.has_pending_deck_choice());

  EXPECT_TRUE(run.resolve_deck_choice(CardId::Strike));
  EXPECT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(count_bottled(run, Bottle::Flame), 1);
}

// The marker is on the instance, so an upgrade — an id swap — keeps it.
TEST(Bottles, ABottledCardStaysBottledWhenUpgraded) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::BottledFlame);
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Bash));

  for (Card& c : run.master_deck) {
    if (c.bottle == Bottle::Flame) upgrade_card_in_place(c);
  }
  for (const Card& c : run.master_deck) {
    if (c.bottle == Bottle::Flame) EXPECT_EQ(c.card_id, CardId::BashPlus);
  }
}

TEST(Bottles, AnOpenChoiceBlocksOtherRunActions) {
  RunState run = RunState::start(1);
  run.phase = Phase::Reward;
  run.card_reward = {Card{CardId::Anger}};
  run.obtain_relic(RelicId::BottledFlame);
  const size_t deck_before = run.master_deck.size();

  run.take_card_reward(0);

  EXPECT_EQ(run.master_deck.size(), deck_before);
  EXPECT_EQ(run.card_reward.size(), 1u) << "the reward screen moved on";
}

// Two Bottles from one room wait in acquisition order, each asking for its
// own card type.
TEST(Bottles, TwoBottlesAreAskedInTurn) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::BottledFlame);
  run.obtain_relic(RelicId::BottledLightning);
  ASSERT_EQ(run.pending_deck_choices.size(), 2u);

  ASSERT_TRUE(run.resolve_deck_choice(CardId::Strike));
  ASSERT_TRUE(run.has_pending_deck_choice());
  EXPECT_EQ(run.deck_choice_candidates(), std::vector<CardId>{CardId::Defend})
      << "the second choice should ask for Skills";

  ASSERT_TRUE(run.resolve_deck_choice(CardId::Defend));
  EXPECT_EQ(count_bottled(run, Bottle::Flame), 1);
  EXPECT_EQ(count_bottled(run, Bottle::Lightning), 1);
}

// A room does not exit under an open choice: a chest that paid a Bottle
// leaves only once the card is chosen.
TEST(Bottles, AChestWaitsForTheChoiceBeforeLeaving) {
  RunState run = RunState::start(1);
  run.phase = Phase::Treasure;
  run.obtain_relic(RelicId::BottledFlame);

  run.leave_room();
  EXPECT_EQ(run.phase, Phase::Treasure) << "left with the choice still open";

  ASSERT_TRUE(run.resolve_deck_choice(CardId::Strike));
  EXPECT_EQ(run.phase, Phase::Map);
}

TEST(Bottles, ABottledCardCannotBeRemoved) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::BottledFlame);
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Bash));
  int bash = -1;
  for (int i = 0; i < static_cast<int>(run.master_deck.size()); ++i) {
    if (run.master_deck[i].bottle == Bottle::Flame) bash = i;
  }
  ASSERT_GE(bash, 0);
  const size_t before = run.master_deck.size();

  EXPECT_FALSE(run.remove_card_from_deck(bash));
  EXPECT_EQ(run.master_deck.size(), before);
}

// ------------------------------------------------------ Matryoshka (§6.20)

TEST(Chest, MatryoshkaStartsWithTwoCharges) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Matryoshka);
  EXPECT_EQ(find_relic(run, RelicId::Matryoshka)->counter, kMatryoshkaCharges);
}

// Two relics from one chest, Matryoshka's collected FIRST (it fires before the
// chest adds its own), and one charge spent.
TEST(Chest, MatryoshkaAddsASecondRelicToTheNextChest) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Matryoshka);
  run.floor = 9;
  const size_t before = run.relics.size();

  run.open_chest();

  ASSERT_EQ(run.relics.size(), before + 2) << "a Matryoshka chest pays twice";
  const RelicTier extra = relic_tier(run.relics[before].id);
  EXPECT_TRUE(extra == RelicTier::Common || extra == RelicTier::Uncommon)
      << "Matryoshka's relic is only ever Common or Uncommon";
  EXPECT_EQ(find_relic(run, RelicId::Matryoshka)->counter, 1);
}

// The CHEST's own roll is identical whether or not a charge is spent, because
// Matryoshka draws after it. The baseline holds a SPENT Matryoshka rather than
// none: held relics are excluded from every draw, so dropping Matryoshka from
// the run would change the chest's candidate list and prove nothing.
TEST(Chest, MatryoshkaDoesNotChangeWhatTheChestItselfRolls) {
  for (uint64_t seed = 0; seed < 20; ++seed) {
    RunState charged = RunState::start(seed);
    charged.obtain_relic(RelicId::Matryoshka);
    RunState spent = charged;
    spent.held_relic(RelicId::Matryoshka)->counter = kMatryoshkaUsedUp;
    charged.floor = spent.floor = 9;

    charged.open_chest();
    spent.open_chest();

    ASSERT_EQ(spent.relics.size() + 1, charged.relics.size());
    EXPECT_EQ(charged.relics.back().id, spent.relics.back().id)
        << "seed " << seed << ": the chest's own relic moved";
    EXPECT_EQ(charged.gold, spent.gold) << "seed " << seed;
  }
}

TEST(Chest, MatryoshkaIsSpentAfterTwoChests) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Matryoshka);

  run.floor = 9;
  run.open_chest();
  run.floor = 10;
  run.open_chest();
  EXPECT_EQ(find_relic(run, RelicId::Matryoshka)->counter, kMatryoshkaUsedUp);

  const size_t before = run.relics.size();
  run.floor = 11;
  run.open_chest();
  EXPECT_EQ(run.relics.size(), before + 1) << "a third chest paid twice";
  EXPECT_EQ(find_relic(run, RelicId::Matryoshka)->counter, kMatryoshkaUsedUp);
}

// ----------------------------------------------------------- potion drops

TEST(PotionDrops, SomeFightsDropPotionsAndSomeDoNot) {
  int drops = 0;
  for (uint64_t s = 0; s < 200; ++s) {
    RunState run = RunState::start(s);
    run.begin_combat(EncounterPool::Weak);
    run.end_combat();
    if (!run.potions.empty()) ++drops;
  }
  EXPECT_GT(drops, 0);
  EXPECT_LT(drops, 200);
}

// The drift is SYMMETRIC — it falls after a drop. Modelling it as a one-way
// pity counter would make potions far too common.
TEST(PotionDrops, TheChanceFallsAfterADropAndRisesAfterAMiss) {
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 0;

  // Force a miss: nothing can drop at 0% less the bonus.
  run.potion_chance_bonus = -40;
  const int before_miss = run.potion_chance_bonus;
  run.roll_potion_drop(0);
  EXPECT_GT(run.potion_chance_bonus, before_miss) << "a miss did not raise the chance";

  // Force a hit: guaranteed at 100%.
  RunState hit = RunState::start(1);
  hit.potion_chance_bonus = 60;  // 40 + 60 = 100
  hit.roll_potion_drop(0);
  ASSERT_EQ(hit.potions.size(), 1u);
  EXPECT_LT(hit.potion_chance_bonus, 60) << "a drop did not lower the chance";
}

// Potions are coupled to the other rewards: a screen already holding four
// suppresses the drop entirely.
TEST(PotionDrops, AFullRewardScreenSuppressesTheDrop) {
  RunState run = RunState::start(1);
  run.potion_chance_bonus = 60;  // would otherwise be guaranteed
  run.roll_potion_drop(4);
  EXPECT_TRUE(run.potions.empty());
}

TEST(PotionDrops, WhiteBeastStatueGuaranteesADrop) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::WhiteBeastStatue);
  run.potion_chance_bonus = -40;  // would otherwise never drop
  run.roll_potion_drop(0);
  EXPECT_EQ(run.potions.size(), 1u);
}

// ----------------------------------------------- run-layer relics (batch 2b)
//
// Maw Bank, Meal Ticket, Ceramic Fish, Old Coin and Tiny House. None is a
// combat trigger (§3.2): they fire on entering a room, on a card joining the
// master deck, or the moment the relic is taken.

TEST(RunLayerRelics, MawBankPaysOnEnteringARoom) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MawBank);
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.gold, kMawBankGold);
}

// onEnterRoom has no room check. Written as a shop effect it would pass a
// shop-only test and be wrong in every other room.
TEST(RunLayerRelics, MawBankPaysOnEveryRoomNotJustShops) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MawBank);
  run.enter_room(RoomType::Rest);
  run.enter_room(RoomType::Shop);
  EXPECT_EQ(run.gold, 2 * kMawBankGold);
}

TEST(RunLayerRelics, MawBankStopsForeverOnceGoldIsSpent) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MawBank);
  run.enter_room(RoomType::Rest);
  ASSERT_EQ(run.gold, kMawBankGold);

  run.spend_gold(5);
  const int after_spending = run.gold;
  run.enter_room(RoomType::Rest);
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.gold, after_spending) << "the bank paid again after a spend";

  const HeldRelic* maw = find_relic(run, RelicId::MawBank);
  ASSERT_NE(maw, nullptr);
  EXPECT_EQ(maw->counter, kMawBankUsedUp);
}

TEST(RunLayerRelics, AZeroGoldPaymentDoesNotUseUpMawBank) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MawBank);
  run.spend_gold(0);
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.gold, kMawBankGold);
}

// Through the SHOP, not a direct spend_gold call. The four buy_* paths are the
// reason the spend funnel exists, so one of them has to be exercised here.
TEST(RunLayerRelics, BuyingFromAShopUsesUpMawBank) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MawBank);
  run.gold = 500;
  run.enter_room(RoomType::Shop);
  ASSERT_FALSE(run.shop_cards.empty());
  run.buy_card(0);

  const HeldRelic* maw = find_relic(run, RelicId::MawBank);
  ASSERT_NE(maw, nullptr);
  EXPECT_EQ(maw->counter, kMawBankUsedUp);
}

TEST(RunLayerRelics, MealTicketHealsFifteenOnEnteringAShop) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MealTicket);
  run.hp = 40;
  run.enter_room(RoomType::Shop);
  EXPECT_EQ(run.hp, 40 + kMealTicketHeal);
}

TEST(RunLayerRelics, MealTicketDoesNothingOutsideAShop) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MealTicket);
  run.hp = 40;
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.hp, 40);
}

TEST(RunLayerRelics, MealTicketCannotHealAboveMaxHp) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MealTicket);
  run.hp = run.max_hp - 3;
  run.enter_room(RoomType::Shop);
  EXPECT_EQ(run.hp, run.max_hp);
}

// Armed on entering a rest site; a second one before the next fight arms it
// again rather than stacking; and a non-rest room leaves it armed.
TEST(RunLayerRelics, AncientTeaSetArmsAtARestSiteAndDoesNotStack) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::AncientTeaSet);
  EXPECT_NE(find_relic(run, RelicId::AncientTeaSet)->counter,
            kAncientTeaSetArmed);

  run.enter_room(RoomType::Rest);
  EXPECT_EQ(find_relic(run, RelicId::AncientTeaSet)->counter,
            kAncientTeaSetArmed);
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(find_relic(run, RelicId::AncientTeaSet)->counter,
            kAncientTeaSetArmed);
  run.enter_room(RoomType::Shop);
  EXPECT_EQ(find_relic(run, RelicId::AncientTeaSet)->counter,
            kAncientTeaSetArmed)
      << "the charge did not survive a room that was not a fight";
}

// End to end through the run layer: armed at a rest site, spent by the next
// fight's first turn, and disarmed in the state the fight writes back.
TEST(RunLayerRelics, AncientTeaSetIsSpentByTheNextFight) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::AncientTeaSet);
  run.enter_room(RoomType::Rest);

  RunState control = RunState::start(1);
  control.obtain_relic(RelicId::AncientTeaSet);

  run.floor = control.floor = 3;
  run.begin_combat(EncounterPool::Weak);
  control.begin_combat(EncounterPool::Weak);

  EXPECT_EQ(run.combat.character.energy,
            control.combat.character.energy + kAncientTeaSetEnergy);
  for (const HeldRelic& r : run.combat.relics) {
    if (r.id == RelicId::AncientTeaSet) EXPECT_NE(r.counter, kAncientTeaSetArmed);
  }
}

TEST(RunLayerRelics, CeramicFishPaysNineForEachCardAdded) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::CeramicFish);
  const int before = run.gold;
  run.add_card(Card{CardId::Strike});
  run.add_card(Card{CardId::Defend});
  EXPECT_EQ(run.gold, before + 2 * kCeramicFishGold);
}

// add_card deals the starter deck too, so a relic held at that moment would pay
// ten times. Ceramic Fish cannot be held on floor 0 — asserted because the
// alternative is a 90-gold head start nothing else would notice.
TEST(RunLayerRelics, ARunStartsWithNoGold) {
  RunState run = RunState::start(1);
  EXPECT_EQ(run.gold, 0);
}

// ------------------------------------------------------------ the Eggs (§6.19)

// is_upgraded reads the upgrade graph. Searing Blow's ladder is the case that
// separates it from !is_upgradable: its middle rungs are both.
TEST(CardUpgrades, IsUpgradedReadsTheUpgradeGraph) {
  EXPECT_FALSE(is_upgraded(CardId::Strike));
  EXPECT_TRUE(is_upgraded(CardId::StrikePlus));
  EXPECT_FALSE(is_upgraded(CardId::SearingBlow));
  EXPECT_TRUE(is_upgraded(CardId::SearingBlowPlus));
  EXPECT_TRUE(is_upgradable(CardId::SearingBlowPlus));
  EXPECT_TRUE(is_upgraded(CardId::SearingBlow5));
  EXPECT_FALSE(is_upgraded(CardId::Slimed));
}

struct EggCase {
  RelicId relic;
  CardType type;
};
constexpr EggCase kEggs[] = {
    {RelicId::MoltenEgg, CardType::Attack},
    {RelicId::ToxicEgg, CardType::Skill},
    {RelicId::FrozenEgg, CardType::Power},
};

// Exhaustive over the card database, so no card type or edge case is left to
// whichever cards a test happened to name: `type matches && canUpgrade() &&
// !upgraded` (decompiled *Egg2.onObtainCard), and otherwise untouched.
TEST(EggRelics, EachEggUpgradesExactlyItsOwnTypeAcrossTheWholeDatabase) {
  for (const EggCase& egg : kEggs) {
    RunState run = RunState::start(1);
    run.obtain_relic(egg.relic);

    for (const auto& [id, data] : CARD_DATABASE) {
      Card card{id};
      run.apply_egg_upgrades(card);

      const bool eligible =
          data.type == egg.type && is_upgradable(id) && !is_upgraded(id);
      Card expected{id};
      if (eligible) upgrade_card_in_place(expected);
      EXPECT_TRUE(card.same_as(expected))
          << "egg " << static_cast<int>(egg.relic) << " on card "
          << static_cast<int>(id) << (eligible ? " (eligible)" : " (not eligible)");
    }
  }
}

// The one case `!upgraded` exists for: an upgraded Searing Blow is still
// upgradable, and the Egg must leave it alone.
TEST(EggRelics, MoltenEggLeavesAnAlreadyUpgradedSearingBlowAlone) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MoltenEgg);

  Card base{CardId::SearingBlow};
  run.apply_egg_upgrades(base);
  EXPECT_EQ(base.card_id, CardId::SearingBlowPlus);

  Card plus{CardId::SearingBlowPlus};
  run.apply_egg_upgrades(plus);
  EXPECT_EQ(plus.card_id, CardId::SearingBlowPlus)
      << "the Egg upgraded a card that was already upgraded";
}

TEST(EggRelics, AddingAMatchingCardToTheDeckUpgradesIt) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MoltenEgg);

  run.add_card(Card{CardId::Strike});
  EXPECT_EQ(run.master_deck.back().card_id, CardId::StrikePlus);

  run.add_card(Card{CardId::Defend});
  EXPECT_EQ(run.master_deck.back().card_id, CardId::Defend)
      << "Molten Egg upgraded a Skill";
}

// onEquip: the reward already on screen is upgraded the moment the Egg is taken.
TEST(EggRelics, PickingUpAnEggUpgradesThePendingCardReward) {
  RunState run = RunState::start(1);
  run.card_reward = {Card{CardId::Strike}, Card{CardId::Defend},
                     Card{CardId::Inflame}};

  run.obtain_relic(RelicId::FrozenEgg);

  EXPECT_EQ(run.card_reward[0].card_id, CardId::Strike);
  EXPECT_EQ(run.card_reward[1].card_id, CardId::Defend);
  EXPECT_EQ(run.card_reward[2].card_id, CardId::InflamePlus);
}

// StoreRelic.purchaseRelic: buying an Egg upgrades the shelf in front of you.
TEST(EggRelics, TakingAnEggInAShopUpgradesTheShelf) {
  RunState run = RunState::start(1);
  run.phase = Phase::Shop;
  ShopItem strike;
  strike.card = Card{CardId::Strike};
  ShopItem defend;
  defend.card = Card{CardId::Defend};
  run.shop_cards = {strike, defend};

  run.obtain_relic(RelicId::ToxicEgg);

  EXPECT_EQ(run.shop_cards[0].card.card_id, CardId::Strike);
  EXPECT_EQ(run.shop_cards[1].card.card_id, CardId::DefendPlus);
}

// The preview passes, as invariants over every draw: with all three Eggs held,
// no offer can still be an unupgraded upgradable card, whatever was rolled.
//
// The distinct-ids assertion guards the ordering: previewing INSIDE the draw
// loop would let an offered Bash+ fail to match a later Bash, and the reward
// would show Bash+ twice.
TEST(EggRelics, EveryCardRewardOfferIsPreviewed) {
  RunState run = RunState::start(1);
  for (const EggCase& egg : kEggs) run.obtain_relic(egg.relic);

  for (int floor = 1; floor <= 15; ++floor) {
    run.floor = floor;
    run.generate_card_reward(RewardSource::Monster);
    std::set<CardId> seen;
    for (const Card& offer : run.card_reward) {
      EXPECT_FALSE(is_upgradable(offer.card_id) && !is_upgraded(offer.card_id))
          << "an offer was shown unupgraded: " << static_cast<int>(offer.card_id);
      EXPECT_TRUE(seen.insert(offer.card_id).second)
          << "the same card was offered twice";
    }
  }
}

TEST(EggRelics, EveryShopCardIsPreviewed) {
  RunState run = RunState::start(1);
  for (const EggCase& egg : kEggs) run.obtain_relic(egg.relic);

  for (int floor = 1; floor <= 15; ++floor) {
    run.floor = floor;
    run.generate_shop();
    std::set<CardId> seen;
    for (const ShopItem& item : run.shop_cards) {
      const CardId id = item.card.card_id;
      EXPECT_FALSE(is_upgradable(id) && !is_upgraded(id))
          << "a shop card was shown unupgraded: " << static_cast<int>(id);
      EXPECT_TRUE(seen.insert(id).second) << "the shelf stocked a card twice";
    }
  }
}

TEST(RunLayerRelics, OldCoinPaysThreeHundredOnPickup) {
  RunState run = RunState::start(1);
  ASSERT_EQ(run.gold, 0);
  run.obtain_relic(RelicId::OldCoin);
  EXPECT_EQ(run.gold, kOldCoinGold);
}

// Old Coin's payout goes through gain_gold like every other gain, so the run
// that can no longer gain gold gains none of it.
TEST(RunLayerRelics, EctoplasmRefusesOldCoinsGold) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Ectoplasm);
  run.obtain_relic(RelicId::OldCoin);
  EXPECT_EQ(run.gold, 0);
}

TEST(RunLayerRelics, EctoplasmRefusesMawBanksGold) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::Ectoplasm);
  run.obtain_relic(RelicId::MawBank);
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.gold, 0);
}

TEST(RunLayerRelics, TinyHousePaysGoldMaxHpAndAPotion) {
  RunState run = RunState::start(1);
  const int max_before = run.max_hp;
  const size_t deck_before = run.master_deck.size();

  run.obtain_relic(RelicId::TinyHouse);

  EXPECT_EQ(run.gold, kTinyHouseGold);
  EXPECT_EQ(run.max_hp, max_before + kTinyHouseMaxHp);
  EXPECT_EQ(run.potions.size(), 1u);
  // The deck does not grow — a statement about THIS engine, not about StS.
  // Tiny House really does give a card reward (onEquip -> combatRewardScreen
  // .open -> setupItemReward), and that half is unimplemented because it needs
  // a choice screen (§7). Pinned so the gap stays visible; INVERT this when the
  // card reward lands.
  EXPECT_EQ(run.master_deck.size(), deck_before);
}

TEST(RunLayerRelics, TinyHouseUpgradesExactlyOneCard) {
  RunState run = RunState::start(1);
  std::vector<CardId> before;
  for (const Card& c : run.master_deck) before.push_back(c.card_id);

  run.obtain_relic(RelicId::TinyHouse);

  ASSERT_EQ(run.master_deck.size(), before.size());
  int changed = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    if (run.master_deck[i].card_id != before[i]) ++changed;
  }
  // One, not two: the decompiled else-branch checks two bottled cards but
  // upgrades only index 0.
  EXPECT_EQ(changed, 1);
}

// ------------------------------------------------------- Eternal Feather

TEST(RunLayerRelics, EternalFeatherHealsThreePerFiveCardsAtARestSite) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::EternalFeather);
  ASSERT_EQ(run.master_deck.size(), 10u) << "the Ironclad starter deck";
  run.hp = 40;
  run.enter_room(RoomType::Rest);
  // 10 / 5 * 3 = 6.
  EXPECT_EQ(run.hp, 46);
}

// Integer division, not a rate. The decompiled source is
// `masterDeck.size() / 5 * 3`, so the cards past the last full five pay
// nothing — 14 cards heal 6, the same as 10.
TEST(RunLayerRelics, EternalFeatherTruncatesThePartialFive) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::EternalFeather);
  for (int i = 0; i < 4; ++i) run.add_card(Card{CardId::Strike});
  ASSERT_EQ(run.master_deck.size(), 14u);
  run.hp = 40;
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.hp, 46) << "14 / 5 = 2, so 6 — not 8";
}

TEST(RunLayerRelics, EternalFeatherDoesNothingOutsideARestSite) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::EternalFeather);
  run.hp = 40;
  run.enter_room(RoomType::Shop);
  EXPECT_EQ(run.hp, 40);
}

TEST(RunLayerRelics, EternalFeatherCannotHealAboveMaxHp) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::EternalFeather);
  run.hp = run.max_hp - 2;
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.hp, run.max_hp);
}

// The property this batch's design doc got wrong once. Magic Flower boosts
// healing DURING COMBAT only, so a rest site's heal is untouched by it. If this
// ever heals 9 rather than 6, the modifier has leaked out of the combat layer.
TEST(RunLayerRelics, MagicFlowerDoesNotBoostEternalFeather) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::EternalFeather);
  run.obtain_relic(RelicId::MagicFlower);
  run.hp = 40;
  run.enter_room(RoomType::Rest);
  EXPECT_EQ(run.hp, 46);
}

// Same point from the other side: the run layer's other heals are untouched.
TEST(RunLayerRelics, MagicFlowerDoesNotBoostMealTicket) {
  RunState run = RunState::start(1);
  run.obtain_relic(RelicId::MealTicket);
  run.obtain_relic(RelicId::MagicFlower);
  run.hp = 40;
  run.enter_room(RoomType::Shop);
  EXPECT_EQ(run.hp, 40 + kMealTicketHeal);
}

// ------------------------------------------------------- Prismatic Shard

namespace {

bool in_pool(const std::vector<CardId>& pool, CardId id) {
  for (CardId c : pool) {
    if (c == id) return true;
  }
  return false;
}

}  // namespace

// A BOSS reward bypasses the rarity roll and is always Rare (§4.2). That makes
// the POOL deterministic even though the draw is not, which is the handle these
// assertions need — they state what is true of every draw, never what one seed
// produces.
TEST(RewardRelics, WithoutPrismaticShardRewardsAreIroncladOnly) {
  for (uint64_t seed = 0; seed < 40; ++seed) {
    RunState run = RunState::start(seed);
    run.generate_card_reward(RewardSource::Boss);
    ASSERT_FALSE(run.card_reward.empty()) << "seed " << seed;
    for (const Card& c : run.card_reward) {
      EXPECT_TRUE(in_pool(IRONCLAD_RARE_POOL, c.card_id))
          << card_name(c.card_id) << " is not an Ironclad rare (seed " << seed
          << ")";
    }
  }
}

// The widening, proved DIRECTLY on the pool function. reward_pool is pure, so
// membership is exhaustive: no seed, no probability, no platform.
//
// This replaces a version that drew rewards across 40 seeds and asserted some
// colorless card turned up, defended as "a property of the pool's composition".
// That was still a probabilistic argument about a fact the RNG has nothing to do
// with. The rule is absolute: unless the randomness IS the subject, a test must
// not depend on it.
TEST(RewardRelics, PrismaticShardWidensTheRarePool) {
  const std::vector<CardId> without = reward_pool(CardRarity::Rare, false);
  const std::vector<CardId> with = reward_pool(CardRarity::Rare, true);

  EXPECT_EQ(without, IRONCLAD_RARE_POOL) << "the unheld pool must be untouched";
  ASSERT_EQ(with.size(),
            IRONCLAD_RARE_POOL.size() + COLORLESS_RARE_POOL.size());
  for (CardId id : IRONCLAD_RARE_POOL) {
    EXPECT_TRUE(in_pool(with, id)) << card_name(id) << " was dropped";
  }
  for (CardId id : COLORLESS_RARE_POOL) {
    EXPECT_TRUE(in_pool(with, id)) << card_name(id) << " was not added";
  }
}

TEST(RewardRelics, PrismaticShardWidensTheUncommonPool) {
  const std::vector<CardId> without = reward_pool(CardRarity::Uncommon, false);
  const std::vector<CardId> with = reward_pool(CardRarity::Uncommon, true);

  EXPECT_EQ(without, IRONCLAD_UNCOMMON_POOL);
  ASSERT_EQ(with.size(),
            IRONCLAD_UNCOMMON_POOL.size() + COLORLESS_UNCOMMON_POOL.size());
  for (CardId id : COLORLESS_UNCOMMON_POOL) {
    EXPECT_TRUE(in_pool(with, id)) << card_name(id) << " was not added";
  }
}

// Colorless has no common tier, so a Common roll is untouched even while the
// relic is held (§6.16). Real StS widens Common too, across the other
// characters' cards; ours cannot, which is a consequence of §6.8's divergence
// rather than a separate decision.
TEST(RewardRelics, PrismaticShardLeavesTheCommonPoolAlone) {
  EXPECT_EQ(reward_pool(CardRarity::Common, true), IRONCLAD_COMMON_POOL);
  EXPECT_EQ(reward_pool(CardRarity::Common, false), IRONCLAD_COMMON_POOL);
}

// Appended, not reordered: an Ironclad id keeps its index, so a run without the
// relic draws exactly what it drew before the relic existed. Asserted rather
// than inferred from the suite staying green.
TEST(RewardRelics, PrismaticShardAppendsWithoutReordering) {
  const std::vector<CardId> with = reward_pool(CardRarity::Rare, true);
  ASSERT_GE(with.size(), IRONCLAD_RARE_POOL.size());
  for (size_t i = 0; i < IRONCLAD_RARE_POOL.size(); ++i) {
    EXPECT_EQ(with[i], IRONCLAD_RARE_POOL[i]) << "index " << i;
  }
}

// Integration: every card a reward offers stays inside the widened pool. An
// invariant true of every draw, so no seed and no standard library can make it
// flaky.
TEST(RewardRelics, PrismaticShardRewardsStayInsideIroncladPlusColorless) {
  for (uint64_t seed = 0; seed < 40; ++seed) {
    RunState run = RunState::start(seed);
    run.obtain_relic(RelicId::PrismaticShard);
    run.generate_card_reward(RewardSource::Boss);
    ASSERT_FALSE(run.card_reward.empty()) << "seed " << seed;
    for (const Card& c : run.card_reward) {
      EXPECT_TRUE(in_pool(IRONCLAD_RARE_POOL, c.card_id) ||
                  in_pool(COLORLESS_RARE_POOL, c.card_id))
          << card_name(c.card_id) << " is outside Ironclad + Colorless (seed "
          << seed << ")";
    }
  }
}

// Prismatic Shard does not touch SHOP stock. StS puts the check inside the
// reward loop only, and the relic appears in three files of the decompiled game,
// none of them shop generation. This is why reward_pool is a sibling of pool_of
// rather than a change to it — modifying pool_of would have leaked colorless
// into the shop's typed slots, and nothing else would have noticed.
TEST(RewardRelics, PrismaticShardDoesNotChangeTheShop) {
  RunState bare = RunState::start(1);
  bare.floor = 5;
  bare.phase = Phase::Shop;
  bare.generate_shop();

  RunState with = RunState::start(1);
  with.obtain_relic(RelicId::PrismaticShard);
  with.floor = 5;
  with.phase = Phase::Shop;
  with.generate_shop();

  ASSERT_EQ(with.shop_cards.size(), bare.shop_cards.size());
  for (size_t i = 0; i < bare.shop_cards.size(); ++i) {
    EXPECT_EQ(with.shop_cards[i].card.card_id, bare.shop_cards[i].card.card_id)
        << "shop slot " << i << " changed";
  }
}

// --------------------------- Empty Cage, Astrolabe, Pandora's Box (§6.30)
//
// Decks are built through add_card, so every card has a real uid. Nothing here
// depends on which card a transform draws: each assertion holds for any draw.

RunState with_deck(std::vector<CardId> ids, std::vector<RelicId> relics = {}) {
  RunState run = RunState::start(1);
  run.master_deck.clear();
  for (CardId id : ids) run.add_card(Card{id});
  for (RelicId r : relics) run.obtain_relic(r);
  run.phase = Phase::Map;
  return run;
}

int count_id(const RunState& run, CardId id) {
  return static_cast<int>(std::count_if(
      run.master_deck.begin(), run.master_deck.end(),
      [id](const Card& c) { return c.card_id == id; }));
}

// Every upgraded class card a transform with an upgrade can produce.
std::set<CardId> upgraded_class_cards() {
  std::set<CardId> out;
  for (CardId id : class_card_pool()) out.insert(upgraded_card(id));
  return out;
}

TEST(DeckChange, PurgeableExcludesCurseOfTheBellAndKeepsBottledCards) {
  RunState run = with_deck({CardId::Strike, CardId::CurseOfTheBell, CardId::Bash});
  run.master_deck[2].bottle = Bottle::Flame;
  EXPECT_EQ(run.purgeable_card_indices(), (std::vector<int>{0, 2}));
}

TEST(DeckChange, TransformPoolsFollowTransformCard) {
  // A class card: the three class pools, never itself, compared by base id.
  std::vector<CardId> without_anger = class_card_pool();
  without_anger.erase(std::find(without_anger.begin(), without_anger.end(),
                                CardId::Anger));
  EXPECT_EQ(transform_pool(CardId::Anger), without_anger);
  EXPECT_EQ(transform_pool(CardId::AngerPlus), without_anger);
  // A starter is in none of the pools, so nothing is taken out.
  EXPECT_EQ(transform_pool(CardId::Strike), class_card_pool());
  EXPECT_EQ(transform_pool(CardId::StrikePlus), class_card_pool());
  // Searing Blow's rungs chain back to the base card.
  const std::vector<CardId> from_blow = transform_pool(CardId::SearingBlow3);
  EXPECT_EQ(std::count(from_blow.begin(), from_blow.end(), CardId::SearingBlow), 0);
  EXPECT_EQ(from_blow.size(), class_card_pool().size() - 1);
  // A colorless card: the colorless pool, healing included, never itself.
  const std::vector<CardId> colorless = transform_pool(CardId::Apotheosis);
  EXPECT_EQ(std::count(colorless.begin(), colorless.end(), CardId::Apotheosis), 0);
  EXPECT_EQ(std::count(colorless.begin(), colorless.end(), CardId::BandageUp), 1);
  EXPECT_EQ(colorless.size(),
            COLORLESS_UNCOMMON_POOL.size() + COLORLESS_RARE_POOL.size() - 1);
  // A curse: the other curses, never Curse of the Bell.
  std::vector<CardId> curses = CURSE_POOL;
  curses.erase(std::find(curses.begin(), curses.end(), CardId::Regret));
  EXPECT_EQ(transform_pool(CardId::Regret), curses);
}

TEST(DeckChange, EmptyCageRemovesTwoPicksTogether) {
  RunState run = with_deck({CardId::Strike, CardId::Strike, CardId::Defend,
                            CardId::Bash, CardId::CurseOfTheBell});
  run.obtain_relic(RelicId::EmptyCage);
  ASSERT_TRUE(run.has_pending_deck_choice());
  EXPECT_EQ(run.deck_choice_candidates(),
            (std::vector<CardId>{CardId::Strike, CardId::Defend, CardId::Bash}))
      << "Curse of the Bell is not purgeable";

  ASSERT_TRUE(run.resolve_deck_choice(CardId::Strike));
  EXPECT_EQ(run.master_deck.size(), 5u) << "nothing leaves until both picks";
  EXPECT_EQ(count_id(run, CardId::Strike), 2);

  ASSERT_TRUE(run.resolve_deck_choice(CardId::Bash));
  EXPECT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(count_id(run, CardId::Strike), 1);
  EXPECT_EQ(count_id(run, CardId::Bash), 0);
  EXPECT_EQ(run.master_deck.size(), 3u);
}

TEST(DeckChange, EmptyCageWithTwoPurgeableRemovesBothAtOnce) {
  // Parasite costs 3 Max HP on the way out; Curse of the Bell stays.
  RunState run = with_deck(
      {CardId::Parasite, CardId::Strike, CardId::CurseOfTheBell});
  const int max_hp = run.max_hp;
  run.obtain_relic(RelicId::EmptyCage);
  EXPECT_FALSE(run.has_pending_deck_choice());
  ASSERT_EQ(run.master_deck.size(), 1u);
  EXPECT_EQ(run.master_deck[0].card_id, CardId::CurseOfTheBell);
  EXPECT_EQ(run.max_hp, max_hp - 3);
}

TEST(DeckChange, APickedCopyIsNotOfferedAgainAndNothingChangesUntilTheLast) {
  RunState run = with_deck(
      {CardId::Strike, CardId::Defend, CardId::Bash, CardId::Anger});
  run.obtain_relic(RelicId::Astrolabe);
  ASSERT_TRUE(run.has_pending_deck_choice());
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Bash));
  const std::vector<CardId> left = run.deck_choice_candidates();
  EXPECT_EQ(std::count(left.begin(), left.end(), CardId::Bash), 0);
  EXPECT_FALSE(run.resolve_deck_choice(CardId::Bash));
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Anger));
  EXPECT_EQ(count_id(run, CardId::Bash), 1) << "transformed before the last pick";
  EXPECT_EQ(count_id(run, CardId::Anger), 1);
  // While the choice is open, other run actions refuse.
  run.potions = {PotionId::FirePotion};
  run.discard_potion(0);
  EXPECT_EQ(run.potions.size(), 1u);
}

TEST(DeckChange, AstrolabeTransformsThreeIntoUpgradedClassCards) {
  // Ceramic Fish pays for each card obtained (wiki.gg: 27 for Astrolabe).
  RunState run = with_deck({CardId::Strike, CardId::Strike, CardId::Strike,
                            CardId::Defend, CardId::Defend, CardId::Bash},
                           {RelicId::CeramicFish});
  const int gold = run.gold;
  run.obtain_relic(RelicId::Astrolabe);
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Strike));
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Strike));
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Bash));
  EXPECT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(run.master_deck.size(), 6u);
  EXPECT_EQ(count_id(run, CardId::Strike), 1);
  EXPECT_EQ(count_id(run, CardId::Defend), 2);
  EXPECT_EQ(count_id(run, CardId::Bash), 0);
  const std::set<CardId> allowed = upgraded_class_cards();
  int transformed = 0;
  for (const Card& c : run.master_deck) {
    if (c.card_id == CardId::Strike || c.card_id == CardId::Defend) continue;
    EXPECT_EQ(allowed.count(c.card_id), 1u) << "not an upgraded class card";
    ++transformed;
  }
  EXPECT_EQ(transformed, 3);
  EXPECT_EQ(run.gold, gold + 3 * kCeramicFishGold);
}

TEST(DeckChange, AstrolabeWithThreeOrFewerPurgeableTransformsThemAtOnce) {
  RunState run = with_deck(
      {CardId::Strike, CardId::Bash, CardId::CurseOfTheBell});
  run.obtain_relic(RelicId::Astrolabe);
  EXPECT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(run.master_deck.size(), 3u);
  EXPECT_EQ(count_id(run, CardId::CurseOfTheBell), 1);
  EXPECT_EQ(count_id(run, CardId::Strike), 0);
  EXPECT_EQ(count_id(run, CardId::Bash), 0);
}

TEST(DeckChange, APickTakesAnUnbottledCopyWhenThereIsOne) {
  // Rob, §6.30 P2.
  RunState run = with_deck(
      {CardId::Strike, CardId::Strike, CardId::Defend, CardId::Bash});
  run.master_deck[0].bottle = Bottle::Flame;
  run.obtain_relic(RelicId::EmptyCage);
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Strike));
  ASSERT_TRUE(run.resolve_deck_choice(CardId::Defend));
  ASSERT_EQ(count_id(run, CardId::Strike), 1);
  for (const Card& c : run.master_deck) {
    if (c.card_id == CardId::Strike) {
      EXPECT_EQ(c.bottle, Bottle::Flame) << "the bottled copy was taken";
    }
  }

  // With the bottled copy the only one, it is purgeable and is taken.
  RunState only = with_deck({CardId::Strike, CardId::Defend, CardId::Bash});
  only.master_deck[0].bottle = Bottle::Flame;
  only.obtain_relic(RelicId::EmptyCage);
  ASSERT_TRUE(only.resolve_deck_choice(CardId::Strike));
  ASSERT_TRUE(only.resolve_deck_choice(CardId::Defend));
  EXPECT_EQ(count_id(only, CardId::Strike), 0);
}

TEST(DeckChange, PandorasBoxReplacesEveryStrikeAndDefend) {
  // Upgraded and bottled copies go too; Bash stays. As many class cards
  // arrive, none of them a starter, and Ceramic Fish pays for each.
  RunState run = with_deck(
      {CardId::Strike, CardId::StrikePlus, CardId::Strike, CardId::Defend,
       CardId::DefendPlus, CardId::Bash},
      {RelicId::CeramicFish});
  run.master_deck[3].bottle = Bottle::Lightning;
  const int gold = run.gold;
  run.obtain_relic(RelicId::PandorasBox);
  EXPECT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(run.master_deck.size(), 6u);
  EXPECT_EQ(count_id(run, CardId::Bash), 1);
  const std::vector<CardId> pool = class_card_pool();
  for (const Card& c : run.master_deck) {
    if (c.card_id == CardId::Bash) continue;
    EXPECT_EQ(std::count(pool.begin(), pool.end(), c.card_id), 1)
        << "not a class card";
    EXPECT_EQ(c.bottle, Bottle::None);
  }
  EXPECT_EQ(run.gold, gold + 5 * kCeramicFishGold);
}

// ===================================================== curse acquisition
// curse-effects.md §6. Draws are never fixtured: a test that needs a particular
// relic drawn holds every other candidate of that tier, so the draw has one
// possible answer.

// Hold every relic of `tier` except `keep`, WITHOUT their pickup effects, so a
// draw from the tier can only be one of `keep`.
void hold_all_of_tier_except(RunState& run, RelicTier tier,
                             std::vector<RelicId> keep) {
  for (RelicId id : relic_pool(tier)) {
    if (std::find(keep.begin(), keep.end(), id) != keep.end()) continue;
    if (!run.has_relic(id)) run.relics.push_back(HeldRelic{id, 0});
  }
}

int counter_of(RunState& run, RelicId id) { return run.held_relic(id)->counter; }

TEST(CurseAcquisition, OmamoriNegatesTwoCursesAndNotAThird) {
  // A negated curse never reaches onObtainCard: no Ceramic Fish gold for it.
  RunState run = with_deck({}, {RelicId::Omamori, RelicId::CeramicFish});
  ASSERT_EQ(counter_of(run, RelicId::Omamori), kOmamoriCharges);
  const int gold = run.gold;
  for (int i = 0; i < 3; ++i) run.add_card(Card{CardId::Injury});
  EXPECT_EQ(count_id(run, CardId::Injury), 1);
  EXPECT_EQ(counter_of(run, RelicId::Omamori), 0);
  EXPECT_EQ(run.gold, gold + kCeramicFishGold);
}

TEST(CurseAcquisition, OmamoriIgnoresCardsThatAreNotCurses) {
  RunState run = with_deck({}, {RelicId::Omamori});
  run.add_card(Card{CardId::Strike});
  run.add_card(Card{CardId::Wound});  // a Status is not a curse
  EXPECT_EQ(run.master_deck.size(), 2u);
  EXPECT_EQ(counter_of(run, RelicId::Omamori), kOmamoriCharges);
}

TEST(CurseAcquisition, DarkstonePeriaptGivesMaxHpAndHpPerCurse) {
  RunState run = with_deck({}, {RelicId::DarkstonePeriapt});
  run.hp = 50;
  const int max_hp = run.max_hp;
  run.add_card(Card{CardId::Injury});
  EXPECT_EQ(run.max_hp, max_hp + kDarkstoneMaxHp);
  EXPECT_EQ(run.hp, 50 + kDarkstoneMaxHp);
  run.add_card(Card{CardId::Strike});
  EXPECT_EQ(run.max_hp, max_hp + kDarkstoneMaxHp) << "a Strike is not a curse";
}

TEST(CurseAcquisition, ACurseOmamoriNegatesGivesDarkstoneNothing) {
  RunState run = with_deck({}, {RelicId::Omamori, RelicId::DarkstonePeriapt});
  const int max_hp = run.max_hp;
  run.add_card(Card{CardId::Injury});
  EXPECT_EQ(count_id(run, CardId::Injury), 0);
  EXPECT_EQ(run.max_hp, max_hp);
}

TEST(CurseAcquisition, AstrolabeTransformingACurseUnderOmamoriRemovesIt) {
  // Three purgeable cards, so Astrolabe takes all of them at once. The curse's
  // replacement is another curse, which Omamori negates (wiki.gg Omamori).
  RunState run = with_deck({CardId::Injury, CardId::Strike, CardId::Defend},
                           {RelicId::Omamori});
  run.obtain_relic(RelicId::Astrolabe);
  ASSERT_FALSE(run.has_pending_deck_choice());
  EXPECT_EQ(run.master_deck.size(), 2u);
  for (const Card& c : run.master_deck) {
    EXPECT_NE(CARD_DATABASE.at(c.card_id).type, CardType::Curse);
  }
  EXPECT_EQ(counter_of(run, RelicId::Omamori), kOmamoriCharges - 1);
}

TEST(CurseAcquisition, CursedKeyAddsACurseFromThePoolWhenAChestOpens) {
  RunState run = with_deck({}, {RelicId::CursedKey});
  run.open_chest();
  int curses = 0;
  for (const Card& c : run.master_deck) {
    if (CARD_DATABASE.at(c.card_id).type != CardType::Curse) continue;
    ++curses;
    EXPECT_EQ(std::count(CURSE_POOL.begin(), CURSE_POOL.end(), c.card_id), 1)
        << "not one of getCurse's ten";
  }
  EXPECT_EQ(curses, 1);
}

TEST(CurseAcquisition, AChestWithoutCursedKeyAddsNoCard) {
  RunState run = with_deck({}, {});
  run.open_chest();
  EXPECT_TRUE(run.master_deck.empty());
}

TEST(CurseAcquisition, OmamoriAlreadyHeldBlocksCursedKeysCurse) {
  RunState run = with_deck({}, {RelicId::Omamori, RelicId::CursedKey});
  run.open_chest();
  EXPECT_TRUE(run.master_deck.empty());
  EXPECT_EQ(counter_of(run, RelicId::Omamori), kOmamoriCharges - 1);
}

TEST(CurseAcquisition, CallingBellGivesItsCurseAndOneRelicOfEachTier) {
  RunState run = with_deck({}, {});
  const std::size_t held_before = run.relics.size();
  run.obtain_relic(RelicId::CallingBell);
  EXPECT_EQ(count_id(run, CardId::CurseOfTheBell), 1);
  ASSERT_EQ(run.relics.size(), held_before + 4);  // the Bell and three more
  std::vector<RelicTier> tiers;
  for (std::size_t i = held_before + 1; i < run.relics.size(); ++i) {
    tiers.push_back(relic_tier(run.relics[i].id));
  }
  EXPECT_EQ(tiers, (std::vector<RelicTier>{RelicTier::Common,
                                           RelicTier::Uncommon,
                                           RelicTier::Rare}));
}

TEST(CurseAcquisition, CallingBellNeverGivesARelicThatOpensAScreen) {
  // Every candidate held but the four returnRandomScreenlessRelic redraws: no
  // tier has anything left to give.
  const std::vector<RelicId> screens = {RelicId::BottledFlame,
                                        RelicId::BottledLightning,
                                        RelicId::BottledTornado,
                                        RelicId::Whetstone};
  RunState run = with_deck({}, {});
  for (RelicTier tier :
       {RelicTier::Common, RelicTier::Uncommon, RelicTier::Rare}) {
    hold_all_of_tier_except(run, tier, screens);
  }
  run.obtain_relic(RelicId::CallingBell);
  for (RelicId id : screens) EXPECT_FALSE(run.has_relic(id));
}

TEST(CurseAcquisition, OmamoriAlreadyHeldBlocksTheBellsCurse) {
  RunState run = with_deck({}, {RelicId::Omamori});
  run.obtain_relic(RelicId::CallingBell);
  EXPECT_EQ(count_id(run, CardId::CurseOfTheBell), 0);
  EXPECT_EQ(counter_of(run, RelicId::Omamori), kOmamoriCharges - 1);
}

TEST(CurseAcquisition, DarkstoneAlreadyHeldCountsTheBellsCurse) {
  // Every Common, Uncommon and Rare is held, so the Bell grants no relic and
  // nothing else can move Max HP: the +6 is Darkstone's, for the curse.
  RunState run = with_deck({}, {RelicId::DarkstonePeriapt});
  for (RelicTier tier :
       {RelicTier::Common, RelicTier::Uncommon, RelicTier::Rare}) {
    hold_all_of_tier_except(run, tier, {});
  }
  const int max_hp = run.max_hp;
  run.obtain_relic(RelicId::CallingBell);
  EXPECT_EQ(run.max_hp, max_hp + kDarkstoneMaxHp);
}

TEST(CurseAcquisition, TheBellsOwnOmamoriAndDarkstoneComeTooLateForItsCurse) {
  // The Common draw can only be Omamori and the Uncommon only Darkstone
  // Periapt; every Rare is held, so the Rare draw gives nothing. The curse
  // arrives first, so neither acts on it.
  RunState run = with_deck({}, {});
  hold_all_of_tier_except(run, RelicTier::Common, {RelicId::Omamori});
  hold_all_of_tier_except(run, RelicTier::Uncommon, {RelicId::DarkstonePeriapt});
  hold_all_of_tier_except(run, RelicTier::Rare, {});
  const int max_hp = run.max_hp;
  run.obtain_relic(RelicId::CallingBell);
  ASSERT_TRUE(run.has_relic(RelicId::Omamori));
  ASSERT_TRUE(run.has_relic(RelicId::DarkstonePeriapt));
  EXPECT_EQ(count_id(run, CardId::CurseOfTheBell), 1);
  EXPECT_EQ(counter_of(run, RelicId::Omamori), kOmamoriCharges);
  EXPECT_EQ(run.max_hp, max_hp);
}

}  // namespace
}  // namespace minispire
