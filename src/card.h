#pragma once

#include <array>
#include <cctype>
#include <string>
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
  // Exhaust-driven cards: they exhaust other cards (randomly, by choice, or
  // wholesale) or react to being exhausted themselves.
  TrueGrit,
  TrueGritPlus,
  BurningPact,
  BurningPactPlus,
  SecondWind,
  SecondWindPlus,
  FiendFire,
  FiendFirePlus,
  Sentinel,
  SentinelPlus,
  // Life-total cards: the first to heal or raise max HP.
  Feed,
  FeedPlus,
  Reaper,
  ReaperPlus,
  // Meta-cards: they cause OTHER cards to be played or generated. These are
  // the re-entrant cases the action queue was built for.
  DoubleTap,
  DoubleTapPlus,
  Havoc,
  HavocPlus,
  InfernalBlade,
  InfernalBladePlus,
  // --- ROB-87 rung ladders. Per-instance growth IS card identity: each rung is
  // a distinct type reached by an ID swap, exactly like every other upgrade.
  // Appended rather than inserted so existing action indices
  // (card_id * kMaxEnemies + target) keep their meaning.
  //
  // Searing Blow @2..@5 — rung 0 is SearingBlow, rung 1 is SearingBlowPlus.
  SearingBlow2,   // 21 damage
  SearingBlow3,   // 27 damage
  SearingBlow4,   // 34 damage
  SearingBlow5,   // 42 damage
  // Rampage +5..+30 (the bonus is in the name; +0 is Rampage).
  Rampage5,
  Rampage10,
  Rampage15,
  Rampage20,
  Rampage25,
  Rampage30,
  // Rampage+ reachable bonuses {5a + 8b}: grown `a` times at +5, upgraded by
  // Armaments, then grown `b` times at +8. NOT every integer — +22, +27 and
  // +35 are unreachable and deliberately absent. Generated, not transcribed.
  RampagePlus5,
  RampagePlus8,
  RampagePlus10,
  RampagePlus13,
  RampagePlus15,
  RampagePlus16,
  RampagePlus18,
  RampagePlus20,
  RampagePlus21,
  RampagePlus23,
  RampagePlus24,
  RampagePlus25,
  RampagePlus26,
  RampagePlus28,
  RampagePlus29,
  RampagePlus30,
  RampagePlus31,
  RampagePlus32,
  RampagePlus33,
  RampagePlus34,
  RampagePlus36,
  RampagePlus37,
  RampagePlus38,
  RampagePlus39,
  RampagePlus40,

  // --- Colorless (v2). 35 cards, 70 ids. ---
  //
  // APPENDED, never inserted. A card's action index is `id * kMaxEnemies`, so
  // inserting here would renumber every card after it and invalidate any
  // trained policy's card mapping. Appending leaves all 189 existing indices
  // byte-identical and only moves kEndTurnAction, which is derived.
  //
  // Reachable in Act 1 via the shop's 2 colorless slots and Neow's colorless
  // blessings — neither implemented yet, so nothing can obtain these today.
  // Cards whose effects need machinery that does not exist are marked
  // `unplayable` in CARD_DATABASE, the same gate Dazed uses, so they occupy a
  // stable action index without being a playable no-op.
  BandageUp,
  BandageUpPlus,
  Blind,
  BlindPlus,
  DarkShackles,
  DarkShacklesPlus,
  DeepBreath,
  DeepBreathPlus,
  Discovery,
  DiscoveryPlus,
  DramaticEntrance,
  DramaticEntrancePlus,
  Enlightenment,
  EnlightenmentPlus,
  Finesse,
  FinessePlus,
  FlashOfSteel,
  FlashOfSteelPlus,
  GoodInstincts,
  GoodInstinctsPlus,
  Forethought,
  ForethoughtPlus,
  Impatience,
  ImpatiencePlus,
  JackOfAllTrades,
  JackOfAllTradesPlus,
  Madness,
  MadnessPlus,
  MindBlast,
  MindBlastPlus,
  Panacea,
  PanaceaPlus,
  PanicButton,
  PanicButtonPlus,
  Purity,
  PurityPlus,
  SwiftStrike,
  SwiftStrikePlus,
  Trip,
  TripPlus,

  // --- Colorless, rare pool (15 cards) ---
  Apotheosis,
  ApotheosisPlus,
  HandOfGreed,
  HandOfGreedPlus,
  MasterOfStrategy,
  MasterOfStrategyPlus,
  Violence,
  ViolencePlus,
  Chrysalis,
  ChrysalisPlus,
  Metamorphosis,
  MetamorphosisPlus,
  Transmutation,
  TransmutationPlus,
  Magnetism,
  MagnetismPlus,
  Mayhem,
  MayhemPlus,
  Panache,
  PanachePlus,
  SadisticNature,
  SadisticNaturePlus,
  SecretTechnique,
  SecretTechniquePlus,
  SecretWeapon,
  SecretWeaponPlus,
  TheBomb,
  TheBombPlus,
  ThinkingAhead,
  ThinkingAheadPlus,

  // --- Curses (11). NO upgraded forms: curses cannot be upgraded, which is why
  // this block is 11 ids and not 22, and why CARDS is 270 rather than 281.
  //
  // Ascender's Bane is deliberately absent — it only appears at Ascension 10+,
  // and Ascension is pinned at 0 (§15 correction #8), so it would be a dead
  // index.
  //
  // All are Unplayable by design. Unlike the colorless block's `unplayable`,
  // which marks "not implemented yet", here it is the card's actual rule.
  Clumsy,
  Decay,
  Doubt,
  Injury,
  Normality,
  Pain,
  Parasite,
  Regret,
  Shame,
  Writhe,
  CurseOfTheBell,
};

// Number of distinct card types. Drives the obs pile-count stride and the
// action-space size (card x target). Update CARD_DATABASE + kObsCardOrder in
// lockstep — a static_assert in combat_env.cc enforces the count matches.
// 189 Ironclad (v1.0.0) + 70 colorless + 11 curses = 270, which is the figure
// §5.1 commits to and the width the v2 action space is sized against.
inline constexpr int kNumCardTypes = 270;

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
  ExhaustCardInHand,        // Burning Pact, True Grit+: exhaust a chosen card
  // v2.0.0 (map / shop / events) appends here — no encoding change.
};

// Which cards a card exhausts wholesale from the hand. Sever Soul's variant
// (non-attacks, no scaling) predates this and stays on its own flag.
enum class ExhaustHandRule {
  None,
  NonAttacks,  // Second Wind
  All,         // Fiend Fire
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
  EqualToDrawPile,  // Mind Blast: damage = cards left in the draw pile, read at
                    // resolution. The card has already left the hand by then,
                    // so it never counts itself.
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
// A card that exists only for the duration of one combat: statuses the enemy
// adds (Wound, Dazed, Slimed, Burn) and cards conjured mid-fight (Infernal
// Blade). Write-back skips these, so they need no per-card special case.
inline constexpr int kCombatScopedCardUid = -1;

struct Card {
  CardId card_id;
  // Extra damage accumulated this combat (Rampage). Combat-scoped.
  int bonus_damage = 0;
  // Times this specific card has been upgraded (Searing Blow). Run-scoped in
  // v2; today it only changes if something upgrades the card mid-combat.
  int upgrades = 0;
  // Stable identity of this instance within a run, so run-scoped changes can be
  // written back to the right card in the master deck. Assigned from
  // RunState::next_card_uid when a card enters the master deck; anything created
  // during a fight keeps the sentinel.
  //
  // Declared LAST deliberately: every existing `Card{id}` / `Card{id, bonus,
  // upgrades}` aggregate initialisation stays valid and picks up the sentinel.
  //
  // Never enters the observation — it is bookkeeping, not something a human
  // perceives, and a raw id in a float slot would assert a false ordinal.
  int uid = kCombatScopedCardUid;

