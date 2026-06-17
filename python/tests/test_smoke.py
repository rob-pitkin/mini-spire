"""Smoke tests that verify the build + import surface, plus CombatEnv basics."""

import numpy as np
import pytest

import minispire


def test_package_imports():
    assert minispire.__version__ == "0.1.0"


def test_core_extension_loadable():
    from minispire import _core
    assert _core is not None


def test_combat_env_constructs_and_resets():
    env = minispire._core.CombatEnv()
    obs, info = env.reset(seed=0)
    assert obs.shape == (env.OBS_SIZE,)
    assert obs.shape == (45,)
    assert obs.dtype == np.float32
    assert isinstance(info, dict)


def test_action_mask_shape_and_dtype():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    mask = env.action_mask()
    assert mask.shape == (env.NUM_ACTIONS,)
    assert mask.shape == (7,)
    assert mask.dtype == np.bool_


def test_end_turn_always_legal_at_start():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    end_turn = env.NUM_ACTIONS - 1
    assert env.action_mask()[end_turn]


def test_step_returns_gym_tuple():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    end_turn = env.NUM_ACTIONS - 1
    result = env.step(end_turn)
    assert len(result) == 5
    obs, reward, terminated, truncated, info = result
    assert obs.shape == (45,)
    assert obs.dtype == np.float32
    assert isinstance(reward, float)
    assert isinstance(terminated, bool)
    assert isinstance(truncated, bool)
    assert "outcome" in info
    assert "turn_number" in info


def test_step_raises_on_invalid_action():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    with pytest.raises(ValueError):
        env.step(-1)
    with pytest.raises(ValueError):
        env.step(env.NUM_ACTIONS)


def test_clone_produces_independent_env():
    env = minispire._core.CombatEnv()
    env.reset(seed=7)
    copy = env.clone()
    end_turn = env.NUM_ACTIONS - 1
    copy.step(end_turn)
    # Original unaffected.
    assert env.turn_number == 1
    assert copy.turn_number == 2


def test_obs_slot_zero_is_character_hp():
    """Per ROB-40 layout: slot 0 = character.hp, slot 1 = max_hp."""
    env = minispire._core.CombatEnv()
    obs, _ = env.reset(seed=0)
    assert obs[0] == 80.0  # Ironclad starting HP
    assert obs[1] == 80.0  # max HP


def test_obs_view_reflects_state_after_step():
    env = minispire._core.CombatEnv()
    obs, _ = env.reset(seed=0)
    hp_before = float(obs[0])
    end_turn = env.NUM_ACTIONS - 1
    new_obs, _, _, _, _ = env.step(end_turn)
    # Character either took damage or has block; HP shouldn't increase
    # without a heal effect (none in v1).
    assert float(new_obs[0]) <= hp_before


# ---------------------------------------------------------------------------
# state_piles + CardId enum (ROB-46)
# ---------------------------------------------------------------------------


def test_state_piles_after_reset():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    piles = env.state_piles()
    assert len(piles.hand) == 5
    # draw is a count map (dict-like); total count across CardIds is 5.
    assert sum(piles.draw.values()) == 5
    assert len(piles.discard) == 0
    assert len(piles.exhaust) == 0


def test_state_piles_items_are_card_id_enum():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    piles = env.state_piles()
    for card_id in piles.hand:
        assert isinstance(card_id, minispire._core.CardId)
    for card_id in piles.draw:
        assert isinstance(card_id, minispire._core.CardId)


def test_card_id_enum_has_expected_values():
    CardId = minispire._core.CardId
    # All 6 v1 variants exist.
    assert CardId.Strike is not None
    assert CardId.Defend is not None
    assert CardId.Bash is not None
    assert CardId.StrikePlus is not None
    assert CardId.DefendPlus is not None
    assert CardId.BashPlus is not None


def test_state_piles_total_matches_starter_deck():
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    piles = env.state_piles()
    CardId = minispire._core.CardId

    def total_for(card_id):
        in_hand = sum(1 for c in piles.hand if c == card_id)
        in_draw = piles.draw.get(card_id, 0)
        return in_hand + in_draw

    assert total_for(CardId.Strike) == 5
    assert total_for(CardId.Defend) == 4
    assert total_for(CardId.Bash) == 1


def test_state_piles_draw_does_not_leak_order():
    """The draw pile is exposed as a count map, not an ordered sequence."""
    env = minispire._core.CombatEnv()
    env.reset(seed=0)
    piles = env.state_piles()
    # draw should not be a list/tuple — it's a dict-like count map.
    assert not isinstance(piles.draw, (list, tuple))
