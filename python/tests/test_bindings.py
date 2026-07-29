"""Every C++ enum value must be nameable from Python.

This file exists because the same bug shipped twice in one day (ROB-88, ROB-87):
a value was added to a C++ enum, the observation grew a slot for it, and the
pybind11 ``.value()`` line was forgotten — leaving obs and action slots that
nothing on the Python side could refer to. Both times the whole suite stayed
green, because no test looked, and both were caught only by manually poking a
live import.

The counts come from the C++ headers while the members come from explicit
``.value()`` calls in bindings/_core.cc, so the two can only agree if every
value was bound. That is what makes these tests catch the omission rather than
restate it.
"""

from minispire import _core


def _members(enum):
    return enum.__members__


def test_every_card_id_is_bound():
    # kNumCardTypes drives the obs pile-plane stride and the action space, so an
    # unbound CardId means real slots exist that Python cannot name.
    assert len(_members(_core.CardId)) == _core.CombatEnv.NUM_CARD_TYPES


def test_card_id_values_are_dense_and_contiguous():
    # Stronger than a count: catches a duplicated or mistyped binding, where the
    # total still matches but one value is bound twice and another not at all.
    values = sorted(int(v) for v in _members(_core.CardId).values())
    assert values == list(range(_core.CombatEnv.NUM_CARD_TYPES))


def test_every_bound_card_id_has_a_name():
    # card_name() reads CARD_DATABASE, so this fails if a bound id has no row.
    for name, value in _members(_core.CardId).items():
        assert _core.card_name(value), f"{name} has no CARD_DATABASE entry"


def test_every_debuff_is_bound():
    # The None sentinel is deliberately unbound and excluded from kNumDebuffs.
    assert len(_members(_core.Debuff)) == _core.CombatEnv.NUM_DEBUFFS


def test_every_player_power_is_bound():
    assert len(_members(_core.Power)) == _core.CombatEnv.NUM_PLAYER_POWERS


def test_obs_and_action_sizes_match_the_layout_constants():
    # Guards against a stale extension as much as a layout slip: scikit-build-core
    # caches the built module, and a cached build makes a shape change look green
    # everywhere else. Measuring a LIVE env against the constants catches it.
    env = _core.single_enemy_fixture_env(0)
    result = env.reset(0)
    obs = result[0] if isinstance(result, tuple) else result
    assert len(obs) == _core.CombatEnv.OBS_SIZE
    assert len(env.action_mask()) == _core.CombatEnv.NUM_ACTIONS


def test_obs_size_matches_its_component_blocks():
    # Recomputes OBS_SIZE from its parts, so a block that grows without the total
    # being updated (or vice versa) cannot pass quietly.
    env_cls = _core.CombatEnv
    piles = 5 * env_cls.NUM_CARD_TYPES  # 4 piles + the free-this-turn plane
    choice = 5 + env_cls.NUM_CARD_TYPES * 3
    expected = (
        env_cls.PLAYER_OBS_SIZE
        + env_cls.MAX_ENEMIES * env_cls.ENEMY_OBS_STRIDE
        + piles
        + 1  # turn number
        + choice
    )
    assert expected == env_cls.OBS_SIZE
