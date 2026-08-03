"""Keypress -> intent translation for the TUI (ROB-83 part 1).

A pure function, deliberately: `App.run_test()` can drive a real terminal, but
a table of (key, mode) -> intent needs no terminal at all, and the interactive
loop is the part of the old TUI that `test_play.py` declares untestable.
Keeping the decision here means the widgets only have to render.

It knows nothing about the game. It does not know what a card is, that some
cards need a target, or that a choice is pending — only that the screen is
showing N options and the player pressed something. Targeting and a mid-card
choice are therefore the SAME mode to it: pick one of N. Which of the two is
happening, and what to do with the answer, is the app's business.

That split is what keeps this testable. The moment this function knows a card
needs a target, it needs to remember a half-finished selection, and a pure
function with memory is just a state machine wearing a hat.
"""
from __future__ import annotations

import enum
from dataclasses import dataclass


class Mode(enum.Enum):
    """What the screen is currently showing.

    CHOOSE covers every "pick one of N" screen — enemy targeting, Armaments'
    upgrade menu, Exhume's pile. They differ in what the options MEAN, which is
    exactly the thing this module is not allowed to care about.
    """

    PLAY = "play"      # hand + end turn
    PILES = "piles"    # read-only pile inspection; action keys are inert
    CHOOSE = "choose"  # pick one of N


@dataclass(frozen=True)
class Select:
    """Pick option `index` outright (a number key)."""

    index: int


@dataclass(frozen=True)
class MoveFocus:
    """Move the highlight by `delta`, wrapping at the ends."""

    delta: int


@dataclass(frozen=True)
class Confirm:
    """Take the focused option (Enter). Carries the index so the caller does
    not have to re-read focus and risk disagreeing about it."""

    index: int


class _Singleton:
    __slots__ = ()

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return type(self).__name__

    def __eq__(self, other: object) -> bool:
        return type(self) is type(other)

    def __hash__(self) -> int:
        return hash(type(self))


class Cancel(_Singleton):
    """Back out of the current screen without acting (Escape)."""


class TogglePiles(_Singleton):
    """Show or hide the pile view."""


class Quit(_Singleton):
    """Leave the game."""


Intent = Select | MoveFocus | Confirm | Cancel | TogglePiles | Quit

# Textual's key names. Both arrow axes move the selection: a hand reads
# left-to-right, but a pile list reads top-to-bottom, and the player should not
# have to think about which widget they are in.
_BACK = {"left", "up", "shift+tab"}
_FORWARD = {"right", "down", "tab"}


def key_to_intent(
    key: str,
    *,
    mode: Mode,
    option_count: int,
    focus: int = 0,
) -> Intent | None:
    """Translate a keypress into an intent, or None if the key does nothing.

    `option_count` is however many things are selectable on screen right now,
    including End Turn when the hand is showing — the caller decides what the
    options are and keeps them in the same order it rendered them.

    None is returned rather than raising: an unbound key is the normal case,
    not an error, and every unhandled keypress in a TUI has to be survivable.
    """
    if key in ("q", "Q"):
        return Quit()
    if key in ("p", "P"):
        return TogglePiles()
    if key == "escape":
        return Cancel()

    # Pile view is read-only. Without this, a digit pressed while browsing piles
    # steps the environment — the old loop guarded it with an early `continue`,
    # and that guard is exactly the kind that gets lost moving into a widget
    # tree, because nothing fails loudly when it goes missing.
    if mode is Mode.PILES:
        return None

    if option_count <= 0:
        return None

    if key in _BACK:
        return MoveFocus(-1)
    if key in _FORWARD:
        return MoveFocus(1)

    if key == "enter":
        # A stale focus (the hand shrank under it) must not select whatever
        # happens to sit at that index now.
        return Confirm(focus) if 0 <= focus < option_count else None

    if len(key) == 1 and key.isdigit():
        index = int(key)
        return Select(index) if index < option_count else None

    return None
