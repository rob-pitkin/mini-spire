"""Derive the keystrokes for the demo GIF from a real winning playthrough.

`assets/demo.tape` drives the TUI by keypress, and a keypress is a LOCAL index
into the hand — which shifts every time a card leaves it. The previous tape was
transcribed by hand from a play log and carried a comment apologising for
exactly that fragility. This computes it instead.

Run:  uv run python -m analysis.derive_demo_line
Emits the tape body on stdout; paste it into assets/demo.tape.

The policy is deliberately simple and readable rather than strong: the GIF is
showing the interface, not an agent. What matters is that the line WINS, so the
recording ends on a victory screen rather than a defeat.
"""
from __future__ import annotations

import argparse

import numpy as np

from minispire import _core
from minispire.env import MinispireEnv
from minispire.render import screen
from minispire.render.app import resolve_card_action


def _hand_options(env) -> list:
    """The local-index -> CardId map the TUI would show right now."""
    _panel, action_map = screen.build_hand(env)
    return action_map


def _pick(env, action_map: list) -> int | None:
    """Choose a local index to press, or None to end the turn.

    Attacks before skills, biggest damage first — a legible aggressive line that
    reads well on screen. Not a good policy; it does not need to be.
    """
    best, best_dmg = None, -1
    for i, card_id in enumerate(action_map):
        data = _core.card_data(card_id)
        if data.damage > best_dmg:
            best, best_dmg = i, data.damage
    return best if best_dmg > 0 else (0 if action_map else None)


def play(seed: int, max_turns: int = 30) -> tuple[bool, list[str], int]:
    """Play one fight. Returns (won, keystrokes, turns)."""
    env = MinispireEnv()
    obs, _ = env.reset(seed=seed)
    keys: list[str] = []

    for _ in range(max_turns * 12):
        if env.outcome != _core.Outcome.InProgress:
            break

        view = env.choice_view()
        if view.active:
            keys.append("0")  # take the first option; the box grid is on screen
            obs, *_ = env.step(_core.CombatEnv.FIRST_OPTION_SLOT)
            continue

        action_map = _hand_options(env)
        index = _pick(env, action_map)
        if index is None or index >= len(action_map):
            keys.append(str(len(action_map)))  # End Turn is the last option
            obs, *_ = env.step(_core.CombatEnv.END_TURN_ACTION)
            continue

        card_id = action_map[index]
        living = screen.living_enemy_slots(obs)
        action = resolve_card_action(card_id, living)
        keys.append(str(index))
        if action is None:
            # Targeting screen: the app opens a pick-one-of-N, so the demo
            # presses a second key. Slot 0 is the first living enemy.
            keys.append("0")
            action = int(card_id) * _core.CombatEnv.MAX_ENEMIES + living[0]
        obs, _r, terminated, truncated, _i = env.step(action)
        if terminated or truncated:
            break

    return env.outcome == _core.Outcome.Won, keys, env.turn_number


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-seed", type=int, default=200)
    ap.add_argument("--max-keys", type=int, default=34,
                    help="keep the GIF short; longer lines make a slow demo")
    args = ap.parse_args()

    for seed in range(args.max_seed):
        won, keys, turns = play(seed)
        if won and len(keys) <= args.max_keys:
            print(f"# seed {seed}: WON in {turns} turns, {len(keys)} keypresses")
            for k in keys:
                print(f'Type "{k}"')
                print("Sleep 700ms")
            return
    print("# no short winning line found; widen --max-seed or --max-keys")


if __name__ == "__main__":
    main()
