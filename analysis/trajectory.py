"""Replay a policy and analyze *what it learned* — behavioral trajectory stats.

The eval harness (ROB-54) reports outcomes (win rate, HP retained). This goes
a layer deeper: it replays episodes step-by-step, records the action taken and
the relevant game state (enemy intent), and aggregates qualitative behaviors
that reveal whether the agent plays *well*, e.g.:

  - Does it sequence Bash -> Strike to exploit Vulnerable?
  - Does it block on turns when the enemy intends to attack?

These numbers are the substance of the M1 blog's "what the agent learned"
section.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

import numpy as np

from minispire import _core
from minispire.env import MinispireEnv

Policy = Callable[[np.ndarray, np.ndarray], int]

# Obs slots we care about (ROB-40 layout).
_CHAR_BLOCK = 2
_INTENT_IS_ATTACKING = 16
_INTENT_ATTACK_DMG = 17

# Action index -> card name (the end-turn action is NUM_ACTIONS-1).
_END_TURN = MinispireEnv.NUM_ACTIONS - 1
_ACTION_NAMES = {
    int(_core.CardId.Strike): "Strike",
    int(_core.CardId.Defend): "Defend",
    int(_core.CardId.Bash): "Bash",
    int(_core.CardId.StrikePlus): "Strike+",
    int(_core.CardId.DefendPlus): "Defend+",
    int(_core.CardId.BashPlus): "Bash+",
    _END_TURN: "EndTurn",
}


def action_name(action: int) -> str:
    return _ACTION_NAMES.get(action, f"?{action}")


@dataclass
class Step:
    """One env step in a replayed episode."""

    action: int
    action_label: str
    # Enemy intent *before* this action resolved (what the agent saw).
    enemy_attacking: bool
    enemy_attack_dmg: int
    char_block: int


@dataclass
class Episode:
    seed: int
    steps: list[Step] = field(default_factory=list)
    won: bool = False
    final_hp: int = 0


def replay(policy: Policy, seed: int) -> Episode:
    """Replay one deterministic episode, recording per-step actions + state."""
    env = MinispireEnv()
    obs, _ = env.reset(seed=seed)
    ep = Episode(seed=seed)
    while True:
        mask = env.action_masks()
        action = int(policy(obs, mask))
        ep.steps.append(
            Step(
                action=action,
                action_label=action_name(action),
                enemy_attacking=bool(obs[_INTENT_IS_ATTACKING]),
                enemy_attack_dmg=int(obs[_INTENT_ATTACK_DMG]),
                char_block=int(obs[_CHAR_BLOCK]),
            )
        )
        obs, _reward, terminated, truncated, info = env.step(action)
        if terminated or truncated:
            ep.won = bool(info["won"])
            ep.final_hp = int(info["final_hp"])
            break
    return ep


def _bash_before_strike_rate(episodes: list[Episode]) -> float:
    """Fraction of episodes where the agent plays Bash before its first Strike.

    Bash applies Vulnerable; playing it before Strikes means subsequent Strikes
    hit harder. Only counts episodes that play both at least once.
    """
    qualifying = 0
    exploited = 0
    for ep in episodes:
        cards = [s.action_label for s in ep.steps]
        bash_idx = next(
            (i for i, c in enumerate(cards) if c in ("Bash", "Bash+")), None
        )
        strike_idx = next(
            (i for i, c in enumerate(cards) if c in ("Strike", "Strike+")), None
        )
        if bash_idx is None or strike_idx is None:
            continue
        qualifying += 1
        if bash_idx < strike_idx:
            exploited += 1
    return exploited / qualifying if qualifying else 0.0


def _defend_action_count(steps: list[Step]) -> int:
    return sum(1 for s in steps if s.action_label in ("Defend", "Defend+"))


def _defend_usage_rate(episodes: list[Episode]) -> float:
    """Fraction of episodes in which the agent plays Defend at least once."""
    if not episodes:
        return 0.0
    used = sum(1 for ep in episodes if _defend_action_count(ep.steps) > 0)
    return used / len(episodes)


def analyze(policy: Policy, n_seeds: int = 200) -> dict:
    """Replay n_seeds episodes and aggregate behavioral statistics."""
    episodes = [replay(policy, s) for s in range(n_seeds)]
    wins = [ep for ep in episodes if ep.won]

    card_plays: dict[str, int] = {}
    for ep in episodes:
        for s in ep.steps:
            card_plays[s.action_label] = card_plays.get(s.action_label, 0) + 1

    return {
        "n_seeds": n_seeds,
        "win_rate": len(wins) / n_seeds if n_seeds else 0.0,
        "bash_before_strike_rate": _bash_before_strike_rate(episodes),
        "defend_usage_rate": _defend_usage_rate(episodes),
        "mean_actions_per_episode": float(
            np.mean([len(ep.steps) for ep in episodes])
        )
        if episodes
        else 0.0,
        "card_play_counts": card_plays,
    }
