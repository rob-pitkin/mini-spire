#pragma once

namespace minispire {

enum class CardId {
  Strike,
  Defend,
  Bash,
};

struct Card {
  CardId card_id;
  bool upgraded;
};

}  // namespace minispire
