"""Display-correctness regressions found by playing (ROB-83).

Both bugs here were invisible to the test suite and obvious within one fight,
which is the argument for the TUI existing at all. Both are also the same
shape: a number rendered from the wrong source.
"""
from __future__ import annotations

import numpy as np

from minispire import _core
from minispire.env import MinispireEnv
from minispire.render import screen


def test_turn_index_is_not_the_last_obs_slot():
    # The choice block sits AFTER the turn float, so OBS_SIZE - 1 lands inside
    # it. The TUI computed the index that way and rendered a choice-channel
    # value — zero unless a choice was pending — as the turn counter.
    #
    # Identical in shape to the kEndTurnAction bug CLAUDE.md records: a landmark
    # that used to be last, displaced by a block appended after it.
    assert screen.TURN_NUMBER == _core.CombatEnv.TURN_OBS_INDEX
    assert screen.TURN_NUMBER != _core.CombatEnv.OBS_SIZE - 1


def test_turn_counter_actually_advances():
    # What the player sees. Ending a turn must move the number on screen.
    env = MinispireEnv()
    obs, _ = env.reset(seed=1)
    start = int(obs[screen.TURN_NUMBER])
    obs, *_ = env.step(_core.CombatEnv.END_TURN_ACTION)
    assert int(obs[screen.TURN_NUMBER]) > start


def test_turn_index_agrees_with_the_engine_state():
    env = MinispireEnv()
    obs, _ = env.reset(seed=2)
    for _ in range(4):
        assert int(obs[screen.TURN_NUMBER]) == env.turn_number
        obs, _r, terminated, truncated, _i = env.step(_core.CombatEnv.END_TURN_ACTION)
        if terminated or truncated:
            break


def test_effective_cost_reflects_a_free_this_turn_grant():
    # Infernal Blade adds a random Attack that costs 0 for the turn. The hand
    # showed CardData.cost and so advertised a price the engine would not
    # charge — the player reads 1, pays 0.
    env = MinispireEnv(deck=[_core.CardId.InfernalBlade] * 5)
    env.reset(seed=1)
    blade = _core.CardId.InfernalBlade
    legal = [
        a for a in np.flatnonzero(env.action_masks())
        if a // _core.CombatEnv.MAX_ENEMIES == int(blade)
    ]
    assert legal, "Infernal Blade should be playable on turn 1"
    env.step(int(legal[0]))

    granted = [c for c in env.state_piles().hand if c != blade]
    assert granted, "Infernal Blade should have added an Attack"
    card = granted[0]
    assert env.effective_cost(card) == 0
    assert _core.card_data(card).cost > 0, "pick a card whose base cost differs"


def test_hand_panel_prints_the_effective_cost():
    # The panel is what the player reads, so the fix has to reach the render,
    # not just the accessor.
    env = MinispireEnv(deck=[_core.CardId.InfernalBlade] * 5)
    env.reset(seed=1)
    blade = _core.CardId.InfernalBlade
    legal = [
        a for a in np.flatnonzero(env.action_masks())
        if a // _core.CombatEnv.MAX_ENEMIES == int(blade)
    ]
    env.step(int(legal[0]))
    granted = [c for c in env.state_piles().hand if c != blade]
    assert granted

    panel, _action_map = screen.build_hand(env)
    from rich.console import Console

    console = Console(file=__import__("io").StringIO(), width=120)
    console.print(panel)
    text = console.file.getvalue()
    name = _core.card_name(granted[0])
    line = next(ln for ln in text.splitlines() if name in ln)
    assert "{0}" in line, f"expected a free cost in {line!r}"
