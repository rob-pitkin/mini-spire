"""Tests for the evaluation harness (ROB-54). No checkpoint required."""

import numpy as np

from minispire import _core
from minispire.env import MinispireEnv
from minispire.eval import RANDOM_BASELINE_WIN_RATE, evaluate


def _first_legal_policy(obs, mask):
    return int(np.flatnonzero(mask)[0])


def _end_turn_policy(obs, mask):
    return MinispireEnv.NUM_ACTIONS - 1


def test_evaluate_returns_well_formed_dict():
    results = evaluate(_first_legal_policy, n_seeds=10)
    assert results["n_seeds"] == 10
    assert results["wins"] + results["losses"] == 10
    assert 0.0 <= results["win_rate"] <= 1.0
    assert len(results["per_seed"]) == 10
    assert results["random_baseline_win_rate"] == RANDOM_BASELINE_WIN_RATE
    # Per-seed records carry the expected fields.
    rec = results["per_seed"][0]
    for key in ("seed", "won", "final_hp", "hp_fraction", "length"):
        assert key in rec


def test_evaluate_is_deterministic():
    a = evaluate(_first_legal_policy, n_seeds=20)
    b = evaluate(_first_legal_policy, n_seeds=20)
    assert a["win_rate"] == b["win_rate"]
    assert a["mean_hp_retained_on_win"] == b["mean_hp_retained_on_win"]
    assert a["per_seed"] == b["per_seed"]


def test_hp_retained_is_win_conditioned():
    # mean_hp_retained_on_win must average only the won episodes — a losing
    # episode (hp_fraction 0) must not drag it down.
    results = evaluate(_first_legal_policy, n_seeds=50)
    wins = [r["hp_fraction"] for r in results["per_seed"] if r["won"]]
    if wins:
        expected = float(np.mean(wins))
        assert abs(results["mean_hp_retained_on_win"] - expected) < 1e-6
    else:
        assert results["mean_hp_retained_on_win"] == 0.0


def test_end_turn_policy_loses_with_zero_hp_retained():
    # Always ending the turn -> always lose -> no won episodes -> HP retained 0.
    results = evaluate(_end_turn_policy, n_seeds=10)
    assert results["wins"] == 0
    assert results["mean_hp_retained_on_win"] == 0.0
    # Every per-seed loss has hp_fraction 0 (character died).
    assert all(r["hp_fraction"] == 0.0 for r in results["per_seed"])


def test_per_seed_hp_fraction_matches_final_hp():
    results = evaluate(_first_legal_policy, n_seeds=20)
    for rec in results["per_seed"]:
        assert rec["hp_fraction"] == rec["final_hp"] / 80.0 or abs(
            rec["hp_fraction"] - rec["final_hp"] / 80.0
        ) < 1e-6
