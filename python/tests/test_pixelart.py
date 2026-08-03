"""Half-block sprite rendering (ROB-83).

Colour cannot be asserted meaningfully, but structure can — and structure is
where a sprite goes wrong silently: a ragged grid shifts every row below it,
and a missing palette entry renders a hole that looks like intentional
transparency.
"""
from __future__ import annotations

import pytest

from minispire.render.pixelart import Sprite

PALETTE = {"normal": {"#": "rgb(80,180,90)", ".": "rgb(40,90,45)", " ": None}}


def test_two_pixel_rows_per_text_row():
    # The whole point of half-blocks: a 4-pixel-tall sprite costs 2 text rows.
    sprite = Sprite(["####", "....", "####", "...."], PALETTE)
    assert sprite.height == 4
    assert len(sprite.render().plain.split("\n")) == 2


def test_odd_height_does_not_drop_the_last_row():
    # An odd pixel height has no partner row for the final line; it must render
    # as a top-half block rather than vanishing.
    sprite = Sprite(["####", "....", "####"], PALETTE)
    lines = sprite.render().plain.split("\n")
    assert len(lines) == 2
    assert lines[-1].strip() != ""


def test_ragged_rows_are_rejected_at_construction():
    # A row one character short shifts everything below it, and the result
    # still renders — it just looks subtly wrong. Fail loudly at authoring time
    # instead, since these grids are written by hand.
    with pytest.raises(ValueError, match="ragged"):
        Sprite(["####", "...", "####", "...."], PALETTE)


def test_a_sprite_must_define_normal():
    with pytest.raises(ValueError, match="normal"):
        Sprite(["##", "##"], {"hurt": {"#": "red"}})


def test_unknown_state_falls_back_to_normal():
    # A new state must not render blank. Falling back means adding "poisoned"
    # before its palette exists degrades to the normal look, not to nothing.
    sprite = Sprite(["####", "####"], PALETTE)
    assert sprite.render("poisoned").plain == sprite.render("normal").plain


def test_palettes_reuse_one_grid():
    # The reason for indexed colour: a damaged enemy is a recolour, not a
    # second drawing that has to be kept in sync with the first.
    sprite = Sprite(
        ["####", "####"],
        {"normal": {"#": "rgb(80,180,90)"}, "hurt": {"#": "rgb(180,60,50)"}},
    )
    assert sprite.render("normal").plain == sprite.render("hurt").plain
    normal_styles = [s.style for s in sprite.render("normal").spans]
    hurt_styles = [s.style for s in sprite.render("hurt").spans]
    assert normal_styles != hurt_styles


def test_transparent_pixels_render_as_gaps():
    # Empty must be a gap, not a black square — the panel background shows
    # through, so a sprite can sit on any surface.
    sprite = Sprite(["    ", "    "], PALETTE)
    assert sprite.render().plain.strip() == ""


def test_empty_sprite_is_rejected():
    with pytest.raises(ValueError):
        Sprite([], PALETTE)


def test_state_rows_swap_the_grid_not_the_palette():
    # Injury is shown by expression, not by recolouring. Tinting whole bodies
    # red was tried and flattened every enemy into the same wounded mush, so a
    # hurt Cultist read as a hurt Jaw Worm instead of as a Cultist.
    sprite = Sprite(
        ["#o#", "###"],
        PALETTE | {"normal": {"#": "green", "o": "white", "!": "red", " ": None}},
        state_rows={"hurt": ["#!#", "###"]},
    )
    assert sprite.render("hurt").plain == sprite.render("normal").plain
    assert sprite.render("hurt").spans != sprite.render("normal").spans


def test_state_rows_must_match_the_base_grid():
    # A differently-shaped alternate grid would make the creature jump size the
    # instant it crossed the HP threshold.
    with pytest.raises(ValueError, match="match the base grid"):
        Sprite(["####", "####"], PALETTE, state_rows={"hurt": ["###", "###"]})
    with pytest.raises(ValueError, match="match the base grid"):
        Sprite(["####", "####"], PALETTE, state_rows={"hurt": ["####"]})


def test_a_state_without_alternate_rows_uses_the_base_grid():
    sprite = Sprite(["####", "####"], PALETTE)
    assert sprite.render("hurt").plain == sprite.render("normal").plain
