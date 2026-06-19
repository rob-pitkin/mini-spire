"""Evaluate a policy deterministically over a fixed seed set (ROB-54).

This is how M1 is measured: load a trained policy, run it over seeds
0..N-1 with deterministic action selection, and report win rate + HP
retained on wins vs the 52.8% random baseline.

Algorithm-agnostic: the core takes a `policy(obs, mask) -> action` callable
(same convention as watch() / ROB-50), so Phase 5's DQN / MCTS reuse the
same harness + seed set. A --checkpoint convenience path loads an SB3
MaskablePPO model and wraps it.

Run:
    minispire-eval --checkpoint checkpoints/<run>/final.zip --n-seeds 1000
"""
from __future__ import annotations

import argparse
import json
import pathlib
import statistics
from typing import Callable

import numpy as np

from minispire import _core
from minispire.env import MinispireEnv

Policy = Callable[[np.ndarray, np.ndarray], int]

# The random-play floor (measured, n=1000, seed 0). See ROB-43 / ROB-51.
RANDOM_BASELINE_WIN_RATE = 0.528


def evaluate(
    policy: Policy,
    n_seeds: int = 1000,
    *,
    hp_reward_coeff: float = 0.0,
) -> dict:
    """Run `policy` over seeds 0..n_seeds-1; return aggregate + per-seed stats.

    Each episode is reproducible: the seed passes straight to the C++ engine.
    The policy must return a legal action (respecting the mask) — an illegal
    action surfaces as step()'s ValueError.

    hp_reward_coeff only affects the reward signal, not the metrics; it's
    accepted so eval can mirror the training env if desired, but win rate /
    HP retained are reward-independent.
    """
    env = MinispireEnv(hp_reward_coeff=hp_reward_coeff)
    per_seed: list[dict] = []

    for seed in range(n_seeds):
        obs, info = env.reset(seed=seed)
        length = 0
        while True:
            mask = env.action_masks()
            action = int(policy(obs, mask))
            obs, _reward, terminated, truncated, info = env.step(action)
            length += 1
            if terminated or truncated:
                break
        per_seed.append(
            {
                "seed": seed,
                "won": bool(info["won"]),
                "final_hp": int(info["final_hp"]),
                "hp_fraction": float(info["hp_fraction"]),
                "length": length,
            }
        )

    wins = [r for r in per_seed if r["won"]]
    win_rate = len(wins) / n_seeds if n_seeds else 0.0
    # HP retained is only meaningful on the won subset — averaging over losses
    # (hp_fraction 0) would conflate "lost badly" with "won inefficiently".
    hp_on_win = [r["hp_fraction"] for r in wins]
    lengths = [r["length"] for r in per_seed]

    return {
        "n_seeds": n_seeds,
        "win_rate": win_rate,
        "wins": len(wins),
        "losses": n_seeds - len(wins),
        "mean_hp_retained_on_win": float(np.mean(hp_on_win)) if hp_on_win else 0.0,
        "median_hp_retained_on_win": (
            float(statistics.median(hp_on_win)) if hp_on_win else 0.0
        ),
        "mean_episode_length": float(np.mean(lengths)) if lengths else 0.0,
        "random_baseline_win_rate": RANDOM_BASELINE_WIN_RATE,
        "per_seed": per_seed,
    }


def load_sb3_policy(checkpoint: str) -> Policy:
    """Load an SB3 MaskablePPO checkpoint into a deterministic policy callable.

    Assumes a MaskablePPO model (the only algorithm trained so far). The
    returned callable passes the action mask to predict() so masked actions
    are never chosen, and uses deterministic=True for reproducible eval.
    """
    from sb3_contrib import MaskablePPO

    model = MaskablePPO.load(checkpoint)

    def _policy(obs: np.ndarray, mask: np.ndarray) -> int:
        action, _ = model.predict(obs, action_masks=mask, deterministic=True)
        return int(action)

    return _policy


def _print_summary(results: dict, name: str) -> None:
    print(f"\n=== Eval: {name} ===")
    print(f"  Seeds:                  {results['n_seeds']}")
    print(
        f"  Win rate:               {results['win_rate'] * 100:.1f}%  "
        f"({results['wins']}/{results['n_seeds']})"
    )
    print(
        f"  Random baseline:        "
        f"{results['random_baseline_win_rate'] * 100:.1f}%"
    )
    print(
        f"  Mean HP retained (win): "
        f"{results['mean_hp_retained_on_win'] * 100:.1f}%"
    )
    print(
        f"  Median HP retained:     "
        f"{results['median_hp_retained_on_win'] * 100:.1f}%"
    )
    print(f"  Mean episode length:    {results['mean_episode_length']:.1f} turns")


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="minispire-eval",
        description="Evaluate a trained policy over a fixed seed set.",
    )
    parser.add_argument(
        "--checkpoint", required=True, help="Path to an SB3 MaskablePPO .zip."
    )
    parser.add_argument(
        "--n-seeds", type=int, default=1000, help="Seeds 0..N-1 (default 1000)."
    )
    parser.add_argument(
        "--out",
        default=None,
        help="Output JSON path (default results/<checkpoint stem>.json).",
    )
    args = parser.parse_args()

    policy = load_sb3_policy(args.checkpoint)
    results = evaluate(policy, n_seeds=args.n_seeds)
    results["checkpoint"] = args.checkpoint

    name = pathlib.Path(args.checkpoint).stem
    # Disambiguate "final" by including the parent run dir.
    parent = pathlib.Path(args.checkpoint).parent.name
    out_name = f"{parent}_{name}" if name == "final" else name
    out_path = pathlib.Path(args.out) if args.out else pathlib.Path("results") / f"{out_name}.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(results, f, indent=2)

    _print_summary(results, out_name)
    print(f"  Results written to:     {out_path}\n")


if __name__ == "__main__":
    main()
