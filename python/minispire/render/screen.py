"""rich-based screen rendering for the Mini-Spire TUI.

Reads the observation vector (per ROB-40 layout), action mask, and
state_piles to render the fight. Pure rendering — no input handling.
"""
from __future__ import annotations

import functools

import numpy as np
from rich.console import Console, Group
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from minispire import _core
from minispire.render import avatars, sprites
from minispire.render.intent import intent_text

# --- Observation layout (ROB-40 + ROB-59 multi-enemy) ----------------------
# Indices into the obs vector. Player block + status, then kMaxEnemies enemy
# blocks of ENEMY_STRIDE floats, then pile counts, then turn (last slot).
CHAR_HP = 0
CHAR_MAX_HP = 1
CHAR_BLOCK = 2
CHAR_ENERGY = 3
CHAR_ENERGY_PER_TURN = 4
# Layout sizes are read from the engine (single source of truth) so they track
# obs changes as statuses/cards are added — never hardcode the stride.
_NUM_STATUS = _core.CombatEnv.PLAYER_STATUS_SIZE
_NUM_ENEMY_STATUS = _core.CombatEnv.ENEMY_STATUS_SIZE
MAX_ENEMIES = _core.CombatEnv.MAX_ENEMIES

CHAR_STATUS = slice(5, 5 + _NUM_STATUS)  # per-status stacks (V/W/S/D/Frail/Ritual)

# Enemy blocks start after the player block; each is ENEMY_STRIDE floats:
#   +0 is_alive, +1 hp, +2 block, then status(_NUM_STATUS), then
#   intent(ENEMY_INTENT_SIZE), then a one-hot over the enemy kinds.
ENEMY_BASE = _core.CombatEnv.PLAYER_OBS_SIZE
ENEMY_STRIDE = _core.CombatEnv.ENEMY_OBS_STRIDE
ENEMY_OFF_IS_ALIVE = 0
ENEMY_OFF_HP = 1
ENEMY_OFF_BLOCK = 2
ENEMY_OFF_STATUS = slice(3, 3 + _NUM_ENEMY_STATUS)
_INTENT = 3 + _NUM_ENEMY_STATUS  # intent block start
ENEMY_OFF_INTENT_IS_ATTACKING = _INTENT + 0
ENEMY_OFF_INTENT_ATTACK_DAMAGE = _INTENT + 1
ENEMY_OFF_INTENT_IS_BLOCKING = _INTENT + 2
ENEMY_OFF_INTENT_IS_BUFFING = _INTENT + 3
ENEMY_OFF_INTENT_IS_DEBUFFING = _INTENT + 4
ENEMY_OFF_INTENT_IS_ESCAPING = _INTENT + 5
ENEMY_OFF_INTENT_IS_SPLITTING = _INTENT + 6

# Turn number is the last obs slot.
# NOT OBS_SIZE - 1. The choice block sits after the turn float, so that
# computed index read a choice-channel value — zero unless a choice happened to
# be pending — and the turn counter never moved. Read the engine's constant.
TURN_NUMBER = _core.CombatEnv.TURN_OBS_INDEX


def enemy_base(slot: int) -> int:
    """Obs index where enemy `slot`'s block begins."""
    return ENEMY_BASE + slot * ENEMY_STRIDE


def living_enemy_slots(obs) -> list[int]:
    """Slot indices of living enemies (is_alive flag set), in slot order."""
    return [
        s
        for s in range(MAX_ENEMIES)
        if obs[enemy_base(s) + ENEMY_OFF_IS_ALIVE] > 0.5
    ]

