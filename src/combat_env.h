#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "card.h"
#include "combat_state.h"
#include "encounter.h"
#include "turn_loop.h"  // action-layout constants (kTotalActions, ...)

namespace minispire {

// Pile contents accessor returned by CombatEnv::state_piles(). Used by the
// Python TUI (ROB-47) to render the pile-view. Not called per training step;
// allocation overhead is acceptable.
//
// Hand / discard / exhaust are ordered lists because the player sees them
// in order in real STS (hand left-to-right, discard top-most = most recently
// played, exhaust similarly).
//
// Draw is a **count map** because real STS shows the draw pile as a shuffled
// peek — the player can see *what* is in the draw pile but never the order.
// Exposing the engine's ordered draw_pile vector would leak the secret
// shuffle order to any consumer.
//
// The `*_costs` lists run parallel to hand / discard / exhaust: what THAT copy
// costs to play (instance_effective_cost). Copies of one CardId can differ —
// Snecko-rolled, Madness'd, made free this turn — and the player reads each
// card's own number, so a per-type cost would show the cheapest on all of them.
//
// `draw_costs` does the same for the draw pile without giving up its order:
// each CardId maps to its copies' costs sorted ascending. StS's draw-pile screen
// shows every copy with its own cost, sorted by type and name rather than by
// draw order (decompiled DrawPileViewScreen.open).
struct StatePiles {
  std::vector<CardId> hand;
  std::unordered_map<CardId, int> draw;
  std::vector<CardId> discard;
  std::vector<CardId> exhaust;
  std::vector<int> hand_costs;
  std::vector<int> discard_costs;
  std::vector<int> exhaust_costs;
  std::unordered_map<CardId, std::vector<int>> draw_costs;
};

// Pending-choice accessor for the TUI (Stage 4c). The obs already encodes this
// for the agent; the TUI wants it as named values rather than float offsets.
// `options[i]` is answered with encode_action(ActionBlock::CardSelect,
// options[i]) — entity-indexed (v2-spec.md §6.2), not a positional slot.
struct ChoiceView {
  bool active = false;
  ChoiceKind kind = ChoiceKind::None;
  // None when no card opened the menu: a relic did (Toolbox), or the view is
  // inactive. Mirrors PendingChoice::source_card, which this copies.
  CardId source_card = CardId::None;
  bool is_optional = false;
  int copies = 1;
  // The rest of what the menu tells a human (potion-effects.md §3.3): whether
  // the chosen card costs 0 this turn, how many more picks may be made, the
  // cards staged so far, and the pool a Discover menu rolled from — the TUI
  // names the card type from it ("Choose a Power card").
  bool arrives_free = false;
  int picks_left = 0;
  std::vector<CardId> staged;
  GenerationPool pool = GenerationPool::None;
  std::vector<CardId> options;
  // Per-option effective damage, parallel to `options`. Lets the TUI show two
  // Rampages apart ("Rampage (8)" vs "Rampage (13)"); 0 for non-attacks.
  std::vector<int> option_damage;
  // Per-option cost, parallel to `options`: what THAT copy costs to play — an
  // Exhume target, an Armaments pick — rather than the cheapest copy in hand.
  std::vector<int> option_costs;
};

// CombatEnv wraps CombatState + TurnLoop into a Gym-shaped env that owns its
// observation and action-mask buffers. The buffers are stable (never
// reallocated) so the Python binding can expose them as zero-copy numpy
// views without lifetime hazards.
//
// Lifecycle: default-construct, then call reset(seed) before any step().
class CombatEnv {
 public:
  // Observation layout (ROB-40 + ROB-59 multi-enemy). Flat float32 vector:
  //   player        [0:7]   hp, max_hp, block, energy, energy_per_turn,
  //                         hp_loss_events, combust_casts
  //   player status [7 : 7 + kPlayerStatusSize]   debuffs then ALL powers
  //   enemies       [.. : .. + kMaxEnemies*kEnemyObsStride]  kMaxEnemies blocks
  //   piles         [.. : .. + kNumPilePlanes*kNumCardTypes]
  //                         hand/draw/discard/exhaust, then the hand's copies
  //                         at each effective cost 0..kMaxCardCost
  //   turn          [last]
  // Each enemy block (kEnemyObsStride floats): is_alive, hp, block,
  // status(kEnemyStatusSize), intent(kEnemyIntentSize), kind(one-hot over
  // kNumEnemyKinds). NOTE: enemies intentionally omit max_hp (redundant for the
  // policy — current hp gives lethality, intent gives threat; ROB-59). The
  // player keeps max_hp (fixed run-level anchor + HP-shaping reward).
  //
  // Per-entity status widths (Stage 4a): both blocks are [debuffs then
  // powers], but only the player carries the player-only powers (Demon Form,
  // Juggernaut, ...) — in an enemy block those would be always-zero floats.
  static constexpr int kPlayerStatusSize = kNumDebuffs + kNumPlayerPowers;
  static constexpr int kEnemyStatusSize = kNumDebuffs + kNumEnemyPowers;
  // hp, max_hp, block, energy, energy_per_turn, then the query-layer counters
  // a human can read off the cards (ROB-40 B1): hp_loss_events (Blood for
  // Blood's displayed cost), combust_casts (Combust's tooltip) and
  // cards_played_this_turn (Normality's text, Velvet Choker's counter;
  // curse-effects.md §3.7), then gold, which the top bar shows and a Looter
  // takes mid-fight (thievery.md, Rob L4).
  static constexpr int kPlayerBaseSize = 9;
  // The two newest player-block slots, named so nothing finds them by counting
  // back from the end of the block (which moved when gold was added after the
  // cards-played count).
  static constexpr int kCardsPlayedSlot = 7;
  static constexpr int kGoldSlot = 8;
  static_assert(kGoldSlot == kPlayerBaseSize - 1,
                "gold is the player block's last base slot");
  static constexpr int kPlayerObsSize = kPlayerBaseSize + kPlayerStatusSize;
  // intent: is_attacking, atk_dmg, is_blocking, is_buffing, is_debuffing,
  // is_escaping, is_splitting. ROB-96 added the last three and split buff from
  // debuff. StS draws each of these as a distinct icon:
  //
  //  - escape / split carried no damage, block or status application, so they
  //    encoded as all-zero — bit-identical to an enemy doing nothing, when
  //    "it flees next turn" changes the correct play completely.
  //  - one flag used to cover BOTH "the enemy strengthens itself" and "the
  //    enemy weakens you". Red Louse's Grow and Green Louse's Spit Web are the
  //    moves that distinguish those two enemies, and they set the same bit.
  static constexpr int kEnemyIntentSize = 7;
  // Which enemy this is, one-hot. Categorical, not an index: an index would
  // tell the network Cultist sits "between" Jaw Worm and Red Louse, the same
  // false ordering observation-space.md §5.2 rejects.
  static constexpr int kEnemyKindSize = kNumEnemyKinds;
  static constexpr int kEnemyObsStride = 3 + kEnemyStatusSize +
                                         kEnemyIntentSize +
                                         kEnemyKindSize;
  // Per-card-type count vectors: the four piles (hand/draw/discard/exhaust),
  // then one plane per COST — "how many copies of type T in hand cost k right
  // now", k = 0..kMaxCardCost (relic-effects.md §6.28).
  //
  // A human reads each card's cost off the card, and under Snecko Eye every
  // copy can show a different one, so a single "free this turn" plane (ROB-40
  // B3) no longer says what is visible; Enlightenment's 1s never fit in it
  // either. X-cost and unplayable cards appear in no cost plane: their cost
  // never changes, so the card type already says it (observation-space.md §1).
  static constexpr int kNumCostPlanes = kMaxCardCost + 1;
  static constexpr int kNumPilePlanes = 4 + kNumCostPlanes;
  static constexpr int kPileObsSize = kNumPilePlanes * kNumCardTypes;

