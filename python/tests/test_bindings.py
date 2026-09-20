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


#: The one bound CardId that is NOT a card: the "no source card" sentinel a
#: PendingChoice carries when a relic (Toolbox) opened the menu rather than a
#: card. Python cannot spell ``CardId.None``, hence getattr.
NO_SOURCE_CARD = getattr(_core.CardId, "None")


def test_every_card_id_is_bound():
    # kNumCardTypes drives the obs pile-plane stride and the action space, so an
    # unbound CardId means real slots exist that Python cannot name. The +1 is
    # the sentinel, which is deliberately NOT part of the card vocabulary.
    assert len(_members(_core.CardId)) == _core.CombatEnv.NUM_CARD_TYPES + 1


def test_card_id_values_are_dense_and_contiguous():
    # Stronger than a count: catches a duplicated or mistyped binding, where the
    # total still matches but one value is bound twice and another not at all.
    values = sorted(int(v) for v in _members(_core.CardId).values())
    assert values == list(range(_core.CombatEnv.NUM_CARD_TYPES + 1))


def test_the_no_card_sentinel_sits_past_the_vocabulary():
    # It has to be the LAST value. The action space is sized NUM_CARD_TYPES
    # wide, so a sentinel anywhere else would occupy a real card's index and
    # shift every card above it.
    assert int(NO_SOURCE_CARD) == _core.CombatEnv.NUM_CARD_TYPES


def test_every_bound_card_id_has_a_name():
    # card_name() reads CARD_DATABASE, so this fails if a bound id has no row.
    #
    # The sentinel is excluded EXPLICITLY rather than left in: it has no row,
    # and card_name returns "(none)" for it, so leaving it in would make this
    # test pass on the fallback string and quietly stop checking the database.
    for name, value in _members(_core.CardId).items():
        if value == NO_SOURCE_CARD:
            continue
        assert _core.card_name(value), f"{name} has no CARD_DATABASE entry"


def test_the_sentinel_has_no_card_name():
    # Total rather than throwing: card_name is CARD_DATABASE.at() underneath,
    # and the TUI calls it on a choice's source_card.
    assert _core.card_name(NO_SOURCE_CARD) == "(none)"


def test_every_debuff_is_bound():
    # The None sentinel is deliberately unbound and excluded from kNumDebuffs.
    assert len(_members(_core.Debuff)) == _core.CombatEnv.NUM_DEBUFFS


def test_every_player_power_is_bound():
    assert len(_members(_core.Power)) == _core.CombatEnv.NUM_PLAYER_POWERS


def test_every_choice_kind_is_bound():
    # Five kinds shipped unbound (the colorless batches and Toolbox), so the
    # TUI could not name them and test_choice_prompts_cover_every_choice_kind
    # passed by iterating only the half that WAS bound. The count comes from
    # the C++ header, which is what makes this catch an omission rather than
    # restate it. ChoiceKind::None is bound, so it is included here.
    assert len(_members(_core.ChoiceKind)) == _core.CombatEnv.NUM_CHOICE_KINDS


def test_choice_kind_values_are_dense_and_contiguous():
    values = sorted(int(v) for v in _members(_core.ChoiceKind).values())
    assert values == list(range(_core.CombatEnv.NUM_CHOICE_KINDS))


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
