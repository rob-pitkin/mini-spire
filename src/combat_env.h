#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "card.h"
#include "combat_state.h"

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

// CombatEnv wraps CombatState + TurnLoop into a Gym-shaped env that owns its
// observation and action-mask buffers. The buffers are stable (never
// reallocated) so the Python binding can expose them as zero-copy numpy
// views without lifetime hazards.
//
// Lifecycle: default-construct, then call reset(seed) before any step().
class CombatEnv {
 public:
  // Observation layout (ROB-40 + ROB-59 multi-enemy). Flat float32 vector:
  //   player        [0:5]   hp, max_hp, block, energy, energy_per_turn
  //   player status [5:9]   Vulnerable, Weak, Strength, Dexterity
  //   enemies       [9 : 9 + kMaxEnemies*kEnemyObsStride]  kMaxEnemies blocks
  //   piles         [.. : .. + 24]  hand/draw/discard/exhaust x 6 card types
  //   turn          [last]
  // Each enemy block (kEnemyObsStride floats): is_alive, hp, block,
  // status(4: V/W/S/D), intent(4: is_attacking, atk_dmg, is_blocking,
  // is_buffing). NOTE: enemies intentionally omit max_hp (redundant for the
  // policy — current hp gives lethality, intent gives threat; ROB-59). The
  // player keeps max_hp (fixed run-level anchor + HP-shaping reward).
  static constexpr int kEnemyObsStride = 11;
  static constexpr int kObsSize =
      5 + 4 + kMaxEnemies * kEnemyObsStride + 24 + 1;  // = 78 at N=4

  // Action space: (card x target) cross-product + end-turn (ROB-60).
  //   action = card_idx * kMaxEnemies + enemy_idx   for card_idx in [0,6)
  //   end_turn = num_card_ids * kMaxEnemies          (last index)
  // num_card_ids * kMaxEnemies + 1. Currently 6*4 + 1 = 25. Enforced by
  // static_assert in the .cc against CARD_DATABASE.size().
  static constexpr int kNumActions = 6 * kMaxEnemies + 1;

  // hp_reward_coeff is a per-env reward-shaping hyperparameter, fixed for the
  // env's lifetime. On a win the reward is 1 + coeff * (final_hp / max_hp);
  // coeff = 0 (default) is the pure sparse +1/-1/0 signal. See ROB-52.
  // Debug builds assert coeff >= 0 (a negative bonus is meaningless).
  explicit CombatEnv(float hp_reward_coeff = 0.0f);

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

  void compute_obs();
  void compute_mask();
};

}  // namespace minispire
