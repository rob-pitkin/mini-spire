"""Integration tests across the pybind11 -> Gymnasium boundary."""

import numpy as np
import pytest

from minispire import MinispireEnv


def test_clone_through_wrapper_is_independent():
    """Cloning the wrapper's underlying _env produces an independent env."""
    env = MinispireEnv()
    env.reset(seed=7)

    cloned_core = env._env.clone()
    end_turn = MinispireEnv.NUM_ACTIONS - 1
    cloned_core.step(end_turn)

    # Original wrapper's view is unchanged.
    assert env.turn_number == 1
    assert cloned_core.turn_number == 2


def test_action_mask_always_has_legal_actions_while_in_progress():
    """Every step before terminal must offer at least one legal action."""
    env = MinispireEnv()
    env.reset(seed=0)
    rng = np.random.default_rng(0)
    for _ in range(200):
        mask = env.action_masks()
        assert mask.any(), "Mask has no legal actions while InProgress"
        valid = np.flatnonzero(mask)
        action = int(rng.choice(valid))
        _, _, terminated, truncated, _ = env.step(action)
        if terminated or truncated:
            return
    pytest.fail("Rollout did not terminate within 200 steps")


def test_run_episodes_returns_expected_keys():
    """run_episodes() produces a stats dict with the expected shape."""
    from minispire.random_agent import run_episodes

    stats = run_episodes(n=3, seed=0)
    assert stats["n"] == 3
    assert stats["wins"] + stats["losses"] == 3
    assert 0.0 <= stats["win_rate"] <= 1.0
    assert len(stats["episode_lengths"]) == 3
    assert stats["min_length"] >= 1
    assert stats["max_length"] >= stats["min_length"]
