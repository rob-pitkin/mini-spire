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

#include <algorithm>
#include <iterator>
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

// One copy's cost, re-derived from its fields rather than asked of the query
// layer, which is the whole point of this oracle: X-cost stays X; Corruption
// sits above every layer; then free_once, the turn cost, the combat cost, the
// printed cost (relic-effects.md §6.27).
int oracle_copy_cost(const CombatState& state, const Card& c) {
  const CardData& data = CARD_DATABASE.at(c.card_id);
  if (data.cost == kXCost) return kXCost;
  auto cp = state.character.powers.find(Power::Corruption);
  if (data.type == CardType::Skill && cp != state.character.powers.end() &&
      cp->second > 0) {
    return 0;
  }
  return c.free_once                       ? 0
         : c.turn_cost != kNoCostOverride   ? c.turn_cost
         : c.combat_cost != kNoCostOverride ? c.combat_cost
                                            : data.cost;
}

// The action's cost column for that copy (v2-spec.md §6.5): its cost, with
// X-cost and unplayable cards fixed at column 0. Restated from the spec, not
// shared with cost_column.
int oracle_column(const CombatState& state, const Card& c) {
  const CardData& data = CARD_DATABASE.at(c.card_id);
  if (data.cost == kXCost || data.unplayable) return 0;
  return oracle_copy_cost(state, c);
}

