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

# --- Observation layout (ROB-40) -------------------------------------------
# Indices into the 45-float obs vector.
CHAR_HP = 0
CHAR_MAX_HP = 1
CHAR_BLOCK = 2
CHAR_ENERGY = 3
CHAR_ENERGY_PER_TURN = 4
CHAR_STATUS = slice(5, 9)  # Vulnerable, Weak, Strength, Dexterity
ENEMY_HP = 9
ENEMY_MAX_HP = 10
ENEMY_BLOCK = 11
ENEMY_STATUS = slice(12, 16)
INTENT_IS_ATTACKING = 16
INTENT_ATTACK_DAMAGE = 17
INTENT_IS_BLOCKING = 18
INTENT_IS_BUFFING = 19
TURN_NUMBER = 44

# Status effect names in obs order.
STATUS_NAMES = ["Vulnerable", "Weak", "Strength", "Dexterity"]
STATUS_COLORS = {
    "Vulnerable": "orange3",
    "Weak": "purple",
    "Strength": "red",
    "Dexterity": "green",
}

# CardId display names + colors for cost annotation.
CARD_NAMES = {
    _core.CardId.Strike: "Strike",
    _core.CardId.Defend: "Defend",
    _core.CardId.Bash: "Bash",
    _core.CardId.StrikePlus: "Strike+",
    _core.CardId.DefendPlus: "Defend+",
    _core.CardId.BashPlus: "Bash+",
}

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


def render_fight(
    console: Console,
    obs: np.ndarray,
    *,
    ascii_only: bool = False,
) -> None:
    """Clear the screen and render the main fight panel from the obs vector."""
    console.clear()

    turn = int(obs[TURN_NUMBER])

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

    # Enemy column (intent line as the extra).
    intent = Text("Intent: ")
    intent.append_text(
        intent_text(
            is_attacking=bool(obs[INTENT_IS_ATTACKING]),
            attack_damage=int(obs[INTENT_ATTACK_DAMAGE]),
            is_blocking=bool(obs[INTENT_IS_BLOCKING]),
            is_buffing=bool(obs[INTENT_IS_BUFFING]),
            ascii_only=ascii_only,
        )
    )
    enemy = _entity_block(
        "JAW WORM",
        "JAW_WORM",
        int(obs[ENEMY_HP]),
        int(obs[ENEMY_MAX_HP]),
        int(obs[ENEMY_BLOCK]),
        obs,
        ENEMY_STATUS,
        extra_lines=[intent],
    )

    grid = Table.grid(expand=True, padding=(0, 4))
    grid.add_column(ratio=1)
    grid.add_column(ratio=1)
    grid.add_row(char, enemy)

    console.print(
        Panel(grid, title=f"MINI-SPIRE  ·  Turn {turn}", border_style="bright_blue")
    )


def render_hand(console: Console, env) -> list[int]:
    """Render every card in hand (duplicates included) as a numbered list.

    Each hand slot gets a local index. Because the action space is
    card-type indexed (ROB-35), multiple copies of the same card map to the
    same global action — but the player still sees all of them so they can
    tell how many copies they hold.

    Affordable cards get a live `(N)` index; unaffordable cards get a dim
    `-` and are not selectable. Returns the local-index -> global-action
    mapping; only playable slots appear in it (so the indices the player
    types line up). The end-turn local index is len(returned list).
    """
    mask = env.action_masks()
    hand = env.state_piles().hand

    action_map: list[int] = []
    table = Table.grid(padding=(0, 3))
    for _ in range(3):
        table.add_column()

    row: list[Text] = []
    for card_id in hand:
        data = _core.card_data(card_id)
        global_action = int(card_id)
        playable = bool(mask[global_action])

        entry = Text()
        if playable:
            local = len(action_map)
            action_map.append(global_action)
            entry.append(f"({local}) ", style="bold white")
            entry.append(f"{CARD_NAMES[card_id]:<8}", style="white")
            entry.append(f"{{{data.cost}}}", style="yellow")
        else:
            entry.append("  -  ", style="dim")
            entry.append(f"{CARD_NAMES[card_id]:<8}", style="dim")
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
            out.append(f"    {CARD_NAMES[c]:<8}", style="white")
            out.append(f"{{{data.cost}}}\n", style="yellow")
        return out

    def fmt_counts(count_map) -> Text:
        if not count_map:
            return Text("    (empty)", style="dim")
        out = Text()
        # Sort by CardId value for stable display (does not reveal draw order).
        for c in sorted(count_map, key=lambda x: int(x)):
            data = _core.card_data(c)
            out.append(f"    {CARD_NAMES[c]:<8}", style="white")
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
    console: Console, obs: np.ndarray, outcome, log_path: str | None
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
    enemy_hp = max(int(obs[ENEMY_HP]), 0)
    summary = Text()
    summary.append(f"\nIRONCLAD  HP {char_hp}/{int(obs[CHAR_MAX_HP])}\n", style="white")
    summary.append(f"JAW WORM  HP {enemy_hp}/{int(obs[ENEMY_MAX_HP])}\n", style="white")
    summary.append(f"\nCombat lasted {int(obs[TURN_NUMBER])} turns.\n", style="white")
    if log_path:
        summary.append(f"Trajectory saved to {log_path}\n", style="dim")

    console.print(
        Panel(Group(banner, summary), title="MINI-SPIRE", border_style="bright_blue")
    )
    console.print("(press enter to exit)")
