// Differential verification of the action mask (ROB-89).
//
// This was a throwaway script in a scratch directory for most of the project's
// life, run by hand and quoted in commit messages as a verification gate. That
// made those claims unreproducible: the tool was not in the repository, could
// not run in CI, and survived only as long as a temp directory did. It is a
// real gate, so it lives here now and `ctest` enforces it.
//
// HOW IT WORKS, AND THE ONE RULE THAT MAKES IT WORTH ANYTHING
//
// reference_valid_actions() below re-derives the legality rules from scratch:
// what is in hand, what it costs after modifiers, whether Entangle forbids it,
// whether the target is alive, and what a pending choice allows. It is then
// compared against the engine's valid_actions() over many randomised states.
//
// It MUST NOT call into query.cc or share any helper with the implementation
// it checks. The instant it does, it stops being an independent oracle and
// becomes a restatement of the code — it would agree with a bug as readily as
// with correct behaviour. Duplication here is the point, not a smell.
//
// This has already earned its keep once: it proved a 39% optimisation of
// valid_actions() was behaviour-preserving across 15,317 masks, which no
// hand-written test could have established.
#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "action.h"
#include "card.h"
#include "combat_state.h"
#include "encounter.h"
#include "query.h"
#include "turn_loop.h"

using namespace minispire;

namespace {

// The reference implementation. Deliberately independent — see the header note.
std::vector<bool> reference_valid_actions(const CombatState& state) {
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  std::vector<bool> mask(kTotalActions, false);
  if (state.outcome != Outcome::InProgress) return mask;

  // During a pending choice ONLY the offered cards are legal — at their CardId
  // in the card-selection block (§6.2), not at their rank — and the whole
  // combat block is illegal.
  if (state.pending_choice.active()) {
    for (int i = 0; i < state.pending_choice.num_options; ++i) {
      const int card = static_cast<int>(state.pending_choice.options[i].card_id);
      mask[kCardSelectBlock + card] = true;
    }
    if (state.pending_choice.is_optional) mask[kDeclineAction] = true;
    return mask;
  }

  bool entangled = false;
  auto it = state.character.debuffs.find(Debuff::Entangle);
  if (it != state.character.debuffs.end() && it->second > 0) entangled = true;

  // Combat indices decoded with RAW arithmetic, deliberately. This oracle is
  // only worth anything if it is independent of the code under test, so it does
  // not call decode_action — which makes it the one INTENTIONAL cross-check of
  // the encoder's combat block, rather than a second copy of it. Everywhere else
  // in the codebase, offset arithmetic goes through encode/decode_action.
  for (int action = 0; action < kEndTurnAction; ++action) {
    const int card_idx = action / kMaxEnemies;
    const int target = action % kMaxEnemies;
    if (card_idx < 0 || card_idx >= num_card_ids) continue;
    const CardId card = static_cast<CardId>(card_idx);
    const CardData& data = CARD_DATABASE.at(card);
    if (data.unplayable) continue;

    bool in_hand = false;
    for (const Card& c : state.current_hand) {
      if (c.card_id == card) {
        in_hand = true;
        break;
      }
    }

    // Cost modifiers, recomputed here rather than asked of effective_cost.
    int cost = data.cost;
    if (cost != kXCost) {
      // A per-instance override (Infernal Blade, Discovery, Transmutation)
      // wins, and the CHEAPEST copy in hand is the one the engine plays
      // (colorless-effects.md D2). Re-derived from the hand rather than asked
      // of the query layer, which is the whole point of this oracle.
      int override_cost = -1;
      for (const Card& c : state.current_hand) {
        if (c.card_id != card || c.cost_override == kNoCostOverride) continue;
        if (override_cost < 0 || c.cost_override < override_cost) {
          override_cost = c.cost_override;
        }
      }
      bool corrupted = false;
      auto cp = state.character.powers.find(Power::Corruption);
      if (cp != state.character.powers.end() && cp->second > 0) corrupted = true;
      if (override_cost >= 0) {
        cost = override_cost;
      } else if (data.type == CardType::Skill && corrupted) {
        cost = 0;
      } else if (data.cost_drops_per_hp_loss) {
        cost = data.cost - state.character.hp_loss_events;
        if (cost < 0) cost = 0;
      }
    }
    const bool affordable = cost == kXCost || state.character.energy >= cost;
    if (!in_hand || !affordable) continue;
    if (entangled && data.type == CardType::Attack) continue;

    // Clash: every card in hand must be an Attack.
    if (data.attacks_only_in_hand) {
      bool all_attacks = true;
      for (const Card& c : state.current_hand) {
        if (CARD_DATABASE.at(c.card_id).type != CardType::Attack) {
          all_attacks = false;
          break;
        }
      }
      if (!all_attacks) continue;
    }

    if (card_targets_enemy(data)) {
      mask[action] = target < static_cast<int>(state.enemies.size()) &&
                     state.enemies[target].hp > 0;
    } else {
      mask[action] = (target == 0);
    }
  }
  mask[kEndTurnAction] = true;
  return mask;
}

}  // namespace