  // Do these two instances play identically? Used to decide whether they
  // collapse into one option in a choice.
  //
  // uid is deliberately NOT compared. Two Strikes play the same whichever copy
  // they are; including uid would make every card unique, stop duplicate options
  // collapsing, and change shipped v1.0.0 choice behaviour.
  bool same_as(const Card& other) const {
    return card_id == other.card_id && bonus_damage == other.bonus_damage &&
           upgrades == other.upgrades;
  }
  // Does this instance carry any state beyond its id? Same reasoning as
  // same_as: uid is not "state" in this sense.
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
  // True Grit: exhaust N RANDOM cards from hand (the + lets you choose, via
  // ChoiceKind::ExhaustCardInHand instead).
  int exhaust_random_from_hand = 0;
  // Second Wind / Fiend Fire: exhaust a swathe of the hand, then scale an
  // effect by how many were exhausted. `exhausts_hand` selects which cards go.
  ExhaustHandRule exhausts_hand = ExhaustHandRule::None;
  int block_per_exhausted = 0;   // Second Wind
  int damage_per_exhausted = 0;  // Fiend Fire
  // Sentinel: gain this much energy when this card is EXHAUSTED (not played).
  int energy_when_exhausted = 0;
  // Feed: raise max HP by this much if this card's damage KILLS an enemy
  // ("If Fatal"). None of the Act 1 roster are minions, which is the only
  // exclusion StS applies.
  int max_hp_on_kill = 0;
  // Reaper: heal the player for the UNBLOCKED damage this card dealt (summed
  // across all targets, since Reaper is AoE).
  bool heals_unblocked_damage = false;
  // Havoc: play the top card of the draw pile, then force-exhaust it.
  bool plays_top_of_draw = false;
  // Infernal Blade: add a random Attack to hand, costing 0 this turn.
  bool generates_random_attack = false;
  // --- Colorless cards (v2). Appended, never inserted: every field below sits
  // AFTER the existing ones so all 189 positional CARD_DATABASE initialisers
  // stay valid and untouched — the same reason Card::uid was declared last. ---
  //
  // Bandage Up: flat healing, unrelated to damage dealt (that is
  // heals_unblocked_damage, above, which is Reaper's).
  int heal = 0;
  // Deep Breath: shuffle the discard pile into the draw pile. Distinct from the
  // automatic reshuffle inside draw_one, which only fires when the draw pile has
  // run dry — this one happens regardless, which is the whole card.
  bool shuffles_discard_into_draw = false;
  // Impatience: `draw` only happens when the hand holds no Attacks. Checked at
  // TRANSLATION time, when the card has already left the hand — so Impatience
  // never counts itself, and it is a Skill anyway.
  bool draw_needs_no_attacks_in_hand = false;
  // Regret: at end of turn, lose HP equal to the number of cards in hand. HP
  // LOSS, not damage — block does not absorb it. Counts the whole hand
  // including Regret itself, which is why holding several is quadratic.
  bool end_of_turn_hp_loss_per_card_in_hand = false;
  // Doubt / Shame: at end of turn, gain this debuff. The card discards itself
  // at the same moment, exactly as Burn and Decay do.
  Debuff end_of_turn_self_debuff = Debuff::None;
  int end_of_turn_self_debuff_amount = 0;
  // Pain: while in hand, lose 1 HP whenever ANOTHER card is played — before
  // that card resolves.
  int hp_loss_in_hand_per_card_played = 0;
  // Normality: while in hand, caps how many cards may be played this turn.
  // 0 = no cap.
  int cards_playable_cap_in_hand = 0;
  // Parasite: lose this much Max HP if the card is transformed or REMOVED from
  // the deck. Not on exhaust — Blue Candle dodges it.
  int max_hp_loss_on_removal = 0;
  // Curse of the Bell: cannot be removed or transformed, by any means.
  bool cannot_be_removed = false;
  // --- Colorless effects, batch 1 (docs/design/colorless-effects.md §5).
  // Appended, never inserted, for the same reason as every block above. ---
  //
  // Apotheosis: upgrade every upgradable card in hand, draw, discard AND
  // exhaust. Never itself (the card is in flight while it resolves) and never
  // cards generated afterwards.
  bool upgrades_all_piles = false;
  // Violence: move `draw_pile_to_hand_count` random cards of this type from the
  // draw pile into the hand. Fewer matches than asked for simply yields fewer.
  int draw_pile_to_hand_count = 0;
  CardType draw_pile_to_hand_type = CardType::Attack;
  // Dark Shackles: the target loses this much Strength for the rest of the
  // turn. StS models "for the turn" as a permanent loss PLUS a Shackled power
  // that hands it back at the end of the enemy's turn — and it applies Shackled
  // only when the target has no Artifact, since a charge negates the loss and
  // leaves nothing to give back.
  int enemy_strength_loss_for_turn = 0;
  // Hand of Greed: gold when this card's damage is FATAL. Recorded on the
  // combat state; RunState writes it back (colorless-effects.md D5).
  int gold_on_kill = 0;
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
    // --- Colorless (v2) ---
    {CardId::BandageUp, CardId::BandageUpPlus},
    {CardId::Blind, CardId::BlindPlus},
    {CardId::DarkShackles, CardId::DarkShacklesPlus},
    {CardId::DeepBreath, CardId::DeepBreathPlus},
    {CardId::Discovery, CardId::DiscoveryPlus},
    {CardId::DramaticEntrance, CardId::DramaticEntrancePlus},
    {CardId::Enlightenment, CardId::EnlightenmentPlus},
    {CardId::Finesse, CardId::FinessePlus},
    {CardId::FlashOfSteel, CardId::FlashOfSteelPlus},
    {CardId::GoodInstincts, CardId::GoodInstinctsPlus},
    {CardId::Forethought, CardId::ForethoughtPlus},
    {CardId::Impatience, CardId::ImpatiencePlus},
    {CardId::JackOfAllTrades, CardId::JackOfAllTradesPlus},
    {CardId::Madness, CardId::MadnessPlus},
    {CardId::MindBlast, CardId::MindBlastPlus},
    {CardId::Panacea, CardId::PanaceaPlus},
    {CardId::PanicButton, CardId::PanicButtonPlus},
    {CardId::Purity, CardId::PurityPlus},
    {CardId::SwiftStrike, CardId::SwiftStrikePlus},
    {CardId::Trip, CardId::TripPlus},
    // --- Colorless, rare pool ---
    {CardId::Apotheosis, CardId::ApotheosisPlus},
    {CardId::HandOfGreed, CardId::HandOfGreedPlus},
    {CardId::MasterOfStrategy, CardId::MasterOfStrategyPlus},
    {CardId::Violence, CardId::ViolencePlus},
    {CardId::Chrysalis, CardId::ChrysalisPlus},
    {CardId::Metamorphosis, CardId::MetamorphosisPlus},
    {CardId::Transmutation, CardId::TransmutationPlus},
    {CardId::Magnetism, CardId::MagnetismPlus},
    {CardId::Mayhem, CardId::MayhemPlus},
    {CardId::Panache, CardId::PanachePlus},
    {CardId::SadisticNature, CardId::SadisticNaturePlus},
    {CardId::SecretTechnique, CardId::SecretTechniquePlus},
    {CardId::SecretWeapon, CardId::SecretWeaponPlus},
    {CardId::TheBomb, CardId::TheBombPlus},
    {CardId::ThinkingAhead, CardId::ThinkingAheadPlus},
    {CardId::WildStrike, CardId::WildStrikePlus},
    {CardId::PowerThrough, CardId::PowerThroughPlus},
    {CardId::Immolate, CardId::ImmolatePlus},
    {CardId::RecklessCharge, CardId::RecklessChargePlus},
    {CardId::Anger, CardId::AngerPlus},
    {CardId::SwordBoomerang, CardId::SwordBoomerangPlus},
    {CardId::LimitBreak, CardId::LimitBreakPlus},
    {CardId::SpotWeakness, CardId::SpotWeaknessPlus},
    {CardId::TrueGrit, CardId::TrueGritPlus},
    {CardId::BurningPact, CardId::BurningPactPlus},
    {CardId::SecondWind, CardId::SecondWindPlus},
    {CardId::FiendFire, CardId::FiendFirePlus},
    {CardId::Sentinel, CardId::SentinelPlus},
    {CardId::Feed, CardId::FeedPlus},
    {CardId::Reaper, CardId::ReaperPlus},
    {CardId::DoubleTap, CardId::DoubleTapPlus},
    {CardId::Havoc, CardId::HavocPlus},
    {CardId::InfernalBlade, CardId::InfernalBladePlus},
    // --- ROB-87 rung ladders. Searing Blow is no longer a special case: its
    // upgrades are ordinary CardId swaps up its own ladder, so the old
    // instance-counter branch is gone. Rung 5 has no edge and saturates.
    {CardId::SearingBlow, CardId::SearingBlowPlus},
    {CardId::SearingBlowPlus, CardId::SearingBlow2},
    {CardId::SearingBlow2, CardId::SearingBlow3},
    {CardId::SearingBlow3, CardId::SearingBlow4},
    {CardId::SearingBlow4, CardId::SearingBlow5},
    // A grown Rampage upgrades to the Rampage+ rung of the SAME accumulated
    // bonus — every base rung's bonus is reachable on the upgraded ladder
    // (asserted by RungLaddersMatchTheReachableSet).
    {CardId::Rampage5, CardId::RampagePlus5},
    {CardId::Rampage10, CardId::RampagePlus10},
    {CardId::Rampage15, CardId::RampagePlus15},
    {CardId::Rampage20, CardId::RampagePlus20},
    {CardId::Rampage25, CardId::RampagePlus25},
    {CardId::Rampage30, CardId::RampagePlus30},
};

// Which rung of the Searing Blow ladder an id sits on, i.e. how many times it
// has been upgraded (ROB-87). Rung 0 is the printed card; rung 1 is the id
// historically called SearingBlowPlus. Non-Searing-Blow ids are rung 0.
inline int searing_blow_rung(CardId id) {
  switch (id) {
    case CardId::SearingBlowPlus: return 1;
    case CardId::SearingBlow2:    return 2;
    case CardId::SearingBlow3:    return 3;
    case CardId::SearingBlow4:    return 4;
    case CardId::SearingBlow5:    return 5;
    default:                      return 0;
  }
}

// The Ironclad's obtainable card pools, by rarity. See v2-spec.md §4.2.
//
// These are what a card reward, a shop and a transform draw FROM — you sample a
// pool, you do not scan every card filtering on a rarity field. Kept as pools
// rather than a `rarity` member on CardData for that reason, and because it
// leaves CARD_DATABASE's 189 rows untouched.
//
// Only BASE cards appear: rewards never offer an upgraded card, so `Anger` is
// here and `AngerPlus` is not. 20 + 36 + 16 = 72, which is the pool size that
// reproduces our 189 card types (72x2 upgrades + 3 starters x2 + rungs +
// statuses) — see §5.1.
//
// Source: sts_lightspeed `RarityCardPool::cardBlob` (Ironclad slice), with the
// membership cross-checked against wiki.gg's Ironclad card list. Note the wiki
// page's *stated* totals (21/31/10) disagree with its own name lists, which are
// what agree with the pools below — a summary that states a total is not a
// count (CLAUDE.md).
inline const std::vector<CardId> IRONCLAD_COMMON_POOL = {
    CardId::Anger,        CardId::Cleave,          CardId::Warcry,
    CardId::Flex,         CardId::IronWave,        CardId::BodySlam,
    CardId::TrueGrit,     CardId::ShrugItOff,      CardId::Clash,
    CardId::Thunderclap,  CardId::PommelStrike,    CardId::TwinStrike,
    CardId::Clothesline,  CardId::Armaments,       CardId::Havoc,
    CardId::Headbutt,     CardId::WildStrike,      CardId::HeavyBlade,
    CardId::PerfectedStrike, CardId::SwordBoomerang,
};

inline const std::vector<CardId> IRONCLAD_UNCOMMON_POOL = {
    CardId::SpotWeakness,  CardId::Inflame,       CardId::PowerThrough,
    CardId::DualWield,     CardId::InfernalBlade, CardId::RecklessCharge,
    CardId::Hemokinesis,   CardId::Intimidate,    CardId::BloodForBlood,
    CardId::FlameBarrier,  CardId::Pummel,        CardId::BurningPact,
    CardId::Metallicize,   CardId::Shockwave,     CardId::Rampage,
    CardId::SeverSoul,     CardId::Whirlwind,     CardId::Combust,
    CardId::DarkEmbrace,   CardId::SeeingRed,     CardId::Disarm,
    CardId::FeelNoPain,    CardId::Rage,          CardId::Entrench,
    CardId::Sentinel,      CardId::BattleTrance,  CardId::SearingBlow,
    CardId::SecondWind,    CardId::Rupture,       CardId::Bloodletting,
    CardId::Carnage,       CardId::Dropkick,      CardId::FireBreathing,
    CardId::GhostlyArmor,  CardId::Uppercut,      CardId::Evolve,
};

inline const std::vector<CardId> IRONCLAD_RARE_POOL = {
    CardId::Immolate,   CardId::Offering,  CardId::Exhume,     CardId::Reaper,
    CardId::Brutality,  CardId::Juggernaut, CardId::Impervious, CardId::Berserk,
    CardId::FiendFire,  CardId::Barricade, CardId::Corruption, CardId::LimitBreak,
    CardId::Feed,       CardId::Bludgeon,  CardId::DemonForm,  CardId::DoubleTap,
};

// The colorless pool, split {0 common, 20 uncommon, 15 rare} — the same shape
// sts_lightspeed's ColorlessRarityCardPool declares, and each row here was
// wiki-verified as it was written rather than transcribed in bulk.
//
// There is deliberately NO common tier: colorless has none, which is why the
// shop's colorless slots and Neow's blessings roll only these two.
//
// Only the UNUPGRADED ids. A pool is what can be OFFERED; the upgraded forms
// are reached by upgrading. (Transmutation+ generates upgraded colorless cards,
// which is a transform of this list rather than a second pool.)
inline const std::vector<CardId> COLORLESS_UNCOMMON_POOL = {
    CardId::BandageUp,       CardId::Blind,        CardId::DarkShackles,
    CardId::DeepBreath,      CardId::Discovery,    CardId::DramaticEntrance,
    CardId::Enlightenment,   CardId::Finesse,      CardId::FlashOfSteel,
    CardId::Forethought,     CardId::GoodInstincts, CardId::Impatience,
    CardId::JackOfAllTrades, CardId::Madness,      CardId::MindBlast,
    CardId::Panacea,         CardId::PanicButton,  CardId::Purity,
    CardId::SwiftStrike,     CardId::Trip,
};

// The random curse pool — what an event or relic rolls when it says "gain a
// curse". Ten cards, matching sts_lightspeed's curseCardPool.
//
// Curse of the Bell is NOT in it: it comes only from the Calling Bell relic,
// never from a roll. Ascender's Bane is absent too — Ascension 10+ content,
// and Ascension is pinned at 0 (§15 correction #8).
inline const std::vector<CardId> CURSE_POOL = {
    CardId::Regret,  CardId::Injury, CardId::Shame, CardId::Parasite,
    CardId::Normality, CardId::Doubt, CardId::Writhe, CardId::Pain,
    CardId::Decay,   CardId::Clumsy,
};

inline const std::vector<CardId> COLORLESS_RARE_POOL = {
    CardId::Apotheosis,     CardId::Chrysalis,    CardId::HandOfGreed,
    CardId::Magnetism,      CardId::MasterOfStrategy, CardId::Mayhem,
    CardId::Metamorphosis,  CardId::Panache,      CardId::SadisticNature,
    CardId::SecretTechnique, CardId::SecretWeapon, CardId::TheBomb,
    CardId::ThinkingAhead,  CardId::Transmutation, CardId::Violence,
};

// What a card BECOMES after being played, for cards that grow (ROB-87). Growth
// is an ID swap, the same mechanism as an upgrade — Rampage's "+5 this combat"
// moves it one rung up its ladder rather than mutating a hidden counter.
//
// Absent = at the cap, where the engine falls back to Card::bonus_damage and
// keeps counting internally, so damage stays exact above the cap even though
// the observation and action encoding saturate.
inline const std::unordered_map<CardId, CardId> CARD_GROWTH = {
    {CardId::Rampage, CardId::Rampage5},
    {CardId::Rampage5, CardId::Rampage10},
    {CardId::Rampage10, CardId::Rampage15},
    {CardId::Rampage15, CardId::Rampage20},
    {CardId::Rampage20, CardId::Rampage25},
    {CardId::Rampage25, CardId::Rampage30},
    // The upgraded ladder steps by 8, so it threads between the base rungs;
    // a mixed history (grown, then upgraded, then grown) lands on values like
    // +13 or +26 that no pure sequence reaches.
    {CardId::RampagePlus, CardId::RampagePlus8},
    {CardId::RampagePlus5, CardId::RampagePlus13},
    {CardId::RampagePlus8, CardId::RampagePlus16},
    {CardId::RampagePlus10, CardId::RampagePlus18},
    {CardId::RampagePlus13, CardId::RampagePlus21},
    {CardId::RampagePlus15, CardId::RampagePlus23},
    {CardId::RampagePlus16, CardId::RampagePlus24},
    {CardId::RampagePlus18, CardId::RampagePlus26},
    {CardId::RampagePlus20, CardId::RampagePlus28},
    {CardId::RampagePlus21, CardId::RampagePlus29},
    {CardId::RampagePlus23, CardId::RampagePlus31},
    {CardId::RampagePlus24, CardId::RampagePlus32},
    {CardId::RampagePlus25, CardId::RampagePlus33},
    {CardId::RampagePlus26, CardId::RampagePlus34},
    {CardId::RampagePlus28, CardId::RampagePlus36},
    {CardId::RampagePlus29, CardId::RampagePlus37},
    {CardId::RampagePlus30, CardId::RampagePlus38},
    {CardId::RampagePlus31, CardId::RampagePlus39},
    {CardId::RampagePlus32, CardId::RampagePlus40},
    // +33 and above would exceed the cap; those rungs saturate.
};

// Grow a card IN PLACE after a play. Returns true if it moved a rung; false at
// the cap, where the caller adds to Card::bonus_damage instead.
inline bool grow_card_in_place(Card& card) {
  auto it = CARD_GROWTH.find(card.card_id);
  if (it == CARD_GROWTH.end()) return false;
  card.card_id = it->second;
  return true;
}

// Cards sitting at the TOP of a rung ladder (ROB-87). Below the cap every rung
// is a real CARD_UPGRADES edge; at the cap there is no higher id, so further
// upgrades accumulate on the instance instead.
//
// This is the saturation clause, and it is load-bearing for PARITY: Searing
// Blow's text is "can be upgraded any number of times", so the top rung must
// stay upgradable. Were this to return false, is_upgradable would too and
// Armaments would silently stop offering the card — a rule break with no
// compile error and no failing test until one is written for exactly this.
inline bool is_instance_upgradable(CardId id) {
  return id == CardId::SearingBlow5;
}

// Upgrade a card IN PLACE. Normally the id swaps for the next rung (or the "+"
// form, which is the same mechanism); past the top rung the instance counter
// grows instead. Returns false if the card cannot be upgraded at all.
inline bool upgrade_card_in_place(Card& card) {
  auto it = CARD_UPGRADES.find(card.card_id);
  if (it != CARD_UPGRADES.end()) {
    card.card_id = it->second;
    return true;
  }
  if (is_instance_upgradable(card.card_id)) {
    ++card.upgrades;  // above the cap: keep counting internally
    return true;
  }
  return false;
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

// Base CardData for a colorless card, filled in by NAME rather than position.
//
// The 189 existing rows are positional, which reads fine for the fields near the
// front and badly for anything late in the struct — Reaper's row spells out
// thirty fields to reach one bool at the end. The colorless block adds seventy
// rows and several of them touch late fields, so they are built with named
// assignment instead. C++17 has no designated initialisers, hence the
// immediately-invoked lambda at each row.
//
// Existing rows are deliberately NOT converted: rewriting 189 working
// initialisers to gain nothing is how transcription errors get introduced.
inline CardData colorless(const char* name, int cost, CardType type,
                          CardTarget target = CardTarget::None) {
  CardData d{};
  d.name = name;
  d.cost = cost;
  d.type = type;
  d.target = target;
  return d;
}

// A curse. Always Unplayable and untargeted — and here `unplayable` is the
// card's REAL RULE, not the colorless block's "not implemented yet" marker.
// Cost is irrelevant to an unplayable card; it is 0 so nothing reads a
// meaningful number out of it.
inline CardData curse(const char* name) {
  CardData d{};
  d.name = name;
  d.cost = 0;
  d.type = CardType::Curse;
  d.target = CardTarget::None;
  d.unplayable = true;
  return d;
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
    // Flex: "Gain N Strength. At the end of this turn, lose N Strength." StS
    // pairs the gain with an equal Strength Down (ROB-85) — without it Flex is
    // a free permanent Inflame+.
    {CardId::Flex, {"Flex", 0, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 2, Target::Character}, {Power::StrengthDown, 2, Target::Character}}, CardType::Skill, false, false}},
    {CardId::FlexPlus, {"Flex+", 0, 0, 0, 0, CardTarget::Self, {}, {{Power::Strength, 4, Target::Character}, {Power::StrengthDown, 4, Target::Character}}, CardType::Skill, false, false}},
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
    // --- Exhaust-driven cards.
    // True Grit: 7 block, exhaust a RANDOM card. The + lets you CHOOSE, which
    // makes it a choice card rather than a bigger number.
    {CardId::TrueGrit, {"True Grit", 1, 0, 1, 7, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, /*exhaust_random_from_hand=*/1}},
    {CardId::TrueGritPlus, {"True Grit+", 1, 0, 1, 9, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, /*requests_choice=*/ChoiceKind::ExhaustCardInHand}},
    // Burning Pact: exhaust a CHOSEN card, then draw 2 (3).
    {CardId::BurningPact, {"Burning Pact", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, /*draw=*/2, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, /*requests_choice=*/ChoiceKind::ExhaustCardInHand}},
    {CardId::BurningPactPlus, {"Burning Pact+", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, /*draw=*/3, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, /*requests_choice=*/ChoiceKind::ExhaustCardInHand}},
    // Second Wind: exhaust all non-Attacks, 5 (7) block for each.
    {CardId::SecondWind, {"Second Wind", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::NonAttacks, /*block_per_exhausted=*/5}},
    {CardId::SecondWindPlus, {"Second Wind+", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::NonAttacks, /*block_per_exhausted=*/7}},
    // Fiend Fire: exhaust the WHOLE hand, 7 (10) damage per card exhausted.
    {CardId::FiendFire, {"Fiend Fire", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::All, 0, /*damage_per_exhausted=*/7}},
    {CardId::FiendFirePlus, {"Fiend Fire+", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::All, 0, /*damage_per_exhausted=*/10}},
    // Sentinel: 5 (8) block, and 2 (3) energy IF this card is exhausted —
    // which playing it normally does NOT do (Corruption, True Grit etc. do).
    {CardId::Sentinel, {"Sentinel", 1, 0, 1, 5, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, /*energy_when_exhausted=*/2}},
    {CardId::SentinelPlus, {"Sentinel+", 1, 0, 1, 8, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, /*energy_when_exhausted=*/3}},
    // --- Life-total cards. Both exhaust.
    // Feed: 10 (12) damage; if it KILLS, +3 (+4) max HP.
    {CardId::Feed, {"Feed", 1, 10, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, /*max_hp_on_kill=*/3}},
    {CardId::FeedPlus, {"Feed+", 1, 12, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, /*max_hp_on_kill=*/4}},
    // Reaper: 4 (5) damage to ALL enemies, heal the unblocked total.
    {CardId::Reaper, {"Reaper", 2, 4, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, 0, /*heals_unblocked_damage=*/true}},
    {CardId::ReaperPlus, {"Reaper+", 2, 5, 1, 0, CardTarget::AllEnemies, {}, {}, CardType::Attack, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, 0, /*heals_unblocked_damage=*/true}},
    // --- Meta-cards: they play or generate OTHER cards.
    // Double Tap: this turn, the next 1 (2) Attacks are played twice. Modelled
    // as a turn-scoped Power so the charge count is visible in the obs.
    {CardId::DoubleTap, {"Double Tap", 1, 0, 1, 0, CardTarget::None, {}, {{Power::DoubleTap, 1, Target::Character}}, CardType::Skill, false, false}},
    {CardId::DoubleTapPlus, {"Double Tap+", 1, 0, 1, 0, CardTarget::None, {}, {{Power::DoubleTap, 2, Target::Character}}, CardType::Skill, false, false}},
    // Havoc: play the top card of the draw pile and force-exhaust it.
    {CardId::Havoc, {"Havoc", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, 0, false, /*plays_top_of_draw=*/true}},
    {CardId::HavocPlus, {"Havoc+", 0, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, 0, false, /*plays_top_of_draw=*/true}},
    // Infernal Blade: add a random Attack to hand; it costs 0 this turn.
    {CardId::InfernalBlade, {"Infernal Blade", 1, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, 0, false, false, /*generates_random_attack=*/true}},
    {CardId::InfernalBladePlus, {"Infernal Blade+", 0, 0, 1, 0, CardTarget::None, {}, {}, CardType::Skill, /*exhaust=*/true, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, 0, 0, CardId::Strike, 0, GeneratedPile::Discard, false, false, 0, 0, 0, ExhaustHandRule::None, 0, 0, 0, 0, false, false, /*generates_random_attack=*/true}},
    // --- ROB-87 rung ladders. GENERATED by scratch/gen_rungs.py from the
    // reachable-value spec in docs/design/observation-space.md §9, not typed by
    // hand: the Rampage+ ladder is 26 irregular values with deliberate gaps at
    // +22/+27/+35, which is not eyeballable in a positional initializer.
    // RungLaddersMatchTheReachableSet recomputes the set independently.
    //
    // Searing Blow keeps DamageRule::SearingBlow; the rung comes from the ID.
    {CardId::SearingBlow2, {"Searing Blow+2", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::SearingBlow}},
    {CardId::SearingBlow3, {"Searing Blow+3", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::SearingBlow}},
    {CardId::SearingBlow4, {"Searing Blow+4", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::SearingBlow}},
    {CardId::SearingBlow5, {"Searing Blow+5", 2, 0, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::SearingBlow}},
    // Rampage rungs carry their accumulated damage in CardData::damage (8 + N),
    // so bonus_damage on the instance is overflow-only above the cap.
    {CardId::Rampage5, {"Rampage 13", 1, 13, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/5}},
    {CardId::Rampage10, {"Rampage 18", 1, 18, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/5}},
    {CardId::Rampage15, {"Rampage 23", 1, 23, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/5}},
    {CardId::Rampage20, {"Rampage 28", 1, 28, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/5}},
    {CardId::Rampage25, {"Rampage 33", 1, 33, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/5}},
    {CardId::Rampage30, {"Rampage 38", 1, 38, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/5}},
    {CardId::RampagePlus5, {"Rampage+ 13", 1, 13, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus8, {"Rampage+ 16", 1, 16, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus10, {"Rampage+ 18", 1, 18, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus13, {"Rampage+ 21", 1, 21, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus15, {"Rampage+ 23", 1, 23, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus16, {"Rampage+ 24", 1, 24, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus18, {"Rampage+ 26", 1, 26, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus20, {"Rampage+ 28", 1, 28, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus21, {"Rampage+ 29", 1, 29, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus23, {"Rampage+ 31", 1, 31, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus24, {"Rampage+ 32", 1, 32, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus25, {"Rampage+ 33", 1, 33, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus26, {"Rampage+ 34", 1, 34, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus28, {"Rampage+ 36", 1, 36, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus29, {"Rampage+ 37", 1, 37, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus30, {"Rampage+ 38", 1, 38, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus31, {"Rampage+ 39", 1, 39, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus32, {"Rampage+ 40", 1, 40, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus33, {"Rampage+ 41", 1, 41, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus34, {"Rampage+ 42", 1, 42, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus36, {"Rampage+ 44", 1, 44, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus37, {"Rampage+ 45", 1, 45, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus38, {"Rampage+ 46", 1, 46, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus39, {"Rampage+ 47", 1, 47, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},
    {CardId::RampagePlus40, {"Rampage+ 48", 1, 48, 1, 0, CardTarget::Enemy, {}, {}, CardType::Attack, false, false, false, 0, 0, 0, false, DamageRule::Normal, 0, 1, false, false, false, false, false, false, ChoiceKind::None, false, 1, /*bonus_damage_per_play=*/8}},

    // ================================================== COLORLESS (v2) =====
    // Built with `colorless()` + named assignment; see the note above the
    // helper. Each card's cost, type and numbers are wiki-verified before the
    // row is written.

    // Bandage Up: "Heal 4 HP. Exhaust." A flat heal, unrelated to damage dealt
    // — that is Reaper's heals_unblocked_damage, a different field.
    {CardId::BandageUp, [] {
       CardData d = colorless("Bandage Up", 0, CardType::Skill);
       d.exhaust = true;
       d.heal = 4;
       return d;
     }()},
    {CardId::BandageUpPlus, [] {
       CardData d = colorless("Bandage Up+", 0, CardType::Skill);
       d.exhaust = true;
       d.heal = 6;
       return d;
     }()},

    // Blind: "Apply 2 Weak." The upgrade changes the TARGET, not the amount —
    // 2 Weak either way, but to ALL enemies. One of the few cards whose upgrade
    // is a targeting change, so the two rows differ in CardTarget.
    {CardId::Blind, [] {
       CardData d = colorless("Blind", 0, CardType::Skill, CardTarget::Enemy);
       d.applies_debuffs = {{Debuff::Weak, 2, Target::Enemy}};
       return d;
     }()},
    {CardId::BlindPlus, [] {
       CardData d =
           colorless("Blind+", 0, CardType::Skill, CardTarget::AllEnemies);
       d.applies_debuffs = {{Debuff::Weak, 2, Target::Enemy}};
       return d;
     }()},

    // Dark Shackles: "Enemy loses 9 Strength this turn. Exhaust." The upgrade
    // deepens the loss to 15.
    //
    // "This turn" is a Strength loss plus Power::Shackled, which returns it at
    // the end of the enemy's turn — StS's own two-power model, and the reason
    // the give-back is skipped when the target holds Artifact.
    {CardId::DarkShackles, [] {
       CardData d =
           colorless("Dark Shackles", 0, CardType::Skill, CardTarget::Enemy);
       d.exhaust = true;
       d.enemy_strength_loss_for_turn = 9;
       return d;
     }()},
    {CardId::DarkShacklesPlus, [] {
       CardData d =
           colorless("Dark Shackles+", 0, CardType::Skill, CardTarget::Enemy);
       d.exhaust = true;
       d.enemy_strength_loss_for_turn = 15;
       return d;
     }()},

    // Deep Breath: "Shuffle your discard pile into your draw pile. Draw 1."
    // The shuffle happens whether or not the draw pile is empty, which is what
    // separates it from draw_one's automatic reshuffle.
    {CardId::DeepBreath, [] {
       CardData d = colorless("Deep Breath", 0, CardType::Skill);
       d.shuffles_discard_into_draw = true;
       d.draw = 1;
       return d;
     }()},
    {CardId::DeepBreathPlus, [] {
       CardData d = colorless("Deep Breath+", 0, CardType::Skill);
       d.shuffles_discard_into_draw = true;
       d.draw = 2;
       return d;
     }()},

    // Discovery: "Choose 1 of 3 random cards to add into your hand. It costs 0
    // this turn. Exhaust." The upgrade REMOVES the Exhaust rather than changing
    // a number — so the two rows differ in a flag, not a value.
    //
    // UNPLAYABLE: needs a choice over three GENERATED cards. The choice
    // machinery exists (ChoiceKind), but not a choice whose options are rolled
    // rather than drawn from a pile.
    {CardId::Discovery, [] {
       CardData d = colorless("Discovery", 1, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::DiscoveryPlus, [] {
       CardData d = colorless("Discovery+", 1, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},

    // Dramatic Entrance: "Deal 8 damage to ALL enemies. Innate. Exhaust."
    {CardId::DramaticEntrance, [] {
       CardData d = colorless("Dramatic Entrance", 0, CardType::Attack,
                              CardTarget::AllEnemies);
       d.damage = 8;
       d.innate = true;
       d.exhaust = true;
       return d;
     }()},
    {CardId::DramaticEntrancePlus, [] {
       CardData d = colorless("Dramatic Entrance+", 0, CardType::Attack,
                              CardTarget::AllEnemies);
       d.damage = 12;
       d.innate = true;
       d.exhaust = true;
       return d;
     }()},

    // Enlightenment: "Reduce the cost of all cards in your hand to 1 this
    // turn." The upgrade changes the DURATION — this turn becomes this combat.
    //
    // UNPLAYABLE: needs a cost override that applies to the hand for a
    // duration. free_this_turn is per-card-id and means "costs 0 once", which
    // is a different thing from "everything costs at most 1 until the fight
    // ends".
    {CardId::Enlightenment, [] {
       CardData d = colorless("Enlightenment", 0, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},
    {CardId::EnlightenmentPlus, [] {
       CardData d = colorless("Enlightenment+", 0, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},

    // Finesse: "Gain 2 Block. Draw 1 card."
    {CardId::Finesse, [] {
       CardData d = colorless("Finesse", 0, CardType::Skill);
       d.block = 2;
       d.draw = 1;
       return d;
     }()},
    {CardId::FinessePlus, [] {
       CardData d = colorless("Finesse+", 0, CardType::Skill);
       d.block = 4;
       d.draw = 1;
       return d;
     }()},

    // Flash of Steel: "Deal 3 damage. Draw 1 card."
    {CardId::FlashOfSteel, [] {
       CardData d =
           colorless("Flash of Steel", 0, CardType::Attack, CardTarget::Enemy);
       d.damage = 3;
       d.draw = 1;
       return d;
     }()},
    {CardId::FlashOfSteelPlus, [] {
       CardData d =
           colorless("Flash of Steel+", 0, CardType::Attack, CardTarget::Enemy);
       d.damage = 6;
       d.draw = 1;
       return d;
     }()},

    // Good Instincts: "Gain 6 Block."
    {CardId::GoodInstincts, [] {
       CardData d = colorless("Good Instincts", 0, CardType::Skill);
       d.block = 6;
       return d;
     }()},
    {CardId::GoodInstinctsPlus, [] {
       CardData d = colorless("Good Instincts+", 0, CardType::Skill);
       d.block = 9;
       return d;
     }()},

    // Forethought: "Put a card from your hand to the bottom of your draw pile.
    // It costs 0 until played." The upgrade makes it ANY NUMBER — a
    // single-select becoming a multi-select, which is a shape change, not a
    // number.
    //
    // UNPLAYABLE: needs a hand choice that moves to the BOTTOM of the draw pile
    // and a per-instance "costs 0 until played" marker that survives being
    // drawn again. free_this_turn is keyed by card id and expires at end of
    // turn, so it is the wrong tool for both halves.
    {CardId::Forethought, [] {
       CardData d = colorless("Forethought", 0, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},
    {CardId::ForethoughtPlus, [] {
       CardData d = colorless("Forethought+", 0, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},

    // Impatience: "If you have no Attacks in your hand, draw 2 cards."
    {CardId::Impatience, [] {
       CardData d = colorless("Impatience", 0, CardType::Skill);
       d.draw = 2;
       d.draw_needs_no_attacks_in_hand = true;
       return d;
     }()},
    {CardId::ImpatiencePlus, [] {
       CardData d = colorless("Impatience+", 0, CardType::Skill);
       d.draw = 3;
       d.draw_needs_no_attacks_in_hand = true;
       return d;
     }()},

    // Jack of All Trades: "Add 1 random Colorless card into your hand.
    // Exhaust."
    //
    // UNPLAYABLE: needs generation from the colorless pool, which is only
    // partly populated while this block is being added one card at a time.
    // Wiring it before the pool is complete would make the card's randomness
    // depend on how far through the list we happened to be.
    {CardId::JackOfAllTrades, [] {
       CardData d = colorless("Jack of All Trades", 0, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::JackOfAllTradesPlus, [] {
       CardData d = colorless("Jack of All Trades+", 0, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},

    // Madness: "Reduce the cost of a random card in your hand to 0 this combat.
    // Exhaust." The upgrade changes only the CARD'S OWN COST, 1 to 0 — the
    // effect text is identical.
    //
    // UNPLAYABLE: "this combat" is a PERMANENT cost override on one card
    // INSTANCE. free_this_turn is keyed by card id and expires at end of turn,
    // so it is wrong on both counts — it would make every copy of that id free,
    // and only until the turn ended.
    {CardId::Madness, [] {
       CardData d = colorless("Madness", 1, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::MadnessPlus, [] {
       CardData d = colorless("Madness+", 0, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},

    // Mind Blast: "Deal damage equal to the number of cards in your draw pile.
    // Innate." The upgrade changes only the cost, 2 to 1.
    {CardId::MindBlast, [] {
       CardData d =
           colorless("Mind Blast", 2, CardType::Attack, CardTarget::Enemy);
       d.damage_rule = DamageRule::EqualToDrawPile;
       d.innate = true;
       return d;
     }()},
    {CardId::MindBlastPlus, [] {
       CardData d =
           colorless("Mind Blast+", 1, CardType::Attack, CardTarget::Enemy);
       d.damage_rule = DamageRule::EqualToDrawPile;
       d.innate = true;
       return d;
     }()},

    // Panacea: "Gain 1 Artifact. Exhaust."
    {CardId::Panacea, [] {
       CardData d = colorless("Panacea", 0, CardType::Skill);
       d.applies_powers = {{Power::Artifact, 1, Target::Character}};
       d.exhaust = true;
       return d;
     }()},
    {CardId::PanaceaPlus, [] {
       CardData d = colorless("Panacea+", 0, CardType::Skill);
       d.applies_powers = {{Power::Artifact, 2, Target::Character}};
       d.exhaust = true;
       return d;
     }()},

    // Panic Button: "Gain 30 Block. You cannot gain Block from cards for 2
    // turns. Exhaust."
    //
    // The drawback is Debuff::NoBlock, applied AFTER this card's own block is
    // queued — so Panic Button keeps its 30 and the ban starts immediately
    // afterwards. 2 turns counts the turn it is played, which is what the
    // debuff tick gives for free.
    {CardId::PanicButton, [] {
       CardData d = colorless("Panic Button", 0, CardType::Skill);
       d.block = 30;
       d.applies_debuffs = {{Debuff::NoBlock, 2, Target::Character}};
       d.exhaust = true;
       return d;
     }()},
    {CardId::PanicButtonPlus, [] {
       CardData d = colorless("Panic Button+", 0, CardType::Skill);
       d.block = 40;
       d.applies_debuffs = {{Debuff::NoBlock, 2, Target::Character}};
       d.exhaust = true;
       return d;
     }()},

    // Purity: "Exhaust up to 3 cards in your hand. Exhaust."
    //
    // UNPLAYABLE: "up to N" is a multi-select with an optional count, which
    // §9 defers — the choice machinery answers one option at a time.
    {CardId::Purity, [] {
       CardData d = colorless("Purity", 0, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::PurityPlus, [] {
       CardData d = colorless("Purity+", 0, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},

    // Swift Strike: "Deal 7 damage."
    {CardId::SwiftStrike, [] {
       CardData d =
           colorless("Swift Strike", 0, CardType::Attack, CardTarget::Enemy);
       d.damage = 7;
       return d;
     }()},
    {CardId::SwiftStrikePlus, [] {
       CardData d =
           colorless("Swift Strike+", 0, CardType::Attack, CardTarget::Enemy);
       d.damage = 10;
       return d;
     }()},

    // Trip: "Apply 2 Vulnerable." Like Blind, the upgrade changes the TARGET
    // rather than the amount.
    {CardId::Trip, [] {
       CardData d = colorless("Trip", 0, CardType::Skill, CardTarget::Enemy);
       d.applies_debuffs = {{Debuff::Vulnerable, 2, Target::Enemy}};
       return d;
     }()},
    {CardId::TripPlus, [] {
       CardData d =
           colorless("Trip+", 0, CardType::Skill, CardTarget::AllEnemies);
       d.applies_debuffs = {{Debuff::Vulnerable, 2, Target::Enemy}};
       return d;
     }()},

    // ------------------------------------------------ colorless, rare pool ---

    // Apotheosis: "Upgrade ALL your cards for the rest of combat. Exhaust."
    // The upgrade changes only the cost, 2 to 1.
    //
    // All four piles, and never itself: the action is queued before this card's
    // own pile move, so it is still in flight and in no pile when it runs.
    // Cards generated afterwards are likewise untouched, which falls out of
    // upgrading once rather than setting a lasting flag.
    {CardId::Apotheosis, [] {
       CardData d = colorless("Apotheosis", 2, CardType::Skill);
       d.exhaust = true;
       d.upgrades_all_piles = true;
       return d;
     }()},
    {CardId::ApotheosisPlus, [] {
       CardData d = colorless("Apotheosis+", 1, CardType::Skill);
       d.exhaust = true;
       d.upgrades_all_piles = true;
       return d;
     }()},

    // Hand of Greed: "Deal 20 damage. If Fatal, gain 20 Gold."
    //
    // The gold is recorded on the combat state and written back by RunState
    // (colorless-effects.md D5), which is what gives combat a channel to a
    // run-layer resource without reaching into the run. It goes through the
    // run's gold-gain path, so Ectoplasm refuses it — but Golden Idol's +25%
    // does NOT apply, because in StS that bonus is on the reward pile, not on
    // gainGold.
    {CardId::HandOfGreed, [] {
       CardData d =
           colorless("Hand of Greed", 2, CardType::Attack, CardTarget::Enemy);
       d.damage = 20;
       d.gold_on_kill = 20;
       return d;
     }()},
    {CardId::HandOfGreedPlus, [] {
       CardData d =
           colorless("Hand of Greed+", 2, CardType::Attack, CardTarget::Enemy);
       d.damage = 25;
       d.gold_on_kill = 25;
       return d;
     }()},

    // Master of Strategy: "Draw 3 cards. Exhaust."
    {CardId::MasterOfStrategy, [] {
       CardData d = colorless("Master of Strategy", 0, CardType::Skill);
       d.draw = 3;
       d.exhaust = true;
       return d;
     }()},
    {CardId::MasterOfStrategyPlus, [] {
       CardData d = colorless("Master of Strategy+", 0, CardType::Skill);
       d.draw = 4;
       d.exhaust = true;
       return d;
     }()},

    // Violence: "Put 3 random Attacks from your draw pile into your hand.
    // Exhaust." The upgrade pulls 4.
    //
    // Random from ANYWHERE in the pile, not off the top — so it neither reveals
    // nor disturbs draw order. A pull that would overflow the hand goes to the
    // discard pile instead, which is the generic hand-full rule.
    {CardId::Violence, [] {
       CardData d = colorless("Violence", 0, CardType::Skill);
       d.exhaust = true;
       d.draw_pile_to_hand_count = 3;
       d.draw_pile_to_hand_type = CardType::Attack;
       return d;
     }()},
    {CardId::ViolencePlus, [] {
       CardData d = colorless("Violence+", 0, CardType::Skill);
       d.exhaust = true;
       d.draw_pile_to_hand_count = 4;
       d.draw_pile_to_hand_type = CardType::Attack;
       return d;
     }()},

    // Chrysalis / Metamorphosis: "Shuffle 3 random Skills [Attacks] into your
    // draw pile. They cost 0 this combat. Exhaust." Mirror images of each
    // other, differing only in the card type they generate.
    //
    // UNPLAYABLE: generation into the draw pile, plus a per-instance "costs 0
    // this combat" marker on cards that do not exist yet. Both halves are the
    // same gaps Madness and Forethought hit.
    {CardId::Chrysalis, [] {
       CardData d = colorless("Chrysalis", 2, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::ChrysalisPlus, [] {
       CardData d = colorless("Chrysalis+", 2, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::Metamorphosis, [] {
       CardData d = colorless("Metamorphosis", 2, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::MetamorphosisPlus, [] {
       CardData d = colorless("Metamorphosis+", 2, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},

    // Transmutation: "Add X random Colorless cards into your hand. They cost 0
    // this turn. Exhaust." The upgrade generates UPGRADED colorless cards — a
    // change to what is generated, not how many.
    //
    // UNPLAYABLE: colorless generation, and the pool is still being populated.
    {CardId::Transmutation, [] {
       CardData d = colorless("Transmutation", kXCost, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::TransmutationPlus, [] {
       CardData d = colorless("Transmutation+", kXCost, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},

    // Magnetism: "At the start of your turn, add a random Colorless card into
    // your hand." The only colorless POWER, and the upgrade changes only the
    // cost, 2 to 1.
    //
    // UNPLAYABLE: a turn-start power that generates. The power registry can
    // hold it, but the generation it needs does not exist.
    {CardId::Magnetism, [] {
       CardData d = colorless("Magnetism", 2, CardType::Power);
       d.unplayable = true;
       return d;
     }()},
    {CardId::MagnetismPlus, [] {
       CardData d = colorless("Magnetism+", 1, CardType::Power);
       d.unplayable = true;
       return d;
     }()},

    // Mayhem: "At the start of your turn, play the top card of your draw pile."
    // UNPLAYABLE: Havoc's plays_top_of_draw exists as a CARD effect; this needs
    // it as a recurring POWER, fired at turn start.
    {CardId::Mayhem, [] {
       CardData d = colorless("Mayhem", 2, CardType::Power);
       d.unplayable = true;
       return d;
     }()},
    {CardId::MayhemPlus, [] {
       CardData d = colorless("Mayhem+", 1, CardType::Power);
       d.unplayable = true;
       return d;
     }()},

    // Panache: "Every time you play 5 cards in a single turn, deal 10 damage to
    // ALL enemies."
    // UNPLAYABLE: a POWER with a per-turn play counter. The relic counters
    // (Kunai, Shuriken) are the same shape, but powers have no counter field —
    // their stacks are the effect's magnitude, not a tally.
    {CardId::Panache, [] {
       CardData d = colorless("Panache", 0, CardType::Power);
       d.unplayable = true;
       return d;
     }()},
    {CardId::PanachePlus, [] {
       CardData d = colorless("Panache+", 0, CardType::Power);
       d.unplayable = true;
       return d;
     }()},

    // Sadistic Nature: "Whenever you apply a debuff to an enemy, they take 5
    // damage."
    // UNPLAYABLE: needs an on-debuff-APPLIED hook. apply_debuff is a mutator,
    // not a trigger site, and adding one there touches every debuff in the game.
    {CardId::SadisticNature, [] {
       CardData d = colorless("Sadistic Nature", 0, CardType::Power);
       d.unplayable = true;
       return d;
     }()},
    {CardId::SadisticNaturePlus, [] {
       CardData d = colorless("Sadistic Nature+", 0, CardType::Power);
       d.unplayable = true;
       return d;
     }()},

    // Secret Technique / Secret Weapon: "Put a Skill [Attack] from your draw
    // pile into your hand. Exhaust." Mirror images, and BOTH upgrades remove
    // the Exhaust rather than changing a number.
    // UNPLAYABLE: a choice whose options are the draw pile FILTERED BY TYPE.
    {CardId::SecretTechnique, [] {
       CardData d = colorless("Secret Technique", 0, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::SecretTechniquePlus, [] {
       CardData d = colorless("Secret Technique+", 0, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},
    {CardId::SecretWeapon, [] {
       CardData d = colorless("Secret Weapon", 0, CardType::Skill);
       d.exhaust = true;
       d.unplayable = true;
       return d;
     }()},
    {CardId::SecretWeaponPlus, [] {
       CardData d = colorless("Secret Weapon+", 0, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},

    // The Bomb: "At the end of 3 turns, deal 40 damage to ALL enemies."
    // UNPLAYABLE: a DELAYED effect — the only card in the block that schedules
    // something for a future turn. Nothing in the engine defers an effect
    // across turn boundaries today.
    {CardId::TheBomb, [] {
       CardData d = colorless("The Bomb", 2, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},
    {CardId::TheBombPlus, [] {
       CardData d = colorless("The Bomb+", 2, CardType::Skill);
       d.unplayable = true;
       return d;
     }()},

    // Thinking Ahead: "Draw 2 cards. Put a card from your hand on top of your
    // draw pile. Exhaust." The upgrade removes the Exhaust.
    //
    // Exactly Warcry's shape, which the engine already resolves in the right
    // order: the choice queues AFTER the draw, so a just-drawn card is a legal
    // option. sts_lightspeed implements it as literally DrawCards(2) followed
    // by Warcry's own action. An earlier comment here claimed this needed new
    // machinery; it does not.
    {CardId::ThinkingAhead, [] {
       CardData d = colorless("Thinking Ahead", 0, CardType::Skill);
       d.exhaust = true;
       d.draw = 2;
       d.requests_choice = ChoiceKind::HandToTopOfDraw;
       return d;
     }()},
    {CardId::ThinkingAheadPlus, [] {
       CardData d = colorless("Thinking Ahead+", 0, CardType::Skill);
       d.draw = 2;
       d.requests_choice = ChoiceKind::HandToTopOfDraw;
       return d;
     }()},

    // ============================================================ CURSES =====
    // All Unplayable by rule. Several act from INSIDE the hand, which is a
    // shape no Ironclad card has except Burn.

    // Clumsy: "Unplayable. Ethereal." No effect at all — it exhausts itself at
    // end of turn, so it clogs exactly one hand.
    {CardId::Clumsy, [] {
       CardData d = curse("Clumsy");
       d.ethereal = true;
       return d;
     }()},

    // Decay: "Unplayable. At the end of your turn, take 2 damage." Same field
    // and same self-discarding behaviour as Burn — the wiki notes it leaves the
    // hand at end of turn along with the damage, which is why it is in the
    // family that bypasses Runic Pyramid (relic batch 1b).
    {CardId::Decay, [] {
       CardData d = curse("Decay");
       d.end_of_turn_damage_in_hand = 2;
       return d;
     }()},

    // Injury: "Unplayable." Nothing else. The purest clog in the game.
    {CardId::Injury, [] { return curse("Injury"); }()},

    // Regret: "Unplayable. At the end of your turn, lose HP equal to the number
    // of cards in your hand." HP LOSS, so block does not absorb it — and the
    // count includes Regret itself.
    {CardId::Regret, [] {
       CardData d = curse("Regret");
       d.end_of_turn_hp_loss_per_card_in_hand = true;
       return d;
     }()},

    // Doubt / Shame: "Unplayable. At the end of your turn, gain 1 Weak
    // [Frail]." Mirror images. Both discard themselves as they fire, like Burn
    // and Decay.
    {CardId::Doubt, [] {
       CardData d = curse("Doubt");
       d.end_of_turn_self_debuff = Debuff::Weak;
       d.end_of_turn_self_debuff_amount = 1;
       return d;
     }()},
    {CardId::Shame, [] {
       CardData d = curse("Shame");
       d.end_of_turn_self_debuff = Debuff::Frail;
       d.end_of_turn_self_debuff_amount = 1;
       return d;
     }()},

    // Pain: "Unplayable. While in hand, lose 1 HP when other cards are played."
    // Fires BEFORE the played card resolves, and once per play — so a card
    // played twice costs 2 HP.
    {CardId::Pain, [] {
       CardData d = curse("Pain");
       d.hp_loss_in_hand_per_card_played = 1;
       return d;
     }()},

    // Writhe: "Unplayable. Innate." No effect beyond starting in your hand
    // every fight, which is the effect — it costs a card slot on turn 1.
    {CardId::Writhe, [] {
       CardData d = curse("Writhe");
       d.innate = true;
       return d;
     }()},

    // Normality: "Unplayable. While in hand, you cannot play more than 3 cards
    // this turn." A restriction on the MASK rather than an effect.
    {CardId::Normality, [] {
       CardData d = curse("Normality");
       d.cards_playable_cap_in_hand = 3;
       return d;
     }()},

    // Parasite: "Unplayable. If transformed or removed from your deck, lose 3
    // Max HP." Fires on removal, NOT on exhaust — Blue Candle exhausts it for
    // free, which is the intended out.
    {CardId::Parasite, [] {
       CardData d = curse("Parasite");
       d.max_hp_loss_on_removal = 3;
       return d;
     }()},

    // Curse of the Bell: "Unplayable. Cannot be removed from your deck." The
    // wiki adds that it cannot be TRANSFORMED either, which the in-game text
    // omits — so the flag covers both.
    {CardId::CurseOfTheBell, [] {
       CardData d = curse("Curse of the Bell");
       d.cannot_be_removed = true;
       return d;
     }()},
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

// --- Card descriptions (ROB-97) -------------------------------------------
//
// The rules text a player reads on the card, verbatim from slaythespire.wiki.gg.
// The TUI rendered name + cost only, which was survivable at 8 cards and is not
// at 154 — you cannot check Sentinel's on-exhaust energy or Corruption's
// skill-exhaust by playing if nothing on screen says they exist, and human play
// is how deck parity actually gets validated.
//
// STORED, not composed from CardData. Composing looks tempting until you meet
// the sentinels: `damage` is 0 for every card whose damage is computed
// (Body Slam, Searing Blow), `cost` is kXCost for Whirlwind, and `hits` is -1
// for X-hit cards. A generator would need a special case for each just to avoid
// printing "Deal 0 damage" or "Deal 5 damage -1 times". A stored string simply
// says what the card says.
//
// A SEPARATE map rather than a CardData field, because CardData has 42
// positional fields and most rows lean on trailing defaults — a 43rd field
// would mean respelling every default in all 189 rows, where one positional
// slip is silent.
//
// The numbers in every one of these strings were cross-checked against
// CARD_DATABASE (damage, block, status applications, draw, energy, hits,
// exhaust, innate). The wiki text came out of a page summariser that got
// several *costs* wrong, so it was verified rather than trusted.
inline const std::unordered_map<CardId, const char*> CARD_DESCRIPTIONS = {
#include "card_descriptions.inc"  // NOLINT — 154 rows, one per authored card
};

namespace detail {

// Replace the first "Deal N damage" figure in `base` with `damage`.
// Returns `base` unchanged if it has no such figure.
inline std::string with_damage(const std::string& base, int damage) {
  const std::string kNeedle = "Deal ";
  const std::size_t start = base.find(kNeedle);
  if (start == std::string::npos) return base;
  const std::size_t first = start + kNeedle.size();
  std::size_t last = first;
  while (last < base.size() && std::isdigit(static_cast<unsigned char>(base[last]))) ++last;
  if (last == first) return base;  // "Deal damage equal to your Block."
  return base.substr(0, first) + std::to_string(damage) + base.substr(last);
}

// Build the full table: the authored rows, plus one generated row per rung.
//
// Rung IDs (Rampage's growth ladder, Searing Blow's upgrade ladder) are NOT
// authored. Their text is the base card's with a single number swapped, so
// hand-writing 35 near-identical strings would be 35 chances to mistype a
// number CARD_DATABASE already holds. The base is found by walking CARD_GROWTH
// backwards rather than from a hand-listed rung->base table — same reasoning
// as ROB-87 deriving kObsCardOrder: a parallel list drifts silently.
inline std::unordered_map<CardId, std::string> build_card_descriptions() {
  std::unordered_map<CardId, std::string> out;
  for (const auto& [id, text] : CARD_DESCRIPTIONS) out.emplace(id, text);

  std::unordered_map<CardId, CardId> grew_from, upgraded_from;
  for (const auto& [from, to] : CARD_GROWTH) grew_from.emplace(to, from);
  for (const auto& [from, to] : CARD_UPGRADES) upgraded_from.emplace(to, from);

  // Follow the growth ladder back to its head (the card nothing grows into).
  const auto growth_head = [&grew_from](CardId id) {
    for (int guard = 0; guard < 64; ++guard) {
      const auto prev = grew_from.find(id);
      if (prev == grew_from.end()) break;
      id = prev->second;
    }
    return id;
  };

  for (const auto& [id, data] : CARD_DATABASE) {
    if (out.count(id)) continue;

    // Searing Blow's ladder is upgrades, not growth, so it is not in
    // CARD_GROWTH. Damage follows query.cc's rule: n(n+7)/2 + 12.
    if (const int n = searing_blow_rung(id); n >= 2) {
      const auto base = out.find(CardId::SearingBlow);
      if (base != out.end()) {
        out.emplace(id, with_damage(base->second, n * (n + 7) / 2 + 12));
      }
      continue;
    }

    // Rampage. Walking the growth ladder back finds the head for a rung grown
    // from an authored card — but a rung reached by UPGRADING a grown copy
    // (Rampage+ 13 comes from upgrading Rampage 13, not from growing Rampage+)
    // has no growth predecessor at all, and dead-ends on itself.
    //
    // For those, cross the upgrade edge first, find THAT card's head, and
    // upgrade it. Landing on the wrong head is not cosmetic: the two ladders
    // word their growth step differently ("by 5" vs "by 8"), so the base head
    // would state a number this card does not use.
    // Cross the upgrade edge FROM THE HEAD, not from `id` — Rampage+ 47 grew
    // from Rampage+ 39 and back down to Rampage+ 13, and it is that head, not
    // the rung we started at, which carries the upgrade edge to Rampage 13.
    CardId head = growth_head(id);
    if (!out.count(head)) {
      if (const auto up = upgraded_from.find(head); up != upgraded_from.end()) {
        const CardId pre = growth_head(up->second);
        if (out.count(pre)) head = upgraded_card(pre);
      }
    }
    if (const auto base = out.find(head); base != out.end()) {
      out.emplace(id, with_damage(base->second, data.damage));
    }
  }
  return out;
}

}  // namespace detail

// Rules text for a card. Empty string if the id has none — callers render what
// they get rather than branching, and CardDescriptionsCoverEveryCard makes the
// empty case a test failure rather than a blank panel someone notices in play.
//
// Built once on first call: the rung rows are generated, so this cannot be a
// constexpr table.
inline const std::string& card_description(CardId id) {
  static const std::unordered_map<CardId, std::string> table =
      detail::build_card_descriptions();
  static const std::string kNone;
  const auto it = table.find(id);
  return it == table.end() ? kNone : it->second;
}

}  // namespace minispire
