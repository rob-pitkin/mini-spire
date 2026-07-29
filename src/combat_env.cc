#include "combat_env.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <stdexcept>
#include <utility>

#include "card.h"
#include "enemy.h"
#include "query.h"  // instance_card_damage (per-instance cards)
#include "status_effect.h"
#include "turn_loop.h"

namespace minispire {

namespace {

// CardId ordering for the obs pile-count layout. Must list every CardId and
// match kNumCardTypes (static_assert below).
constexpr std::array<CardId, kNumCardTypes> kObsCardOrder = {
    CardId::Strike,     CardId::Defend,     CardId::Bash,
    CardId::StrikePlus, CardId::DefendPlus, CardId::BashPlus,
    CardId::Slimed,     CardId::Dazed,
    // Ironclad Tier A (ROB-80).
    CardId::Cleave, CardId::CleavePlus, CardId::Clothesline,
    CardId::ClotheslinePlus, CardId::Flex, CardId::FlexPlus,
    CardId::IronWave, CardId::IronWavePlus, CardId::Thunderclap,
    CardId::ThunderclapPlus, CardId::TwinStrike, CardId::TwinStrikePlus,
    CardId::Carnage, CardId::CarnagePlus, CardId::Disarm,
    CardId::DisarmPlus, CardId::GhostlyArmor, CardId::GhostlyArmorPlus,
    CardId::Intimidate, CardId::IntimidatePlus, CardId::Pummel,
    CardId::PummelPlus, CardId::Shockwave, CardId::ShockwavePlus,
    CardId::Uppercut, CardId::UppercutPlus, CardId::Whirlwind,
    CardId::WhirlwindPlus, CardId::Bludgeon, CardId::BludgeonPlus,
    // Ironclad Tier B (ROB-80).
    CardId::PommelStrike, CardId::PommelStrikePlus, CardId::ShrugItOff,
    CardId::ShrugItOffPlus, CardId::Bloodletting, CardId::BloodlettingPlus,
    CardId::Hemokinesis, CardId::HemokinesisPlus, CardId::SeeingRed,
    CardId::SeeingRedPlus, CardId::Offering, CardId::OfferingPlus,
    // Ironclad Tier C (Stage 4a) — player powers.
    CardId::Inflame, CardId::InflamePlus, CardId::Impervious,
    CardId::ImperviousPlus, CardId::DemonForm, CardId::DemonFormPlus,
    CardId::Combust, CardId::CombustPlus, CardId::FeelNoPain,
    CardId::FeelNoPainPlus, CardId::DarkEmbrace, CardId::DarkEmbracePlus,
    CardId::Evolve, CardId::EvolvePlus, CardId::FireBreathing,
    CardId::FireBreathingPlus, CardId::Rupture, CardId::RupturePlus,
    CardId::Juggernaut, CardId::JuggernautPlus, CardId::Rage,
    CardId::RagePlus, CardId::FlameBarrier, CardId::FlameBarrierPlus,
    CardId::Brutality, CardId::BrutalityPlus, CardId::Berserk,
    CardId::BerserkPlus, CardId::Metallicize, CardId::MetallicizePlus,
    // Ironclad Tier D (Stage 4b) — the query/modifier layer.
    CardId::BodySlam, CardId::BodySlamPlus, CardId::Clash, CardId::ClashPlus,
    CardId::HeavyBlade, CardId::HeavyBladePlus, CardId::PerfectedStrike,
    CardId::PerfectedStrikePlus, CardId::BattleTrance,
    CardId::BattleTrancePlus, CardId::BloodForBlood, CardId::BloodForBloodPlus,
    CardId::Dropkick, CardId::DropkickPlus, CardId::Entrench,
    CardId::EntrenchPlus, CardId::SeverSoul, CardId::SeverSoulPlus,
    CardId::Barricade, CardId::BarricadePlus, CardId::Corruption,
    CardId::CorruptionPlus,
    // Ironclad Tier E (Stage 4c) — the choice cards.
    CardId::Armaments, CardId::ArmamentsPlus, CardId::Warcry,
    CardId::WarcryPlus, CardId::Headbutt, CardId::HeadbuttPlus,
    CardId::Exhume, CardId::ExhumePlus, CardId::DualWield,
    CardId::DualWieldPlus,
    // Per-instance cards: the pile counts here are per TYPE; a copy's
    // individual damage is carried in the choice block's payload instead.
    CardId::Rampage, CardId::RampagePlus, CardId::SearingBlow,
    CardId::SearingBlowPlus,
    // Player-generated status cards.
    CardId::Wound, CardId::Burn,
    // Cards that generate other cards.
    CardId::WildStrike, CardId::WildStrikePlus, CardId::PowerThrough,
    CardId::PowerThroughPlus, CardId::Immolate, CardId::ImmolatePlus,
    CardId::RecklessCharge, CardId::RecklessChargePlus, CardId::Anger,
    CardId::AngerPlus,
    // Simple new mechanisms.
    CardId::SwordBoomerang, CardId::SwordBoomerangPlus, CardId::LimitBreak,
    CardId::LimitBreakPlus, CardId::SpotWeakness, CardId::SpotWeaknessPlus,
    // Exhaust-driven cards.
    CardId::TrueGrit, CardId::TrueGritPlus, CardId::BurningPact,
    CardId::BurningPactPlus, CardId::SecondWind, CardId::SecondWindPlus,
    CardId::FiendFire, CardId::FiendFirePlus, CardId::Sentinel,
    CardId::SentinelPlus,
    // Life-total cards.
    CardId::Feed, CardId::FeedPlus, CardId::Reaper, CardId::ReaperPlus,
    // Meta-cards.
    CardId::DoubleTap, CardId::DoubleTapPlus, CardId::Havoc, CardId::HavocPlus,
    CardId::InfernalBlade, CardId::InfernalBladePlus,
};
static_assert(kObsCardOrder.size() == kNumCardTypes,
              "kObsCardOrder must list every card type");

// Per-entity status block = [debuffs then powers] (ROB-78). Each order array
// must list every real value of its enum (excluding the None sentinel) and match
// its count (static_asserts below).
constexpr std::array<Debuff, kNumDebuffs> kObsDebuffOrder = {
    Debuff::Vulnerable,
    Debuff::Weak,
    Debuff::Frail,
    Debuff::Entangle,
    Debuff::NoDraw,
};
static_assert(kObsDebuffOrder.size() == kNumDebuffs,
              "kObsDebuffOrder must list every debuff");
// Per-entity power orders (Stage 4a): the player block lists EVERY power, the
// enemy block only the enemy-relevant prefix — player powers would be 15
// always-zero floats in each of the 5 enemy slots. Both arrays must match the
// Power enum's declaration order (static_asserts below).
constexpr std::array<Power, kNumEnemyPowers> kObsEnemyPowerOrder = {
    Power::Strength,
    Power::Dexterity,
    Power::Ritual,
    Power::Metallicize,
    Power::Enrage,
    Power::Artifact,
};
static_assert(kObsEnemyPowerOrder.size() == kNumEnemyPowers,
              "kObsEnemyPowerOrder must list every enemy power");
constexpr std::array<Power, kNumPlayerPowers> kObsPlayerPowerOrder = {
    Power::Strength,     Power::Dexterity,     Power::Ritual,
    Power::Metallicize,  Power::Enrage,        Power::Artifact,
    Power::DemonForm,    Power::Combust,       Power::FeelNoPain,
    Power::DarkEmbrace,  Power::Evolve,        Power::FireBreathing,
    Power::Rupture,      Power::Juggernaut,    Power::Rage,
    Power::FlameBarrier, Power::Brutality,     Power::Berserk,
    Power::Corruption,   Power::Barricade,     Power::DoubleTap,
    Power::StrengthDown,
};
static_assert(kObsPlayerPowerOrder.size() == kNumPlayerPowers,
              "kObsPlayerPowerOrder must list every power");
// The enemy order must be a prefix of the player order (the Power enum's
// layout contract) so a stack means the same thing in both blocks.
static_assert(kObsEnemyPowerOrder[kNumEnemyPowers - 1] ==
                  kObsPlayerPowerOrder[kNumEnemyPowers - 1],
              "enemy power order must prefix the player power order");

// Which pile a choice draws from, as a small int for the obs header (Stage
// 4c). 0 = hand, 1 = draw, 2 = discard, 3 = exhaust, 4 = external (v2's card
// rewards / shop, which come from outside the deck).
int choice_source_pile(ChoiceKind kind) {
  switch (kind) {
    case ChoiceKind::UpgradeCardInHand:
    case ChoiceKind::HandToTopOfDraw:
    case ChoiceKind::CopyAttackOrPowerInHand:
    case ChoiceKind::ExhaustCardInHand:
      return 0;  // hand
    case ChoiceKind::DiscardToTopOfDraw:
      return 2;  // discard
    case ChoiceKind::ExhaustToHand:
      return 3;  // exhaust
    case ChoiceKind::None:
      break;
  }
  return 0;
}

template <typename Effect>
float status_stacks(const std::unordered_map<Effect, int>& effects, Effect e) {
  auto it = effects.find(e);
  return it == effects.end() ? 0.0f : static_cast<float>(it->second);
}

// Count cards of a given id in a pile.
int pile_count(const std::vector<Card>& pile, CardId id) {
  int n = 0;
  for (const Card& c : pile) {
    if (c.card_id == id) ++n;
  }
  return n;
}

}  // namespace

CombatEnv::CombatEnv(float hp_reward_coeff, EncounterPool pool,
                     std::vector<Card> deck)
    : mask_buffer_(kNumActions, 0),
      hp_reward_coeff_(hp_reward_coeff),
      pool_(pool),
      deck_(deck.empty() ? starter_deck() : std::move(deck)) {
  // Engine invariant: the action space is the combat block — (card x target)
  // plus one end-turn action (ROB-60) — followed by the Stage 4c option-slot
  // channel (one slot per possible option, plus decline).
  static_assert(kEndTurnAction == kNumCardTypes * kMaxEnemies,
                "end-turn must be the last index of the combat block");
  static_assert(kNumActions == kNumCardTypes * kMaxEnemies + 1 +
                                   kNumOptionSlots + 1,
                "kNumActions must be the combat block plus the slot channel");
  // Slots are sized so a pile choice can never overflow (a pile cannot hold
  // more distinct card types than exist) — truncation would be a parity bug.
  static_assert(kNumOptionSlots >= kNumCardTypes,
                "option slots must cover every distinct card type");
  // And kNumCardTypes must match the actual card database.
  assert(static_cast<int>(CARD_DATABASE.size()) == kNumCardTypes &&
         "kNumCardTypes out of sync with CARD_DATABASE");
  // A negative reward bonus is meaningless; catch it in debug builds. Other
  // values (including unusual ones) are trusted — it's a hyperparameter.
  assert(hp_reward_coeff_ >= 0.0f && "hp_reward_coeff must be >= 0");
}

CombatEnv::CombatEnv(CombatState state, float hp_reward_coeff)
    : state_(std::move(state)),
      mask_buffer_(kNumActions, 0),
      hp_reward_coeff_(hp_reward_coeff) {
  assert(hp_reward_coeff_ >= 0.0f && "hp_reward_coeff must be >= 0");
  // Make the buffers consistent with the injected state immediately.
  compute_obs();
  compute_mask();
}

void CombatEnv::reset(uint32_t seed) {
  // Copy the deck — start_combat consumes it, and reset() may be called again.
  state_ = start_combat(seed, pool_, deck_);
  reward_ = 0.0f;
  compute_obs();
  compute_mask();
}

void CombatEnv::step(int action) {
  if (action < 0 || action >= kNumActions) {
    throw std::invalid_argument("CombatEnv::step: action out of range");
  }
  if (!mask_buffer_[action]) {
    throw std::invalid_argument(
        "CombatEnv::step: action is masked off (illegal in current state)");
  }

  bool ok = apply_action(state_, action);
  if (!ok) {
    // Should be unreachable: the mask check above mirrors valid_actions.
    throw std::invalid_argument("CombatEnv::step: apply_action rejected action");
  }

  switch (state_.outcome) {
    case Outcome::Won: {
      // Sparse +1 plus an optional HP-retention bonus (ROB-52). Read the
      // character's (survivor's) post-step HP; float division.
      const Character& c = state_.character;
      float hp_frac = c.max_hp > 0
                          ? static_cast<float>(c.hp) / static_cast<float>(c.max_hp)
                          : 0.0f;
      reward_ = 1.0f + hp_reward_coeff_ * hp_frac;
      break;
    }
    case Outcome::Lost:       reward_ = -1.0f; break;
    case Outcome::InProgress: reward_ =  0.0f; break;
  }

  compute_obs();
  compute_mask();
}

void CombatEnv::compute_obs() {
  // Layout per ROB-40 + ROB-59 (multi-enemy). All values are raw — downstream
  // consumers can normalize. Section offsets are derived from constants so the
  // layout has no hand-maintained magic numbers (the off-by-one risk).
  std::array<float, kObsSize>& o = obs_buffer_;
  std::fill(o.begin(), o.end(), 0.0f);

  const Character& c = state_.character;

  // --- Player (slots 0..6) ---
  o[0] = static_cast<float>(c.hp);
  o[1] = static_cast<float>(c.max_hp);  // player keeps max_hp; enemies do not
  o[2] = static_cast<float>(c.current_block);
  o[3] = static_cast<float>(c.energy);
  o[4] = static_cast<float>(c.energy_per_turn);
  // Query-layer counters the player can read off the cards (ROB-40 B1): Blood
  // for Blood displays a cost reduced by hp_loss_events, and Combust's tooltip
  // displays the HP its casts will cost.
  o[5] = static_cast<float>(c.hp_loss_events);
  o[6] = static_cast<float>(c.combust_casts);

  // --- Player status = [debuffs then powers] ---
  constexpr int kStatusBase = CombatEnv::kPlayerBaseSize;
  for (std::size_t i = 0; i < kObsDebuffOrder.size(); ++i) {
    o[kStatusBase + i] = status_stacks(c.debuffs, kObsDebuffOrder[i]);
  }
  for (std::size_t i = 0; i < kObsPlayerPowerOrder.size(); ++i) {
    o[kStatusBase + kNumDebuffs + i] =
        status_stacks(c.powers, kObsPlayerPowerOrder[i]);
  }

  // --- Enemies: kMaxEnemies blocks of kEnemyObsStride floats each ---
  // Per block: [0] is_alive, [1] hp, [2] block, then status (kNumStatusEffects),
  // then intent (4: is_attacking, atk_dmg, is_blocking, is_buffing). Offsets are
  // derived from constants so adding a status can't drift the layout.
  constexpr int kEnemyBase = kPlayerObsSize;
  constexpr int kStatusOff = 3;
  constexpr int kIntentOff = kStatusOff + CombatEnv::kEnemyStatusSize;
  for (std::size_t slot = 0; slot < kMaxEnemies; ++slot) {
    const int base = kEnemyBase + static_cast<int>(slot) * kEnemyObsStride;
    if (slot >= state_.enemies.size()) continue;  // empty slot: leave zeros
    const Enemy& e = state_.enemies[slot];
    if (e.hp <= 0) continue;  // dead: leave zeros (is_alive stays 0)

    o[base + 0] = 1.0f;  // is_alive
    o[base + 1] = static_cast<float>(e.hp);
    o[base + 2] = static_cast<float>(e.current_block);
    // Status = [debuffs then powers].
    for (std::size_t i = 0; i < kObsDebuffOrder.size(); ++i) {
      o[base + kStatusOff + i] = status_stacks(e.debuffs, kObsDebuffOrder[i]);
    }
    for (std::size_t i = 0; i < kObsEnemyPowerOrder.size(); ++i) {
      o[base + kStatusOff + kNumDebuffs + i] =
          status_stacks(e.powers, kObsEnemyPowerOrder[i]);
    }
    // Intent. last_move is primed at combat start and each enemy turn, but
    // guard defensively (a freshly-spawned split child may not be primed yet).
    if (e.last_move.has_value()) {
      auto move_it = e.moves.find(*e.last_move);
      if (move_it != e.moves.end()) {
        const Move& m = move_it->second;
        const bool is_attacking = m.damage > 0;
        o[base + kIntentOff + 0] = is_attacking ? 1.0f : 0.0f;
        // Displayed attack damage (enemy Strength/Weak + player Vulnerable
        // factored in) — matches what the TUI shows.
        o[base + kIntentOff + 1] = is_attacking
                          ? static_cast<float>(compute_attack_damage(
                                m.damage, e.powers, e.debuffs, c.debuffs))
                          : 0.0f;
        o[base + kIntentOff + 2] = m.block > 0 ? 1.0f : 0.0f;
        o[base + kIntentOff + 3] =
            (!m.applies_debuffs.empty() || !m.applies_powers.empty()) ? 1.0f
                                                                      : 0.0f;
      }
    }
  }

  // --- Pile counts per CardId: hand/draw/discard/exhaust, each a
  // kNumCardTypes-long count vector. Stride derives from kNumCardTypes so
  // adding a card type can't drift the per-pile offsets. ---
  constexpr int kPileBase = kEnemyBase + kMaxEnemies * kEnemyObsStride;
  constexpr int kStride = kNumCardTypes;
  for (std::size_t i = 0; i < kObsCardOrder.size(); ++i) {
    CardId id = kObsCardOrder[i];
    o[kPileBase + 0 * kStride + i] = static_cast<float>(pile_count(state_.current_hand, id));
    o[kPileBase + 1 * kStride + i] = static_cast<float>(pile_count(state_.draw_pile, id));
    o[kPileBase + 2 * kStride + i] = static_cast<float>(pile_count(state_.discard_pile, id));
    o[kPileBase + 3 * kStride + i] = static_cast<float>(pile_count(state_.exhaust_pile, id));
    // Plane 5 is not a pile: it is the count of copies of this type that cost 0
    // for the rest of the turn (ROB-40 B3). Read from the map directly rather
    // than counted from a container.
    const auto free_it = state_.character.free_this_turn.find(id);
    o[kPileBase + 4 * kStride + i] =
        free_it == state_.character.free_this_turn.end()
            ? 0.0f
            : static_cast<float>(free_it->second);
  }

  // --- Turn number ---
  constexpr int kTurnOff = kPileBase + kPileObsSize;
  o[kTurnOff] = static_cast<float>(state_.turn_number);

  // --- Choice block (Stage 4c) ---
  // Header, then one descriptor per option slot. Written every step (zeroed by
  // the fill above when no choice pends), so the agent can always tell whether
  // a menu is open — the NLE failure mode this block exists to prevent.
  constexpr int kChoiceBase = kTurnOff + 1;
  const PendingChoice& pc = state_.pending_choice;
  if (pc.active()) {
    o[kChoiceBase + 0] = 1.0f;
    o[kChoiceBase + 1] = static_cast<float>(static_cast<int>(pc.kind));
    o[kChoiceBase + 2] = static_cast<float>(choice_source_pile(pc.kind));
    o[kChoiceBase + 3] = static_cast<float>(static_cast<int>(pc.source_card));
    o[kChoiceBase + 4] = pc.is_optional ? 1.0f : 0.0f;
    const int slots = kChoiceBase + kChoiceHeaderSize;
    for (int i = 0; i < pc.num_options; ++i) {
      const int base = slots + i * kChoiceSlotStride;
      o[base + 0] = 1.0f;  // occupied
      o[base + 1] =
          static_cast<float>(static_cast<int>(pc.options[i].card_id));
      // Payload value: for card options this is the INSTANCE damage, so two
      // Rampages at different bonuses are distinguishable in the obs and not
      // just in the mask. (v2's shop reuses this float for gold cost.)
      o[base + 2] =
          static_cast<float>(instance_card_damage(state_, pc.options[i]));
    }
  }
}

void CombatEnv::compute_mask() {
  std::vector<bool> v = valid_actions(state_);
  // Mirror size guarantee — valid_actions returns num_card_ids + 1.
  // mask_buffer_ was sized to kNumActions in the constructor.
  for (int i = 0; i < kNumActions; ++i) {
    mask_buffer_[i] = (i < static_cast<int>(v.size()) && v[i]) ? 1 : 0;
  }
}

StatePiles CombatEnv::state_piles() const {
  StatePiles out;
  out.hand.reserve(state_.current_hand.size());
  out.discard.reserve(state_.discard_pile.size());
  out.exhaust.reserve(state_.exhaust_pile.size());
  for (const Card& c : state_.current_hand) out.hand.push_back(c.card_id);
  for (const Card& c : state_.discard_pile) out.discard.push_back(c.card_id);
  for (const Card& c : state_.exhaust_pile) out.exhaust.push_back(c.card_id);
  // Draw pile: tally per CardId. Order is not exposed (see StatePiles doc).
  for (const Card& c : state_.draw_pile) ++out.draw[c.card_id];
  return out;
}

ChoiceView CombatEnv::choice_view() const {
  ChoiceView out;
  const PendingChoice& pc = state_.pending_choice;
  out.active = pc.active();
  if (!out.active) return out;
  out.kind = pc.kind;
  out.source_card = pc.source_card;
  out.is_optional = pc.is_optional;
  out.copies = pc.copies;
  out.options.reserve(pc.num_options);
  out.option_damage.reserve(pc.num_options);
  for (int i = 0; i < pc.num_options; ++i) {
    out.options.push_back(pc.options[i].card_id);
    out.option_damage.push_back(instance_card_damage(state_, pc.options[i]));
  }
  return out;
}

std::vector<int> CombatEnv::enemy_max_hps() const {
  std::vector<int> out;
  out.reserve(state_.enemies.size());
  for (const Enemy& e : state_.enemies) out.push_back(e.max_hp);
  return out;
}

std::vector<EnemyKind> CombatEnv::enemy_kinds() const {
  std::vector<EnemyKind> out;
  out.reserve(state_.enemies.size());
  for (const Enemy& e : state_.enemies) out.push_back(e.kind);
  return out;
}

}  // namespace minispire