TEST(MaskOracle, EngineMaskMatchesAnIndependentDerivation) {
  std::mt19937 rng(999);
  long long checked = 0, mismatches = 0;
  // Coverage counters — a stress harness that never reaches a branch passes
  // vacuously, which is exactly how the free-cost branch went unexercised for
  // as long as it did.
  long long saw_choice = 0, saw_free = 0, saw_entangle = 0, saw_rung = 0;

  for (uint32_t seed = 0; seed < 300; ++seed) {
    for (EncounterPool pool : {EncounterPool::Weak, EncounterPool::Strong,
                               EncounterPool::Elite}) {
      CombatState s = start_combat(seed, pool, starter_deck());
      for (int step = 0; step < 60; ++step) {
        // Stress the edge cases: entangle, energy, dead enemies, odd cards.
        if (rng() % 7 == 0) {
          s.character.debuffs[Debuff::Entangle] = 1;
          ++saw_entangle;
        }
        if (rng() % 11 == 0) s.character.energy = static_cast<int>(rng() % 5);
        if (rng() % 13 == 0 && !s.enemies.empty()) {
          s.enemies[rng() % s.enemies.size()].hp = 0;
        }
        // Any card type, which since ROB-87 includes the rung ladders.
        if (rng() % 5 == 0) {
          const CardId id = static_cast<CardId>(rng() % CARD_DATABASE.size());
          s.current_hand.push_back(Card{id});
          if (CARD_DATABASE.at(id).bonus_damage_per_play > 0 ||
              searing_blow_rung(id) > 0) {
            ++saw_rung;
          }
        }
        // Query-layer state: Corruption, Blood for Blood, Barricade.
        if (rng() % 9 == 0) s.character.powers[Power::Corruption] = 1;
        if (rng() % 9 == 0) s.character.powers[Power::Barricade] = 1;
        if (rng() % 6 == 0) {
          s.character.hp_loss_events = static_cast<int>(rng() % 6);
        }
        // A per-instance discount (Infernal Blade, Discovery, Transmutation).
        // Previously only reachable if a random play happened to resolve
        // Infernal Blade, so the reference's free-cost branch was barely
        // exercised — now driven directly. Set on ONE copy, which is what the
        // instance override buys over the per-type counter it replaced: the
        // oracle and the engine must agree that a second copy still pays.
        if (rng() % 10 == 0 && !s.current_hand.empty()) {
          Card& victim = s.current_hand[rng() % s.current_hand.size()];
          victim.cost_override = 0;
          victim.cost_duration = CostDuration::ThisTurn;
          ++saw_free;
        }
        // Choice mode, including the optional/decline variant.
        if (rng() % 8 == 0) {
          static const ChoiceKind kinds[] = {
              ChoiceKind::UpgradeCardInHand, ChoiceKind::HandToTopOfDraw,
              ChoiceKind::DiscardToTopOfDraw, ChoiceKind::ExhaustToHand,
              ChoiceKind::CopyAttackOrPowerInHand,
              ChoiceKind::ExhaustCardInHand};
          s.pending_choice = build_choice(s, kinds[rng() % 6], CardId::Strike);
          if (rng() % 3 == 0) s.pending_choice.is_optional = true;
          if (s.pending_choice.active()) ++saw_choice;
        }

        const std::vector<bool> got = valid_actions(s);
        const std::vector<bool> want = reference_valid_actions(s);
        ++checked;
        if (got != want) {
          ++mismatches;
          if (mismatches <= 3) {  // report a few, not thousands
            for (std::size_t a = 0; a < got.size(); ++a) {
              EXPECT_EQ(got[a], want[a])
                  << "seed=" << seed << " step=" << step << " action=" << a;
            }
          }
        }

        std::vector<int> legal;
        for (std::size_t a = 0; a < got.size(); ++a) {
          if (got[a]) legal.push_back(static_cast<int>(a));
        }
        if (legal.empty()) break;
        apply_action(s, legal[rng() % legal.size()]);
        if (s.outcome != Outcome::InProgress) break;
      }
    }
  }

  EXPECT_EQ(mismatches, 0) << "engine mask disagrees with the oracle";
  EXPECT_GT(checked, 10000) << "the sweep collapsed — too few states visited";

  // Assert the stress actually reached each branch. Without these the test can
  // pass while checking almost nothing interesting.
  EXPECT_GT(saw_choice, 100) << "choice branch barely exercised";
  EXPECT_GT(saw_free, 100) << "free-this-turn cost branch barely exercised";
  EXPECT_GT(saw_entangle, 100) << "entangle branch barely exercised";
  EXPECT_GT(saw_rung, 50) << "rung ladder cards barely exercised";
}
