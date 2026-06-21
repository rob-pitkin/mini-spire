"""Static explainer figures for the mini-spire intro blog post.

These are *structural* diagrams of the environment — the 45-float observation
layout, the 7-way action space, and the throughput numbers. They don't depend on
any run artifacts, so they regenerate identically.

    python -m analysis.blog_figures --out blog/figures

Styling: dark background, warm-orange palette, and a monospace font — tuned to
sit natively inside the author's Bear Blog theme (dark #222129 bg, #FFA86A
accent, Fira Code mono). All colors live in PALETTE below; edit that one dict to
recolor every figure. Numbers (env throughput, training fps) are constants below
so figure and prose stay in sync.
"""
from __future__ import annotations

import argparse
import contextlib
import pathlib

import matplotlib.font_manager as fm
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt

# --- Palette (single source of truth — recolor everything here) ----------------
# Colors map to the *semantic groups* of the observation vector, not ten
# arbitrary slices: agent / enemy / deck / meta. Adjacent blocks in the same
# group share a color, so the band reads as a clean grouped story rather than a
# choppy gradient. Anchored to the blog's dark bg + orange ink; the group fills
# are a small balanced set (teal/coral/amber/grey) chosen for distinctness on
# dark and clear separation from the orange theme text.
PALETTE = {
    "bg": "#222129",       # page/figure background (Bear --background-color)
    "ink": "#FFA86A",      # all outlines + text (Bear --text/heading/link color)
    # Semantic-group fills:
    "agent": "#4FA8A0",    # the character (hp/block/energy + char status) — teal
    "enemy": "#E8705A",    # everything enemy (stats, status, intent) — coral
    "deck": "#F2B441",     # all four card piles (hand/draw/discard/exhaust) — amber
    "meta": "#7A7480",     # turn number and other bookkeeping — grey
    # Aliases used by the bar/action figures:
    "accent": "#F2B441",   # primary bar / action cards (amber — not the orange ink)
    "muted": "#6b5d52",    # de-emphasized bar (contended)
}

# Monospace font to echo the blog's Fira Code. Fira Code usually isn't installed;
# Menlo (macOS default mono) is visually close and embeds in the PNG.
_MONO_FONT = next(
    (n for n in ("Fira Code", "Fira Mono", "Menlo", "DejaVu Sans Mono")
     if n in {f.name for f in fm.fontManager.ttflist}),
    "monospace",
)


@contextlib.contextmanager
def _theme():
    """Dark background + warm-orange ink + monospace font, matched to the blog."""
    rc = {
        "font.family": _MONO_FONT,
        "figure.facecolor": PALETTE["bg"],
        "axes.facecolor": PALETTE["bg"],
        "savefig.facecolor": PALETTE["bg"],
        "text.color": PALETTE["ink"],
        "axes.edgecolor": PALETTE["ink"],
        "axes.labelcolor": PALETTE["ink"],
        "xtick.color": PALETTE["ink"],
        "ytick.color": PALETTE["ink"],
        "axes.titlecolor": PALETTE["ink"],
    }
    with plt.rc_context(rc):
        yield


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
    ("Character\nhp, max_hp,\nblock, energy,\nenergy/turn", 0, 5, "agent"),
    ("Char\nstatus\nV/W/S/D", 5, 4, "agent"),
    ("Enemy\nhp, max,\nblock", 9, 3, "enemy"),
    ("Enemy\nstatus\nV/W/S/D", 12, 4, "enemy"),
    ("Enemy\nintent\natk/dmg/\nblk/buff", 16, 4, "enemy"),
    ("Hand\ncounts", 20, 6, "deck"),
    ("Draw\ncounts", 26, 6, "deck"),
    ("Discard\ncounts", 32, 6, "deck"),
    ("Exhaust\ncounts", 38, 6, "deck"),
    ("Turn", 44, 1, "meta"),
]

# Dark fills want dark text on them; light fills (gold/tan) want dark text too.
# A single dark ink for the on-block labels keeps them readable on every fill.
_BLOCK_TEXT = "#222129"


