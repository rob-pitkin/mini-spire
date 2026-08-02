"""Rendering helpers for the Mini-Spire human-play TUI.

Everything in this package needs the optional ``tui`` extra. The RL path —
``MinispireEnv``, training, evaluation — does not import it, which is the point:
someone installing Mini-Spire to train an agent should not also be installing a
terminal rendering stack they never call.
"""
from __future__ import annotations

_TUI_HINT = (
    "Mini-Spire's terminal UI needs the optional 'tui' extra, which is not "
    "installed.\n\n"
    "    pip install 'minispire[tui]'\n"
    "    uv pip install -e '.[tui]'      # from a checkout\n\n"
    "The RL environment itself does not require it — only the human-play TUI "
    "(minispire-play), the policy viewer (minispire.watch), and "
    "MinispireEnv.render()."
)


def require_tui() -> None:
    """Raise a legible error if the TUI dependencies are missing.

    Call this BEFORE importing rich, so the failure explains the fix instead of
    surfacing as a bare ModuleNotFoundError from somewhere in the import graph.
    """
    try:
        import rich  # noqa: F401
    except ModuleNotFoundError as exc:  # pragma: no cover - depends on install
        raise ModuleNotFoundError(_TUI_HINT) from exc
