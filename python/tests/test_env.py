"""Tests for the MinispireEnv Gymnasium wrapper."""

import gymnasium as gym
import numpy as np
import pytest

import minispire
from minispire import MinispireEnv


# ---------------------------------------------------------------------------
# Construction
# ---------------------------------------------------------------------------


def test_instantiates_with_no_args():
    env = MinispireEnv()
    assert env is not None
    assert env.render_mode is None


def test_render_mode_human_accepted():
    env = MinispireEnv(render_mode="human")
    assert env.render_mode == "human"


def test_render_mode_unknown_raises():
    with pytest.raises(ValueError):
        MinispireEnv(render_mode="rgb_array")


# ---------------------------------------------------------------------------
# Spaces
# ---------------------------------------------------------------------------


def test_observation_space_shape_and_dtype():
    env = MinispireEnv()
    assert env.observation_space.shape == (45,)
    assert env.observation_space.dtype == np.float32


def test_action_space_size():
    env = MinispireEnv()
    assert isinstance(env.action_space, gym.spaces.Discrete)
    assert env.action_space.n == 7


def test_class_constants_match_core():
    assert MinispireEnv.OBS_SIZE == minispire._core.CombatEnv.OBS_SIZE
    assert MinispireEnv.NUM_ACTIONS == minispire._core.CombatEnv.NUM_ACTIONS


# ---------------------------------------------------------------------------
# reset / step
# ---------------------------------------------------------------------------


def test_reset_returns_obs_and_info():
    env = MinispireEnv()
    obs, info = env.reset(seed=42)
    assert isinstance(obs, np.ndarray)
    assert obs.shape == (45,)
    assert obs.dtype == np.float32
    assert isinstance(info, dict)


def test_reset_with_no_seed_works():
    env = MinispireEnv()
    obs, info = env.reset()
    assert obs.shape == (45,)


def test_reset_seed_passes_through_to_cpp():
    """Same Python seed must produce identical first obs."""
    env_a = MinispireEnv()
    env_b = MinispireEnv()
    obs_a, _ = env_a.reset(seed=42)
    obs_b, _ = env_b.reset(seed=42)
    np.testing.assert_array_equal(obs_a, obs_b)


def test_reset_different_seeds_produce_different_trajectories():
    """Different seeds produce different enemy HP rolls (most of the time)."""
    obs_seeds = []
    for s in [0, 1, 2, 3, 4, 5]:
        env = MinispireEnv()
        obs, _ = env.reset(seed=s)
        obs_seeds.append(obs[9])  # enemy HP
    # At least some seeds give different HPs (HP roll is in [40, 44]).
    assert len(set(obs_seeds)) > 1


def test_seeded_trajectory_determinism():
    """Same seed + same action sequence produces identical trajectories."""
    end_turn = MinispireEnv.NUM_ACTIONS - 1
    actions = [end_turn] * 3

    env_a = MinispireEnv()
    obs_a, _ = env_a.reset(seed=7)
    final_obs_a = obs_a
    for a in actions:
        final_obs_a, _, _, _, _ = env_a.step(a)

    env_b = MinispireEnv()
    obs_b, _ = env_b.reset(seed=7)
    final_obs_b = obs_b
    for a in actions:
        final_obs_b, _, _, _, _ = env_b.step(a)

    np.testing.assert_array_equal(final_obs_a, final_obs_b)


def test_step_returns_5_tuple():
    env = MinispireEnv()
    env.reset(seed=0)
    end_turn = MinispireEnv.NUM_ACTIONS - 1
    result = env.step(end_turn)
    assert len(result) == 5
    obs, reward, terminated, truncated, info = result
    assert obs.shape == (45,)
    assert obs.dtype == np.float32
    assert isinstance(reward, float)
    assert isinstance(terminated, bool)
    assert isinstance(truncated, bool)
    assert "outcome" in info
    assert "turn_number" in info


def test_step_accepts_numpy_int():
    """Agents often pass np.int64 actions; wrapper must handle that."""
    env = MinispireEnv()
    env.reset(seed=0)
    end_turn = np.int64(MinispireEnv.NUM_ACTIONS - 1)
    obs, _, _, _, _ = env.step(end_turn)
    assert obs.shape == (45,)


# ---------------------------------------------------------------------------
# action_masks (MaskablePPO convention)
# ---------------------------------------------------------------------------


def test_action_masks_returns_bool_array():
    env = MinispireEnv()
    env.reset(seed=0)
    mask = env.action_masks()
    assert isinstance(mask, np.ndarray)
    assert mask.shape == (7,)
    assert mask.dtype == np.bool_


def test_action_masks_end_turn_always_legal():
    env = MinispireEnv()
    env.reset(seed=0)
    end_turn = MinispireEnv.NUM_ACTIONS - 1
    assert env.action_masks()[end_turn]


# ---------------------------------------------------------------------------
# Properties
# ---------------------------------------------------------------------------


def test_properties_after_reset():
    env = MinispireEnv()
    env.reset(seed=0)
    assert env.outcome == minispire._core.Outcome.InProgress
    assert env.turn_number == 1
    assert env.reward == 0.0


# ---------------------------------------------------------------------------
# gym.make / registration
# ---------------------------------------------------------------------------


def test_gym_make_works():
    env = gym.make("Minispire-v0")
    obs, _ = env.reset(seed=0)
    # gym.make wraps in OrderEnforcing/TimeLimit etc., but obs comes through.
    assert obs.shape == (45,)


# ---------------------------------------------------------------------------
# Gymnasium env_checker — intentionally NOT run.
#
# `gymnasium.utils.env_checker.check_env` samples random actions from the full
# action_space, ignoring the action mask. Our step() raises on masked actions
# (per ROB-41 design), so check_env will flake whenever it happens to sample
# an illegal action. The check is designed for envs without masking.
#
# Our explicit per-aspect tests (spaces, reset/step shape, determinism,
# action_masks, gym.make registration) provide stronger conformance guarantees
# specific to the masked-env design. MaskablePPO doesn't use check_env either.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Random rollout
# ---------------------------------------------------------------------------


def test_random_rollout_terminates():
    """Random valid actions reach a terminal state in finite steps."""
    env = MinispireEnv()
    obs, _ = env.reset(seed=0)
    rng = np.random.default_rng(0)
    for _ in range(200):
        mask = env.action_masks()
        valid_actions = np.flatnonzero(mask)
        assert len(valid_actions) > 0, "No legal actions while InProgress"
        action = int(rng.choice(valid_actions))
        obs, reward, terminated, truncated, info = env.step(action)
        if terminated or truncated:
            assert reward in (-1.0, 1.0)
            return
    pytest.fail("Random rollout did not terminate within 200 steps")
