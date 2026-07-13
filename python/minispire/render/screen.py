"""rich-based screen rendering for the Mini-Spire TUI.

Reads the observation vector (per ROB-40 layout), action mask, and
state_piles to render the fight. Pure rendering — no input handling.
"""
from __future__ import annotations

import numpy as np
from rich.console import Console, Group
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from minispire import _core
from minispire.render import avatars
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
_NUM_STATUS = _core.CombatEnv.NUM_STATUS_EFFECTS
MAX_ENEMIES = _core.CombatEnv.MAX_ENEMIES

CHAR_STATUS = slice(5, 5 + _NUM_STATUS)  # per-status stacks (V/W/S/D/Frail/Ritual)

# Enemy blocks start after the player block; each is ENEMY_STRIDE floats:
#   +0 is_alive, +1 hp, +2 block, then status(_NUM_STATUS), then intent(4).
ENEMY_BASE = _core.CombatEnv.PLAYER_OBS_SIZE
ENEMY_STRIDE = _core.CombatEnv.ENEMY_OBS_STRIDE
ENEMY_OFF_IS_ALIVE = 0
ENEMY_OFF_HP = 1
ENEMY_OFF_BLOCK = 2
ENEMY_OFF_STATUS = slice(3, 3 + _NUM_STATUS)
_INTENT = 3 + _NUM_STATUS  # intent block start
ENEMY_OFF_INTENT_IS_ATTACKING = _INTENT + 0
ENEMY_OFF_INTENT_ATTACK_DAMAGE = _INTENT + 1
ENEMY_OFF_INTENT_IS_BLOCKING = _INTENT + 2
ENEMY_OFF_INTENT_IS_BUFFING = _INTENT + 3

# Turn number is the last obs slot.
TURN_NUMBER = _core.CombatEnv.OBS_SIZE - 1


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
STATUS_NAMES = [d for d in _core.Debuff.__members__ if d != "None"] + [
    p for p in _core.Power.__members__ if p != "None"
]
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
}

# CardId display names + colors for cost annotation.
HP_BAR_WIDTH = 16


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


def _status_line(obs: np.ndarray, status_slice: slice) -> Text:
    """Build a colored status-effects line. Empty Text if no statuses."""
    stacks = obs[status_slice]
    out = Text()
    first = True
    for name, value in zip(STATUS_NAMES, stacks):
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


def _entity_block(
    title: str,
    avatar_name: str,
    hp: int,
    max_hp: int,
    block: int,
    obs: np.ndarray,
    status_slice: slice,
    extra_lines: list[Text] | None = None,
) -> Group:
    """Render one entity: avatar, name, HP bar, block, plus extra lines."""
    lines: list = []
    lines.append(Text(avatars.select_avatar(avatar_name, hp, max_hp), style="bold"))
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

    status = _status_line(obs, status_slice)
    if status.plain:
        lines.append(status)

    if extra_lines:
        lines.extend(extra_lines)

    return Group(*lines)


def _enemy_status_slice(slot: int) -> slice:
    """Obs slice for enemy `slot`'s status block (V/W/S/D)."""
    base = enemy_base(slot)
    return slice(base + ENEMY_OFF_STATUS.start, base + ENEMY_OFF_STATUS.stop)


def render_fight(
    console: Console,
    obs: np.ndarray,
    env,
    *,
    ascii_only: bool = False,
) -> None:
    """Clear the screen and render the main fight panel from the obs vector.

    `env` is the live env (for enemy max HP, which the obs intentionally omits).
    """
    console.clear()

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
            )
        )

    grid = Table.grid(expand=True, padding=(0, 4))
    grid.add_column(ratio=1)  # character
    for _ in enemy_columns:
        grid.add_column(ratio=1)
    grid.add_row(char, *enemy_columns)

    console.print(
        Panel(grid, title=f"MINI-SPIRE  ·  Turn {turn}", border_style="bright_blue")
    )


def card_playable(mask, card_id) -> bool:
    """True if any (card_id, target) action is legal in the mask.

    The action space is (card x target) cross-product (ROB-60); a card is
    playable iff at least one of its target slots is unmasked.
    """
    base = int(card_id) * MAX_ENEMIES
    return any(bool(mask[base + t]) for t in range(MAX_ENEMIES))