  // Choice block (Stage 4c; docs/design/decision-points.md §5.2). An 8-float
  // header, one 4-float descriptor per option slot, then the staged picks.
  //   header: pending, kind, source_pile, source_card, is_optional,
  //           copies, arrives_free, picks_left
  //   slot:   occupied, card id, instance damage, cost column
  //   staged: count of each card type staged in an open multi-select
  // The cost column is the one the option's CardSelect action names (v2-spec.md
  // §6.5). Without it a pile choice — Exhume, Headbutt — offers actions whose
  // costs the agent cannot see: the mask is not an observation.
  //
  // The last three header floats and the staged plane are what the menu shows
  // a human (potion-effects.md §3.3, S6/S7): "add 2 copies", "it costs 0 this
  // turn", "choose 2 cards", and the cards already selected. A potion's menu has
  // no source card, and Sacred Bark is not observed in combat, so nothing else
  // carries them. is_optional means "may stop now", so with picks_left it also
  // says how many picks are still required.
  static constexpr int kChoiceHeaderSize = 8;
  static constexpr int kChoiceSlotStride = 4;
  static constexpr int kChoiceStagedSize = kNumCardTypes;
  static constexpr int kChoiceObsSize = kChoiceHeaderSize +
                                        kNumOptionSlots * kChoiceSlotStride +
                                        kChoiceStagedSize;

