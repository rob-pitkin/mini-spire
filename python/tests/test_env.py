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
    assert env.observation_space.shape == (MinispireEnv.OBS_SIZE,)
    assert env.observation_space.dtype == np.float32


def test_action_space_size():
    env = MinispireEnv()
    assert isinstance(env.action_space, gym.spaces.Discrete)
    assert env.action_space.n == MinispireEnv.NUM_ACTIONS


def test_class_constants_match_core():
    assert MinispireEnv.OBS_SIZE == minispire._core.CombatEnv.OBS_SIZE
    assert MinispireEnv.NUM_ACTIONS == minispire._core.CombatEnv.NUM_ACTIONS


# ---------------------------------------------------------------------------
# Reward shaping (ROB-52)
# ---------------------------------------------------------------------------


def test_hp_reward_coeff_defaults_to_zero():
    env = MinispireEnv()
    assert env.hp_reward_coeff == 0.0


def test_hp_reward_coeff_stored():
    env = MinispireEnv(hp_reward_coeff=0.5)
    assert env.hp_reward_coeff == 0.5


def _play_to_win_with_seed(env, seed, max_steps=500):
    """Greedy attack policy: prefer damage cards, else first legal, else end
    turn. Returns the final reward if won, else None."""
    damage_actions = [
        int(minispire._core.CardId.Strike),
        int(minispire._core.CardId.Bash),
        int(minispire._core.CardId.StrikePlus),
        int(minispire._core.CardId.BashPlus),
    ]
    end_turn = MinispireEnv.NUM_ACTIONS - 1
    obs, _ = env.reset(seed=seed)
    last_reward = 0.0
    for _ in range(max_steps):
        mask = env.action_masks()
        action = end_turn
        for a in damage_actions:
            if mask[a]:
                action = a
                break
        if action == end_turn:
            legal = np.flatnonzero(mask)
            action = int(legal[0]) if len(legal) else end_turn
        obs, last_reward, terminated, truncated, info = env.step(action)
        if terminated or truncated:
            won = info["outcome"] == minispire._core.Outcome.Won
            return last_reward if won else None
    return None


def test_sparse_win_reward_is_one():
    # Default coeff: a won fight rewards exactly 1.0.
    for seed in range(50):
        env = MinispireEnv()
        reward = _play_to_win_with_seed(env, seed)
        if reward is not None:
            assert reward == 1.0
            return
    pytest.skip("No win in 50 seeds for the greedy policy")


def test_shaped_win_reward_exceeds_one():
    # coeff 0.5: a won fight rewards in (1.0, 1.5].
    for seed in range(50):
        env = MinispireEnv(hp_reward_coeff=0.5)
        reward = _play_to_win_with_seed(env, seed)
        if reward is not None:
            assert 1.0 < reward <= 1.5
            return
    pytest.skip("No win in 50 seeds for the greedy policy")


# ---------------------------------------------------------------------------
# reset / step
# ---------------------------------------------------------------------------


def test_reset_returns_obs_and_info():
    env = MinispireEnv()
    obs, info = env.reset(seed=42)
    assert isinstance(obs, np.ndarray)
    assert obs.shape == (MinispireEnv.OBS_SIZE,)
    assert obs.dtype == np.float32
    assert isinstance(info, dict)


def test_reset_with_no_seed_works():
    env = MinispireEnv()
    obs, info = env.reset()
    assert obs.shape == (MinispireEnv.OBS_SIZE,)


def test_reset_seed_passes_through_to_cpp():
    """Same Python seed must produce identical first obs."""
    env_a = MinispireEnv()
    env_b = MinispireEnv()
    obs_a, _ = env_a.reset(seed=42)
    obs_b, _ = env_b.reset(seed=42)
    np.testing.assert_array_equal(obs_a, obs_b)


def test_reset_different_seeds_produce_different_trajectories():
    """Different seeds produce different enemy HP rolls (most of the time)."""
    # Enemy 0 HP = player block + is_alive(1). Derive from constants so this
    # tracks obs layout changes (ROB-59/72/73).
    enemy0_hp = minispire._core.CombatEnv.PLAYER_OBS_SIZE + 1
    obs_seeds = []
    for s in [0, 1, 2, 3, 4, 5]:
        env = MinispireEnv()
        obs, _ = env.reset(seed=s)
        obs_seeds.append(obs[enemy0_hp])
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
    assert obs.shape == (MinispireEnv.OBS_SIZE,)
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
    assert obs.shape == (MinispireEnv.OBS_SIZE,)


# ---------------------------------------------------------------------------
# action_masks (MaskablePPO convention)
# ---------------------------------------------------------------------------


def test_action_masks_returns_bool_array():
    env = MinispireEnv()
    env.reset(seed=0)
    mask = env.action_masks()
    assert isinstance(mask, np.ndarray)
    assert mask.shape == (MinispireEnv.NUM_ACTIONS,)
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
    assert obs.shape == (MinispireEnv.OBS_SIZE,)


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
