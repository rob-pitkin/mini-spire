// Pybind11 entry point for the minispire._core extension module.
//
// ROB-39 deliberately ships an empty module so the build pipeline can be
// verified end-to-end (scikit-build-core → CMake → pybind11 → importable
// extension) before any real engine surface is exposed.
//
// Real bindings (CombatEnv, reset/step/clone, observation array) land in
// ROB-41.

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(_core, m) {
  m.doc() = "Mini-Spire C++ engine bindings (stub; real surface in ROB-41).";
}
