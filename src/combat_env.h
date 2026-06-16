#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "combat_state.h"

namespace minispire {

// CombatEnv wraps CombatState + TurnLoop into a Gym-shaped env that owns its
// observation and action-mask buffers. The buffers are stable (never
// reallocated) so the Python binding can expose them as zero-copy numpy
// views without lifetime hazards.
//
// Lifecycle: default-construct, then call reset(seed) before any step().
class CombatEnv {
 public:
  // The 45-float observation layout is defined in ROB-40.
  static constexpr int kObsSize = 45;

  // Action space: [0, num CardIds) play card by id; index num_card_ids ends
  // the turn. Currently 6 cards + end-turn = 7. Must match
  // CARD_DATABASE.size() + 1; static_assert in the .cc enforces this.
  static constexpr int kNumActions = 7;

  CombatEnv();

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

 private:
  CombatState state_;
  // Fixed-size array: pointer stays stable for the env's lifetime, so the
  // numpy view from Python can rely on it.
  std::array<float, kObsSize> obs_buffer_{};
  // uint8_t instead of bool — std::vector<bool> is bit-packed and can't be
  // exposed via the buffer protocol without copies. Sized once in the ctor.
  std::vector<uint8_t> mask_buffer_;
  float reward_ = 0.0f;

  void compute_obs();
  void compute_mask();
};

}  // namespace minispire
