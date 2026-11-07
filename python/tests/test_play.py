"""Unit tests for the TUI's pure formatter helpers.

The interactive loop and rendered output are not tested here — rich output
is fragile across versions. We test the logic functions that compute display
content.
"""

from minispire.render import avatars
from minispire.render.intent import intent_text
from minispire.render.screen import format_hp_bar


# ---------------------------------------------------------------------------
# HP bar
# ---------------------------------------------------------------------------


def test_format_hp_bar_full():
    bar = format_hp_bar(80, 80, width=16)
    # Full HP -> all 16 segments filled, no empties.
    assert bar.plain == "[" + "#" * 16 + "]"


def test_format_hp_bar_empty():
    bar = format_hp_bar(0, 80, width=16)
    assert bar.plain == "[" + "-" * 16 + "]"


def test_format_hp_bar_partial_has_both():
    bar = format_hp_bar(40, 80, width=16)
    plain = bar.plain
    assert "#" in plain
    assert "-" in plain
    # Total width consistent.
    assert len(plain) == 18  # 16 + 2 brackets


def test_format_hp_bar_negative_clamps():
    bar = format_hp_bar(-5, 80, width=16)
    assert bar.plain == "[" + "-" * 16 + "]"


# ---------------------------------------------------------------------------
# Intent icons
# ---------------------------------------------------------------------------


def test_intent_attack_unicode():
    t = intent_text(
        is_attacking=True, attack_damage=11, is_blocking=False,
        is_buffing=False, ascii_only=False,
    )
    assert "11" in t.plain
    assert "⚔" in t.plain


def test_intent_attack_ascii():
    t = intent_text(
        is_attacking=True, attack_damage=11, is_blocking=False,
        is_buffing=False, ascii_only=True,
    )
    assert ">>11<<" in t.plain
    assert "⚔" not in t.plain


def test_intent_compound_attack_and_block():
    t = intent_text(
        is_attacking=True, attack_damage=7, is_blocking=True,
        is_buffing=False, ascii_only=True,
    )
    assert ">>7<<" in t.plain
    assert "[BLK]" in t.plain


def test_intent_debuff_is_distinct_from_buff():
    # STS shows different icons for "the enemy strengthens itself" and "the
    # enemy weakens you". The obs tracked them as one flag until ROB-96, so a
    # Green Louse's Spit Web rendered identically to a Red Louse's Grow.
    buff = intent_text(
        is_attacking=False, attack_damage=0, is_blocking=False,
        is_buffing=True, ascii_only=True,
    )
    debuff = intent_text(
        is_attacking=False, attack_damage=0, is_blocking=False,
        is_buffing=False, is_debuffing=True, ascii_only=True,
    )
    assert "(+)" in buff.plain
    assert "(-)" in debuff.plain
    assert buff.plain != debuff.plain


def test_intent_escape_and_split_are_not_none():
    # Both carry no damage, block or status, so they fell through to "(none)".
    flee = intent_text(
        is_attacking=False, attack_damage=0, is_blocking=False,
        is_buffing=False, is_escaping=True, ascii_only=True,
    )
    split = intent_text(
        is_attacking=False, attack_damage=0, is_blocking=False,
        is_buffing=False, is_splitting=True, ascii_only=True,
    )
    assert "[FLEE]" in flee.plain
    assert "[SPLIT]" in split.plain
    assert "(none)" not in flee.plain
    assert "(none)" not in split.plain


def test_intent_buff_only():
    t = intent_text(
        is_attacking=False, attack_damage=0, is_blocking=False,
        is_buffing=True, ascii_only=True,
    )
    assert "(+)" in t.plain


def test_intent_none():
    t = intent_text(
        is_attacking=False, attack_damage=0, is_blocking=False,
        is_buffing=False, ascii_only=False,
    )
    assert t.plain == "(none)"


# ---------------------------------------------------------------------------
# Avatars
# ---------------------------------------------------------------------------


def test_select_avatar_normal_above_threshold():
    art = avatars.select_avatar("IRONCLAD", 80, 80)
    assert art == avatars.IRONCLAD_NORMAL


def test_select_avatar_critical_below_threshold():
    art = avatars.select_avatar("IRONCLAD", 10, 80)  # 12.5%
    assert art == avatars.IRONCLAD_CRITICAL


def test_select_avatar_ko_at_zero():
    art = avatars.select_avatar("JAW_WORM", 0, 44)
    assert art == avatars.KO


# ---------------------------------------------------------------------------
# Action encoding (ROB-60/71) — the play->action->step path that the pure
# render tests do NOT cover. This is what silently broke when the action space
# changed from Discrete(7) to Discrete(25); guard it directly.
# ---------------------------------------------------------------------------

import numpy as np
import pytest

from minispire import _core
from minispire.env import MinispireEnv, make_single_enemy_env
from minispire.render.app import resolve_play_action
from minispire.render import screen


def test_every_starter_copy_is_playable_on_turn_one():
    # Every starter card costs at most 2 and turn 1 has 3 energy, so EVERY
    # copy in the opening hand is playable — an invariant of the deal, not a
    # fact about which cards seed 0 happens to draw.
    env = MinispireEnv()
    env.reset(seed=0)
    mask = env.action_masks()
    hand = env.state_piles().hand
    assert hand
    for position in range(len(hand)):
        assert screen.copy_playable(env, mask, position) is True, position


