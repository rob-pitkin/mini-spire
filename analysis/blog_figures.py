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

# Dark fills want dark text on them; light fills (gold/tan) want dark text too.
# A single dark ink for the on-block labels keeps them readable on every fill.
_BLOCK_TEXT = "#222129"


def _obs_blocks():
    """Obs-layout blocks, widths DERIVED from the engine constants (ROB-79) so
    the figure can't drift as statuses/cards are added. Abbreviated: the 5 enemy
    slots collapse to one 'x5' block and the 4 piles to one block.
    (label, width, color_key)."""
    from minispire._core import CombatEnv as C
    nd, npw = C.NUM_DEBUFFS, C.NUM_POWERS
    stride, n = C.ENEMY_OBS_STRIDE, C.MAX_ENEMIES
    cards = C.NUM_CARD_TYPES
    return [
        (f"Character\nstats (5)", 5, "agent"),
        (f"Char\ndebuffs\n({nd})", nd, "agent"),
        (f"Char\npowers\n({npw})", npw, "agent"),
        (f"Enemy slot  x{n}\nstats(3) debuffs({nd})\npowers({npw}) intent(4)\n"
         f"= {stride} each", stride * n, "enemy"),
        (f"Pile counts  x4\n(hand/draw/discard/\nexhaust) x {cards} cards\n"
         f"= {4 * cards}", 4 * cards, "deck"),
        ("Turn", 1, "meta"),
    ]


def plot_obs_layout(out_dir: pathlib.Path) -> None:
    """A linear band showing how the observation vector is carved up.

    Abbreviated: the enemy slots and pile blocks are summarized rather than
    enumerated (the point is the structure, not every individual cell). Widths
    and the total are derived from the engine so this never goes stale."""
    blocks = _obs_blocks()
    total = sum(w for _, w, _ in blocks)
    with _theme():
        fig, ax = plt.subplots(figsize=(12, 3.4))
        start = 0
        for label, width, key in blocks:
            ax.add_patch(
                mpatches.Rectangle(
                    (start, 0), width, 1, facecolor=PALETTE[key],
                    edgecolor=PALETTE["ink"], linewidth=1.5,
                )
            )
            # Wide blocks fit the label inside; narrow blocks put it above with a
            # short leader so the text doesn't overflow neighbors.
            cx = start + width / 2
            if width >= 12:
                ax.text(cx, 0.5, label, ha="center", va="center",
                        fontsize=7.5, color=_BLOCK_TEXT)
            else:
                ax.plot([cx, cx], [1.0, 1.18], color=PALETTE["ink"], linewidth=0.8)
                ax.text(cx, 1.22, label, ha="center", va="bottom",
                        fontsize=6.5, color=PALETTE["ink"])
            start += width
        ax.set_xlim(0, total)
        ax.set_ylim(-0.9, 2.1)
        ax.set_yticks([])
        ax.set_xticks([])
        from minispire._core import CombatEnv as _C
        ax.set_title(f"Observation space: a flat {total}-float vector "
                     f"({_C.MAX_ENEMIES} enemy slots, fixed size)")
        ax.text(0, -0.75,
                "debuffs = Vulnerable/Weak/Frail/Entangle   ·   "
                "powers = Strength/Dexterity/Ritual/Metallicize/Enrage/Artifact",
                fontsize=6.5, color=PALETTE["ink"])
        for s in ("top", "right", "left", "bottom"):
            ax.spines[s].set_visible(False)
        fig.tight_layout()
        fig.savefig(out_dir / "obs_layout.png", dpi=150)
        plt.close(fig)


def plot_action_space(out_dir: pathlib.Path) -> None:
    """The Discrete(41) action space = 8 card types x 5 targets + end turn.

    Abbreviated as a card x target grid rather than 41 individual boxes."""
    cards = ["Strike", "Defend", "Bash", "Strike+", "Defend+", "Bash+",
             "Slimed", "Dazed"]
    n_targets = 5
    with _theme():
        fig, ax = plt.subplots(figsize=(9, 3.6))
        # A grid: rows = card types, columns = target slots. Cell (r,c) is the
        # action card_r * 5 + target_c.
        for r, card in enumerate(cards):
            for c in range(n_targets):
                ax.add_patch(mpatches.Rectangle(
                    (c, len(cards) - 1 - r), 0.92, 0.92,
                    facecolor=PALETTE["accent"], edgecolor=PALETTE["ink"],
                    linewidth=1.0))
            ax.text(-0.2, len(cards) - 1 - r + 0.46, card, ha="right",
                    va="center", fontsize=7.5, color=PALETTE["ink"])
        # End-turn action sits apart.
        ax.add_patch(mpatches.Rectangle(
            (n_targets + 0.4, len(cards) / 2 - 0.5), 0.92, 0.92,
            facecolor=PALETTE["meta"], edgecolor=PALETTE["ink"], linewidth=1.0))
        ax.text(n_targets + 0.86, len(cards) / 2 - 0.02, "40\nEnd\nturn",
                ha="center", va="center", fontsize=6.5, color=_BLOCK_TEXT)
        for c in range(n_targets):
            ax.text(c + 0.46, len(cards) + 0.15, f"→ enemy {c}", ha="center",
                    va="bottom", fontsize=6.5, color=PALETTE["ink"])
        ax.set_xlim(-2.2, n_targets + 1.6)
        ax.set_ylim(-0.9, len(cards) + 0.7)
        ax.axis("off")
        ax.set_title("Action space: Discrete(41) = 8 card types x 5 target slots "
                     "+ end turn")
        ax.text((n_targets) / 2, -0.7,
                "action = card x 5 + target.  Untargeted cards use slot 0; "
                "invalid actions are masked each step.",
                ha="center", fontsize=7, color=PALETTE["ink"])
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
