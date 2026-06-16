"""Random-action agent for ad-hoc env analysis.

Runs N episodes of uniformly-random valid actions against MinispireEnv and
reports summary stats. Used as a sanity-check artifact, not a benchmark —
random play is not expected to perform any particular way.

Usage:

    # As a library
    from minispire.random_agent import run_episodes
    stats = run_episodes(n=100, seed=0)

    # As a script (uses hardcoded defaults: n=100, seed=0)
    python -m minispire.random_agent
"""
from __future__ import annotations

import numpy as np

from minispire import MinispireEnv


def run_episodes(n: int = 100, seed: int = 0) -> dict:
    """Run n episodes of random valid actions; return summary stats.

    Args:
        n: number of episodes to play.
        seed: base seed. The action-selection RNG is seeded with `seed`; the
            env is reseeded each episode with `seed + episode_index` so every
            episode is reproducible.

    Returns:
        Dict with keys: n, wins, losses, win_rate, episode_lengths,
        mean_length, median_length, min_length, max_length.
    """
    env = MinispireEnv()
    rng = np.random.default_rng(seed)
    episode_lengths: list[int] = []
    wins = 0
    losses = 0

    for i in range(n):
        env.reset(seed=seed + i)
        steps = 0
        while True:
            mask = env.action_masks()
            valid = np.flatnonzero(mask)
            action = int(rng.choice(valid))
            _, reward, terminated, truncated, _ = env.step(action)
            steps += 1
            if terminated or truncated:
                if reward > 0:
                    wins += 1
                else:
                    losses += 1
                episode_lengths.append(steps)
                break

    return {
        "n": n,
        "wins": wins,
        "losses": losses,
        "win_rate": wins / n if n else 0.0,
        "episode_lengths": episode_lengths,
        "mean_length": float(np.mean(episode_lengths)),
        "median_length": float(np.median(episode_lengths)),
        "min_length": int(np.min(episode_lengths)),
        "max_length": int(np.max(episode_lengths)),
    }


if __name__ == "__main__":
    stats = run_episodes(n=100, seed=0)
    print(f"Episodes:      {stats['n']}")
    print(f"Wins:          {stats['wins']} ({stats['win_rate'] * 100:.1f}%)")
    print(f"Losses:        {stats['losses']}")
    print(f"Mean length:   {stats['mean_length']:.1f} turns")
    print(f"Median length: {stats['median_length']:.1f} turns")
    print(f"Min length:    {stats['min_length']} turns")
    print(f"Max length:    {stats['max_length']} turns")