def test_living_enemy_slots_single_enemy():
    _env, obs = make_single_enemy_env()
    # The fixture is exactly one living enemy in slot 0.
    assert screen.living_enemy_slots(obs) == [0]


def test_resolve_play_action_auto_targets_the_single_enemy():
    """With one living enemy no card prompts for a target: every copy resolves
    to its own (card, cost) play at slot 0, and the engine agrees it is legal."""
    env, obs = make_single_enemy_env()
    living = screen.living_enemy_slots(obs)
    assert living == [0]
    hand = env.state_piles().hand
    for position in range(len(hand)):
        action = resolve_play_action(env, position, living)
        assert action is not None, "one living enemy: never a target prompt"
        decoded = _core.decode_action(action)
        assert decoded.card() == hand[position]
        assert decoded.target == 0
        assert env.action_masks()[action]


def test_resolve_untargeted_card_uses_slot_zero():
    # A deck of Defends, so every copy in hand is untargeted whatever is dealt.
    env = MinispireEnv(deck=[_core.CardId.Defend] * 10)
    obs, _ = env.reset(seed=0)
    living = screen.living_enemy_slots(obs)
    for position in range(len(env.state_piles().hand)):
        action = resolve_play_action(env, position, living)
        assert action is not None
        assert _core.decode_action(action).target == 0
        assert env.action_masks()[action]


def test_resolved_action_steps_without_error():
    """End-to-end: render_hand -> resolve -> step actually advances the env."""
    env, obs = make_single_enemy_env()
    # Play the first copy in hand via the same path the TUI uses. No starter
    # card ends a Jaw Worm fight on its own.
    action = resolve_play_action(env, 0, screen.living_enemy_slots(obs))
    obs2, _r, term, trunc, _info = env.step(action)
    assert obs2.shape == (MinispireEnv.OBS_SIZE,)
    assert not (term or trunc)  # one Strike doesn't end the fight


# --- CLI parse helpers (ROB-79 commit 4) ---


def test_parse_pool_maps_names():
    from minispire.play import _parse_pool

    assert _parse_pool("weak") == _core.EncounterPool.Weak
    assert _parse_pool("STRONG") == _core.EncounterPool.Strong
    assert _parse_pool("elite") == _core.EncounterPool.Elite
    assert _parse_pool(None) is None


def test_parse_pool_rejects_unknown():
    from minispire.play import _parse_pool

    with pytest.raises(SystemExit):
        _parse_pool("bogus")


def test_parse_deck_from_string_and_names():
    from minispire.play import _parse_deck

    deck = _parse_deck("strike,strike,defend,bash")
    assert deck == [
        _core.CardId.Strike,
        _core.CardId.Strike,
        _core.CardId.Defend,
        _core.CardId.Bash,
    ]
    # Display names (with '+') and enum names both resolve.
    assert _parse_deck("strike+") == [_core.CardId.StrikePlus]
    assert _parse_deck("StrikePlus") == [_core.CardId.StrikePlus]
    assert _parse_deck(None) is None
    # YAML gives a list, not a string.
    assert _parse_deck(["strike", "bash"]) == [
        _core.CardId.Strike,
        _core.CardId.Bash,
    ]


def test_parse_deck_rejects_unknown_card():
    from minispire.play import _parse_deck

    with pytest.raises(SystemExit):
        _parse_deck("strike,notacard")


# ---------------------------------------------------------------------------
# Mid-card choices (Stage 4c). Per this file's convention we test the logic and
# the engine-facing contract, not the rendered rich output.
# ---------------------------------------------------------------------------


def test_choice_prompts_cover_every_choice_kind():
    """A new ChoiceKind must not silently render a fallback prompt."""
    import minispire._core as _core
    from minispire.render.screen import CHOICE_PROMPTS

    for name in _core.ChoiceKind.__members__:
        if name == "None":
            continue
        assert name in CHOICE_PROMPTS, f"no TUI prompt for ChoiceKind.{name}"


def test_choice_view_is_inactive_during_normal_combat():
    from minispire import MinispireEnv

    env = MinispireEnv()
    env.reset(seed=0)
    view = env.choice_view()
    assert view.active is False
    assert list(view.options) == []


def test_action_layout_constants_are_consistent():
    """The v2 layout (v2-spec.md §6): published by the engine, and checked here
    by DECODING the landmarks rather than by re-deriving them with offsets."""
    import minispire._core as _core

    ce = _core.CombatEnv
    B = _core.ActionBlock
    assert ce.NUM_ACTIONS == 8613
    assert ce.NUM_COST_COLUMNS == 5
    assert ce.END_TURN_ACTION == (
        ce.NUM_CARD_TYPES * ce.NUM_COST_COLUMNS * ce.MAX_ENEMIES)
    # End turn is NOT the last index — the whole reason this bug class existed.
    assert ce.END_TURN_ACTION != ce.NUM_ACTIONS - 1
    assert _core.decode_action(ce.END_TURN_ACTION).block == B.EndTurn
    assert _core.decode_action(ce.CARD_SELECT_BLOCK).block == B.CardSelect
    assert _core.decode_action(ce.DECLINE_ACTION).block == B.Decline


def test_every_action_round_trips_through_encode_and_decode():
    """Python sees the same layout the engine does, index for index."""
    import minispire._core as _core

    for a in range(_core.CombatEnv.NUM_ACTIONS):
        d = _core.decode_action(a)
        assert _core.encode_action(d.block, d.entity, d.target, d.cost) == a, a
