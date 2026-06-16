#include "combat_env.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "card.h"
#include "enemy.h"
#include "status_effect.h"
#include "turn_loop.h"

namespace minispire {

namespace {

// CardId ordering for the obs layout (per ROB-40 section 7).
// Matches CardId declaration order in card.h.
constexpr std::array<CardId, 6> kObsCardOrder = {
    CardId::Strike,     CardId::Defend,     CardId::Bash,
    CardId::StrikePlus, CardId::DefendPlus, CardId::BashPlus,
};

// StatusEffect ordering for the obs layout.
// Matches StatusEffect declaration order in status_effect.h.
constexpr std::array<StatusEffect, 4> kObsStatusOrder = {
    StatusEffect::Vulnerable,
    StatusEffect::Weak,
    StatusEffect::Strength,
    StatusEffect::Dexterity,
};

float status_stacks(const std::unordered_map<StatusEffect, int>& effects,
                    StatusEffect e) {
  auto it = effects.find(e);
  return it == effects.end() ? 0.0f : static_cast<float>(it->second);
}

// Count cards of a given id in a pile.
int pile_count(const std::vector<Card>& pile, CardId id) {
  int n = 0;
  for (const Card& c : pile) {
    if (c.card_id == id) ++n;
  }
  return n;
}

}  // namespace

CombatEnv::CombatEnv() : mask_buffer_(kNumActions, 0) {
  // Engine invariant: action space is num CardIds + end-turn.
  static_assert(kNumActions == 6 + 1,
                "kNumActions must equal CARD_DATABASE size + 1 (end turn)");
}

void CombatEnv::reset(uint32_t seed) {
  state_ = start_v1_combat(seed);
  reward_ = 0.0f;
  compute_obs();
  compute_mask();
}

void CombatEnv::step(int action) {
  if (action < 0 || action >= kNumActions) {
    throw std::invalid_argument("CombatEnv::step: action out of range");
  }
  if (!mask_buffer_[action]) {
    throw std::invalid_argument(
        "CombatEnv::step: action is masked off (illegal in current state)");
  }

  bool ok = apply_action(state_, action);
  if (!ok) {
    // Should be unreachable: the mask check above mirrors valid_actions.
    throw std::invalid_argument("CombatEnv::step: apply_action rejected action");
  }

  switch (state_.outcome) {
    case Outcome::Won:        reward_ =  1.0f; break;
    case Outcome::Lost:       reward_ = -1.0f; break;
    case Outcome::InProgress: reward_ =  0.0f; break;
  }

  compute_obs();
  compute_mask();
}

void CombatEnv::compute_obs() {
  // Layout per ROB-40 section 7. Indices are documented in the design doc.
  // All values are raw — downstream consumers can normalize.
  std::array<float, kObsSize>& o = obs_buffer_;
  std::fill(o.begin(), o.end(), 0.0f);

  const Character& c = state_.character;
  const Enemy& e = state_.enemies[0];

  // Character (slots 0..8)
  o[0] = static_cast<float>(c.hp);
  o[1] = static_cast<float>(c.max_hp);
  o[2] = static_cast<float>(c.current_block);
  o[3] = static_cast<float>(c.energy);
  o[4] = static_cast<float>(c.energy_per_turn);
  for (std::size_t i = 0; i < kObsStatusOrder.size(); ++i) {
    o[5 + i] = status_stacks(c.status_effects, kObsStatusOrder[i]);
  }

  // Enemy (slots 9..15)
  o[9]  = static_cast<float>(e.hp);
  o[10] = static_cast<float>(e.max_hp);
  o[11] = static_cast<float>(e.current_block);
  for (std::size_t i = 0; i < kObsStatusOrder.size(); ++i) {
    o[12 + i] = status_stacks(e.status_effects, kObsStatusOrder[i]);
  }

  // Enemy intent (slots 16..19). last_move always primed by make_jaw_worm
  // and by handle_end_turn — but guard defensively.
  if (e.last_move.has_value()) {
    auto move_it = e.moves.find(*e.last_move);
    if (move_it != e.moves.end()) {
      const Move& m = move_it->second;
      bool is_attacking = m.damage > 0;
      bool is_blocking  = m.block  > 0;
      bool is_buffing   = !m.applies.empty();
      o[16] = is_attacking ? 1.0f : 0.0f;
      // Displayed attack damage (Strength/Weak on enemy, Vulnerable on
      // character all factored in) — matches what the CLI shows.
      o[17] = is_attacking
                  ? static_cast<float>(compute_attack_damage(
                        m.damage, e.status_effects, c.status_effects))
                  : 0.0f;
      o[18] = is_blocking ? 1.0f : 0.0f;
      o[19] = is_buffing  ? 1.0f : 0.0f;
    }
  }

  // Pile counts per CardId (slots 20..43).
  // Layout: hand (20..25), draw (26..31), discard (32..37), exhaust (38..43)
  for (std::size_t i = 0; i < kObsCardOrder.size(); ++i) {
    CardId id = kObsCardOrder[i];
    o[20 + i] = static_cast<float>(pile_count(state_.current_hand, id));
    o[26 + i] = static_cast<float>(pile_count(state_.draw_pile, id));
    o[32 + i] = static_cast<float>(pile_count(state_.discard_pile, id));
    o[38 + i] = static_cast<float>(pile_count(state_.exhaust_pile, id));
  }

  // Turn number (slot 44).
  o[44] = static_cast<float>(state_.turn_number);
}

void CombatEnv::compute_mask() {
  std::vector<bool> v = valid_actions(state_);
  // Mirror size guarantee — valid_actions returns num_card_ids + 1.
  // mask_buffer_ was sized to kNumActions in the constructor.
  for (int i = 0; i < kNumActions; ++i) {
    mask_buffer_[i] = (i < static_cast<int>(v.size()) && v[i]) ? 1 : 0;
  }
}

}  // namespace minispire
