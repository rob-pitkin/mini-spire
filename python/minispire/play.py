"""Interactive human-play TUI for Mini-Spire.

Drives a MinispireEnv from stdin, renders the fight with rich, and writes a
JSONL trajectory log. Run via `minispire-play [seed]` or
`python -m minispire.play`.

Exit codes: 0 win, 1 loss, 2 quit.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import pathlib
import sys

import numpy as np
from rich.console import Console

from minispire import _core
from minispire.env import MinispireEnv
from minispire.render import screen


def _open_log(seed: int) -> tuple[object, str | None]:
    """Open the JSONL trajectory log. Returns (file_or_None, path_or_None)."""
    log_dir = pathlib.Path("logs")
    try:
        log_dir.mkdir(exist_ok=True)
        timestamp = _dt.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        path = log_dir / f"{timestamp}-seed{seed}.jsonl"
        return open(path, "w"), str(path)
    except OSError as exc:
        print(f"warning: could not open log file: {exc}", file=sys.stderr)
        return None, None


def _log_state(log, env, obs: np.ndarray) -> None:
    if log is None:
        return
    record = {
        "event": "state",
        "turn": int(obs[screen.TURN_NUMBER]),
        "outcome": str(env.outcome),
        "obs": [float(x) for x in obs],
        "action_mask": [bool(b) for b in env.action_masks()],
    }
    log.write(json.dumps(record) + "\n")


def _log_action(log, turn: int, action: int, reward: float, terminated: bool) -> None:
    if log is None:
        return
    record = {
        "event": "action",
        "turn": turn,
        "action": action,
        "reward": float(reward),
        "terminated": bool(terminated),
    }
    log.write(json.dumps(record) + "\n")


def _parse_seed(raw: str | None) -> int:
    if raw is None:
        return 0
    try:
        return int(raw)
    except ValueError:
        print(f"warning: could not parse seed {raw!r}; using 0", file=sys.stderr)
        return 0


def _resolve_card_action(console, env, obs, card_id) -> int | None:
    """Resolve a chosen card to a global action index (card x target, ROB-60).

    Untargeted cards (Defend) use the canonical offset-0 slot. Targeted cards
    auto-target when there's a single living enemy; with multiple living
    enemies, prompt for the target slot. Returns None if the player cancels or
    enters an invalid target (the caller re-prompts).
    """
    n = _core.CombatEnv.MAX_ENEMIES

    if not _core.card_targets_enemy(card_id):
        return int(card_id) * n + 0  # untargeted -> canonical slot 0

    living = screen.living_enemy_slots(obs)
    if len(living) == 1:
        return int(card_id) * n + living[0]  # auto-target the only enemy

    # Multiple living enemies — prompt for the target slot.
    console.print(f"  target which enemy? {living}  (or 'c' to cancel)")
    try:
        raw = input("target> ").strip()
    except EOFError:
        return None
    if raw in ("c", "C"):
        return None
    try:
        target = int(raw)
    except ValueError:
        return None
    if target not in living:
        return None
    return int(card_id) * n + target


def run(seed: int, ascii_only: bool) -> int:
    """Run one interactive fight. Returns the process exit code."""
    console = Console()
    env = MinispireEnv()
    obs, _info = env.reset(seed=seed)

    log, log_path = _open_log(seed)
    pile_view = False

    try:
        while env.outcome == _core.Outcome.InProgress:
            _log_state(log, env, obs)

            screen.render_fight(console, obs, env, ascii_only=ascii_only)
            if pile_view:
                screen.render_piles(console, env)
                action_map: list = []
            else:
                action_map = screen.render_hand(console, env)
            screen.render_prompt(console, action_map, pile_view)

            try:
                raw = input("> ").strip()
            except EOFError:
                return 2

            if raw in ("q", "Q"):
                return 2
            if raw in ("p", "P"):
                pile_view = not pile_view
                continue
            if pile_view:
                continue  # action keys ignored in pile view

            # Parse a local action index.
            try:
                local = int(raw)
            except ValueError:
                continue  # re-prompt

            end_turn_local = len(action_map)
            if local == end_turn_local:
                global_action = _core.CombatEnv.NUM_ACTIONS - 1
            elif 0 <= local < len(action_map):
                card_id = action_map[local]
                global_action = _resolve_card_action(console, env, obs, card_id)
                if global_action is None:
                    continue  # targeting cancelled / invalid -> re-prompt
            else:
                continue  # out of range, re-prompt

            turn_before = int(obs[screen.TURN_NUMBER])
            obs, reward, terminated, truncated, _info = env.step(global_action)
            _log_action(log, turn_before, global_action, reward, terminated)

        # Terminal — final state + end screen.
        _log_state(log, env, obs)
        screen.render_end_screen(console, obs, env, env.outcome, log_path)
        try:
            input()
        except EOFError:
            pass
        return 0 if env.outcome == _core.Outcome.Won else 1
    finally:
        if log is not None:
            log.close()


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="minispire-play",
        description="Interactive human play of a Mini-Spire combat.",
    )
    parser.add_argument(
        "seed", nargs="?", default="0", help="RNG seed (default: 0)"
    )
    parser.add_argument(
        "--ascii-only",
        action="store_true",
        help="Use plain ASCII intent icons instead of unicode.",
    )
    args = parser.parse_args()

    seed = _parse_seed(args.seed)
    sys.exit(run(seed, args.ascii_only))


if __name__ == "__main__":
    main()
