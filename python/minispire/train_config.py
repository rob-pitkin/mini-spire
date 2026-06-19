"""Validated training configuration loaded from YAML (ROB-53).

A TrainConfig is parsed from a committed `configs/*.yaml` file and validated
by Pydantic on load, so a typo or out-of-range value fails fast with a clear
message instead of surfacing thousands of steps into a run.
"""
from __future__ import annotations

import pathlib

import yaml
from pydantic import BaseModel, Field


class TrainConfig(BaseModel):
    """Hyperparameters + run config for a MaskablePPO training run.

    Extra YAML keys are rejected (extra="forbid") so a misspelled field is a
    load-time error, not a silently ignored setting.
    """

    model_config = {"extra": "forbid"}

    # --- Experiment identity ---
    run_name: str = "minispire-ppo"
    wandb_project: str = "mini-spire"
    seed: int = 0

    # --- Env / reward ---
    n_envs: int = Field(8, gt=0)
    hp_reward_coeff: float = Field(0.0, ge=0.0)

    # --- PPO hyperparameters ---
    total_timesteps: int = Field(200_000, gt=0)
    learning_rate: float = Field(3e-4, gt=0.0)
    n_steps: int = Field(2048, gt=0)
    batch_size: int = Field(64, gt=0)
    n_epochs: int = Field(10, gt=0)
    gamma: float = Field(0.99, gt=0.0, le=1.0)

    # --- Checkpointing ---
    # Save a checkpoint every `save_freq` *env* steps (per the CheckpointCallback
    # convention). 0 disables periodic checkpoints (final save still happens).
    save_freq: int = Field(50_000, ge=0)

    @classmethod
    def from_yaml(cls, path: str | pathlib.Path) -> "TrainConfig":
        """Load and validate a TrainConfig from a YAML file."""
        with open(path) as f:
            data = yaml.safe_load(f) or {}
        return cls(**data)
