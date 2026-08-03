"""Half-block pixel-art rendering for avatars (ROB-83).

A terminal cell can hold two pixels: draw `▀` with a foreground colour for the
top half and a background colour for the bottom. With 24-bit colour that is two
full-colour pixels per cell, so a 24x24 sprite costs 24 columns and 12 text
rows. It is ordinary styled text, which means Textual renders it natively — no
terminal image protocol, no Sixel, nothing terminal-specific.

Art is authored here as pixel data rather than derived from game assets. Slay
the Spire's sprites are copyrighted and this repo is meant to be a clean
open artifact, so the avatars have to be ours.

Sprites are indexed colour: a grid of single-character palette keys plus a
palette mapping key -> colour. That indirection is what makes state cheap —
a damaged or poisoned enemy is the same grid with a different palette, not a
second drawing to keep in sync with the first.
"""
from __future__ import annotations

from rich.text import Text

#: Palette key meaning "nothing here" — renders as a gap, not a black pixel.
TRANSPARENT = " "


class Sprite:
    """Indexed-colour pixel art.

    `rows` are equal-length strings of palette keys; `palettes` maps a state
    name to {key: colour}. Every sprite must define "normal"; anything else
    falls back to it, so a new state cannot render blank.

    `state_rows` supplies an ALTERNATE GRID per state — used for "hurt", where
    the creature keeps its colours and changes its face. Recolouring the body
    instead was tried first and was wrong: tinting everything red flattened
    twelve distinct palettes into the same wounded mush, so a hurt Cultist and
    a hurt Jaw Worm looked like each other rather than like themselves. Colour
    is identity here; expression is state.
    """

    def __init__(
        self,
        rows: list[str],
        palettes: dict[str, dict[str, str | None]],
        *,
        state_rows: dict[str, list[str]] | None = None,
    ):
        if not rows:
            raise ValueError("sprite has no rows")
        width = len(rows[0])
        if any(len(r) != width for r in rows):
            bad = [i for i, r in enumerate(rows) if len(r) != width]
            raise ValueError(f"ragged sprite: rows {bad} are not {width} wide")
        if "normal" not in palettes:
            raise ValueError("sprite needs a 'normal' palette")
        for state, alt in (state_rows or {}).items():
            # An alternate grid of a different shape would make the creature
            # jump size the moment it got hurt.
            if len(alt) != len(rows) or any(len(r) != width for r in alt):
                raise ValueError(
                    f"state_rows[{state!r}] must match the base grid "
                    f"({width}x{len(rows)})"
                )
        self.rows = rows
        self.palettes = palettes
        self.state_rows = state_rows or {}

    @property
    def width(self) -> int:
        return len(self.rows[0])

    @property
    def height(self) -> int:
        """Height in PIXELS. Text rows are half this (rounded up)."""
        return len(self.rows)

    def render(self, state: str = "normal") -> Text:
        """Render to styled text, two pixel rows per text row."""
        palette = self.palettes.get(state, self.palettes["normal"])
        rows = self.state_rows.get(state, self.rows)
        out = Text()
        blank = TRANSPARENT * self.width
        for y in range(0, self.height, 2):
            top = rows[y]
            bottom = rows[y + 1] if y + 1 < self.height else blank
            for x in range(self.width):
                fg = palette.get(top[x])
                bg = palette.get(bottom[x])
                if fg is None and bg is None:
                    out.append(" ")
                elif bg is None:
                    out.append("▀", style=fg)
                elif fg is None:
                    # Lower half only: draw the bottom block rather than
                    # setting a background, so the empty top stays transparent
                    # against whatever the panel is using.
                    out.append("▄", style=bg)
                else:
                    out.append("▀", style=f"{fg} on {bg}")
            if y + 2 < self.height:
                out.append("\n")
        return out
