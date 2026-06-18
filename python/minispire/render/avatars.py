"""ASCII art avatars for the TUI.

Each avatar is ~5 lines x ~12 cols. Frames are state-reactive: the renderer
selects NORMAL vs CRITICAL based on HP %, and KO when HP hits 0. Keeping
avatars short preserves vertical screen budget on 24-line terminals.
"""
from __future__ import annotations

# Ironclad — a cloaked warrior with a raised blade.
IRONCLAD_NORMAL = r"""
   ,---.
  ( o o )   /
   ) ^ (---+
  /|   |\
  /_| |_\
""".strip("\n")

# Wounded: head bowed, blade lowered.
IRONCLAD_CRITICAL = r"""
   ,---.
  ( x x )
   ) v ( \
  /|   |\ +
  /_| |_\
""".strip("\n")

# Jaw Worm — a wormy maw with prominent jaws.
JAW_WORM_NORMAL = r"""
   _____
  / o o \
 | \___/ |
  \vvvvv/
   |||||
""".strip("\n")

# Wounded: drooping, mouth slack.
JAW_WORM_CRITICAL = r"""
   _____
  / x x \
 | \~~~/ |
  \mmmmm/
   |||||
""".strip("\n")

# Shared knockout frame.
KO = r"""

   _____
   X___X
  (dead)

""".strip("\n")

_NORMAL = {
    "IRONCLAD": IRONCLAD_NORMAL,
    "JAW_WORM": JAW_WORM_NORMAL,
}
_CRITICAL = {
    "IRONCLAD": IRONCLAD_CRITICAL,
    "JAW_WORM": JAW_WORM_CRITICAL,
}

# HP fraction at or below which an entity switches to the wounded frame.
CRITICAL_THRESHOLD = 0.30


def select_avatar(name: str, hp: int, max_hp: int) -> str:
    """Pick the avatar frame for an entity given its HP.

    Args:
        name: "IRONCLAD" or "JAW_WORM".
        hp: current HP.
        max_hp: max HP (used for the critical-threshold ratio).

    Returns:
        The multi-line ASCII art string.
    """
    if hp <= 0:
        return KO
    if max_hp > 0 and hp / max_hp <= CRITICAL_THRESHOLD:
        return _CRITICAL.get(name, KO)
    return _NORMAL.get(name, KO)
