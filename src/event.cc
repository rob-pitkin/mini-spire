#include "event.h"

#include "turn_loop.h"  // kNumEventOptions, the action block's width

namespace minispire {

static_assert(static_cast<int>(EventId::WomanInBlue) + 1 == kNumEvents,
              "EventId and kNumEvents disagree");
static_assert(static_cast<int>(EventOption::WomanInBlueLeave) + 1 ==
                  kNumEventOptionIds,
              "EventOption and kNumEventOptionIds disagree");
static_assert(kNumEventOptionIds == kNumEventOptions,
              "the event-option action block must hold exactly one action per "
              "EventOption (v2-spec.md §6.3)");

const char* event_name(EventId id) {
  switch (id) {
    case EventId::BigFish: return "Big Fish";
    case EventId::TheCleric: return "The Cleric";
    case EventId::DeadAdventurer: return "Dead Adventurer";
    case EventId::GoldenIdol: return "Golden Idol";
    case EventId::WingStatue: return "Wing Statue";
    case EventId::WorldOfGoop: return "World of Goop";
    case EventId::TheSsssserpent: return "The Ssssserpent";
    case EventId::LivingWall: return "Living Wall";
    case EventId::Mushrooms: return "Hypnotizing Colored Mushrooms";
    case EventId::ScrapOoze: return "Scrap Ooze";
    case EventId::ShiningLight: return "Shining Light";
    case EventId::MatchAndKeep: return "Match and Keep!";
    case EventId::GoldenShrine: return "Golden Shrine";
    case EventId::Transmogrifier: return "Transmogrifier";
    case EventId::Purifier: return "Purifier";
    case EventId::UpgradeShrine: return "Upgrade Shrine";
    case EventId::WheelOfChange: return "Wheel of Change";
    case EventId::OminousForge: return "Ominous Forge";
    case EventId::BonfireSpirits: return "Bonfire Spirits";
    case EventId::FaceTrader: return "Face Trader";
    case EventId::DivineFountain: return "The Divine Fountain";
    case EventId::Lab: return "Lab";
    case EventId::NoteForYourself: return "A Note For Yourself";
    case EventId::WeMeetAgain: return "We Meet Again!";
    case EventId::WomanInBlue: return "The Woman in Blue";
  }
  return "?";
}

EventId event_of(EventOption option) {
  // The options are grouped by event, so the event is the group the id falls
  // in. Each group's first id marks where it starts.
  const auto at_or_after = [option](EventOption first) {
    return static_cast<int>(option) >= static_cast<int>(first);
  };
  if (at_or_after(EventOption::WomanInBlueBuyOne)) return EventId::WomanInBlue;
  if (at_or_after(EventOption::WeMeetAgainGivePotion)) return EventId::WeMeetAgain;
  if (at_or_after(EventOption::NoteForYourselfTakeAndGive)) {
    return EventId::NoteForYourself;
  }
  if (at_or_after(EventOption::DivineFountainDrink)) return EventId::DivineFountain;
  if (at_or_after(EventOption::FaceTraderTouch)) return EventId::FaceTrader;
  if (at_or_after(EventOption::OminousForgeForge)) return EventId::OminousForge;
  if (at_or_after(EventOption::UpgradeShrinePray)) return EventId::UpgradeShrine;
  if (at_or_after(EventOption::PurifierPray)) return EventId::Purifier;
  if (at_or_after(EventOption::TransmogrifierPray)) return EventId::Transmogrifier;
  if (at_or_after(EventOption::GoldenShrinePray)) return EventId::GoldenShrine;
  if (at_or_after(EventOption::ShiningLightEnter)) return EventId::ShiningLight;
  if (at_or_after(EventOption::ScrapOozeReachIn)) return EventId::ScrapOoze;
  if (at_or_after(EventOption::MushroomsStomp)) return EventId::Mushrooms;
  if (at_or_after(EventOption::LivingWallForget)) return EventId::LivingWall;
  if (at_or_after(EventOption::SsssserpentAgree)) return EventId::TheSsssserpent;
  if (at_or_after(EventOption::GoopGather)) return EventId::WorldOfGoop;
  if (at_or_after(EventOption::WingStatuePray)) return EventId::WingStatue;
  if (at_or_after(EventOption::GoldenIdolTake)) return EventId::GoldenIdol;
  if (at_or_after(EventOption::DeadAdventurerSearch)) {
    return EventId::DeadAdventurer;
  }
  if (at_or_after(EventOption::ClericHeal)) return EventId::TheCleric;
  return EventId::BigFish;
}

int event_damage_after_relics(int amount, EventDamage kind, bool boot,
                              bool torii, bool tungsten_rod) {
  if (amount <= 0) return 0;
  // The Boot runs in onAttackToChangeDamage, which AbstractPlayer.damage calls
  // only when the hit's owner is the player itself.
  if (kind == EventDamage::Player && boot && amount < 5) amount = 5;
  // Torii runs in onAttacked, inside `if (info.owner != null)`, and skips
  // HP_LOSS. Of the three kinds only Player has an owner.
  if (kind == EventDamage::Player && torii && amount > 1 && amount <= 5) {
    amount = 1;
  }
  // Tungsten Rod's onLoseHpLast runs for every hit.
  if (tungsten_rod) amount -= 1;
  return amount > 0 ? amount : 0;
}

std::vector<RelicId> face_trader_faces() {
  return {RelicId::CultistHeadpiece, RelicId::FaceOfCleric,
          RelicId::GremlinVisage, RelicId::NlothsHungryFace,
          RelicId::SsserpentHead};
}

}  // namespace minispire
