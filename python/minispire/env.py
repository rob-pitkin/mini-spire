"""Gymnasium wrapper around minispire._core.CombatEnv."""
from __future__ import annotations

import gymnasium as gym
import numpy as np

from minispire._core import CardId
from minispire._core import CombatEnv as _CombatEnv
from minispire._core import EncounterPool


class MinispireEnv(gym.Env):
    """Single-combat Slay the Spire environment wrapping the C++ engine.

    Action space is `Discrete(NUM_ACTIONS)` — indices 0..NUM_ACTIONS-2 play
    a card by CardId, the last index ends the turn (see ROB-35).

    Observation space is `Box(-inf, inf, shape=(OBS_SIZE,), dtype=float32)`
    — raw, unnormalized values per ROB-40.

    Use with sb3-contrib MaskablePPO: the env exposes `action_masks()` which
    MaskablePPO calls between policy forward passes.
    """

    metadata = {"render_modes": ["human"]}
    OBS_SIZE = _CombatEnv.OBS_SIZE
    NUM_ACTIONS = _CombatEnv.NUM_ACTIONS

    def __init__(
        self,
        hp_reward_coeff: float = 0.0,
        render_mode: str | None = None,
        pool: EncounterPool = EncounterPool.Weak,
        deck: list[CardId] | None = None,
    ):
        super().__init__()
        if render_mode is not None and render_mode not in self.metadata["render_modes"]:
            raise ValueError(
                f"render_mode={render_mode!r} is not supported. "
                f"Supported: {self.metadata['render_modes']}."
            )
        self.render_mode = render_mode
        # Reward-shaping hyperparameter (ROB-52). 0.0 = sparse +1/-1/0; >0 adds
        # an HP-retention bonus on wins. Fixed for the env's lifetime.
        self.hp_reward_coeff = hp_reward_coeff
        # Encounter pool + deck (ROB-66). deck=None -> Ironclad starter. Both
        # fixed for the env's lifetime; reset(seed) samples from the pool.
        self.pool = pool
        self._env = _CombatEnv(
            hp_reward_coeff=hp_reward_coeff, pool=pool, deck=deck or []
        )
        self._last_obs: np.ndarray | None = None
        self._console = None  # lazily created on first human render
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
        self._last_obs = obs
        return obs, info

    def step(self, action: int):
        obs, reward, terminated, truncated, info = self._env.step(int(action))
        self._last_obs = obs
        return obs, reward, terminated, truncated, info

    def render(self):
        """Render the current state. Only render_mode='human' is supported.

        Draws the fight panel via the rich renderer. Returns None per the
        Gymnasium human-mode convention.
        """
        if self.render_mode is None:
            return None
        if self._last_obs is None:
            raise RuntimeError("render() called before reset()")
        # Import here so the renderer (and rich) is only pulled in when
        # actually rendering, keeping the training import path lean.
        from rich.console import Console

        from minispire.render import screen

        if self._console is None:
            self._console = Console()
        screen.render_fight(self._console, self._last_obs, self)
        return None

    def action_masks(self) -> np.ndarray:
        """Return the boolean action mask. MaskablePPO calls this."""
        return self._env.action_mask()

    def state_piles(self):
        """Return pile contents (hand/draw/discard/exhaust). For the TUI /
        inspection — not used in the training loop. See ROB-46."""
        return self._env.state_piles()

    def enemy_max_hps(self):
        """Per-enemy-slot max HP (the obs omits enemy max_hp; ROB-59). For the
        TUI's enemy HP bars — not used in the training loop."""
        return self._env.enemy_max_hps()

    def enemy_kinds(self):
        """Per-enemy-slot EnemyKind (ROB-79). For the TUI to name each enemy —
        not used in the training loop."""
        return self._env.enemy_kinds()

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


def make_single_enemy_env(seed: int = 0) -> tuple[MinispireEnv, np.ndarray]:
    """A MinispireEnv fixed to the canonical single Jaw Worm fight (ROB-66).

    reset() now samples a random Act 1 encounter, so tests/tools that need a
    deterministic ONE-enemy fight use this instead. The returned env is already
    in the fixture state — do NOT call reset() (it would re-sample). Returns
    (env, obs).
    """
    from minispire._core import single_enemy_fixture_env

    env = MinispireEnv()
    env._env = single_enemy_fixture_env(seed)
    obs = np.asarray(env._env.obs())
    env._last_obs = obs
    return env, obs
