"""The RL path must not require the optional `tui` extra (ROB-83 part 4).

v1.0.0 ships to PyPI. Someone running `pip install minispire` to train an agent
should get numpy, gymnasium and the C++ extension — not a terminal rendering
stack they never call. `rich` moved to the `tui` extra to make that true, and
these tests are what keep it true: a top-level `from rich...` added to env.py
would silently undo it, and nothing else in the suite would notice, because
every dev and CI install HAS rich.

So the check has to actively hide rich rather than trust its absence.
"""
from __future__ import annotations

import builtins
import importlib
import sys

import numpy as np
import pytest


class _HideRich:
    """Make `import rich` fail, as it would in a training-only install."""

    def __enter__(self):
        self._real = builtins.__import__
        self._saved = {k: v for k, v in sys.modules.items() if k.split(".")[0] == "rich"}
        for name in self._saved:
            del sys.modules[name]

        def guard(name, *args, **kwargs):
            if name == "rich" or name.startswith("rich."):
                raise ModuleNotFoundError("No module named 'rich'")
            return self._real(name, *args, **kwargs)

        builtins.__import__ = guard
        return self

    def __exit__(self, *exc):
        builtins.__import__ = self._real
        sys.modules.update(self._saved)
        return False


def test_env_runs_a_full_episode_without_rich():
    # The whole point of the extra. If this fails, a training-only install is
    # broken and the failure surfaces on someone else's machine, not here.
    with _HideRich():
        env_mod = importlib.reload(importlib.import_module("minispire.env"))
        env = env_mod.MinispireEnv()
        obs, _ = env.reset(seed=1)
        from minispire import _core

        assert obs.shape == (_core.CombatEnv.OBS_SIZE,)
        for _ in range(200):
            mask = env.action_masks()
            legal = np.flatnonzero(mask)
            if legal.size == 0:
                break
            obs, reward, terminated, truncated, _ = env.step(int(legal[0]))
            if terminated or truncated:
                break


def test_render_without_the_extra_says_how_to_fix_it():
    # A bare ModuleNotFoundError three frames into the import graph tells the
    # user nothing. The error has to name the extra and the command.
    with _HideRich():
        env_mod = importlib.reload(importlib.import_module("minispire.env"))
        env = env_mod.MinispireEnv(render_mode="human")
        env.reset(seed=1)
        with pytest.raises(ModuleNotFoundError) as exc:
            env.render()
        message = str(exc.value)
        assert "tui" in message
        assert "pip install" in message


def test_require_tui_passes_when_rich_is_present():
    # The guard must be a no-op in a normal install — an over-eager check that
    # raised anyway would break every TUI user.
    from minispire.render import require_tui

    require_tui()


@pytest.fixture(autouse=True)
def _restore_env_module():
    # Each test reloads minispire.env under a patched importer; put the real one
    # back so later tests in the session see an unpatched module.
    yield
    importlib.reload(importlib.import_module("minispire.env"))
