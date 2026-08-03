"""Textual app for human play (ROB-83).

Replaces the print-and-prompt loop. The reason for the framework is not this
screen — turn-based combat is the friendliest case for print-and-prompt, since
nothing updates concurrently — it is the v2.0.0 roadmap, which is five more
screens with lists, prices and nested selection, plus scrolling: a pile choice
can offer 100+ options and a print loop dumps all of them and redraws on every
keystroke.

Layering, which is the part worth protecting:

    keys.key_to_intent   pure, knows nothing about the game
    MinispireApp         holds state, owns the two-phase selection
    screen.build_*       builds Rich renderables, knows nothing about input

The app is the only piece that knows a card might need a target. keys.py sees
"pick one of N" and cannot tell targeting from a mid-card choice; screen.py
draws what it is handed. That is deliberate — see keys.py's module docstring.
"""
from __future__ import annotations

import numpy as np
from rich.console import Group
from rich.text import Text
from textual import events
from textual.app import App, ComposeResult
from textual.widgets import Static

from minispire import _core
from minispire.env import MinispireEnv
from minispire.render import screen
from minispire.render.keys import (
    Cancel,
    Confirm,
    Mode,
    MoveFocus,
    MoveRow,
    Quit,
    Select,
    TogglePiles,
    key_to_intent,
)

# Process exit codes, unchanged from the print-and-prompt loop so scripts and
# the docs keep working.
EXIT_WIN = 0
EXIT_LOSS = 1
EXIT_QUIT = 2


def card_action(card_id, target_slot: int) -> int:
    """Global action index for playing `card_id` at `target_slot`.

    The action space is a (card x target) cross-product (ROB-60), so an
    untargeted card uses the canonical slot 0.
    """
    return int(card_id) * _core.CombatEnv.MAX_ENEMIES + target_slot


