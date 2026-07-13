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

# --- Per-enemy avatars (ROB-79). Keyed by EnemyKind name (avatar_key). Enemies
# without a specific avatar fall back to their category silhouette. Size/color
# variants that look alike share one frame (mapped in _NORMAL/_CRITICAL below).

# Cultist — hooded figure with glowing eyes.
CULTIST_NORMAL = r"""
   /###\
  ( 0 0 )
  /|~~~|\
   |___|
""".strip("\n")
CULTIST_CRITICAL = r"""
   /###\
  ( x x )
  /|~~~|\
   |___|
""".strip("\n")

# Louse — a curled bug with legs.
LOUSE_NORMAL = r"""
   ,---.
  ( o o )
  <(===)>
   ^^ ^^
""".strip("\n")
LOUSE_CRITICAL = r"""
   ,---.
  ( x x )
  <(~~~)>
   ^^ ^^
""".strip("\n")

# Acid slime — a drippy blob.
ACID_SLIME_NORMAL = r"""
   _____
  / o o \
 ( \___/ )
  \~~~~~/
   ' ' '
""".strip("\n")
ACID_SLIME_CRITICAL = r"""
   _____
  / x x \
 ( \___/ )
  \.....//
""".strip("\n")

# Spike slime — a blob with spikes.
SPIKE_SLIME_NORMAL = r"""
  /\ /\ /\
 < o   o >
  \ \_/ /
   \___/
""".strip("\n")
SPIKE_SLIME_CRITICAL = r"""
  /\ /\ /\
 < x   x >
  \ \_/ /
   \...//
""".strip("\n")

# Fungi Beast — a spore-topped fungus.
FUNGI_NORMAL = r"""
   .-^-.
  ( ooo )
   \| |/
   /| |\
""".strip("\n")
FUNGI_CRITICAL = r"""
   .-^-.
  ( xxx )
   \| |/
   /| |\
""".strip("\n")

# Slaver — a whip-wielding humanoid.
SLAVER_NORMAL = r"""
   ,==.
  ( -- )~~
  /|  |\
  _|  |_
""".strip("\n")
SLAVER_CRITICAL = r"""
   ,==.
  ( xx )
  /|  |\
  _|  |_
""".strip("\n")

# Thief (Looter / Mugger) — a masked bandit.
THIEF_NORMAL = r"""
   ,--.
  (=..=)
  /|  |\
  '|  |'
""".strip("\n")
THIEF_CRITICAL = r"""
   ,--.
  (=xx=)
  /|  |\
  '|  |'
""".strip("\n")

# Gremlin Wizard — a pointy-hatted caster.
WIZARD_NORMAL = r"""
    /\
   /--\
  ( >< )*
  /|  |\
""".strip("\n")
WIZARD_CRITICAL = r"""
    /\
   /--\
  ( xx )
  /|  |\
""".strip("\n")

# Gremlin Nob — a hulking brute.
NOB_NORMAL = r"""
  __/\__
 ( >  < )
 /| || |\
  |_||_|
""".strip("\n")
NOB_CRITICAL = r"""
  __/\__
 ( x  x )
 /| || |\
  |_||_|
""".strip("\n")

# Lagavulin — a shelled, sleeping beast.
LAGAVULIN_NORMAL = r"""
  .------.
 ( zzzzzz )
 | (____)|
  \______/
""".strip("\n")
LAGAVULIN_CRITICAL = r"""
  .------.
 ( x  x  )
 | (____)|
  \______/
""".strip("\n")

# Sentry — a floating orb-turret.
SENTRY_NORMAL = r"""
   .====.
  { (##) }
   '===='
    |  |
""".strip("\n")
SENTRY_CRITICAL = r"""
   .====.
  { (xx) }
   '===='
    |  |
""".strip("\n")

# Shared knockout frame.
KO = r"""

   _____
   X___X
  (dead)

""".strip("\n")

# EnemyKind name (avatar_key) -> avatar frame. Look-alike variants share one:
# all slimes by color, all louses, all slavers, both thieves.
_NORMAL = {
    "IRONCLAD": IRONCLAD_NORMAL,
    "JawWorm": JAW_WORM_NORMAL,
    "Cultist": CULTIST_NORMAL,
    "RedLouse": LOUSE_NORMAL,
    "GreenLouse": LOUSE_NORMAL,
    "AcidSlimeS": ACID_SLIME_NORMAL,
    "AcidSlimeM": ACID_SLIME_NORMAL,
    "AcidSlimeL": ACID_SLIME_NORMAL,
    "SpikeSlimeS": SPIKE_SLIME_NORMAL,
    "SpikeSlimeM": SPIKE_SLIME_NORMAL,
    "SpikeSlimeL": SPIKE_SLIME_NORMAL,
    "FungiBeast": FUNGI_NORMAL,
    "BlueSlaver": SLAVER_NORMAL,
    "RedSlaver": SLAVER_NORMAL,
    "Looter": THIEF_NORMAL,
    "Mugger": THIEF_NORMAL,
    "GremlinWizard": WIZARD_NORMAL,
    "GremlinNob": NOB_NORMAL,
    "Lagavulin": LAGAVULIN_NORMAL,
    "Sentry": SENTRY_NORMAL,
    # Fat/Mad/Sneaky/Shield gremlins use the gremlin category fallback.
}
_CRITICAL = {
    "IRONCLAD": IRONCLAD_CRITICAL,
    "JawWorm": JAW_WORM_CRITICAL,
    "Cultist": CULTIST_CRITICAL,
    "RedLouse": LOUSE_CRITICAL,
    "GreenLouse": LOUSE_CRITICAL,
    "AcidSlimeS": ACID_SLIME_CRITICAL,
    "AcidSlimeM": ACID_SLIME_CRITICAL,
    "AcidSlimeL": ACID_SLIME_CRITICAL,
    "SpikeSlimeS": SPIKE_SLIME_CRITICAL,
    "SpikeSlimeM": SPIKE_SLIME_CRITICAL,
    "SpikeSlimeL": SPIKE_SLIME_CRITICAL,
    "FungiBeast": FUNGI_CRITICAL,
    "BlueSlaver": SLAVER_CRITICAL,
    "RedSlaver": SLAVER_CRITICAL,
    "Looter": THIEF_CRITICAL,
    "Mugger": THIEF_CRITICAL,
    "GremlinWizard": WIZARD_CRITICAL,
    "GremlinNob": NOB_CRITICAL,
    "Lagavulin": LAGAVULIN_CRITICAL,
    "Sentry": SENTRY_CRITICAL,
}

