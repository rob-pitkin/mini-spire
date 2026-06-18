"""Enemy intent icons for the TUI.

Mirrors how STS floats an intent icon above the enemy. The displayed attack
damage is the engine's already-modified value (Strength/Weak/Vulnerable
applied), pulled from obs slot 17.

Unicode icons by default; `--ascii-only` falls back to plain ASCII for
terminals that misalign emoji.
"""
from __future__ import annotations

from rich.text import Text

_ATTACK_COLOR = "red"
_BLOCK_COLOR = "cyan"
_BUFF_COLOR = "magenta"


def intent_text(
    *,
    is_attacking: bool,
    attack_damage: int,
    is_blocking: bool,
    is_buffing: bool,
    ascii_only: bool = False,
) -> Text:
    """Build a colored rich.Text describing the enemy's intent.

    A compound move (e.g. attack + block) concatenates icons with spacing.
    Returns "(none)" if the enemy has no intent components.
    """
    parts: list[Text] = []

    if is_attacking:
        if ascii_only:
            parts.append(Text(f">>{attack_damage}<<", style=_ATTACK_COLOR))
        else:
            parts.append(Text(f"⚔ {attack_damage}", style=_ATTACK_COLOR))

    if is_blocking:
        if ascii_only:
            parts.append(Text("[BLK]", style=_BLOCK_COLOR))
        else:
            parts.append(Text("\U0001f6e1", style=_BLOCK_COLOR))

    if is_buffing:
        if ascii_only:
            parts.append(Text("(+)", style=_BUFF_COLOR))
        else:
            parts.append(Text("✦", style=_BUFF_COLOR))

    if not parts:
        return Text("(none)", style="dim")

    out = Text()
    for i, part in enumerate(parts):
        if i > 0:
            out.append("  ")
        out.append_text(part)
    return out
