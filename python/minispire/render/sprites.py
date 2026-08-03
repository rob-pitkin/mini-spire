"""Enemy sprite data — 12x12 indexed-colour pixel art (ROB-83).

12 wide because avatar width is a budget, not a taste: the fight panel gives
every living enemy its own column plus one for the Ironclad, so five slimes
means six columns. At 12 that is ~96 columns and safe anywhere; at 24 it is
~168 and clips on a laptop.

Palette keys, used consistently so a sprite can be read as a drawing:

    ' '  transparent      #  body (main)
    .    body (shadow)    o  highlight / eye white
    @    dark / pupil     +  accent (horn, spike, metal, cloth)

Every sprite is ours. Slay the Spire's art is copyrighted and this repo is a
clean open artifact, so nothing here is traced or downsampled from the game.

States are palette swaps over one grid, never a second drawing — see
pixelart.Sprite. "hurt" is used below the critical-HP threshold.
"""
from __future__ import annotations

from minispire.render.pixelart import Sprite


def _palettes(body: str, shadow: str, accent: str = None) -> dict:
    """One palette. Colour is IDENTITY here, not state.

    An earlier version derived a reddened "hurt" palette and it was a mistake:
    tinting every body red flattened twelve distinct colour schemes into the
    same wounded mush, so a hurt Cultist read as a hurt Jaw Worm rather than as
    a Cultist. Injury is shown by expression instead — see `_hurt_face`.
    """
    return {
        "normal": {
            "#": body,
            ".": shadow,
            "o": "rgb(240,240,230)",  # eye white
            "!": HURT_EYE,            # eye white, wounded
            "@": "rgb(18,16,22)",     # pupil / dark
            "+": accent or shadow,
            " ": None,
        }
    }


#: Colour of a wounded eye. One constant, so the whole roster's injury read can
#: be retuned in a single place instead of across twelve palettes.
HURT_EYE = "rgb(226,74,64)"


def _hurt_face(rows: list[str]) -> list[str]:
    """Derive a wounded expression: the eye white (`o`) becomes `!`.

    Derived rather than hand-drawn so twelve enemies do not need twenty-four
    grids kept in agreement — the failure there is an enemy whose hurt sprite
    silently stops matching its normal one after an edit.

    Only eye pixels change, so the silhouette is byte-identical and a creature
    cannot appear to change shape the instant it crosses the threshold. The eye
    is also the one place a 12x12 sprite has enough salience to carry state:
    two pixels at the focal point read, where two pixels on a flank do not.
    """
    return [r.replace("o", "!") for r in rows]


# --- Slimes: one silhouette per size, so the three read apart at a glance ---

_ACID_L = [
    "   ......   ",
    " ..########.",
    ".############",
    ".##@@##@@###.",
    ".#@o@##@o@##.",
    ".##@@##@@###.",
    ".###########.",
    ".##.......##.",
    ".#.........#.",
    ".###########.",
    " ..#######.. ",
    "   ......   ",
]
# Trimmed to 12 columns exactly (the row above is a draft width guard).
_ACID_L = [r[:12].ljust(12) for r in _ACID_L]

_ACID_M = [
    "            ",
    "    ....    ",
    "  ..######..",
    " .##########",
    " .#@@##@@##.",
    " .@o@##@o@#.",
    " .##########",
    " .##......##",
    " .#........#",
    "  .########.",
    "   ..####.. ",
    "            ",
]
_ACID_M = [r[:12].ljust(12) for r in _ACID_M]

_ACID_S = [
    "            ",
    "            ",
    "     ...    ",
    "   ..####.. ",
    "  .########.",
    "  .#@##@###.",
    "  .########.",
    "  .#......#.",
    "   .######. ",
    "    ..##..  ",
    "            ",
    "            ",
]
_ACID_S = [r[:12].ljust(12) for r in _ACID_S]

# --- Louse: one grid, two palettes. The whole argument for indexed colour --
_LOUSE = [
    "            ",
    "    ....    ",
    "  ..####..  ",
    " .########. ",
    ".#@o##@o##+.",
    ".##########.",
    ".#.######.#.",
    "+#........#+",
    " .########. ",
    "  ..####..  ",
    " +  .  .  + ",
    "            ",
]

_JAW_WORM = [
    "     ..     ",
    "   ......   ",
    "  ..####..  ",
    " .########. ",
    ".##@o##o@##.",
    ".##########.",
    ".#+#####+#+.",
    ".##########.",
    " .#+#+#+#+#.",
    " .########. ",
    "  ..####..  ",
    "   ..  ..   ",
]

