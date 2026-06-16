"""Mini-Spire: a fast Slay the Spire combat engine for RL research.

The C++ engine is exposed via the private `_core` extension module. The
public API is re-exported from this package — import from `minispire` rather
than reaching into `_core` directly.
"""

from minispire import _core  # noqa: F401  — verify the extension is loadable

__version__ = "0.1.0"