# Status names in obs order, derived from the engine enums (ROB-78/79): the obs
# status block is [debuffs then powers], and each enum is declared in obs order
# (kObsDebuffOrder / kObsPowerOrder), so the enum member order IS the obs order.
# Excludes the None sentinels. Deriving this means a new status can't desync the
# labels from the obs.
_DEBUFF_NAMES = [d for d in _core.Debuff.__members__ if d != "None"]
_POWER_NAMES = [p for p in _core.Power.__members__ if p != "None"]
# The player block carries every power; the enemy block only the enemy-relevant
# prefix (Stage 4a), so the two blocks need different label lists.
STATUS_NAMES = _DEBUFF_NAMES + _POWER_NAMES
ENEMY_STATUS_NAMES = (
    _DEBUFF_NAMES + _POWER_NAMES[: _core.CombatEnv.NUM_ENEMY_POWERS]
)
STATUS_COLORS = {
    "Vulnerable": "orange3",
    "Weak": "purple",
    "Frail": "cyan",
    "Entangle": "magenta",
    "Strength": "red",
    "Dexterity": "green",
    "Ritual": "yellow",
    "Metallicize": "bright_black",
    "Enrage": "bright_red",
    "Artifact": "bright_yellow",
    # Player powers (Stage 4a). Unlisted names fall back to white.
    "DemonForm": "bright_red",
    "Combust": "orange3",
    "FeelNoPain": "cyan",
    "DarkEmbrace": "purple",
    "Evolve": "bright_green",
    "FireBreathing": "orange3",
    "Rupture": "red",
    "Juggernaut": "bright_magenta",
    "Rage": "bright_red",
    "FlameBarrier": "orange1",
    "Brutality": "red",
    "Berserk": "bright_yellow",
}

HP_BAR_WIDTH = 16


def cost_str(cost: int) -> str:
    """Display a card cost: 'X' for X-cost cards (kXCost sentinel, ROB-80),
    else the number."""
    return "X" if cost < 0 else str(cost)


def format_hp_bar(hp: int, max_hp: int, width: int = HP_BAR_WIDTH) -> Text:
    """Return a colored HP bar: filled '#' segments + empty '-' segments.

    Color reflects HP fraction: green > 60%, yellow 30-60%, red < 30%.
    """
    hp = max(hp, 0)
    if max_hp <= 0:
        filled = 0
    else:
        # ceil so any positive HP shows at least one filled segment.
        filled = (hp * width + max_hp - 1) // max_hp
    filled = max(0, min(filled, width))

    frac = (hp / max_hp) if max_hp > 0 else 0.0
    if frac > 0.6:
        color = "green"
    elif frac >= 0.3:
        color = "yellow"
    else:
        color = "red"

    bar = Text()
    bar.append("[", style="white")
    bar.append("#" * filled, style=color)
    bar.append("-" * (width - filled), style="dim")
    bar.append("]", style="white")
    return bar


def _status_line(
    obs: np.ndarray, status_slice: slice, names: list[str] | None = None
) -> Text:
    """Build a colored status-effects line. Empty Text if no statuses.

    `names` labels the block: the player block carries every power, an enemy
    block only the enemy-relevant prefix (Stage 4a).
    """
    stacks = obs[status_slice]
    out = Text()
    first = True
    for name, value in zip(names or STATUS_NAMES, stacks):
        n = int(value)
        if n <= 0:
            continue
        if not first:
            out.append("  ")
        first = False
        out.append(f"{name}({n})", style=STATUS_COLORS.get(name, "white"))
    return out


def _energy_pips(energy: int, per_turn: int) -> Text:
    out = Text()
    for i in range(max(energy, 0)):
        if i > 0:
            out.append(" ")
        out.append("●", style="yellow")  # ●
    out.append(f"  {energy}/{per_turn}", style="white")
    return out


@functools.lru_cache(maxsize=1)
def _has_color() -> bool:
    """Whether the terminal can show colour at all.

    Deliberately NOT a truecolor check. Rich quantizes RGB down to 256 or 16
    colours by itself, and a quantized sprite still reads as a creature, so
    demanding truecolor would drop good terminals to ASCII for no reason. Only
    a genuinely colourless output — a pipe, a dumb terminal — needs the
    fallback, and there the sprite would be a field of identical blocks.

    `--ascii-only` remains the escape hatch for terminals that render colour
    fine but misalign unicode.
    """
    return Console().color_system is not None


def _avatar(name: str, hp: int, max_hp: int, ascii_only: bool) -> Text:
    """The creature's picture: a 12x12 sprite, or ASCII where colour is not
    available. `hp` picks the expression, not the palette (ROB-83)."""
    if not ascii_only and _has_color():
        sprite = sprites.SPRITES.get(name)
        if sprite is not None:
            hurt = max_hp > 0 and hp / max_hp <= avatars.CRITICAL_THRESHOLD
            return sprite.render("hurt" if hurt else "normal")
    return Text(avatars.select_avatar(name, hp, max_hp), style="bold")