def plot_obs_layout(out_dir: pathlib.Path) -> None:
    """A linear band showing how the 45-float observation vector is carved up."""
    with _theme():
        fig, ax = plt.subplots(figsize=(11, 2.8))
        for label, start, count, key in _OBS_BLOCKS:
            ax.add_patch(
                mpatches.Rectangle(
                    (start, 0), count, 1, facecolor=PALETTE[key],
                    edgecolor=PALETTE["ink"], linewidth=1.5,
                )
            )
            ax.text(start + count / 2, 0.5, label, ha="center", va="center",
                    fontsize=7, color=_BLOCK_TEXT)
            ax.text(start + count / 2, 1.12, f"[{start}:{start + count}]",
                    ha="center", va="bottom", fontsize=6.5, color=PALETTE["ink"])
        ax.set_xlim(0, 45)
        ax.set_ylim(-0.7, 1.4)
        ax.set_yticks([])
        ax.set_xticks(range(0, 46, 5))
        ax.set_xlabel("float32 index")
        ax.set_title("Observation space: a flat 45-float vector")
        ax.text(0, -0.6, "V/W/S/D = Vulnerable, Weak, Strength, Dexterity stacks",
                fontsize=6.5, color=PALETTE["ink"])
        for s in ("top", "right", "left"):
            ax.spines[s].set_visible(False)
        fig.tight_layout()
        fig.savefig(out_dir / "obs_layout.png", dpi=150)
        plt.close(fig)


def plot_action_space(out_dir: pathlib.Path) -> None:
    """The 7 discrete actions, with a note on masking."""
    actions = ["Strike", "Defend", "Bash", "Strike+", "Defend+", "Bash+", "End turn"]
    colors = [PALETTE["accent"]] * 6 + [PALETTE["meta"]]
    with _theme():
        fig, ax = plt.subplots(figsize=(8, 2.4))
        for i, (a, c) in enumerate(zip(actions, colors)):
            ax.add_patch(mpatches.Rectangle(
                (i, 0), 0.9, 1, facecolor=c, edgecolor=PALETTE["ink"], linewidth=1.5))
            ax.text(i + 0.45, 0.5, f"{i}\n{a}", ha="center", va="center",
                    fontsize=8, color=_BLOCK_TEXT)
        ax.set_xlim(-0.1, 7)
        ax.set_ylim(-0.5, 1.2)
        ax.axis("off")
        ax.set_title("Action space: Discrete(7) — play a card type, or end turn")
        ax.text(3.5, -0.35,
                "Invalid actions (not in hand / not enough energy) are masked each step.",
                ha="center", fontsize=7.5, color=PALETTE["ink"])
        fig.tight_layout()
        fig.savefig(out_dir / "action_space.png", dpi=150)
        plt.close(fig)


def plot_throughput(out_dir: pathlib.Path) -> None:
    """Env stepping vs end-to-end training throughput (log scale)."""
    labels = ["Raw env\n(single, no learning)", "MaskablePPO\n(8 envs, best)",
              "MaskablePPO\n(8 envs, contended)"]
    values = [ENV_STEPS_PER_SEC, TRAIN_FPS_BEST, TRAIN_FPS_CONTENDED]
    colors = [PALETTE["accent"], PALETTE["enemy"], PALETTE["muted"]]
    with _theme():
        fig, ax = plt.subplots(figsize=(7, 4.2))
        bars = ax.bar(labels, values, color=colors,
                      edgecolor=PALETTE["ink"], linewidth=1.5)
        ax.set_yscale("log")
        ax.set_ylabel("environment steps / sec (log)")
        ax.set_title("Throughput on an M1 MacBook Pro")
        for bar, v in zip(bars, values):
            ax.text(bar.get_x() + bar.get_width() / 2, v * 1.12,
                    f"{v:,}", ha="center", va="bottom", fontsize=9,
                    color=PALETTE["ink"])
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
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
    print(f"font: {_MONO_FONT}")


if __name__ == "__main__":
    main()