class MinispireApp(App):
    """One interactive fight."""

    CSS = """
    Screen { layout: vertical; }
    #board { height: auto; }
    #panel { height: auto; }
    #footer { height: auto; color: $text-muted; }
    """

    def __init__(
        self,
        *,
        seed: int = 0,
        pool=None,
        deck=None,
        ascii_only: bool = False,
    ) -> None:
        super().__init__()
        kwargs = {}
        if pool is not None:
            kwargs["pool"] = pool
        if deck is not None:
            kwargs["deck"] = deck
        self.env = MinispireEnv(**kwargs)
        self._seed = seed
        self.ascii_only = ascii_only

        self.obs: np.ndarray | None = None
        self.mode = Mode.PLAY
        self.focus = 0
        # The two-phase selection. Set when a card needs a target and more than
        # one enemy is alive; cleared when the target is picked or cancelled.
        self.pending_card = None
        self._action_map: list = []
        self._targets: list[int] = []
        self.exit_code = EXIT_QUIT

    # -- lifecycle ----------------------------------------------------------

    def compose(self) -> ComposeResult:
        yield Static(id="board")
        yield Static(id="panel")
        yield Static(id="footer")

    def on_mount(self) -> None:
        self.obs, _ = self.env.reset(seed=self._seed)
        self._redraw()

    # -- the option list ----------------------------------------------------
    #
    # Rendering and key handling BOTH go through this. They used to be derived
    # separately, and that is how a keypress comes to mean a different option
    # than the one the player was looking at.

    def option_count(self) -> int:
        """How many things are selectable right now."""
        if self.mode is Mode.PILES:
            return 0
        if self.mode is Mode.CHOOSE:
            return len(self._targets) if self.pending_card is not None else self._choice_count()
        return len(self._action_map) + 1  # hand + End Turn

    def _choice_count(self) -> int:
        view = self.env.choice_view()
        return len(view.options) if view.active else 0

    # -- input --------------------------------------------------------------

    def on_key(self, event: events.Key) -> None:
        intent = key_to_intent(
            event.key,
            mode=self.mode,
            option_count=self.option_count(),
            focus=self.focus,
        )
        if intent is None:
            return
        event.stop()

        if isinstance(intent, Quit):
            self.exit(self.exit_code)
            return
        if isinstance(intent, TogglePiles):
            # Piles are inspect-only; leaving a half-made selection intact would
            # be worse than dropping it, since the hand may change underneath.
            self.mode = Mode.PLAY if self.mode is Mode.PILES else Mode.PILES
            self.focus = 0
            self._redraw()
            return
        if isinstance(intent, Cancel):
            self._cancel()
            return
        if isinstance(intent, (MoveFocus, MoveRow)):
            count = self.option_count()
            if count:
                # A row is however wide the grid was drawn. keys.py reports
                # "down a row" without knowing that, which is why the width
                # lives here next to the rendering.
                step = intent.delta
                if isinstance(intent, MoveRow):
                    step *= screen.HAND_COLUMNS if self.mode is not Mode.CHOOSE else 1
                self.focus = (self.focus + step) % count  # wrap
                self._redraw()
            return
        if isinstance(intent, (Select, Confirm)):
            self._choose(intent.index)

    def _cancel(self) -> None:
        if self.pending_card is not None:
            self.pending_card = None
            self.mode = Mode.PLAY
            self.focus = 0
            self._redraw()
        elif self.mode is Mode.PILES:
            self.mode = Mode.PLAY
            self.focus = 0
            self._redraw()

    def _choose(self, index: int) -> None:
        """Act on option `index` in the current mode."""
        if self.mode is Mode.CHOOSE:
            if self.pending_card is not None:
                if not (0 <= index < len(self._targets)):
                    return
                card, self.pending_card = self.pending_card, None
                self.mode = Mode.PLAY
                self._step(card_action(card, self._targets[index]))
            else:
                self._step(_core.CombatEnv.FIRST_OPTION_SLOT + index)
            return

        # PLAY: the hand, then End Turn.
        if index == len(self._action_map):
            self._step(_core.CombatEnv.END_TURN_ACTION)
            return
        if not (0 <= index < len(self._action_map)):
            return

        card_id = self._action_map[index]
        if not _core.card_targets_enemy(card_id):
            self._step(card_action(card_id, 0))
            return
        living = screen.living_enemy_slots(self.obs)
        if len(living) == 1:
            self._step(card_action(card_id, living[0]))
            return
        # Two or more living enemies: enter the targeting phase. keys.py sees
        # this as an ordinary "pick one of N" — only the app knows it is a
        # target and that a card is waiting on it.
        self.pending_card = card_id
        self._targets = living
        self.mode = Mode.CHOOSE
        self.focus = 0
        self._redraw()

    # -- stepping -----------------------------------------------------------

    def _step(self, action: int) -> None:
        self.obs, _reward, terminated, truncated, info = self.env.step(action)
        self.focus = 0
        if terminated or truncated:
            # info["won"] comes from the engine (ROB-58) rather than being
            # inferred from the reward, which is shaped when hp_reward_coeff
            # is set and so is not a reliable win signal.
            won = bool(info.get("won", self.env.outcome == _core.Outcome.Won))
            self.exit_code = EXIT_WIN if won else EXIT_LOSS
            self._redraw(final=True)
            self.exit(self.exit_code)
            return
        # A card may have opened a mid-resolution choice (Armaments, Exhume).
        view = self.env.choice_view()
        self.mode = Mode.CHOOSE if view.active else Mode.PLAY
        self._redraw()

    # -- rendering ----------------------------------------------------------

    def _redraw(self, *, final: bool = False) -> None:
        self.query_one("#board", Static).update(
            screen.build_fight(self.obs, self.env, ascii_only=self.ascii_only)
        )

        # Focus is only meaningful for the list the keys are indexing. While
        # targeting, that list is the enemies — so the hand must NOT also show a
        # highlight, or two things look selected at once.
        targeting = self.mode is Mode.CHOOSE and self.pending_card is not None

        if self.mode is Mode.PILES:
            panel = screen.build_piles(self.env)
            self._action_map = []
        elif self.mode is Mode.CHOOSE and not targeting:
            panel, _count = screen.build_choice(self.env, focus=self.focus)
            self._action_map = []
        else:
            panel, self._action_map = screen.build_hand(
                self.env, focus=None if targeting else self.focus
            )

        if targeting:
            prompt = Text("  target: ", style="bold yellow")
            for i, slot in enumerate(self._targets):
                name = _core.enemy_name(self.env.enemy_kinds()[slot])
                label = Text(f" {screen.FOCUS_MARK} ({i}) {name} " if i == self.focus
                             else f"   ({i}) {name} ")
                if i == self.focus:
                    label.stylize(screen.FOCUS_STYLE)
                prompt.append_text(label)
            prompt.append("   (esc to cancel)", style="dim")
            panel = Group(panel, prompt)

        self.query_one("#panel", Static).update(panel)
        self.query_one("#footer", Static).update(self._footer(final=final))

    def _footer(self, *, final: bool = False) -> Text:
        if final:
            won = self.exit_code == EXIT_WIN
            return Text(
                "  VICTORY" if won else "  DEFEAT",
                style="bold green" if won else "bold red",
            )
        if self.mode is Mode.PILES:
            return Text("  p back  ·  q quit", style="dim")
        count = self.option_count()
        focus = self.focus if count else 0
        return Text(
            f"  <- -> move ({focus + 1}/{count})  ·  enter select  ·  "
            f"0-9 direct  ·  p piles  ·  q quit",
            style="dim",
        )


def run(
    seed: int = 0,
    *,
    pool=None,
    deck=None,
    ascii_only: bool = False,
) -> int:
    """Run one fight. Returns the process exit code (0 win, 1 loss, 2 quit)."""
    app = MinispireApp(seed=seed, pool=pool, deck=deck, ascii_only=ascii_only)
    result = app.run()
    return result if isinstance(result, int) else app.exit_code
