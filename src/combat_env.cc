#include "combat_env.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <stdexcept>
#include <utility>

#include "card.h"
#include "enemy.h"
#include "status_effect.h"
#include "turn_loop.h"

namespace minispire {

namespace {

// CardId ordering for the obs pile-count layout. Must list every CardId and
// match kNumCardTypes (static_assert below).
constexpr std::array<CardId, kNumCardTypes> kObsCardOrder = {
    CardId::Strike,     CardId::Defend,     CardId::Bash,
    CardId::StrikePlus, CardId::DefendPlus, CardId::BashPlus,
    CardId::Slimed,
};
static_assert(kObsCardOrder.size() == kNumCardTypes,
              "kObsCardOrder must list every card type");

// StatusEffect ordering for the obs layout. Must list every StatusEffect and
// match kNumStatusEffects (static_assert below).
constexpr std::array<StatusEffect, kNumStatusEffects> kObsStatusOrder = {
    StatusEffect::Vulnerable,
    StatusEffect::Weak,
    StatusEffect::Strength,
    StatusEffect::Dexterity,
    StatusEffect::Frail,
    StatusEffect::Ritual,
};
static_assert(kObsStatusOrder.size() == kNumStatusEffects,
              "kObsStatusOrder must list every status effect");

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

CombatEnv::CombatEnv(float hp_reward_coeff)
    : mask_buffer_(kNumActions, 0), hp_reward_coeff_(hp_reward_coeff) {
  // Engine invariant: action space is the (card x target) cross-product plus
  // a single end-turn action (ROB-60), sized from kNumCardTypes.
  static_assert(kNumActions == kNumCardTypes * kMaxEnemies + 1,
                "kNumActions must equal kNumCardTypes * kMaxEnemies + 1");
  // And kNumCardTypes must match the actual card database.
  assert(static_cast<int>(CARD_DATABASE.size()) == kNumCardTypes &&
         "kNumCardTypes out of sync with CARD_DATABASE");
  // A negative reward bonus is meaningless; catch it in debug builds. Other
  // values (including unusual ones) are trusted — it's a hyperparameter.
  assert(hp_reward_coeff_ >= 0.0f && "hp_reward_coeff must be >= 0");
}

CombatEnv::CombatEnv(CombatState state, float hp_reward_coeff)
    : state_(std::move(state)),
      mask_buffer_(kNumActions, 0),
      hp_reward_coeff_(hp_reward_coeff) {
  assert(hp_reward_coeff_ >= 0.0f && "hp_reward_coeff must be >= 0");
  // Make the buffers consistent with the injected state immediately.
  compute_obs();
  compute_mask();
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
    case Outcome::Won: {
      // Sparse +1 plus an optional HP-retention bonus (ROB-52). Read the
      // character's (survivor's) post-step HP; float division.
      const Character& c = state_.character;
      float hp_frac = c.max_hp > 0
                          ? static_cast<float>(c.hp) / static_cast<float>(c.max_hp)
                          : 0.0f;
      reward_ = 1.0f + hp_reward_coeff_ * hp_frac;
      break;
    }
    case Outcome::Lost:       reward_ = -1.0f; break;
    case Outcome::InProgress: reward_ =  0.0f; break;
  }

