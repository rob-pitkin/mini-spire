#ifndef MINISPIRE_RELIC_H
#define MINISPIRE_RELIC_H

#include <cstdint>
#include <vector>

namespace minispire {

// Every relic an Ironclad can hold in Act 1 at Ascension 0 — and ONLY those.
// See docs/design/v2-spec.md §5.1.
//
// 140, verified rather than assumed:
//
//   starter   1   Burning Blood
//   common   33
//   uncommon 30
//   rare     28
//   shop     17
//   boss     21   (22 minus Black Blood, below)
//   special  10   granted by specific Act 1 events
//
// Sourced from sts_lightspeed's Ironclad relic pools and cross-checked against
// its relicCanSpawn gating. Every floor gate there clears Act 1 comfortably —
// the tightest is Tiny Chest at floor <= 35 against Act 1's ~16 — and every
// conditional gate is satisfiable.
//
// Satisfiable is not the same as satisfied: a gate that reads the RUN can be
// false at the moment of a draw. Reading all 35 decompiled canSpawn overrides
// finds three such groups in Act 1: the Bottles (a card type in the deck — none
// can spawn on a starter deck), Courier / Maw Bank / Old Coin / Smiling Mask
// (never while the current room is a shop), and Girya / Peace Pipe / Shovel
// (fewer than two of those three held). RunState::relic_can_spawn applies the
// gates for all three groups (relic-effects.md §6.23, §6.24).
//
// ⚠️ BLACK BLOOD IS EXCLUDED, and the reason is an ordering, not a property.
// relicCanSpawn(BLACK_BLOOD) is `has(BURNING_BLOOD)`, which is TRUE for the
// Ironclad. It is unreachable only because Neow's boss-relic option applies its
// LOSE_STARTER_RELIC drawback *before* granting the bonus, removing Burning
// Blood first. Implement Neow bonus-first and Black Blood becomes reachable and
// this enum becomes 141. The ordering is load-bearing for the vocabulary.
//
// ⚠️ Not in Act 1, deliberately absent (no dead indices, §5.1 rule 5): all
// other-class relics, blights, Circlet / Red Circlet (awarded only when every
// relic is already collected), and the special relics granted by Act 2–3 events
// — Bloody Idol, Mutagenic Strength, Necronomicon, Nilry's Codex, Enchiridion,
// Red Mask, N'loth's Gift, Mark of the Bloom.
enum class RelicId {
  // --- Starter (1) ---
  BurningBlood,

  // --- Common (33) ---
  Whetstone, TheBoot, BloodVial, MealTicket, PenNib, Akabeko, Lantern,
  RegalPillow, BagOfPreparation, AncientTeaSet, SmilingMask, PotionBelt,
  PreservedInsect, Omamori, MawBank, ArtOfWar, ToyOrnithopter, CeramicFish,
  Vajra, CentennialPuzzle, Strawberry, HappyFlower, OddlySmoothStone, WarPaint,
  BronzeScales, JuzuBracelet, DreamCatcher, Nunchaku, TinyChest, Orichalcum,
  Anchor, BagOfMarbles, RedSkull,

  // --- Uncommon (30) ---
  BottledTornado, Sundial, Kunai, Pear, BlueCandle, EternalFeather, StrikeDummy,
  SingingBowl, Matryoshka, InkBottle, TheCourier, FrozenEgg, OrnamentalFan,
  BottledLightning, GremlinHorn, HornCleat, ToxicEgg, LetterOpener,
  QuestionCard, BottledFlame, Shuriken, MoltenEgg, MeatOnTheBone,
  DarkstonePeriapt, MummifiedHand, Pantograph, WhiteBeastStatue,
  MercuryHourglass, SelfFormingClay, PaperPhrog,

  // --- Rare (28) ---
  Ginger, OldCoin, BirdFacedUrn, UnceasingTop, Torii, StoneCalendar, Shovel,
  WingBoots, ThreadAndNeedle, Turnip, IceCream, Calipers, LizardTail,
  PrayerWheel, Girya, DeadBranch, DuVuDoll, Pocketwatch, Mango, IncenseBurner,
  GamblingChip, PeacePipe, CaptainsWheel, FossilizedHelix, TungstenRod,
  MagicFlower, CharonsAshes, ChampionBelt,

