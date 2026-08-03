"""Tests for keypress -> intent translation (ROB-83 part 1).

These need no terminal, which is the whole reason the translation is a separate
pure function: `test_play.py` states the interactive loop is not testable, and
this is the part of it that carries the decisions.
"""
from __future__ import annotations

import pytest

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


# --- global keys -----------------------------------------------------------


@pytest.mark.parametrize("mode", list(Mode))
@pytest.mark.parametrize("key,expected", [("q", Quit()), ("Q", Quit()),
                                          ("p", TogglePiles()), ("P", TogglePiles()),
                                          ("escape", Cancel())])
def test_global_keys_work_in_every_mode(mode, key, expected):
    # Quit especially: a player must be able to leave from any screen, including
    # a half-finished targeting prompt.
    assert key_to_intent(key, mode=mode, option_count=3) == expected


# --- pile view is read-only ------------------------------------------------


@pytest.mark.parametrize("key", ["0", "3", "enter"])
def test_pile_view_ignores_ACTION_keys(key):
    # THE regression this module exists to prevent. In the old loop a digit
    # pressed while browsing piles was stopped by an early `continue`; losing
    # that guard means browsing your discard pile silently plays a card.
    assert key_to_intent(key, mode=Mode.PILES, option_count=5, focus=1) is None


@pytest.mark.parametrize("key", ["left", "right", "up", "down"])
def test_pile_view_still_allows_MOVEMENT(key):
    # Browsing a pile means moving a highlight over cards to read them. The
    # first version returned early for every key in PILES, which blocked the
    # action keys correctly and made the pile view unbrowsable at the same
    # time — the guard has to stop acting, not looking.
    assert key_to_intent(key, mode=Mode.PILES, option_count=5, focus=1) is not None


def test_pile_view_still_exits():
    assert key_to_intent("p", mode=Mode.PILES, option_count=5) == TogglePiles()
    assert key_to_intent("q", mode=Mode.PILES, option_count=5) == Quit()


# --- number keys -----------------------------------------------------------


@pytest.mark.parametrize("mode", [Mode.PLAY, Mode.CHOOSE])
def test_digits_select_directly(mode):
    # Numbers stay alongside arrows: for a 3-card hand, pressing 2 beats
    # arrowing to it.
    assert key_to_intent("0", mode=mode, option_count=4) == Select(0)
    assert key_to_intent("3", mode=mode, option_count=4) == Select(3)


def test_digit_past_the_end_does_nothing():
    # A 3-option screen must not act on "7". The engine would reject it, but the
    # rejection belongs here, before an action index is ever built.
    assert key_to_intent("7", mode=Mode.PLAY, option_count=3) is None


def test_no_options_means_no_selection():
    assert key_to_intent("0", mode=Mode.CHOOSE, option_count=0) is None
    assert key_to_intent("enter", mode=Mode.CHOOSE, option_count=0, focus=0) is None


# --- focus movement --------------------------------------------------------


@pytest.mark.parametrize("key", ["left", "shift+tab"])
def test_backward_keys_step_one_option(key):
    assert key_to_intent(key, mode=Mode.PLAY, option_count=5, focus=2) == MoveFocus(-1)


@pytest.mark.parametrize("key", ["right", "tab"])
def test_forward_keys_step_one_option(key):
    assert key_to_intent(key, mode=Mode.PLAY, option_count=5, focus=2) == MoveFocus(1)


def test_up_and_down_move_by_rows_not_by_one():
    # The hand is drawn as a grid. Treating down as +1 made the highlight crawl
    # sideways when the player expected it to drop to the card below — the
    # first thing noticed on playing it.
    assert key_to_intent("down", mode=Mode.PLAY, option_count=6, focus=0) == MoveRow(1)
    assert key_to_intent("up", mode=Mode.PLAY, option_count=6, focus=4) == MoveRow(-1)


def test_row_movement_carries_no_row_width():
    # MoveRow reports intent only. How wide a row is belongs to whoever drew the
    # grid, so this module stays free of layout the same way it is free of game
    # rules — the delta is in ROWS, not options.
    assert key_to_intent("down", mode=Mode.PLAY, option_count=99, focus=0).delta == 1


# --- confirm ---------------------------------------------------------------


def test_enter_confirms_the_focused_option():
    assert key_to_intent("enter", mode=Mode.PLAY, option_count=5, focus=3) == Confirm(3)


def test_enter_with_stale_focus_does_nothing():
    # The failure this guards: focus sits on index 4, the player plays a card,
    # the hand shrinks to 3 options, and Enter would otherwise confirm whatever
    # slid into that position — most likely End Turn.
    assert key_to_intent("enter", mode=Mode.PLAY, option_count=3, focus=4) is None
    assert key_to_intent("enter", mode=Mode.PLAY, option_count=3, focus=-1) is None


def test_confirm_carries_the_index():
    # Confirm reports which option it took rather than leaving the caller to
    # re-read focus, so the two cannot disagree about what was selected.
    assert key_to_intent("enter", mode=Mode.CHOOSE, option_count=2, focus=1).index == 1


# --- unbound keys ----------------------------------------------------------


@pytest.mark.parametrize("key", ["x", "f1", "ctrl+z", "", "space"])
def test_unbound_keys_are_ignored_not_errors(key):
    # Every unhandled keypress in a TUI has to be survivable; an exception here
    # would crash the app on a stray keystroke.
    assert key_to_intent(key, mode=Mode.PLAY, option_count=3, focus=0) is None


def test_targeting_and_card_choice_are_the_same_mode():
    # The design claim, made concrete: this module cannot tell "pick an enemy"
    # from "pick a card to upgrade", because it does not know what an option is.
    # Both are CHOOSE, and identical inputs give identical intents.
    target = key_to_intent("1", mode=Mode.CHOOSE, option_count=3)
    upgrade = key_to_intent("1", mode=Mode.CHOOSE, option_count=3)
    assert target == upgrade == Select(1)
