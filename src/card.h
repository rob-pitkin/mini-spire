#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include "status_effect.h"

namespace minispire {

// Action-space stability note: the integer values of CardId enumerators are
// the agent's action indices. Reordering or inserting new enumerators in the
// middle will invalidate any trained policy's action mapping. Append-only.
enum class CardId {
  Strike,
  Defend,
  Bash,
  StrikePlus,
  DefendPlus,
  BashPlus,
  // Status cards (added mid-fight by enemies; not part of any deck). Slimed:
  // 1-cost do-nothing that Exhausts on play (ROB-72).
  Slimed,
  // Dazed (ROB-65 Sentries): UNPLAYABLE + Ethereal.
  Dazed,
  // Ironclad card pool, Tier A (ROB-80) — pure-data cards (AoE / multi-hit /
  // X-cost). Appended (CardId values are action indices; append-only).
  // Generated from data/ironclad_cards.csv via analysis/gen_cards.py.
  Cleave,
  CleavePlus,
  Clothesline,
  ClotheslinePlus,
  Flex,
  FlexPlus,
  IronWave,
  IronWavePlus,
  Thunderclap,
  ThunderclapPlus,
  TwinStrike,
  TwinStrikePlus,
  Carnage,
  CarnagePlus,
  Disarm,
  DisarmPlus,
  GhostlyArmor,
  GhostlyArmorPlus,
  Intimidate,
  IntimidatePlus,
  Pummel,
  PummelPlus,
  Shockwave,
  ShockwavePlus,
  Uppercut,
  UppercutPlus,
  Whirlwind,
  WhirlwindPlus,
  Bludgeon,
  BludgeonPlus,
  // Tier B (ROB-80) — card-flow: draw / energy / lose-HP.
  PommelStrike,
  PommelStrikePlus,
  ShrugItOff,
  ShrugItOffPlus,
  Bloodletting,
  BloodlettingPlus,
  Hemokinesis,
  HemokinesisPlus,
  SeeingRed,
  SeeingRedPlus,
  Offering,
  OfferingPlus,
  // Tier C (Stage 4a) — player powers via the static registry
  // (fire_player_power_hooks) + pure-data rares.
  Inflame,
  InflamePlus,
  Impervious,
  ImperviousPlus,
  DemonForm,
  DemonFormPlus,
  Combust,  // scoped enums: no clash with Power::Combust
  CombustPlus,
  FeelNoPain,
  FeelNoPainPlus,
  DarkEmbrace,
  DarkEmbracePlus,
  Evolve,
  EvolvePlus,
  FireBreathing,
  FireBreathingPlus,
  Rupture,
  RupturePlus,
  Juggernaut,
  JuggernautPlus,
  Rage,
  RagePlus,
  FlameBarrier,
  FlameBarrierPlus,
  Brutality,
  BrutalityPlus,
  Berserk,
  BerserkPlus,
  Metallicize,
  MetallicizePlus,
  // Tier D (Stage 4b) — the query/modifier layer: cards whose cost, damage,
  // legality, or block rule is COMPUTED from state rather than stored.
  BodySlam,
  BodySlamPlus,
  Clash,
  ClashPlus,
  HeavyBlade,
  HeavyBladePlus,
  PerfectedStrike,
  PerfectedStrikePlus,
  BattleTrance,
  BattleTrancePlus,
  BloodForBlood,
  BloodForBloodPlus,
  Dropkick,
  DropkickPlus,
  Entrench,
  EntrenchPlus,
  SeverSoul,
  SeverSoulPlus,
  Barricade,
  BarricadePlus,
  Corruption,
  CorruptionPlus,
  // Tier E (Stage 4c) — the choice cards: each pauses resolution to let the
  // player pick a card from a pile.
  Armaments,
  ArmamentsPlus,
  Warcry,
  WarcryPlus,
  Headbutt,
  HeadbuttPlus,
  Exhume,
  ExhumePlus,
  DualWield,
  DualWieldPlus,
  // Per-instance state: these cards' damage depends on the individual copy,
  // not just its type (see struct Card).
  Rampage,
  RampagePlus,
  SearingBlow,
  SearingBlowPlus,
  // Status cards added by the player's own cards (Wild Strike, Power Through,
  // Reckless Charge, Immolate). Unplayable, like Slimed/Dazed.
  //
  // No Burn+ here: it exists in StS only through relic/curse upgrade paths we
  // don't model, and nothing in this engine can generate one (Immolate adds
  // the base Burn, and Status cards are not upgradable). An unreachable CardId
  // would still cost 5 action indices and 4 obs floats.
  Wound,
  Burn,
  // Cards that add a Status card, or a copy of themselves, to a pile.
  WildStrike,
  WildStrikePlus,
  PowerThrough,
  PowerThroughPlus,
  Immolate,
  ImmolatePlus,
  RecklessCharge,
  RecklessChargePlus,
  Anger,
  AngerPlus,
  // Simple new mechanisms: random per-hit targeting, a Strength multiplier,
  // and an intent-conditional buff.
  SwordBoomerang,
  SwordBoomerangPlus,
  LimitBreak,
  LimitBreakPlus,
  SpotWeakness,
  SpotWeaknessPlus,
};

// Number of distinct card types. Drives the obs pile-count stride and the
// action-space size (card x target). Update CARD_DATABASE + kObsCardOrder in
// lockstep — a static_assert in combat_env.cc enforces the count matches.
inline constexpr int kNumCardTypes = 134;

// A card's inherent StS type. This is a real property, NOT inferable from
// damage/block: an Attack can gain block (Body Slam) and a Skill can deal
// damage, so `damage > 0` is not a faithful proxy. Drives Entangle's attack
// mask (ROB-75) and the Gremlin Nob's Enrage (OnPlayerSkill, ROB-65).
enum class CardType {
  Attack,
  Skill,
  Power,
  Status,  // added mid-fight by enemies (Slimed, Dazed); not in any deck
  Curse,
};

// How a card is targeted (ROB-80). Replaces the old derived predicate: an AoE
// card "targets an enemy" but picks none, so targeting must be an explicit
// property.
enum class CardTarget {
  None,        // no target (Defend) — canonical action slot 0
  Enemy,       // pick one living enemy (Strike, Bash)
  AllEnemies,  // hits every living enemy, no pick (Cleave) — canonical slot 0
  Self,        // affects the player (Flex) — canonical slot 0
};

// X-cost sentinel (ROB-80): playing an X-cost card (Whirlwind) spends ALL
// current energy; X = the energy spent. Stored in CardData::cost.
inline constexpr int kXCost = -2;

// ---------------------------------------------------------------------------
// Mid-resolution player choices (Stage 4c; docs/design/decision-points.md).
//
// Every choice is "pick 1 of N from a labeled set". The engine builds the
// candidate list (applying each card's filter and the canonical ordering), the
// mask exposes it, and resolve_choice() consumes the answer. v2.0.0's
// non-combat decisions become new ChoiceKind values with no interface change.
// ---------------------------------------------------------------------------

enum class ChoiceKind {
  None,
  UpgradeCardInHand,        // Armaments: upgrade a card in hand
  HandToTopOfDraw,          // Warcry: put a hand card on top of the draw pile
  DiscardToTopOfDraw,       // Headbutt: discard pile -> top of draw
  ExhaustToHand,            // Exhume: exhaust pile -> hand
  CopyAttackOrPowerInHand,  // Dual Wield: copy an Attack/Power in hand
  // v2.0.0 (map / shop / events) appends here — no encoding change.
};

// Where a generated card lands. StS is specific per card, and the difference
// matters: a shuffled card can be drawn this combat, one added to the discard
// cannot until the pile reshuffles, and one added to hand clogs it now.
enum class GeneratedPile {
  Discard,       // Immolate's Burn, Anger's copy
  Hand,          // Power Through's Wounds
  ShuffleDraw,   // Wild Strike's Wound, Reckless Charge's Dazed
};

// How a card's base damage is computed (Stage 4b). Most cards just use
// CardData::damage; a few derive it from state, which is a QUERY (pulled at
// resolution), not a stored value. Resolved by base_card_damage in query.cc.
enum class DamageRule {
  Normal,           // use CardData::damage
  EqualToBlock,     // Body Slam: the player's current block
  PerStrikeInDeck,  // Perfected Strike: + amount per "Strike"-named card
  // Searing Blow: damage = n(n+7)/2 + 12 at n upgrades (wiki-verified against
  // the published progression 12/16/21/27/34/...). Read from the card
  // INSTANCE's upgrade count, not from CardData.
  SearingBlow,
};

// A card INSTANCE. Most cards are fully described by their CardId, but a few
// carry state that differs between two copies of the same card:
//
//   Rampage      — permanently gains +N damage each time it is played, so two
//                  Rampages in the same deck can have different damage.
//   Searing Blow — can be upgraded any number of times; `upgrades` is the count
//                  (there is no "Searing Blow++" CardId).
//
// Consequences, which the engine honours rather than papering over: two cards
// of the same type are interchangeable ONLY when their instance state matches
// (see build_choice's dedup), and the obs carries a per-card-type instance
// block so a buffed Rampage is visible.
//
// Kept a small POD so piles stay plain vectors and clone() stays a deep copy.
struct Card {
  CardId card_id;
  // Extra damage accumulated this combat (Rampage). Combat-scoped.
  int bonus_damage = 0;
  // Times this specific card has been upgraded (Searing Blow). Run-scoped in
  // v2; today it only changes if something upgrades the card mid-combat.
  int upgrades = 0;

