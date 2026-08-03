"""Card boxes for the stop-and-read screens (ROB-83 part 3).

The hand stays a compact list; a pending choice and an opened pile render real
card boxes. That split is the point: a card box is 7 rows, so a ten-card hand
at three across is over 30 rows and does not fit an 80x24 terminal — while
"which card do I Exhume" is unanswerable from a name once the pool is 154 deep.
"""
from __future__ import annotations

import io
import textwrap

from rich.console import Console

from minispire import _core
from minispire.env import MinispireEnv
from minispire.render import screen


def _render(renderable, width: int = 90) -> str:
    console = Console(file=io.StringIO(), width=width, force_terminal=False)
    console.print(renderable)
    return console.file.getvalue()


def test_card_boxes_fit_their_descriptions():
    # CARD_BOX_HEIGHT is derived from the longest description in the pool, not
    # guessed. A new card that wraps longer would be silently clipped — the
    # player would read truncated rules and not know it.
    inner = screen.CARD_BOX_WIDTH - 4  # borders + padding
    body_lines = screen.CARD_BOX_HEIGHT - 2  # borders
    worst, worst_name = 0, ""
    for name, card_id in _core.CardId.__members__.items():
        lines = len(textwrap.wrap(_core.card_description(card_id), inner)) or 1
        if lines > worst:
            worst, worst_name = lines, _core.card_name(card_id)
    assert worst <= body_lines, (
        f"{worst_name} needs {worst} lines but a box holds {body_lines}; "
        f"raise CARD_BOX_HEIGHT"
    )


def test_every_box_in_a_row_is_the_same_height():
    # Rich sizes a Panel to its content unless told otherwise, which made a row
    # of three come out ragged — Strike three rows tall beside Bash at four,
    # reading as broken rather than as a grid.
    short = screen.build_card_box(_core.CardId.Strike, 1)
    tall = screen.build_card_box(_core.CardId.Clash, 0)
    assert len(_render(short).rstrip("\n").split("\n")) == \
           len(_render(tall).rstrip("\n").split("\n"))


def test_a_box_shows_cost_name_and_rules_text():
    text = _render(screen.build_card_box(_core.CardId.Sentinel, 1, index=3))
    assert "Sentinel" in text
    assert "(3)" in text
    assert "{1}" in text
    assert "Exhausted" in text, "the rules text is the reason the box exists"


def test_a_box_shows_the_effective_cost_it_is_given():
    # The caller passes effective_cost, so a free card reads {0} even though
    # CardData.cost is higher.
    text = _render(screen.build_card_box(_core.CardId.Bash, 0))
    assert "{0}" in text


def _env_with_cards_in_discard(count: int) -> MinispireEnv:
    """A fight whose discard pile holds at least `count` cards.

    Ending turns, not playing cards: energy caps plays at ~3 a turn, so filling
    a discard by playing needs more turns than the fight lasts. An unplayed hand
    is discarded whole at end of turn, which fills it five at a time.

    The deck is oversized so the draw pile never empties and reshuffles the
    discard back — that reshuffle is what made the first version of this test
    assert against three empty piles and skip itself.
    """
    env = MinispireEnv(deck=[_core.CardId.Strike] * (count * 3 + 10))
    env.reset(seed=1)
    for _ in range(20):
        if len(env.state_piles().discard) >= count:
            break
        _obs, _r, terminated, truncated, _i = env.step(
            _core.CombatEnv.END_TURN_ACTION
        )
        if terminated or truncated:
            break
    return env


def test_small_piles_render_as_boxes():
    env = _env_with_cards_in_discard(2)
    assert len(env.state_piles().discard) <= screen.PILE_BOX_LIMIT
    panel, _count = screen.build_piles(env)
    text = _render(panel, width=100)
    assert "Strike" in text
    assert "Deal 6 damage" in text, "a small pile should show rules text"


def test_large_piles_stay_a_list():
    # Boxes stop helping once they no longer fit together. A long pile as boxes
    # is scrolling past everything rather than inspecting one thing, so past
    # PILE_BOX_LIMIT it degrades to names and costs.
    #
    # Asserted on the focusable count, not on the rendered text: the panel
    # holds three piles and the small ones are still boxed, so searching the
    # output for rules text finds the DRAW pile's box and proves nothing about
    # the discard. The count is per-pile and unambiguous.
    env = _env_with_cards_in_discard(screen.PILE_BOX_LIMIT + 2)
    discard = env.state_piles().discard
    assert len(discard) > screen.PILE_BOX_LIMIT, (
        "fixture failed to fill the discard — the test would pass vacuously"
    )
    panel, boxed = screen.build_piles(env)
    assert boxed < len(discard), (
        "an over-limit discard must not contribute card boxes"
    )
    assert "Strike" in _render(panel, width=100)
