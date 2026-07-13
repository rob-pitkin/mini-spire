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
  // Status cards (added mid-fight by enemies; not part of any deck). Grouped at
  // the end. Slimed: 1-cost do-nothing that Exhausts on play (ROB-72).
  Slimed,
  // Dazed (ROB-65 Sentries): UNPLAYABLE (masked, never a legal action) and
  // Ethereal (exhausts at end of turn if still in hand).
  Dazed,
};

// Number of distinct card types. Drives the obs pile-count stride and the
// action-space size (card x target). Update CARD_DATABASE + kObsCardOrder in
// lockstep — a static_assert in combat_env.cc enforces the count matches.
inline constexpr int kNumCardTypes = 8;

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

// FUTURE: per-instance card state (e.g. Ritual Dagger's accumulated damage,
// Searing Blow's cumulative upgrades) will require widening this struct.
// v1 cards don't need it.
struct Card {
  CardId card_id;
};

struct CardData {
  const char* name;  // display name — single source of truth (ROB-79)
  int cost;
  int damage;
  int block;
  std::vector<DebuffApplication> applies_debuffs;
  std::vector<PowerApplication> applies_powers;
  bool exhaust = false;  // exhausts when PLAYED (Slimed)
  CardType type = CardType::Attack;
  bool unplayable = false;  // never a legal action (Dazed) — masked out
  bool ethereal = false;    // exhausts at end of turn if unplayed in hand (Dazed)
};

inline const std::unordered_map<CardId, CardData> CARD_DATABASE = {
    {CardId::Strike,     {"Strike",  1, 6,  0, {}, {}, false, CardType::Attack}},
    {CardId::StrikePlus, {"Strike+", 1, 9,  0, {}, {}, false, CardType::Attack}},
    {CardId::Defend,     {"Defend",  1, 0,  5, {}, {}, false, CardType::Skill}},
    {CardId::DefendPlus, {"Defend+", 1, 0,  8, {}, {}, false, CardType::Skill}},
    {CardId::Bash,       {"Bash",    2, 8,  0, {{Debuff::Vulnerable, 2, Target::Enemy}}, {}, false, CardType::Attack}},
    {CardId::BashPlus,   {"Bash+",   2, 10, 0, {{Debuff::Vulnerable, 3, Target::Enemy}}, {}, false, CardType::Attack}},
    // Slimed: a status card (ROB-72). 1 energy, does nothing, Exhausts on play.
    {CardId::Slimed,     {"Slimed",  1, 0,  0, {}, {}, /*exhaust=*/true, CardType::Status}},
    // Dazed (ROB-65): unplayable + ethereal. cost is moot (never played).
    {CardId::Dazed,      {"Dazed",   0, 0,  0, {}, {}, false, CardType::Status,
                          /*unplayable=*/true, /*ethereal=*/true}},
};

// Whether a card acts on a chosen enemy (vs. the player / self). Derived, not a
// stored field: a card is enemy-targeting iff it deals damage or applies a
// status to an enemy. This drives the (card x target) action space (ROB-60) —
// targeted cards get a target index and are masked on the target being alive;
// untargeted cards (Defend) use the canonical offset-0 slot.
//
// FUTURE (AoE): a card that hits *all* enemies (Cleave, Whirlwind) is still
// "targeting an enemy" by this predicate, but it does not *pick* one. When such
// cards land, "targets an enemy" and "needs a specific target index" diverge —
// AoE cards should resolve over all living enemies and not consume a target
// index. Revisit the masking/decoding fork then.
inline bool card_targets_enemy(const CardData& data) {
  if (data.damage > 0) return true;
  for (const DebuffApplication& app : data.applies_debuffs) {
    if (app.target == Target::Enemy) return true;
  }
  for (const PowerApplication& app : data.applies_powers) {
    if (app.target == Target::Enemy) return true;
  }
  return false;
}

// Display name for a card (ROB-79) — reads CardData::name, the single source of
// truth. The TUI uses this so it never maintains its own name map.
inline const char* card_name(CardId id) { return CARD_DATABASE.at(id).name; }

}  // namespace minispire
