"""Smoke-test a built wheel in its target interpreter.

Run by cibuildwheel (CIBW_TEST_COMMAND) after each wheel is built, and worth
running by hand against any artifact before it goes anywhere.

This exists as a FILE rather than an inline `python -c` one-liner because the
one-liner did not survive YAML. A folded block scalar left a leading space, so
the whole thing failed with IndentationError on every platform at once — the
wheels were fine and the test was broken, which is the most annoying way for a
release to fail.

What it checks is the failure that actually matters: a wheel can build
perfectly and still not load its extension module on the target platform, and
the build step cannot see that.
"""
from __future__ import annotations

import sys

import numpy as np

import minispire
from minispire import _core


def main() -> int:
    env = minispire.MinispireEnv()
    obs, _info = env.reset(seed=0)

    assert obs.shape == (_core.CombatEnv.OBS_SIZE,), obs.shape
    assert obs.dtype == np.float32, obs.dtype

    # Card descriptions live in src/card_descriptions.inc, which card.h
    # #includes. If that file were missing from the sdist the extension would
    # not compile at all — but if it were ever made a runtime data file, this
    # is where that would surface.
    assert _core.card_description(_core.CardId.Sentinel), "no card text"

    # Play to a terminal state. An extension that imports but crashes mid-step
    # is still a broken wheel.
    steps = 0
    info: dict = {}
    while steps < 500:
        legal = np.flatnonzero(env.action_masks())
        if legal.size == 0:
            break
        obs, _reward, terminated, truncated, info = env.step(int(legal[-1]))
        steps += 1
        if terminated or truncated:
            break

    assert steps > 0, "no legal action on turn 1"
    print(
        f"wheel OK  minispire {minispire.__version__}  "
        f"python {sys.version.split()[0]}  "
        f"obs {obs.shape[0]}  steps {steps}  info {info}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