_CULTIST = [
    "    ++++    ",
    "   ++++++   ",
    "   .####.   ",
    "  .#@oo@#.  ",
    "  .######.  ",
    "   .####.   ",
    "  ++####++  ",
    " ++.####.++ ",
    "   .####.   ",
    "   .####.   ",
    "  ..####..  ",
    "  ..    ..  ",
]

_SENTRY = [
    "    ++++    ",
    "   +.##.+   ",
    "  +.####.+  ",
    "  .#@oo@#.  ",
    "  .######.  ",
    "  +.####.+  ",
    "   +####+   ",
    "    +##+    ",
    "     ++     ",
    "     ++     ",
    "    ++++    ",
    "   ++  ++   ",
]

_GREMLIN_NOB = [
    "  +      +  ",
    "  ++    ++  ",
    "   .####.   ",
    "  .######.  ",
    " .#@o##o@#. ",
    " .########. ",
    " .##.--.##. ",
    ".##########.",
    ".##########.",
    " .#.####.#. ",
    " .#..##..#. ",
    "  ..    ..  ",
]
_GREMLIN_NOB = [r.replace("-", "@") for r in _GREMLIN_NOB]


def _sprite(rows: list[str], body: str, shadow: str, accent: str = None) -> Sprite:
    """A sprite plus its automatically-derived wounded expression."""
    return Sprite(
        rows,
        _palettes(body, shadow, accent),
        state_rows={"hurt": _hurt_face(rows)},
    )


# --- Gremlins: five silhouettes, because they fight completely differently --

_FAT_GREMLIN = [
    "            ",
    "  ........  ",
    " .########. ",
    ".##@o##o@##.",
    ".##########.",
    ".#...##...#.",
    ".##########.",
    ".##########.",
    " .########. ",
    "  .# ## #.  ",
    "   .    .   ",
    "            ",
]

_MAD_GREMLIN = [
    " +  +  +  + ",
    "  + ++ +  + ",
    "   .####.   ",
    "  .######.  ",
    " .#@o##o@#. ",
    " .########. ",
    " .#.####.#. ",
    " .########. ",
    "  .######.  ",
    "  .#.##.#.  ",
    "  .# .. #.  ",
    "   .    .   ",
]

_SNEAKY_GREMLIN = [
    "            ",
    "    ....    ",
    "   .####.   ",
    "  .#@o#o@#. ",
    "  .#######. ",
    " .########+ ",
    " .########+ ",
    " .#######.+ ",
    " .######.   ",
    " .#.###.    ",
    "  . .#.     ",
    "     .      ",
]

_SHIELD_GREMLIN = [
    "         ++ ",
    "   ....++++ ",
    "  .####++++ ",
    " .#@o#+++++ ",
    " .####+++++ ",
    " .####+++++ ",
    " .####+++++ ",
    " .####++++  ",
    " .#####++   ",
    " .#.###.    ",
    "  . . .     ",
    "            ",
]

_GREMLIN_WIZARD = [
    "     ++     ",
    "    ++++    ",
    "   ++++++   ",
    "  ++++++++  ",
    "   .####.   ",
    "  .#@o#o@#. ",
    "  .#######. ",
    " +.#######. ",
    " +.#######. ",
    " + .#####.  ",
    " +  .# #.   ",
    "    .   .   ",
]

# --- Humanoids: Slavers share a grid, Looter and Mugger share another -------

_SLAVER = [
    "    ....    ",
    "   .####.   ",
    "  .#@o#o@#. ",
    "  .#######. ",
    " +.#######.+",
    " +.#######.+",
    " + .#####. +",
    "   .#####.  ",
    "   .##.##.  ",
    "   .#. .#.  ",
    "   .#. .#.  ",
    "   ..   ..  ",
]

_THIEF = [
    "    ....    ",
    "   .####.   ",
    "  .@@@@@@#. ",
    "  .#@o#o@#. ",
    "  .#######. ",
    "  .#######. ",
    " +.#######. ",
    " +.#######. ",
    "  .##.###.  ",
    "  .#.  .#.  ",
    "  .#.  .#.  ",
    "  ..    ..  ",
]

_FUNGI_BEAST = [
    "   ......   ",
    " ..########.",
    ".##########.",
    ".#.######.#.",
    ".##########.",
    " ..########.",
    "   .####.   ",
    "   .#@o#.   ",
    "   .####.   ",
    "   .####.   ",
    "  ..####..  ",
    "  ..    ..  ",
]

_LAGAVULIN = [
    "            ",
    " .........  ",
    ".##########.",
    ".#@@####@@#.",
    ".##########.",
    ".##+####+##.",
    ".##########.",
    ".#.######.#.",
    ".##########.",
    " .########. ",
    "  ..####..  ",
    "            ",
]