def _entity_block(
    title: str,
    avatar_name: str,
    hp: int,
    max_hp: int,
    block: int,
    obs: np.ndarray,
    status_slice: slice,
    extra_lines: list[Text] | None = None,
    status_names: list[str] | None = None,
    ascii_only: bool = False,
) -> Group:
    """Render one entity: avatar, name, HP bar, block, plus extra lines."""
    lines: list = []
    lines.append(_avatar(avatar_name, hp, max_hp, ascii_only))
    lines.append(Text(title, style="bold white"))

    hp_line = Text("HP  ")
    hp_line.append_text(format_hp_bar(hp, max_hp))
    hp_line.append(f"  {max(hp, 0)}/{max_hp}", style="white")
    lines.append(hp_line)

    if block > 0:
        blk = Text("BLK ", style="cyan")
        blk.append("#" * min(block, 10), style="cyan")
        blk.append(f"  {block}", style="white")
        lines.append(blk)

    status = _status_line(obs, status_slice, status_names)
    if status.plain:
        lines.append(status)

    if extra_lines:
        lines.extend(extra_lines)

    return Group(*lines)


def _enemy_status_slice(slot: int) -> slice:
    """Obs slice for enemy `slot`'s status block (V/W/S/D)."""
    base = enemy_base(slot)
    return slice(base + ENEMY_OFF_STATUS.start, base + ENEMY_OFF_STATUS.stop)


def build_fight(
    obs: np.ndarray,
    env,
    *,
    ascii_only: bool = False,
) -> Panel:
    """Build the main fight panel from the obs vector, without drawing it.

    `env` is the live env (for enemy max HP, which the obs intentionally omits).

    Split from render_fight (ROB-83) so the same renderable can be printed to a
    Console by the print-and-prompt loop OR returned from a Textual widget's
    render(). Textual draws Rich renderables natively, so the migration reuses
    this code rather than reimplementing it — but only once "build it" stopped
    being fused to "print it".
    """
    turn = int(obs[TURN_NUMBER])
    max_hps = env.enemy_max_hps()
    kinds = env.enemy_kinds()  # per-slot EnemyKind (ROB-79)

    # Character column.
    char = _entity_block(
        "IRONCLAD",
        "IRONCLAD",
        int(obs[CHAR_HP]),
        int(obs[CHAR_MAX_HP]),
        int(obs[CHAR_BLOCK]),
        obs,
        CHAR_STATUS,
        extra_lines=[
            Text("NRG ").append_text(
                _energy_pips(int(obs[CHAR_ENERGY]), int(obs[CHAR_ENERGY_PER_TURN]))
            )
        ],
        ascii_only=ascii_only,
    )

    # One column per living enemy, labeled with its real per-slot name (ROB-79).
    enemy_columns: list[Group] = []
    for slot in living_enemy_slots(obs):
        base = enemy_base(slot)
        kind = kinds[slot]
        intent = Text("Intent: ")
        intent.append_text(
            intent_text(
                is_attacking=bool(obs[base + ENEMY_OFF_INTENT_IS_ATTACKING]),
                attack_damage=int(obs[base + ENEMY_OFF_INTENT_ATTACK_DAMAGE]),
                is_blocking=bool(obs[base + ENEMY_OFF_INTENT_IS_BLOCKING]),
                is_buffing=bool(obs[base + ENEMY_OFF_INTENT_IS_BUFFING]),
                is_debuffing=bool(obs[base + ENEMY_OFF_INTENT_IS_DEBUFFING]),
                is_escaping=bool(obs[base + ENEMY_OFF_INTENT_IS_ESCAPING]),
                is_splitting=bool(obs[base + ENEMY_OFF_INTENT_IS_SPLITTING]),
                ascii_only=ascii_only,
            )
        )
        enemy_columns.append(
            _entity_block(
                f"[{slot}] {_core.enemy_name(kind).upper()}",
                avatars.avatar_key(kind),
                int(obs[base + ENEMY_OFF_HP]),
                int(max_hps[slot]) if slot < len(max_hps) else int(obs[base + ENEMY_OFF_HP]),
                int(obs[base + ENEMY_OFF_BLOCK]),
                obs,
                _enemy_status_slice(slot),
                extra_lines=[intent],
                status_names=ENEMY_STATUS_NAMES,
                ascii_only=ascii_only,
            )
        )

    grid = Table.grid(expand=True, padding=(0, 4))
    grid.add_column(ratio=1)  # character
    for _ in enemy_columns:
        grid.add_column(ratio=1)
    grid.add_row(char, *enemy_columns)

    return Panel(
        grid, title=f"MINI-SPIRE  ·  Turn {turn}", border_style="bright_blue"
    )


