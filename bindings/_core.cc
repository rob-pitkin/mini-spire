// Pybind11 bindings for the minispire C++ engine.
//
// Exposes CombatEnv with zero-copy numpy views over the env's internal
// observation and action-mask buffers. The buffers are stable for the
// env's lifetime (per CombatEnv's design); the keep_alive parent on each
// array ensures Python won't garbage-collect the env while a view exists.

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "card.h"
#include "combat_env.h"
#include "combat_state.h"
#include "status_effect.h"

namespace py = pybind11;
using namespace minispire;

namespace {

// Zero-copy numpy view over the env's observation buffer.
py::array_t<float> obs_view(CombatEnv& self) {
  return py::array_t<float>(
      {CombatEnv::kObsSize},                                       // shape
      {static_cast<py::ssize_t>(sizeof(float))},                   // strides
      self.obs().data(),                                           // pointer
      py::cast(&self, py::return_value_policy::reference));        // owner
}

// Zero-copy numpy view over the env's action-mask buffer.
// uint8_t and bool have the same size and layout on supported platforms.
py::array_t<bool> mask_view(CombatEnv& self) {
  return py::array_t<bool>(
      {CombatEnv::kNumActions},
      {static_cast<py::ssize_t>(sizeof(uint8_t))},
      reinterpret_cast<const bool*>(self.action_mask().data()),
      py::cast(&self, py::return_value_policy::reference));
}

}  // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "Mini-Spire C++ engine bindings.";

  py::enum_<Outcome>(m, "Outcome")
      .value("InProgress", Outcome::InProgress)
      .value("Won", Outcome::Won)
      .value("Lost", Outcome::Lost);

  // CardId is exposed so Python code (the TUI in ROB-47) can identify cards
  // in StatePiles without needing to also expose CARD_DATABASE.
  py::enum_<CardId>(m, "CardId")
      .value("Strike", CardId::Strike)
      .value("Defend", CardId::Defend)
      .value("Bash", CardId::Bash)
      .value("StrikePlus", CardId::StrikePlus)
      .value("DefendPlus", CardId::DefendPlus)
      .value("BashPlus", CardId::BashPlus);

  py::class_<StatePiles>(m, "StatePiles")
      .def_readonly("hand", &StatePiles::hand)
      .def_readonly("draw", &StatePiles::draw)
      .def_readonly("discard", &StatePiles::discard)
      .def_readonly("exhaust", &StatePiles::exhaust);

  // Card data model (ROB-49) — lets the TUI render what each card does.
  py::enum_<StatusEffect>(m, "StatusEffect")
      .value("Vulnerable", StatusEffect::Vulnerable)
      .value("Weak", StatusEffect::Weak)
      .value("Strength", StatusEffect::Strength)
      .value("Dexterity", StatusEffect::Dexterity);

  py::class_<StatusApplication> status_application(m, "StatusApplication");
  py::enum_<StatusApplication::Target>(status_application, "Target")
      .value("Character", StatusApplication::Target::Character)
      .value("Enemy", StatusApplication::Target::Enemy);
  status_application
      .def_readonly("effect", &StatusApplication::effect)
      .def_readonly("amount", &StatusApplication::amount)
      .def_readonly("target", &StatusApplication::target);

  py::class_<CardData>(m, "CardData")
      .def_readonly("cost", &CardData::cost)
      .def_readonly("damage", &CardData::damage)
      .def_readonly("block", &CardData::block)
      .def_readonly("applies", &CardData::applies)
      .def_readonly("exhaust", &CardData::exhaust);

  // Look up the static card definition for a CardId. Backed by CARD_DATABASE.
  m.def(
      "card_data",
      [](CardId id) { return CARD_DATABASE.at(id); },
      py::arg("card_id"),
      "Return the CardData (cost / damage / block / applies / exhaust) for a "
      "CardId.");

  py::class_<CombatEnv>(m, "CombatEnv")
      .def(py::init<float>(), py::arg("hp_reward_coeff") = 0.0f)
      .def_readonly_static("OBS_SIZE", &CombatEnv::kObsSize)
      .def_readonly_static("NUM_ACTIONS", &CombatEnv::kNumActions)
      .def(
          "reset",
          [](CombatEnv& self, uint32_t seed) {
            self.reset(seed);
            return py::make_tuple(obs_view(self), py::dict());
          },
          py::arg("seed"),
          "Reset the env with the given seed. Returns (obs, info).")
      .def(
          "step",
          [](CombatEnv& self, int action) {
            self.step(action);
            py::dict info;
            info["outcome"] = self.outcome();
            info["turn_number"] = self.turn_number();
            return py::make_tuple(obs_view(self),
                                  self.reward(),
                                  self.terminated(),
                                  self.truncated(),
                                  info);
          },
          py::arg("action"),
          "Apply an action. Returns (obs, reward, terminated, truncated, info).")
      .def("action_mask", &mask_view,
           "Boolean mask of legal actions (length NUM_ACTIONS).")
      .def("state_piles", &CombatEnv::state_piles,
           "Read pile contents (hand/draw/discard/exhaust) as CardId lists. "
           "Allocates — not for use in the training loop.")
      .def("clone", &CombatEnv::clone,
           "Deep-copy clone of the env (for MCTS).")
      .def_property_readonly("outcome", &CombatEnv::outcome)
      .def_property_readonly("turn_number", &CombatEnv::turn_number)
      .def_property_readonly("reward", &CombatEnv::reward)
      .def_property_readonly("terminated", &CombatEnv::terminated)
      .def_property_readonly("truncated", &CombatEnv::truncated);
}
