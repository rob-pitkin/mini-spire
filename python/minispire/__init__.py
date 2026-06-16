"""Mini-Spire: a fast Slay the Spire combat engine for RL research.

The C++ engine is exposed via the private `_core` extension module. The
public API is re-exported from this package — import from `minispire` rather
than reaching into `_core` directly.
"""

import gymnasium as _gym

from minispire import _core  # noqa: F401  — verify the extension is loadable
from minispire.env import MinispireEnv

__version__ = "0.1.0"
__all__ = ["MinispireEnv"]

# Register with Gymnasium so `gym.make("Minispire-v0")` works after import.
# The -v0 suffix follows Gymnasium convention; future env revisions bump the
# version to preserve backward compatibility for trained models.
_gym.register(
    id="Minispire-v0",
    entry_point="minispire.env:MinispireEnv",
)
