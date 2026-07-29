#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "card.h"
#include "combat_state.h"
#include "encounter.h"
#include "turn_loop.h"  // action-layout constants (kTotalActions, ...)

namespace minispire {

// Pile contents accessor returned by CombatEnv::state_piles(). Used by the
// Python TUI (ROB-47) to render the pile-view. Not called per training step;
// allocation overhead is acceptable.
//
// Hand / discard / exhaust are ordered lists because the player sees them
// in order in real STS (hand left-to-right, discard top-most = most recently
// played, exhaust similarly).
//
// Draw is a **count map** because real STS shows the draw pile as a shuffled
// peek — the player can see *what* is in the draw pile but never the order.
// Exposing the engine's ordered draw_pile vector would leak the secret
// shuffle order to any consumer.
struct StatePiles {
  std::vector<CardId> hand;
  std::unordered_map<CardId, int> draw;
  std::vector<CardId> discard;
  std::vector<CardId> exhaust;
};

// Pending-choice accessor for the TUI (Stage 4c). The obs already encodes this
// for the agent; the TUI wants it as named values rather than float offsets.
// `options[i]` is answered with action FIRST_OPTION_SLOT + i.
struct ChoiceView {
  bool active = false;
  ChoiceKind kind = ChoiceKind::None;
  CardId source_card = CardId::Strike;
  bool is_optional = false;
  int copies = 1;
  std::vector<CardId> options;
  // Per-option effective damage, parallel to `options`. Lets the TUI show two
  // Rampages apart ("Rampage (8)" vs "Rampage (13)"); 0 for non-attacks.
  std::vector<int> option_damage;
};

// CombatEnv wraps CombatState + TurnLoop into a Gym-shaped env that owns its
// observation and action-mask buffers. The buffers are stable (never
// reallocated) so the Python binding can expose them as zero-copy numpy
// views without lifetime hazards.
//
// Lifecycle: default-construct, then call reset(seed) before any step().
class CombatEnv {
 public:
  // Observation layout (ROB-40 + ROB-59 multi-enemy). Flat float32 vector:
  //   player        [0:7]   hp, max_hp, block, energy, energy_per_turn,
  //                         hp_loss_events, combust_casts
  //   player status [7 : 7 + kPlayerStatusSize]   debuffs then ALL powers
  //   enemies       [.. : .. + kMaxEnemies*kEnemyObsStride]  kMaxEnemies blocks
  //   piles         [.. : .. + 5*kNumCardTypes]  hand/draw/discard/exhaust,
  //                         then free-this-turn
  //   turn          [last]
  // Each enemy block (kEnemyObsStride floats): is_alive, hp, block,
  // status(kEnemyStatusSize), intent(4: is_attacking, atk_dmg, is_blocking,
  // is_buffing). NOTE: enemies intentionally omit max_hp (redundant for the
  // policy — current hp gives lethality, intent gives threat; ROB-59). The
  // player keeps max_hp (fixed run-level anchor + HP-shaping reward).
  //
  // Per-entity status widths (Stage 4a): both blocks are [debuffs then
  // powers], but only the player carries the player-only powers (Demon Form,
  // Juggernaut, ...) — in an enemy block those would be always-zero floats.
  static constexpr int kPlayerStatusSize = kNumDebuffs + kNumPlayerPowers;
  static constexpr int kEnemyStatusSize = kNumDebuffs + kNumEnemyPowers;
  // hp, max_hp, block, energy, energy_per_turn, then the two query-layer
  // counters a human can read off the cards (ROB-40 B1): hp_loss_events
  // (Blood for Blood's displayed cost) and combust_casts (Combust's tooltip).
  static constexpr int kPlayerBaseSize = 7;
  static constexpr int kPlayerObsSize = kPlayerBaseSize + kPlayerStatusSize;
  static constexpr int kEnemyIntentSize = 4;
  static constexpr int kEnemyObsStride =
      3 + kEnemyStatusSize + kEnemyIntentSize;  // is_alive,hp,block + status + intent
  // 5 planes, each a per-card-type count vector: the four piles
  // (hand/draw/discard/exhaust) plus the free-this-turn discount plane
  // (ROB-40 B3) — "how many copies of type T cost 0 for the rest of this turn".
  // Almost always all-zero; without it the agent is told a card costs 1, pays 0,
  // and has no way to see why.
  static constexpr int kNumPilePlanes = 5;
  static constexpr int kPileObsSize = kNumPilePlanes * kNumCardTypes;

