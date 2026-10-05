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
    Actions come from env.play_action — the copy at a hand position, at each
    target — rather than from index arithmetic, which CLAUDE.md reserves to
    the encoder.
    """
    blade = _core.CardId.InfernalBlade
    env = MinispireEnv(deck=[blade] * 5)
    env.reset(seed=seed)

    mask = env.action_masks()
    legal = [
        a for a in (
            env.play_action(0, t) for t in range(_core.CombatEnv.MAX_ENEMIES)
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
    """StatePiles with per-copy costs, each list parallel to its pile."""

    def __init__(self, hand=(), hand_costs=(), discard=(), discard_costs=(),
                 draw_costs=None):
        self.hand = list(hand)
        self.hand_costs = list(hand_costs)
        # The engine's shape: CardId -> count, and CardId -> sorted costs.
        self.draw_costs = {c: sorted(v) for c, v in (draw_costs or {}).items()}
        self.draw = {c: len(v) for c, v in self.draw_costs.items()}
        self.discard = list(discard)
        self.discard_costs = list(discard_costs)
        self.exhaust = []
        self.exhaust_costs = []


class _StubMask(dict):
    """A sparse action mask. Anything not listed is masked off."""

    def __missing__(self, key):
        return False


class _StubHandEnv:
    """The three calls build_hand makes, answered with fixed values. Every copy
    is playable; play_action's "action" is just a key the stub mask knows.

    A real fight cannot test this render. Catching a base-vs-effective mix-up
    needs a card whose PRINTED cost differs from what it costs right now, and
    the only in-combat source of one is Infernal Blade — whose roll is
    platform-dependent and can land on Clash, printed cost 0, which renders
    {0} whether or not build_hand was ever fixed. Stating both numbers here
    makes the gap exact and the same on every platform. This is the reasoning
    the choice stubs below already use.

    There is deliberately no effective_cost(card_id): it answers per TYPE, and
    a panel that calls it is showing one copy's cost on every copy.
    """

    def __init__(self, hand, costs):
        self._piles = _StubPiles(hand, costs)

    def action_masks(self):
        return _StubMask({
            self.play_action(position, t): True
            for position in range(len(self._piles.hand))
            for t in range(_core.CombatEnv.MAX_ENEMIES)
        })

    def play_action(self, hand_index, target):
        return ("play", hand_index, target)

    def state_piles(self):
        return self._piles


def test_hand_panel_prints_the_effective_cost():
    # The panel is what the player reads, so the fix has to reach the render,
    # not just the accessor.
    bash = _core.CardId.Bash
    assert _core.card_data(bash).cost == 2, "fixture needs a nonzero base cost"

    text = _render(screen.build_hand(_StubHandEnv([bash], [0]))[0])
    line = next(ln for ln in text.splitlines() if _core.card_name(bash) in ln)
    assert "{0}" in line, f"expected the effective cost in {line!r}"
    assert "{2}" not in line, "the panel printed CardData.cost, not effective_cost"


def test_hand_panel_prints_each_copys_own_cost():
    # Madness on one Bash made every Bash in hand read {0}: the panel asked
    # for the type's cost, which is the cheapest copy's. Each box has to show
    # what THAT copy costs, in hand order.
    bash = _core.CardId.Bash
    name = _core.card_name(bash)

    text = _render(screen.build_hand(_StubHandEnv([bash, bash], [0, 2]))[0])
    assert f"(0) {name}  {{0}}" in text, text
    assert f"(1) {name}  {{2}}" in text, "the second copy took the first's cost"


# --------------------------------------------------------------------------
# The choice header. A relic can open a menu (Toolbox, before the first hand
# is dealt), and such a choice has no source CARD.
#
# These build the view directly instead of driving a real fight: the engine
# can produce a relic-sourced choice, but CombatEnv takes no relics yet, so
# Python cannot reach one. Only the two methods build_choice calls are stubbed.
# --------------------------------------------------------------------------


class _StubView:
    def __init__(self, source_card, kind, options, option_costs=None):
        self.active = True
        self.kind = kind
        self.source_card = source_card
        self.is_optional = False
        self.copies = 1
        self.options = options
        self.option_damage = [0] * len(options)
        self.option_costs = (
            option_costs if option_costs is not None
            else [_core.card_data(c).cost for c in options]
        )


class _StubChoiceEnv:
    def __init__(self, view):
        self._view = view

    def choice_view(self):
        return self._view


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


def test_choice_panel_prints_each_options_own_cost():
    # Exhume over an exhaust pile holding a Madness'd Bash and a plain one:
    # they are different picks, and the menu has to say so.
    bash = _core.CardId.Bash
    name = _core.card_name(bash)
    view = _StubView(
        _core.CardId.Exhume,
        _core.ChoiceKind.ExhaustToHand,
        [bash, bash],
        option_costs=[0, 2],
    )
    text = _render(screen.build_choice(_StubChoiceEnv(view))[0])
    assert f"(0) {name}  {{0}}" in text, text
    assert f"(1) {name}  {{2}}" in text, "the second option took the first's cost"


class _StubPilesEnv:
    def __init__(self, piles):
        self._piles = piles

    def state_piles(self):
        return self._piles


def test_pile_panel_prints_each_discarded_copys_own_cost():
    # Both render paths: boxes for a small pile, the name list past
    # PILE_BOX_LIMIT. The list path formats the cost on its own, so it can
    # regress separately from the boxes.
    bash = _core.CardId.Bash
    name = _core.card_name(bash)

    small = _StubPiles(discard=[bash, bash], discard_costs=[0, 2])
    text = _render(screen.build_piles(_StubPilesEnv(small))[0])
    assert f"{name}  {{0}}" in text, text
    assert f"{name}  {{2}}" in text, "the second copy took the first's cost"

    n = screen.PILE_BOX_LIMIT + 1
    big = _StubPiles(discard=[bash] * n, discard_costs=[0] + [2] * (n - 1))
    lines = [ln for ln in _render(screen.build_piles(_StubPilesEnv(big))[0])
             .splitlines() if name in ln]
    assert len(lines) == n, "expected the name-list rendering"
    assert "{0}" in lines[0] and "{2}" in lines[1], lines[:2]


def test_pile_panel_prints_each_draw_pile_cost():
    # A Madness'd Bash shuffled back into the draw pile keeps its 0; the panel
    # asked effective_cost(Bash), which answers from the HAND, so it showed
    # the printed 2 — or 0 on every Bash while a free copy sat in hand.
    bash = _core.CardId.Bash
    name = _core.card_name(bash)

    small = _StubPiles(draw_costs={bash: [2, 0, 2]})
    text = _render(screen.build_piles(_StubPilesEnv(small))[0])
    assert f"{name}  {{0}}" in text, text
    assert f"{name}  {{2}}" in text, "the full-price copies lost their cost"

    # Past PILE_BOX_LIMIT the pile is a name list with one line per
    # (type, cost) and its count.
    fillers = [c for c in _core.CardId.__members__.values()
               if c not in (bash, screen.NO_SOURCE_CARD)][:screen.PILE_BOX_LIMIT]
    big = _StubPiles(draw_costs={bash: [2, 0, 2], **{c: [1] for c in fillers}})
    lines = [ln for ln in _render(screen.build_piles(_StubPilesEnv(big))[0])
             .splitlines() if f"{name} " in ln]
    assert any("x1" in ln and "{0}" in ln for ln in lines), lines
    assert any("x2" in ln and "{2}" in ln for ln in lines), lines
