"""Tests for the trajectory behavioral-analysis tooling (ROB-56)."""

import numpy as np

from analysis.trajectory import action_name, analyze, replay
from minispire import _core
from minispire.env import MinispireEnv

_END_TURN = MinispireEnv.NUM_ACTIONS - 1


def _first_legal_policy(obs, mask):
    return int(np.flatnonzero(mask)[0])


def _end_turn_policy(obs, mask):
    return _END_TURN


def test_action_name_known_and_unknown():
    assert action_name(int(_core.CardId.Strike)) == "Strike"
    assert action_name(_END_TURN) == "EndTurn"
    assert action_name(999).startswith("?")


def test_replay_records_steps_and_outcome():
    ep = replay(_first_legal_policy, seed=0)
    assert ep.seed == 0
    assert len(ep.steps) > 0
    assert isinstance(ep.won, bool)
    # Each step carries action + intent state.
    s = ep.steps[0]
    assert hasattr(s, "action_label")
    assert hasattr(s, "enemy_attacking")


def test_replay_is_deterministic():
    a = replay(_first_legal_policy, seed=3)
    b = replay(_first_legal_policy, seed=3)
    assert [s.action for s in a.steps] == [s.action for s in b.steps]
    assert a.won == b.won


def test_end_turn_policy_only_plays_end_turn():
    ep = replay(_end_turn_policy, seed=0)
    assert all(s.action_label == "EndTurn" for s in ep.steps)
    assert ep.won is False  # passing every turn loses


def test_analyze_returns_expected_keys():
    stats = analyze(_first_legal_policy, n_seeds=20)
    for key in (
        "win_rate",
        "bash_before_strike_rate",
        "defend_usage_rate",
        "mean_actions_per_episode",
        "card_play_counts",
    ):
        assert key in stats
    assert 0.0 <= stats["bash_before_strike_rate"] <= 1.0
    assert 0.0 <= stats["defend_usage_rate"] <= 1.0


def test_end_turn_policy_has_zero_defend_usage():
    stats = analyze(_end_turn_policy, n_seeds=10)
    assert stats["defend_usage_rate"] == 0.0
    assert stats["win_rate"] == 0.0