  // --- Shop (17) ---
  SlingOfCourage, HandDrill, Toolbox, ChemicalX, LeesWaffle, Orrery,
  DollysMirror, OrangePellets, PrismaticShard, ClockworkSouvenir, FrozenEye,
  TheAbacus, MedicalKit, Cauldron, StrangeSpoon, MembershipCard, Brimstone,

  // --- Boss (21) ---
  FusionHammer, VelvetChoker, RunicDome, SlaversCollar, SneckoEye, PandorasBox,
  CursedKey, BustedCrown, Ectoplasm, TinyHouse, Sozu, PhilosophersStone,
  Astrolabe, BlackStar, SacredBark, EmptyCage, RunicPyramid, CallingBell,
  CoffeeDripper, MarkOfPain, RunicCube,

  // --- Special: granted by specific Act 1 events (10) ---
  NeowsLament,       // Neow's tier-2 blessing
  GoldenIdol,        // Golden Idol event
  OddMushroom,       // Hypnotizing Colored Mushrooms
  WarpedTongs,       // Ominous Forge
  SpiritPoop,        // Bonfire Spirits
  FaceOfCleric,      // Face Trader
  SsserpentHead,     // Face Trader
  GremlinVisage,     // Face Trader
  NlothsHungryFace,  // Face Trader
  CultistHeadpiece,  // Face Trader
};

inline constexpr int kNumRelics = 140;

// Which pool a relic is drawn from. Shop and Boss are not "rarer" than Rare —
// they are separate pools with their own draw sites.
enum class RelicTier { Starter, Common, Uncommon, Rare, Shop, Boss, Special };

// Shop price by tier, from sts_lightspeed's relicTierPrices. Special-tier
// relics are never sold, so they have no price.
inline constexpr int kRelicTierPrices[] = {0, 150, 250, 300, 150, 999, 0};

RelicTier relic_tier(RelicId id);
const char* relic_name(RelicId id);

// The pools a random draw samples. Special relics are absent: they come from a
// named event, never from a roll.
const std::vector<RelicId>& relic_pool(RelicTier tier);

// One relic the player holds. `counter` is the number drawn on the relic icon —
// one run-scoped int per relic, loaded into combat and written back out, with
// nothing resetting at a fight boundary (§3.3). It also carries non-counter
// bookkeeping for relics that need it, such as Omamori's remaining charges.
struct HeldRelic {
  RelicId id;
  int counter = 0;
};

// Thresholds for the counter relics. They live here rather than in run_state.h
// because the counters are read inside COMBAT (action.cc), which does not see
// the run layer — and a relic's own threshold belongs with the relic anyway.
//
// Happy Flower fires every third player turn, counted ACROSS combats: a counter
// left at 2 fires on the first turn of the next fight (§3.3).
inline constexpr int kHappyFlowerTurns = 3;

// Incense Burner grants 1 Intangible every sixth player turn, counted across
// combats like Happy Flower's.
inline constexpr int kIncenseBurnerTurns = 6;

// Stone Calendar fires at the end of turn 7 ONLY — not every seventh turn.
inline constexpr int kStoneCalendarTurn = 7;
inline constexpr int kStoneCalendarDamage = 52;

// --- run-layer relic amounts (§3.2, §6.14) ---

// Maw Bank pays 12 gold on entering a room until the first time gold is spent,
// and then never again for the rest of the run. The latch is the counter, set
// to -2 — the value decompiled MawBank.setCounter treats as "used up", kept
// rather than a bool so the held record stays one int and the displayed number
// matches the game's.
inline constexpr int kMawBankGold = 12;
inline constexpr int kMawBankUsedUp = -2;

// Meal Ticket heals this much on entering a SHOP, and only a shop.
inline constexpr int kMealTicketHeal = 15;

// Ceramic Fish pays out every time a card joins the master deck.
inline constexpr int kCeramicFishGold = 9;

// Old Coin's one payout, the moment it is taken.
inline constexpr int kOldCoinGold = 300;

// Tiny House pays four ways at once: one random upgrade, this much Max HP, this
// much gold, and one potion. No card — see the note in obtain_relic.
inline constexpr int kTinyHouseMaxHp = 5;
inline constexpr int kTinyHouseGold = 50;

// Eternal Feather heals 3 for every 5 cards in the master deck on entering a
// Rest site — floor(deck / 5) * 3, integer division exactly as the decompiled
// source writes it (`masterDeck.size() / 5 * 3`). The wiki adds the timing: the
// heal lands on ENTERING, before any rest option is chosen.
inline constexpr int kEternalFeatherCardsPer = 5;
inline constexpr int kEternalFeatherHeal = 3;

// Tiny Chest turns every 4th `?` room into a Treasure room. The counter counts
// EVERY `?` entered, the forced chest included, and wraps to 0 when it fires
// (EventHelper.roll). It starts at 0 on pickup, which HeldRelic's default gives.
inline constexpr int kTinyChestRooms = 4;

// Matryoshka adds a relic to the next 2 chests: 75% Common, else Uncommon
// (decompiled onChestOpen, `relicRng.randomBoolean(0.75f)`). The counter starts
// at the charge count — set in obtain_relic, since HeldRelic defaults to 0 —
// and becomes -2 when spent, the value StS's setCounter treats as used up.
inline constexpr int kMatryoshkaCharges = 2;
inline constexpr int kMatryoshkaCommonChance = 75;
inline constexpr int kMatryoshkaUsedUp = -2;

// Ancient Tea Set: entering a Rest site arms it; the next combat's first turn
// gains 2 Energy and disarms it. It does not stack. StS encodes armed as -2 and
// idle as -1, neither of which is displayed (the relic glows instead); here the
// counter is a plain flag, 1 armed and 0 idle, so the default is idle.
// Singing Bowl: skip a card reward for this much Max HP instead, which also
// heals it (SingingBowlButton, `increaseMaxHp(2, true)`).
inline constexpr int kSingingBowlMaxHp = 2;

// Wing Boots: this many moves to ANY room on the next row. A charge is spent
// only by a move no edge allows; at zero the counter becomes -2, the used-up
// value StS's setCounter recognises (WingBoots, MapRoomNode).
inline constexpr int kWingBootsCharges = 3;
inline constexpr int kWingBootsUsedUp = -2;

// Cauldron brews this many potions onto a reward screen when it is taken
// (Cauldron.onEquip; potion-effects.md §6.2).
inline constexpr int kCauldronPotions = 5;

// Empty Cage removes, and Astrolabe transforms and upgrades, this many cards
// on pickup (relic-effects.md §6.30).
inline constexpr int kEmptyCageCards = 2;
inline constexpr int kAstrolabeCards = 3;

// Snecko Eye: draw this many more cards every turn, the opening hand included
// (onEquip, `masterHandSize += 2`) — and Confusion, whose roll on each drawn
// card is 0 to kConfusionMaxCost inclusive (`cardRandomRng.random(3)`).
inline constexpr int kSneckoEyeExtraDraw = 2;
inline constexpr int kConfusionMaxCost = 3;

// Girya, Peace Pipe and Shovel stop spawning once this many of the three are
// held (decompiled canSpawn, `campfireRelicCount < 2`).
inline constexpr int kMaxCampfireRelics = 2;

inline constexpr int kAncientTeaSetEnergy = 2;
inline constexpr int kAncientTeaSetArmed = 1;

// Chemical X adds 2 to the EFFECT of an X-cost card — its hits, or how many
// cards it makes — without changing the energy spent (wiki.gg). In StS the
// check sits inside each X card's own action, so a free play (Havoc) and a
// replay (Double Tap) each get their own +2.
inline constexpr int kChemicalXBonus = 2;

// Pocketwatch: play this many cards or FEWER in a turn and draw
// kPocketwatchDraw extra at the start of the next, after the normal draw. The
// counter is the cards played this turn, which the game displays on the relic.
inline constexpr int kPocketwatchMaxCards = 3;
inline constexpr int kPocketwatchDraw = 3;

// --- combat-layer relic amounts ---

// Magic Flower multiplies healing by 1.5 — but only DURING COMBAT
// (MagicFlower.onPlayerHeal checks RoomPhase.COMBAT; wiki.gg: "Healing is 50%
// more effective during combat"). It is a query modifier (§3.3), not a trigger.
//
// Rounded HALF UP: StS uses libGDX MathUtils.round, which is floor(x + 0.5).
// Every other rounding in this engine truncates, so this one is spelled out —
// a 3 heal becomes 5, not 4.
//
// Held as a RATIO and applied in integer arithmetic, not as a float:
// (n * 3 + 1) / 2 is exactly round-half-up for x1.5. ROB-100 is open because
// std::uniform_int_distribution already disagrees across standard libraries,
// and a value that must be identical on every platform has no business going
// through a float rounding path to get there.
inline constexpr int kMagicFlowerHealNumerator = 3;
inline constexpr int kMagicFlowerHealDenominator = 2;

// Red Skull: Strength while HP is at or below half (relic-effects.md §6.29).
inline constexpr int kRedSkullStrength = 3;

// Velvet Choker: no card can be played once this many have been this turn
// (VelvetChoker.canPlay, curse-effects.md §3.3).
inline constexpr int kVelvetChokerCards = 6;

// Blue Candle: HP lost when a curse is played under it (BlueCandle.onUseCard,
// curse-effects.md §3.6).
inline constexpr int kBlueCandleHpLoss = 1;

// Lizard Tail revives the player once per run (potion-effects.md §5.4). The
// counter's default 0 is unused; -2 is used, the value LizardTail.setCounter
// treats as used up. RunState carries the counter between fights like any
// other.
inline constexpr int kLizardTailUsedUp = -2;

// Charon's Ashes: 3 damage to ALL enemies whenever a card is exhausted.
// THORNS-type in StS — `createDamageMatrix(3, true)`, the same call Bird-Faced
// Urn's neighbour uses — so it is unscaled by Strength, Weak and Vulnerable and
// cannot trigger an enemy's Thorns. DamageAllEnemies already means exactly that
// here, so the property holds by construction rather than by remembering it.
inline constexpr int kCharonsAshesDamage = 3;

// Toy Ornithopter: heal 5 HP whenever a potion is used — in combat through the
// queue, after the potion's own effects (potion-effects.md §2).
inline constexpr int kToyOrnithopterHeal = 5;

// Champion Belt: 1 Weak alongside every Vulnerable the PLAYER applies to an
// enemy.
//
// Suppressed entirely when the target holds Artifact. StS guards on
// `!target.hasPower("Artifact")` BEFORE the charge is spent (ApplyPowerAction),
// so an Artifact-bearing enemy eats the Vulnerable and gets no Weak either —
// not "the Vulnerable is negated but the Weak lands".
inline constexpr int kChampionBeltWeak = 1;

// The card-play counter relics come in TWO kinds, and the difference is the
// whole of their design:
//
//   PER TURN  — "3 Attacks in a single turn". The counter resets every turn, so
//               three Attacks spread over three turns do nothing.
//   PERSISTENT — "every time you play 10 Attacks", with the wiki stating the
//               counter "is not reset between turns or combats". Progress
//               accumulates across the entire run.
//
// Both use HeldRelic::counter; the per-turn ones are cleared at each turn
// boundary and at combat start. Conflating them makes Kunai far too strong and
// Nunchaku nearly unreachable.
inline constexpr int kPerTurnCardRelicThreshold = 3;
inline constexpr int kPersistentCardRelicThreshold = 10;

// True for the relics whose card counter resets every turn.
inline bool relic_counter_is_per_turn(RelicId id) {
  return id == RelicId::Kunai || id == RelicId::Shuriken ||
         id == RelicId::OrnamentalFan || id == RelicId::LetterOpener;
}

}  // namespace minispire

#endif  // MINISPIRE_RELIC_H
