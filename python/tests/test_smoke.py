"""Smoke tests that verify the build + import surface, plus CombatEnv basics."""

import numpy as np
import pytest

import minispire


def test_package_imports():
    assert minispire.__version__ == "0.1.0"


def test_core_extension_loadable():
    from minispire import _core
    assert _core is not None


def test_combat_env_constructs_and_resets():
    env = minispire._core.CombatEnv()
    obs, info = env.reset(seed=0)
    assert obs.shape == (env.OBS_SIZE,)
    assert obs.shape == (45,)
    assert obs.dtype == np.float32
    assert isinstance(info, dict)


def test_action_mask_shape_and_dtype():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    mask = env.action_mask()
    assert mask.shape == (env.NUM_ACTIONS,)
    assert mask.shape == (7,)
    assert mask.dtype == np.bool_


def test_end_turn_always_legal_at_start():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    end_turn = env.NUM_ACTIONS - 1
    assert env.action_mask()[end_turn]


def test_step_returns_gym_tuple():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    end_turn = env.NUM_ACTIONS - 1
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


def test_step_raises_on_invalid_action():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    with pytest.raises(ValueError):
        env.step(-1)
    with pytest.raises(ValueError):
        env.step(env.NUM_ACTIONS)


def test_clone_produces_independent_env():
    env = minispire._core.CombatEnv()
    env.reset(seed=7)
    copy = env.clone()
    end_turn = env.NUM_ACTIONS - 1
    copy.step(end_turn)
    # Original unaffected.
    assert env.turn_number == 1
    assert copy.turn_number == 2


def test_obs_slot_zero_is_character_hp():
    """Per ROB-40 layout: slot 0 = character.hp, slot 1 = max_hp."""
    env = minispire._core.CombatEnv()
    obs, _ = env.reset(seed=0)
    assert obs[0] == 80.0  # Ironclad starting HP
    assert obs[1] == 80.0  # max HP


def test_obs_view_reflects_state_after_step():
    env = minispire._core.CombatEnv()
    obs, _ = env.reset(seed=0)
    hp_before = float(obs[0])
    end_turn = env.NUM_ACTIONS - 1
    new_obs, _, _, _, _ = env.step(end_turn)
    # Character either took damage or has block; HP shouldn't increase
    # without a heal effect (none in v1).
    assert float(new_obs[0]) <= hp_before
