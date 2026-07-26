"""One-shot importer: parse data/ironclad_cards.csv and emit C++ for card.h.

This is a migration tool, not a build step (ROB-80). The CSV is a one-time
import of the Ironclad card pool; its output is pasted directly into card.h,
which stays hand-written. Kept in-repo so the import is reproducible / re-runnable
if the CSV changes.

Usage:
    uv run python -m analysis.gen_cards --classify      # tier report only
    uv run python -m analysis.gen_cards --emit-tier A    # C++ for Tier A cards
"""
from __future__ import annotations

import argparse
import csv
import pathlib
from dataclasses import dataclass, field

CSV_PATH = pathlib.Path(__file__).resolve().parent.parent / "data" / "ironclad_cards.csv"


def _blank(v: str) -> bool:
    return v.strip().lower() in ("", "n/a", "none")


@dataclass
class Card:
    id: str
    name: str
    type: str
    rarity: str
    cost: str
    target: str
    damage: int
    hits: str  # int, or "X" (Whirlwind: X hits)
    block: int
    draw: int
    energy: int
    applies: str  # raw DSL
    exhaust: bool
    ethereal: bool
    innate: bool
    retain: bool
    special: str  # prose, or "" if none
    upgrade_of: str  # id, or ""
    tiers_needed: set[str] = field(default_factory=set)  # mechanics this card needs


def _b(v: str) -> bool:
    return v.strip().upper() == "TRUE"


def load_cards() -> list[Card]:
    cards = []
    with open(CSV_PATH, newline="") as f:
        for row in csv.DictReader(f):
            cards.append(
                Card(
                    id=row["id"].strip(),
                    name=row["name"].strip(),
                    type=row["type"].strip(),
                    rarity=row["rarity"].strip(),
                    cost=row["cost"].strip(),
                    target=row["target"].strip(),
                    damage=int(row["damage"] or 0),
                    hits=(row["hits"].strip() or "0"),
                    block=int(row["block"] or 0),
                    draw=int(row["draw"] or 0),
                    energy=int(row["energy"] or 0),
                    applies="" if _blank(row["applies"]) else row["applies"].strip(),
                    exhaust=_b(row["exhaust"]),
                    ethereal=_b(row["ethereal"]),
                    innate=_b(row["innate"]),
                    retain=_b(row["retain"]),
                    special="" if _blank(row["special"]) else row["special"].strip(),
                    upgrade_of="" if _blank(row["upgrade_of"]) else row["upgrade_of"].strip(),
                )
            )
    return cards


import re

# "Lose N HP" is a Tier-B mechanic (lose_hp), even though it lives in the CSV's
# special prose. Extract it so cards whose ONLY special is HP-loss stay Tier B.
_LOSE_HP_RE = re.compile(r"los[et]\s+(\d+)\s+HP", re.IGNORECASE)


def lose_hp_amount(c: Card) -> int:
    m = _LOSE_HP_RE.search(c.special)
    return int(m.group(1)) if m else 0


def _residual_special(c: Card) -> str:
    """The special prose with the 'Lose N HP' clause removed — what's LEFT is
    the genuinely-special effect (deck manip, conditionals) needing Tier D/E."""
    return _LOSE_HP_RE.sub("", c.special).strip(" .")


# Which mechanic-tiers a card requires. Tier A needs NONE beyond AoE/hits/X-cost.
def classify(c: Card) -> str:
    needs = set()
    if c.draw or c.energy or lose_hp_amount(c):
        needs.add("B")  # player card-flow (draw / energy / lose-HP)
    if c.type == "Power":
        needs.add("C")  # player powers
    if _residual_special(c):
        needs.add("E")  # a special handler beyond lose-HP
    c.tiers_needed = needs
    if not needs:
        return "A"
    # HIGHEST required tier wins (a card needing both B and E is E).
    for t in ("E", "D", "C", "B"):
        if t in needs:
            return t
    return "A"


# --- C++ emission -----------------------------------------------------------

