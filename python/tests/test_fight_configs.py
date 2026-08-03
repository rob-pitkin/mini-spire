"""Every scenario in fights/ must actually load (ROB-83).

The README pointed at `fights/nob.yaml` for weeks and the directory did not
exist — a documented file nobody ran. These configs are only useful if they
work, and a broken one fails at the worst moment: when someone sits down to
play-test a mechanic.
"""
from __future__ import annotations

import pathlib

import pytest

from minispire.env import MinispireEnv
from minispire.play import _load_config, _parse_deck, _parse_pool, _parse_seed

FIGHTS = sorted((pathlib.Path(__file__).parents[2] / "fights").glob("*.yaml"))


def test_there_are_fight_configs():
    # Guard against the glob silently finding nothing and every test below
    # vacuously passing.
    assert FIGHTS, "no fights/*.yaml found"


@pytest.mark.parametrize("path", FIGHTS, ids=lambda p: p.name)
def test_config_loads_and_starts_a_fight(path):
    cfg = _load_config(str(path))
    seed = _parse_seed(str(cfg["seed"]) if cfg.get("seed") is not None else None)
    pool = _parse_pool(cfg.get("pool"))
    deck = _parse_deck(cfg.get("deck"))

    kwargs = {}
    if pool is not None:
        kwargs["pool"] = pool
    if deck is not None:
        kwargs["deck"] = deck
    env = MinispireEnv(**kwargs)
    obs, _ = env.reset(seed=seed)

    assert obs.shape[0] > 0
    assert env.state_piles().hand, "the fight should deal an opening hand"
    # An unknown card name raises SystemExit in _parse_deck, so reaching here
    # means every card in the file resolved against the real CardId enum.