def render_fight(
    console: Console,
    obs: np.ndarray,
    env,
    *,
    ascii_only: bool = False,
) -> None:
    """Clear the screen and draw the fight panel. Used by the print-and-prompt
    loop; the Textual widgets call build_fight() and return the panel instead.
    """
    console.clear()
    console.print(build_fight(obs, env, ascii_only=ascii_only))


def card_playable(mask, card_id) -> bool:
    """True if any (card_id, target) action is legal in the mask.

    The action space is (card x target) cross-product (ROB-60); a card is
    playable iff at least one of its target slots is unmasked.
    """
    base = int(card_id) * MAX_ENEMIES
    return any(bool(mask[base + t]) for t in range(MAX_ENEMIES))


#: Marker on the focused entry. A style alone is not enough — reverse video is
#: invisible in some terminal themes, and a colour cannot be seen at all by a
#: player who cannot distinguish it. The caret carries the meaning; the style
#: reinforces it.
FOCUS_MARK = "▸"
FOCUS_STYLE = "bold black on bright_white"

#: Cards per row in the hand and choice grids. Published because the app moves
#: focus by whole rows on up/down, and a width it guessed independently would
#: send the highlight somewhere the player is not looking.
HAND_COLUMNS = 3

#: Width of a rendered card box. The longest card name is "Perfected Strike+"
#: at 17 characters, so 24 leaves room for a border and a cost without the
#: title wrapping. Three across needs ~78 columns.
CARD_BOX_WIDTH = 24
CARD_COLUMNS = 3

#: Fixed box height, so boxes in one row line up. Without it Rich sizes each to
#: its own text and a row of three comes out ragged — Strike three rows tall
#: beside Bash at four, which reads as broken rather than as a grid.
#:
#: 7 = 2 border + 5 text lines, and 5 is the widest description in the pool
#: (Clash, wrapped at 20 columns) rather than a guess. A new card longer than
#: that would be clipped, which CardBoxesFitTheirDescriptions catches.
CARD_BOX_HEIGHT = 7


def build_card_box(card_id, cost: int, index: int | None = None,
                   focused: bool = False, dim: bool = False) -> Panel:
    """One card drawn as a card: cost and name on top, rules text below.

    For the screens where the player has STOPPED to read — a pending choice, a
    pile they opened. The in-combat hand deliberately stays a compact list with
    detail on the focused card only: a card box is ~7 rows, so a ten-card hand
    at three across is over 30 rows and does not fit an 80x24 terminal. Real
    StS makes the same split, compact in hand and full when you inspect.
    """
    title = Text()
    if index is not None:
        title.append(f"({index}) ", style="dim" if dim else "bold white")
    title.append(_core.card_name(card_id), style="dim" if dim else "bold")
    title.append(f"  {{{cost_str(cost)}}}", style="dim" if dim else "yellow")

    body = Text(
        _core.card_description(card_id), style="dim" if dim else "italic"
    )
    return Panel(
        body,
        title=title,
        width=CARD_BOX_WIDTH,
        height=CARD_BOX_HEIGHT,
        border_style=FOCUS_STYLE if focused else ("grey35" if dim else "grey50"),
        padding=(0, 1),
    )


def _card_grid(boxes: list[Panel], columns: int = CARD_COLUMNS) -> Table:
    """Lay card boxes out in a grid, padding the last row so it aligns."""
    table = Table.grid(padding=(0, 1))
    for _ in range(columns):
        table.add_column()
    row: list = []
    for box in boxes:
        row.append(box)
        if len(row) == columns:
            table.add_row(*row)
            row = []
    if row:
        while len(row) < columns:
            row.append(Text(""))
        table.add_row(*row)
    return table