_IRONCLAD = [
    "    ++++    ",
    "   .####.   ",
    "  .#@o#o@#. ",
    "  .#######. ",
    " +.#######.+",
    "++.#######.+",
    "+ .#######. ",
    "  .#######. ",
    "  .##.#.##. ",
    "  .#. .  #. ",
    "  .#.   .#. ",
    "  ..     .. ",
]


#: avatar_key -> Sprite. Kinds absent here fall back to the ASCII avatars.
SPRITES: dict[str, Sprite] = {
    "AcidSlimeL": _sprite(_ACID_L, "rgb(96,176,80)", "rgb(52,104,44)"),
    "AcidSlimeM": _sprite(_ACID_M, "rgb(96,176,80)", "rgb(52,104,44)"),
    "AcidSlimeS": _sprite(_ACID_S, "rgb(96,176,80)", "rgb(52,104,44)"),
    "SpikeSlimeL": _sprite(_ACID_L, "rgb(150,140,190)", "rgb(84,76,116)"),
    "SpikeSlimeM": _sprite(_ACID_M, "rgb(150,140,190)", "rgb(84,76,116)"),
    "SpikeSlimeS": _sprite(_ACID_S, "rgb(150,140,190)", "rgb(84,76,116)"),
    "RedLouse": _sprite(_LOUSE, "rgb(190,72,60)", "rgb(112,40,34)", "rgb(60,30,26)"),
    "GreenLouse": _sprite(_LOUSE, "rgb(110,168,72)", "rgb(60,96,40)", "rgb(34,52,24)"),
    "JawWorm": _sprite(_JAW_WORM, "rgb(186,116,68)", "rgb(110,64,36)",
                       "rgb(232,200,150)"),
    "Cultist": _sprite(_CULTIST, "rgb(120,96,160)", "rgb(64,48,96)", "rgb(206,176,96)"),
    "Sentry": _sprite(_SENTRY, "rgb(140,146,160)", "rgb(72,78,92)", "rgb(206,176,96)"),
    "GremlinNob": _sprite(_GREMLIN_NOB, "rgb(176,84,72)", "rgb(100,44,38)",
                          "rgb(226,214,190)"),
    # Gremlins share a green family so they read as a species, and differ by
    # silhouette — which is what the player actually has to tell apart, since
    # they fight nothing alike.
    "FatGremlin": _sprite(_FAT_GREMLIN, "rgb(126,166,86)", "rgb(70,96,48)"),
    "MadGremlin": _sprite(_MAD_GREMLIN, "rgb(150,158,72)", "rgb(86,92,40)",
                          "rgb(198,86,64)"),
    "SneakyGremlin": _sprite(_SNEAKY_GREMLIN, "rgb(110,150,96)", "rgb(60,86,52)",
                             "rgb(206,206,214)"),
    "ShieldGremlin": _sprite(_SHIELD_GREMLIN, "rgb(118,152,104)", "rgb(64,88,56)",
                             "rgb(176,150,80)"),
    "GremlinWizard": _sprite(_GREMLIN_WIZARD, "rgb(124,148,88)", "rgb(68,84,48)",
                             "rgb(122,96,180)"),
    # Slavers: one grid, two palettes — the difference between them IS colour.
    "RedSlaver": _sprite(_SLAVER, "rgb(178,74,62)", "rgb(102,42,34)",
                         "rgb(60,52,48)"),
    "BlueSlaver": _sprite(_SLAVER, "rgb(74,112,178)", "rgb(40,62,104)",
                          "rgb(60,52,48)"),
    # Looter and Mugger are the same thief; the Mugger is the richer one.
    "Looter": _sprite(_THIEF, "rgb(150,140,120)", "rgb(84,78,66)",
                      "rgb(198,176,84)"),
    "Mugger": _sprite(_THIEF, "rgb(122,116,148)", "rgb(66,62,84)",
                      "rgb(216,190,96)"),
    "FungiBeast": _sprite(_FUNGI_BEAST, "rgb(178,116,140)", "rgb(102,62,80)",
                          "rgb(212,196,168)"),
    "Lagavulin": _sprite(_LAGAVULIN, "rgb(96,110,124)", "rgb(50,60,70)",
                         "rgb(180,150,72)"),
    # The player. Not an enemy kind, so it is keyed by the name _entity_block
    # already passes for the character column.
    "IRONCLAD": _sprite(_IRONCLAD, "rgb(178,68,58)", "rgb(100,38,32)",
                        "rgb(198,198,206)"),
}