  // Index of the turn-number float. Exposed because it is NOT kObsSize - 1:
  // the choice block sits after it. The TUI computed it that way and spent
  // every fight rendering a float from the choice channel — zero unless a
  // choice happened to be pending — as the turn counter. Same shape as the
  // kEndTurnAction bug: a landmark that used to be last, silently displaced by
  // a block appended after it.
  static constexpr int kTurnObsIndex =
      kPlayerObsSize + kMaxEnemies * kEnemyObsStride + kPileObsSize;

  // Potion block (docs/design/potion-effects.md §3.2), appended after the
  // choice block so every earlier index keeps its position: the count held of
  // each PotionId (v2-spec.md §5.6 — a count vector, not slots), then the
  // belt's slot capacity, since a human sees the empty slots.
  static constexpr int kPotionObsSize = kNumPotions + 1;
  static constexpr int kPotionObsIndex = kTurnObsIndex + 1 + kChoiceObsSize;

  static constexpr int kObsSize = kPotionObsIndex + kPotionObsSize;

  // Action space: the v2 layout (v2-spec.md §6). The layout constants and the
  // encode_action / decode_action pair live in turn_loop.h, so the env and the
  // engine cannot disagree about it.
  static constexpr int kNumActions = kTotalActions;

  // hp_reward_coeff is a per-env reward-shaping hyperparameter, fixed for the
  // env's lifetime. On a win the reward is 1 + coeff * (final_hp / max_hp);
  // coeff = 0 (default) is the pure sparse +1/-1/0 signal. See ROB-52.
  // Debug builds assert coeff >= 0 (a negative bonus is meaningless).
  //
  // `pool` selects which Act 1 encounter table reset() samples from; `deck` is
  // the player's deck (empty -> the Ironclad starter). Both fixed for the env's
  // lifetime (ROB-66).
  explicit CombatEnv(float hp_reward_coeff = 0.0f,
                     EncounterPool pool = EncounterPool::Weak,
                     std::vector<Card> deck = {});

  // Construct directly from an existing CombatState. Computes the obs/mask
  // buffers from the given state so they're immediately consistent. This is
  // the entry point for restoring a serialized state, wrapping a mid-fight
  // state for MCTS rollouts, and building deterministic test scenarios.
  explicit CombatEnv(CombatState state, float hp_reward_coeff = 0.0f);

