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
