"""Gymnasium wrapper around minispire._core.CombatEnv."""
from __future__ import annotations

import gymnasium as gym
import numpy as np

from minispire._core import CombatEnv as _CombatEnv


class MinispireEnv(gym.Env):
    """Single-combat Slay the Spire environment wrapping the C++ engine.

    Action space is `Discrete(NUM_ACTIONS)` — indices 0..NUM_ACTIONS-2 play
    a card by CardId, the last index ends the turn (see ROB-35).

    Observation space is `Box(-inf, inf, shape=(OBS_SIZE,), dtype=float32)`
    — raw, unnormalized values per ROB-40.

    Use with sb3-contrib MaskablePPO: the env exposes `action_masks()` which
    MaskablePPO calls between policy forward passes.
    """

    metadata = {"render_modes": []}
    OBS_SIZE = _CombatEnv.OBS_SIZE
    NUM_ACTIONS = _CombatEnv.NUM_ACTIONS

    def __init__(self, render_mode: str | None = None):
        super().__init__()
        # ROB-45 will add a Python TUI for human play. Until then, render
        # modes are unsupported — fail loudly rather than silently no-op.
        if render_mode is not None:
            raise ValueError(
                f"render_mode={render_mode!r} is not yet supported. "
                "ROB-45 will add a Python TUI."
            )
        self.render_mode = render_mode
        self._env = _CombatEnv()
        self.observation_space = gym.spaces.Box(
            low=-np.inf,
            high=np.inf,
            shape=(self.OBS_SIZE,),
            dtype=np.float32,
        )
        self.action_space = gym.spaces.Discrete(self.NUM_ACTIONS)

    def reset(self, *, seed: int | None = None, options: dict | None = None):
        # super().reset(seed=seed) initializes self.np_random per Gymnasium's
        # contract. We don't use np_random for actual env randomness — the
        # C++ engine has its own seeded mt19937 — but env_checker validates
        # we make the call.
        super().reset(seed=seed)
        if seed is not None:
            cpp_seed = seed
        else:
            cpp_seed = int(self.np_random.integers(0, 2**32 - 1))
        obs, info = self._env.reset(cpp_seed)
        return obs, info

    def step(self, action: int):
        obs, reward, terminated, truncated, info = self._env.step(int(action))
        return obs, reward, terminated, truncated, info

    def action_masks(self) -> np.ndarray:
        """Return the boolean action mask. MaskablePPO calls this."""
        return self._env.action_mask()

    def state_piles(self):
        """Return pile contents (hand/draw/discard/exhaust). For the TUI /
        inspection — not used in the training loop. See ROB-46."""
        return self._env.state_piles()

    @property
    def outcome(self):
        return self._env.outcome

    @property
    def turn_number(self) -> int:
        return self._env.turn_number

    @property
    def reward(self) -> float:
        return self._env.reward

    # close() intentionally not overridden — the default no-op is correct
    # (no file handles, no network, no subprocesses). When ROB-45 adds a
    # TUI, close() may need to override to clean up terminal state.
