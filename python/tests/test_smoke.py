"""Smoke tests that verify the build + import surface."""

import minispire


def test_package_imports():
    assert minispire.__version__ == "0.1.0"


def test_core_extension_loadable():
    from minispire import _core
    assert _core is not None
