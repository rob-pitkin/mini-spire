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
};

// Number of distinct card types. Drives the obs pile-count stride and the
// action-space size (card x target). Update CARD_DATABASE + kObsCardOrder in
// lockstep — a static_assert in combat_env.cc enforces the count matches.
inline constexpr int kNumCardTypes = 7;

// FUTURE: per-instance card state (e.g. Ritual Dagger's accumulated damage,
// Searing Blow's cumulative upgrades) will require widening this struct.
// v1 cards don't need it.
struct Card {
  CardId card_id;
};

struct CardData {
  int cost;
  int damage;
  int block;
  std::vector<StatusApplication> applies;
  bool exhaust = false;
};

inline const std::unordered_map<CardId, CardData> CARD_DATABASE = {
    {CardId::Strike,     {1, 6,  0, {}}},
    {CardId::StrikePlus, {1, 9,  0, {}}},
    {CardId::Defend,     {1, 0,  5, {}}},
    {CardId::DefendPlus, {1, 0,  8, {}}},
    {CardId::Bash,       {2, 8,  0, {StatusApplication{StatusEffect::Vulnerable, 2, StatusApplication::Target::Enemy}}}},
    {CardId::BashPlus,   {2, 10, 0, {StatusApplication{StatusEffect::Vulnerable, 3, StatusApplication::Target::Enemy}}}},
    // Slimed: a status card (ROB-72). 1 energy, does nothing, Exhausts on play.
    {CardId::Slimed,     {1, 0,  0, {}, /*exhaust=*/true}},
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
  for (const StatusApplication& app : data.applies) {
    if (app.target == StatusApplication::Target::Enemy) return true;
  }
  return false;
}

}  // namespace minispire
