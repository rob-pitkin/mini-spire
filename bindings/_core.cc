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

// Build the Gym info dict — shared by reset() and step() so the keys are
// uniform across both calls (ROB-58). Includes outcome/turn_number plus the
// metrics a training callback aggregates: won, final_hp, hp_fraction.
py::dict make_info(CombatEnv& self) {
  const Character& c = self.state().character;
  py::dict info;
  info["outcome"] = self.outcome();
  info["turn_number"] = self.turn_number();
  info["won"] = (self.outcome() == Outcome::Won);
  info["final_hp"] = c.hp;
  info["hp_fraction"] =
      c.max_hp > 0 ? static_cast<float>(c.hp) / static_cast<float>(c.max_hp)
                   : 0.0f;
  return info;
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
      .value("BashPlus", CardId::BashPlus)
      .value("Slimed", CardId::Slimed)
      .value("Dazed", CardId::Dazed);

  py::class_<StatePiles>(m, "StatePiles")
      .def_readonly("hand", &StatePiles::hand)
      .def_readonly("draw", &StatePiles::draw)
      .def_readonly("discard", &StatePiles::discard)
      .def_readonly("exhaust", &StatePiles::exhaust);

  // Card data model (ROB-49 -> ROB-78) — lets the TUI render what each card does.
  py::enum_<Debuff>(m, "Debuff")
      .value("Vulnerable", Debuff::Vulnerable)
      .value("Weak", Debuff::Weak)
      .value("Frail", Debuff::Frail)
      .value("Entangle", Debuff::Entangle);
  py::enum_<Power>(m, "Power")
      .value("Strength", Power::Strength)
      .value("Dexterity", Power::Dexterity)
      .value("Ritual", Power::Ritual)
      .value("Metallicize", Power::Metallicize)
      .value("Enrage", Power::Enrage)
      .value("Artifact", Power::Artifact);

  py::enum_<Target>(m, "Target")
      .value("Character", Target::Character)
      .value("Enemy", Target::Enemy);
  py::class_<DebuffApplication>(m, "DebuffApplication")
      .def_readonly("effect", &DebuffApplication::effect)
      .def_readonly("amount", &DebuffApplication::amount)
      .def_readonly("target", &DebuffApplication::target);
  py::class_<PowerApplication>(m, "PowerApplication")
      .def_readonly("effect", &PowerApplication::effect)
      .def_readonly("amount", &PowerApplication::amount)
      .def_readonly("target", &PowerApplication::target);

  py::class_<CardData>(m, "CardData")
      .def_readonly("cost", &CardData::cost)
      .def_readonly("damage", &CardData::damage)
      .def_readonly("block", &CardData::block)
      .def_readonly("applies_debuffs", &CardData::applies_debuffs)
      .def_readonly("applies_powers", &CardData::applies_powers)
      .def_readonly("exhaust", &CardData::exhaust);

  // Look up the static card definition for a CardId. Backed by CARD_DATABASE.
  m.def(
      "card_data",
      [](CardId id) { return CARD_DATABASE.at(id); },
      py::arg("card_id"),
      "Return the CardData (cost / damage / block / applies / exhaust) for a "
      "CardId.");

  // Whether a card targets an enemy (ROB-60). Exposed so the TUI knows which
  // cards need a target without reimplementing the predicate — single source of
  // truth with the engine's masking/decode.
  m.def(
      "card_targets_enemy",
      [](CardId id) { return card_targets_enemy(CARD_DATABASE.at(id)); },
      py::arg("card_id"),
      "True if the card acts on a chosen enemy (deals damage or applies an "
      "enemy status); False for self/untargeted cards like Defend.");

  py::class_<CombatEnv>(m, "CombatEnv")
      .def(py::init<float>(), py::arg("hp_reward_coeff") = 0.0f)
      .def_readonly_static("OBS_SIZE", &CombatEnv::kObsSize)
      .def_readonly_static("NUM_ACTIONS", &CombatEnv::kNumActions)
      // Max enemy slots — the action target stride (action = card*MAX_ENEMIES +
      // target). Exposed so the TUI encodes targets without hardcoding N.
      .def_readonly_static("MAX_ENEMIES", &kMaxEnemies)
      // Obs-layout constants so the TUI derives slot offsets instead of
      // hardcoding them (they drift as statuses/cards are added). Single source
      // of truth across the C++/Python boundary.
      .def_readonly_static("PLAYER_OBS_SIZE", &CombatEnv::kPlayerObsSize)
      .def_readonly_static("ENEMY_OBS_STRIDE", &CombatEnv::kEnemyObsStride)
      .def_readonly_static("NUM_STATUS_EFFECTS", &kNumStatusEffects)
      .def_readonly_static("NUM_DEBUFFS", &kNumDebuffs)
      .def_readonly_static("NUM_POWERS", &kNumPowers)
      .def_readonly_static("NUM_CARD_TYPES", &kNumCardTypes)
      .def(
          "reset",
          [](CombatEnv& self, uint32_t seed) {
            self.reset(seed);
            return py::make_tuple(obs_view(self), make_info(self));
          },
          py::arg("seed"),
          "Reset the env with the given seed. Returns (obs, info).")
      .def(
          "step",
          [](CombatEnv& self, int action) {
            self.step(action);
            return py::make_tuple(obs_view(self),
                                  self.reward(),
                                  self.terminated(),
                                  self.truncated(),
                                  make_info(self));
          },
          py::arg("action"),
          "Apply an action. Returns (obs, reward, terminated, truncated, info).")
      .def("action_mask", &mask_view,
           "Boolean mask of legal actions (length NUM_ACTIONS).")
      .def("state_piles", &CombatEnv::state_piles,
           "Read pile contents (hand/draw/discard/exhaust) as CardId lists. "
           "Allocates — not for use in the training loop.")
      .def("enemy_max_hps", &CombatEnv::enemy_max_hps,
           "Per-enemy-slot max HP, in slot order (the obs omits enemy max_hp). "
           "Debug accessor for the TUI; not for the training loop.")
      .def("clone", &CombatEnv::clone,
           "Deep-copy clone of the env (for MCTS).")
      .def_property_readonly("outcome", &CombatEnv::outcome)
      .def_property_readonly("turn_number", &CombatEnv::turn_number)
      .def_property_readonly("reward", &CombatEnv::reward)
      .def_property_readonly("terminated", &CombatEnv::terminated)
      .def_property_readonly("truncated", &CombatEnv::truncated);
}