  // Do these two instances play identically? Used to decide whether they
  // collapse into one option in a choice.
  bool same_as(const Card& other) const {
    return card_id == other.card_id && bonus_damage == other.bonus_damage &&
           upgrades == other.upgrades;
  }
  // Does this instance carry any state beyond its id?
  bool has_instance_state() const { return bonus_damage != 0 || upgrades != 0; }
};

struct CardData {
  const char* name;  // display name — single source of truth (ROB-79)
  int cost;
  int damage;
  int hits = 1;  // multi-hit: total damage = damage x hits (Strength per hit).
                 // -1 = X hits (Whirlwind: hits == energy spent).
  int block = 0;
  CardTarget target = CardTarget::Enemy;
  std::vector<DebuffApplication> applies_debuffs;
  std::vector<PowerApplication> applies_powers;
  CardType type = CardType::Attack;
  bool exhaust = false;     // exhausts when PLAYED (Slimed)
  bool ethereal = false;    // exhausts at end of turn if unplayed in hand (Dazed)
  bool unplayable = false;  // never a legal action (Dazed) — masked out
  // Card-flow effects (ROB-80 Tier B). Resolve with the card:
  int draw = 0;     // draw N cards (resolves last)
  int energy = 0;   // gain N energy
  int lose_hp = 0;  // player loses N HP — an EFFECT (not a cost, can kill you),
                    // direct HP loss that bypasses block (verified).
  // Innate (Stage 4a, Brutality+): starts in the opening hand, counting toward
  // the opening draw.
  bool innate = false;
  // --- Query/modifier hooks (Stage 4b). These are DECLARATIVE: the rule lives
  // in query.cc, the card only says which rule applies. See
  // docs/design/effects-architecture.md §4.5. ---
  DamageRule damage_rule = DamageRule::Normal;
  int damage_rule_amount = 0;  // per-Strike bonus (Perfected Strike)
  int strength_mult = 1;  // Heavy Blade counts Strength 3x / 5x
  bool cost_drops_per_hp_loss = false;   // Blood for Blood
  bool attacks_only_in_hand = false;     // Clash
  bool exhausts_non_attacks_in_hand = false;  // Sever Soul
  bool doubles_block = false;                 // Entrench
  bool no_draw_after = false;                 // Battle Trance
  bool bonus_if_target_vulnerable = false;    // Dropkick: +1 energy, +1 draw
  // --- Mid-card choices (Stage 4c). `requests_choice` names the choice this
  // card opens; the rest are its parameters, keeping the declarative pattern
  // (the card says WHAT, the executor says HOW). ---
  ChoiceKind requests_choice = ChoiceKind::None;
  // Armaments+ upgrades the WHOLE hand — a shape change, not a number, so it
  // skips the choice entirely rather than offering one.
  bool upgrades_whole_hand = false;
  // Dual Wield+ adds 2 copies rather than 1.
  int choice_copies = 1;
  // Rampage: playing this card permanently adds N damage to THAT COPY for the
  // rest of the combat ("each copy scales separately" — wiki).
  int bonus_damage_per_play = 0;
  // Burn: while this card sits in HAND at end of turn, the player takes N
  // damage (blockable, unlike a lose-HP effect). The card then discards
  // normally rather than exhausting.
  int end_of_turn_damage_in_hand = 0;
  // Cards that generate other cards. `generated_card` is what to make,
  // `generated_count` how many, and `generated_pile` where it lands — StS is
  // specific about this (Wild Strike SHUFFLES a Wound into the draw pile,
  // Power Through adds Wounds to HAND, Immolate adds a Burn to the DISCARD).
  CardId generated_card = CardId::Strike;
  int generated_count = 0;
  GeneratedPile generated_pile = GeneratedPile::Discard;
  // Anger: adds a copy of ITSELF (rather than a fixed card) to the discard.
  bool generates_self_copy = false;
  // Sword Boomerang: each hit picks its own random living enemy, rather than
  // all hits landing on one chosen target.
  bool hits_random_enemies = false;
  // Limit Break: multiply the player's Strength (2 = double).
  int strength_multiply = 0;
  // Spot Weakness: grant this much Strength only if the target's queued intent
  // is an attack.
  int strength_if_target_attacking = 0;
};

// What a card becomes when upgraded (Armaments; v2's rest-site smith).
// Sourced from data/ironclad_cards.csv's `upgrade_of` column, inverted.
//
// Kept as a separate table rather than a CardData field so the 102 existing
// CARD_DATABASE rows don't all need editing — and because "what I upgrade to"
// is a relation between two cards, not a property of one. Absent = cannot be
// upgraded: already-upgraded cards, and the Status cards Slimed/Dazed (which
// StS correctly forbids upgrading).
inline const std::unordered_map<CardId, CardId> CARD_UPGRADES = {
    {CardId::Strike, CardId::StrikePlus},
    {CardId::Defend, CardId::DefendPlus},
    {CardId::Bash, CardId::BashPlus},
    // Tier A
    {CardId::Cleave, CardId::CleavePlus},
    {CardId::Clothesline, CardId::ClotheslinePlus},
    {CardId::Flex, CardId::FlexPlus},
    {CardId::IronWave, CardId::IronWavePlus},
    {CardId::Thunderclap, CardId::ThunderclapPlus},
    {CardId::TwinStrike, CardId::TwinStrikePlus},
    {CardId::Carnage, CardId::CarnagePlus},
    {CardId::Disarm, CardId::DisarmPlus},
    {CardId::GhostlyArmor, CardId::GhostlyArmorPlus},
    {CardId::Intimidate, CardId::IntimidatePlus},
    {CardId::Pummel, CardId::PummelPlus},
    {CardId::Shockwave, CardId::ShockwavePlus},
    {CardId::Uppercut, CardId::UppercutPlus},
    {CardId::Whirlwind, CardId::WhirlwindPlus},
    {CardId::Bludgeon, CardId::BludgeonPlus},
    // Tier B
    {CardId::PommelStrike, CardId::PommelStrikePlus},
    {CardId::ShrugItOff, CardId::ShrugItOffPlus},
    {CardId::Bloodletting, CardId::BloodlettingPlus},
    {CardId::Hemokinesis, CardId::HemokinesisPlus},
    {CardId::SeeingRed, CardId::SeeingRedPlus},
    {CardId::Offering, CardId::OfferingPlus},
    // Tier C
    {CardId::Inflame, CardId::InflamePlus},
    {CardId::Impervious, CardId::ImperviousPlus},
    {CardId::DemonForm, CardId::DemonFormPlus},
    {CardId::Combust, CardId::CombustPlus},
    {CardId::FeelNoPain, CardId::FeelNoPainPlus},
    {CardId::DarkEmbrace, CardId::DarkEmbracePlus},
    {CardId::Evolve, CardId::EvolvePlus},
    {CardId::FireBreathing, CardId::FireBreathingPlus},
    {CardId::Rupture, CardId::RupturePlus},
    {CardId::Juggernaut, CardId::JuggernautPlus},
    {CardId::Rage, CardId::RagePlus},
    {CardId::FlameBarrier, CardId::FlameBarrierPlus},
    {CardId::Brutality, CardId::BrutalityPlus},
    {CardId::Berserk, CardId::BerserkPlus},
    {CardId::Metallicize, CardId::MetallicizePlus},
    // Tier D
    {CardId::BodySlam, CardId::BodySlamPlus},
    {CardId::Clash, CardId::ClashPlus},
    {CardId::HeavyBlade, CardId::HeavyBladePlus},
    {CardId::PerfectedStrike, CardId::PerfectedStrikePlus},
    {CardId::BattleTrance, CardId::BattleTrancePlus},
    {CardId::BloodForBlood, CardId::BloodForBloodPlus},
    {CardId::Dropkick, CardId::DropkickPlus},
    {CardId::Entrench, CardId::EntrenchPlus},
    {CardId::SeverSoul, CardId::SeverSoulPlus},
    {CardId::Barricade, CardId::BarricadePlus},
    {CardId::Corruption, CardId::CorruptionPlus},
    // Tier E
    {CardId::Armaments, CardId::ArmamentsPlus},
    {CardId::Warcry, CardId::WarcryPlus},
    {CardId::Headbutt, CardId::HeadbuttPlus},
    {CardId::Exhume, CardId::ExhumePlus},
    {CardId::DualWield, CardId::DualWieldPlus},
    {CardId::Rampage, CardId::RampagePlus},
    {CardId::WildStrike, CardId::WildStrikePlus},
    {CardId::PowerThrough, CardId::PowerThroughPlus},
    {CardId::Immolate, CardId::ImmolatePlus},
    {CardId::RecklessCharge, CardId::RecklessChargePlus},
    {CardId::Anger, CardId::AngerPlus},
    {CardId::SwordBoomerang, CardId::SwordBoomerangPlus},
    {CardId::LimitBreak, CardId::LimitBreakPlus},
    {CardId::SpotWeakness, CardId::SpotWeaknessPlus},
    // NOTE: Searing Blow is deliberately absent — it upgrades by incrementing
    // the INSTANCE's counter, not by swapping CardId (see upgrade_card_in_place
    // and is_instance_upgradable). There is no "Searing Blow++" id to map to.
};

// Searing Blow can be upgraded without limit, so its upgrades live on the card
// instance rather than in CARD_UPGRADES. Anything that upgrades a card must
// consult this first.
inline bool is_instance_upgradable(CardId id) {
  return id == CardId::SearingBlow || id == CardId::SearingBlowPlus;
}

// Upgrade a card IN PLACE, handling both models: normally the id is swapped
// for its "+" form; for Searing Blow the instance's upgrade counter grows.
// Returns false if the card cannot be upgraded at all.
inline bool upgrade_card_in_place(Card& card) {
  if (is_instance_upgradable(card.card_id)) {
    ++card.upgrades;
    return true;
  }
  auto it = CARD_UPGRADES.find(card.card_id);
  if (it == CARD_UPGRADES.end()) return false;
  card.card_id = it->second;
  return true;
}

// Can this card be upgraded? False for already-upgraded cards and for Status
// cards (Slimed, Dazed). Armaments' candidate filter reads this.
inline bool is_upgradable(CardId id) {
  // Searing Blow is upgradable without limit via its instance counter, so it
  // qualifies even though it has no entry in CARD_UPGRADES.
  return CARD_UPGRADES.count(id) > 0 || is_instance_upgradable(id);
}

// Upper bound on simultaneous options. Set to kNumCardTypes so a pile choice
// can NEVER overflow: a pile cannot hold more distinct card types than exist.
// This makes truncation — which would be a parity violation, since a human can
// pick any card — structurally impossible rather than merely unlikely.
inline constexpr int kNumOptionSlots = kNumCardTypes;

// The suspended-choice record. POD with a fixed array (no heap) so
// CombatState::clone() stays a plain copy and MCTS can branch on a paused
// state. Options are DEDUPLICATED distinct card types in ascending CardId
// order — the canonical ordering is part of the public interface, since slot
// indices are actions.
struct PendingChoice {
  ChoiceKind kind = ChoiceKind::None;
  CardId source_card = CardId::Strike;  // the card that caused the pause
  bool is_optional = false;             // may the agent decline?
  int copies = 1;                       // Dual Wield+ adds 2
  int num_options = 0;
  // Card INSTANCES, not just ids: two Rampages at different bonuses are
  // genuinely different choices, so they occupy separate slots (only truly
  // identical copies collapse). Still POD, so clone() stays a plain copy.
  std::array<Card, kNumOptionSlots> options{};

