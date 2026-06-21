"""Watch a policy play a Mini-Spire fight with the rich renderer.

Unlike the human TUI (`play.py`), this drives the fight from a callable
policy instead of stdin — useful for spectating a trained agent. No input
loop; renders each step as the policy chooses actions.

A policy is any callable `fn(obs, action_mask) -> int` returning a *legal*
action (one whose mask entry is True). `watch` does not re-sample illegal
actions — that's the policy's contract; an illegal action surfaces as the
ValueError that env.step raises, which helps catch buggy policies.
"""
from __future__ import annotations

import time
from typing import Callable

import numpy as np
from rich.console import Console

from minispire import _core
from minispire.env import MinispireEnv
from minispire.render import screen

Policy = Callable[[np.ndarray, np.ndarray], int]


def random_policy(obs: np.ndarray, mask: np.ndarray) -> int:
    """Convenience policy: pick a uniformly random legal action.

    Seeded by the global numpy RNG; for reproducible demos seed numpy first
    via np.random.seed(...) or pass your own policy.
    """
    valid = np.flatnonzero(mask)
    return int(np.random.choice(valid))


def watch(
    policy: Policy,
    seed: int = 0,
    *,
    delay: float = 0.0,
    ascii_only: bool = False,
    console: Console | None = None,
) -> "_core.Outcome":
    """Run a full fight driven by `policy`, rendering each step.

    Args:
        policy: callable mapping (obs, action_mask) -> legal action index.
        seed: env seed.
        delay: seconds to sleep between rendered steps (0 = instant).
        ascii_only: use plain-ASCII intent icons.
        console: optional rich Console (defaults to a fresh one).

    Returns:
        The final Outcome (Won / Lost).
    """
    console = console or Console()
    env = MinispireEnv()
    obs, _info = env.reset(seed=seed)

    while env.outcome == _core.Outcome.InProgress:
        screen.render_fight(console, obs, env, ascii_only=ascii_only)
        mask = env.action_masks()
        action = int(policy(obs, mask))
        obs, _reward, _terminated, _truncated, _info = env.step(action)
        if delay:
            time.sleep(delay)

    screen.render_end_screen(console, obs, env, env.outcome, log_path=None)
    return env.outcome
