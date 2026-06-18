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
