#ifndef MINISPIRE_EVENT_H
#define MINISPIRE_EVENT_H

#include <cstdint>
#include <vector>

#include "relic.h"

namespace minispire {

// The 25 events reachable in Act 1 (docs/design/events.md §1).
//
// Declaration order is StS's list order within each group (Exordium's event
// and shrine lists, then the one-time list filtered to Act 1), and it is the
// observation's event one-hot (v2-spec.md §5.2, block 17): an interface.
enum class EventId : uint8_t {
  // Act 1 events (Exordium.initializeEventList).
  BigFish,
  TheCleric,
  DeadAdventurer,
  GoldenIdol,
  WingStatue,      // "Golden Wing"
  WorldOfGoop,
  TheSsssserpent,  // "Liars Game"
  LivingWall,
  Mushrooms,       // Hypnotizing Colored Mushrooms
  ScrapOoze,
  ShiningLight,
  // Act 1 shrines (Exordium.initializeShrineList).
  MatchAndKeep,
  GoldenShrine,
  Transmogrifier,
  Purifier,
  UpgradeShrine,
  WheelOfChange,
  // One-time events that can pass their gate in Act 1
  // (initializeSpecialOneTimeEventList).
  OminousForge,    // "Accursed Blacksmith"
  BonfireSpirits,
  FaceTrader,
  DivineFountain,  // "Fountain of Cleansing"
  Lab,
  NoteForYourself,
  WeMeetAgain,
  WomanInBlue,
};

inline constexpr int kNumEvents = 25;

// Every option an event screen can offer, one global id each (v2-spec.md §6.3,
// events.md §5). Grouped by event in EventId order; within an event, in the
// order StS shows them. An option that is unavailable is masked in place, never
// renumbered. A screen with one option advances on its own (events.md §3.2),
// which is why Lab, Wheel of Change, Match and Keep and Bonfire Spirits have
// none here.
enum class EventOption : uint8_t {
  BigFishBanana, BigFishDonut, BigFishBox,
  ClericHeal, ClericPurify, ClericLeave,
  DeadAdventurerSearch, DeadAdventurerLeave,
  // Two screens on disjoint ids: Take/Leave, then the trap's three.
  GoldenIdolTake, GoldenIdolLeave,
  GoldenIdolOutrun, GoldenIdolSmash, GoldenIdolHide,
  WingStatuePray, WingStatueDestroy, WingStatueLeave,
  GoopGather, GoopLeave,
  SsssserpentAgree, SsssserpentDisagree,
  LivingWallForget, LivingWallChange, LivingWallGrow,
  MushroomsStomp, MushroomsEat,
  ScrapOozeReachIn, ScrapOozeLeave,
  ShiningLightEnter, ShiningLightLeave,

  GoldenShrinePray, GoldenShrineDesecrate, GoldenShrineLeave,
  TransmogrifierPray, TransmogrifierLeave,
  PurifierPray, PurifierLeave,
  UpgradeShrinePray, UpgradeShrineLeave,