  // Choice block (Stage 4c; docs/design/decision-points.md §5.2). A 5-float
  // header plus one 3-float descriptor per option slot. Appended last so every
  // existing feature index keeps its position.
  //   header: pending, kind, source_pile, source_card, is_optional
  //   slot:   occupied, payload_id, cost
  // The payload fields are reserved NOW and always written (zero for card
  // choices) so v2.0.0's map/shop/event decisions are pure data — new
  // ChoiceKind values, no obs shape change.
  static constexpr int kChoiceHeaderSize = 5;
  static constexpr int kChoiceSlotStride = 3;
  static constexpr int kChoiceObsSize =
      kChoiceHeaderSize + kNumOptionSlots * kChoiceSlotStride;

  static constexpr int kObsSize = kPlayerObsSize +
                                  kMaxEnemies * kEnemyObsStride + kPileObsSize +
                                  1 + kChoiceObsSize;

  // Action space: the combat block (card x target + end-turn, ROB-60) plus the
  // Stage 4c option-slot channel. Layout constants live in turn_loop.h next to
  // decode_action, so the env and the engine cannot disagree about it.
  static constexpr int kNumActions = kTotalActions;

  // hp_reward_coeff is a per-env reward-shaping hyperparameter, fixed for the
  // env's lifetime. On a win the reward is 1 + coeff * (final_hp / max_hp);
  // coeff = 0 (default) is the pure sparse +1/-1/0 signal. See ROB-52.
  // Debug builds assert coeff >= 0 (a negative bonus is meaningless).
  //
  // `pool` selects which Act 1 encounter table reset() samples from; `deck` is
  // the player's deck (empty -> the Ironclad starter). Both fixed for the env's
  // lifetime (ROB-66).
  explicit CombatEnv(float hp_reward_coeff = 0.0f,
                     EncounterPool pool = EncounterPool::Weak,
                     std::vector<Card> deck = {});

  // Construct directly from an existing CombatState. Computes the obs/mask
  // buffers from the given state so they're immediately consistent. This is
  // the entry point for restoring a serialized state, wrapping a mid-fight
  // state for MCTS rollouts, and building deterministic test scenarios.
  explicit CombatEnv(CombatState state, float hp_reward_coeff = 0.0f);

  // Initialize from start_v1_combat(seed). Refreshes obs and mask buffers.
  void reset(uint32_t seed);

  // Apply the action. Throws std::invalid_argument if out of range or
  // masked-off. Refreshes obs and mask buffers. Sets reward_ based on the
  // resulting outcome (+1 Won, -1 Lost, 0 InProgress).
  void step(int action);

  // Reward for the most recent step (0 immediately after reset).
  float reward() const { return reward_; }

  bool terminated() const { return state_.outcome != Outcome::InProgress; }

  // No time limit in v1. Future-proofing for benchmark-mode turn caps.
  bool truncated() const { return false; }

  // Zero-copy buffer accessors for the pybind11 layer.
  const std::array<float, kObsSize>& obs() const { return obs_buffer_; }
  const std::vector<uint8_t>& action_mask() const { return mask_buffer_; }

  // Deep-copy clone for MCTS. Compiler-generated copy is correct: CombatState
  // is value-typed (ROB-32), buffers are value-typed.
  CombatEnv clone() const { return *this; }

  // Read-only access to internals for inspection / rendering.
  const CombatState& state() const { return state_; }
  Outcome outcome() const { return state_.outcome; }
  int turn_number() const { return state_.turn_number; }

  // Pile contents — for the Python TUI pile-view. Not used during training.
  StatePiles state_piles() const;

  // The pending mid-card choice, if any (Stage 4c). For the TUI; not called
  // per training step.
  ChoiceView choice_view() const;

  // Per-enemy-slot max HP, in slot order. The observation intentionally omits
  // enemy max_hp (ROB-59), but the TUI needs it to draw enemy HP bars. Debug
  // accessor only — not used during training.
  std::vector<int> enemy_max_hps() const;

  // Per-enemy-slot kind, in slot order (ROB-79). The obs doesn't carry kind
  // (not a stat); the TUI needs it to show each enemy's real name. Debug only.
  std::vector<EnemyKind> enemy_kinds() const;

 private:
  CombatState state_;
  // Fixed-size array: pointer stays stable for the env's lifetime, so the
  // numpy view from Python can rely on it.
  std::array<float, kObsSize> obs_buffer_{};
  // uint8_t instead of bool — std::vector<bool> is bit-packed and can't be
  // exposed via the buffer protocol without copies. Sized once in the ctor.
  std::vector<uint8_t> mask_buffer_;
  float reward_ = 0.0f;
  float hp_reward_coeff_ = 0.0f;
  // Encounter pool + deck used by reset() (ROB-66). Fixed for the env's
  // lifetime, like hp_reward_coeff_.
  EncounterPool pool_ = EncounterPool::Weak;
  std::vector<Card> deck_;

  void compute_obs();
  void compute_mask();
};

}  // namespace minispire
