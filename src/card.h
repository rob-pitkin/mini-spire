#pragma once

#include <unordered_map>
#include <vector>

#include "status_effect.h"

namespace minispire {

// Action-space stability note: the integer values of CardId enumerators are
// the agent's action indices. Reordering or inserting new enumerators in the
// middle will invalidate any trained policy's action mapping. Append-only.
enum class CardId {
  Strike,
  Defend,
  Bash,
  StrikePlus,
  DefendPlus,
  BashPlus,
  // Status cards (added mid-fight by enemies; not part of any deck). Slimed:
  // 1-cost do-nothing that Exhausts on play (ROB-72).
  Slimed,
  // Dazed (ROB-65 Sentries): UNPLAYABLE + Ethereal.
  Dazed,
  // Ironclad card pool, Tier A (ROB-80) — pure-data cards (AoE / multi-hit /
  // X-cost). Appended (CardId values are action indices; append-only).
  // Generated from data/ironclad_cards.csv via analysis/gen_cards.py.
  Cleave,
  CleavePlus,
  Clothesline,
  ClotheslinePlus,
  Flex,
  FlexPlus,
  IronWave,
  IronWavePlus,
  Thunderclap,
  ThunderclapPlus,
  TwinStrike,
  TwinStrikePlus,
  Carnage,
  CarnagePlus,
  Disarm,
  DisarmPlus,
  GhostlyArmor,
  GhostlyArmorPlus,
  Intimidate,
  IntimidatePlus,
  Pummel,
  PummelPlus,
  Shockwave,
  ShockwavePlus,
  Uppercut,
  UppercutPlus,
  Whirlwind,
  WhirlwindPlus,
  Bludgeon,
  BludgeonPlus,
  // Tier B (ROB-80) — card-flow: draw / energy / lose-HP.
  PommelStrike,
  PommelStrikePlus,
  ShrugItOff,
  ShrugItOffPlus,
  Bloodletting,
  BloodlettingPlus,
  Hemokinesis,
  HemokinesisPlus,
  SeeingRed,
  SeeingRedPlus,
  Offering,
  OfferingPlus,
};

// Number of distinct card types. Drives the obs pile-count stride and the
// action-space size (card x target). Update CARD_DATABASE + kObsCardOrder in
// lockstep — a static_assert in combat_env.cc enforces the count matches.
inline constexpr int kNumCardTypes = 50;

// A card's inherent StS type. This is a real property, NOT inferable from
// damage/block: an Attack can gain block (Body Slam) and a Skill can deal
// damage, so `damage > 0` is not a faithful proxy. Drives Entangle's attack
// mask (ROB-75) and the Gremlin Nob's Enrage (OnPlayerSkill, ROB-65).
enum class CardType {
  Attack,
  Skill,
  Power,
  Status,  // added mid-fight by enemies (Slimed, Dazed); not in any deck
  Curse,
};

// How a card is targeted (ROB-80). Replaces the old derived predicate: an AoE
// card "targets an enemy" but picks none, so targeting must be an explicit
// property.
enum class CardTarget {
  None,        // no target (Defend) — canonical action slot 0
  Enemy,       // pick one living enemy (Strike, Bash)
  AllEnemies,  // hits every living enemy, no pick (Cleave) — canonical slot 0
  Self,        // affects the player (Flex) — canonical slot 0
};

// X-cost sentinel (ROB-80): playing an X-cost card (Whirlwind) spends ALL
// current energy; X = the energy spent. Stored in CardData::cost.
inline constexpr int kXCost = -2;

// FUTURE: per-instance card state (e.g. Ritual Dagger's accumulated damage,
// Searing Blow's cumulative upgrades) will require widening this struct.
struct Card {
  CardId card_id;
};

struct CardData {
  const char* name;  // display name — single source of truth (ROB-79)
  int cost;
  int damage;
  int hits = 1;  // multi-hit: total damage = damage x hits (Strength per hit).
                 // -1 = X hits (Whirlwind: hits == energy spent).
  int block = 0;
  CardTarget target = CardTarget::Enemy;
  std::vector<DebuffApplication> applies_debuffs;
  std::vector<PowerApplication> applies_powers;
  CardType type = CardType::Attack;
  bool exhaust = false;     // exhausts when PLAYED (Slimed)
  bool ethereal = false;    // exhausts at end of turn if unplayed in hand (Dazed)
  bool unplayable = false;  // never a legal action (Dazed) — masked out
  // Card-flow effects (ROB-80 Tier B). Resolve with the card:
  int draw = 0;     // draw N cards (resolves last)
  int energy = 0;   // gain N energy
  int lose_hp = 0;  // player loses N HP — an EFFECT (not a cost, can kill you),
                    // direct HP loss that bypasses block (verified).
};

// CardData row order: name, cost, damage, hits, block, target, debuffs, powers,
// type, exhaust, ethereal, unplayable.
inline const std::unordered_map<CardId, CardData> CARD_DATABASE = {
    // Starter deck.
    {CardId::Strike,     {"Strike",  1, 6, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack}},
    {CardId::StrikePlus, {"Strike+", 1, 9, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack}},
    {CardId::Defend,     {"Defend",  1, 0, 1, 5, CardTarget::None,  {}, {}, CardType::Skill}},
    {CardId::DefendPlus, {"Defend+", 1, 0, 1, 8, CardTarget::None,  {}, {}, CardType::Skill}},
    {CardId::Bash,       {"Bash",    2, 8, 1, 0, CardTarget::Enemy, {{Debuff::Vulnerable, 2, Target::Enemy}}, {}, CardType::Attack}},
    {CardId::BashPlus,   {"Bash+",   2, 10, 1, 0, CardTarget::Enemy, {{Debuff::Vulnerable, 3, Target::Enemy}}, {}, CardType::Attack}},
    // Status cards (enemy-added, not in the CSV). Slimed: exhausts on play.
    {CardId::Slimed,     {"Slimed",  1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Status, /*exhaust=*/true}},
    // Dazed: unplayable + ethereal.
    {CardId::Dazed,      {"Dazed",   0, 0, 1, 0, CardTarget::None, {}, {}, CardType::Status, /*exhaust=*/false, /*ethereal=*/true, /*unplayable=*/true}},
    // --- Ironclad Tier A (ROB-80), generated from data/ironclad_cards.csv ---
    {CardId::Cleave, {"Cleave", 1, 8, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::CleavePlus, {"Cleave+", 1, 11, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::Clothesline, {"Clothesline", 2, 12, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 2, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::ClotheslinePlus, {"Clothesline+", 2, 14, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 3, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::Flex, {"Flex", 0, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 2, Target::Character}}, CardType::Skill, false, false}},
    {CardId::FlexPlus, {"Flex+", 0, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 4, Target::Character}}, CardType::Skill, false, false}},
    {CardId::IronWave, {"Iron Wave", 1, 5, 1, 5, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::IronWavePlus, {"Iron Wave+", 1, 7, 1, 7, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::Thunderclap, {"Thunderclap", 1, 4, 1, 0, CardTarget::AllEnemies, {{Debuff::Vulnerable, 1, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::ThunderclapPlus, {"Thunderclap+", 1, 7, 1, 0, CardTarget::AllEnemies, {{Debuff::Vulnerable, 1, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::TwinStrike, {"Twin Strike", 1, 5, 2, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::TwinStrikePlus, {"Twin Strike+", 1, 7, 2, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::Carnage, {"Carnage", 2, 20, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, true}},
    {CardId::CarnagePlus, {"Carnage+", 2, 28, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, true}},
    {CardId::Disarm, {"Disarm", 1, 0, 1, 0, CardTarget::Enemy, {}, {{Power::Strength, -2, Target::Enemy}}, CardType::Skill, true, false}},
    {CardId::DisarmPlus, {"Disarm+", 1, 0, 1, 0, CardTarget::Enemy, {}, {{Power::Strength, -3, Target::Enemy}}, CardType::Skill, true, false}},
    {CardId::GhostlyArmor, {"Ghostly Armor", 1, 0, 0, 10, CardTarget::None, {}, {}, CardType::Skill, false, true}},
    {CardId::GhostlyArmorPlus, {"Ghostly Armor+", 1, 0, 0, 13, CardTarget::None, {}, {}, CardType::Skill, false, true}},
    {CardId::Intimidate, {"Intimidate", 0, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 1, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::IntimidatePlus, {"Intimidate+", 0, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 2, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::Pummel, {"Pummel", 1, 2, 4, 0, CardTarget::Enemy, {}, {}, CardType::Attack, true, false}},
    {CardId::PummelPlus, {"Pummel+", 1, 2, 5, 0, CardTarget::Enemy, {}, {}, CardType::Attack, true, false}},
    {CardId::Shockwave, {"Shockwave", 2, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 3, Target::Enemy}, {Debuff::Vulnerable, 3, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::ShockwavePlus, {"Shockwave+", 2, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 5, Target::Enemy}, {Debuff::Vulnerable, 5, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::Uppercut, {"Uppercut", 2, 13, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 1, Target::Enemy}, {Debuff::Vulnerable, 1, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::UppercutPlus, {"Uppercut+", 2, 13, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 2, Target::Enemy}, {Debuff::Vulnerable, 2, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::Whirlwind, {"Whirlwind", kXCost, 5, -1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::WhirlwindPlus, {"Whirlwind+", kXCost, 8, -1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::Bludgeon, {"Bludgeon", 3, 32, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::BludgeonPlus, {"Bludgeon+", 3, 42, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    // --- Ironclad Tier B (ROB-80): card-flow (draw / energy / lose-HP) ---
    {CardId::PommelStrike, {"Pommel Strike", 1, 9, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 1, 0, 0}},
    {CardId::PommelStrikePlus, {"Pommel Strike+", 1, 10, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 2, 0, 0}},
    {CardId::ShrugItOff, {"Shrug It Off", 1, 0, 0, 8, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 1, 0, 0}},
    {CardId::ShrugItOffPlus, {"Shrug It Off+", 1, 0, 0, 11, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 1, 0, 0}},
    {CardId::Bloodletting, {"Bloodletting", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 2, 3}},
    {CardId::BloodlettingPlus, {"Bloodletting+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 3, 3}},
    {CardId::Hemokinesis, {"Hemokinesis", 1, 15, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 2}},
    {CardId::HemokinesisPlus, {"Hemokinesis+", 1, 20, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 2}},
    {CardId::SeeingRed, {"Seeing Red", 1, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 0, 2, 0}},
    {CardId::SeeingRedPlus, {"Seeing Red+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 0, 2, 0}},
    {CardId::Offering, {"Offering", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 3, 2, 6}},
    {CardId::OfferingPlus, {"Offering+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 5, 2, 6}},
};

// Whether a card needs the player to PICK a specific enemy slot (ROB-80). Only
// CardTarget::Enemy does — it gets a target index and is masked on that slot
// being alive. None / AllEnemies / Self all resolve without a pick and use the
// canonical action slot 0 (AoE loops all living enemies at resolve time).
inline bool card_targets_enemy(const CardData& data) {
  return data.target == CardTarget::Enemy;
}

// Display name for a card (ROB-79) — reads CardData::name, the single source of
// truth. The TUI uses this so it never maintains its own name map.
inline const char* card_name(CardId id) { return CARD_DATABASE.at(id).name; }

}  // namespace minispire
