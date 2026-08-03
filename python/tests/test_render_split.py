"""The build_* / render_* split in screen.py (ROB-83).

Textual draws Rich renderables natively, so the migration can reuse the existing
Panel/Table/Text code instead of reimplementing it — but only because building a
renderable is now separate from printing it. Before this split every renderer
wrote straight to a Console, which a widget's render() cannot do.

These tests pin both halves: build_* returns something a widget can return, and
render_* still behaves for the print-and-prompt loop that has not migrated yet.
"""
from __future__ import annotations

import io

from rich.console import Console
from rich.panel import Panel

from minispire.env import MinispireEnv
from minispire.render import screen


def _env(seed: int = 5):
    env = MinispireEnv()
    obs, _ = env.reset(seed=seed)
    return env, obs


def _sink() -> Console:
    return Console(file=io.StringIO(), width=100)


def test_build_functions_return_renderables():
    env, obs = _env()
    assert isinstance(screen.build_fight(obs, env), Panel)
    piles_panel, pile_count = screen.build_piles(env)
    assert isinstance(piles_panel, Panel)
    assert isinstance(pile_count, int)
    panel, action_map = screen.build_hand(env)
    assert isinstance(panel, Panel)
    assert isinstance(action_map, list)


def test_render_wrappers_still_draw():
    # The print-and-prompt loop is still the shipping entry point; the split
    # must not have quietly broken it.
    env, obs = _env()
    console = _sink()
    screen.render_fight(console, obs, env)
    screen.render_piles(console, env)
    assert console.file.getvalue().strip() != ""


def test_hand_map_is_the_same_through_both_paths():
    # THE invariant. A keypress indexes into the action map; the player chose
    # from the panel. If the two are ever derived by separate passes they can
    # disagree, and a selection means a different card than the one on screen.
    # build_hand returns them from one pass for exactly this reason.
    env, _ = _env()
    _, built = screen.build_hand(env)
    printed = screen.render_hand(_sink(), env)
    assert built == printed


def test_hand_map_only_contains_playable_cards():
    # The map is what a number key indexes, so an unplayable card appearing in
    # it would let the player select something the engine will reject.
    env, _ = _env()
    _, action_map = screen.build_hand(env)
    mask = env.action_masks()
    for card_id in action_map:
        assert screen.card_playable(mask, card_id)