def build_hand(env, *, focus: int | None = None) -> tuple[Panel, list]:
    """Build the hand panel and the local-index -> CardId map together.

    Returned as a pair on purpose (ROB-83). The map is what a keypress indexes
    into, and the panel is what the player read before pressing — deriving them
    in separate passes is how a selection ends up meaning a different card than
    the one on screen. One pass, one source, no chance of disagreement.

    Each playable card slot gets a local index; targeting is resolved by the
    caller, since a card's global action depends on the chosen target (ROB-60).
    The end-turn local index is len(action_map), and End Turn is drawn as a real
    entry so the highlight can land somewhere visible when it is selected.

    The hand renders as card boxes, same as the choice and pile screens. It was
    a compact list first, on the reasoning that boxes cost too much vertical
    space to play from — but reading a card's text should not require arrowing
    onto it, and one consistent way of drawing a card beats two.

    Unplayable cards still render, dimmed and unnumbered, because knowing what
    you are holding and cannot afford is part of planning the turn.
    """
    mask = env.action_masks()
    hand = env.state_piles().hand

    action_map: list = []  # local index -> CardId
    boxes: list = []
    for card_id in hand:
        playable = card_playable(mask, card_id)
        index = None
        if playable:
            index = len(action_map)
            action_map.append(card_id)
        # effective_cost, not CardData.cost: Infernal Blade makes a card free
        # for the turn and Blood for Blood drops a point per HP loss. Showing
        # the base cost told the player a number the engine would not charge.
        boxes.append(
            build_card_box(
                card_id,
                env.effective_cost(card_id),
                index=index,
                focused=(index is not None and index == focus),
                dim=not playable,
            )
        )

    body: list = [Text("HAND:", style="bold")]
    body.append(_card_grid(boxes) if hand else Text("   (empty)", style="dim"))

    # End Turn is an option like any other, so it gets an index and can hold the
    # highlight. Previously it existed only in the footer text, which meant
    # arrowing onto it looked like the highlight had vanished.
    end_index = len(action_map)
    end = Text()
    end.append(f"{FOCUS_MARK} " if focus == end_index else "  ")
    end.append(f"({end_index}) End Turn", style="bold white")
    if focus == end_index:
        end.stylize(FOCUS_STYLE)
    body.append(Text(""))
    body.append(end)

    return Panel(Group(*body), border_style="grey50"), action_map


# Human-readable prompt per ChoiceKind. Keyed by enum name so a new kind shows
# up as a missing-key error rather than silently rendering the wrong prompt.
CHOICE_PROMPTS = {
    "UpgradeCardInHand": "Upgrade a card in your hand",
    "HandToTopOfDraw": "Put a card from your hand on top of your draw pile",
    "DiscardToTopOfDraw": "Put a card from your discard pile on top of your draw pile",
    "ExhaustToHand": "Put a card from your exhaust pile into your hand",
    "CopyAttackOrPowerInHand": "Choose an Attack or Power to copy",
    "ExhaustCardInHand": "Exhaust a card from your hand",
}


def build_choice(env, *, focus: int | None = None) -> tuple[Panel, int]:
    """Build the pending-choice panel and its option count.

    Local index i maps to the global action FIRST_OPTION_SLOT + i, so the caller
    needs no mapping table of its own. The count is returned alongside the panel
    for the same reason build_hand returns its map: it is what a keypress is
    bounds-checked against, and it must describe the panel actually shown.
    """
    view = env.choice_view()
    prompt = CHOICE_PROMPTS.get(
        view.kind.name, f"Choose a card ({view.kind.name})"
    )

    header = Text()
    header.append(f"{_core.card_name(view.source_card)}: ", style="bold yellow")
    header.append(prompt, style="bold white")
    if view.copies > 1:
        header.append(f"  (x{view.copies} copies)", style="yellow")

    # Full card boxes, not a name list. This is the screen where the player has
    # stopped to decide, and choosing which card to Exhume or upgrade is not
    # answerable from a name once the pool is 154 cards deep.
    table = _card_grid([
        build_card_box(card_id, env.effective_cost(card_id), index=i,
                       focused=(i == focus))
        for i, card_id in enumerate(view.options)
    ])

    body: list = [header, Text(""), table]
    if view.is_optional:
        body.append(Text(f"\n(s) skip", style="dim"))

    return Panel(Group(*body), border_style="yellow", title="CHOOSE"), len(view.options)


#: Above this many cards a pile renders as a name list rather than card boxes.
#: A 40-card draw pile as boxes is ~14 rows of grid — past the point where
#: "inspect one card" turns into "scroll past everything".
PILE_BOX_LIMIT = 12


