"""Tests for the agent-driven watch() helper and render_mode='human'."""

import numpy as np
import pytest
from rich.console import Console

from minispire import _core
from minispire.env import MinispireEnv
from minispire.watch import random_policy, watch


def _quiet_console():
    # Render to a string buffer so tests don't spam the terminal.
    return Console(file=open("/dev/null", "w"), force_terminal=False)


def test_watch_random_policy_terminates():
    outcome = watch(random_policy, seed=0, delay=0.0, console=_quiet_console())
    assert outcome in (_core.Outcome.Won, _core.Outcome.Lost)


def test_watch_returns_outcome_for_end_turn_policy():
    # Policy that always ends the turn — should terminate (likely a loss).
    end_turn = MinispireEnv.NUM_ACTIONS - 1

    def always_end_turn(obs, mask):
        return end_turn

    outcome = watch(always_end_turn, seed=0, delay=0.0, console=_quiet_console())
    assert outcome in (_core.Outcome.Won, _core.Outcome.Lost)


def test_watch_deterministic_for_same_seed_and_policy():
    # A deterministic policy + same seed should give the same outcome.
    def first_legal(obs, mask):
        return int(np.flatnonzero(mask)[0])

    a = watch(first_legal, seed=3, console=_quiet_console())
    b = watch(first_legal, seed=3, console=_quiet_console())
    assert a == b


# ---------------------------------------------------------------------------
# render_mode='human'
# ---------------------------------------------------------------------------


def test_render_mode_human_render_runs():
    env = MinispireEnv(render_mode="human")
    # Swap in a quiet console so we don't spam the test output.
    env._console = _quiet_console()
    env.reset(seed=0)
    assert env.render() is None  # human mode returns None


def test_render_none_mode_is_noop():
    env = MinispireEnv()  # render_mode=None
    env.reset(seed=0)
    assert env.render() is None


def test_render_before_reset_raises():
    env = MinispireEnv(render_mode="human")
    with pytest.raises(RuntimeError):
        env.render()


def test_gym_render_loop():
    """The standard Gym render loop works with a callable policy."""
    env = MinispireEnv(render_mode="human")
    env._console = _quiet_console()
    obs, _ = env.reset(seed=0)
    for _ in range(200):
        mask = env.action_masks()
        action = int(np.flatnonzero(mask)[0])
        obs, _, terminated, truncated, _ = env.step(action)
        env.render()
        if terminated or truncated:
            break
    assert env.outcome in (_core.Outcome.Won, _core.Outcome.Lost)
