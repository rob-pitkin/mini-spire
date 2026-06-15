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
};

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
};

}  // namespace minispire
