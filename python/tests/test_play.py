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
from minispire.render.app import resolve_card_action
from minispire.render import screen


def test_card_playable_matches_mask_for_starter_hand():
    env = MinispireEnv()
    env.reset(seed=0)
    mask = env.action_masks()
    # Strike/Bash/Defend are all in the opening hand at full energy -> playable.
    for cid in (_core.CardId.Strike, _core.CardId.Defend, _core.CardId.Bash):
        assert screen.card_playable(mask, cid) is True


def test_living_enemy_slots_single_enemy():
    _env, obs = make_single_enemy_env()
    # The fixture is exactly one living enemy in slot 0.
    assert screen.living_enemy_slots(obs) == [0]


def test_resolve_targeted_card_auto_targets_single_enemy():
    """A targeted card with one living enemy encodes card*N + 0, no prompt."""
    env, obs = make_single_enemy_env()
    n = _core.CombatEnv.MAX_ENEMIES
    action = resolve_card_action(_core.CardId.Strike, screen.living_enemy_slots(obs))
    assert action == int(_core.CardId.Strike) * n + 0
    # And the engine agrees it's legal.
    assert env.action_masks()[action]


def test_resolve_untargeted_card_uses_offset_zero():
    env = MinispireEnv()
    obs, _ = env.reset(seed=0)
    n = _core.CombatEnv.MAX_ENEMIES
    action = resolve_card_action(_core.CardId.Defend, screen.living_enemy_slots(obs))
    assert action == int(_core.CardId.Defend) * n + 0
    assert env.action_masks()[action]


def test_resolved_action_steps_without_error():
    """End-to-end: render_hand -> resolve -> step actually advances the env."""
    env, obs = make_single_enemy_env()
    # Play Strike via the same path the TUI uses.
    action = resolve_card_action(_core.CardId.Strike, screen.living_enemy_slots(obs))
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
    """The TUI computes global actions from these; they must not drift."""
    import minispire._core as _core

    ce = _core.CombatEnv
    assert ce.END_TURN_ACTION == ce.NUM_CARD_TYPES * ce.MAX_ENEMIES
    assert ce.FIRST_OPTION_SLOT == ce.END_TURN_ACTION + 1
    assert ce.DECLINE_ACTION == ce.FIRST_OPTION_SLOT + ce.NUM_OPTION_SLOTS
    assert ce.NUM_ACTIONS == ce.DECLINE_ACTION + 1
    # End turn is NOT the last index — the whole reason this bug class existed.
    assert ce.END_TURN_ACTION != ce.NUM_ACTIONS - 1