// The reference implementation. Deliberately independent — see the header note.
std::vector<bool> reference_valid_actions(const CombatState& state) {
  const int num_card_ids = static_cast<int>(CARD_DATABASE.size());
  std::vector<bool> mask(kTotalActions, false);
  if (state.outcome != Outcome::InProgress) return mask;

  // During a pending choice ONLY the offered cards are legal — at their
  // (CardId, cost column) in the card-selection block (§6.2, §6.5), not at
  // their rank — and the whole combat block is illegal. Raw arithmetic, like
  // the combat block below.
  if (state.pending_choice.active()) {
    for (int i = 0; i < state.pending_choice.num_options; ++i) {
      const Card& option = state.pending_choice.options[i];
      const int card = static_cast<int>(option.card_id);
      mask[kCardSelectBlock + card * kNumCostColumns +
           oracle_column(state, option)] = true;
    }
    // Decline once the minimum is met (potion-effects.md S3) — derived here
    // from the counts, not read off is_optional, which is the engine's own
    // derivation and the thing this checks.
    if (state.pending_choice.picks_made >= state.pending_choice.min_picks) {
      mask[kDeclineAction] = true;
    }
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
  // Layout (v2-spec.md §6.5): card-major, then cost column, then target.
  for (int action = 0; action < kEndTurnAction; ++action) {
    const int card_idx = action / (kNumCostColumns * kMaxEnemies);
    const int column = (action / kMaxEnemies) % kNumCostColumns;
    const int target = action % kMaxEnemies;
    if (card_idx < 0 || card_idx >= num_card_ids) continue;
    const CardId card = static_cast<CardId>(card_idx);
    const CardData& data = CARD_DATABASE.at(card);
    if (data.unplayable) continue;

    // A copy of this card AT THIS COLUMN must be in hand; its own cost decides
    // affordability. Every copy in one column costs the same, so the first
    // found answers.
    const Card* copy = nullptr;
    for (const Card& c : state.current_hand) {
      if (c.card_id == card && oracle_column(state, c) == column) {
        copy = &c;
        break;
      }
    }
    if (copy == nullptr) continue;
    const int cost = oracle_copy_cost(state, *copy);
    const bool affordable = cost == kXCost || state.character.energy >= cost;
    if (!affordable) continue;
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

    // Secret Technique / Secret Weapon: the DRAW PILE must hold a card of the
    // required type. StS greys the card out rather than letting it resolve to
    // nothing, so it is a legality rule and belongs in the mask.
    if (data.requires_draw_pile_type) {
      bool found = false;
      for (const Card& c : state.draw_pile) {
        if (CARD_DATABASE.at(c.card_id).type == data.required_draw_pile_type) {
          found = true;
          break;
        }
      }
      if (!found) continue;
    }

    if (card_targets_enemy(data)) {
      mask[action] = target < static_cast<int>(state.enemies.size()) &&
                     state.enemies[target].hp > 0;
    } else {
      mask[action] = (target == 0);
    }
  }

  // Potions (potion-effects.md §3.1), restated rather than asked of the
  // engine. Every held potion can be discarded. It can be drunk if its effect
  // is built — the list of steps 1 and 2, restated here on purpose and widened
  // as each step lands — at a living enemy if it targets one, else at slot 0.
  // Raw arithmetic: use is potion × target, discard one per potion.
  static const PotionId kBuilt[] = {
      PotionId::BloodPotion,     PotionId::BlockPotion,
      PotionId::DexterityPotion, PotionId::EnergyPotion,
      PotionId::ExplosivePotion, PotionId::FirePotion,
      PotionId::StrengthPotion,  PotionId::SwiftPotion,
      PotionId::WeakPotion,      PotionId::FearPotion,
      PotionId::FlexPotion,      PotionId::SpeedPotion,
      PotionId::BlessingOfTheForge, PotionId::RegenPotion,
      PotionId::AncientPotion,   PotionId::LiquidBronze,
      PotionId::EssenceOfSteel,  PotionId::HeartOfIron,
      PotionId::CultistPotion,   PotionId::FruitJuice,
      // Step 2: the choice potions.
      PotionId::AttackPotion,    PotionId::SkillPotion,
      PotionId::PowerPotion,     PotionId::ColorlessPotion,
      PotionId::ElixirPotion,    PotionId::GamblersBrew,
      PotionId::LiquidMemories};
  for (PotionId p : state.potions) {
    const int id = static_cast<int>(p);
    mask[kPotionDiscardBlock + id] = true;
    if (std::find(std::begin(kBuilt), std::end(kBuilt), p) == std::end(kBuilt)) {
      continue;
    }
    const bool targeted = p == PotionId::FirePotion ||
                          p == PotionId::WeakPotion || p == PotionId::FearPotion;
    for (int target = 0; target < kMaxEnemies; ++target) {
      const bool ok =
          targeted ? target < static_cast<int>(state.enemies.size()) &&
                         state.enemies[target].hp > 0
                   : target == 0;
      if (ok) mask[kPotionUseBlock + id * kMaxEnemies + target] = true;
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
  long long saw_choice = 0, saw_free = 0, saw_entangle = 0, saw_rung = 0,
            saw_potion = 0, saw_staged = 0, saw_required = 0;

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
        // Potions: any of the 33, built or not, so the oracle sees both the
        // drinkable and the discard-only kind. Capped near a real belt.
        if (rng() % 6 == 0 && s.potions.size() < 5) {
          s.potions.push_back(static_cast<PotionId>(rng() % kNumPotions));
          ++saw_potion;
        }
        // Query-layer state: Corruption, Barricade.
        if (rng() % 9 == 0) s.character.powers[Power::Corruption] = 1;
        if (rng() % 9 == 0) s.character.powers[Power::Barricade] = 1;
        // Per-instance cost layers (§6.27), each on ONE copy — which is what
        // the instance layers buy over the per-type counter they replaced: the
        // oracle and the engine must agree that a second copy still pays.
        // Driven directly rather than through plays, so every layer and the
        // precedence between them is exercised, not only what a random play
        // happens to resolve. Blood for Blood's reduction is a combat-layer
        // write, so the combat case covers it.
        if (!s.current_hand.empty()) {
          if (rng() % 10 == 0) {  // turn layer: Infernal Blade, Discovery
            Card& victim = s.current_hand[rng() % s.current_hand.size()];
            set_turn_cost(victim, 0);
            ++saw_free;
          }
          if (rng() % 10 == 0) {  // combat layer: Madness, Snecko, BfB
            Card& victim = s.current_hand[rng() % s.current_hand.size()];
            if (combat_layer_cost(victim) >= 0) {
              set_combat_cost(victim, static_cast<int>(rng() % 5));
            }
          }
          if (rng() % 15 == 0) {  // free_once: Forethought
            s.current_hand[rng() % s.current_hand.size()].free_once = true;
          }
        }
        // Choice mode, including the optional/decline variant and the
        // multi-select one, whose picks stage across the steps that follow.
        if (rng() % 8 == 0) {
          static const ChoiceKind kinds[] = {
              ChoiceKind::UpgradeCardInHand, ChoiceKind::HandToTopOfDraw,
              ChoiceKind::DiscardToTopOfDraw, ChoiceKind::ExhaustToHand,
              ChoiceKind::CopyAttackOrPowerInHand,
              ChoiceKind::ExhaustCardInHand, ChoiceKind::DiscardForDraw,
              ChoiceKind::DiscardToHand};
          s.pending_choice = build_choice(s, kinds[rng() % 8], CardId::Strike);
          PendingChoice& pc = s.pending_choice;
          switch (rng() % 3) {
            case 0:  // skippable
              pc.min_picks = 0;
              pc.is_optional = true;
              break;
            case 1:  // a multi-select, sometimes with picks it must make
              pc.max_picks = 2 + static_cast<int>(rng() % 3);
              pc.min_picks = static_cast<int>(rng() % 3);
              pc.is_optional = pc.min_picks == 0;
              break;
            default:  // a mandatory single pick, as built
              break;
          }
          if (pc.active()) ++saw_choice;
        }
        if (s.pending_choice.active() && s.pending_choice.picks_made > 0) {
          ++saw_staged;
          if (s.pending_choice.picks_made < s.pending_choice.min_picks) {
            ++saw_required;
          }
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
  EXPECT_GT(saw_potion, 100) << "potions barely exercised";
  EXPECT_GT(saw_staged, 100) << "multi-select staging barely exercised";
  EXPECT_GT(saw_required, 20) << "required picks barely exercised";
}