def _parse_applies(dsl: str) -> tuple[list[tuple[str, int, str]], list[tuple[str, int, str]]]:
    """Split the applies DSL into (debuffs, powers). Each entry is
    (effect, amount, target). target in {Enemy, Self, AllEnemies}."""
    DEBUFFS = {"Vulnerable", "Weak", "Frail", "Entangle"}
    POWERS = {"Strength", "Dexterity", "Ritual", "Metallicize", "Enrage", "Artifact"}
    debuffs, powers = [], []
    for part in filter(None, (p.strip() for p in dsl.split(";"))):
        eff_amt, _, tgt = part.partition("@")
        eff, _, amt = eff_amt.partition(":")
        eff, amt, tgt = eff.strip(), int(amt), tgt.strip()
        if eff in DEBUFFS:
            debuffs.append((eff, amt, tgt))
        elif eff in POWERS:
            powers.append((eff, amt, tgt))
        else:
            raise ValueError(f"unknown effect in applies: {eff!r}")
    return debuffs, powers


def _cpp_target(t: str) -> str:
    # DSL applies-target -> C++ StatusApplication Target, for a PLAYER card:
    # Self affects the player (Character); Enemy/AllEnemies affect the enemy the
    # card resolves against (AllEnemies loops all living enemies, each Enemy).
    return {"Enemy": "Target::Enemy", "Self": "Target::Character",
            "AllEnemies": "Target::Enemy", "Character": "Target::Character"}[t]


STARTERS = {"Strike", "StrikePlus", "Defend", "DefendPlus", "Bash", "BashPlus"}


def _row(c: Card) -> str:
    """One CARD_DATABASE row (fully positional). Field order matches CardData:
    name, cost, damage, hits, block, target, debuffs, powers, type, exhaust,
    ethereal, unplayable, draw, energy, lose_hp."""
    cost = "kXCost" if c.cost == "X" else c.cost
    hits = "-1" if c.hits == "X" else c.hits  # -1 = X hits
    debuffs, powers = _parse_applies(c.applies)
    deb = ", ".join(f"{{Debuff::{e}, {a}, {_cpp_target(t)}}}" for e, a, t in debuffs)
    pow_ = ", ".join(f"{{Power::{e}, {a}, {_cpp_target(t)}}}" for e, a, t in powers)
    raw_target = "Enemy" if c.target == "Enemy/None" else c.target
    tgt = {"None": "CardTarget::None", "Enemy": "CardTarget::Enemy",
           "AllEnemies": "CardTarget::AllEnemies", "Self": "CardTarget::Self"}[raw_target]
    return (
        f'    {{CardId::{c.id}, {{"{c.name}", {cost}, {c.damage}, {hits}, '
        f'{c.block}, {tgt}, {{{deb}}}, {{{pow_}}}, CardType::{c.type}, '
        f'{"true" if c.exhaust else "false"}, '
        f'{"true" if c.ethereal else "false"}, false, '  # unplayable
        f'{c.draw}, {c.energy}, {lose_hp_amount(c)}}}}},'
    )


def emit_tier(cards: list[Card], tier: str) -> str:
    """Emit CardId enum entries + CARD_DATABASE rows for one tier (skipping the
    hand-written starters)."""
    members = [c for c in cards if c.tier == tier and c.id not in STARTERS]
    enum = "\n".join(f"  {c.id}," for c in members)
    db = "\n".join(_row(c) for c in members)
    return f"// ---- CardId enum (Tier {tier}, append) ----\n{enum}" \
           f"\n\n// ---- CARD_DATABASE rows (Tier {tier}) ----\n{db}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--classify", action="store_true")
    ap.add_argument("--emit-tier", default=None)
    args = ap.parse_args()

    cards = load_cards()
    for c in cards:
        c.tier = classify(c)

    if args.classify:
        from collections import Counter
        Counter(c.tier for c in cards)
        print(f"{len(cards)} cards total")
        for t in ("A", "B", "C", "D", "E"):
            members = [c.id for c in cards if c.tier == t]
            print(f"\nTier {t}: {len(members)} cards")
            print("  " + ", ".join(members))
    if args.emit_tier:
        print(emit_tier(cards, args.emit_tier))


if __name__ == "__main__":
    main()
