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
    the figure can't drift as statuses/cards are added. Abbreviated: the enemy
    slots collapse to one 'xN' block and the pile planes to one block.
    (label, width, color_key).

    The blocks are checked to sum to OBS_SIZE (ROB-82). They did not: this
    function referenced NUM_POWERS, which stopped existing when powers split
    per-entity in Stage 4a — so the script CRASHED rather than drew a stale
    figure — and even before that it omitted the choice channel entirely, which
    is now over a third of the vector. A figure that can silently lose a block
    is the same failure as a doc that restates a constant, so the total is
    asserted rather than trusted."""
    from minispire._core import CombatEnv as C
    nd = C.NUM_DEBUFFS
    npw, nep = C.NUM_PLAYER_POWERS, C.NUM_ENEMY_POWERS
    stride, n = C.ENEMY_OBS_STRIDE, C.MAX_ENEMIES
    cards = C.NUM_CARD_TYPES
    # Player block = stats + the two query-layer counters, then debuffs+powers.
    base = C.PLAYER_OBS_SIZE - (nd + npw)
    piles = 5 * cards          # hand/draw/discard/exhaust + free-this-turn
    # Whatever is left after the known blocks is the choice channel; derived
    # rather than restated so it cannot drift out of sync.
    choice = C.OBS_SIZE - (C.PLAYER_OBS_SIZE + stride * n + piles + 1)
    # The three character sub-blocks are collapsed into one. At 1772 floats they
    # are 0.4%, 0.3% and 1.2% of the width, so their labels overlapped into an
    # unreadable pile — and the style guide already groups adjacent same-colour
    # blocks. The breakdown moves to the caption, where it stays legible.
    intent, kinds = C.ENEMY_INTENT_SIZE, C.NUM_ENEMY_KINDS
    blocks = [
        (f"Character\nstats({base}) debuffs({nd})\npowers({npw}) = {C.PLAYER_OBS_SIZE}",
         C.PLAYER_OBS_SIZE, "agent"),
        (f"Enemy slot x{n}\nstats(3) debuffs({nd})\npowers({nep}) intent({intent})\n"
         f"kind({kinds}) = {stride} each", stride * n, "enemy"),
        (f"Pile counts x5\n(hand/draw/discard/exhaust\n+ free-this-turn) x {cards} cards\n"
         f"= {piles}", piles, "deck"),
        ("Turn", 1, "meta"),
        (f"Choice channel\nheader(5) + {cards} slots x 3\n= {choice}", choice, "meta"),
    ]
    total = sum(w for _, w, _ in blocks)
    if total != C.OBS_SIZE:
        raise AssertionError(
            f"obs figure blocks sum to {total}, but OBS_SIZE is {C.OBS_SIZE} — "
            "a block is missing or double-counted")
    return blocks


def plot_obs_layout(out_dir: pathlib.Path) -> None:
    """A linear band showing how the observation vector is carved up.

    Abbreviated: the enemy slots and pile blocks are summarized rather than
    enumerated (the point is the structure, not every individual cell). Widths
    and the total are derived from the engine so this never goes stale."""
    blocks = _obs_blocks()
    total = sum(w for _, w, _ in blocks)
    with _theme():
        fig, ax = plt.subplots(figsize=(12, 3.9))
        start = 0
        # Whether a label fits inside is a question of PROPORTION, not float
        # count. This was `width >= 12`, which worked at 133 floats and silently
        # stopped working at 1642: the character block is 34 floats, well over
        # 12 — but only 2% of the bar, so its label rendered as a smear across
        # its neighbours. Narrow labels also get staggered heights, because the
        # leftmost blocks sit close together and a single height collides.
        narrow_seen = 0
        for label, width, key in blocks:
            ax.add_patch(
                mpatches.Rectangle(
                    (start, 0), width, 1, facecolor=PALETTE[key],
                    edgecolor=PALETTE["ink"], linewidth=1.5,
                )
            )
            cx = start + width / 2
            if width / total >= 0.10:
                ax.text(cx, 0.5, label, ha="center", va="center",
                        fontsize=7.5, color=_BLOCK_TEXT)
            else:
                tip = 1.15 + 0.42 * (narrow_seen % 3)
                narrow_seen += 1
                ax.plot([cx, cx], [1.0, tip], color=PALETTE["ink"],
                        linewidth=0.8)
                # Labels near the left edge extend rightwards rather than
                # centring, so they cannot run off the axes.
                align = "left" if cx / total < 0.12 else "center"
                ax.text(cx, tip + 0.04, label, ha=align, va="bottom",
                        fontsize=6.5, color=PALETTE["ink"])
            start += width
        ax.set_xlim(0, total)
        ax.set_ylim(-1.05, 2.75)
        ax.set_yticks([])
        ax.set_xticks([])
        from minispire._core import CombatEnv as _C
        ax.set_title(f"Observation space: a flat {total}-float vector "
                     f"({_C.MAX_ENEMIES} enemy slots, fixed size)")
        # The enemy blocks carry only the enemy-relevant power prefix; listing
        # all 22 player powers here would be unreadable, and claiming the enemy
        # list applies to both (as this caption used to) is simply wrong.
        ax.text(0, -0.75,
                "debuffs = Vulnerable/Weak/Frail/Entangle/NoDraw   ·   "
                "enemy powers = Strength/Dexterity/Ritual/Metallicize/Enrage/"
                f"Artifact   ·   character carries all {_C.NUM_PLAYER_POWERS}\n"
                "pile counts are per card type, not ordered lists   ·   the "
                "choice channel is zeroed unless a decision is pending",
                fontsize=6.5, color=PALETTE["ink"])
        for s in ("top", "right", "left", "bottom"):
            ax.spines[s].set_visible(False)
        fig.tight_layout()
        fig.savefig(out_dir / "obs_layout.png", dpi=150)
        plt.close(fig)


def plot_action_space(out_dir: pathlib.Path) -> None:
    """The action space as two mutually-exclusive blocks (ROB-82).

    This used to enumerate an 8-card x 5-target grid with a hardcoded
    "Discrete(41)" title. That stopped being drawable at 189 card types, and it
    never showed the option-slot channel at all — which is now a third of the
    space. Drawn as proportional bands instead, with every index derived."""
    from minispire._core import CombatEnv as C
    cards, targets = C.NUM_CARD_TYPES, C.MAX_ENEMIES
    end_turn, first_slot = C.END_TURN_ACTION, C.FIRST_OPTION_SLOT
    decline, total = C.DECLINE_ACTION, C.NUM_ACTIONS
    combat = end_turn          # 0 .. end_turn-1 are the card x target actions
    slots = decline - first_slot

    # Coloured by BLOCK, not by individual action, because the mutual exclusion
    # is the whole point of the figure. PALETTE["accent"] and PALETTE["deck"]
    # are the same amber, so colouring these four bands separately made the two
    # blocks indistinguishable — the one thing the reader needs to see.
    # Grey matches the choice channel in the observation figure.
    bands = [
        (f"play card x target\n{cards} cards x {targets} slots = {combat}",
         combat, "deck"),
        (f"end turn\n[{end_turn}]", 1, "deck"),
        (f"option slots\n{slots} = one per card type", slots, "meta"),
        (f"decline\n[{decline}]", 1, "meta"),
    ]
    if sum(w for _, w, _ in bands) != total:
        raise AssertionError("action bands do not sum to NUM_ACTIONS")

    with _theme():
        fig, ax = plt.subplots(figsize=(11, 2.5))
        x = 0
        for label, width, key in bands:
            ax.add_patch(mpatches.Rectangle(
                (x, 0), width, 1, facecolor=PALETTE[key],
                edgecolor=PALETTE["ink"], linewidth=1.2))
            # Narrow bands get their label above the bar, not inside it.
            if width / total > 0.06:
                ax.text(x + width / 2, 0.5, label, ha="center", va="center",
                        fontsize=7.5, color=_BLOCK_TEXT)
            else:
                ax.text(x + width / 2, 1.15, label, ha="center", va="bottom",
                        fontsize=6.5, color=PALETTE["ink"])
            x += width
        ax.set_xlim(-total * 0.02, total * 1.02)
        ax.set_ylim(-0.75, 2.0)
        ax.axis("off")
        ax.set_title(f"Action space: Discrete({total}) — two mutually-exclusive "
                     "blocks")
        ax.text(total / 2, -0.55,
                "action = card x MAX_ENEMIES + target during combat; the option "
                "slots are live only while a choice is pending.\n"
                "Exactly one block is unmasked at a time — end turn is NOT the "
                "last index.",
                ha="center", fontsize=7, color=PALETTE["ink"])
        fig.tight_layout()
        fig.savefig(out_dir / "action_space.png", dpi=150)
        plt.close(fig)


def _box(ax, x, y, w, h, label, key, fontsize=7.0, text_key=None):
    """One labelled block in the blog palette. Returns its centre."""
    ax.add_patch(mpatches.FancyBboxPatch(
        (x, y), w, h, boxstyle="round,pad=0.012,rounding_size=0.02",
        facecolor=PALETTE[key], edgecolor=PALETTE["ink"], linewidth=1.2))
    ax.text(x + w / 2, y + h / 2, label, ha="center", va="center",
            fontsize=fontsize,
            color=PALETTE[text_key] if text_key else _BLOCK_TEXT)
    return (x + w / 2, y + h / 2)


def _arrow(ax, xy_from, xy_to, label=None, rad=0.0, fontsize=6.4,
           label_xy=None):
    ax.annotate("", xy=xy_to, xytext=xy_from,
                arrowprops=dict(arrowstyle="-|>", color=PALETTE["ink"],
                                linewidth=1.2,
                                connectionstyle=f"arc3,rad={rad}",
                                shrinkA=2, shrinkB=2))
    if label:
        lx, ly = label_xy or ((xy_from[0] + xy_to[0]) / 2,
                              (xy_from[1] + xy_to[1]) / 2)
        ax.text(lx, ly, label, ha="center", va="center", fontsize=fontsize,
                color=PALETTE["ink"])


def plot_architecture(out_dir: pathlib.Path) -> None:
    """Two panels: how one card play resolves, and how a full turn cycles.

    The action queue is the distinctive thing about this engine, and it is
    invisible from the outside — an env consumer sees step() and never learns
    that a card is DATA translated into a list of effects rather than a function
    that mutates state. Real cards are named so the mechanics stay concrete."""
    with _theme():
        fig, (ax1, ax2) = plt.subplots(
            2, 1, figsize=(11.5, 8.2), gridspec_kw={"height_ratios": [1.15, 1]})

        # ---- Panel 1: the life of one card play ---------------------------
        ax1.set_xlim(0, 100)
        ax1.set_ylim(0, 42)
        ax1.axis("off")
        ax1.set_title("Playing a card: translate, then drain", fontsize=10.5,
                      color=PALETTE["ink"], loc="left", pad=8)

        played = _box(ax1, 2, 27, 13, 7, "play Bash\n(cost 2)", "deck")
        ax1.text(8.5, 24.5, "a card is DATA,\nnot a function",
                 ha="center", va="top", fontsize=6.2, color=PALETTE["ink"])

        # The queue itself. Width is chosen so the four boxes plus the terminal
        # "queue empty" block fit without overlapping — at 15.5 they collided.
        qx, qy, qw, qh, gap = 24, 27, 12.6, 7, 1.2
        labels = ["SpendEnergy\n2", "DealDamage\n8", "ApplyDebuff\nVulnerable 2",
                  "DiscardCard\nBash"]
        centres = []
        for i, lab in enumerate(labels):
            centres.append(_box(ax1, qx + i * (qw + gap), qy, qw, qh, lab,
                                "deck", fontsize=6.0))
        q_right = qx + 3 * (qw + gap) + qw
        _arrow(ax1, (15.4, 30.5), (qx - 0.6, 30.5), "translate",
               label_xy=(19.7, 32.6))
        ax1.text((qx + q_right) / 2, 36.4,
                 "ActionQueue — drained front to back, never recursively",
                 ha="center", va="bottom", fontsize=7.2, color=PALETTE["ink"])

        # Drain loop under the queue.
        _box(ax1, 28, 11, 16, 7, "pop the front\nand execute it", "meta")
        _arrow(ax1, (centres[0][0], qy - 0.4), (34, 18.4), rad=-0.18)
        _box(ax1, 54, 11, 20, 7, "an executor may PUSH\nmore actions", "meta")
        _arrow(ax1, (44.4, 14.5), (53.6, 14.5))
        _arrow(ax1, (64, 18.4), (centres[3][0], qy - 0.4), rad=-0.3)
        # Below the boxes, not beside them: the curved return arrow sweeps
        # through the space to the right of the push block.
        ax1.text(64, 7.6,
                 "Bash kills the enemy → CheckDeath → Spore Cloud's Vulnerable.\n"
                 "Havoc plays another card the same way, as a flat queue step.",
                 ha="center", va="center", fontsize=6.2, color=PALETTE["ink"])

        _box(ax1, 83, 27, 15, 7, "queue empty\n→ agent acts", "agent",
             fontsize=6.6)
        _arrow(ax1, (q_right + 0.6, 30.5), (82.4, 30.5))
        ax1.text(90.5, 24.6,
                 "the invariant: empty\nat every decision point",
                 ha="center", va="top", fontsize=6.2, color=PALETTE["ink"])
        ax1.text(2, 1.8,
                 "The one exception is a card that asks a question — Armaments, "
                 "Exhume, Warcry. Those SUSPEND the drain mid-resolution,\nhand "
                 "the menu to the agent through the option slots, and resume "
                 "once it answers.",
                 ha="left", va="center", fontsize=6.6, color=PALETTE["ink"])

        # ---- Panel 2: the turn cycle --------------------------------------
        ax2.set_xlim(0, 100)
        ax2.set_ylim(0, 34)
        ax2.axis("off")
        ax2.set_title("One full turn", fontsize=10.5, color=PALETTE["ink"],
                      loc="left", pad=8)

        p1 = _box(ax2, 2, 20, 17, 7.5,
                  "start of turn\nblock reset, energy,\npowers, draw 5", "agent",
                  fontsize=6.3)
        p2 = _box(ax2, 22, 20, 17, 7.5,
                  "play cards\neach one: translate\n+ drain", "agent",
                  fontsize=6.3)
        p3 = _box(ax2, 42, 20, 17, 7.5,
                  "end turn\nhand discards, then\nend-of-turn powers", "agent",
                  fontsize=6.3)
        _arrow(ax2, (19.4, 23.7), (21.6, 23.7))
        _arrow(ax2, (39.4, 23.7), (41.6, 23.7))
        ax2.text(30.5, 29.4, "PLAYER", ha="center", fontsize=7.6,
                 color=PALETTE["agent"])

        e1 = _box(ax2, 64, 20, 16, 7.5,
                  "start-of-turn hooks\nMetallicize", "enemy", fontsize=6.3)
        e2 = _box(ax2, 82, 20, 16, 7.5,
                  "the primed move\ntranslate + drain", "enemy", fontsize=6.3)
        e3 = _box(ax2, 82, 8, 16, 7.5,
                  "end-of-turn hooks\nRitual", "enemy", fontsize=6.3)
        e4 = _box(ax2, 64, 8, 16, 7.5,
                  "tick debuffs, then\npick next intent", "enemy", fontsize=6.3)
        _arrow(ax2, (59.4, 23.7), (63.6, 23.7))
        _arrow(ax2, (80.4, 23.7), (81.6, 23.7))
        _arrow(ax2, (90, 19.6), (90, 15.6))
        _arrow(ax2, (81.6, 11.7), (80.4, 11.7))
        ax2.text(81, 29.4, "EACH ENEMY, IN SLOT ORDER", ha="center",
                 fontsize=7.6, color=PALETTE["enemy"])

        # Loop back to the player.
        _arrow(ax2, (64, 11.7), (10.5, 11.7))
        _arrow(ax2, (10.5, 11.7), (10.5, 19.6))
        ax2.text(37, 13.2, "next turn", ha="center", fontsize=6.6,
                 color=PALETTE["ink"])

        ax2.text(2, 3.0,
                 "The enemy's next move is chosen at the END of its turn, so the "
                 "intent the agent sees during its own turn is\nalready final — "
                 "including any Strength the enemy just gained.",
                 ha="left", va="center", fontsize=6.6, color=PALETTE["ink"])

        fig.tight_layout(h_pad=2.0)
        fig.savefig(out_dir / "architecture.png", dpi=150)
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
    plot_architecture(out_dir)
    plot_throughput(out_dir)
    print(f"Wrote obs_layout.png, action_space.png, architecture.png, "
          f"throughput.png to {out_dir}")
    print(f"font: {_MONO_FONT}")


if __name__ == "__main__":
    main()
