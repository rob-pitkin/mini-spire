"""Validation tests for TrainConfig (ROB-53). No training is run here."""

import pathlib

import pytest
from pydantic import ValidationError

from minispire.train_config import TrainConfig

CONFIGS_DIR = pathlib.Path(__file__).resolve().parents[2] / "configs"


def test_defaults_are_valid():
    cfg = TrainConfig()
    assert cfg.hp_reward_coeff == 0.0
    assert cfg.n_envs == 8
    assert cfg.total_timesteps > 0


def test_rejects_negative_hp_reward_coeff():
    with pytest.raises(ValidationError):
        TrainConfig(hp_reward_coeff=-1.0)


def test_rejects_nonpositive_n_steps():
    with pytest.raises(ValidationError):
        TrainConfig(n_steps=0)


def test_rejects_nonpositive_total_timesteps():
    with pytest.raises(ValidationError):
        TrainConfig(total_timesteps=-100)


def test_rejects_gamma_above_one():
    with pytest.raises(ValidationError):
        TrainConfig(gamma=1.5)


def test_rejects_zero_n_envs():
    with pytest.raises(ValidationError):
        TrainConfig(n_envs=0)


def test_rejects_unknown_key():
    # extra="forbid" — a misspelled field should fail at load.
    with pytest.raises(ValidationError):
        TrainConfig(lr=0.0003)  # should be learning_rate


def test_baseline_sparse_yaml_loads():
    cfg = TrainConfig.from_yaml(CONFIGS_DIR / "baseline_sparse.yaml")
    assert cfg.hp_reward_coeff == 0.0
    assert cfg.run_name == "baseline-sparse"


def test_baseline_shaped_yaml_loads():
    cfg = TrainConfig.from_yaml(CONFIGS_DIR / "baseline_shaped.yaml")
    assert cfg.hp_reward_coeff == 0.5
    assert cfg.run_name == "baseline-shaped"
