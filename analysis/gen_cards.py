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


# Which mechanic-tiers a card requires. Tier A cards need NONE beyond the base
# engine + AoE/hits/X-cost (all landing in Tier A itself).
def classify(c: Card) -> str:
    needs = set()
    if c.draw or c.energy:
        needs.add("B")  # player card-flow
    if c.type == "Power":
        needs.add("C")  # player powers
    if c.special:
        needs.add("E")  # a special handler (may overlap other tiers)
    # exhaust as a keyword is fine (Tier A handles it — Slimed already exhausts);
    # exhaust-SYNERGY lives in card `special` prose, already caught by E.
    c.tiers_needed = needs
    if not needs:
        return "A"
    # First non-A tier in order determines where it lands.
    for t in ("B", "C", "D", "E"):
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


def emit_tier_a(cards: list[Card]) -> str:
    """Emit C++ CardId enum entries + CARD_DATABASE rows for Tier A cards, EXCEPT
    the six starters (already hand-written in card.h). Field order matches the
    CardData struct: name, cost, damage, hits, block, target, debuffs, powers,
    type, exhaust, ethereal, unplayable."""
    STARTERS = {"Strike", "StrikePlus", "Defend", "DefendPlus", "Bash", "BashPlus"}
    tier_a = [c for c in cards if c.tier == "A" and c.id not in STARTERS]
    lines_enum = []
    lines_db = []
    for c in tier_a:
        lines_enum.append(f"  {c.id},")
        cost = "kXCost" if c.cost == "X" else c.cost
        hits = "-1" if c.hits == "X" else c.hits  # -1 = X hits
        debuffs, powers = _parse_applies(c.applies)
        deb = ", ".join(f"{{Debuff::{e}, {a}, {_cpp_target(t)}}}" for e, a, t in debuffs)
        pow_ = ", ".join(f"{{Power::{e}, {a}, {_cpp_target(t)}}}" for e, a, t in powers)
        # Iron Wave's CSV "Enemy/None" -> Enemy (it deals damage, so it targets).
        raw_target = "Enemy" if c.target == "Enemy/None" else c.target
        tgt = {"None": "CardTarget::None", "Enemy": "CardTarget::Enemy",
               "AllEnemies": "CardTarget::AllEnemies",
               "Self": "CardTarget::Self"}[raw_target]
        lines_db.append(
            f'    {{CardId::{c.id}, {{"{c.name}", {cost}, {c.damage}, {hits}, '
            f'{c.block}, {tgt}, {{{deb}}}, {{{pow_}}}, CardType::{c.type}, '
            f'{"true" if c.exhaust else "false"}, '
            f'{"true" if c.ethereal else "false"}}}}},'
        )
    return "// ---- CardId enum (append after existing) ----\n" + \
           "\n".join(lines_enum) + \
           "\n\n// ---- CARD_DATABASE rows ----\n" + "\n".join(lines_db)


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
    if args.emit_tier == "A":
        print(emit_tier_a(cards))


if __name__ == "__main__":
    main()
