"""Textual app tests, driven headlessly by Pilot (ROB-83).

test_play.py states the interactive loop is not testable. It was not — a
print-and-prompt loop blocked on stdin. `App.run_test()` runs the real app with
no terminal and simulates keypresses, so the loop that actually plays the game
is now covered.

These are about WIRING: that a keypress reaches the engine, that the two-phase
targeting hands off correctly, and that the option list the keys index into is
the one on screen. Layout is verified by playing it.
"""
from __future__ import annotations

import pytest

from minispire import _core
from minispire.render import screen
from minispire.render.app import EXIT_QUIT, MinispireApp, card_action
from minispire.render.keys import Mode


def test_card_action_is_the_cross_product_index():
    # The action space is (card x target); slot 0 is canonical for untargeted.
    n = _core.CombatEnv.MAX_ENEMIES
    assert card_action(_core.CardId.Strike, 0) == int(_core.CardId.Strike) * n
    assert card_action(_core.CardId.Strike, 3) == int(_core.CardId.Strike) * n + 3


@pytest.mark.asyncio
async def test_app_starts_in_play_mode_with_a_hand():
    app = MinispireApp(log=False, seed=1)
    async with app.run_test():
        assert app.mode is Mode.PLAY
        # option_count is hand + End Turn, so it always exceeds the hand size.
        assert app.option_count() == len(app._action_map) + 1
        assert app._action_map, "a fresh hand should have at least one playable card"


@pytest.mark.asyncio
async def test_q_quits_with_the_quit_exit_code():
    app = MinispireApp(log=False, seed=1)
    async with app.run_test() as pilot:
        await pilot.press("q")
    assert app.exit_code == EXIT_QUIT


@pytest.mark.asyncio
async def test_p_toggles_pile_view_and_back():
    app = MinispireApp(log=False, seed=1)
    async with app.run_test() as pilot:
        await pilot.press("p")
        assert app.mode is Mode.PILES
        await pilot.press("p")
        assert app.mode is Mode.PLAY


@pytest.mark.asyncio
async def test_digits_in_pile_view_do_not_step_the_env():
    app = MinispireApp(log=False, seed=1)
    async with app.run_test() as pilot:
        await pilot.press("p")
        turn_before = app.env.turn_number
        for key in ("0", "1", "2", "enter"):
            await pilot.press(key)
        assert app.env.turn_number == turn_before
        assert app.mode is Mode.PILES


@pytest.mark.asyncio
async def test_focus_wraps_at_both_ends():
    app = MinispireApp(log=False, seed=1)
    async with app.run_test() as pilot:
        count = app.option_count()
        assert app.focus == 0
        await pilot.press("left")
        assert app.focus == count - 1, "left from the first option wraps to the last"
        await pilot.press("right")
        assert app.focus == 0, "right from the last wraps back to the first"


@pytest.mark.asyncio
async def test_end_turn_advances_the_turn():
    app = MinispireApp(log=False, seed=1)
    async with app.run_test() as pilot:
        turn_before = app.env.turn_number
        # End Turn is the last option: index len(action_map).
        await pilot.press(str(len(app._action_map)))
        assert app.env.turn_number > turn_before


@pytest.mark.asyncio
async def test_playing_a_card_changes_the_hand():
    app = MinispireApp(log=False, seed=1)
    async with app.run_test() as pilot:
        hand_before = list(app.env.state_piles().hand)
        await pilot.press("0")
        # Either the card resolved, or it needs a target and we are now choosing.
        if app.mode is Mode.CHOOSE:
            assert app.pending_card is not None
        else:
            assert list(app.env.state_piles().hand) != hand_before


@pytest.mark.asyncio
async def test_targeting_is_two_phase_and_cancellable():
    # Find a seed whose opening hand has a targeted card and 2+ living enemies,
    # so the app must enter the targeting phase rather than auto-target.
    for seed in range(60):
        app = MinispireApp(log=False, seed=seed, pool=_core.EncounterPool.Strong)
        async with app.run_test() as pilot:
            if len(screen.living_enemy_slots(app.obs)) < 2:
                continue
            targeted = [
                i for i, cid in enumerate(app._action_map)
                if _core.card_targets_enemy(cid)
            ]
            if not targeted:
                continue

            await pilot.press(str(targeted[0]))
            assert app.mode is Mode.CHOOSE
            assert app.pending_card is not None
            # The app holds the half-made selection — keys.py never sees it.
            assert app.option_count() == len(app._targets)

            await pilot.press("escape")
            assert app.pending_card is None, "escape must drop the pending card"
            assert app.mode is Mode.PLAY
            return
    pytest.skip("no seed in range produced a targeted card with 2+ living enemies")


@pytest.mark.asyncio
async def test_the_app_holds_on_the_end_screen_instead_of_vanishing():
    # Found by playing, not by the suite: the app called exit() on the winning
    # blow, and Textual tears the screen down on exit — so a player who won was
    # dropped straight back to a shell prompt and never saw a result.
    #
    # No test caught it because none of them played a fight to the end; they
    # all asserted on the first turn or two.
    import numpy as np

    app = MinispireApp(log=False, seed=0)
    async with app.run_test() as pilot:
        for _ in range(400):
            if app.finished:
                break
            # Drive with the engine's own mask rather than the UI, to reach a
            # terminal state quickly regardless of what the hand looks like.
            legal = np.flatnonzero(app.env.action_masks())
            if legal.size == 0:
                break
            app._step(int(legal[-1]))
            await pilot.pause()

        assert app.finished, "could not reach a terminal state"
        assert app.is_running, "the app must still be up to show the result"
        assert app.exit_code in (0, 1)

        # Any key leaves — there is nothing left to decide, so routing this
        # through key_to_intent would leave a winner pressing keys at a frozen
        # screen wondering what it wanted.
        await pilot.press("3")
    assert not app.is_running


@pytest.mark.asyncio
async def test_option_count_matches_what_was_rendered():
    # The invariant the whole design turns on: keys are bounds-checked against
    # option_count, and the player chose from the rendered list. If those are
    # ever computed from different passes, a selection means a different card
    # than the one on screen.
    app = MinispireApp(log=False, seed=4)
    async with app.run_test():
        _panel, rendered_map = screen.build_hand(app.env)
        assert app._action_map == rendered_map
        assert app.option_count() == len(rendered_map) + 1