  compute_obs();
  compute_mask();
}

void CombatEnv::compute_obs() {
  // Layout per ROB-40 + ROB-59 (multi-enemy). All values are raw — downstream
  // consumers can normalize. Section offsets are derived from constants so the
  // layout has no hand-maintained magic numbers (the off-by-one risk).
  std::array<float, kObsSize>& o = obs_buffer_;
  std::fill(o.begin(), o.end(), 0.0f);

  const Character& c = state_.character;

  // --- Player (slots 0..4) ---
  o[0] = static_cast<float>(c.hp);
  o[1] = static_cast<float>(c.max_hp);  // player keeps max_hp; enemies do not
  o[2] = static_cast<float>(c.current_block);
  o[3] = static_cast<float>(c.energy);
  o[4] = static_cast<float>(c.energy_per_turn);

  // --- Player status (slots 5 .. 5+kNumStatusEffects) ---
  for (std::size_t i = 0; i < kObsStatusOrder.size(); ++i) {
    o[5 + i] = status_stacks(c.status_effects, kObsStatusOrder[i]);
  }

  // --- Enemies: kMaxEnemies blocks of kEnemyObsStride floats each ---
  // Per block: [0] is_alive, [1] hp, [2] block, then status (kNumStatusEffects),
  // then intent (4: is_attacking, atk_dmg, is_blocking, is_buffing). Offsets are
  // derived from constants so adding a status can't drift the layout.
  constexpr int kEnemyBase = kPlayerObsSize;
  constexpr int kStatusOff = 3;
  constexpr int kIntentOff = kStatusOff + kNumStatusEffects;
  for (std::size_t slot = 0; slot < kMaxEnemies; ++slot) {
    const int base = kEnemyBase + static_cast<int>(slot) * kEnemyObsStride;
    if (slot >= state_.enemies.size()) continue;  // empty slot: leave zeros
    const Enemy& e = state_.enemies[slot];
    if (e.hp <= 0) continue;  // dead: leave zeros (is_alive stays 0)

    o[base + 0] = 1.0f;  // is_alive
    o[base + 1] = static_cast<float>(e.hp);
    o[base + 2] = static_cast<float>(e.current_block);
    for (std::size_t i = 0; i < kObsStatusOrder.size(); ++i) {
      o[base + kStatusOff + i] = status_stacks(e.status_effects, kObsStatusOrder[i]);
    }
    // Intent. last_move is primed at combat start and each enemy turn, but
    // guard defensively (a freshly-spawned split child may not be primed yet).
    if (e.last_move.has_value()) {
      auto move_it = e.moves.find(*e.last_move);
      if (move_it != e.moves.end()) {
        const Move& m = move_it->second;
        const bool is_attacking = m.damage > 0;
        o[base + kIntentOff + 0] = is_attacking ? 1.0f : 0.0f;
        // Displayed attack damage (enemy Strength/Weak + player Vulnerable
        // factored in) — matches what the TUI shows.
        o[base + kIntentOff + 1] = is_attacking
                          ? static_cast<float>(compute_attack_damage(
                                m.damage, e.status_effects, c.status_effects))
                          : 0.0f;
        o[base + kIntentOff + 2] = m.block > 0 ? 1.0f : 0.0f;
        o[base + kIntentOff + 3] = !m.applies.empty() ? 1.0f : 0.0f;
      }
    }
  }

  // --- Pile counts per CardId: hand/draw/discard/exhaust, each a
  // kNumCardTypes-long count vector. Stride derives from kNumCardTypes so
  // adding a card type can't drift the per-pile offsets. ---
  constexpr int kPileBase = kEnemyBase + kMaxEnemies * kEnemyObsStride;
  constexpr int kStride = kNumCardTypes;
  for (std::size_t i = 0; i < kObsCardOrder.size(); ++i) {
    CardId id = kObsCardOrder[i];
    o[kPileBase + 0 * kStride + i] = static_cast<float>(pile_count(state_.current_hand, id));
    o[kPileBase + 1 * kStride + i] = static_cast<float>(pile_count(state_.draw_pile, id));
    o[kPileBase + 2 * kStride + i] = static_cast<float>(pile_count(state_.discard_pile, id));
    o[kPileBase + 3 * kStride + i] = static_cast<float>(pile_count(state_.exhaust_pile, id));
  }

  // --- Turn number (last slot) ---
  o[kObsSize - 1] = static_cast<float>(state_.turn_number);
}

void CombatEnv::compute_mask() {
  std::vector<bool> v = valid_actions(state_);
  // Mirror size guarantee — valid_actions returns num_card_ids + 1.
  // mask_buffer_ was sized to kNumActions in the constructor.
  for (int i = 0; i < kNumActions; ++i) {
    mask_buffer_[i] = (i < static_cast<int>(v.size()) && v[i]) ? 1 : 0;
  }
}

StatePiles CombatEnv::state_piles() const {
  StatePiles out;
  out.hand.reserve(state_.current_hand.size());
  out.discard.reserve(state_.discard_pile.size());
  out.exhaust.reserve(state_.exhaust_pile.size());
  for (const Card& c : state_.current_hand) out.hand.push_back(c.card_id);
  for (const Card& c : state_.discard_pile) out.discard.push_back(c.card_id);
  for (const Card& c : state_.exhaust_pile) out.exhaust.push_back(c.card_id);
  // Draw pile: tally per CardId. Order is not exposed (see StatePiles doc).
  for (const Card& c : state_.draw_pile) ++out.draw[c.card_id];
  return out;
}

std::vector<int> CombatEnv::enemy_max_hps() const {
  std::vector<int> out;
  out.reserve(state_.enemies.size());
  for (const Enemy& e : state_.enemies) out.push_back(e.max_hp);
  return out;
}

}  // namespace minispire
