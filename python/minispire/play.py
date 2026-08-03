"""Entry point for interactive human play.

Parses the seed / pool / deck / config arguments and launches the Textual UI
(`minispire.render.app`). Run via `minispire-play [seed]` or
`python -m minispire.play`.

Exit codes: 0 win, 1 loss, 2 quit.

This module used to BE the UI — a print-and-prompt loop that redrew the screen
and blocked on input(). That loop was retired once the Textual UI reached
parity (ROB-83), the same way the C++ minispire-cli was retired once this file
replaced it. What survives here is argument handling, which never belonged to
either UI.
"""
from __future__ import annotations

import argparse
import sys

from minispire.render import require_tui

require_tui()  # before the render imports below, so a missing extra explains itself

from minispire import _core  # noqa: E402


def _parse_seed(raw: str | None) -> int:
    if raw is None:
        return 0
    try:
        return int(raw)
    except ValueError:
        print(f"warning: could not parse seed {raw!r}; using 0", file=sys.stderr)
        return 0


def _parse_pool(raw: str | None):
    """Map a pool name ('weak'/'strong'/'elite') to an EncounterPool, or None."""
    if raw is None:
        return None
    from minispire._core import EncounterPool

    pools = {
        "weak": EncounterPool.Weak,
        "strong": EncounterPool.Strong,
        "elite": EncounterPool.Elite,
    }
    key = str(raw).strip().lower()
    if key not in pools:
        raise SystemExit(
            f"unknown pool {raw!r}; choose from {sorted(pools)}"
        )
    return pools[key]


def _parse_deck(raw) -> list | None:
    """Parse a deck spec into a list[CardId]. Accepts a comma-separated string
    ('strike,strike,defend') or a list of names (from YAML). Names match CardId
    members case-insensitively (strike, strike+, defend, bash, ...). None -> the
    starter deck (handled downstream)."""
    if raw is None:
        return None
    names = raw.split(",") if isinstance(raw, str) else list(raw)
    # Build a case-insensitive lookup from the engine's CardId enum + display
    # names (so both "StrikePlus" and "strike+" work).
    lookup: dict[str, object] = {}
    for member_name, cid in _core.CardId.__members__.items():
        lookup[member_name.lower()] = cid
        lookup[_core.card_name(cid).lower()] = cid
    deck = []
    for n in names:
        key = str(n).strip().lower()
        if not key:
            continue
        if key not in lookup:
            raise SystemExit(f"unknown card {n!r} in deck")
        deck.append(lookup[key])
    return deck


def _load_config(path: str) -> dict:
    """Load a YAML play config: keys seed / pool / deck (all optional). CLI
    flags override these."""
    import yaml

    with open(path) as f:
        data = yaml.safe_load(f) or {}
    if not isinstance(data, dict):
        raise SystemExit(f"config {path!r} must be a mapping")
    return data


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="minispire-play",
        description="Interactive human play of a Mini-Spire combat.",
    )
    parser.add_argument(
        "seed", nargs="?", default=None, help="RNG seed (default: 0)"
    )
    parser.add_argument(
        "--pool",
        choices=["weak", "strong", "elite"],
        default=None,
        help="Act 1 encounter pool to draw the fight from (default: weak).",
    )
    parser.add_argument(
        "--deck",
        default=None,
        help="Comma-separated deck, e.g. 'strike,strike,defend,bash' "
        "(default: Ironclad starter). Card names match CardId case-insensitively.",
    )
    parser.add_argument(
        "--config",
        default=None,
        help="Optional YAML config with seed/pool/deck. CLI flags override it.",
    )
    parser.add_argument(
        "--ascii-only",
        action="store_true",
        help="Use plain ASCII avatars and intent icons instead of pixel art "
        "and unicode, for terminals that misalign wide glyphs.",
    )
    args = parser.parse_args()

    # Config file provides defaults; explicit CLI flags override each key.
    cfg = _load_config(args.config) if args.config else {}
    seed_raw = args.seed if args.seed is not None else cfg.get("seed")
    pool_raw = args.pool if args.pool is not None else cfg.get("pool")
    deck_raw = args.deck if args.deck is not None else cfg.get("deck")

    seed = _parse_seed(str(seed_raw) if seed_raw is not None else None)
    pool = _parse_pool(pool_raw)
    deck = _parse_deck(deck_raw)

    from minispire.render.app import run

    sys.exit(run(seed, pool=pool, deck=deck, ascii_only=args.ascii_only))


if __name__ == "__main__":
    main()