  // Initialize from start_v1_combat(seed). Refreshes obs and mask buffers.
  void reset(uint32_t seed);

  // Apply the action. Throws std::invalid_argument if out of range or
  // masked-off. Refreshes obs and mask buffers. Sets reward_ based on the
  // resulting outcome (+1 Won or Escaped, -1 Lost, 0 InProgress).
  void step(int action);

  // Reward for the most recent step (0 immediately after reset).
  float reward() const { return reward_; }

  bool terminated() const { return state_.outcome != Outcome::InProgress; }

  // No time limit in v1. Future-proofing for benchmark-mode turn caps.
  bool truncated() const { return false; }

  // Zero-copy buffer accessors for the pybind11 layer.
  const std::array<float, kObsSize>& obs() const { return obs_buffer_; }
  const std::vector<uint8_t>& action_mask() const { return mask_buffer_; }

  // Deep-copy clone for MCTS. Compiler-generated copy is correct: CombatState
  // is value-typed (ROB-32), buffers are value-typed.
  CombatEnv clone() const { return *this; }

  // Read-only access to internals for inspection / rendering.
  const CombatState& state() const { return state_; }
  Outcome outcome() const { return state_.outcome; }
  int turn_number() const { return state_.turn_number; }

  // The info dict's `won` and `escaped` (potion-effects.md §1.4 V1). An escape
  // is scored as a win (Q1), so it is both; `escaped` tells the two apart.
  bool won() const {
    return state_.outcome == Outcome::Won || state_.outcome == Outcome::Escaped;
  }
  bool escaped() const { return state_.outcome == Outcome::Escaped; }

  // Pile contents — for the Python TUI pile-view. Not used during training.
  StatePiles state_piles() const;

  // The pending mid-card choice, if any (Stage 4c). For the TUI; not called
  // per training step.
  ChoiceView choice_view() const;

  // Per-enemy-slot max HP, in slot order. The observation intentionally omits
  // enemy max_hp (ROB-59), but the TUI needs it to draw enemy HP bars. Debug
  // accessor only — not used during training.
  std::vector<int> enemy_max_hps() const;

  // Per-enemy-slot kind, in slot order (ROB-79). The obs doesn't carry kind
  // (not a stat); the TUI needs it to show each enemy's real name. Debug only.
  std::vector<EnemyKind> enemy_kinds() const;

  // What `card` costs to play RIGHT NOW, which is not always CardData::cost:
  // Infernal Blade's grant makes a card free for the turn, and Blood for Blood
  // drops a point per HP loss this combat. Delegates to the query layer, so the
  // number a player reads is the number the engine will charge — the TUI used
  // to print the base cost and was simply wrong for both cases.
  int effective_cost(CardId card) const;

  // The action that plays the copy at hand position `hand_index` at enemy slot
  // `target`, and the action that picks option `option_index` of the pending
  // choice. Both name the copy by (CardId, cost column) — v2-spec.md §6.5 — and
  // exist so Python never derives a cost column itself. Neither checks
  // legality; the mask does. The TUI's, not the training loop's.
  int play_action(int hand_index, int target) const;
  int choice_action(int option_index) const;

 private:
  CombatState state_;
  // Fixed-size array: pointer stays stable for the env's lifetime, so the
  // numpy view from Python can rely on it.
  std::array<float, kObsSize> obs_buffer_{};
  // uint8_t instead of bool — std::vector<bool> is bit-packed and can't be
  // exposed via the buffer protocol without copies. Sized once in the ctor.
  std::vector<uint8_t> mask_buffer_;
  float reward_ = 0.0f;
  float hp_reward_coeff_ = 0.0f;
  // Encounter pool + deck used by reset() (ROB-66). Fixed for the env's
  // lifetime, like hp_reward_coeff_.
  EncounterPool pool_ = EncounterPool::Weak;
  std::vector<Card> deck_;

  void compute_obs();
  void compute_mask();
};

}  // namespace minispire