def render_hand(console: Console, env) -> list:
    """Render every card in hand (duplicates included) as a numbered list.

    Each playable card slot gets a local index. Returns the local-index ->
    CardId mapping (targeting is resolved by the controller, since a card's
    global action depends on the chosen target — ROB-60). The end-turn local
    index is len(returned list).

    Affordable + targetable cards get a live `(N)` index; others get a dim `-`
    and are not selectable.
    """
    mask = env.action_masks()
    hand = env.state_piles().hand

    action_map: list = []  # local index -> CardId
    table = Table.grid(padding=(0, 3))
    for _ in range(3):
        table.add_column()

    row: list[Text] = []
    for card_id in hand:
        data = _core.card_data(card_id)
        playable = card_playable(mask, card_id)

        entry = Text()
        if playable:
            local = len(action_map)
            action_map.append(card_id)
            entry.append(f"({local}) ", style="bold white")
            entry.append(f"{_core.card_name(card_id):<8}", style="white")
            entry.append(f"{{{data.cost}}}", style="yellow")
        else:
            entry.append("  -  ", style="dim")
            entry.append(f"{_core.card_name(card_id):<8}", style="dim")
            entry.append(f"{{{data.cost}}}", style="dim")

        row.append(entry)
        if len(row) == 3:
            table.add_row(*row)
            row = []
    if row:
        while len(row) < 3:
            row.append(Text(""))
        table.add_row(*row)

    body: list = [Text("HAND:", style="bold"), table]
    if not hand:
        body = [Text("HAND:", style="bold"), Text("   (empty)", style="dim")]

    console.print(Panel(Group(*body), border_style="grey50"))

    return action_map


def render_piles(console: Console, env) -> None:
    """Render the pile view (toggled with 'p'). Does not consume a step."""
    piles = env.state_piles()

    def fmt_list(cards) -> Text:
        if not cards:
            return Text("    (empty)", style="dim")
        out = Text()
        for c in cards:
            data = _core.card_data(c)
            out.append(f"    {_core.card_name(c):<8}", style="white")
            out.append(f"{{{data.cost}}}\n", style="yellow")
        return out

    def fmt_counts(count_map) -> Text:
        if not count_map:
            return Text("    (empty)", style="dim")
        out = Text()
        # Sort by CardId value for stable display (does not reveal draw order).
        for c in sorted(count_map, key=lambda x: int(x)):
            data = _core.card_data(c)
            out.append(f"    {_core.card_name(c):<8}", style="white")
            out.append(f"x{count_map[c]}  ", style="bright_white")
            out.append(f"{{{data.cost}}}\n", style="yellow")
        return out

    draw_total = sum(piles.draw.values())
    body = Group(
        Text(f"DRAW ({draw_total}, shuffled — order hidden):", style="bold"),
        fmt_counts(piles.draw),
        Text(f"DISCARD ({len(piles.discard)}, top-most last):", style="bold"),
        fmt_list(piles.discard),
        Text(f"EXHAUST ({len(piles.exhaust)}):", style="bold"),
        fmt_list(piles.exhaust),
    )
    console.print(Panel(body, title="PILES", border_style="grey50"))


def render_prompt(console: Console, action_map: list[int], pile_view: bool) -> None:
    """Render the input prompt line."""
    if pile_view:
        console.print("[bold](p)[/bold] hand view   [bold](q)[/bold] quit")
    else:
        end_turn_local = len(action_map)
        if action_map:
            play = f"(0..{len(action_map) - 1}) play   "
        else:
            play = "(no playable cards)   "
        console.print(
            f"Action: {play}"
            f"[bold]({end_turn_local})[/bold] end turn   "
            f"[bold](p)[/bold] pile view   [bold](q)[/bold] quit"
        )


def render_end_screen(
    console: Console, obs: np.ndarray, env, outcome, log_path: str | None
) -> None:
    """Render the terminal end-of-fight screen."""
    console.clear()
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

    console.print(
        Panel(Group(banner, summary), title="MINI-SPIRE", border_style="bright_blue")
    )
    console.print("(press enter to exit)")