def build_piles(env, *, focus: int | None = None) -> tuple[Panel, int]:
    """Build the pile-view panel (toggled with 'p') and its focusable count.

    Small piles render as card boxes, because opening the exhaust pile is
    usually a question about ONE card's text. Large piles stay a name list:
    boxes stop helping the moment they no longer fit on screen together.

    Focus moves a highlight across every boxed card, in render order —
    draw, then discard, then exhaust — so the pile view is browsable rather
    than a static dump. Nothing here is selectable; keys.py refuses to turn a
    keypress into an action while the piles are open.
    """
    piles = env.state_piles()
    boxed = 0  # running count, so focus indexes match render order

    def fmt_list(cards) -> Text | Table:
        nonlocal boxed
        if not cards:
            return Text("    (empty)", style="dim")
        if len(cards) <= PILE_BOX_LIMIT:
            grid = _card_grid([
                build_card_box(c, env.effective_cost(c),
                               focused=(focus == boxed + i))
                for i, c in enumerate(cards)
            ])
            boxed += len(cards)
            return grid
        out = Text()
        for c in cards:
            out.append(f"    {_core.card_name(c):<20}", style="white")
            out.append(f"{{{cost_str(env.effective_cost(c))}}}\n", style="yellow")
        return out

    def fmt_counts(count_map) -> Text | Table:
        nonlocal boxed
        if not count_map:
            return Text("    (empty)", style="dim")
        # Sort by CardId value for stable display (does not reveal draw order —
        # the draw pile is a count map precisely so order stays hidden).
        types = sorted(count_map, key=lambda x: int(x))
        if len(types) <= PILE_BOX_LIMIT:
            grid = _card_grid([
                build_card_box(c, env.effective_cost(c),
                               focused=(focus == boxed + i))
                for i, c in enumerate(types)
            ])
            boxed += len(types)
            return grid
        out = Text()
        for c in types:
            out.append(f"    {_core.card_name(c):<20}", style="white")
            out.append(f"x{count_map[c]}  ", style="bright_white")
            out.append(f"{{{cost_str(env.effective_cost(c))}}}\n", style="yellow")
        return out

    draw_total = sum(piles.draw.values())
    # Built in render order on purpose: `boxed` accumulates as each section is
    # laid out, so a focus index means the same card the player is looking at.
    draw_section = fmt_counts(piles.draw)
    discard_section = fmt_list(piles.discard)
    exhaust_section = fmt_list(piles.exhaust)
    body = Group(
        Text(f"DRAW ({draw_total}, shuffled — order hidden):", style="bold"),
        draw_section,
        Text(f"DISCARD ({len(piles.discard)}, top-most last):", style="bold"),
        discard_section,
        Text(f"EXHAUST ({len(piles.exhaust)}):", style="bold"),
        exhaust_section,
    )
    return Panel(body, title="PILES", border_style="grey50"), boxed


def build_end_screen(
    obs: np.ndarray, env, outcome, log_path: str | None
) -> Panel:
    """Build the end-of-fight screen.

    Split out (ROB-86) because the Textual app exited the instant the fight
    ended, and Textual tears the screen down on exit — so a player who won saw
    their terminal prompt, not a result. The app now renders this and waits for
    a keypress, which is what the print-and-prompt loop did all along.
    """
    won = outcome == _core.Outcome.Won
    banner = Text(
        "*** YOU WIN ***" if won else "*** YOU LOSE ***",
        style="bold green" if won else "bold red",
        justify="center",
    )

    char_hp = max(int(obs[CHAR_HP]), 0)
    max_hps = env.enemy_max_hps()
    kinds = env.enemy_kinds()
    summary = Text()
    summary.append(f"\nIRONCLAD  HP {char_hp}/{int(obs[CHAR_MAX_HP])}\n", style="white")
    living = living_enemy_slots(obs)
    if living:
        for slot in living:
            base = enemy_base(slot)
            hp = max(int(obs[base + ENEMY_OFF_HP]), 0)
            mx = int(max_hps[slot]) if slot < len(max_hps) else hp
            name = _core.enemy_name(kinds[slot]).upper()
            summary.append(f"{name}  HP {hp}/{mx}\n", style="white")
    else:
        summary.append("All enemies defeated.\n", style="white")
    summary.append(f"\nCombat lasted {int(obs[TURN_NUMBER])} turns.\n", style="white")
    if log_path:
        summary.append(f"Trajectory saved to {log_path}\n", style="dim")

    return Panel(
        Group(banner, summary), title="MINI-SPIRE", border_style="bright_blue"
    )


def render_end_screen(
    console: Console, obs: np.ndarray, env, outcome, log_path: str | None
) -> None:
    """Draw the end screen. Used by the policy viewer, which is not interactive."""
    console.clear()
    console.print(build_end_screen(obs, env, outcome, log_path))