  bool active() const { return kind != ChoiceKind::None; }
};

// The upgraded form of `id`, or `id` itself if it cannot be upgraded. Total —
// never throws, so callers that upgrade a whole pile need no per-card guard.
inline CardId upgraded_card(CardId id) {
  auto it = CARD_UPGRADES.find(id);
  return it == CARD_UPGRADES.end() ? id : it->second;
}

// CardData row order: name, cost, damage, hits, block, target, debuffs, powers,
// type, exhaust, ethereal, unplayable.
inline const std::unordered_map<CardId, CardData> CARD_DATABASE = {
    // Starter deck.
    {CardId::Strike,     {"Strike",  1, 6, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack}},
    {CardId::StrikePlus, {"Strike+", 1, 9, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack}},
    {CardId::Defend,     {"Defend",  1, 0, 1, 5, CardTarget::None,  {}, {}, CardType::Skill}},
    {CardId::DefendPlus, {"Defend+", 1, 0, 1, 8, CardTarget::None,  {}, {}, CardType::Skill}},
    {CardId::Bash,       {"Bash",    2, 8, 1, 0, CardTarget::Enemy, {{Debuff::Vulnerable, 2, Target::Enemy}}, {}, CardType::Attack}},
    {CardId::BashPlus,   {"Bash+",   2, 10, 1, 0, CardTarget::Enemy, {{Debuff::Vulnerable, 3, Target::Enemy}}, {}, CardType::Attack}},
    // Status cards (enemy-added, not in the CSV). Slimed: exhausts on play.
    {CardId::Slimed,     {"Slimed",  1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Status, /*exhaust=*/true}},
    // Dazed: unplayable + ethereal.
    {CardId::Dazed,      {"Dazed",   0, 0, 1, 0, CardTarget::None, {}, {}, CardType::Status, /*exhaust=*/false, /*ethereal=*/true, /*unplayable=*/true}},
    // --- Ironclad Tier A (ROB-80), generated from data/ironclad_cards.csv ---
    {CardId::Cleave, {"Cleave", 1, 8, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::CleavePlus, {"Cleave+", 1, 11, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::Clothesline, {"Clothesline", 2, 12, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 2, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::ClotheslinePlus, {"Clothesline+", 2, 14, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 3, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::Flex, {"Flex", 0, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 2, Target::Character}}, CardType::Skill, false, false}},
    {CardId::FlexPlus, {"Flex+", 0, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 4, Target::Character}}, CardType::Skill, false, false}},
    {CardId::IronWave, {"Iron Wave", 1, 5, 1, 5, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::IronWavePlus, {"Iron Wave+", 1, 7, 1, 7, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::Thunderclap, {"Thunderclap", 1, 4, 1, 0, CardTarget::AllEnemies, {{Debuff::Vulnerable, 1, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::ThunderclapPlus, {"Thunderclap+", 1, 7, 1, 0, CardTarget::AllEnemies, {{Debuff::Vulnerable, 1, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::TwinStrike, {"Twin Strike", 1, 5, 2, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::TwinStrikePlus, {"Twin Strike+", 1, 7, 2, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::Carnage, {"Carnage", 2, 20, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, true}},
    {CardId::CarnagePlus, {"Carnage+", 2, 28, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, true}},
    {CardId::Disarm, {"Disarm", 1, 0, 1, 0, CardTarget::Enemy, {}, {{Power::Strength, -2, Target::Enemy}}, CardType::Skill, true, false}},
    {CardId::DisarmPlus, {"Disarm+", 1, 0, 1, 0, CardTarget::Enemy, {}, {{Power::Strength, -3, Target::Enemy}}, CardType::Skill, true, false}},
    {CardId::GhostlyArmor, {"Ghostly Armor", 1, 0, 0, 10, CardTarget::None, {}, {}, CardType::Skill, false, true}},
    {CardId::GhostlyArmorPlus, {"Ghostly Armor+", 1, 0, 0, 13, CardTarget::None, {}, {}, CardType::Skill, false, true}},
    {CardId::Intimidate, {"Intimidate", 0, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 1, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::IntimidatePlus, {"Intimidate+", 0, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 2, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::Pummel, {"Pummel", 1, 2, 4, 0, CardTarget::Enemy, {}, {}, CardType::Attack, true, false}},
    {CardId::PummelPlus, {"Pummel+", 1, 2, 5, 0, CardTarget::Enemy, {}, {}, CardType::Attack, true, false}},
    {CardId::Shockwave, {"Shockwave", 2, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 3, Target::Enemy}, {Debuff::Vulnerable, 3, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::ShockwavePlus, {"Shockwave+", 2, 0, 0, 0, CardTarget::AllEnemies, {{Debuff::Weak, 5, Target::Enemy}, {Debuff::Vulnerable, 5, Target::Enemy}}, {}, CardType::Skill, true, false}},
    {CardId::Uppercut, {"Uppercut", 2, 13, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 1, Target::Enemy}, {Debuff::Vulnerable, 1, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::UppercutPlus, {"Uppercut+", 2, 13, 1, 0, CardTarget::Enemy, {{Debuff::Weak, 2, Target::Enemy}, {Debuff::Vulnerable, 2, Target::Enemy}}, {}, CardType::Attack, false, false}},
    {CardId::Whirlwind, {"Whirlwind", kXCost, 5, -1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::WhirlwindPlus, {"Whirlwind+", kXCost, 8, -1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false}},
    {CardId::Bludgeon, {"Bludgeon", 3, 32, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    {CardId::BludgeonPlus, {"Bludgeon+", 3, 42, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false}},
    // --- Ironclad Tier B (ROB-80): card-flow (draw / energy / lose-HP) ---
    {CardId::PommelStrike, {"Pommel Strike", 1, 9, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 1, 0, 0}},
    {CardId::PommelStrikePlus, {"Pommel Strike+", 1, 10, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 2, 0, 0}},
    {CardId::ShrugItOff, {"Shrug It Off", 1, 0, 0, 8, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 1, 0, 0}},
    {CardId::ShrugItOffPlus, {"Shrug It Off+", 1, 0, 0, 11, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 1, 0, 0}},
    {CardId::Bloodletting, {"Bloodletting", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 2, 3}},
    {CardId::BloodlettingPlus, {"Bloodletting+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 3, 3}},
    {CardId::Hemokinesis, {"Hemokinesis", 1, 15, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 2}},
    {CardId::HemokinesisPlus, {"Hemokinesis+", 1, 20, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 2}},
    {CardId::SeeingRed, {"Seeing Red", 1, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 0, 2, 0}},
    {CardId::SeeingRedPlus, {"Seeing Red+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 0, 2, 0}},
    {CardId::Offering, {"Offering", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 3, 2, 6}},
    {CardId::OfferingPlus, {"Offering+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, true, false, false, 5, 2, 6}},
    // --- Ironclad Tier C (Stage 4a): player powers. Each Power card applies a
    // Power to the player; the BEHAVIOR lives in the static registry
    // (fire_player_power_hooks in action.cc), keyed by the Power enum. A played
    // Power card vanishes — it enters no pile (see handle_play_card).
    {CardId::Inflame, {"Inflame", 1, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 2, Target::Character}}, CardType::Power, false, false}},
    {CardId::InflamePlus, {"Inflame+", 1, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 3, Target::Character}}, CardType::Power, false, false}},
    // Impervious is a SKILL (exhausts to the exhaust pile), not a Power.
    {CardId::Impervious, {"Impervious", 2, 0, 0, 30, CardTarget::None, {}, {}, CardType::Skill, true, false}},
    {CardId::ImperviousPlus, {"Impervious+", 2, 0, 0, 40, CardTarget::None, {}, {}, CardType::Skill, true, false}},
    {CardId::DemonForm, {"Demon Form", 3, 0, 0, 0, CardTarget::None, {}, {{Power::DemonForm, 2, Target::Character}}, CardType::Power, false, false}},
    {CardId::DemonFormPlus, {"Demon Form+", 3, 0, 0, 0, CardTarget::None, {}, {{Power::DemonForm, 3, Target::Character}}, CardType::Power, false, false}},
    // Combust: stacks = accumulated damage; the per-cast HP loss is counted
    // separately in Character::combust_casts (mixed upgrades need both).
    {CardId::Combust, {"Combust", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Combust, 5, Target::Character}}, CardType::Power, false, false}},
    {CardId::CombustPlus, {"Combust+", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Combust, 7, Target::Character}}, CardType::Power, false, false}},
    {CardId::FeelNoPain, {"Feel No Pain", 1, 0, 0, 0, CardTarget::None, {}, {{Power::FeelNoPain, 3, Target::Character}}, CardType::Power, false, false}},
    {CardId::FeelNoPainPlus, {"Feel No Pain+", 1, 0, 0, 0, CardTarget::None, {}, {{Power::FeelNoPain, 4, Target::Character}}, CardType::Power, false, false}},
    {CardId::DarkEmbrace, {"Dark Embrace", 2, 0, 0, 0, CardTarget::None, {}, {{Power::DarkEmbrace, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::DarkEmbracePlus, {"Dark Embrace+", 1, 0, 0, 0, CardTarget::None, {}, {{Power::DarkEmbrace, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::Evolve, {"Evolve", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Evolve, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::EvolvePlus, {"Evolve+", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Evolve, 2, Target::Character}}, CardType::Power, false, false}},
    {CardId::FireBreathing, {"Fire Breathing", 1, 0, 0, 0, CardTarget::None, {}, {{Power::FireBreathing, 6, Target::Character}}, CardType::Power, false, false}},
    {CardId::FireBreathingPlus, {"Fire Breathing+", 1, 0, 0, 0, CardTarget::None, {}, {{Power::FireBreathing, 10, Target::Character}}, CardType::Power, false, false}},
    {CardId::Rupture, {"Rupture", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Rupture, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::RupturePlus, {"Rupture+", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Rupture, 2, Target::Character}}, CardType::Power, false, false}},
    {CardId::Juggernaut, {"Juggernaut", 2, 0, 0, 0, CardTarget::None, {}, {{Power::Juggernaut, 5, Target::Character}}, CardType::Power, false, false}},
    {CardId::JuggernautPlus, {"Juggernaut+", 2, 0, 0, 0, CardTarget::None, {}, {{Power::Juggernaut, 7, Target::Character}}, CardType::Power, false, false}},
    // Rage and Flame Barrier are turn-scoped SKILLS whose effect is modeled as
    // a Power (StS shows them as power icons); the registry removes them at the
    // matching turn boundary.
    {CardId::Rage, {"Rage", 0, 0, 0, 0, CardTarget::None, {}, {{Power::Rage, 3, Target::Character}}, CardType::Skill, false, false}},
    {CardId::RagePlus, {"Rage+", 0, 0, 0, 0, CardTarget::None, {}, {{Power::Rage, 5, Target::Character}}, CardType::Skill, false, false}},
    {CardId::FlameBarrier, {"Flame Barrier", 2, 0, 0, 12, CardTarget::None, {}, {{Power::FlameBarrier, 4, Target::Character}}, CardType::Skill, false, false}},
    {CardId::FlameBarrierPlus, {"Flame Barrier+", 2, 0, 0, 16, CardTarget::None, {}, {{Power::FlameBarrier, 6, Target::Character}}, CardType::Skill, false, false}},
    {CardId::Brutality, {"Brutality", 0, 0, 0, 0, CardTarget::None, {}, {{Power::Brutality, 1, Target::Character}}, CardType::Power, false, false}},
    // Brutality+ is Innate: it starts in the opening hand.
    {CardId::BrutalityPlus, {"Brutality+", 0, 0, 0, 0, CardTarget::None, {}, {{Power::Brutality, 1, Target::Character}}, CardType::Power, false, false, false, 0, 0, 0, /*innate=*/true}},
    // Berserk's self-Vulnerable is the cost of its permanent +1 energy.
    {CardId::Berserk, {"Berserk", 0, 0, 0, 0, CardTarget::Self, {{Debuff::Vulnerable, 2, Target::Character}}, {{Power::Berserk, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::BerserkPlus, {"Berserk+", 0, 0, 0, 0, CardTarget::Self, {{Debuff::Vulnerable, 1, Target::Character}}, {{Power::Berserk, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::Metallicize, {"Metallicize", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Metallicize, 3, Target::Character}}, CardType::Power, false, false}},
    {CardId::MetallicizePlus, {"Metallicize+", 1, 0, 0, 0, CardTarget::None, {}, {{Power::Metallicize, 4, Target::Character}}, CardType::Power, false, false}},
    // --- Ironclad Tier D (Stage 4b): the query/modifier layer. The trailing
    // flags select a RULE in query.cc; the card never carries the logic.
    // Body Slam: damage = current block (damage field unused).
    {CardId::BodySlam, {"Body Slam", 1, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::EqualToBlock}},
    {CardId::BodySlamPlus, {"Body Slam+", 0, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::EqualToBlock}},
    // Clash: only playable when every card in hand is an Attack.
    {CardId::Clash, {"Clash", 0, 14, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, /*attacks_only_in_hand=*/true}},
    {CardId::ClashPlus, {"Clash+", 0, 18, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, /*attacks_only_in_hand=*/true}},
    // Heavy Blade: Strength counts 3x (5x upgraded).
    {CardId::HeavyBlade, {"Heavy Blade", 2, 14, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, /*strength_mult=*/3}},
    {CardId::HeavyBladePlus, {"Heavy Blade+", 2, 14, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, /*strength_mult=*/5}},
    // Perfected Strike: +2 (+3) per "Strike"-named card in hand/draw/discard.
    {CardId::PerfectedStrike, {"Perfected Strike", 2, 6, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::PerStrikeInDeck, 2}},
    {CardId::PerfectedStrikePlus, {"Perfected Strike+", 2, 6, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::PerStrikeInDeck, 3}},
    // Battle Trance: draw, then no further draws this turn.
    {CardId::BattleTrance, {"Battle Trance", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 3, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, /*no_draw_after=*/true}},
    {CardId::BattleTrancePlus, {"Battle Trance+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 4, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, /*no_draw_after=*/true}},
    // Blood for Blood: costs 1 less per HP-loss event this combat.
    {CardId::BloodForBlood, {"Blood For Blood", 4, 18, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, /*cost_drops_per_hp_loss=*/true}},
    {CardId::BloodForBloodPlus, {"Blood For Blood+", 3, 22, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, /*cost_drops_per_hp_loss=*/true}},
    // Dropkick: if the target is Vulnerable, gain 1 energy and draw 1.
    {CardId::Dropkick, {"Dropkick", 1, 5, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, /*bonus_if_target_vulnerable=*/true}},
    {CardId::DropkickPlus, {"Dropkick+", 1, 8, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, /*bonus_if_target_vulnerable=*/true}},
    // Entrench: double your current block.
    {CardId::Entrench, {"Entrench", 2, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, /*doubles_block=*/true}},
    {CardId::EntrenchPlus, {"Entrench+", 1, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, /*doubles_block=*/true}},
    // Sever Soul: exhaust all non-Attack cards in hand, then deal damage.
    {CardId::SeverSoul, {"Sever Soul", 2, 16, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, /*exhausts_non_attacks_in_hand=*/true}},
    {CardId::SeverSoulPlus, {"Sever Soul+", 2, 22, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, /*exhausts_non_attacks_in_hand=*/true}},
    // Barricade / Corruption: pure query-layer powers (no hook behavior).
    {CardId::Barricade, {"Barricade", 3, 0, 0, 0, CardTarget::None, {}, {{Power::Barricade, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::BarricadePlus, {"Barricade+", 2, 0, 0, 0, CardTarget::None, {}, {{Power::Barricade, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::Corruption, {"Corruption", 3, 0, 0, 0, CardTarget::None, {}, {{Power::Corruption, 1, Target::Character}}, CardType::Power, false, false}},
    {CardId::CorruptionPlus, {"Corruption+", 2, 0, 0, 0, CardTarget::None, {}, {{Power::Corruption, 1, Target::Character}}, CardType::Power, false, false}},
    // --- Ironclad Tier E (Stage 4c): the choice cards. `requests_choice`
    // names the pause; upgrades that change the choice's SHAPE (rather than a
    // number) are parameters, not new ChoiceKinds.
    // Armaments: gain 5 Block, upgrade a card in hand. Armaments+ upgrades the
    // WHOLE hand, so it opens no choice at all.
    {CardId::Armaments, {"Armaments", 1, 0, 0, 5, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::UpgradeCardInHand}},
    {CardId::ArmamentsPlus, {"Armaments+", 1, 0, 0, 5, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, /*upgrades_whole_hand=*/true}},
    // Warcry: draw, put a hand card on top of the draw pile, Exhaust.
    {CardId::Warcry, {"Warcry", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/true, false, false, /*draw=*/1, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::HandToTopOfDraw}},
    {CardId::WarcryPlus, {"Warcry+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/true, false, false, /*draw=*/2, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::HandToTopOfDraw}},
    // Headbutt: damage, then discard -> top of draw.
    {CardId::Headbutt, {"Headbutt", 1, 9, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::DiscardToTopOfDraw}},
    {CardId::HeadbuttPlus, {"Headbutt+", 1, 12, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::DiscardToTopOfDraw}},
    // Exhume: exhaust pile -> hand. Exhausts itself (so it can't retrieve itself).
    {CardId::Exhume, {"Exhume", 1, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::ExhaustToHand}},
    {CardId::ExhumePlus, {"Exhume+", 0, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::ExhaustToHand}},
    // Dual Wield: copy an Attack/Power in hand. The + adds 2 copies.
    {CardId::DualWield, {"Dual Wield", 1, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::CopyAttackOrPowerInHand, false, /*choice_copies=*/1}},
    {CardId::DualWieldPlus, {"Dual Wield+", 1, 0, 0, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::CopyAttackOrPowerInHand, false, /*choice_copies=*/2}},
    // --- Per-instance cards. Their damage depends on the individual copy, so
    // CardData holds only the BASE; the instance carries the rest.
    // Rampage: 8 damage, and that copy permanently gains +5 (+8) this combat.
    {CardId::Rampage, {"Rampage", 1, 8, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/5}},
    {CardId::RampagePlus, {"Rampage+", 1, 8, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    // Searing Blow: damage comes entirely from the instance's upgrade count
    // via DamageRule::SearingBlow, so CardData::damage is unused. The "+"
    // form is just the n=1 starting point.
    {CardId::SearingBlow, {"Searing Blow", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::SearingBlow}},
    {CardId::SearingBlowPlus, {"Searing Blow+", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::SearingBlow}},
    // --- Status cards the player's own cards generate. Unplayable, like
    // Slimed and Dazed (which enemies generate).
    // Wound: pure dead weight — clogs the hand and does nothing else.
    {CardId::Wound, {"Wound", 0, 0, 1, 0, CardTarget::None, {}, {}, CardType::Status, /*exhaust=*/false, /*ethereal=*/false, /*unplayable=*/true}},
    // Burn: 2 damage at end of turn while in hand, then discards normally.
    {CardId::Burn, {"Burn", 0, 0, 1, 0, CardTarget::None, {}, {}, CardType::Status, false, false, /*unplayable=*/true, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, /*end_of_turn_damage_in_hand=*/2}},
    // --- Cards that generate other cards. Where the generated card lands is
    // card-specific in StS and materially different (see GeneratedPile).
    // Wild Strike: 12 damage, SHUFFLE a Wound into the draw pile.
    {CardId::WildStrike, {"Wild Strike", 1, 12, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Wound, 1, GeneratedPile::ShuffleDraw}},
    {CardId::WildStrikePlus, {"Wild Strike+", 1, 17, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Wound, 1, GeneratedPile::ShuffleDraw}},
    // Power Through: 15 block, add 2 Wounds to HAND.
    {CardId::PowerThrough, {"Power Through", 1, 0, 1, 15, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Wound, 2, GeneratedPile::Hand}},
    {CardId::PowerThroughPlus, {"Power Through+", 1, 0, 1, 20, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Wound, 2, GeneratedPile::Hand}},
    // Immolate: 21 AoE damage, add a Burn to the DISCARD pile.
    {CardId::Immolate, {"Immolate", 2, 21, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Burn, 1, GeneratedPile::Discard}},
    {CardId::ImmolatePlus, {"Immolate+", 2, 28, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Burn, 1, GeneratedPile::Discard}},
    // Reckless Charge: 7 damage, SHUFFLE a Dazed into the draw pile.
    {CardId::RecklessCharge, {"Reckless Charge", 0, 7, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Dazed, 1, GeneratedPile::ShuffleDraw}},
    {CardId::RecklessChargePlus, {"Reckless Charge+", 0, 10, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Dazed, 1, GeneratedPile::ShuffleDraw}},
    // Anger: 6 damage, add a copy of ITSELF to the discard pile.
    {CardId::Anger, {"Anger", 0, 6, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 1, GeneratedPile::Discard, /*generates_self_copy=*/true}},
    {CardId::AngerPlus, {"Anger+", 0, 8, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 1, GeneratedPile::Discard, /*generates_self_copy=*/true}},
    // --- Simple new mechanisms.
    // Sword Boomerang: 3 damage, 3 (4) times, each hit to a RANDOM enemy.
    // CardTarget::None because the player picks no target.
    {CardId::SwordBoomerang, {"Sword Boomerang", 1, 3, 3, 0, CardTarget::None, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, /*hits_random_enemies=*/true}},
    {CardId::SwordBoomerangPlus, {"Sword Boomerang+", 1, 3, 4, 0, CardTarget::None, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, /*hits_random_enemies=*/true}},
    // Limit Break: double your Strength. Exhausts (the + does not).
    {CardId::LimitBreak, {"Limit Break", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, /*strength_multiply=*/2}},
    {CardId::LimitBreakPlus, {"Limit Break+", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, /*strength_multiply=*/2}},
    // Spot Weakness: 3 (4) Strength, but only if the target intends to attack.
    {CardId::SpotWeakness, {"Spot Weakness", 1, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, /*strength_if_target_attacking=*/3}},
    {CardId::SpotWeaknessPlus, {"Spot Weakness+", 1, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, /*strength_if_target_attacking=*/4}},
};

// Whether a card needs the player to PICK a specific enemy slot (ROB-80). Only
// CardTarget::Enemy does — it gets a target index and is masked on that slot
// being alive. None / AllEnemies / Self all resolve without a pick and use the
// canonical action slot 0 (AoE loops all living enemies at resolve time).
inline bool card_targets_enemy(const CardData& data) {
  return data.target == CardTarget::Enemy;
}

// Display name for a card (ROB-79) — reads CardData::name, the single source of
// truth. The TUI uses this so it never maintains its own name map.
inline const char* card_name(CardId id) { return CARD_DATABASE.at(id).name; }

}  // namespace minispire