# Category silhouettes — the graceful fallback (ROB-79). An enemy with no
# specific avatar renders its category's shape, so the TUI always shows
# *something* sensible and a new enemy can never crash/blank the render.
# Per-enemy avatars (ROB-79 commit 3) override these.
SLIME_NORMAL = r"""
   _____
  /=====\
 ( ~~~~~ )
  \_____/
""".strip("\n")
SLIME_CRITICAL = r"""
   _____
  /=====\
 ( x   x )
  \__.__/
""".strip("\n")
GREMLIN_NORMAL = r"""
   /^^^\
  ( >_< )
  /|   |\
   ^   ^
""".strip("\n")
GREMLIN_CRITICAL = r"""
   /^^^\
  ( x_x )
  /|   |\
   ^   ^
""".strip("\n")
HUMANOID_NORMAL = r"""
   ,---.
  ( o o )
   ) - (
  /|   |\
""".strip("\n")
HUMANOID_CRITICAL = r"""
   ,---.
  ( x x )
   ) ~ (
  /|   |\
""".strip("\n")
BEAST_NORMAL = r"""
   /\_/\
  ( o o )
  =( V )=
   )   (
""".strip("\n")
BEAST_CRITICAL = r"""
   /\_/\
  ( x x )
  =( ~ )=
   )   (
""".strip("\n")

# Category -> (normal, critical) fallback frames.
_CATEGORY = {
    "slime": (SLIME_NORMAL, SLIME_CRITICAL),
    "gremlin": (GREMLIN_NORMAL, GREMLIN_CRITICAL),
    "humanoid": (HUMANOID_NORMAL, HUMANOID_CRITICAL),
    "beast": (BEAST_NORMAL, BEAST_CRITICAL),
}


def avatar_key(kind) -> str:
    """Map an EnemyKind to an avatar key (ROB-79). A specific per-enemy avatar
    is used if one exists in _NORMAL/_CRITICAL; otherwise select_avatar falls
    back to the enemy's category silhouette. The key is the EnemyKind's name."""
    return str(kind).split(".")[-1]  # e.g. "EnemyKind.GremlinNob" -> "GremlinNob"


# EnemyKind name -> category, for the fallback when no specific avatar exists.
_KIND_CATEGORY = {
    "AcidSlimeS": "slime", "AcidSlimeM": "slime", "AcidSlimeL": "slime",
    "SpikeSlimeS": "slime", "SpikeSlimeM": "slime", "SpikeSlimeL": "slime",
    "FatGremlin": "gremlin", "MadGremlin": "gremlin", "SneakyGremlin": "gremlin",
    "GremlinWizard": "gremlin", "ShieldGremlin": "gremlin", "GremlinNob": "gremlin",
    "Cultist": "humanoid", "BlueSlaver": "humanoid", "RedSlaver": "humanoid",
    "Looter": "humanoid", "Mugger": "humanoid", "Lagavulin": "humanoid",
    "Sentry": "humanoid",
    "JawWorm": "beast", "RedLouse": "beast", "GreenLouse": "beast",
    "FungiBeast": "beast",
}

# HP fraction at or below which an entity switches to the wounded frame.
CRITICAL_THRESHOLD = 0.30


def select_avatar(name: str, hp: int, max_hp: int) -> str:
    """Pick the avatar frame for an entity given its HP.

    `name` is "IRONCLAD" or an EnemyKind name (from avatar_key). A specific
    avatar is used if one exists; otherwise the enemy's category silhouette is
    the fallback (ROB-79), so a new enemy never crashes or blanks the render.
    Returns the multi-line ASCII art string.
    """
    if hp <= 0:
        return KO
    critical = max_hp > 0 and hp / max_hp <= CRITICAL_THRESHOLD
    normal, crit = (_NORMAL, _CRITICAL)
    if name in normal:  # specific avatar exists
        return crit[name] if critical else normal[name]
    # Fallback to the category silhouette.
    category = _KIND_CATEGORY.get(name)
    if category is not None:
        cat_normal, cat_crit = _CATEGORY[category]
        return cat_crit if critical else cat_normal
    return KO
