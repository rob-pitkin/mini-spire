"""Generate the M1 blog plots from eval result JSONs (ROB-56).

Reads results/<run>.json (produced by minispire-eval) and writes PNGs to
blog/figures/. The headline figure is the HP-retained distribution comparing
the sparse and HP-shaped agents — the win-rate is 100% for both, so the
*efficiency* (HP retained) is where the story lives.

Usage:
    python -m analysis.plots \\
        --sparse results/65w5s134_final.json \\
        --shaped results/p6fke762_final.json \\
        --out blog/figures
"""
from __future__ import annotations

import argparse
import json
import pathlib

import matplotlib.pyplot as plt
import numpy as np

RANDOM_BASELINE = 0.528


def _load(path: str) -> dict:
    with open(path) as f:
        return json.load(f)


def plot_win_rate_bars(sparse: dict, shaped: dict, out_dir: pathlib.Path) -> None:
    """Random vs sparse vs shaped win rate."""
    labels = ["Random", "PPO (sparse)", "PPO (HP-shaped)"]
    values = [RANDOM_BASELINE, sparse["win_rate"], shaped["win_rate"]]
    colors = ["#999999", "#4c72b0", "#55a868"]

    fig, ax = plt.subplots(figsize=(6, 4))
    bars = ax.bar(labels, [v * 100 for v in values], color=colors)
    ax.set_ylabel("Win rate (%)")
    ax.set_ylim(0, 110)
    ax.set_title("Win rate vs Jaw Worm (1000 seeds)")
    for bar, v in zip(bars, values):
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            bar.get_height() + 1,
            f"{v * 100:.1f}%",
            ha="center",
            va="bottom",
        )
    fig.tight_layout()
    fig.savefig(out_dir / "win_rate_bars.png", dpi=150)
    plt.close(fig)


def plot_hp_retained_hist(sparse: dict, shaped: dict, out_dir: pathlib.Path) -> None:
    """Distribution of HP retained on wins — the headline efficiency figure."""
    sparse_hp = [r["hp_fraction"] * 100 for r in sparse["per_seed"] if r["won"]]
    shaped_hp = [r["hp_fraction"] * 100 for r in shaped["per_seed"] if r["won"]]

    fig, ax = plt.subplots(figsize=(7, 4))
    bins = np.linspace(0, 100, 21)
    ax.hist(sparse_hp, bins=bins, alpha=0.6, label="PPO (sparse)", color="#4c72b0")
    ax.hist(shaped_hp, bins=bins, alpha=0.6, label="PPO (HP-shaped)", color="#55a868")
    ax.axvline(np.mean(sparse_hp), color="#4c72b0", linestyle="--", linewidth=1)
    ax.axvline(np.mean(shaped_hp), color="#55a868", linestyle="--", linewidth=1)
    ax.set_xlabel("HP retained on win (%)")
    ax.set_ylabel("Episodes")
    ax.set_title("HP retained on wins: shaping improves the low-HP tail")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / "hp_retained_hist.png", dpi=150)
    plt.close(fig)


def plot_hp_vs_seed_scatter(shaped: dict, out_dir: pathlib.Path) -> None:
    """Per-seed HP retained — shows the spread of outcomes across the fixed set."""
    seeds = [r["seed"] for r in shaped["per_seed"]]
    hp = [r["hp_fraction"] * 100 for r in shaped["per_seed"]]
    won = [r["won"] for r in shaped["per_seed"]]
    colors = ["#55a868" if w else "#c44e52" for w in won]

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.scatter(seeds, hp, c=colors, s=6, alpha=0.5)
    ax.set_xlabel("Seed")
    ax.set_ylabel("HP retained (%)")
    ax.set_title("Per-seed HP retained (HP-shaped agent, 1000 seeds)")
    fig.tight_layout()
    fig.savefig(out_dir / "hp_vs_seed_scatter.png", dpi=150)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate M1 blog plots.")
    parser.add_argument("--sparse", required=True, help="Sparse eval JSON.")
    parser.add_argument("--shaped", required=True, help="Shaped eval JSON.")
    parser.add_argument("--out", default="blog/figures", help="Output dir.")
    args = parser.parse_args()

    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    sparse = _load(args.sparse)
    shaped = _load(args.shaped)

    plot_win_rate_bars(sparse, shaped, out_dir)
    plot_hp_retained_hist(sparse, shaped, out_dir)
    plot_hp_vs_seed_scatter(shaped, out_dir)
    print(f"Wrote 3 figures to {out_dir}")


if __name__ == "__main__":
    main()
