"""Static explainer figures for the mini-spire intro blog post.

These are *structural* diagrams of the environment — the 45-float observation
layout, the 7-way action space, and the throughput numbers. They don't depend on
any run artifacts, so they regenerate identically.

    python -m analysis.blog_figures --out blog/figures

Numbers (env throughput, training fps) are passed in as constants below so the
figure and the prose stay in sync; update them here if the measurement changes.
"""
from __future__ import annotations

import argparse
import pathlib

import matplotlib.patches as mpatches
import matplotlib.pyplot as plt

# --- Measured constants (M1 MacBook Pro, 2020) ---------------------------------
# Raw single-env stepping through the Python bindings (mask + step + reset).
# Median of 5 trials, very tight (+-0.5%).
ENV_STEPS_PER_SEC = 145_000
# End-to-end MaskablePPO throughput (8 vec envs + gradient updates), directly
# timed on an idle machine (W&B disabled). Drops under core contention (several
# runs at once) — that's the learner/scheduler, not the env.
TRAIN_FPS_BEST = 4_300
TRAIN_FPS_CONTENDED = 1_135

# --- Observation layout (src/combat_env.cc::compute_obs, 45 floats) ------------
# (label, start_idx, count, color_key)
_OBS_BLOCKS = [
    ("Character\nhp, max_hp,\nblock, energy,\nenergy/turn", 0, 5, "char"),
    ("Char\nstatus\nV/W/S/D", 5, 4, "char"),
    ("Enemy\nhp, max,\nblock", 9, 3, "enemy"),
    ("Enemy\nstatus\nV/W/S/D", 12, 4, "enemy"),
    ("Enemy\nintent\natk/dmg/\nblk/buff", 16, 4, "intent"),
    ("Hand\ncounts", 20, 6, "pile"),
    ("Draw\ncounts", 26, 6, "pile"),
    ("Discard\ncounts", 32, 6, "pile"),
    ("Exhaust\ncounts", 38, 6, "pile"),
    ("Turn", 44, 1, "turn"),
]
_OBS_COLORS = {
    "char": "#4c72b0",
    "enemy": "#c44e52",
    "intent": "#dd8452",
    "pile": "#55a868",
    "turn": "#8172b3",
}


def plot_obs_layout(out_dir: pathlib.Path) -> None:
    """A linear band showing how the 45-float observation vector is carved up."""
    fig, ax = plt.subplots(figsize=(11, 2.6))
    for label, start, count, key in _OBS_BLOCKS:
        ax.add_patch(
            mpatches.Rectangle(
                (start, 0), count, 1, facecolor=_OBS_COLORS[key],
                edgecolor="white", linewidth=1.5,
            )
        )
        ax.text(
            start + count / 2, 0.5, label, ha="center", va="center",
            fontsize=7.5, color="white", wrap=True,
        )
        ax.text(start + count / 2, 1.12, f"[{start}:{start + count}]",
                ha="center", va="bottom", fontsize=6.5, color="#444")
    ax.set_xlim(0, 45)
    ax.set_ylim(-0.7, 1.35)
    ax.set_yticks([])
    ax.set_xticks(range(0, 46, 5))
    ax.set_xlabel("float32 index")
    ax.set_title("Observation space: a flat 45-float vector")
    ax.text(0, -0.55, "V/W/S/D = Vulnerable, Weak, Strength, Dexterity stacks",
            fontsize=7, color="#666", transform=ax.transData)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(out_dir / "obs_layout.png", dpi=150)
    plt.close(fig)


def plot_action_space(out_dir: pathlib.Path) -> None:
    """The 7 discrete actions, with a note on masking."""
    actions = ["Strike", "Defend", "Bash", "Strike+", "Defend+", "Bash+", "End turn"]
    colors = ["#4c72b0"] * 6 + ["#8172b3"]
    fig, ax = plt.subplots(figsize=(8, 2.2))
    for i, (a, c) in enumerate(zip(actions, colors)):
        ax.add_patch(mpatches.Rectangle((i, 0), 0.9, 1, facecolor=c, edgecolor="white"))
        ax.text(i + 0.45, 0.5, f"{i}\n{a}", ha="center", va="center",
                fontsize=8, color="white")
    ax.set_xlim(-0.1, 7)
    ax.set_ylim(-0.4, 1.2)
    ax.axis("off")
    ax.set_title("Action space: Discrete(7) — play a card type, or end turn")
    ax.text(3.5, -0.3,
            "Invalid actions (not in hand / not enough energy) are masked each step.",
            ha="center", fontsize=8, color="#444")
    fig.tight_layout()
    fig.savefig(out_dir / "action_space.png", dpi=150)
    plt.close(fig)


def plot_throughput(out_dir: pathlib.Path) -> None:
    """Env stepping vs end-to-end training throughput (log scale)."""
    labels = ["Raw env\n(single, no learning)", "MaskablePPO\n(8 envs, best)",
              "MaskablePPO\n(8 envs, contended)"]
    values = [ENV_STEPS_PER_SEC, TRAIN_FPS_BEST, TRAIN_FPS_CONTENDED]
    colors = ["#55a868", "#4c72b0", "#a0b4d4"]
    fig, ax = plt.subplots(figsize=(7, 4))
    bars = ax.bar(labels, values, color=colors)
    ax.set_yscale("log")
    ax.set_ylabel("environment steps / sec (log)")
    ax.set_title("Throughput on an M1 MacBook Pro")
    for bar, v in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width() / 2, v * 1.1,
                f"{v:,}", ha="center", va="bottom", fontsize=9)
    fig.tight_layout()
    fig.savefig(out_dir / "throughput.png", dpi=150)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate intro-post figures.")
    parser.add_argument("--out", default="blog/figures", help="Output dir.")
    args = parser.parse_args()
    out_dir = pathlib.Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    plot_obs_layout(out_dir)
    plot_action_space(out_dir)
    plot_throughput(out_dir)
    print(f"Wrote obs_layout.png, action_space.png, throughput.png to {out_dir}")


if __name__ == "__main__":
    main()
