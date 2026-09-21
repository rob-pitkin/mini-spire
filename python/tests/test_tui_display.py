"""Display-correctness regressions found by playing (ROB-83).

Both bugs here were invisible to the test suite and obvious within one fight,
which is the argument for the TUI existing at all. Both are also the same
shape: a number rendered from the wrong source.
"""
from __future__ import annotations

from minispire import _core
from minispire.env import MinispireEnv
from minispire.render import screen


def test_turn_index_is_not_the_last_obs_slot():
    # The choice block sits AFTER the turn float, so OBS_SIZE - 1 lands inside
    # it. The TUI computed the index that way and rendered a choice-channel
    # value — zero unless a choice was pending — as the turn counter.
    #
    # Identical in shape to the kEndTurnAction bug CLAUDE.md records: a landmark
    # that used to be last, displaced by a block appended after it.
    assert screen.TURN_NUMBER == _core.CombatEnv.TURN_OBS_INDEX
    assert screen.TURN_NUMBER != _core.CombatEnv.OBS_SIZE - 1


def test_turn_counter_actually_advances():
    # What the player sees. Ending a turn must move the number on screen.
    env = MinispireEnv()
    obs, _ = env.reset(seed=1)
    start = int(obs[screen.TURN_NUMBER])
    obs, *_ = env.step(_core.CombatEnv.END_TURN_ACTION)
    assert int(obs[screen.TURN_NUMBER]) > start


def test_turn_index_agrees_with_the_engine_state():
    env = MinispireEnv()
    obs, _ = env.reset(seed=2)
    for _ in range(4):
        assert int(obs[screen.TURN_NUMBER]) == env.turn_number
        obs, _r, terminated, truncated, _i = env.step(_core.CombatEnv.END_TURN_ACTION)
        if terminated or truncated:
            break


def _play_infernal_blade(seed: int):
    """Open a fight of nothing but Infernal Blades, play one, return the grant.

    Returns (env, granted), granted being the card ids the blade put in hand.
    Targets are built with encode_action rather than by dividing an index by
    MAX_ENEMIES, which CLAUDE.md reserves to the encoder.
    """
    blade = _core.CardId.InfernalBlade
    env = MinispireEnv(deck=[blade] * 5)
    env.reset(seed=seed)

    mask = env.action_masks()
    legal = [
        a for a in (
            _core.encode_action(_core.ActionBlock.Combat, int(blade), t)
            for t in range(_core.CombatEnv.MAX_ENEMIES)
        )
        if mask[a]
    ]
    assert legal, f"Infernal Blade should be playable on turn 1 (seed {seed})"
    env.step(int(legal[0]))

    granted = [c for c in env.state_piles().hand if c != blade]
    assert granted, f"Infernal Blade should have added an Attack (seed {seed})"
    return env, granted


def test_effective_cost_reflects_a_free_this_turn_grant():
    # Infernal Blade adds a random Attack that costs 0 for the turn. The hand
    # showed CardData.cost and so advertised a price the engine would not
    # charge — the player reads 1, pays 0.
    #
    # Asserted as an invariant over whatever is rolled, never against a chosen
    # card. WHICH Attack appears is platform-dependent — libstdc++ and libc++
    # do not agree on std::uniform_int_distribution, so seed 1 yields a cost-1
    # Attack on macOS and Clash, printed cost 0, on Linux. The grant is free on
    # both, because GenerateCards sets the override unconditionally on the
    # free-this-turn branch, unlike the free-this-combat branch which skips a
    # card that already costs 0 (src/action.cc).
    for seed in range(5):
        env, granted = _play_infernal_blade(seed)
        for card in granted:
            assert env.effective_cost(card) == 0, (
                f"{_core.card_name(card)} was granted at cost "
                f"{env.effective_cost(card)}, not free (seed {seed})"
            )


class _StubPiles:
    def __init__(self, hand):
        self.hand = hand


class _StubMask(dict):
    """A sparse action mask. Anything not listed is masked off."""

    def __missing__(self, key):
        return False


class _StubHandEnv:
    """The three calls build_hand makes, answered with fixed values.

    A real fight cannot test this render. Catching a base-vs-effective mix-up
    needs a card whose PRINTED cost differs from what it costs right now, and
    the only in-combat source of one is Infernal Blade — whose roll is
    platform-dependent and can land on Clash, printed cost 0, which renders
    {0} whether or not build_hand was ever fixed. Stating both numbers here
    makes the gap exact and the same on every platform. This is the reasoning
    the choice stubs below already use.
    """

    def __init__(self, hand, costs):
        self._piles = _StubPiles(hand)
        self._costs = costs

    def action_masks(self):
        return _StubMask({
            _core.encode_action(_core.ActionBlock.Combat, int(card_id), t): True
            for card_id in self._piles.hand
            for t in range(_core.CombatEnv.MAX_ENEMIES)
        })

    def state_piles(self):
        return self._piles

    def effective_cost(self, card_id):
        return self._costs[card_id]


def test_hand_panel_prints_the_effective_cost():
    # The panel is what the player reads, so the fix has to reach the render,
    # not just the accessor.
    bash = _core.CardId.Bash
    assert _core.card_data(bash).cost == 2, "fixture needs a nonzero base cost"

    text = _render(screen.build_hand(_StubHandEnv([bash], {bash: 0}))[0])
    line = next(ln for ln in text.splitlines() if _core.card_name(bash) in ln)
    assert "{0}" in line, f"expected the effective cost in {line!r}"
    assert "{2}" not in line, "the panel printed CardData.cost, not effective_cost"


# --------------------------------------------------------------------------
# The choice header. A relic can open a menu (Toolbox, before the first hand
# is dealt), and such a choice has no source CARD.
#
# These build the view directly instead of driving a real fight: the engine
# can produce a relic-sourced choice, but CombatEnv takes no relics yet, so
# Python cannot reach one. Only the two methods build_choice calls are stubbed.
# --------------------------------------------------------------------------


class _StubView:
    def __init__(self, source_card, kind, options):
        self.active = True
        self.kind = kind
        self.source_card = source_card
        self.is_optional = False
        self.copies = 1
        self.options = options
        self.option_damage = [0] * len(options)


class _StubChoiceEnv:
    def __init__(self, view):
        self._view = view

    def choice_view(self):
        return self._view

    def effective_cost(self, card_id):
        return _core.card_data(card_id).cost


def _render(panel) -> str:
    import io

    from rich.console import Console

    console = Console(file=io.StringIO(), width=200)
    console.print(panel)
    return console.file.getvalue()


def test_a_relic_opened_choice_names_no_card():
    # source_card defaulted to Strike, so Toolbox's menu announced "Strike:" —
    # a card the player never played, stated as the cause of the prompt.
    view = _StubView(
        screen.NO_SOURCE_CARD,
        _core.ChoiceKind.DiscoverColorlessCard,
        [_core.CardId.Finesse, _core.CardId.Panacea],
    )
    panel, count = screen.build_choice(_StubChoiceEnv(view))
    assert count == 2

    text = _render(panel)
    assert "Choose a Colorless card" in text, "the kind fell back to the generic prompt"
    assert "Strike" not in text
    assert "(none)" not in text, "the sentinel leaked into the header"


def test_a_card_opened_choice_still_names_its_card():
    # The control: the prefix is dropped only when there is no source card.
    view = _StubView(
        _core.CardId.Armaments,
        _core.ChoiceKind.UpgradeCardInHand,
        [_core.CardId.Strike, _core.CardId.Defend],
    )
    text = _render(screen.build_choice(_StubChoiceEnv(view))[0])
    assert "Armaments:" in text
