"""JSONL trajectory logging for human play.

Every state the player saw and every action they took, appended as one JSON
object per line. Human games are the only source of expert trajectories this
project has, so a fight played and not recorded is data thrown away.

Lifted out of play.py when the print-and-prompt loop was retired (ROB-83): the
logging belonged to the loop only by accident of where it was written, and
dropping it along with the loop would have been a silent regression.
"""
from __future__ import annotations

import datetime as _dt
import json
import pathlib
import sys

import numpy as np


def open_log(seed: int) -> tuple[object, str | None]:
    """Open the trajectory log for a fight. Returns (file_or_None, path_or_None).

    A failure to open is a warning, never fatal — losing the recording of a
    game is worse than not playing it, but not much worse.
    """
    log_dir = pathlib.Path("logs")
    try:
        log_dir.mkdir(exist_ok=True)
        timestamp = _dt.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        path = log_dir / f"{timestamp}-seed{seed}.jsonl"
        return open(path, "w"), str(path)
    except OSError as exc:
        print(f"warning: could not open log file: {exc}", file=sys.stderr)
        return None, None


def log_state(log, env, obs: np.ndarray) -> None:
    """Record the state the player is about to act on.

    The full obs vector, not a summary: this is training data, and which parts
    matter is not knowable when the line is written.
    """
    if log is None:
        return
    log.write(json.dumps({
        "event": "state",
        "turn": int(env.turn_number),
        "outcome": str(env.outcome),
        "obs": [float(x) for x in obs],
        "action_mask": [bool(b) for b in env.action_masks()],
    }) + "\n")


def log_action(log, turn: int, action: int, reward: float, terminated: bool) -> None:
    """Record the action taken and what it produced."""
    if log is None:
        return
    log.write(json.dumps({
        "event": "action",
        "turn": turn,
        "action": action,
        "reward": float(reward),
        "terminated": bool(terminated),
    }) + "\n")
