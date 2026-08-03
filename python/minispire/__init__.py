"""Mini-Spire: a fast Slay the Spire combat engine for RL research.

The C++ engine is exposed via the private `_core` extension module. The
public API is re-exported from this package — import from `minispire` rather
than reaching into `_core` directly.
"""

from importlib.metadata import PackageNotFoundError, version as _pkg_version

import gymnasium as _gym

from minispire import _core  # noqa: F401  — verify the extension is loadable
from minispire.env import MinispireEnv

# Read from installed metadata rather than hardcoded here, so pyproject.toml is
# the single source. It used to be written in both places, which is one place
# to forget at exactly the moment it matters — cutting a release.
try:
    __version__ = _pkg_version("minispire")
except PackageNotFoundError:  # pragma: no cover — running from a source tree
    __version__ = "0.0.0.dev0"

__all__ = ["MinispireEnv", "__version__"]

# Register with Gymnasium so `gym.make("Minispire-v0")` works after import.
# The -v0 suffix follows Gymnasium convention; future env revisions bump the
# version to preserve backward compatibility for trained models.
_gym.register(
    id="Minispire-v0",
    entry_point="minispire.env:MinispireEnv",
)