  OminousForgeForge, OminousForgeRummage, OminousForgeLeave,
  FaceTraderTouch, FaceTraderTrade, FaceTraderLeave,
  DivineFountainDrink, DivineFountainLeave,
  NoteForYourselfTakeAndGive, NoteForYourselfIgnore,
  WeMeetAgainGivePotion, WeMeetAgainGiveGold, WeMeetAgainGiveCard,
  WeMeetAgainAttack,
  WomanInBlueBuyOne, WomanInBlueBuyTwo, WomanInBlueBuyThree, WomanInBlueLeave,
};

// Counted per event in events.md §5: 29 + 9 + 18.
inline constexpr int kNumEventOptionIds = 56;

const char* event_name(EventId id);

// The event an option belongs to.
EventId event_of(EventOption option);

// The share of a `?` that resolves to an event and draws a shrine first
// (AbstractDungeon.shrineChance; events.md §2.2).
inline constexpr float kShrineChance = 0.25f;

// --- amounts (Ascension 0; events.md §5) ---

inline constexpr int kBigFishMaxHp = 5;
inline constexpr int kGoopDamage = 11;
inline constexpr int kGoopGold = 75;
inline constexpr int kGoopLossMin = 20;
inline constexpr int kGoopLossMax = 50;
inline constexpr int kSsssserpentGold = 175;
inline constexpr int kScrapOozeDamage = 3;    // +1 per failed attempt
inline constexpr int kScrapOozeChance = 25;   // +10 per failed attempt
inline constexpr int kScrapOozeDamageStep = 1;
inline constexpr int kScrapOozeChanceStep = 10;
inline constexpr int kShiningLightUpgrades = 2;
inline constexpr int kGoldenShrineGold = 100;
inline constexpr int kGoldenShrineDesecrateGold = 275;
inline constexpr int kFaceTraderGold = 75;
inline constexpr int kClericHealCost = 35;
inline constexpr int kClericPurifyCost = 50;
inline constexpr int kWingStatueDamage = 7;
inline constexpr int kWingStatueGoldMin = 50;
inline constexpr int kWingStatueGoldMax = 80;
inline constexpr int kWingStatueAttackDamage = 10;  // Destroy needs one this big
inline constexpr int kBonfireCommonHeal = 5;
inline constexpr int kBonfireRareMaxHp = 10;
inline constexpr int kWheelResults = 6;
inline constexpr int kWheelGold = 100;  // Exordium's (setGold)
inline constexpr float kWheelHpLossFraction = 0.10f;
inline constexpr int kLabPotions = 3;  // a third below Ascension 15
inline constexpr int kWomanInBlueCostOne = 20;
inline constexpr int kWomanInBlueCostTwo = 30;
inline constexpr int kWomanInBlueCostThree = 40;
inline constexpr int kWeMeetAgainMinGold = 50;  // and no gold offer below it
inline constexpr int kWeMeetAgainMaxGold = 150;
inline constexpr int kDeadAdventurerRewards = 3;
inline constexpr int kDeadAdventurerGold = 30;  // found, or a fight reward
inline constexpr int kDeadAdventurerChance = 25;  // +25 per reward found
inline constexpr int kDeadAdventurerChanceStep = 25;
inline constexpr int kDeadAdventurerFightGoldMin = 25;
inline constexpr int kDeadAdventurerFightGoldMax = 35;
inline constexpr int kMushroomsFightGoldMin = 20;
inline constexpr int kMushroomsFightGoldMax = 30;

// Dead Adventurer's three rewards, found one per successful search in an order
// shuffled on entry; those not found by the fight go on its reward screen.
enum class DeadAdventurerReward : uint8_t { Gold, Nothing, Relic };

// The elite under the body, `miscRng.random(0, 2)` in this order. A player
// reads which one from the event's text (events.md §6).
enum class DeadAdventurerElite : uint8_t {
  ThreeSentries,
  GremlinNob,
  Lagavulin,
};

// Search's fight chance after `found` rewards: 25, 50, 75. The fight starts
// when `miscRng.random(0, 99)` is below it. Pure, so the boundary is tested.
inline int dead_adventurer_chance(int found) {
  return kDeadAdventurerChance + found * kDeadAdventurerChanceStep;
}

// Gates (events.md §2.2).
inline constexpr int kEventsLateFloor = 6;  // Dead Adventurer, Mushrooms: floor > 6
inline constexpr int kClericMinGold = 35;
inline constexpr int kWomanInBlueMinGold = 50;

// Scrap Ooze's success test, `miscRng.random(0, 99) >= 99 - chance`. The
// comparison makes the real odds chance + 1 percent (26, 36, 46…), which the
// wiki states too. Pure, so the boundary is tested directly.
inline bool scrap_ooze_finds_relic(int roll, int chance) {
  return roll >= 99 - chance;
}

// How a hit outside combat is attributed (events.md §4.1). StS passes a
// DamageInfo to AbstractPlayer.damage, and what runs depends on its owner and
// type.
enum class EventDamage : uint8_t {
  Player,   // owner is the player: The Boot, then Torii, then Tungsten Rod
  Unowned,  // no owner: Tungsten Rod only
  HpLoss,   // no owner, HP_LOSS: Tungsten Rod only
};

// The HP an out-of-combat hit takes after the relics that change it. The same
// thresholds as combat's boot_adjusted_damage and reduce_player_hp_loss
// (query.cc): The Boot raises 1-4 to 5 (onAttackToChangeDamage, owner is the
// player), Torii lowers 2-5 to 1 (onAttacked, an owner and not HP_LOSS), and
// Tungsten Rod takes 1 off whatever is left (onLoseHpLast). Block is 0 out of
// combat. Pure, so each rule is tested on its own.
int event_damage_after_relics(int amount, EventDamage kind, bool boot,
                              bool torii, bool tungsten_rod);

// The face relics Face Trader can give (getRandomFace), in StS's order.
std::vector<RelicId> face_trader_faces();

}  // namespace minispire

#endif  // MINISPIRE_EVENT_H
